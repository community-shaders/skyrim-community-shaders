/*
* This file accompanies RaytracedGI.h
* Please refer to the header for more information.
*
* ProfJack
* 2025-06-28
*/

#include "RaytracedGI.h"
#include "InverseSquareLighting.h"

#include "Globals.h"
#include "State.h"
#include "RaytracedGI/ShaderUtils.h"
#include "ShaderCache.h"

#include <filesystem>
#include <shlobj.h>
#include <windows.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RaytracedGI::Settings,
	Enabled)

////////////////////////////////////////////////////////////////////////////////////

void RaytracedGI::RestoreDefaultSettings()
{
	settings = {};
}

void RaytracedGI::LoadSettings(json& o_json)
{
	settings = o_json;
}

void RaytracedGI::SaveSettings(json& o_json)
{
	o_json = settings;
}

void RaytracedGI::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Ray-Traced Global Illumination.");
	}

	if (settings.EnablePIXCapture)
	{
		if (ImGui::Button("Create PIX Capture")) {
			capture = true;
		}
	}

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Mesh Data (vertex, index and BLAS buffers): {}", meshVector.size()).c_str());
		ImGui::Text(std::format("Instances: {}", instances.size()).c_str());
		ImGui::Text(std::format("Lights: {}", lightData.size()).c_str());

		ImGui::TreePop();
	}

	D3D11_TEXTURE2D_DESC desc;
	diffuseGITexture->resource11->GetDesc(&desc);
	ImGui::Image(diffuseGITexture->srv, { desc.Width * 0.5f, desc.Height * 0.5f });
}

void RaytracedGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	handleIncrement = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Common heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC commonHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.NumDescriptors = 4096,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		};
		d3d12Device->CreateDescriptorHeap(&commonHeapDesc, IID_PPV_ARGS(&commonHeap));	
	}

	logger::debug("Creating textures...");
	{	
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC mainDesc;
		mainTex.texture->GetDesc(&mainDesc);

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		diffuseGITexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		specularGITexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());

		DX::ThrowIfFailed(diffuseGITexture->resource->SetName(L"Diffuse GI Texture"));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = texDesc.Format;

		auto cpuHandle = commonHeap->GetCPUDescriptorHandleForHeapStart();
		d3d12Device->CreateUnorderedAccessView(diffuseGITexture->resource.get(), nullptr, &uavDesc, cpuHandle);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, cheeseSampler.put()));
	}

	logger::debug("Creating framebuffer...");
	{
		auto frameBufferDesc = BASIC_BUFFER_DESC;
		frameBufferDesc.Width = (sizeof(FrameBuffer) + 255) & ~255;

		d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &frameBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frameBuffer));
		DX::ThrowIfFailed(frameBuffer->SetName(L"Frame Buffer"));
		frameBuffer->Map(0, nullptr, reinterpret_cast<void**>(&frameBufferData));	
	}

	//MAX_LIGHTS
	logger::debug("Creating structured buffers...");
	{
		lightBuffer = eastl::make_unique<StructuredBufferDX12<Light>>(d3d12Device.get(), MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->gpuBuffer->SetName(L"Light Buffer"));
		lightBuffer->CreateSRV(commonHeap.get(), handleIncrement * 2);
	}

	CompileShaders();
}

bool IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

eastl::vector<LightLimitFix::LightData> RaytracedGI::GetPointLights()
{
	eastl::vector<LightLimitFix::LightData> lightsData{};

	auto accumulator = *globals::game::currentAccumulator.get();
	const auto activeShadowSceneNode = accumulator->GetRuntimeData().activeShadowSceneNode;

	auto& isl = globals::features::inverseSquareLighting;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) 
	{
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightLimitFix::LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						light.color *= runtimeData.fade;
					}

					light.color *= bsLight->lodDimmer;

					if (!IsGlobalLight(bsLight)) {
						light.lightFlags.set(LightLimitFix::LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(shadowLightIndex, shadowLight);
						light.shadowMaskIndex = shadowLightIndex;
						light.lightFlags.set(LightLimitFix::LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						auto worldPos = niLight->world.translate;

						light.positionWS[0].data = float3(worldPos.x, worldPos.y, worldPos.z);

						if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	const auto& activeLights = activeShadowSceneNode->GetRuntimeData().activeLights;
	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = activeShadowSceneNode->GetRuntimeData().activeShadowLights;
	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	return lightsData;
}

void RaytracedGI::UpdateLights() 
{
	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& directionNi = dirLight->GetWorldDirection();
		auto direction = float3(directionNi.x, directionNi.y, directionNi.z);
		direction.Normalize();

		auto diffuse = dirLight->GetLightRuntimeData().diffuse;
		
		frameBufferData->DirectionalLight.Vector = -direction;
		frameBufferData->DirectionalLight.Color = float3(diffuse.red, diffuse.green, diffuse.blue); // * ( Util::IsInterior() ? 0.0f : 1.0f );
	}

	// Point lights
	/*{
		lightData.clear();
		lightData.reserve(MAX_LIGHTS);

		for (auto data : GetPointLights()) {
			if (lightData.size() >= MAX_LIGHTS)
				break;

			lightData.push_back({
				.Vector = data.positionWS[0].data,
				.Range = data.radius,
				.Color = data.color,
				.Pad = 0
			});
		}

		lightBuffer->Update(lightData.data(), lightData.size());
	}*/
}

DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
{
	DirectX::XMMATRIX temp;

	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][0],
										   m.entry[1][0],
										   m.entry[2][0],
										   0.0f),
		scale);

	temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][1],
										   m.entry[1][1],
										   m.entry[2][1],
										   0.0f),
		scale);

	temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][2],
										   m.entry[1][2],
										   m.entry[2][2],
										   0.0f),
		scale);

	temp.r[3] = DirectX::XMVectorSet(
		Transform.translate.x,
		Transform.translate.y,
		Transform.translate.z,
		1.0f);

	return temp;
}

void RaytracedGI::Main_RenderWorld(bool a1)
{
	if (Active()) {
		renderingWorld = true;
		CreateBuffers();			
	}

	Hooks::Main_RenderWorld::func(a1);

	if (Active()) {
		renderingWorld = false;
	}
}

template <typename T>
auto GetFlags(uint32_t value) {
	const auto& entries = magic_enum::enum_entries<T>();

	std::string flags;

	for (const auto& [flag, name] : entries) {
		if ((value & static_cast<uint32_t>(flag)) != 0) {
			flags += fmt::format("{} ", name);
		}
	}

	return flags;
};

RE::BSFadeNode* FindBSFadeNode(RE::NiNode* a_niNode)
{
	if (auto fadeNode = a_niNode->AsFadeNode()) {
		return fadeNode;
	}
	return a_niNode->parent ? FindBSFadeNode(a_niNode->parent) : nullptr;
}

template <typename T>
void RaytracedGI::MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res)
 {
	auto desc = BASIC_BUFFER_DESC;
	desc.Width = sizeof(T) * data.size();

	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));

	void* ptr;
	DX::ThrowIfFailed(res->Map(0, nullptr, &ptr));
	memcpy(ptr, data.data(), desc.Width);
	res->Unmap(0, nullptr);
}

inline std::wstring ToWide(const std::string& str)
{
	if (str.empty())
		return std::wstring();

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), &wstr[0], size_needed);
	return wstr;
}

float UInt8ToSNorm(uint8_t value) 
{
	return (static_cast<float>(value) / 255.0f) * 2.0f - 1.0f;
}

float UInt16ToUNorm(uint16_t value)
{
	return static_cast<float>(value) / USHRT_MAX;
}

void RaytracedGI::CreateBuffers()
{
	if (buffersCreated || creatingBuffers)
		return;

	creatingBuffers = true;

	meshVector.clear();
	meshMap.clear();
	instances.clear();
	instanceMap.clear();

	static auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];

	RE::BSVisit::TraverseScenegraphGeometries(shadowSceneNode, [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
		if (RE::BSTriShape* triShape = geometry->AsTriShape()) {

			// Ensure its Lighting shader, for now
			if (!triShape->lightingShaderProp_cast())
				return RE::BSVisit::BSVisitControl::kContinue;

			if (geometry->worldBound.radius == 0.0f)
				return RE::BSVisit::BSVisitControl::kContinue;

			auto& flags = triShape->GetFlags();
			uint32_t bsxFlagsValue = 0;

			if (flags.all(RE::NiAVObject::Flag::kRenderUse)) {
				if (auto fadeNode = FindBSFadeNode((RE::NiNode*)triShape)) {
					if (auto extraData = fadeNode->GetExtraData("BSX")) {
						auto bsxFlags = (RE::BSXFlags*)extraData;

						bsxFlagsValue = static_cast<uint32_t>(bsxFlags->value);

						if (bsxFlagsValue & (uint32_t)RE::BSXFlags::Flag::kEditorMarker)
							return RE::BSVisit::BSVisitControl::kContinue;
					}
				} else { // Else it crashes on Block stuff when reading indexes
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				/*const auto bsxFlagsStr = GetFlags<RE::BSXFlags::Flag>(bsxFlagsValue);
				logger::info(fmt::runtime("BSXFlags: {}"), bsxFlagsStr);

				const auto& geometryType = geometry->GetType().get();
				const auto geometryTypeName = magic_enum::enum_name(geometryType);

				logger::info(fmt::runtime("Geometry [0x{:x}]: {}, Type: {}"), reinterpret_cast<uintptr_t>(geometry), geometry->name.c_str(), geometryTypeName);

				const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flags.underlying());

				logger::info(fmt::runtime("Flags: {}"), flagsStr);*/

				const auto triShapeRuntime = triShape->GetTrishapeRuntimeData();

				uint vertexCount = triShapeRuntime.vertexCount;
				uint indexCount = triShapeRuntime.triangleCount * 3;

				//logger::info(fmt::runtime("Vertex Count: {}, Index Count: {}"), vertexCount, indexCount);

				RE::BSGraphics::TriShape* rendererData = triShape->GetGeometryRuntimeData().rendererData;
				//RE::BSGraphics::TriShape* rendererData = geometry->GetGeometryRuntimeData().rendererData;

				if (!rendererData)
					return RE::BSVisit::BSVisitControl::kContinue;

				auto vertexBufferDX11 = (ID3D11Buffer*)rendererData->vertexBuffer;
				auto indexBufferDX11 = (ID3D11Buffer*)rendererData->indexBuffer;

				auto key = TriBufferPtrKey(vertexBufferDX11, indexBufferDX11);

				winrt::com_ptr<ID3D12Resource> vertexBuffer = nullptr;
				winrt::com_ptr<ID3D12Resource> indexBuffer = nullptr;
				winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;

				if (auto it = meshMap.find(key); it != meshMap.end()) {
					size_t meshDataIndex = it->second;
					MeshData meshData = meshVector[meshDataIndex];

					vertexBuffer = meshData.vertexBuffer;
					indexBuffer = meshData.indexBuffer;
					blasBuffer = meshData.blasBuffer;
				}

				// Create instances before dropping out if buffers already exist
				{
					instances.emplace(
						triShape, 
						InstanceData(
							key, 
							GetXMFromNiTransform(triShape->world)
						)
					);
				}

				// Buffers already created
				if (vertexBuffer != nullptr && indexBuffer != nullptr) // I don't think it is possible for these to be different (eg. one instance doesn't use the same vertex and index buffers with the rest)
					return RE::BSVisit::BSVisitControl::kContinue;

				std::wstring geometryNameW = ToWide(geometry->name.c_str());

				//logger::info(fmt::runtime("Geometry [{}]"), geometry->name.c_str());

				// Create vertex buffer
				if (vertexBuffer == nullptr) // Check comment above
				{
					auto vertexDesc = rendererData->vertexDesc;

					auto vertexFlags = vertexDesc.GetFlags();
					uint32_t stride = vertexDesc.GetSize();

					uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
					uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
					uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
					uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);

					eastl::vector<Vertex> vertices;
					vertices.reserve(vertexCount);

					for (uint32_t i = 0; i < vertexCount; i++) {
						uint8_t* vtx = rendererData->rawVertexData + i * stride;

						Vertex vertexData{};

						if (vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX) {
							const float* pos = reinterpret_cast<const float*>(vtx + posOffset);
							vertexData.Position.x = pos[0];
							vertexData.Position.y = pos[1];
							vertexData.Position.z = pos[2];
							//vertexData.Position.w = pos[3];
						}

						if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
							const uint16_t* texcoord = reinterpret_cast<const uint16_t*>(vtx + uvOffset);

							vertexData.Texcoord[0] = texcoord[0];
							vertexData.Texcoord[1] = texcoord[1];
						}

						if (vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL) {
							const uint8_t* norm = reinterpret_cast<const uint8_t*>(vtx + normOffset);

							vertexData.Normal[0] = norm[0];
							vertexData.Normal[1] = norm[1];
							vertexData.Normal[2] = norm[2];
							vertexData.Normal[3] = norm[3];
						}

						if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
							const uint8_t* col = reinterpret_cast<const uint8_t*>(vtx + colorOffset);

							vertexData.Color[0] = col[0];
							vertexData.Color[1] = col[1];
							vertexData.Color[2] = col[2];
							vertexData.Color[3] = col[3];
						}

						/*logger::info(fmt::runtime("Vertex {} - Position [{}, {}, {}], Texcoord [{}, {}], Normal [{}, {}, {}], Color [{}, {}, {}]"), i, 
							vertexData.Position.x, vertexData.Position.y, vertexData.Position.z, 
							vertexData.Texcoord[0], vertexData.Texcoord[1],
							vertexData.Normal.x, vertexData.Normal.y, vertexData.Normal.z,
							vertexData.Color[0], vertexData.Color[1], vertexData.Color[2], vertexData.Color[3]);*/

						vertices.push_back(eastl::move(vertexData));
					}
					
					MakeAndCopy(vertices, vertexBuffer);
					DX::ThrowIfFailed(vertexBuffer->SetName(std::format(L"Vertex Buffer [{}] - {}", meshVector.size() , geometryNameW).c_str()));

					//DX::ThrowIfFailed(vertexBuffer->SetName((LPCWSTR)std::format("Vertex Buffer - {}", geometry->name.c_str()).c_str()));
				}

				// Create indices buffer
				if (indexBuffer == nullptr) // Ditto
				{
					eastl::vector<uint32_t> indices(indexCount);

					eastl::transform(rendererData->rawIndexData,
						rendererData->rawIndexData + indexCount,
						indices.begin(),
						[](uint16_t idx) { return static_cast<uint32_t>(idx); });

					MakeAndCopy(indices, indexBuffer);

					std::wstring label = std::format(L"Index Buffer [{}] - {}", meshVector.size(), geometryNameW);
					DX::ThrowIfFailed(indexBuffer->SetName(label.c_str()));

					//DX::ThrowIfFailed(indexBuffer->SetName((LPCWSTR)std::format("Index Buffer - {}", geometry->name.c_str()).c_str()));
				}

				// Create BLAS
				if (blasBuffer == nullptr) 
				{
					blasBuffer.attach(MakeBLAS(vertexBuffer.get(), vertexCount, indexBuffer.get(), indexCount));
				}

				// Emplace buffers
				{
					auto [ it, inserted ] = meshMap.emplace(key, meshVector.size());

					if (inserted) 
					{
						meshVector.emplace_back(vertexCount, indexCount, std::move(vertexBuffer), std::move(indexBuffer), std::move(blasBuffer));
					}
				}
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	

	// This probably shoudn't go here
	// Create instance buffer for BLAS
	{
		instanceMap.resize(instances.size());

		auto instancesDesc = BASIC_BUFFER_DESC;
		instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances.size();
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instanceBuffer)));
		DX::ThrowIfFailed(instanceBuffer->SetName(L"Instance Buffer"));
		instanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

		size_t instanceID = 0;
		for (auto& [ triShape, data ] : instances) {
			const auto& key = data.triBufferPtrKey;

			auto& meshIndex = meshMap[key];
			MeshData& meshData = meshVector[meshIndex];

			instanceData[instanceID] = {
				.InstanceID = static_cast<uint>(instanceID),
				.InstanceMask = 1,
				.AccelerationStructure = meshData.blasBuffer->GetGPUVirtualAddress()		
			};

			auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&instanceData[instanceID].Transform);
			XMStoreFloat3x4(ptr, data.transform);

			instanceMap[instanceID] = Instance(static_cast<uint>(meshIndex));
			instanceID++;
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(commonHeap->GetCPUDescriptorHandleForHeapStart());
	handle.Offset(3, handleIncrement);

	// Create instance buffer
	{
		MakeAndCopy(instanceMap, instanceMapBuffer);

		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements = static_cast<int32_t>(instanceMap.size());
		desc.Buffer.StructureByteStride = sizeof(Instance);
		desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		d3d12Device->CreateShaderResourceView(instanceMapBuffer.get(), &desc, handle);
		handle.Offset(1, handleIncrement);
	}

	// Create SRVs
	{
		// Vertex (Structured buffer)
		for (const auto& meshData : meshVector)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = static_cast<int32_t>(meshData.vertexCount);
			vbDesc.Buffer.StructureByteStride = sizeof(Vertex);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(meshData.vertexBuffer.get(), &vbDesc, handle);
			handle.Offset(1, handleIncrement);
		}

		// Index (Structured buffer)
		for (const auto& meshData : meshVector)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
			ibDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			ibDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ibDesc.Format = DXGI_FORMAT_UNKNOWN;
			ibDesc.Buffer.FirstElement = 0;
			ibDesc.Buffer.NumElements = static_cast<int32_t>(meshData.indexCount / 3);
			ibDesc.Buffer.StructureByteStride = sizeof(uint) * 3;
			ibDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(meshData.indexBuffer.get(), &ibDesc, handle);
			handle.Offset(1, handleIncrement);	
		}
	}

	// Create TLAS
	{
		UINT64 updateScratchSize;
		tlas.attach(MakeTLAS(instanceBuffer.get(), static_cast<uint>(instances.size()), &updateScratchSize));

		auto desc = BASIC_BUFFER_DESC;
		// WARP bug workaround: use 8 if the required size was reported as less
		desc.Width = std::max(updateScratchSize, 8ULL);
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));	
		DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch buffer"));
		
		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		CD3DX12_CPU_DESCRIPTOR_HANDLE tlasHandle(commonHeap->GetCPUDescriptorHandleForHeapStart(), 1, handleIncrement);
		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, tlasHandle);
	}

	buffersCreated = true;
	creatingBuffers = false;
}

void RaytracedGI::BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
{
	const auto shader = pass->shader;
	const auto shaderType = shader->shaderType.get();

	if (shaderType != RE::BSShader::Type::Lighting)
		return;

	const auto geometry = pass->geometry;

	const auto triShape = geometry->AsTriShape();

	if (triShape == nullptr)
		return;

	const auto& type = geometry->GetType().get();
	const auto typeName = magic_enum::enum_name(type);

	const auto& flagsUnd = geometry->GetFlags().underlying();
	const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flagsUnd);

	// NiAVObject::Flag
	logger::info(fmt::runtime("Geometry [0x{:x}]: {}"), reinterpret_cast<uintptr_t>(geometry), geometry->name.c_str());
	logger::info(fmt::runtime("Flags: {}"), flagsStr);

	logger::info(
		fmt::runtime("Pass: [0x{:x}], Alpha Test: {}, Render Flags: [0x{:x}], TriShape: [0x{:x}], Type: {}"),
		reinterpret_cast<uintptr_t>(pass),
		alphaTest,
		renderFlags,
		reinterpret_cast<uintptr_t>(triShape),
		typeName);

	if (technique)
		return;

	//int32_t technique = 0x3F & (vertexDescriptor >> 24);
	//logger::info(fmt::runtime("LightingShaderTechniques: {}"), GetFlags<SIE::ShaderCache::LightingShaderTechniques>(technique));
}

void RaytracedGI::BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	const auto shaderType = This->shaderType.get();

	if (!loaded || !settings.Enabled)
		return;

	if (!renderingWorld)
		return;

	const auto geometry = Pass->geometry;
	const auto triShape = geometry->AsTriShape();

	if (triShape == nullptr)
		return;

	auto& type = geometry->GetType();
	auto typeName = magic_enum::enum_name(type.get());

	auto vertexDescriptor = globals::state->modifiedVertexDescriptor;
	auto pixelDescriptor = globals::state->currentPixelDescriptor;
	//auto modifiedPixelDescriptor = globals::state->modifiedPixelDescriptor;

	logger::info(
		"BSShader_SetupGeometry - Pass: [0x{:x}], Flags: [0x{:x}] Shader [0x{:x}]: {}, Geometry [0x{:x}]: {}, TriShape: [0x{:x}], Type: {}",
		reinterpret_cast<uintptr_t>(Pass),
		RenderFlags,
		reinterpret_cast<uintptr_t>(This),
		magic_enum::enum_name(shaderType),
		reinterpret_cast<uintptr_t>(geometry),
		geometry->name.c_str(),
		reinterpret_cast<uintptr_t>(triShape),
		typeName);

	uint32_t technique = 0x3F & (vertexDescriptor >> 24);

	logger::info(fmt::runtime("LightingShaderTechniques: {}"), GetFlags<SIE::ShaderCache::LightingShaderTechniques>(technique));
	logger::info(fmt::runtime("LightingShaderFlags: {}"), GetFlags<SIE::ShaderCache::LightingShaderFlags>(pixelDescriptor));
}

void RaytracedGI::DrawRTGI()
{
	if (!d3d11Context)
	{
		logger::error("d3d11Context is nullptr");
	}

	if (!d3d11Fence)
	{
		logger::error("d3d11Fence is nullptr");
	}

	// Wait for D3D11 to finish
	{
		d3d11Context->Flush();
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;
	}

	// New frame, reset
	{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	bool captureStarted = false;

	if (capture) {
		captureStarted = true;
		ga->BeginCapture();
	}

	// Update framebuffer
	{
		frameBufferData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameBufferData->ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();
		frameBufferData->Position = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameBufferData->FrameCount = globals::state->frameCount;
	}

	// Upload to GPU
	{
		UpdateLights();
		//lightBuffer->Upload(commandList.get());
	}

	// Update scene
	{
		//UpdateTransforms();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {
			.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.Inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
				.NumDescs = static_cast<uint>(instances.size()),
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = instanceBuffer->GetGPUVirtualAddress() 
			},
			.SourceAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.ScratchAccelerationStructureData = tlasUpdateScratch->GetGPUVirtualAddress(),
		};
		commandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

		D3D12_RESOURCE_BARRIER tlasBarrier = { 
			.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
			.UAV = { 
				.pResource = tlas.get() 
			} 
		};

		commandList->ResourceBarrier(1, &tlasBarrier);	
	}

	{
		commandList->SetPipelineState1(pipelineRT.get());
		commandList->SetComputeRootSignature(rootSignature.get());
		auto commonHeapPtr = commonHeap.get();
		commandList->SetDescriptorHeaps(1, &commonHeapPtr);
		auto heapStart = commonHeap->GetGPUDescriptorHandleForHeapStart();

		// Parameter 0: UAV table
		commandList->SetComputeRootDescriptorTable(0, heapStart);

		// Parameter 1: Fixed SRVs (Scene + Lights + Index) - offset 1
		D3D12_GPU_DESCRIPTOR_HANDLE fixedSrvHandle = heapStart;
		fixedSrvHandle.ptr += handleIncrement * 1;
		commandList->SetComputeRootDescriptorTable(1, fixedSrvHandle);

		// Parameter 2: Vertex buffers - offset 4 (after UAV + Scene + Lights)
		D3D12_GPU_DESCRIPTOR_HANDLE vbHandle = heapStart;
		vbHandle.ptr += handleIncrement * 4;
		commandList->SetComputeRootDescriptorTable(2, vbHandle);

		// Parameter 3: Index buffers - offset 4 + mesh count
		D3D12_GPU_DESCRIPTOR_HANDLE ibHandle = heapStart;
		ibHandle.ptr += handleIncrement * (4 + meshVector.size());
		commandList->SetComputeRootDescriptorTable(3, ibHandle);

		// Parameter 4: Constant buffer
		commandList->SetComputeRootConstantBufferView(4, frameBuffer->GetGPUVirtualAddress());

		auto rtDesc = diffuseGITexture->resource->GetDesc();

		D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
			.RayGenerationShaderRecord = {
				.StartAddress = shaderIDs->GetGPUVirtualAddress(),
				.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
			},
			.MissShaderTable = { 
				.StartAddress = shaderIDs->GetGPUVirtualAddress() + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
				.SizeInBytes = 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
			},
			.HitGroupTable = { 
				.StartAddress = shaderIDs->GetGPUVirtualAddress() + 3 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
				.SizeInBytes = 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
			},
			.Width = static_cast<UINT>(rtDesc.Width),
			.Height = rtDesc.Height,
			.Depth = 1
		};

		auto barrier = [&](auto* resource, auto before, auto after) {
			D3D12_RESOURCE_BARRIER rb = {
				.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
				.Transition = {
					.pResource = resource,
					.StateBefore = before,
					.StateAfter = after },
			};
			commandList->ResourceBarrier(1, &rb);
		};

		barrier(diffuseGITexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		commandList->DispatchRays(&dispatchDesc);

		barrier(diffuseGITexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

		commandList->Close();

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish
	{
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		if (capture && captureStarted) {
			ga->EndCapture();
			capture = false;
		}

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	auto main = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	d3d11Context->CopyResource(main.texture, diffuseGITexture->resource11);
}

ID3D12Resource* RaytracedGI::MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize)
{
	auto makeBuffer = [&](UINT64 size, auto initialState) {
		auto desc = BASIC_BUFFER_DESC;
		desc.Width = size;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ID3D12Resource* buffer;
		d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer));
		return buffer;
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
	if (updateScratchSize)
		*updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

	auto* scratch = makeBuffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_COMMON);
	auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = as->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress()
	};

	commandAllocator->Reset();

	commandList->Reset(commandAllocator.get(), nullptr);
	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
	commandList->Close();

	ID3D12CommandList* commandListPtr = commandList.get();
	commandQueue->ExecuteCommandLists(1, &commandListPtr);

	// Flush
	{
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue++, event));
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}

	scratch->Release();
	return as;
}

ID3D12Resource* RaytracedGI::MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,

		.Triangles = {
			.Transform3x4 = 0,

			.IndexFormat = DXGI_FORMAT_R32_UINT,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = indices,
			.VertexCount = vertices,
			.IndexBuffer = indexBuffer->GetGPUVirtualAddress(),
			.VertexBuffer = { 
				.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
				.StrideInBytes = sizeof(Vertex) 
			} 
		}
	};

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometryDesc
	};

	return MakeAccelerationStructure(inputs);
}

ID3D12Resource* RaytracedGI::MakeTLAS(ID3D12Resource* localInstances, UINT numInstances, UINT64* updateScratchSize)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
		.NumDescs = numInstances,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = localInstances->GetGPUVirtualAddress()
	};

	return MakeAccelerationStructure(inputs, updateScratchSize);
}

void RaytracedGI::Flush()
{
	static UINT64 value = 1;
	commandQueue->Signal(d3d12Fence.get(), value);
	d3d12Fence->SetEventOnCompletion(value++, nullptr);
}


void RaytracedGI::PostPostLoad()
{
	Hooks::Install();
	Initialize();

	MenuOpenCloseEventHandler::Register();
}

static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath)) {
		if (directory_entry.is_directory()) {
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str()) {
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty()) {
		// TODO: Error, no PIX installation found
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

void RaytracedGI::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* ppImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			LoadLibrary(GetLatestWinPixGpuCapturerPath().c_str());
		}
	}

	logger::info("[RTGI] Creating D3D12 device");

	// Set Device
	DX::ThrowIfFailed(ppDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(ppImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create debug device
	/*{
		winrt::com_ptr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();

			winrt::com_ptr<ID3D12Debug1> debugController1;
			if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)))) {
				debugController1->SetEnableGPUBasedValidation(TRUE);
			}
		}
	}*/

	if (settings.EnablePIXCapture) {
		DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
	}

	// Create Device
	{
		DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device)));

		// Check hardware raytracing tier
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
			if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
				if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
					logger::info("Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
				else
					logger::warn("Hardware ray tracing not supported.");
			}		
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));
		//DX::ThrowIfFailed(commandList->Close());
	}

	// Create Interop
	{
		HANDLE sharedFenceHandle;
		DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
		DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
		DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
		CloseHandle(sharedFenceHandle);
	}
}

void RaytracedGI::CreateRootSignature()
{
	if (rootSignature)
		return;

	// UAV range
	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	// Fixed SRV ranges (Scene + Lights + Index map)
	CD3DX12_DESCRIPTOR_RANGE1 fixedSrvRanges[3];
	fixedSrvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);  // Scene
	fixedSrvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);  // Lights
	fixedSrvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);  // Index map

	// Vertex buffers (unbounded)
	CD3DX12_DESCRIPTOR_RANGE1 vertexBufferRange;
	vertexBufferRange.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		UINT_MAX,  // Unbounded
		0,         // t0
		1,         // space1
		D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	// Index buffers (unbounded)
	CD3DX12_DESCRIPTOR_RANGE1 indexBufferRange;
	indexBufferRange.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		UINT_MAX,  // Unbounded
		0,         // t0
		2,         // space2
		D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	// Root parameters
	CD3DX12_ROOT_PARAMETER1 params[5];
	params[0].InitAsDescriptorTable(1, &uavRange);           // Parameter 0: UAV
	params[1].InitAsDescriptorTable(3, fixedSrvRanges);      // Parameter 1: Scene + Lights + Index map
	params[2].InitAsDescriptorTable(1, &vertexBufferRange);  // Parameter 2: Vertex buffers
	params[3].InitAsDescriptorTable(1, &indexBufferRange);   // Parameter 3: Index buffers
	params[4].InitAsConstantBufferView(0, 0);                // Parameter 4: CBV

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		_countof(params),
		params,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			OutputDebugStringA((char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
}

void RaytracedGI::Initialize()
{
	
}


void RaytracedGI::ClearShaderCache()
{
	cheeseCs = nullptr;  // This is actually optional
	CompileShaders();
}

void RaytracedGI::CompileShaders()
{
	CreateRootSignature();
	CompileRaytracingShaders();
	CompileComputeShaders();
}

void RaytracedGI::CompileRaytracingShaders()
{
	winrt::com_ptr<IDxcBlob> rayGenBlob, missBlob, shadowMissBlob, closestHitBlob;

	ShaderUtils::CompileShader(rayGenBlob, L"Data/Shaders/RaytracedGI/Raytracing/RayGeneration.hlsl", L"lib_6_3");
	ShaderUtils::CompileShader(missBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl", L"lib_6_3");
	ShaderUtils::CompileShader(shadowMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/ShadowMiss.hlsl", L"lib_6_3");
	ShaderUtils::CompileShader(closestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl", L"lib_6_3");

	// Init pipeline
	{
		std::vector<D3D12_STATE_SUBOBJECT> subobjects;
		//subobjects.reserve(8);

		// Ray generation shader
		D3D12_EXPORT_DESC rayGenExportDesc = {};
		rayGenExportDesc.Name = L"RayGeneration";  // Pipeline-visible name
		rayGenExportDesc.ExportToRename = L"main";  // original HLSL function
		rayGenExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC rayGenLibDesc = {};
		rayGenLibDesc.DXILLibrary.pShaderBytecode = rayGenBlob->GetBufferPointer();
		rayGenLibDesc.DXILLibrary.BytecodeLength = rayGenBlob->GetBufferSize();
		rayGenLibDesc.NumExports = 1;
		rayGenLibDesc.pExports = &rayGenExportDesc;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &rayGenLibDesc });

		// Miss shader
		D3D12_EXPORT_DESC missExportDesc = {};
		missExportDesc.Name = L"Miss";
		missExportDesc.ExportToRename = L"main";
		missExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC missLibDesc = {};
		missLibDesc.DXILLibrary.pShaderBytecode = missBlob->GetBufferPointer();
		missLibDesc.DXILLibrary.BytecodeLength = missBlob->GetBufferSize();
		missLibDesc.NumExports = 1;
		missLibDesc.pExports = &missExportDesc;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &missLibDesc });

		// Shadow miss shader
		D3D12_EXPORT_DESC shadowMissExptDesc = {};
		shadowMissExptDesc.Name = L"ShadowMiss";
		shadowMissExptDesc.ExportToRename = L"main";
		shadowMissExptDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC shadowMissLibDesc = {};
		shadowMissLibDesc.DXILLibrary.pShaderBytecode = shadowMissBlob->GetBufferPointer();
		shadowMissLibDesc.DXILLibrary.BytecodeLength = shadowMissBlob->GetBufferSize();
		shadowMissLibDesc.NumExports = 1;
		shadowMissLibDesc.pExports = &shadowMissExptDesc;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &shadowMissLibDesc });

		// Associates shadow miss shader with shadow ray type
		/*static const wchar_t* shadowMissExports[] = { shadowMissExptDesc.Name };

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shadowMissAssoc = {};
		shadowMissAssoc.NumExports = 1;
		shadowMissAssoc.pExports = shadowMissExports;
		shadowMissAssoc.pSubobjectToAssociate = &subobjects.back();

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, .pDesc = &shadowMissAssoc });*/

		// Closest Hit shader
		D3D12_EXPORT_DESC closestHitExpDesc = {};
		closestHitExpDesc.Name = L"ClosestHit";
		closestHitExpDesc.ExportToRename = L"main";
		closestHitExpDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC closestHitLibDesc = {};
		closestHitLibDesc.DXILLibrary.pShaderBytecode = closestHitBlob->GetBufferPointer();
		closestHitLibDesc.DXILLibrary.BytecodeLength = closestHitBlob->GetBufferSize();
		closestHitLibDesc.NumExports = 1;
		closestHitLibDesc.pExports = &closestHitExpDesc;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &closestHitLibDesc });

		// Common hit Group subobject
		D3D12_HIT_GROUP_DESC hitGroupDesc = {};
		hitGroupDesc.ClosestHitShaderImport = closestHitExpDesc.Name;
		hitGroupDesc.HitGroupExport = L"HitGroup";
		hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupDesc });

		// Shadow hit group subobject
		D3D12_HIT_GROUP_DESC shadowHitGroupDesc = {};
		shadowHitGroupDesc.HitGroupExport = L"ShadowHitGroup";
		shadowHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &shadowHitGroupDesc });

		// Associates shadow hit group with shadow ray type
		/*static const wchar_t* shadowHitExports[] = { shadowHitGroupDesc.HitGroupExport };

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shadowHitAssoc = {};
		shadowHitAssoc.NumExports = 1;
		shadowHitAssoc.pExports = shadowHitExports;
		shadowHitAssoc.pSubobjectToAssociate = &subobjects.back();

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, .pDesc = &shadowHitAssoc });*/

		// Shader config
		D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
			.MaxPayloadSizeInBytes = 16,
			.MaxAttributeSizeInBytes = 8,
		};
		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg });

		// Global root signature
		D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { rootSignature.get() };
		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig });

		// RT pipeline config
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 4 };
		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg });

		// Final state description
		D3D12_STATE_OBJECT_DESC desc = { 
			.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
			.NumSubobjects = (UINT)subobjects.size(),
			.pSubobjects = subobjects.data() 
		};

		HRESULT hr = d3d12Device->CreateStateObject(&desc, IID_PPV_ARGS(&pipelineRT));

		if (FAILED(hr)) {
			logger::error("CreateStateObject failed: {}", hr);
		}

		DX::ThrowIfFailed(hr);
	}

	// Init shader tables
	{
		auto idDesc = BASIC_BUFFER_DESC;
		idDesc.Width = NUM_SHADER_IDS * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &idDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shaderIDs)));
		DX::ThrowIfFailed(shaderIDs->SetName(L"Shader IDs"));

		ID3D12StateObjectProperties* props;
		pipelineRT->QueryInterface(&props);

		void* data;
		auto writeId = [&](const wchar_t* name) {
			void* id = props->GetShaderIdentifier(name);
			memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			data = static_cast<char*>(data) + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		};

		DX::ThrowIfFailed(shaderIDs->Map(0, nullptr, &data));
		writeId(L"RayGeneration");
		writeId(L"Miss");
		writeId(L"ShadowMiss");
		writeId(L"HitGroup");
		writeId(L"ShadowHitGroup");
		shaderIDs->Unmap(0, nullptr);

		props->Release();	
	}
}

void RaytracedGI::CompileComputeShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\RaytracedGI\\nonexistent.cs.hlsl", { { "SOME_MACRO", "0" } }, "cs_5_0")); rawPtr)
		cheeseCs.attach(rawPtr);
}

RE::BSEventNotifyControl RaytracedGI::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a new cell through a loadscreen, update every frame until completion
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		if (!a_event->opening)
			globals::features::raytracedGI.buffersCreated = false;
	}

	return RE::BSEventNotifyControl::kContinue;
}
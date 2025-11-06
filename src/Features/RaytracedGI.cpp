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
		ImGui::Text(std::format("Meshes (vertex, index and BLAS buffers): {}", meshes.size()).c_str());
		ImGui::Text(std::format("Shared Textures: {}", sharedTextures.size()).c_str());

		ImGui::Text(std::format("Instances: {}", instances.size()).c_str());
		ImGui::Text(std::format("Lights: {}", lights.size()).c_str());

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

	commonHeap = eastl::make_unique<DX12::DescriptorHeap<CommonHeap>>(
		d3d12Device.get(), 
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_DESCRIPTORS, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	// u0 - Output texture
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

		d3d12Device->CreateUnorderedAccessView(diffuseGITexture->resource.get(), nullptr, &uavDesc, commonHeap->CPUHandle(OutputUAV));
		
	}

	// t1 - Light buffer
	{
		lightBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Light>>(d3d12Device.get(), MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->buffer->SetName(L"Light Buffer"));

		lightBuffer->CreateSRV(commonHeap->CPUHandle(Lights));
	}

	// t2 - Instance buffer
	{
		instanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Instance>>(d3d12Device.get(), MAX_INSTANCES);
		instanceBuffer->UpdateList(instanceData.data(), instanceData.size());

		instanceBuffer->CreateSRV(commonHeap->CPUHandle(Instances));
	}

	// Create instance buffer for BLAS
	{
		auto instancesDesc = BASIC_BUFFER_DESC;
		instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * MAX_INSTANCES;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&blasInstanceBuffer)));
		DX::ThrowIfFailed(blasInstanceBuffer->SetName(L"Instance Buffer"));

		blasInstances = new D3D12_RAYTRACING_INSTANCE_DESC[MAX_INSTANCES];
		blasInstanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&blasInstances));
	}

	logger::debug("Creating framebuffer...");
	{
		auto frameBufferDesc = BASIC_BUFFER_DESC;
		frameBufferDesc.Width = (sizeof(FrameBuffer) + 255) & ~255;

		d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &frameBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frameBuffer));
		DX::ThrowIfFailed(frameBuffer->SetName(L"Frame Buffer"));
		frameBuffer->Map(0, nullptr, reinterpret_cast<void**>(&frameBufferData));
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

	logger::debug("Creating irradiance cache buffer...");
	{
		irradianceCacheBuffer = eastl::make_unique<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>>(d3d12Device.get(), MAX_IRRADIANCE_ENTRIES);
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

					// pow(abs(color), 1.6)

					LightLimitFix::LightData light{};
					light.color = { pow(abs(runtimeData.diffuse.red), 1.6f), pow(abs(runtimeData.diffuse.green), 1.6f), pow(abs(runtimeData.diffuse.blue), 1.6f) };
					light.lightFlags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						light.color *= runtimeData.fade;
					}

					//light.color *= bsLight->lodDimmer;

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
	if (!renderingWorld || lightsUpdated)
		return;

	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& directionNi = dirLight->GetWorldDirection();
		auto direction = float3(directionNi.x, directionNi.y, directionNi.z);
		direction.Normalize();

		auto diffuse = dirLight->GetLightRuntimeData().diffuse;

		frameBufferData->DirectionalLight.Vector = -direction;
		frameBufferData->DirectionalLight.Color = float3(pow(abs(diffuse.red), 1.6f), pow(abs(diffuse.green), 1.6f), pow(abs(diffuse.blue), 1.6f));  // * ( Util::IsInterior() ? 0.0f : 1.0f );
	}

	// Point lights
	{
		lights.clear();
		lights.reserve(MAX_LIGHTS);

		for (auto data : GetPointLights()) {
			if (lights.size() >= MAX_LIGHTS)
				break;

			lights.emplace_back(data.positionWS[0].data, data.radius, data.color, 0);
		}

		lightBuffer->UpdateList(lights.data(), lights.size());
	}

	lightsUpdated = true;
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
		lightsUpdated = false;
		//CreateBuffers();
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

void RaytracedGI::AddInstance(RE::BSTriShape* pTriShape)
{
	RE::BSGeometry* pGeometry = pTriShape->AsGeometry();

	// Ensure its Lighting shader, for now
	if (!pTriShape->lightingShaderProp_cast())
		return;

	if (pGeometry->worldBound.radius == 0.0f)
		return;

	if (pTriShape->GetFlags().all(RE::NiAVObject::Flag::kRenderUse)) {
		if (auto fadeNode = FindBSFadeNode((RE::NiNode*)pTriShape)) {
			if (auto extraData = fadeNode->GetExtraData("BSX")) {
				auto bsxFlags = (RE::BSXFlags*)extraData;

				if (static_cast<uint32_t>(bsxFlags->value) & (uint32_t)RE::BSXFlags::Flag::kEditorMarker)
					return;
			}
		} else {  // Else it crashes on Block stuff when reading indexes
			return;
		}
	} else {
		return;
	}

	RE::BSGraphics::TriShape* rendererData = pTriShape->GetGeometryRuntimeData().rendererData;

	if (!rendererData)
		return;

	// Our beloved key, this could mess things up if the engine uses the same vertexBuffer for another, but based on Brixelizer it doesn't...
	auto vertexBufferDX11 = (ID3D11Buffer*)rendererData->vertexBuffer;

	auto meshIt = meshes.find(vertexBufferDX11);

	// Mesh doesn't exist yet
	if (meshIt == meshes.end()) 
	{
		const auto triShapeRuntime = pTriShape->GetTrishapeRuntimeData();
		uint vertexCount = triShapeRuntime.vertexCount;
		uint triangleCount = triShapeRuntime.triangleCount;
		uint indexCount = triangleCount * 3;

		logger::info("[RTGI] AddInstance - {}, Vertex Count: {}, Triangle Count: {}", pTriShape->name, vertexCount, triangleCount);

		std::wstring geometryNameW = ToWide(pGeometry->name.c_str());

		winrt::com_ptr<ID3D12Resource> vertexBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> indexBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;

		// Create vertex buffer
		{
			auto vertexDesc = rendererData->vertexDesc;

			auto vertexFlags = vertexDesc.GetFlags();
			uint32_t stride = vertexDesc.GetSize();

			uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
			uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
			uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
			uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);

			eastl::vector<Vertex> vertices(vertexCount);

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

					vertexData.Texcoord0[0] = texcoord[0];
					vertexData.Texcoord0[1] = texcoord[1];
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

				vertices[i] = eastl::move(vertexData);
			}

			MakeAndCopy(vertices, vertexBuffer);
			DX::ThrowIfFailed(vertexBuffer->SetName(std::format(L"Vertex Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));
		}

		// Create indices buffer
		{
			eastl::vector<uint32_t> indices(indexCount);

			eastl::transform(rendererData->rawIndexData,
				rendererData->rawIndexData + indexCount,
				indices.begin(),
				[](uint16_t idx) { return static_cast<uint32_t>(idx); });

			MakeAndCopy(indices, indexBuffer);
			DX::ThrowIfFailed(indexBuffer->SetName(std::format(L"Index Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));

			//const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(indexBuffer.get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			//const auto& barrier = CD3DX12_RESOURCE_BARRIER::UAV(vertexBuffer.get());
			//commandList->ResourceBarrier(1, &barrier);	
		}

		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* glowTexture = nullptr;

		// Register textures buffer
		{
			using State = RE::BSGeometry::States;
			using Feature = RE::BSShaderMaterial::Feature;

			auto geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

			auto effect = geometryRuntimeData.properties[State::kEffect].get();

			if (effect) {
				auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);

				if (lightingShader) {
					auto material = lightingShader->material;

					if (material) {
						auto lightingBaseMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(material);

						auto niDiffuseTexture = lightingBaseMaterial->diffuseTexture;
						auto bsDiffuseTexture = niDiffuseTexture->rendererTexture;

						if (auto it = sharedTextures.find(bsDiffuseTexture->texture); it != sharedTextures.end()) {
							diffuseTexture = it->second.get();
						} else {
							logger::warn(fmt::runtime("[RTGI] Diffuse texture not found"));
						}

						/*if (material->GetFeature() == Feature::kGlowMap) {
									auto lightingGlowMaterial = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material);

									RE::NiSourceTexture* niTextures;
									auto niTexturesCount = lightingGlowMaterial->GetTextures(&niTextures);

									if (niTexturesCount > 1) {
										auto bsGlowTexture = niTextures[2].rendererTexture;

										if (auto it = sharedTextures.find(bsGlowTexture->texture); it != sharedTextures.end()) {
											glowTexture = it->second.get();
										} else {
											logger::warn(fmt::runtime("[RTGI] Glow texture not found"));
										}
									}
								}*/

						/*if (material->GetFeature() == Feature::kDefault)
								{
									auto lightingDefaultMaterial = static_cast<RE::BSLightingShaderMaterial*>(material);

								} else if (material->GetFeature() == Feature::kGlowMap) 
								{
									auto lightingGlowMaterial = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material);
									
								}*/
					}
				}
			}
		}

		// Create BLAS
		{
			blasBuffer.attach(MakeBLAS(vertexBuffer.get(), vertexCount, indexBuffer.get(), indexCount));
		}

		// Mesh buffer
		{
			uint registerIndex = registers.allocate();

			// Vertex structured buffer
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = static_cast<int32_t>(vertexCount);
			vbDesc.Buffer.StructureByteStride = sizeof(Vertex);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(vertexBuffer.get(), &vbDesc, commonHeap->CPUHandle(Vertices, registerIndex));

			// Index/Triangle (Structured buffer)
			D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
			ibDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			ibDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ibDesc.Format = DXGI_FORMAT_UNKNOWN;
			ibDesc.Buffer.FirstElement = 0;
			ibDesc.Buffer.NumElements = static_cast<int32_t>(triangleCount);
			ibDesc.Buffer.StructureByteStride = sizeof(uint) * 3;
			ibDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(indexBuffer.get(), &ibDesc, commonHeap->CPUHandle(Triangles, registerIndex));

			// Diffuse Textures
			if (diffuseTexture) {
				D3D12_RESOURCE_DESC texResDesc = diffuseTexture->GetDesc();

				D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
				texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				texSrvDesc.Format = texResDesc.Format;  // Texture format
				texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				texSrvDesc.Texture2D.MostDetailedMip = 0;
				texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
				texSrvDesc.Texture2D.PlaneSlice = 0;
				texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				d3d12Device->CreateShaderResourceView(diffuseTexture, &texSrvDesc, commonHeap->CPUHandle(DiffuseTextures, registerIndex));
			}

			// Emplace mesh
			meshes.emplace(
				vertexBufferDX11,
				MeshData(
					registerIndex,
					vertexCount,
					indexCount,
					std::move(vertexBuffer),
					std::move(indexBuffer),
					std::move(blasBuffer),
					MaterialData(diffuseTexture, glowTexture),
					{ pTriShape }));
		}
	} else {
		meshIt->second.instances.push_back(pTriShape);
	}

	instances.emplace(
		pTriShape,
		InstanceData(
			vertexBufferDX11,
			GetXMFromNiTransform(pTriShape->world),
			GatherInstanceLights(pTriShape)));
}

eastl::vector<size_t> RaytracedGI::GatherInstanceLights(RE::BSTriShape* pBSTriShape)
{
	eastl::vector<size_t> instanceLights;

	float3 center = Float3(pBSTriShape->worldBound.center);
	float radius = pBSTriShape->worldBound.radius;

	for (size_t i = 0; i < lights.size(); i++) {
		const Light& light = lights[i];

		if ((center - light.Vector).Length() <= radius + light.Range)
			instanceLights.push_back(i);
	}

	return instanceLights;
}

void RaytracedGI::AddUpdateInstance(RE::BSTriShape* pTriShape)
{
	std::lock_guard lock{ meshMutex };

	if (const auto it = instances.find(pTriShape); it != instances.end()) {
		InstanceData& instance = it->second;

		instance.transform = GetXMFromNiTransform(pTriShape->world);
		instance.lights = GatherInstanceLights(pTriShape);		
	} else {
		AddInstance(pTriShape);	
	}
}

void RaytracedGI::VertexBufferReleased(ID3D11Buffer* pBuffer)
{
	std::lock_guard lock{ meshMutex };

	if (const auto it = meshes.find(pBuffer); it != meshes.end()) {
		MeshData& meshData = it->second;

		for (auto& instance : meshData.instances)
			instances.erase(instance);

		registers.free(meshData.registerIndex);

		meshes.erase(pBuffer);
	}
}

void RaytracedGI::UpdateInstances()
{
	std::lock_guard lock{ meshMutex };

	instanceData.clear();
	instanceData.resize(instances.size());

	size_t instanceID = 0;
	for (auto& [triShape, data] : instances) {

		/*auto it = meshes.find(data.meshKey);

		if (it == meshes.end())
			continue;

		MeshData& meshData = it->second;*/

		MeshData& meshData = meshes[data.meshKey];

		/*if (meshData.blasBuffer == nullptr)
			meshData.blasBuffer.attach(MakeBLAS(meshData.vertexBuffer.get(), meshData.vertexCount, meshData.indexBuffer.get(), meshData.indexCount));*/

		blasInstances[instanceID] = {
			.InstanceID = static_cast<uint>(instanceID),
			.InstanceMask = 1,
			.AccelerationStructure = meshData.blasBuffer->GetGPUVirtualAddress()
		};

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&blasInstances[instanceID].Transform);
		XMStoreFloat3x4(ptr, data.transform);

		instanceData[instanceID] = Instance(static_cast<uint>(meshData.registerIndex), LightData(data.lights));

		//logger::info("UpdateInstances - ID: {}, MeshIndex: {}, Vertex Key Buffer: {:x}, Index Key Buffer: {:x}", instanceID, meshIndex, reinterpret_cast<uintptr_t>(key.vertexBuffer), reinterpret_cast<uintptr_t>(key.indexBuffer));

		instanceID++;
	}

	instanceBuffer->UpdateList(instanceData.data(), instanceData.size());
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

void RaytracedGI::BSTriShape_UpdateWorldData(RE::BSTriShape* oThis, RE::NiUpdateData* pData)
{
	if (Active() && pData->flags.any(RE::NiUpdateData::Flag::kDirty)) {
		RE::NiPoint3 pointA = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);

		RE::NiPoint3 pointB = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		if (pointA.GetDistance(pointB) > 0.1f) {
			AddUpdateInstance(oThis);
		}		
	} else {
		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);
	}
}

void RaytracedGI::BSShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active())
		return;

	UpdateLights();

	if (auto triShape = pPass->geometry->AsTriShape())
		AddUpdateInstance(triShape);
}

void RaytracedGI::DrawRTGI()
{
	//std::lock_guard lock{ renderMutex };

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
	/*{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	// Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
	}*/

	bool captureStarted = false;

	if (capture) {
		captureStarted = true;
		ga->BeginCapture();
	}

	UpdateInstances();

	// Upload buffers
	{
		instanceBuffer->Upload(commandList.get());
		lightBuffer->Upload(commandList.get());
	}

	// Update framebuffer
	{
		frameBufferData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameBufferData->ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();
		frameBufferData->Position = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameBufferData->LightCount = static_cast<uint>(lights.size());
		frameBufferData->FrameCount = globals::state->frameCount;
	}

	// Create TLAS and its update scratch or update TLAS
	if (tlas == nullptr || tlasUpdateScratch == nullptr) {
		UINT64 updateScratchSize;
		tlas.attach(MakeTLAS(blasInstanceBuffer.get(), static_cast<uint>(instances.size()), &updateScratchSize));
		DX::ThrowIfFailed(tlas->SetName(L"TLAS"));

		auto desc = BASIC_BUFFER_DESC;
		desc.Width = std::max(updateScratchSize, 8ULL);  // WARP bug workaround: use 8 if the required size was reported as less
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
		DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch"));

		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, commonHeap->CPUHandle(TLAS));
	} else {
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {
			.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.Inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
				.NumDescs = static_cast<uint>(instances.size()),
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = blasInstanceBuffer->GetGPUVirtualAddress() },
			.SourceAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.ScratchAccelerationStructureData = tlasUpdateScratch->GetGPUVirtualAddress(),
		};

		commandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
	}

	const auto& tlasBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
	commandList->ResourceBarrier(1, &tlasBarrier);	

	{
		commandList->SetPipelineState1(pipelineRT.get());
		commandList->SetComputeRootSignature(rootSignature.get());

		auto commonHeapPtr = commonHeap->Heap();
		commandList->SetDescriptorHeaps(1, &commonHeapPtr);

		// Parameter 0: UAV table
		commandList->SetComputeRootDescriptorTable(0, commonHeap->GPUHandle(OutputUAV));

		// Parameter 1: Fixed SRVs (Scene + Lights + Index)
		commandList->SetComputeRootDescriptorTable(1, commonHeap->GPUHandle(TLAS));

		// Parameter 2: Vertex buffers
		commandList->SetComputeRootDescriptorTable(2, commonHeap->GPUHandle(Vertices));

		// Parameter 3: Triangle buffers
		commandList->SetComputeRootDescriptorTable(3, commonHeap->GPUHandle(Triangles));

		// Parameter 4: Textures
		commandList->SetComputeRootDescriptorTable(4, commonHeap->GPUHandle(DiffuseTextures));

		// Parameter 5: Constant buffer
		commandList->SetComputeRootConstantBufferView(5, frameBuffer->GetGPUVirtualAddress());

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

		// Wait until GPU is done with previous frame before reusing allocator
		if (d3d12Fence->GetCompletedValue() < fenceValue) {
			DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
		}

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	//New frame, reset
	{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	//Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
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
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer)));
		return buffer;
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
	if (updateScratchSize)
		*updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

	winrt::com_ptr<ID3D12Resource> scratch;
	scratch.attach(makeBuffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_COMMON));

	auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);


	auto buildDesc = eastl::make_unique<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>(
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC{
			.DestAccelerationStructureData = as->GetGPUVirtualAddress(),
			.Inputs = inputs,
			.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() 
		}
	);

	commandList->BuildRaytracingAccelerationStructure(buildDesc.get(), 0, nullptr);

	rtASDescs.push_back(eastl::move(buildDesc));

	const auto& tlasBarrier = CD3DX12_RESOURCE_BARRIER::UAV(as);
	commandList->ResourceBarrier(1, &tlasBarrier);	

	/*DX::ThrowIfFailed(commandList->Close());

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

	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));*/

	asScratchBuffers.push_back(std::move(scratch));

	return as;
}

ID3D12Resource* RaytracedGI::MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices)
{
	eastl::array<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs({ D3D12_RAYTRACING_GEOMETRY_DESC{
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
	 }});

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	rtGeometryDescs.push_back(eastl::move(geometryDescs));

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

	//MenuOpenCloseEventHandler::Register();
	//TESLoadGameEventHandler::Register();
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

void RaytracedGI::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter)
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
	DX::ThrowIfFailed(pImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create debug device
	if (!settings.EnablePIXCapture)
	{
		winrt::com_ptr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();

			winrt::com_ptr<ID3D12Debug1> debugController1;
			if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)))) {
				debugController1->SetEnableGPUBasedValidation(TRUE);
			}
		}
	}

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

		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
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

	// Textures (unbounded)
	CD3DX12_DESCRIPTOR_RANGE1 texturesRange;
	texturesRange.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		UINT_MAX,  // Unbounded
		0,         // t0
		3,         // space3
		D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	// Root parameters
	CD3DX12_ROOT_PARAMETER1 params[6];
	params[0].InitAsDescriptorTable(1, &uavRange);           // Parameter 0: UAV
	params[1].InitAsDescriptorTable(3, fixedSrvRanges);      // Parameter 1: Scene + Lights + Index map
	params[2].InitAsDescriptorTable(1, &vertexBufferRange);  // Parameter 2: Vertex buffers
	params[3].InitAsDescriptorTable(1, &indexBufferRange);   // Parameter 3: Index buffers
	params[4].InitAsDescriptorTable(1, &texturesRange);      // Parameter 3: Textures
	params[5].InitAsConstantBufferView(0, 0);                // Parameter 4: CBV

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);  // register s0

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		_countof(params),
		params,
		1,
		&staticSampler,
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
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 6 };
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
	// When entering a loadscreen
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		//auto& rtgi = globals::features::raytracedGI;

		logger::info("MenuOpenCloseEventHandler::ProcessEvent - Opening: {}", a_event->opening);

		if (a_event->opening) {			
			//rtgi.meshesCreated = false;		
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl RaytracedGI::TESLoadGameEventHandler::ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*)
{
	logger::info("TESLoadGameEventHandler::ProcessEvent {}", reinterpret_cast<intptr_t>(a_event));

	return RE::BSEventNotifyControl::kContinue;
}
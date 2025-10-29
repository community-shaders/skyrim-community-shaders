/*
* This file accompanies RaytracedGI.h
* Please refer to the header for more information.
*
* ProfJack
* 2025-06-28
*/

#include "RaytracedGI.h"

#include "Globals.h"
#include "State.h"
#include "RaytracedGI/ShaderUtils.h"
#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RaytracedGI::Settings,
	Enabled,
	ColorA,
	IdA,
	UvA)

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
	ImGui::SeparatorText("Cheese");
	ImGui::ColorEdit3("Color A", &settings.ColorA.x);
	uint step = 1;
	ImGui::InputScalarN("Id A", ImGuiDataType_U32, &settings.IdA[0], 2, &step, NULL, "%u annoying uints");
	ImGui::InputFloat2("UV A", &settings.UvA.x);

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Vertex buffers: {}", vertexBuffers.size()).c_str());
		ImGui::Text(std::format("Index buffers: {}", indexBuffers.size()).c_str());

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

	logger::debug("Creating buffers...");
	{
		cheeseCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CbData>());
	}

	// UAV heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		};
		d3d12Device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap));	
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

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = texDesc.Format;
		d3d12Device->CreateUnorderedAccessView(diffuseGITexture->resource.get(), nullptr, &uavDesc, uavHeap->GetCPUDescriptorHandleForHeapStart());
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

	logger::info("[RTGI] Initializing meshes");
	{
		auto makeAndCopy = [&](auto& data, winrt::com_ptr<ID3D12Resource>& res)
		{
			auto desc = BASIC_BUFFER_DESC;
			desc.Width = sizeof(data);

			DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));

			void* ptr;
			DX::ThrowIfFailed(res->Map(0, nullptr, &ptr));
			memcpy(ptr, data, sizeof(data));
			res->Unmap(0, nullptr);
		};

		makeAndCopy(quadVtx, quadVB);
		makeAndCopy(cubeVtx, cubeVB);
		makeAndCopy(cubeIdx, cubeIB);
	}

	logger::info("[RTGI] BLAS");
	{
		quadBlas.attach(MakeBLAS(quadVB.get(), std::size(quadVtx)));
		cubeBlas.attach(MakeBLAS(cubeVB.get(), std::size(cubeVtx), cubeIB.get(), std::size(cubeIdx)));
	}

	logger::info("[RTGI] Init scene");
	{
		auto instancesDesc = BASIC_BUFFER_DESC;
		instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * NUM_INSTANCES;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instances)));
		instances->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

		for (UINT i = 0; i < NUM_INSTANCES; ++i)
			instanceData[i] = {
				.InstanceID = i,
				.InstanceMask = 1,
				.AccelerationStructure = (i ? quadBlas : cubeBlas)->GetGPUVirtualAddress(),
			};

		UpdateTransforms();
	}

	logger::info("[RTGI] TLAS");
	{
		UINT64 updateScratchSize;
		tlas.attach(MakeTLAS(instances.get(), NUM_INSTANCES, &updateScratchSize));

		auto desc = BASIC_BUFFER_DESC;
		// WARP bug workaround: use 8 if the required size was reported as less
		desc.Width = std::max(updateScratchSize, 8ULL);
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
	}

	CompileShaders();
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
	desc.Width = (UINT64)data.size();

	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));

	void* ptr;
	DX::ThrowIfFailed(res->Map(0, nullptr, &ptr));
	memcpy(ptr, data.data(), desc.Width);
	res->Unmap(0, nullptr);
}

void RaytracedGI::CreateBuffers()
{
	if (buffersCreated)
		return;

	vertexBuffers.clear();
	indexBuffers.clear();

	static auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];

	RE::BSVisit::TraverseScenegraphGeometries(shadowSceneNode, [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
		if (RE::BSTriShape* triShape = geometry->AsTriShape()) {

			// Ensure its Lighting shader, for now
			if (!triShape->lightingShaderProp_cast())
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

				const auto bsxFlagsStr = GetFlags<RE::BSXFlags::Flag>(bsxFlagsValue);
				logger::info(fmt::runtime("BSXFlags: {}"), bsxFlagsStr);

				const auto& geometryType = geometry->GetType().get();
				const auto geometryTypeName = magic_enum::enum_name(geometryType);

				logger::info(fmt::runtime("Geometry [0x{:x}]: {}, Type: {}"), reinterpret_cast<uintptr_t>(geometry), geometry->name.c_str(), geometryTypeName);

				const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flags.underlying());

				logger::info(fmt::runtime("Flags: {}"), flagsStr);

				const auto triShapeRuntime = triShape->GetTrishapeRuntimeData();

				uint vertexCount = triShapeRuntime.vertexCount;
				uint indexCount = triShapeRuntime.triangleCount * 3;

				logger::info(fmt::runtime("Vertex Count: {}, Index Count: {}"), vertexCount, indexCount);

				RE::BSGraphics::TriShape* rendererData = triShape->GetGeometryRuntimeData().rendererData;
				//RE::BSGraphics::TriShape* rendererData = geometry->GetGeometryRuntimeData().rendererData;

				if (!rendererData)
					return RE::BSVisit::BSVisitControl::kContinue;

				// Create vertex buffer
				{
					auto vertexDesc = rendererData->vertexDesc;

					auto vertexFlags = vertexDesc.GetFlags();
					uint32_t stride = vertexDesc.GetSize();

					uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
					uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
					uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
					uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);

					eastl::vector<VertexData> vertices;
					vertices.reserve(vertexCount);

					for (uint32_t i = 0; i < vertexCount; i++) {
						uint8_t* vtx = rendererData->rawVertexData + i * stride;

						VertexData vertexData{};

						if (vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX) {
							const float* pos = reinterpret_cast<const float*>(vtx + posOffset);
							vertexData.Position.x = pos[0];
							vertexData.Position.y = pos[1];
							vertexData.Position.z = pos[2];
							vertexData.Position.w = pos[3];
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

						vertices.push_back(vertexData);
					}

					winrt::com_ptr<ID3D12Resource> vertexBuffer = nullptr;
					MakeAndCopy(vertices, vertexBuffer);

					vertexBuffers.emplace((ID3D11Buffer*)rendererData->vertexBuffer, std::move(vertexBuffer));
				}

				// Create indices buffer
				{
					eastl::vector<uint16_t> indexes(rendererData->rawIndexData, rendererData->rawIndexData + indexCount);

					winrt::com_ptr<ID3D12Resource> indexBuffer = nullptr;
					MakeAndCopy(indexes, indexBuffer);

					indexBuffers.emplace((ID3D11Buffer*)rendererData->indexBuffer, std::move(indexBuffer));
				}

			}

		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	buffersCreated = true;
}

void RaytracedGI::UnregisterBuffer(ID3D11Buffer* ppBuffer)
{
	vertexBuffers.erase(ppBuffer);
	indexBuffers.erase(ppBuffer);
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
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue)); // Crash here
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;
	}

	// New frame, reset
	{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	// Update scene
	{
		UpdateTransforms();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {
			.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.Inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
				.NumDescs = NUM_INSTANCES,
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = instances->GetGPUVirtualAddress() 
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

		auto uavHeapPtr = uavHeap.get();
		commandList->SetDescriptorHeaps(1, &uavHeapPtr);

		auto uavTable = uavHeap->GetGPUDescriptorHandleForHeapStart();
		commandList->SetComputeRootDescriptorTable(0, uavTable);  // <u0 vt0

		commandList->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress());	

		auto rtDesc = diffuseGITexture->resource->GetDesc();

		D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
			.RayGenerationShaderRecord = {
				.StartAddress = shaderIDs->GetGPUVirtualAddress(),
				.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
			},
			.MissShaderTable = { 
				.StartAddress = shaderIDs->GetGPUVirtualAddress() + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
				.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
			},
			.HitGroupTable = { 
				.StartAddress = shaderIDs->GetGPUVirtualAddress() + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
				.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
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
		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}
}

void RaytracedGI::UpdateTransforms()
{
	using namespace DirectX;
	auto set = [&](int idx, XMMATRIX mx) {
		auto* ptr = reinterpret_cast<XMFLOAT3X4*>(&instanceData[idx].Transform);
		XMStoreFloat3x4(ptr, mx);
	};

	auto time = static_cast<float>(GetTickCount64()) / 1000;

	auto cube = XMMatrixRotationRollPitchYaw(time / 2, time / 3, time / 5);
	cube *= XMMatrixTranslation(-1.5, 2, 2);
	set(0, cube);

	auto mirror = XMMatrixRotationX(-1.8f);
	mirror *= XMMatrixRotationY(XMScalarSinEst(time) / 8 + 1);
	mirror *= XMMatrixTranslation(2, 2, 2);
	set(1, mirror);

	auto floor = XMMatrixScaling(5, 5, 5);
	floor *= XMMatrixTranslation(0, 0, 2);
	set(2, floor);
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

ID3D12Resource* RaytracedGI::MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertexFloats, ID3D12Resource* indexBuffer, UINT indices)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,

		.Triangles = {
			.Transform3x4 = 0,

			.IndexFormat = indexBuffer ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_UNKNOWN,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = indices,
			.VertexCount = vertexFloats / 3,
			.IndexBuffer = indexBuffer ? indexBuffer->GetGPUVirtualAddress() : 0,
			.VertexBuffer = { 
				.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
				.StrideInBytes = sizeof(float) * 3 
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

void RaytracedGI::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* ppImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	logger::info("[RTGI] Creating D3D12 device");

	// Set Device
	DX::ThrowIfFailed(ppDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(ppImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create debug device
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

	D3D12_DESCRIPTOR_RANGE uavRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		.NumDescriptors = 1,
	};
	D3D12_ROOT_PARAMETER params[] = {
		{ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
			.DescriptorTable = { 
				.NumDescriptorRanges = 1,
				.pDescriptorRanges = &uavRange
			} 
		},
		{ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
			.Descriptor = { 
				.ShaderRegister = 0, 
				.RegisterSpace = 0 
			} 
		}
	};

	D3D12_ROOT_SIGNATURE_DESC desc = { 
		.NumParameters = std::size(params),
		.pParameters = params 
	};

	winrt::com_ptr<ID3DBlob> blob;
	DX::ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), nullptr));
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
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
	winrt::com_ptr<IDxcBlob> rayGenBlob, missBlob, closestHitBlob;

	ShaderUtils::CompileShader(rayGenBlob, L"Data/Shaders/RaytracedGI/Raytracing/RayGeneration.hlsl", L"lib_6_3");
	ShaderUtils::CompileShader(missBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl", L"lib_6_3");
	ShaderUtils::CompileShader(closestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl", L"lib_6_3");

	// Init pipeline
	{
		std::vector<D3D12_STATE_SUBOBJECT> subobjects;
		subobjects.reserve(8);

		// Ray generation shader
		D3D12_EXPORT_DESC rayGenExport = {};
		rayGenExport.Name = L"RayGeneration";          // Pipeline-visible name
		rayGenExport.ExportToRename = L"main";  // original HLSL function
		rayGenExport.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC rayGenLibDesc = {};
		rayGenLibDesc.DXILLibrary.pShaderBytecode = rayGenBlob->GetBufferPointer();
		rayGenLibDesc.DXILLibrary.BytecodeLength = rayGenBlob->GetBufferSize();
		rayGenLibDesc.NumExports = 1;
		rayGenLibDesc.pExports = &rayGenExport;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &rayGenLibDesc });

		// Miss shader
		D3D12_EXPORT_DESC missExport = {};
		missExport.Name = L"Miss";
		missExport.ExportToRename = L"main";
		missExport.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC missLibDesc = {};
		missLibDesc.DXILLibrary.pShaderBytecode = missBlob->GetBufferPointer();
		missLibDesc.DXILLibrary.BytecodeLength = missBlob->GetBufferSize();
		missLibDesc.NumExports = 1;
		missLibDesc.pExports = &missExport;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &missLibDesc });

		// Closest Hit shader
		D3D12_EXPORT_DESC closestHitExport = {};
		closestHitExport.Name = L"ClosestHit";
		closestHitExport.ExportToRename = L"main";
		closestHitExport.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC closestHitLibDesc = {};
		closestHitLibDesc.DXILLibrary.pShaderBytecode = closestHitBlob->GetBufferPointer();
		closestHitLibDesc.DXILLibrary.BytecodeLength = closestHitBlob->GetBufferSize();
		closestHitLibDesc.NumExports = 1;
		closestHitLibDesc.pExports = &closestHitExport;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &closestHitLibDesc });

		// Hit Group subobject
		D3D12_HIT_GROUP_DESC hitGroupDesc = {};
		hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";  // matches DXIL export
		hitGroupDesc.HitGroupExport = L"HitGroup";            // shader table name
		hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupDesc });

		// Shader config
		D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
			.MaxPayloadSizeInBytes = 20,
			.MaxAttributeSizeInBytes = 8,
		};
		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg });

		// Global root signature
		D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { rootSignature.get() };
		subobjects.push_back({ .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig });

		// RT pipeline config
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 3 };
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
		writeId(L"HitGroup");
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
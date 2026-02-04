#include "SkinningPipeline.h"

#include "Features/Raytracing.h"

void SkinningPipeline::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SkinningHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SkinningHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	heap->CreateTable(
		SkinningHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SkinningHeap::Slot::Output, UINT_MAX, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	heap->CreateTable(
		SkinningHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::UpdateData, 1, 0 },
			{ SkinningHeap::Slot::BoneMatrices, 1, 0 } });

	heap->CreateTable(
		SkinningHeap::Table::DynamicBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::DynamicVertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	heap->CreateTable(
		SkinningHeap::Table::VertexBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::Vertices, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	heap->CreateTable(
		SkinningHeap::Table::SkinningBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::SkinningData, UINT_MAX, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	auto rootParameters = heap->GetRootParameters();

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig;
	winrt::com_ptr<ID3DBlob> errorBlob;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignature.put())));
	DX::ThrowIfFailed(rootSignature->SetName(L"Compute Root Signature - Skinning"));
}

void SkinningPipeline::CompileShaders(ID3D12Device5* device)
{
	const auto threadSizeWStr = std::to_wstring(settings.ThreadGroupSize);
	auto mapping = settings.OptimizedMapping ? L"OPTIMIZED_MAPPING" : L"STANDARD_MAPPING";

	winrt::com_ptr<IDxcBlob> shaderBlob;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/SkinningCS.hlsl", { { L"THREAD_GROUP_SIZE", threadSizeWStr.c_str() }, { mapping, L"" } }, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - Skinning"));

	recompile = false;
}

void SkinningPipeline::SetupResources(ID3D12Device5* device)
{
	auto* commandList = globals::features::raytracing.commandList.get();

	vertexUpdateBuffer = eastl::make_unique<DX12::StructuredBufferUpload<VertexUpdateData>>(device, MAX_GEOMETRY, false);
	DX::ThrowIfFailed(vertexUpdateBuffer->resource->SetName(L"Vertex Update Buffer"));
	vertexUpdateBuffer->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	vertexUpdateBuffer->CreateSRV(heap->CPUHandle(SkinningHeap::Slot::UpdateData));

	boneMatricesBuffer = eastl::make_unique<DX12::StructuredBufferUpload<float3x4>>(device, MAX_BONE_MATRICES, false);
	DX::ThrowIfFailed(boneMatricesBuffer->resource->SetName(L"Bone Matrices Buffer"));
	boneMatricesBuffer->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	boneMatricesBuffer->CreateSRV(heap->CPUHandle(SkinningHeap::Slot::BoneMatrices));
}

void SkinningPipeline::QueueUpdate(Shape::Flags updateFlags, eastl::string path, Shape* shape)
{
	queuedShapes.emplace(
		shape,
		QueuedShape{ updateFlags, path });
}

bool SkinningPipeline::PrepareResources([[maybe_unused]]ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount)
{
	if (queuedShapes.empty())
		return false;

	auto queueSize = queuedShapes.size();

	vertexUpdateData.clear();
	vertexUpdateData.reserve(queueSize);

	boneMatricesData.clear();
	boneMatricesData.reserve(MAX_BONE_MATRICES);

	// Barrier to UAV state
	barriers.clear();
	barriers.reserve(queueSize);

	float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	auto eye = Float3(Util::GetEyePosition(0));

	float3 bonePivot = float3(cameraPosition.x, cameraPosition.y, cameraPosition.z) - eye;

	for (auto& [shape, queuedShape] : queuedShapes) {
		uint boneOffset = (uint)boneMatricesData.size();

		if (boneOffset >= MAX_BONE_MATRICES) {
			logger::critical("[RT] SkinningPipeline::PrepareResources - Exceeded maximum bone matrices limit of {}", MAX_BONE_MATRICES);
			break;
		}

		count = (uint)vertexUpdateData.size();

		if (count >= MAX_GEOMETRY) {
			logger::critical("[RT] SkinningPipeline::PrepareResources - Exceeded maximum geometry update limit of {}", MAX_GEOMETRY);
			break;
		}

		vertexCount = std::max(vertexCount, (uint)shape->vertexCount);
		vertexUpdateData.emplace_back(shape->allocation->GetIndex(), queuedShape.updateFlags, shape->vertexCount, boneOffset, bonePivot, shape->boundRadius);

		// Dynamic TriShapes
		shape->UpdateUploadDynamicBuffers(commandList);

		// Skinning - This is a bit more involved
		if (queuedShape.updateFlags & Shape::Flags::Skinned) {
			boneMatricesData.insert(boneMatricesData.end(),
				eastl::make_move_iterator(shape->boneMatrices.begin()),
				eastl::make_move_iterator(shape->boneMatrices.end()));
		}

		CD3DX12_RESOURCE_BARRIER barrier;
		if (shape->vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, barrier))
			barriers.push_back(barrier);
	}

	uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());

	vertexUpdateBuffer->UpdateList(vertexUpdateData.data(), count);
	vertexUpdateBuffer->Upload(commandList);

	boneMatricesBuffer->UpdateList(boneMatricesData.data(), boneMatricesData.size());
	boneMatricesBuffer->Upload(commandList);

	return true;
}

void SkinningPipeline::RestoreResources(ID3D12GraphicsCommandList4* commandList)
{
	// Barrier to NPSR state
	barriers.clear();
	barriers.reserve(queuedShapes.size());

	for (auto& [shape, queuedShape] : queuedShapes) {
		CD3DX12_RESOURCE_BARRIER barrier;
		if (shape->vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, barrier))
			barriers.push_back(barrier);
	}

	const uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());
}

void SkinningPipeline::ClearQueue()
{
	queuedShapes.clear();
}

void SkinningPipeline::Dispatch(ID3D12GraphicsCommandList4* commandList, ID3D12Device5* device)
{
	if (recompile)
		CompileShaders(device);

	if (!frameChecker.IsNewFrame())
		return;

	uint count = 0;
	uint vertexCount = 0;

	if (!PrepareResources(commandList, count, vertexCount))
		return;

	commandList->SetPipelineState(pipelineState.get());
	commandList->SetComputeRootSignature(rootSignature.get());

	auto* pHeap = heap->Heap();
	commandList->SetDescriptorHeaps(1, &pHeap);

	commandList->SetComputeRootDescriptorTable(0, heap->TableGPUHandle(SkinningHeap::Table::UAV));

	commandList->SetComputeRootDescriptorTable(1, heap->TableGPUHandle(SkinningHeap::Table::SRV));

	commandList->SetComputeRootDescriptorTable(2, heap->TableGPUHandle(SkinningHeap::Table::DynamicBuffer));

	commandList->SetComputeRootDescriptorTable(3, heap->TableGPUHandle(SkinningHeap::Table::VertexBuffer));

	commandList->SetComputeRootDescriptorTable(4, heap->TableGPUHandle(SkinningHeap::Table::SkinningBuffer));

	const uint vertexDispatchSize = DivideRoundUp(vertexCount, settings.ThreadGroupSize);
	commandList->Dispatch(count, vertexDispatchSize, 1);

	RestoreResources(commandList);

	ClearQueue();
}
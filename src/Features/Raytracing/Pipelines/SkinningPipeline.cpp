#include "SkinningPipeline.h"

#include "Features/Raytracing.h"

void SkinningPipeline::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SkinningHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SkinningHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	auto unboundTableFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
	auto dynamicFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

	heap->CreateTable(
		SkinningHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SkinningHeap::Slot::Output, UINT_MAX, 0, unboundTableFlags } });

	heap->CreateTable(
		SkinningHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::UpdateData, 1, 0, dynamicFlags },
			{ SkinningHeap::Slot::BoneMatrices, 1, 0, dynamicFlags } });

	heap->CreateTable(
		SkinningHeap::Table::DynamicBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::DynamicVertices, UINT_MAX, 1, unboundTableFlags } });

	heap->CreateTable(
		SkinningHeap::Table::SkinningBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SkinningHeap::Slot::SkinningData, UINT_MAX, 2, unboundTableFlags } });

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
	vertexUpdateBuffer = eastl::make_unique<DX12::StructuredBufferUpload<VertexUpdateData>>(device, RTConstants::MAX_MODELS, false, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DX::ThrowIfFailed(vertexUpdateBuffer->resource->SetName(L"Vertex Update Buffer"));

	vertexUpdateBuffer->CreateSRV(heap->CPUHandle(SkinningHeap::Slot::UpdateData));

	boneMatricesBuffer = eastl::make_unique<DX12::StructuredBufferUpload<float3x4>>(device, MAX_BONE_MATRICES, false, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DX::ThrowIfFailed(boneMatricesBuffer->resource->SetName(L"Bone Matrices Buffer"));

	boneMatricesBuffer->CreateSRV(heap->CPUHandle(SkinningHeap::Slot::BoneMatrices));

}

void SkinningPipeline::QueueUpdate(Flags updateFlags, eastl::string path, Shape* shape, const float3x4& localToRoot, const float3x4& worldToLocal)
{
	queuedShapes.emplace_back(
		updateFlags,
		path, 
		shape, 
		localToRoot,
		worldToLocal);
}

bool SkinningPipeline::PrepareResources(ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount)
{
	if (queuedShapes.empty())
		return false;

	auto queueSize = queuedShapes.size();

	eastl::vector<VertexUpdateData> vertexUpdateData;
	vertexUpdateData.reserve(queueSize);

	eastl::vector<float3x4> boneMatricesData;
	boneMatricesData.reserve(queueSize);

	// Barrier to UAV state
	eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	barriers.reserve(queueSize);

	float4 cameraPosF4 = globals::game::frameBufferCached.GetCameraPosAdjust();
	float3 cameraPos = float3(cameraPosF4.x, cameraPosF4.y, cameraPosF4.z);

	for (auto& queuedShape : queuedShapes) {
		Shape* shape = queuedShape.shape;

		uint boneOffset = (uint)boneMatricesData.size();

		vertexCount = std::max(vertexCount, (uint)shape->vertexCount);
		vertexUpdateData.emplace_back(shape->allocation->GetIndex(), queuedShape.updateFlags, shape->vertexCount, boneOffset, queuedShape.localToRoot, cameraPos, 0, queuedShape.worldToLocal);

		// Dynamic TriShapes
		shape->UpdateUploadDynamicBuffers(commandList);

		// Skinning - This is a bit more involved
		if (queuedShape.updateFlags & Flags::Skinned) {
			// Reset vertices, maybe we should keep a copy of this buffer already bound to our shaders? 
			// That way instead of barrier -> copy -> barrier we just read the initial vertices from the srv
			if (!(queuedShape.updateFlags & Flags::Dynamic)) {
				shape->vertexBuffer->Upload(commandList);
			}

			auto boneMatrices = shape->GetBoneMatrices();

			boneMatricesData.insert(boneMatricesData.end(),
				eastl::make_move_iterator(boneMatrices.begin()),
				eastl::make_move_iterator(boneMatrices.end()));
		}

		CD3DX12_RESOURCE_BARRIER barrier;
		if (shape->vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, barrier))
			barriers.push_back(barrier);
	}

	uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());

	count = (uint)vertexUpdateData.size();

	vertexUpdateBuffer->UpdateList(vertexUpdateData.data(), count);
	vertexUpdateBuffer->Upload(commandList);

	boneMatricesBuffer->UpdateList(boneMatricesData.data(), boneMatricesData.size());
	boneMatricesBuffer->Upload(commandList);

	return true;
}

void SkinningPipeline::RestoreResources(ID3D12GraphicsCommandList4* commandList)
{
	// Barrier to NPSR state
	eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	barriers.reserve(queuedShapes.size());

	for (auto& queuedShape : queuedShapes) {
		Shape* shape = queuedShape.shape;

		CD3DX12_RESOURCE_BARRIER barrier;
		if (shape->vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, barrier))
			barriers.push_back(barrier);
	}

	uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());
}

void SkinningPipeline::UpdateBLASES(ID3D12GraphicsCommandList4* commandList)
{
	auto& rt = globals::features::raytracing;

	eastl::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
	uavBarriers.reserve(queuedShapes.size());
	
	// One model contains multiple shapes, lets make a unique list of all updated model
	eastl::hash_set<eastl::string> paths;
	for (auto& queuedShape : queuedShapes) {
		paths.emplace(queuedShape.path);
	}

	// Lets update all models which had at least one updated shape
	for (auto& path : paths) {
		if (auto it = rt.models.find(path); it != rt.models.end()) {
			auto& model = it->second;

			rt.UpdateModelBLAS(model.get());

			uavBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(model->blasBuffer->GetResource()));
		}
	}

	uint blasUpdateCount = (uint)uavBarriers.size();

	if (blasUpdateCount > 0)
		commandList->ResourceBarrier(blasUpdateCount, uavBarriers.data());
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

	commandList->SetComputeRootDescriptorTable(3, heap->TableGPUHandle(SkinningHeap::Table::SkinningBuffer));

	if (settings.Dispatch) {
		const uint vertexDispatchSize = DivideRoundUp(vertexCount, settings.ThreadGroupSize);
		commandList->Dispatch(count, vertexDispatchSize, 1);
	}

	RestoreResources(commandList);

	if (settings.UpdateBLAS)
		UpdateBLASES(commandList);

	ClearQueue();
}

/*void Raytracing::UpdateDynamicSkinning(ID3D12GraphicsCommandList4* pCommandList)
{
	if (vertexUpdate.empty())
		return;

	auto updateCount = vertexUpdate.size();

	eastl::vector<VertexUpdateData> vertexUpdateData;
	vertexUpdateData.reserve(updateCount);

	// Reset vertices (having another buffer and just reading from it in shaders might be better)
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			vertexUpdateData.emplace_back(item.allocatedIndex, item.flags, item.vertexCount, 0);

			if (item.flags & Flags::Skinned) {
				barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_COPY_DEST));
			}
		}

		if (!barriers.empty()) {
			pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());

			for (auto& item : vertexUpdate) {
				if (item.flags & Flags::Skinned) {
					pCommandList->CopyResource(item.vertexBuffer->resource.get(), item.vertexBuffer->uploadResource[0].get());
				}
			}
		}
	}

	vertexUpdateBuffer->UpdateList(vertexUpdateData.data(), vertexUpdateData.size());
	vertexUpdateBuffer->Upload(pCommandList);

	pCommandList->SetPipelineState(skinningPipeline.get());
	pCommandList->SetComputeRootSignature(skinningRS.get());

	auto computeHeapPtr = skinningHeap->Heap();
	pCommandList->SetDescriptorHeaps(1, &computeHeapPtr);

	pCommandList->SetComputeRootDescriptorTable(0, skinningHeap->TableGPUHandle(SkinningHeap::Table::UAV));

	pCommandList->SetComputeRootDescriptorTable(1, skinningHeap->TableGPUHandle(SkinningHeap::Table::SRV));

	pCommandList->SetComputeRootDescriptorTable(2, skinningHeap->TableGPUHandle(SkinningHeap::Table::DynamicBuffer));

	pCommandList->SetComputeRootDescriptorTable(3, skinningHeap->TableGPUHandle(SkinningHeap::Table::SkinningBuffer));

	// Constant buffer
	//pCommandList->SetComputeRootConstantBufferView(2, shadowsCB->resource->GetGPUVirtualAddress());

	// Transition to Unordered Access
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		}

		pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());
	}

	// Dispatch our GPU vertex update
	//auto dispatchCount = static_cast<uint32_t>(ceil(updateCount / 16.0f));
	//pCommandList->Dispatch(dispatchCount, 1, 1);

	// Transition back to non-pixel shader resource
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}

		pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());
	}

	auto blasUpdateCount = (uint)modelUpdate.size();

	eastl::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
	uavBarriers.reserve(blasUpdateCount);

	for (auto& path : modelUpdate) {
		if (auto modelIt = models.find(path); modelIt != models.end()) {
			auto& model = modelIt->second;

			UpdateModelBLAS(model.get());

			uavBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(model->blasBuffer->GetResource()));
		}
	}

	commandList->ResourceBarrier(blasUpdateCount, uavBarriers.data());

	vertexUpdate.clear();
	modelUpdate.clear();
}*/

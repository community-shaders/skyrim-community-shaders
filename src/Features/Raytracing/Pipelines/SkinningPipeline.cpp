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
		{ { SkinningHeap::Slot::LocalToRoot, 1, 0, dynamicFlags },
			{ SkinningHeap::Slot::UpdateData, 1, 0, dynamicFlags },
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
	const auto threadSizeWStr = std::to_wstring(THREAD_SIZE);

	winrt::com_ptr<IDxcBlob> shaderBlob;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/SkinningCS.hlsl", { { L"THREAD_SIZE", threadSizeWStr.c_str() } }, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - Skinning"));
}

void SkinningPipeline::SetupResources(ID3D12Device5* device)
{
	vertexUpdateBuffer = eastl::make_unique<DX12::StructuredBufferUpload<VertexUpdateData>>(device, RTConstants::MAX_MODELS, false, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DX::ThrowIfFailed(vertexUpdateBuffer->resource->SetName(L"Vertex Update Buffer"));

	vertexUpdateBuffer->CreateSRV(heap->CPUHandle(SkinningHeap::Slot::UpdateData));
}

void SkinningPipeline::QueueUpdate(Flags updateFlags, eastl::string name, Shape* shape)
{
	queueModels.emplace_back(
		name, 
		shape->allocation->GetIndex(), 
		updateFlags & Flags::Dynamic ? shape->dynamicPositionBuffer.get() : nullptr, 
		shape->vertexBuffer.get(), 
		shape->vertexCount, 
		updateFlags);
}

bool SkinningPipeline::PrepareResources(ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount)
{
	if (queueModels.empty())
		return false;

	auto queueSize = queueModels.size();

	eastl::vector<VertexUpdateData> vertexUpdateData;
	vertexUpdateData.reserve(queueSize);

	// Barrier to UAV state
	eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	barriers.reserve(queueSize);

	for (auto& model : queueModels) {
		vertexCount = std::max(vertexCount, (uint)model.vertexCount);
		vertexUpdateData.emplace_back(model.allocatedIndex, model.flags, model.vertexCount, 0);

		CD3DX12_RESOURCE_BARRIER barrier;
		if (model.vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, barrier))
			barriers.push_back(barrier);
	}

	uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());

	count = (uint)vertexUpdateData.size();

	vertexUpdateBuffer->UpdateList(vertexUpdateData.data(), vertexUpdateData.size());
	vertexUpdateBuffer->Upload(commandList);

	return true;
}

void SkinningPipeline::RestoreResources(ID3D12GraphicsCommandList4* commandList)
{
	// Barrier to NPSR state
	eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	barriers.reserve(queueModels.size());

	for (auto& model : queueModels) {
		CD3DX12_RESOURCE_BARRIER barrier;
		if (model.vertexBuffer->GetTransitionBarrier(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, barrier))
			barriers.push_back(barrier);
	}

	uint barrierCount = (uint)barriers.size();

	if (barrierCount > 0)
		commandList->ResourceBarrier(barrierCount, barriers.data());
}

void SkinningPipeline::UpdateBLASES(ID3D12GraphicsCommandList4* commandList)
{
	eastl::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
	uavBarriers.reserve(queueModels.size());

	auto& rt = globals::features::raytracing;

	for (auto& queuedModel : queueModels) {
		if (auto it = rt.models.find(queuedModel.name); it != rt.models.end()) {
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
	queueModels.clear();
}

void SkinningPipeline::Dispatch(ID3D12GraphicsCommandList4* commandList)
{
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

	/*CD3DX12_RESOURCE_BARRIER uavBarrier[3] = {
		CD3DX12_RESOURCE_BARRIER::UAV(sharcHashEntriesBuffer->resource.get()),
		CD3DX12_RESOURCE_BARRIER::UAV(sharcAccumulationBuffer->resource.get()),
		CD3DX12_RESOURCE_BARRIER::UAV(sharcResolvedBuffer->resource.get())
	};

	commandList->ResourceBarrier(_countof(uavBarrier), uavBarrier);*/

	const uint vertexDispatchSize = DivideRoundUp(vertexCount, THREAD_SIZE);
	commandList->Dispatch(count, vertexDispatchSize, 1);

	//commandList->ResourceBarrier(_countof(uavBarrier), uavBarrier);

	RestoreResources(commandList);

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

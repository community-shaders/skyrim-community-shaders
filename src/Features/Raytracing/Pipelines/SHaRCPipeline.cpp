#pragma once

#include "SHaRCPipeline.h"

void SHaRCPipeline::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SHaRCHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SHaRCHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	heap->CreateTable(
		SHaRCHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SHaRCHeap::Slot::SHaRCHashEntries, 1 },
			{ SHaRCHeap::Slot::SHaRCAccumulation, 1 },
			{ SHaRCHeap::Slot::SHaRCResolved, 1 } });

	auto rootParameters = heap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0);
	rootParameters.push_back(constantRootParam);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig = nullptr;
	winrt::com_ptr<ID3DBlob> errorBlob = nullptr;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignature.put())));
	DX::ThrowIfFailed(rootSignature->SetName(L"Compute Root Signature - SHaRC"));
}

void SHaRCPipeline::CompileShaders(ID3D12Device5* device)
{
	winrt::com_ptr<IDxcBlob> shaderBlob = nullptr;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/SharcResolveCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - SHaRC"));
}

void SHaRCPipeline::SetupResources(ID3D12Device5* device)
{
	sharcHashEntriesBuffer = eastl::make_unique<DX12::StructuredBuffer<uint64_t>>(device, MAX_CAPACITY, true);
	sharcHashEntriesBuffer->SetName(L"SHaRC HashEntries Buffer");

	sharcLockBuffer = eastl::make_unique<DX12::StructuredBuffer<uint>>(device, MAX_CAPACITY, true);
	sharcLockBuffer->SetName(L"SHaRC Lock Buffer");

	sharcAccumulationBuffer = eastl::make_unique<DX12::StructuredBuffer<SharcAccumulationData>>(device, MAX_CAPACITY, true);
	sharcAccumulationBuffer->SetName(L"SHaRC Accumulation Buffer");

	sharcResolvedBuffer = eastl::make_unique<DX12::StructuredBuffer<SharcPackedData>>(device, MAX_CAPACITY, true);
	sharcResolvedBuffer->SetName(L"SHaRC Resolved Buffer");
}

void SHaRCPipeline::CreateUAVs(CD3DX12_CPU_DESCRIPTOR_HANDLE hashEntries, CD3DX12_CPU_DESCRIPTOR_HANDLE lock, CD3DX12_CPU_DESCRIPTOR_HANDLE accumulation, CD3DX12_CPU_DESCRIPTOR_HANDLE resolved)
{
	// UAVs for resolve
	sharcHashEntriesBuffer->CreateUAV(heap->CPUHandle(SHaRCHeap::Slot::SHaRCHashEntries));
	sharcAccumulationBuffer->CreateUAV(heap->CPUHandle(SHaRCHeap::Slot::SHaRCAccumulation));
	sharcResolvedBuffer->CreateUAV(heap->CPUHandle(SHaRCHeap::Slot::SHaRCResolved));

	// UAVs for RT
	sharcHashEntriesBuffer->CreateUAV(hashEntries);
	sharcLockBuffer->CreateUAV(lock);
	sharcAccumulationBuffer->CreateUAV(accumulation);
	sharcResolvedBuffer->CreateUAV(resolved);
}

void SHaRCPipeline::Resolve(ID3D12GraphicsCommandList4* commandList)
{
	CD3DX12_RESOURCE_BARRIER uavBarrier[3] = {
		CD3DX12_RESOURCE_BARRIER::UAV(sharcHashEntriesBuffer->resource.get()),
		CD3DX12_RESOURCE_BARRIER::UAV(sharcAccumulationBuffer->resource.get()),
		CD3DX12_RESOURCE_BARRIER::UAV(sharcResolvedBuffer->resource.get())
	};

	commandList->ResourceBarrier(_countof(uavBarrier), uavBarrier);

	const uint dispatchSize = DivideRoundUp(MAX_CAPACITY, GROUP_SIZE);
	commandList->Dispatch(dispatchSize, 1, 1);

	commandList->ResourceBarrier(_countof(uavBarrier), uavBarrier);
}
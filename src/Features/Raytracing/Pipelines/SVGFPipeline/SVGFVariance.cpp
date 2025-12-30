#include "SVGFVariance.h"

void SVGFVariance::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SVGFVarianceHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SVGFVarianceHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	heap->CreateTable(
		SVGFVarianceHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SVGFVarianceHeap::Slot::Variance, 1 } });

	heap->CreateTable(
		SVGFVarianceHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SVGFVarianceHeap::Slot::History, 1 },
			{ SVGFVarianceHeap::Slot::Moments, 1 },
			{ SVGFVarianceHeap::Slot::NormalRoughness, 1 },
			{ SVGFVarianceHeap::Slot::Temporal, 1 },
			{ SVGFVarianceHeap::Slot::Depth, 1 } });

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
	DX::ThrowIfFailed(rootSignature->SetName(L"Compute Root Signature - SVGF Variance"));
}

void SVGFVariance::CompileShaders(ID3D12Device5* device)
{
	winrt::com_ptr<IDxcBlob> shaderBlob = nullptr;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/Denoiser/SVGF/VarianceCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - SVGF Variance"));
}

void SVGFVariance::RegisterResources(ID3D12Device5* device,
	DX12::Texture2D* varianceTexture,
	DX12::Texture2D* historyTexture,
	DX12::Texture2D* momentsTexture,
	ID3D12Resource* normalRoughnessResource,
	DX12::Texture2D* temporalTexture,
	ID3D12Resource* depthResource)
{
	varianceTexture->CreateUAV(heap->CPUHandle(SVGFVarianceHeap::Slot::Variance));

	historyTexture->CreateSRV(heap->CPUHandle(SVGFVarianceHeap::Slot::History));
	momentsTexture->CreateSRV(heap->CPUHandle(SVGFVarianceHeap::Slot::Moments));

	CreateTexture2DSRV(device, normalRoughnessResource, heap->CPUHandle(SVGFVarianceHeap::Slot::NormalRoughness));

	temporalTexture->CreateSRV(heap->CPUHandle(SVGFVarianceHeap::Slot::Temporal));

	CreateTexture2DSRV(device, depthResource, heap->CPUHandle(SVGFVarianceHeap::Slot::Depth));
}

void SVGFVariance::Dispatch(ID3D12GraphicsCommandList4* commandList, uint2 dispatchCount, ID3D12Resource* frameBuffer)
{
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetComputeRootSignature(rootSignature.get());

	auto* pHeap = heap->Heap();
	commandList->SetDescriptorHeaps(1, &pHeap);

	commandList->SetComputeRootDescriptorTable(0, heap->TableGPUHandle(SVGFVarianceHeap::Table::UAV));

	commandList->SetComputeRootDescriptorTable(1, heap->TableGPUHandle(SVGFVarianceHeap::Table::SRV));

	commandList->SetComputeRootConstantBufferView(2, frameBuffer->GetGPUVirtualAddress());

	commandList->Dispatch(dispatchCount.x, dispatchCount.y, 1);
}
#include "SVGFTemporal.h"

void SVGFTemporal::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SVGFTemporalHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SVGFTemporalHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	heap->CreateTable(
		SVGFTemporalHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SVGFTemporalHeap::Slot::Temporal, 1 },
			{ SVGFTemporalHeap::Slot::Moments, 1 } });

	heap->CreateTable(
		SVGFTemporalHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SVGFTemporalHeap::Slot::History, 1 },
			{ SVGFTemporalHeap::Slot::MotionVectors, 1 },
			{ SVGFTemporalHeap::Slot::NormalRoughness, 1 },
			{ SVGFTemporalHeap::Slot::Color, 1 },
			{ SVGFTemporalHeap::Slot::Depth, 1 },
			{ SVGFTemporalHeap::Slot::HistoryMoments, 1 },
			{ SVGFTemporalHeap::Slot::HistoryNormals, 1 } });

	auto rootParameters = heap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0);
	rootParameters.push_back(constantRootParam);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig = nullptr;
	winrt::com_ptr<ID3DBlob> errorBlob = nullptr;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignature.put())));
	DX::ThrowIfFailed(rootSignature->SetName(L"Compute Root Signature - SVGF Temporal"));
}

void SVGFTemporal::CompileShaders(ID3D12Device5* device)
{
	winrt::com_ptr<IDxcBlob> shaderBlob = nullptr;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/Denoiser/SVGF/TemporalCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - SVGF Temporal"));
}

void SVGFTemporal::RegisterResources(ID3D12Device5* device,
	DX12::Texture2D* temporalTexture,
	DX12::Texture2D* momentsTexture,
	DX12::Texture2D* historyTexture,
	ID3D12Resource* motionVectorResource,
	ID3D12Resource* normalRoughnessResource,
	ID3D12Resource* colorResource,
	ID3D12Resource* depthResource,
	DX12::Texture2D* historyMomentsTexture,
	DX12::Texture2D* historyNormalsTexture)
{
	temporalTexture->CreateUAV(heap->CPUHandle(SVGFTemporalHeap::Slot::Temporal));
	momentsTexture->CreateUAV(heap->CPUHandle(SVGFTemporalHeap::Slot::Moments));

	historyTexture->CreateSRV(heap->CPUHandle(SVGFTemporalHeap::Slot::History));

	CreateTexture2DSRV(device, motionVectorResource, heap->CPUHandle(SVGFTemporalHeap::Slot::MotionVectors));
	CreateTexture2DSRV(device, normalRoughnessResource, heap->CPUHandle(SVGFTemporalHeap::Slot::NormalRoughness));
	CreateTexture2DSRV(device, colorResource, heap->CPUHandle(SVGFTemporalHeap::Slot::Color));
	CreateTexture2DSRV(device, depthResource, heap->CPUHandle(SVGFTemporalHeap::Slot::Depth));

	historyMomentsTexture->CreateSRV(heap->CPUHandle(SVGFTemporalHeap::Slot::HistoryMoments));
	historyNormalsTexture->CreateSRV(heap->CPUHandle(SVGFTemporalHeap::Slot::HistoryNormals));
}

void SVGFTemporal::Dispatch(ID3D12GraphicsCommandList4* commandList, uint2 dispatchCount, ID3D12Resource* frameBuffer)
{
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetComputeRootSignature(rootSignature.get());

	auto* pHeap = heap->Heap();
	commandList->SetDescriptorHeaps(1, &pHeap);

	commandList->SetComputeRootDescriptorTable(0, heap->TableGPUHandle(SVGFTemporalHeap::Table::UAV));

	commandList->SetComputeRootDescriptorTable(1, heap->TableGPUHandle(SVGFTemporalHeap::Table::UAV));

	commandList->SetComputeRootConstantBufferView(2, frameBuffer->GetGPUVirtualAddress());

	commandList->Dispatch(dispatchCount.x, dispatchCount.y, 1);
}
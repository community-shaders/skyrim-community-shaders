#include "SVGFSpatial.h"

void SVGFSpatial::CreateRootSignature(ID3D12Device5* device)
{
	heap = eastl::make_unique<DX12::DescriptorHeap<SVGFSpatialHeap>>(
		device,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SVGFSpatialHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	heap->CreateTable(
		SVGFSpatialHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { SVGFSpatialHeap::Slot::ColorVariance, 1 } });

	heap->CreateTable(
		SVGFSpatialHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { SVGFSpatialHeap::Slot::History, 1 },
			{ SVGFSpatialHeap::Slot::MotionVectors, 1 },
			{ SVGFSpatialHeap::Slot::NormalRoughness, 1 },
			{ SVGFSpatialHeap::Slot::VarianceColor, 1 },
			{ SVGFSpatialHeap::Slot::Depth, 1 } });

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
	DX::ThrowIfFailed(rootSignature->SetName(L"Compute Root Signature - SVGF Spatial"));
}

void SVGFSpatial::CompileShaders(ID3D12Device5* device)
{
	winrt::com_ptr<IDxcBlob> shaderBlob = nullptr;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/Denoiser/SVGF/SpatialCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignature.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.put())));
	DX::ThrowIfFailed(pipelineState->SetName(L"Compute Pipeline - SVGF Spatial"));
}

void SVGFSpatial::RegisterResources(ID3D12Device5* device,
	ID3D12Resource* colorResource,
	DX12::Texture2D* historyTexture,
	ID3D12Resource* motionVectorResource,
	ID3D12Resource* normalRoughnessResource,
	DX12::Texture2D* varianceTexture,
	ID3D12Resource* depthResource)
{
	CreateTexture2DUAV(device, colorResource, heap->CPUHandle(SVGFSpatialHeap::Slot::ColorVariance));

	historyTexture->CreateSRV(heap->CPUHandle(SVGFSpatialHeap::Slot::History));

	CreateTexture2DSRV(device, motionVectorResource, heap->CPUHandle(SVGFSpatialHeap::Slot::MotionVectors));
	CreateTexture2DSRV(device, normalRoughnessResource, heap->CPUHandle(SVGFSpatialHeap::Slot::NormalRoughness));

	varianceTexture->CreateSRV(heap->CPUHandle(SVGFSpatialHeap::Slot::VarianceColor));

	CreateTexture2DSRV(device, depthResource, heap->CPUHandle(SVGFSpatialHeap::Slot::Depth));
}

void SVGFSpatial::Dispatch(ID3D12GraphicsCommandList4* commandList, uint atrousIterations, uint2 dispatchCount, DX12::StructuredBufferUpload<SVGF>* frameBuffer, ID3D12Resource* colorResource, ID3D12Resource* varianceResource)
{
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetComputeRootSignature(rootSignature.get());

	auto* pHeap = heap->Heap();
	commandList->SetDescriptorHeaps(1, &pHeap);

	commandList->SetComputeRootDescriptorTable(0, heap->TableGPUHandle(SVGFSpatialHeap::Table::UAV));

	commandList->SetComputeRootDescriptorTable(1, heap->TableGPUHandle(SVGFSpatialHeap::Table::SRV));

	commandList->SetComputeRootConstantBufferView(2, frameBuffer->resource->GetGPUVirtualAddress());

    for (uint i = 0; i < atrousIterations; i++) {
		frameBuffer->Upload(commandList, i + 1);

		bool even = (i % 2 == 0);

		commandList->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		CD3DX12_RESOURCE_BARRIER spatialUAVBarrier[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(even ? colorResource : varianceResource)
		};

		commandList->ResourceBarrier(_countof(spatialUAVBarrier), spatialUAVBarrier);

		// We will use CopyResource to simulate swapping
		if (even) {
			commandList->CopyResource(varianceResource, colorResource);
		} else {
			commandList->CopyResource(colorResource, varianceResource);
		}
	}

	/*if (atrousIterations % 2 == 0) {
		commandList->CopyResource(colorTexture, varianceTexture);
	}*/
}
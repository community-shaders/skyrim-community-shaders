#include "SVGFPipeline.h"

void SVGFPipeline::Initialize()
{
	temporalPipeline = eastl::make_unique<SVGFTemporal>();
	variancePipeline = eastl::make_unique<SVGFVariance>();
	spatialPipeline = eastl::make_unique<SVGFSpatial>();
}

void SVGFPipeline::CreateRootSignature(ID3D12Device5* device)
{
	temporalPipeline->CreateRootSignature(device);
	variancePipeline->CreateRootSignature(device);
	spatialPipeline->CreateRootSignature(device);
}

void SVGFPipeline::CompileShaders(ID3D12Device5* device)
{
	temporalPipeline->CompileShaders(device);
	variancePipeline->CompileShaders(device);
	spatialPipeline->CompileShaders(device);
}

void SVGFPipeline::SetupResources(ID3D12Device5* device)
{
	frameData = eastl::make_unique<SVGF>();
	frameBuffer = eastl::make_unique<DX12::StructuredBufferUpload<SVGF>>(device, 1, false, MAX_ATROUS_ITERATIONS + 1);
}

void SVGFPipeline::SetupTextureResources(ID3D12Device5* device, uint2 size, ID3D12Resource* depthResource, ID3D12Resource* motionVectorResource, ID3D12Resource* normalRoughnessResource, ID3D12Resource* colorResource)
{
	temporalTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	momentsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	varianceTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	historyMomentsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	historyNormalsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	historyTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	RegisterResources(device, depthResource, motionVectorResource, normalRoughnessResource, colorResource);
}

void SVGFPipeline::RegisterResources(ID3D12Device5* device, ID3D12Resource* depthResource, ID3D12Resource* motionVectorResource, ID3D12Resource* normalRoughnessResource, ID3D12Resource* colorResource) const
{
	temporalPipeline->RegisterResources(device,
		temporalTexture.get(),
		momentsTexture.get(),
		historyTexture.get(),
		motionVectorResource,
		normalRoughnessResource,
		colorResource,
		depthResource,
		historyMomentsTexture.get(),
		historyNormalsTexture.get());

	variancePipeline->RegisterResources(device,
		varianceTexture.get(),
		historyTexture.get(),
		momentsTexture.get(),
		normalRoughnessResource,
		temporalTexture.get(),
		depthResource);

	spatialPipeline->RegisterResources(device,
		colorResource,
		historyTexture.get(),
		motionVectorResource,
		normalRoughnessResource,
		varianceTexture.get(),
		depthResource);
}

void SVGFPipeline::Denoise(ID3D12GraphicsCommandList4* commandList, uint2 renderSize, Settings settings, ID3D12Resource* colorTexture) const
{
	frameData->InvMaxAccumulatedFrames = 1.0f / (settings.MaxAccumulatedFrames + 1.0f);
	frameData->AtrousIterations = settings.AtrousIterations;
	frameData->ColorPhi = settings.ColorPhi;
	frameData->NormalPhi = settings.NormalPhi;

	frameData->Resolution = renderSize;
	frameData->ResolutionRcp = float2(1.0f / static_cast<float>(renderSize.x), 1.0f / static_cast<float>(renderSize.y));

	frameData->CameraProjUnjitteredInverse = globals::game::frameBufferCached.GetCameraProjUnjitteredInverse().Transpose();
	frameData->CameraViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	frameData->CameraPreviousViewProjUnjittered = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered().Transpose();

	for (uint i = 0; i < settings.AtrousIterations + 1; i++) {
		if (i > 0) {
			frameData->AtrousIterations = i - 1;
		}

		frameBuffer->Update(frameData.get(), sizeof(SVGF), i);
	}
	
	frameBuffer->Upload(commandList);

	uint2 dispatchCount = { DivideRoundUp(renderSize.x, 8u), DivideRoundUp(renderSize.y, 8u) };
	auto* frameResource = frameBuffer->resource.get();

	temporalPipeline->Dispatch(commandList, dispatchCount, frameResource);

	CD3DX12_RESOURCE_BARRIER temporalUAVBarrier[] = {
		CD3DX12_RESOURCE_BARRIER::UAV(temporalTexture->resource.get()),
		CD3DX12_RESOURCE_BARRIER::UAV(momentsTexture->resource.get())
	};
	commandList->ResourceBarrier(_countof(temporalUAVBarrier), temporalUAVBarrier);

	variancePipeline->Dispatch(commandList, dispatchCount, frameResource);

	CD3DX12_RESOURCE_BARRIER varianceUAVBarrier[] = {
		CD3DX12_RESOURCE_BARRIER::UAV(varianceTexture->resource.get())
	};
	commandList->ResourceBarrier(_countof(varianceUAVBarrier), varianceUAVBarrier);

	spatialPipeline->Dispatch(commandList, settings.AtrousIterations, dispatchCount, frameBuffer.get(), varianceTexture->resource.get(), colorTexture);
}
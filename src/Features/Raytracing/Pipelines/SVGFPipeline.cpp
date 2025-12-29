#include "SVGFPipeline.h"

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
	uint2 size = uint2(1920, 1080);

	temporalTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	momentsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	varianceTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	historyMomentsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	historyNormalsTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	historyTexture = eastl::make_unique<DX12::Texture2D>(device, size.x, size.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);

	/*temporalPipeline->SetupResources(device);
	variancePipeline->SetupResources(device);
	spatialPipeline->SetupResources(device);*/
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
}

void SVGFPipeline::Denoise(ID3D12GraphicsCommandList4* commandList)
{
	temporalPipeline->Dispatch(commandList, nullptr);
	variancePipeline->Dispatch(commandList, nullptr);
	spatialPipeline->Dispatch(commandList, nullptr);
}
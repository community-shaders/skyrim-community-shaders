#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFSpatial.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFTemporal.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFVariance.h"
#include "Features/Raytracing/Types.h"

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

struct SVGFPipeline : IPipeline
{
	static constexpr uint MAX_ATROUS_ITERATIONS = 5;

	eastl::unique_ptr<SVGFTemporal> temporalPipeline = nullptr;
	eastl::unique_ptr<SVGFVariance> variancePipeline = nullptr;
	eastl::unique_ptr<SVGFSpatial> spatialPipeline = nullptr;

	eastl::unique_ptr<DX12::Texture2D> temporalTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> momentsTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> varianceTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> historyMomentsTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> historyNormalsTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> historyTexture = nullptr;

	eastl::unique_ptr<SVGF> frameData = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<SVGF>> frameBuffer = nullptr;

	struct Settings
	{
		uint MaxAccumulatedFrames = 16;
		uint AtrousIterations = 3;
		float ColorPhi = 0.5f;
		float NormalPhi = 512.0f;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, MaxAccumulatedFrames, AtrousIterations, ColorPhi, NormalPhi)
	};

	void Initialize() override;
	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void Denoise(ID3D12GraphicsCommandList4* commandList, uint2 renderSize, Settings settings, ID3D12Resource* colorTexture) const;	
	void SetupTextureResources(ID3D12Device5* device, uint2 size, ID3D12Resource* depthResource, ID3D12Resource* motionVectorResource, ID3D12Resource* normalRoughnessResource, ID3D12Resource* colorResource);
	void RegisterResources(ID3D12Device5* device, ID3D12Resource* depthResource, ID3D12Resource* motionVectorResource, ID3D12Resource* normalRoughnessResource, ID3D12Resource* colorResource) const;
};
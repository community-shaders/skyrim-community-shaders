#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include <d3d11_4.h>
#include <d3d12.h>

#include <NRD.h>
#include "Raytracing/Denoiser/NRD/Shared.hlsli"

#include "Features/Raytracing/Types.h"

#include "Features/Upscaling/DX12SwapChain.h"

// NRD sample doesn't use several instances of the same denoiser in one NRD instance (like REBLUR_DIFFUSE x 3),
// thus we can use fields of "nrd::Denoiser" enum as unique identifiers
#define NRD_ID(x) nrd::Identifier(nrd::Denoiser::x)

#define NRD_COMBINED 1

#if (SIGMA_TRANSLUCENCY == 1)
#	define SIGMA_VARIANT nrd::Denoiser::SIGMA_SHADOW_TRANSLUCENCY
#else
#	define SIGMA_VARIANT nrd::Denoiser::SIGMA_SHADOW
#endif

struct NDRSubPipeline : IPipeline
{
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	winrt::com_ptr<ID3D12PipelineState> pipelineState = nullptr;
};

struct NRDPipeline : IPipeline
{
    // NRD
	nrd::CommonSettings commonSettings = {};

	nrd::SigmaSettings m_SigmaSettings = {};
	nrd::ReferenceSettings m_ReferenceSettings = {};

	//eastl::unordered_map<nrd::Denoiser, nrd::Identifier> denoisers;
	eastl::vector<eastl::unique_ptr<IPipeline>> pipelines;

	nrd::Instance* instance = nullptr;

	float2 jitter = { 0, 0 };
	uint frameIndex = 0;

	static constexpr float kMaxSceneDistance = 50000.0f;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
		nrd::RelaxSettings, 
		checkerboardMode, 
		hitDistanceReconstructionMode, 
		diffuseMaxAccumulatedFrameNum, 
		specularMaxAccumulatedFrameNum,
		diffuseMaxFastAccumulatedFrameNum,
		specularMaxFastAccumulatedFrameNum,
		fastHistoryClampingSigmaScale)

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
		nrd::ReblurSettings,
		checkerboardMode,
		hitDistanceReconstructionMode,
		maxAccumulatedFrameNum,
		maxFastAccumulatedFrameNum,
		maxStabilizedFrameNum,
		fastHistoryClampingSigmaScale)

	struct Settings
	{
		nrd::Denoiser Denoiser = nrd::Denoiser::RELAX_DIFFUSE;
		nrd::RelaxSettings RelaxSettings = {};
		nrd::ReblurSettings ReblurSettings = {};

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, RelaxSettings, ReblurSettings)
	} settings;

	void CompileShaders();
	virtual void SetupResources(ID3D12Device5* device) override;
	void UpdateCommonSettings();
	void Denoise(ID3D12GraphicsCommandList4* commandList);	
	void SetupTextureResources(uint2 size);
};
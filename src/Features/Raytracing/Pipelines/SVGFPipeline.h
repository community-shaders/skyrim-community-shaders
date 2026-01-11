#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include <d3d11_4.h>
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

#include "Buffer.h"

#include "Features/Upscaling/DX12SwapChain.h"

struct SVGFPipeline
{
	// Diffuse and Specular
	static constexpr uint HISTORY_TEXTURES = 2;

	eastl::unique_ptr<Texture2D> temporalTexture = nullptr;
	eastl::unique_ptr<Texture2D> momentsTexture = nullptr;
	eastl::unique_ptr<Texture2D> varianceTexture = nullptr;
	eastl::array<eastl::unique_ptr<Texture2D>, HISTORY_TEXTURES> historyMomentsTexture;
	eastl::unique_ptr<Texture2D> historyDepthTexture = nullptr;
	eastl::unique_ptr<Texture2D> historyNormalsTexture = nullptr;
	eastl::array<eastl::unique_ptr<Texture2D>, HISTORY_TEXTURES> historyTexture;

	eastl::unique_ptr<Texture2D> depthLinearTexture = nullptr;

	eastl::unique_ptr<SVGF> frameData = nullptr;
	eastl::unique_ptr<ConstantBuffer> frameBuffer = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> temporalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> varianceCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialDiffuseCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialSpecularCS = nullptr;

	struct Settings
	{
		uint AlphaFrames = 20;
		uint MomentsAlphaFrames = 10;
		uint AtrousIterations = 2;
		float ColorPhi = 1.0f;
		float NormalPhi = 256.0f;
		float DepthPhi = 0.05f;
		float DepthThreshold = 0.1f;
		uint NormalThreshold = 30;
		uint HistoryThreshold = 2;
		bool Variance = true;
		bool Spatial = true;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, AlphaFrames, MomentsAlphaFrames, AtrousIterations, ColorPhi, NormalPhi, DepthPhi, DepthThreshold, NormalThreshold, HistoryThreshold, Variance, Spatial)
	};

	void CompileShaders();
	void SetupResources();
	void Denoise(ID3D11DeviceContext4* context, uint2 renderSize, Settings settings, WrappedResource* normalRoughness, WrappedResource* color, const bool diffuse = true) const;

	void SetupTextureResources(uint2 size);
};
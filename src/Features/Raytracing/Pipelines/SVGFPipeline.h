#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include <d3d11_4.h>
#include <d3d12.h>

#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFSpatial.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFTemporal.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline/SVGFVariance.h"
#include "Features/Raytracing/Types.h"

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

#include "Buffer.h"

#include "Features/Upscaling/DX12SwapChain.h"

struct SVGFPipeline
{
	eastl::unique_ptr<Texture2D> temporalTexture = nullptr;
	eastl::unique_ptr<Texture2D> momentsTexture = nullptr;
	eastl::unique_ptr<Texture2D> varianceTexture = nullptr;
	eastl::unique_ptr<Texture2D> historyMomentsTexture = nullptr;
	eastl::unique_ptr<Texture2D> historyNormalsTexture = nullptr;
	eastl::unique_ptr<Texture2D> historyTexture = nullptr;

	eastl::unique_ptr<SVGF> frameData = nullptr;
	eastl::unique_ptr<ConstantBuffer> frameBuffer = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> temporalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> varianceCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialDiffuseCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialSpecularCS = nullptr;

	struct Settings
	{
		uint MaxAccumulatedFrames = 16;
		uint AtrousIterations = 3;
		float ColorPhi = 0.5f;
		float NormalPhi = 512.0f;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, MaxAccumulatedFrames, AtrousIterations, ColorPhi, NormalPhi)
	};

	void CompileShaders();
	void SetupResources();
	void Denoise(ID3D11DeviceContext4* context, uint2 renderSize, Settings settings, WrappedResource* normalRoughness, WrappedResource* colorResource, const bool diffuse = true) const;
	void SetupTextureResources(uint2 size);
};
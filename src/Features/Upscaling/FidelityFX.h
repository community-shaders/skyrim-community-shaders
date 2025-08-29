#pragma once

#include <d3d12.h>
#include <winrt/base.h>

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>

#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>
#include <FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp>
#include <FidelityFX/upscalers/include/ffx_upscale.hpp>

#include "../../Buffer.h"
#include "../../State.h"

class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";


	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;
	ffx::Context upscalingContext;

	bool featureFSR3FG = false;
	bool featureFSR3 = false;

	// Track if FidelityFX is currently being used for frame generation
	bool isFrameGenActive = false;

	// Cached DLL version info for FidelityFX plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	std::string versionInfo;

	void LoadFFX();
	void QueryVersion();
	void SetupFrameGeneration();
	void Present(bool a_useFrameGeneration);

	void CreateFSRResources();
	void DestroyFSRResources();
	float GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset);
	void Upscale(
		ID3D12Resource* a_inputColorTexture,
		ID3D12Resource* a_motionVectorTexture,
		ID3D12Resource* a_depthTexture,
		ID3D12Resource* a_reactiveMask,
		ID3D12Resource* a_transparencyCompositionMask,
		ID3D12Resource* a_outputTexture,
		ID3D12GraphicsCommandList* a_commandList,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight,
		float2 a_jitter);
};

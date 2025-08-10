#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>
#include <ffx_upscale.hpp>

#include "Buffer.h"
#include "State.h"
#include <d3d12.h>
#include <winrt/base.h>

class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\SKSE\\Plugins\\FidelityFX";

	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

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

	void LoadFFX();
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
		ID3D12Resource* a_outputTexture,
		ID3D12GraphicsCommandList* a_commandList,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight,
		float2 a_jitter);
};

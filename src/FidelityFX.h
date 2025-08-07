#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>

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

	bool featureFSR3FG = false;  // whether enabled
	bool featureFSR3 = false;   // FSR 3 upscaling feature

	// Track if FidelityFX is currently being used for frame generation
	bool isFrameGenActive = false;

	// Cached DLL version info for FidelityFX plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadFFX();
	void SetupFrameGeneration();
	void Present(bool a_useFrameGeneration);
};

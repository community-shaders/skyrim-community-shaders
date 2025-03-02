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

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;

	FfxFsr3Context fsrContext;

	void LoadFFX();

	void SetupFrameGeneration();

	void Present(bool a_useFrameGeneration);

	void CreateFSRResources();
	void DestroyFSRResources();
	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, float2 a_jitter, bool a_reset, float a_sharpness);
};

#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <ffx_api.h>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx/ffx_api_dx12.h>

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

	HMODULE dll = NULL;

	ffxFunctions* fidelityFXDX12;

	FfxFsr3Context fsrContext;

	void Init();

	void CreateFSRResources();
	void DestroyFSRResources();
	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, float2 a_jitter, bool a_reset, float a_sharpness);
};

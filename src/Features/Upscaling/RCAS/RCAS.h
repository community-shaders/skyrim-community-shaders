#pragma once

#include "../../../Buffer.h"
#include "../../../State.h"

#include <d3d11_4.h>
#include <winrt/base.h>

/**
 * @brief Robust Contrast Adaptive Sharpening (RCAS) implementation.
 *
 * Standalone sharpening pass based on AMD FidelityFX FSR1 RCAS algorithm.
 * Used to apply sharpening to DLSS output in HDR space before tonemapping.
 */
class RCAS
{
public:
	RCAS() = default;
	~RCAS();

	/**
	 * @brief Initializes RCAS resources including compute shader and constant buffer.
	 *
	 * Safe to call multiple times - will early-out if already initialized.
	 */
	void Initialize();

	/**
	 * @brief Applies RCAS sharpening to the input texture.
	 *
	 * @param inputTexture SRV of the texture to sharpen (typically kMAIN render target).
	 * @param outputUAV UAV to write sharpened result to.
	 * @param sharpness Sharpening strength (0.0 = no sharpening, higher = more sharp).
	 */
	void ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness);

private:
	void CreateComputeShader();

	winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;
	ConstantBuffer* rcasConfigCB = nullptr;
};

#pragma once

#include "../../../Buffer.h"
#include "../../../State.h"
#include "NIS_Config.h"

#include <d3d11_4.h>
#include <winrt/base.h>

/**
 * @brief Standalone NVIDIA Image Scaling (NIS) sharpening implementation
 *
 * This class provides standalone NIS sharpening functionality without requiring
 * the Streamline SDK. It uses the official NIS SDK files directly.
 */
class NIS
{
public:
	NIS() = default;

	// NIS resources
	winrt::com_ptr<ID3D11ComputeShader> nisComputeShader;
	winrt::com_ptr<ID3D11Texture2D> nisCoefScalerTexture;
	winrt::com_ptr<ID3D11Texture2D> nisCoefUsmTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> nisCoefScalerSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> nisCoefUsmSRV;

	ConstantBuffer* nisConfigCB = nullptr;

	/**
	 * @brief Initialize the standalone NIS implementation
	 */
	void Initialize();

	/**
	 * @brief Apply NIS sharpening to a texture
	 * @param inputTexture Input texture to sharpen
	 * @param outputTexture Output texture for sharpened result
	 * @param sharpness Sharpening strength (0.0 to 1.0)
	 */
	void ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness = 0.15f);

private:
	/**
	 * @brief Create coefficient textures for NIS filtering
	 */
	void CreateCoefficientTextures();

	/**
	 * @brief Compile NIS compute shader
	 */
	void CreateComputeShader();
};
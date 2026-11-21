#pragma once

#include "Buffer.h"

/**
 * @brief Shared max-depth pyramid for conservative occlusion culling.
 * Depth runs from 0 (near) to 1 (far); padding uses far depth.
 */
class HiZPyramid
{
public:
	/**
	 * @brief Builds from main depth or the prepass copy, reusing this frame's cached pyramid.
	 * @param device Device for texture and view creation.
	 * @param ctx Immediate context for compute dispatches.
	 * @param forceRefresh Rebuild after depth changes within the same frame.
	 * @return Whether a pyramid is available for this frame.
	 * @note Restores render targets and clears used SRV/UAV slots. Call before binding
	 * the next pass's compute shader, constant buffers and resources.
	 */
	bool Build(ID3D11Device* device, ID3D11DeviceContext* ctx, bool forceRefresh = false);

	/** @brief Returns this frame's pyramid SRV, or nullptr. */
	ID3D11ShaderResourceView* GetSRV() const;
	/** @brief Reports whether a build succeeded this frame. */
	bool IsValid() const;
	/** @brief Reports whether this frame's pyramid was built from main depth. */
	bool HasCurrentSceneDepth() const { return IsValid() && usingLiveDepth; }
	/** @brief Returns the active base width in texels, rounded up. */
	uint32_t GetWidth() const { return width; }
	/** @brief Returns the active base height in texels, rounded up. */
	uint32_t GetHeight() const { return height; }
	/** @brief Returns the viewport UV-to-texel scale, preserving partial tiles. */
	float2 GetUVScale() const { return uvScale; }
	/** @brief Returns the usable mip count; 1 when SPD is unavailable. */
	uint32_t GetMipCount() const { return spdCS && texture ? texture->desc.MipLevels : 1u; }

	/** @brief Creates the reduction parameter buffer. */
	void SetupResources();
	/** @brief Clears shaders and compilation failures for a retry on next use. */
	void ClearShaderCache();

private:
	static constexpr uint32_t kDownsampleFactor = 4;
	// Padding to 64x64 preserves every texel through six reductions.
	static constexpr uint32_t kOutputMipCount = 6;
	static constexpr uint32_t kTileSize = 64;

	struct alignas(16) BaseParams
	{
		uint32_t srcWidth;
		uint32_t srcHeight;
		uint32_t dstWidth;
		uint32_t dstHeight;
	};
	STATIC_ASSERT_ALIGNAS_16(BaseParams);

	struct alignas(16) SPDParams
	{
		uint32_t sourceWidth;
		uint32_t sourceHeight;
		uint32_t outputMips;
		uint32_t totalGroups;
	};
	STATIC_ASSERT_ALIGNAS_16(SPDParams);

	/** @brief Returns the prepass depth copy used when main depth is unavailable. */
	static ID3D11ShaderResourceView* GetSourceDepthSRV();

	/** @brief Returns the original main-depth SRV, or nullptr. */
	static ID3D11ShaderResourceView* GetLiveDepthSRV();

	/** @brief Creates up to outputMipCount reduction levels, limited by the texture dimensions. */
	bool CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH, uint32_t outputMipCount);

	std::unique_ptr<Texture2D> texture;
	bool usingLiveDepth = false;
	// SPD writes mips 1 and above; the texture owns the mip-0 UAV.
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> outputUAVs;
	winrt::com_ptr<ID3D11ShaderResourceView> mip0SRV;
	std::unique_ptr<ConstantBuffer> paramsCB;

	winrt::com_ptr<ID3D11ComputeShader> baseCS;
	winrt::com_ptr<ID3D11ComputeShader> spdCS;
	// Failed compiles are retried only after ClearShaderCache.
	bool baseCSFailed = false;
	bool spdCSFailed = false;

	uint32_t width = 0;
	uint32_t height = 0;
	float2 uvScale{};
	bool valid = false;
	uint32_t builtFrame = UINT32_MAX;
};

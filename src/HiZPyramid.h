#pragma once

#include "Buffer.h"

/**
 * @brief Max-depth mip pyramid over scene depth, shared by Grass Optimizations and Procedural Grass for occlusion culling.
 * A texel at level N is exactly the farthest depth of everything beneath it.
 */
class HiZPyramid
{
public:
	/**
	 * @brief Rebuilds the pyramid from the live main depth, falling back to the post-Z-prepass copy.
	 * @param device Device used to create the per-mip views when the size changes.
	 * @param ctx Immediate context the reduction dispatches are issued on.
	 * @param forceRefresh Rebuild even when the pyramid was already produced this frame.
	 * @return True when the pyramid is valid for this frame.
	 */
	bool Build(ID3D11Device* device, ID3D11DeviceContext* ctx, bool forceRefresh = false);

	/** @brief Returns the full-chain SRV, or nullptr when the pyramid is not valid this frame. */
	ID3D11ShaderResourceView* GetSRV() const;
	/** @brief Returns true when Build succeeded for the current frame. */
	bool IsValid() const;
	/** @brief Returns true when the current pyramid uses this frame's live opaque depth. */
	bool HasCurrentSceneDepth() const { return IsValid() && usingLiveDepth; }
	/** @brief Returns the base level width in texels. */
	uint32_t GetWidth() const { return width; }
	/** @brief Returns the base level height in texels. */
	uint32_t GetHeight() const { return height; }
	/** @brief Returns the number of trustworthy mip levels; 1 when the reduction chain is absent. */
	uint32_t GetMipCount() const { return spdCS ? mipCount : 1u; }

	/** @brief Creates the reduction parameter buffer. */
	void SetupResources();
	/** @brief Releases the cached compute shaders so they recompile on next use. */
	void ClearShaderCache();

private:
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

	/** @brief Returns the post-Z-prepass depth SRV, the fallback when the live main depth is unavailable. */
	static ID3D11ShaderResourceView* GetSourceDepthSRV();

	/** @brief Returns the live main depth SRV, or nullptr when unavailable. */
	static ID3D11ShaderResourceView* GetLiveDepthSRV();

	/** @brief (Re)creates the pyramid texture and its per-mip views for a new size. */
	bool CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH);

	std::unique_ptr<Texture2D> texture;
	// False when the base pass read the post-Z-prepass fallback.
	bool usingLiveDepth = false;
	std::array<uint32_t, 10> lastLogKey{};
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
	winrt::com_ptr<ID3D11ShaderResourceView> mip0SRV;
	std::unique_ptr<ConstantBuffer> paramsCB;

	winrt::com_ptr<ID3D11ComputeShader> baseCS;
	winrt::com_ptr<ID3D11ComputeShader> spdCS;
	// Failed compiles are retried only after ClearShaderCache.
	bool baseCSFailed = false;
	bool spdCSFailed = false;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t paddedWidth = 0;
	uint32_t paddedHeight = 0;
	uint32_t mipCount = 1;
	bool valid = false;
	uint32_t builtFrame = UINT32_MAX;

	static constexpr uint32_t kDownsampleFactor = 4;
	// A 64x64-padded base halves exactly for six levels; a seventh could floor an odd level and drop its last row.
	static constexpr uint32_t kExactMips = 6;
	static constexpr uint32_t tileSize = 64;
};

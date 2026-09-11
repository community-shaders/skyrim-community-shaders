#pragma once

#include "Buffer.h"

/** @brief Max-depth mip pyramid over the scene depth copy, used to occlusion-cull grass instances. A texel at level N is exactly the farthest depth of everything beneath it */
class HiZPyramid
{
public:
	/**
	 * @brief Rebuilds the pyramid from the current scene depth copy.
	 * @param device Device used to create the per-mip views when the size changes.
	 * @param ctx Immediate context the reduction dispatches are issued on.
	 * @return True when the pyramid is valid for this frame.
	 */
	bool Build(ID3D11Device* device, ID3D11DeviceContext* ctx);

	/** @brief Marks the pyramid unusable for this frame without releasing anything. */
	void Invalidate() { valid = false; }

	/** @brief Returns the full-chain SRV, or nullptr when the pyramid is not valid this frame. */
	ID3D11ShaderResourceView* GetSRV() const { return valid && texture ? texture->srv.get() : nullptr; }
	/** @brief Returns true when Build succeeded for the current frame. */
	bool IsValid() const { return valid; }
	/** @brief Returns the base level width in texels. */
	uint32_t GetWidth() const { return width; }
	/** @brief Returns the base level height in texels. */
	uint32_t GetHeight() const { return height; }
	/** @brief Returns the number of trustworthy mip levels; 1 when the reduction chain is absent. */
	uint32_t GetMipCount() const { return spdCS ? mipCount : 1u; }
	/** @brief Returns how many nominal screen pixels one base-level texel covers, scaled by dynamic resolution. */
	float GetTexelPixels() const { return texelPixels; }

	/** @brief Creates the parameter constant buffer. Called from the feature's SetupResources. */
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

	/** @brief Returns the SRV of the depth copy that is current at grass draw time. */
	static ID3D11ShaderResourceView* GetSourceDepthSRV();

	/**
	 * @brief Returns the SRV of the live main depth, which holds this frame's opaque geometry.
	 * @return The SRV, or nullptr when it is unavailable and the caller should fall back.
	 */
	static ID3D11ShaderResourceView* GetLiveDepthSRV();

	/** @brief (Re)creates the pyramid texture and its per-mip views for a new size. */
	bool CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH);

	std::unique_ptr<Texture2D> texture;
	// False when the live target was unavailable and the base pass fell back to the stale prepass copy.
	bool usingLiveDepth = false;
	std::array<uint32_t, 10> lastLogKey{};
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
	winrt::com_ptr<ID3D11ShaderResourceView> mip0SRV;
	std::unique_ptr<ConstantBuffer> paramsCB;

	ID3D11ComputeShader* baseCS = nullptr;
	ID3D11ComputeShader* spdCS = nullptr;

	uint32_t width = 0;
	uint32_t height = 0;
	float texelPixels = (float)kDownsampleFactor;
	uint32_t paddedWidth = 0;
	uint32_t paddedHeight = 0;
	uint32_t mipCount = 1;
	bool valid = false;

	static constexpr uint32_t kDownsampleFactor = 4;
	// SPD reduces a 64x64 tile wholly in LDS, so a base padded to that granularity halves exactly for
	// six levels. The seventh would floor an odd mip, dropping a row and underestimating the max.
	static constexpr uint32_t kExactMips = 6;
	static constexpr uint32_t tileSize = 64;
};

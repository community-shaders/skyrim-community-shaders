#pragma once

#include "Buffer.h"

/** @brief Stores conservative maximum-depth mips for grass occlusion culling. */
class HiZPyramid
{
public:
	/** @brief Builds this frame's max-depth pyramid and returns whether it is valid. */
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
	/** @brief Returns the valid mip count, including only the base level if SPD is unavailable. */
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

	static ID3D11ShaderResourceView* GetLiveDepthSRV();

	bool CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH);

	std::unique_ptr<Texture2D> texture;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
	winrt::com_ptr<ID3D11ShaderResourceView> mip0SRV;
	std::unique_ptr<ConstantBuffer> paramsCB;

	winrt::com_ptr<ID3D11ComputeShader> baseCS;
	winrt::com_ptr<ID3D11ComputeShader> spdCS;
	// Latch failed compiles until ClearShaderCache() so the per-frame build doesn't retry and re-log.
	bool baseCSFailed = false;
	bool spdCSFailed = false;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t paddedWidth = 0;
	uint32_t paddedHeight = 0;
	uint32_t mipCount = 1;
	bool valid = false;

	inline static constexpr uint32_t kDownsampleFactor = 4;
	// SPD preserves all edge texels for six reductions of a padded 64x64 tile; a seventh may drop an odd row.
	inline static constexpr uint32_t kExactMips = 6;
	inline static constexpr uint32_t tileSize = 64;
};

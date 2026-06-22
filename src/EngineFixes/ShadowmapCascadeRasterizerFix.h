#pragma once

/**
 * @brief Overrides shadow cascade rasterizer states to fix peter-panning and self-shadowing.
 *
 * The vanilla engine shares a single set of rasterizer states across all shadow cascades.
 * This fix clones those states per-cascade and applies tuned depth-bias, depth-bias-clamp,
 * and slope-scaled-bias values to reduce shadow acne on near cascades and peter-panning
 * on far cascades.
 */
struct ShadowmapRasterizerFix : EngineFix
{
	/** @brief Returns the human-readable name of this fix. */
	std::string GetName() override { return "Shadowmap Cascade Rasterizer Fix"; }

	/** @brief Installs the per-cascade rasterizer state hook into the shadow rendering pipeline. */
	void Install() override;

	using RasterStateArray = ID3D11RasterizerState* [2][3][12][2];

	/**
	 * @brief Clones the global rasterizer state array and applies cascade-specific depth bias settings.
	 * @param inputArray Pointer to the game's global rasterizer state array.
	 * @param cascade Zero-based cascade index whose descriptor values are applied to the cloned states.
	 */
	static void CloneRasterStates(RasterStateArray* inputArray, int cascade);

	static constexpr uint maxCascades = 3;
	static inline uint numCascades = 0;

	static inline RasterStateArray* gRasterStates = nullptr;
	static inline RasterStateArray backupGameRasterStates = {};
	static inline RasterStateArray shadowmapRasterStates[maxCascades] = {};

	static constexpr int firstCascadeDepthBias = 160;
	static constexpr float firstCascadeDepthBiasClamp = 0.015f;
	static constexpr float firstCascadeSlopeScaleBias = 3.2f;

	static constexpr int secondCascadeDepthBias = 100;
	static constexpr float secondCascadeDepthBiasClamp = 0.015f;
	static constexpr float secondCascadeSlopeScaleBias = 3.8f;

	static constexpr int thirdCascadeDepthBias = 100;
	static constexpr float thirdCascadeDepthBiasClamp = 0.015f;
	static constexpr float thirdCascadeSlopeScaleBias = 3.8f;

	struct ShadowMapRasterizerDescriptor
	{
		int rasterDepthBias;
		float rasterDepthBiasClamp;
		float rasterSlopeScaleBias;
	};
	/**
	 * @brief Applies shadow-map-specific depth bias values to a rasterizer descriptor.
	 * @param outputDesc Rasterizer descriptor whose depth bias fields will be overwritten.
	 * @param desc Shadow map bias parameters to apply.
	 */
	static void GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor desc);

	static constexpr ShadowMapRasterizerDescriptor cascadeDescriptors[maxCascades] = {
		{ firstCascadeDepthBias, firstCascadeDepthBiasClamp, firstCascadeSlopeScaleBias },
		{ secondCascadeDepthBias, secondCascadeDepthBiasClamp, secondCascadeSlopeScaleBias },
		{ thirdCascadeDepthBias, thirdCascadeDepthBiasClamp, thirdCascadeSlopeScaleBias }
	};

	struct BSShadowDirectionalLight_RenderShadowmaps_RenderCascade
	{
		static void thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, uint32_t flags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	std::map<std::string, Util::GameSetting> Settings{
		{ "iNumSplits:Display", { "Number of Shadow Map Cascades (INI) ",
									"Controls the number of shadow map cascades used for directional lighting. "
									"Higher values provide better shadow quality but use more GPU resources. "
									"Maximum of 3 cascades supported. ",
									static_cast<uintptr_t>(0), 2, 1, 3 } },
	};
};

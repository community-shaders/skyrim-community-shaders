#pragma once

// This overrides the shadow cascade rasterizers to fix issues with peter panning and self shadowing
struct ShadowmapRasterizerFix : EngineFix
{
	std::string GetName() override { return "Shadowmap Cascade Rasterizer Fix"; }
	void Install() override;

	using RasterStateArray = ID3D11RasterizerState* [2][3][12][2];

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
};

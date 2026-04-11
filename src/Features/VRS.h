#pragma once

#include "Feature.h"
#include "Subrect/Subrect.h"
#include "VRS/NvVrsController.h"

namespace RE::BSGraphics
{
	class BSShaderAccumulator;
}

/// NVAPI Variable Rate Shading — foveated rendering for VR.
///
/// Uses elliptical concentric rings matched to human peripheral acuity falloff,
/// with directional-adaptive asymmetric rates (2×1/1×2) for perceptually
/// smoother transitions.  Significant FPS gain on lower-end GPUs where pixel
/// shading is the bottleneck.
///
/// Subrect integration: foveal center defined by Subrect::Controller, sharing
/// crop presets with Screenshot, DLSSEnhancer, and lossless recording (WIP).
///
/// Known conflicts:
///   - Terrain Blending: visual artifacts when both active.
///   - VR DepthBuffer Culling: incorrect cull combined with VRS cull ring.
///
/// Hard-gated at PostPostLoad: hooks only installed in VR runtime.
struct VRS : Feature
{
public:
	virtual inline std::string GetName() override { return "Variable Rate Shading"; }
	virtual inline std::string GetShortName() override { return "VRS"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return false; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	virtual inline bool IsInMenu() const override { return hardwareAvailable_; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"NVAPI Variable Rate Shading for VR performance optimization",
			{ "Foveated rendering with configurable radii",
				"Full 2x2 and 4x4 uniform shading rate modes",
				"Per-frame diagnostics and debug state" }
		};
	}

	struct Settings
	{
		/// 0 = Disabled, 1 = Enabled.  When off, controller unbinds surface and sends 1×1 LUT.
		uint vrEnableVRS = 1;

		/// SRS ring rate preset: 0=Default (6-step), 1=Faster (4-step), 2=Extreme (3-step).
		uint vrVRSSrsPreset = 0;

		/// LUT debug override: 0=Default (1:1 mapping), 1=Full 1×1, 2=Full 4×4.
		uint vrVRSLutPreset = 0;

		/// Ring boundary growth per step as fraction of base ellipse.
		/// 0.20 = radii at 1.0×, 1.2×, 1.4× ...  Clamped to [0.05, 1.0].
		float vrVRSRingGrowthRate = 0.25f;

		/// Direction-adaptive asymmetric rates (2×1/1×2, 4×2/2×4) for Default preset.
		bool vrEnableDirectionalRates = true;

		/// Checkerboard dithering at ring boundaries to soften transitions.
		bool vrEnableBoundaryDither = true;

		/// Enable diagnostics panel: tile stats, debug visualization (~80 KB upload per rebuild).
		bool vrEnableDiagnostics = false;
	};

	Settings settings;

	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void PostPostLoad() override;

	void UpdateVRShadingRateState();
	void DisableVRShadingRateState();

	NvVrsController nvVrs;            ///< NVAPI surface + rate table lifecycle
	Subrect::Controller subrectCtrl;  ///< Per-eye crop region (foveal center)

private:
	bool hardwareAvailable_ = false;  ///< true after hooks installed in PostPostLoad

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_FinishAccumulatingDispatch
	{
		static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};

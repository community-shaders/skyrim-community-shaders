#include "NativeMenu/NativeMenu.h"

#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#define I18N_KEY_PREFIX "native_menu.upscaling."

namespace
{
	using Feat = Upscaling;
	using Settings = Upscaling::Settings;

	Feat& F() { return globals::features::upscaling; }

	struct UpscalingRoot
	{
		using Type = Settings;
		static Type& Live() { return F().settings; }
		static Type Defaults() { return Type{}; }
	};

	bool __stdcall IsUpscalerSelected()
	{
		auto method = F().GetUpscaleMethod();
		return method == Feat::UpscaleMethod::kFSR || method == Feat::UpscaleMethod::kDLSS;
	}

	bool __stdcall IsFrameGenPathActive()
	{
		return F().IsFrameGenerationDx12PathActive();
	}

	bool __stdcall IsReflexAvailable()
	{
		const auto& sl = Feat::streamline;
		return sl.reflexSupportedOnCurrentAdapter && sl.initialized && sl.featureReflex &&
		       !F().IsFrameGenerationDx12PathActive();
	}

	bool __stdcall IsReflexModeOn()
	{
		return IsReflexAvailable() && F().settings.reflexLowLatencyMode;
	}

	bool __stdcall IsReflexMarkersAvailable()
	{
		return IsReflexAvailable() && Feat::streamline.featurePCL;
	}

	bool __stdcall IsReflexFpsCapOn()
	{
		return IsReflexModeOn() && F().settings.reflexUseFPSLimit;
	}

	uint32_t& ActiveMethodField()
	{
		return Feat::streamline.featureDLSS ? F().settings.upscaleMethod : F().settings.upscaleMethodNoDLSS;
	}

	std::vector<std::string> MethodOptions()
	{
		std::vector<std::string> options = {
			T(TKEY("method_none"), "None"),
			T(TKEY("method_taa"), "TAA"),
			"AMD FSR 3.1",
		};
		if (Feat::streamline.featureDLSS)
			options.push_back("NVIDIA DLSS");
		return options;
	}

	float MethodDefault()
	{
		return Feat::streamline.featureDLSS ?
		           static_cast<float>(Settings{}.upscaleMethod) :
		           static_cast<float>(Settings{}.upscaleMethodNoDLSS);
	}

	float __stdcall GetMethod()
	{
		return static_cast<float>(ActiveMethodField());
	}

	void __stdcall SetMethod(float v)
	{
		const uint32_t maxIndex = Feat::streamline.featureDLSS ? 3u : 2u;
		ActiveMethodField() = std::min<uint32_t>(maxIndex, static_cast<uint32_t>(v));
	}

	float __stdcall GetSharpness()
	{
		if (F().GetUpscaleMethod() == Feat::UpscaleMethod::kDLSS)
			return F().settings.sharpnessEnabledDLSS ? F().settings.sharpnessDLSS : 0.0f;
		return F().settings.sharpnessFSR;
	}

	void __stdcall SetSharpness(float v)
	{
		if (F().GetUpscaleMethod() == Feat::UpscaleMethod::kDLSS) {
			F().settings.sharpnessDLSS = v;
			F().settings.sharpnessEnabledDLSS = v > 0.0f;
		} else {
			F().settings.sharpnessFSR = v;
		}
	}

	void __stdcall FormatSharpness(float value, char* buffer, int bufferSize)
	{
		std::snprintf(buffer, bufferSize, "%.2f", value);
	}

	constexpr std::array<float, 8> kFpsCapValues{ 30.0f, 60.0f, 72.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f };

	size_t FindFpsCapIndex(float target)
	{
		size_t best = 0;
		float bestDelta = std::fabs(kFpsCapValues[0] - target);
		for (size_t i = 1; i < kFpsCapValues.size(); ++i) {
			const float delta = std::fabs(kFpsCapValues[i] - target);
			if (delta < bestDelta) {
				bestDelta = delta;
				best = i;
			}
		}
		return best;
	}

	std::vector<std::string> FpsCapOptions()
	{
		std::vector<std::string> options;
		options.reserve(kFpsCapValues.size());
		for (float v : kFpsCapValues)
			options.push_back(std::format("{}", static_cast<int>(v)));
		return options;
	}

	float __stdcall GetFpsCap()
	{
		return static_cast<float>(FindFpsCapIndex(F().settings.reflexFPSLimit));
	}

	void __stdcall SetFpsCap(float v)
	{
		const size_t idx = static_cast<size_t>(std::clamp<float>(v, 0.0f, static_cast<float>(kFpsCapValues.size() - 1)));
		F().settings.reflexFPSLimit = kFpsCapValues[idx];
	}
}

namespace NativeMenu
{
	std::vector<Row> UpscalingRows()
	{
		using B = UpscalingRoot;

		Row methodRow;
		methodRow.type = RowType::kDropdown;
		methodRow.label = T(TKEY("method"), "Upscaling Method");
		methodRow.description = T(TKEY("method_desc"), "Chooses the upscaling/anti-aliasing technique used to render the game.");
		methodRow.getValue = &GetMethod;
		methodRow.setValue = &SetMethod;
		methodRow.defaultValue = MethodDefault();
		methodRow.options = MethodOptions();
		methodRow.commit = &CommitAndSave;

		Row sharpnessRow;
		sharpnessRow.type = RowType::kSlider;
		sharpnessRow.label = T(TKEY("sharpness"), "Upscaling Sharpness");
		sharpnessRow.description = T(TKEY("sharpness_desc"), "Sharpens the image after upscaling.");
		sharpnessRow.getValue = &GetSharpness;
		sharpnessRow.setValue = &SetSharpness;
		sharpnessRow.defaultValue = 0.0f;
		sharpnessRow.isEnabled = &IsUpscalerSelected;
		sharpnessRow.formatValue = &FormatSharpness;
		sharpnessRow.commit = &CommitAndSave;

		Row fpsCapRow;
		fpsCapRow.type = RowType::kDropdown;
		fpsCapRow.label = T(TKEY("fps_cap"), "NVIDIA Reflex Frame Cap");
		fpsCapRow.description = T(TKEY("fps_cap_desc"), "Frame rate cap used by Reflex when \"Use FPS Limit\" is enabled.");
		fpsCapRow.getValue = &GetFpsCap;
		fpsCapRow.setValue = &SetFpsCap;
		fpsCapRow.defaultValue = static_cast<float>(FindFpsCapIndex(Settings{}.reflexFPSLimit));
		fpsCapRow.options = FpsCapOptions();
		fpsCapRow.isEnabled = &IsReflexFpsCapOn;
		fpsCapRow.commit = &CommitAndSave;

		return {
			NATIVE_MENU_HEADING(T(TKEY("heading"), "Community Shaders")),

			methodRow,

			Dropdown<B, &Settings::qualityMode>(
				T(TKEY("quality"), "Upscaling Quality"),
				{ T(TKEY("preset_native"), "Native AA / DLAA"),
					T(TKEY("preset_quality"), "Quality"),
					T(TKEY("preset_balanced"), "Balanced"),
					T(TKEY("preset_performance"), "Performance"),
					T(TKEY("preset_ultra_performance"), "Ultra Performance") },
				T(TKEY("quality_desc"), "Trades render resolution for performance. Lower entries render at a lower internal resolution."),
				&IsUpscalerSelected),

			sharpnessRow,

			Checkbox<B, &Settings::frameGenerationMode>(
				T(TKEY("frame_generation"), "Frame Generation"),
				T(TKEY("frame_generation_desc"),
					"Interpolates generated frames for a smoother experience. Requires windowed mode and a high refresh rate "
					"monitor, or Force Enable below. Requires a restart to take effect.")),

			Checkbox<B, &Settings::frameGenerationForceEnable>(
				T(TKEY("force_enable_frame_generation"), "Force Enable Frame Generation"),
				T(TKEY("force_enable_frame_generation_desc"),
					"Allows Frame Generation on monitors below the recommended refresh rate. Requires a restart to take effect.")),

			Checkbox<B, &Settings::frameLimitMode>(
				T(TKEY("frame_limit_vrr"), "Frame Limit (Variable Refresh Rate)"),
				T(TKEY("frame_limit_vrr_desc"), "Limits the frame rate for smoother pacing on variable refresh rate displays."),
				&IsFrameGenPathActive),

			Checkbox<B, &Settings::frameGenerationAllowInMenus>(
				T(TKEY("frame_generation_in_menus"), "Frame Generation in Menus"),
				T(TKEY("frame_generation_in_menus_desc"),
					"Keeps frame generation active while game menus are open. May increase menu input latency.")),

			Checkbox<B, &Settings::reflexLowLatencyMode>(
				T(TKEY("low_latency_mode"), "NVIDIA Reflex Low Latency Mode"),
				T(TKEY("low_latency_mode_desc"), "Reduces input latency by syncing CPU work closer to the GPU."),
				&IsReflexAvailable),

			Checkbox<B, &Settings::reflexLowLatencyBoost>(
				T(TKEY("low_latency_boost"), "NVIDIA Reflex Low Latency Boost"),
				T(TKEY("low_latency_boost_desc"), "Keeps GPU clocks higher to avoid latency spikes at low GPU load. Costs extra power and heat."),
				&IsReflexModeOn),

			Checkbox<B, &Settings::reflexUseMarkersToOptimize>(
				T(TKEY("use_markers_to_optimize"), "NVIDIA Reflex Marker Optimization"),
				T(TKEY("use_markers_to_optimize_desc"),
					"Uses frame markers for tighter Reflex timing. Try On first; turn Off if it causes stutter on your setup."),
				&IsReflexMarkersAvailable),

			Checkbox<B, &Settings::reflexUseFPSLimit>(
				T(TKEY("use_fps_limit"), "NVIDIA Reflex FPS Limit"),
				T(TKEY("use_fps_limit_desc"), "Uses Reflex's internal FPS cap below for steadier frame times and lower latency."),
				&IsReflexModeOn),

			fpsCapRow,
		};
	}
}

#undef I18N_KEY_PREFIX

#include "NativeMenu/NativeMenu.h"

#include "Features/ScreenSpaceGI.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include <algorithm>
#include <array>
#include <optional>

#define I18N_KEY_PREFIX "native_menu.ssgi."

namespace
{
	using Settings = ScreenSpaceGI::Settings;

	struct SSGIRoot
	{
		static Settings& Live() { return globals::features::screenSpaceGI.settings; }
		static Settings Defaults() { return Settings{}; }
	};

	struct SSGIPreset
	{
		uint32_t numSlices;
		uint32_t numSteps;
		std::optional<int> resolutionMode;
		bool enableBlur;
		bool enableGI;
	};

	constexpr std::array<SSGIPreset, 5> kSSGIPresets{ {
		{ 1, 6, std::nullopt, true, false },
		{ 10, 12, 2, true, true },
		{ 4, 8, 1, true, true },
		{ 4, 8, 0, true, true },
		{ 8, 10, 0, true, true },
	} };
	constexpr size_t kSSGICustomIndex = kSSGIPresets.size();

	size_t FindSSGIPresetIndex(const Settings& s)
	{
		for (size_t i = 0; i < kSSGIPresets.size(); ++i) {
			const auto& p = kSSGIPresets[i];
			if (s.NumSlices == p.numSlices && s.NumSteps == p.numSteps && s.EnableBlur == p.enableBlur &&
				s.EnableGI == p.enableGI && (!p.resolutionMode || *p.resolutionMode == s.ResolutionMode))
				return i;
		}
		return kSSGICustomIndex;
	}

	std::vector<std::string> SSGIQualityOptions()
	{
		return {
			T(TKEY("preset_ao_only"), "AO Only"),
			T(TKEY("preset_low"), "Low"),
			T(TKEY("preset_standard"), "Standard"),
			T(TKEY("preset_extreme"), "Extreme"),
			T(TKEY("preset_reference"), "Reference"),
			T(TKEY("preset_custom"), "Custom"),
		};
	}

	float __stdcall GetSSGIQuality()
	{
		return static_cast<float>(FindSSGIPresetIndex(SSGIRoot::Live()));
	}

	void __stdcall SetSSGIQuality(float v)
	{
		const auto idx = static_cast<size_t>(std::clamp<float>(v, 0.0f, static_cast<float>(kSSGICustomIndex)));
		if (idx >= kSSGIPresets.size())
			return;

		const auto& preset = kSSGIPresets[idx];
		auto& s = SSGIRoot::Live();
		s.NumSlices = preset.numSlices;
		s.NumSteps = preset.numSteps;
		if (preset.resolutionMode)
			s.ResolutionMode = *preset.resolutionMode;
		s.EnableBlur = preset.enableBlur;
		s.EnableGI = preset.enableGI;
		globals::features::screenSpaceGI.recompileFlag = true;
	}
}

namespace NativeMenu
{
	std::vector<Row> SSGIRows()
	{
		if (!globals::features::screenSpaceGI.loaded)
			return {};

		using Enabled = Bind<SSGIRoot, &Settings::Enabled>;

		return {
			Checkbox<SSGIRoot, &Settings::Enabled>(T(TKEY("enable"), "Enable Screen Space GI"),
				T(TKEY("enable_desc"),
					"Toggles Screen Space GI. When disabled, the quality preset below is greyed out.")),

			Dropdown(T(TKEY("quality"), "Screen Space GI Quality"), SSGIQualityOptions(), &GetSSGIQuality,
				&SetSSGIQuality, static_cast<float>(FindSSGIPresetIndex(Settings{})),
				T(TKEY("quality_desc"),
					"Trades render resolution and sample count for visual quality. Matches the presets in the Screen "
					"Space GI panel's Quality/Performance section."),
				&Enabled::IsFlagOn),
		};
	}
}

#undef I18N_KEY_PREFIX

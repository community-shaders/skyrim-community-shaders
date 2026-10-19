#include "NativeMenu/NativeMenu.h"

#include "Features/ScreenSpaceGI.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include <algorithm>
#include <array>
#include <optional>

#define I18N_KEY_PREFIX "feature.screen_space_gi."

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
			T("menu.features.disabled", "Disabled"),
			T(TKEY("ao_only"), "AO only"),
			T(TKEY("low"), "Low"),
			T(TKEY("standard"), "Standard"),
			T(TKEY("extreme"), "Extreme"),
			T(TKEY("reference"), "Reference"),
			T(TKEY("custom"), "Custom"),
		};
	}

	// Option 0 is Disabled, so the presets and Custom sit one past their index.
	float SSGIOption(const Settings& s)
	{
		return s.Enabled ? static_cast<float>(FindSSGIPresetIndex(s) + 1) : 0.0f;
	}

	float __stdcall GetSSGIQuality()
	{
		return SSGIOption(SSGIRoot::Live());
	}

	void __stdcall SetSSGIQuality(float v)
	{
		const auto option = static_cast<size_t>(std::clamp<float>(v, 0.0f, static_cast<float>(kSSGICustomIndex + 1)));
		auto& s = SSGIRoot::Live();
		s.Enabled = option != 0;

		// Custom keeps the current values, so it re-enables without losing tuning.
		if (option == 0 || option - 1 >= kSSGIPresets.size())
			return;

		const auto& preset = kSSGIPresets[option - 1];
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

		return {
			Dropdown(T(TKEY("name"), "Screen Space GI"), SSGIQualityOptions(), &GetSSGIQuality, &SetSSGIQuality,
				SSGIOption(Settings{}),
				T(TKEY("description"),
					"Screen Space Global Illumination adds realistic indirect lighting and ambient occlusion to the game. This technique simulates how light bounces off surfaces to illuminate other objects naturally.")),
		};
	}
}

#undef I18N_KEY_PREFIX

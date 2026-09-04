#include "NativeMenu/NativeMenu.h"

#include "Features/HDRDisplay.h"
#include "Features/ScreenSpaceGI.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>

#define I18N_KEY_PREFIX "native_menu.graphics."

namespace
{
	void __stdcall OpenCommunityShadersMenu()
	{
		if (globals::menu)
			globals::menu->IsEnabled = true;
	}

	HDRDisplay& HDR() { return globals::features::hdrDisplay; }

	bool __stdcall IsHDRToggleEnabled()
	{
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		return HDRDisplay::isHDRMonitor || HDR().settings.enableHDR;
	}

	float __stdcall GetHDREnabled()
	{
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		return HDR().settings.enableHDR ? 1.0f : 0.0f;
	}

	void __stdcall SetHDREnabled(float v)
	{
		const bool enable = v != 0.0f;
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		if (HDR().settings.enableHDR == enable)
			return;
		HDR().settings.enableHDR = enable;
		HDR().UpdateHDRData();
		HDR().UpdateSwapChainColorSpace();
	}

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

	ScreenSpaceGI& SSGI() { return globals::features::screenSpaceGI; }

	size_t FindSSGIPresetIndex(const ScreenSpaceGI::Settings& s)
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
			T(TKEY("ssgi_preset_ao_only"), "AO Only"),
			T(TKEY("ssgi_preset_low"), "Low"),
			T(TKEY("ssgi_preset_standard"), "Standard"),
			T(TKEY("ssgi_preset_extreme"), "Extreme"),
			T(TKEY("ssgi_preset_reference"), "Reference"),
			T(TKEY("ssgi_preset_custom"), "Custom"),
		};
	}

	bool __stdcall IsSSGIEnabled()
	{
		return SSGI().settings.Enabled;
	}

	float __stdcall GetSSGIQuality()
	{
		return static_cast<float>(FindSSGIPresetIndex(SSGI().settings));
	}

	void __stdcall SetSSGIQuality(float v)
	{
		const auto idx = static_cast<size_t>(std::clamp<float>(v, 0.0f, static_cast<float>(kSSGICustomIndex)));
		if (idx >= kSSGIPresets.size())
			return;

		const auto& preset = kSSGIPresets[idx];
		auto& s = SSGI().settings;
		s.NumSlices = preset.numSlices;
		s.NumSteps = preset.numSteps;
		if (preset.resolutionMode)
			s.ResolutionMode = *preset.resolutionMode;
		s.EnableBlur = preset.enableBlur;
		s.EnableGI = preset.enableGI;
		SSGI().recompileFlag = true;
	}
}

namespace NativeMenu
{
	std::vector<Row> GraphicsRows()
	{
		std::vector<Row> rows{
			NATIVE_MENU_HEADING(T(TKEY("heading"), "Community Shaders")),

			Button(T(TKEY("open_menu"), "Open Community Shaders Menu"), &OpenCommunityShadersMenu,
				T(TKEY("open_menu_desc"), "Opens the full Community Shaders settings window.")),
		};

		if (globals::features::hdrDisplay.loaded) {
			Row hdrRow;
			hdrRow.type = RowType::kCheckbox;
			hdrRow.label = T(TKEY("enable_hdr"), "Enable HDR");
			hdrRow.description = T(TKEY("enable_hdr_desc"),
				"Real HDR output for HDR displays. Greyed out until an HDR-capable, HDR-enabled display is detected; "
				"use the full settings menu's Advanced override to force it otherwise.");
			hdrRow.getValue = &GetHDREnabled;
			hdrRow.setValue = &SetHDREnabled;
			hdrRow.defaultValue = HDRDisplay::Settings{}.enableHDR ? 1.0f : 0.0f;
			hdrRow.isEnabled = &IsHDRToggleEnabled;
			hdrRow.commit = &CommitAndSave;
			rows.push_back(hdrRow);
		}

		if (globals::features::screenSpaceGI.loaded) {
			Row ssgiRow;
			ssgiRow.type = RowType::kDropdown;
			ssgiRow.label = T(TKEY("ssgi_quality"), "Screen Space GI Quality");
			ssgiRow.description = T(TKEY("ssgi_quality_desc"),
				"Trades render resolution and sample count for visual quality. Matches the presets in the Screen "
				"Space GI panel's Quality/Performance section.");
			ssgiRow.getValue = &GetSSGIQuality;
			ssgiRow.setValue = &SetSSGIQuality;
			ssgiRow.defaultValue = static_cast<float>(FindSSGIPresetIndex(ScreenSpaceGI::Settings{}));
			ssgiRow.options = SSGIQualityOptions();
			ssgiRow.isEnabled = &IsSSGIEnabled;
			ssgiRow.commit = &CommitAndSave;
			rows.push_back(ssgiRow);
		}

		return rows;
	}
}

#undef I18N_KEY_PREFIX

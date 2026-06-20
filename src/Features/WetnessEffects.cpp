#include "WetnessEffects.h"
#include "CSEditor.h"
#include "I18n/I18n.h"
#include "Menu.h"

#define I18N_KEY_PREFIX "feature.wetness_effects."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WetnessEffects::Settings,
	EnableWetnessEffects,
	MaxRainWetness,
	MaxPuddleWetness,
	MaxShoreWetness,
	ShoreRange,
	PuddleRadius,
	PuddleMaxAngle,
	PuddleMinWetness,
	MinRainWetness,
	SkinWetness,
	WeatherTransitionSpeed,
	EnableRaindropFx,
	EnableSplashes,
	EnableRipples,
	EnableVanillaRipples,
	RaindropFxRange,
	RaindropGridSize,
	RaindropInterval,
	RaindropChance,
	SplashesLifetime,
	SplashesStrength,
	SplashesMinRadius,
	SplashesMaxRadius,
	RippleStrength,
	RippleRadius,
	RippleBreadth,
	RippleLifetime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WetnessEffects::DebugSettings,
	EnableWetnessOverride,
	EnablePuddleOverride,
	EnableRainOverride,
	EnableIntExOverride,
	WetnessOverride,
	PuddleWetnessOverride,
	RainOverride)

// Climate preset data - defines regional weather characteristics
// Precipitation rates calculated from actual shader mechanics: grid size, interval, and raindrop chance

struct ClimatePresetInfo
{
	const char* name;
	const char* shortDescription;
	const char* const* detailedDescription;
	const char* const* effectDescription;
	WetnessEffects::ClimateSettings settings;
};

// Climate preset detailed descriptions
static constexpr const char* LEGACY_DETAILED[] = {
	"Riverwood's original rain effect values for full backward compatibility.",
	"Max precipitation: ~0.66 mm/hr (very light)",
	"Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x.",
	"Raindrop: 30% chance, grid 4.0 units, interval 0.5s.",
	"Performance impact: Minimal (baseline)",
	nullptr
};
static constexpr const char* LEGACY_EFFECTS[] = {
	"Original wetness accumulation (1.0x)",
	"Original puddle formation (1.0x)",
	"Original weather transitions (1.0x)",
	"Original raindrop frequency (1.0x)",
	nullptr
};

static constexpr const char* ARCTIC_DETAILED[] = {
	"Cold, dry climate with minimal precipitation.",
	"Max precipitation: ~1.08 mm/hr (light)",
	"Multipliers: Wetness 0.5x, Puddle 0.3x, Transition 0.5x.",
	"Raindrop: 30% chance, grid 3.5 units, interval 0.4s.",
	"Performance impact: Minimal",
	nullptr
};
static constexpr const char* ARCTIC_EFFECTS[] = {
	"Slow wetness accumulation (0.5x)",
	"Minimal puddle formation (0.3x)",
	"Slow weather transitions (0.5x)",
	"Sparse precipitation (30% chance)",
	"",
	nullptr
};

static constexpr const char* NORDIC_DETAILED[] = {
	"Balanced temperate Nordic climate.",
	"Max precipitation: ~3.35 mm/hr (moderate)",
	"Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x.",
	"Raindrop: 100% chance, grid 3.0 units, interval 1.0s.",
	"Performance impact: Low",
	nullptr
};
static constexpr const char* NORDIC_EFFECTS[] = {
	"Standard wetness accumulation (1.0x)",
	"Standard puddle formation (1.0x)",
	"Standard weather transitions (1.0x)",
	"Moderate raindrop frequency (100% chance)",
	nullptr
};

static constexpr const char* COASTAL_DETAILED[] = {
	"Maritime climate with frequent, heavy precipitation.",
	"Max precipitation: ~8.06 mm/hr (heavy)",
	"Multipliers: Wetness 1.5x, Puddle 1.7x, Transition 1.7x.",
	"Raindrop: 80% chance, grid 2.5 units, interval 0.25s.",
	"Performance impact: Moderate",
	nullptr
};
static constexpr const char* COASTAL_EFFECTS[] = {
	"Fast wetness accumulation (1.5x)",
	"Enhanced puddle formation (1.7x)",
	"Rapid weather transitions (1.7x)",
	"Frequent rain events (80% chance)",
	nullptr
};

static constexpr const char* MONSOON_DETAILED[] = {
	"Tropical/monsoon climate with extreme precipitation.",
	"Max precipitation: ~22 mm/hr (extreme)",
	"Multipliers: Wetness 2.0x, Puddle 2.5x, Transition 2.0x.",
	"Raindrop: 100% chance, grid 2.0 units, interval 0.2s.",
	"Skyrim light rain will not match wetness.",
	"Performance impact: High (may impact GPU)",
	nullptr
};
static constexpr const char* MONSOON_EFFECTS[] = {
	"Rapid wetness accumulation (2.0x)",
	"Maximum puddle formation (2.5x)",
	"Very dynamic weather (2.0x)",
	"Maximum raindrop frequency (100% chance)",
	nullptr
};

static constexpr std::array<ClimatePresetInfo, 6> CLIMATE_PRESET_INFO = {
	{ { "Custom",
		  "User-defined custom settings",
		  nullptr,
		  nullptr,
		  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
		// Legacy (Original Skyrim)
		{
			"Legacy",
			"Original rain effect values (very light)",
			LEGACY_DETAILED,
			LEGACY_EFFECTS,
			{ 1.0f, 1.0f, 1.0f, 0.3f, 4.0f, 0.5f } },
		// Nordic Standard
		{
			"Nordic (Default)",
			"Balanced Nordic climate (moderate rain)",
			NORDIC_DETAILED,
			NORDIC_EFFECTS,
			{ 1.0f, 1.0f, 1.0f, 1.0f, 3.0f, 1.0f } },
		// Arctic Tundra
		{
			"Arctic Tundra",
			"Cold, dry Arctic climate (light rain)",
			ARCTIC_DETAILED,
			ARCTIC_EFFECTS,
			{ 0.5f, 0.3f, 0.5f, 0.3f, 3.5f, 0.4f } },
		// Temperate Coastal
		{
			"Temperate Coastal",
			"Maritime climate (heavy rain)",
			COASTAL_DETAILED,
			COASTAL_EFFECTS,
			{ 1.5f, 1.7f, 1.7f, 0.8f, 2.5f, 0.25f } },
		// Monsoon/Extreme
		{
			"Monsoon/Extreme",
			"Extreme monsoon climate (extreme rain)",
			MONSOON_DETAILED,
			MONSOON_EFFECTS,
			{ 2.0f, 2.5f, 2.0f, 1.0f, 2.0f, 0.2f } } }
};

// Extract just the settings for the actual climate preset array
static const std::array<WetnessEffects::ClimateSettings, 6> CLIMATE_PRESETS = { {
	CLIMATE_PRESET_INFO[0].settings,  // Custom (placeholder)
	CLIMATE_PRESET_INFO[1].settings,  // Legacy
	CLIMATE_PRESET_INFO[2].settings,  // Nordic Standard
	CLIMATE_PRESET_INFO[3].settings,  // Arctic Tundra
	CLIMATE_PRESET_INFO[4].settings,  // Temperate Coastal
	CLIMATE_PRESET_INFO[5].settings   // Monsoon/Extreme
} };

static const char* GetClimatePresetDisplayName(size_t a_index)
{
	switch (a_index) {
	case 0:
		return T(TKEY("climate_preset_custom"), "Custom");
	case 1:
		return T(TKEY("climate_preset_legacy"), "Legacy");
	case 2:
		return T(TKEY("climate_preset_nordic"), "Nordic (Default)");
	case 3:
		return T(TKEY("climate_preset_arctic"), "Arctic Tundra");
	case 4:
		return T(TKEY("climate_preset_coastal"), "Temperate Coastal");
	case 5:
		return T(TKEY("climate_preset_monsoon"), "Monsoon/Extreme");
	default:
		return T(TKEY("climate_preset_unknown"), "Unknown");
	}
}

static const char* GetClimatePresetShortDescription(size_t a_index)
{
	switch (a_index) {
	case 0:
		return T(TKEY("climate_preset_custom_desc"), "User-defined custom settings");
	case 1:
		return T(TKEY("climate_preset_legacy_desc"), "Original rain effect values (very light)");
	case 2:
		return T(TKEY("climate_preset_nordic_desc"), "Balanced Nordic climate (moderate rain)");
	case 3:
		return T(TKEY("climate_preset_arctic_desc"), "Cold, dry Arctic climate (light rain)");
	case 4:
		return T(TKEY("climate_preset_coastal_desc"), "Maritime climate (heavy rain)");
	case 5:
		return T(TKEY("climate_preset_monsoon_desc"), "Extreme monsoon climate (extreme rain)");
	default:
		return "";
	}
}

static void DrawWeatherAnalysisLabel(const char* a_label)
{
	const auto& palette = Menu::GetSingleton()->GetTheme().Palette;
	ImGui::TextColored(palette.Text, "%s", a_label);
	ImGui::Spacing();
}

static std::vector<const char*> GetClimatePresetDetailedDescription(size_t a_index)
{
	switch (a_index) {
	case 1:
		return {
			T(TKEY("climate_legacy_detail_0"), "Riverwood's original rain effect values for full backward compatibility."),
			T(TKEY("climate_legacy_detail_1"), "Max precipitation: ~0.66 mm/hr (very light)"),
			T(TKEY("climate_legacy_detail_2"), "Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x."),
			T(TKEY("climate_legacy_detail_3"), "Raindrop: 30% chance, grid 4.0 units, interval 0.5s."),
			T(TKEY("climate_legacy_detail_4"), "Performance impact: Minimal (baseline)")
		};
	case 2:
		return {
			T(TKEY("climate_nordic_detail_0"), "Balanced temperate Nordic climate."),
			T(TKEY("climate_nordic_detail_1"), "Max precipitation: ~3.35 mm/hr (moderate)"),
			T(TKEY("climate_nordic_detail_2"), "Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x."),
			T(TKEY("climate_nordic_detail_3"), "Raindrop: 100% chance, grid 3.0 units, interval 1.0s."),
			T(TKEY("climate_nordic_detail_4"), "Performance impact: Low")
		};
	case 3:
		return {
			T(TKEY("climate_arctic_detail_0"), "Cold, dry climate with minimal precipitation."),
			T(TKEY("climate_arctic_detail_1"), "Max precipitation: ~1.08 mm/hr (light)"),
			T(TKEY("climate_arctic_detail_2"), "Multipliers: Wetness 0.5x, Puddle 0.3x, Transition 0.5x."),
			T(TKEY("climate_arctic_detail_3"), "Raindrop: 30% chance, grid 3.5 units, interval 0.4s."),
			T(TKEY("climate_arctic_detail_4"), "Performance impact: Minimal")
		};
	case 4:
		return {
			T(TKEY("climate_coastal_detail_0"), "Maritime climate with frequent, heavy precipitation."),
			T(TKEY("climate_coastal_detail_1"), "Max precipitation: ~8.06 mm/hr (heavy)"),
			T(TKEY("climate_coastal_detail_2"), "Multipliers: Wetness 1.5x, Puddle 1.7x, Transition 1.7x."),
			T(TKEY("climate_coastal_detail_3"), "Raindrop: 80% chance, grid 2.5 units, interval 0.25s."),
			T(TKEY("climate_coastal_detail_4"), "Performance impact: Moderate")
		};
	case 5:
		return {
			T(TKEY("climate_monsoon_detail_0"), "Tropical/monsoon climate with extreme precipitation."),
			T(TKEY("climate_monsoon_detail_1"), "Max precipitation: ~22 mm/hr (extreme)"),
			T(TKEY("climate_monsoon_detail_2"), "Multipliers: Wetness 2.0x, Puddle 2.5x, Transition 2.0x."),
			T(TKEY("climate_monsoon_detail_3"), "Raindrop: 100% chance, grid 2.0 units, interval 0.2s."),
			T(TKEY("climate_monsoon_detail_4"), "Skyrim light rain will not match wetness."),
			T(TKEY("climate_monsoon_detail_5"), "Performance impact: High (may impact GPU)")
		};
	default:
		return {};
	}
}

static std::vector<const char*> GetClimatePresetEffectDescription(size_t a_index)
{
	switch (a_index) {
	case 1:
		return {
			T(TKEY("climate_legacy_effect_0"), "Original wetness accumulation (1.0x)"),
			T(TKEY("climate_legacy_effect_1"), "Original puddle formation (1.0x)"),
			T(TKEY("climate_legacy_effect_2"), "Original weather transitions (1.0x)"),
			T(TKEY("climate_legacy_effect_3"), "Original raindrop frequency (1.0x)")
		};
	case 2:
		return {
			T(TKEY("climate_nordic_effect_0"), "Standard wetness accumulation (1.0x)"),
			T(TKEY("climate_nordic_effect_1"), "Standard puddle formation (1.0x)"),
			T(TKEY("climate_nordic_effect_2"), "Standard weather transitions (1.0x)"),
			T(TKEY("climate_nordic_effect_3"), "Moderate raindrop frequency (100% chance)")
		};
	case 3:
		return {
			T(TKEY("climate_arctic_effect_0"), "Slow wetness accumulation (0.5x)"),
			T(TKEY("climate_arctic_effect_1"), "Minimal puddle formation (0.3x)"),
			T(TKEY("climate_arctic_effect_2"), "Slow weather transitions (0.5x)"),
			T(TKEY("climate_arctic_effect_3"), "Sparse precipitation (30% chance)")
		};
	case 4:
		return {
			T(TKEY("climate_coastal_effect_0"), "Fast wetness accumulation (1.5x)"),
			T(TKEY("climate_coastal_effect_1"), "Enhanced puddle formation (1.7x)"),
			T(TKEY("climate_coastal_effect_2"), "Rapid weather transitions (1.7x)"),
			T(TKEY("climate_coastal_effect_3"), "Frequent rain events (80% chance)")
		};
	case 5:
		return {
			T(TKEY("climate_monsoon_effect_0"), "Rapid wetness accumulation (2.0x)"),
			T(TKEY("climate_monsoon_effect_1"), "Maximum puddle formation (2.5x)"),
			T(TKEY("climate_monsoon_effect_2"), "Very dynamic weather (2.0x)"),
			T(TKEY("climate_monsoon_effect_3"), "Maximum raindrop frequency (100% chance)")
		};
	default:
		return {};
	}
}

// Ripples code borrowed from po3 SplashesofStorms
// https://github.com/powerof3/SplashesOfStorms/blob/master/src/Hooks.cpp under MIT License
namespace Ripples
{
	// Cache settings to avoid repeated singleton access
	static bool s_isEnabled = false;
	static bool s_vanillaRipplesEnabled = false;

	struct ToggleWaterSplashes
	{
		static void thunk(RE::TESWaterSystem* a_waterSystem, bool a_enabled, float a_fadeAmount)
		{
			// Apply our logic only if wetness effects are enabled
			if (s_isEnabled) {
				a_enabled = a_enabled && s_vanillaRipplesEnabled;
			}
			for (auto& waterObject : a_waterSystem->waterObjects) {
				if (waterObject) {
					if (const auto& rippleObject = waterObject->waterRippleObject; rippleObject) {
						rippleObject->SetAppCulled(!a_enabled);
					}
				}
			}

			func(a_waterSystem, a_enabled, a_fadeAmount);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void UpdateSettings()
	{
		auto& wetnessEffects = globals::features::wetnessEffects;
		s_isEnabled = wetnessEffects.settings.EnableWetnessEffects;
		s_vanillaRipplesEnabled = wetnessEffects.settings.EnableVanillaRipples;
		logger::debug("[{}] UpdateSettings: EnableWetnessEffects={}, EnableVanillaRipples={}",
			wetnessEffects.GetName(), s_isEnabled, s_vanillaRipplesEnabled);
	}

	void Install()
	{
		auto& wetnessEffects = globals::features::wetnessEffects;
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(25638, 26179), REL::VariantOffset(0x238, 0x223, 0x238) };
		stl::write_thunk_call<ToggleWaterSplashes>(target.address());
		logger::info("[{}] Installed ripple hooks", wetnessEffects.GetName());
	}
}

void WetnessEffects::PostPostLoad()
{
	splashesOfStormsLoaded = static_cast<bool>(GetModuleHandle(L"po3_SplashesOfStorms.dll"));
	if (splashesOfStormsLoaded) {
		logger::info("[{}] Splashes of Storms detected, compatibility enabled", GetName());
		return;
	}

	// Only hook if SoS is not loaded
	Ripples::Install();
}

void WetnessEffects::DrawSettings()
{
	// Climate Preset Selection - Always visible at the top
	Util::DrawSectionHeader(T(TKEY("climate_presets"), "Climate Presets"), false, false);

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.3f, 0.4f, 0.6f));    // Subtle blue background
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.45f, 0.8f));  // Slightly darker for button

	// Extract names for combo box
	const char* presetNames[CLIMATE_PRESET_INFO.size()];
	for (size_t i = 0; i < CLIMATE_PRESET_INFO.size(); ++i) {
		presetNames[i] = GetClimatePresetDisplayName(i);
	}
	// Map preset enum to combo index (Custom=0, Legacy=1, Nordic=2, Arctic=3, Coastal=4, Monsoon=5)
	int currentComboIndex = static_cast<int>(climatePreset);

	if (ImGui::Combo(T(TKEY("climate_preset"), "Climate Preset"), &currentComboIndex, presetNames, static_cast<int>(CLIMATE_PRESET_INFO.size()))) {  // Map combo index back to preset enum
		// Simplified: map combo index directly to enum, with bounds check
		ClimatePreset newPreset = (currentComboIndex >= 0 && currentComboIndex < static_cast<int>(CLIMATE_PRESET_INFO.size())) ? static_cast<ClimatePreset>(currentComboIndex) : defaultPreset;

		// Update the preset selection
		climatePreset = newPreset;

		// Apply preset settings (but not for Custom, which just means user-modified)
		if (newPreset != ClimatePreset::Custom) {
			ApplyClimatePreset(newPreset);
		}
	}

	ImGui::PopStyleColor(2);  // Pop both style colors
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (currentComboIndex >= 0 && currentComboIndex < static_cast<int>(CLIMATE_PRESET_INFO.size())) {
			// Handle Custom preset differently
			if (currentComboIndex == 0) {  // Custom preset
				Util::DrawMultiLineTooltip({ T(TKEY("custom_preset_tooltip_0"), "Custom settings - you have modified the preset values."),
					T(TKEY("custom_preset_tooltip_1"), "Select a preset above to apply predefined climate settings.") });
			} else {
				// Build combined description lines for actual presets
				std::vector<const char*> tooltipLines;
				tooltipLines.push_back(GetClimatePresetShortDescription(static_cast<size_t>(currentComboIndex)));
				// Add detailed description
				for (const char* line : GetClimatePresetDetailedDescription(static_cast<size_t>(currentComboIndex))) {
					tooltipLines.push_back(line);
				}
				tooltipLines.push_back(T(TKEY("effects"), "Effects:"));
				// Add effect descriptions
				for (const char* effect : GetClimatePresetEffectDescription(static_cast<size_t>(currentComboIndex))) {
					tooltipLines.push_back(effect);
				}

				std::vector<std::string> tooltipLinesStr;
				tooltipLinesStr.reserve(tooltipLines.size());
				for (const char* line : tooltipLines) {
					tooltipLinesStr.emplace_back(line);
				}
				Util::DrawMultiLineTooltip(tooltipLinesStr);
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("wetness_effects"), "Wetness Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox(T(TKEY("enable_wetness"), "Enable Wetness"), (bool*)&settings.EnableWetnessEffects)) {
			Ripples::UpdateSettings();  // Update cache when settings change
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_wetness_tooltip"), "Enables a wetness effect near water and when it is raining."));
		}
		ImGui::SliderFloat(T(TKEY("rain_wetness"), "Rain Wetness"), &settings.MaxRainWetness, 0.0f, 2.5f);
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();

		ImGui::SliderFloat(T(TKEY("puddle_wetness"), "Puddle Wetness"), &settings.MaxPuddleWetness, 0.0f, 6.0f);
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();

		ImGui::SliderFloat(T(TKEY("shore_wetness"), "Shore Wetness"), &settings.MaxShoreWetness, 0.0f, 1.0f);
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("raindrop_effects"), "Raindrop Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_raindrop_effects"), "Enable Raindrop Effects"), (bool*)&settings.EnableRaindropFx);

		ImGui::BeginDisabled(!settings.EnableRaindropFx);

		ImGui::Checkbox(T(TKEY("enable_splashes"), "Enable Splashes"), (bool*)&settings.EnableSplashes);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_splashes_tooltip"), "Enables small splashes of wetness on dry surfaces."));
		ImGui::Checkbox(T(TKEY("enable_ripples"), "Enable Ripples"), (bool*)&settings.EnableRipples);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_ripples_tooltip"), "Enables circular ripples on puddles, and to a less extent other wet surfaces"));

		ImGui::BeginDisabled(splashesOfStormsLoaded);
		const char* checkboxLabel = splashesOfStormsLoaded ?
		                                T(TKEY("enable_vanilla_ripples_controlled"), "Enable Vanilla Ripples - Controlled by Splashes of Storms") :
		                                T(TKEY("enable_vanilla_ripples"), "Enable Vanilla Ripples");

		if (ImGui::Checkbox(checkboxLabel, (bool*)&settings.EnableVanillaRipples)) {
			Ripples::UpdateSettings();  // Update cache when settings change
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({ T(TKEY("vanilla_ripples_tooltip_0"), "Enables default ripples (e.g., Ripples01)."),
				T(TKEY("vanilla_ripples_tooltip_1"), "Disabling may not take effect until the next weather change.") });
		}
		ImGui::EndDisabled();
		ImGui::SliderFloat(T(TKEY("effect_range"), "Effect Range"), &settings.RaindropFxRange, 1e2f, 2e3f, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto meters = Util::Units::GameUnitsToMeters(settings.RaindropFxRange);
			std::vector<std::string> tooltipLines = {
				T(TKEY("effect_range_tooltip"), "Range for raindrop effects"),
				Util::Units::FormatDistance(settings.RaindropFxRange),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(meters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}
		if (ImGui::TreeNodeEx(T(TKEY("raindrops"), "Raindrops"))) {
			ImGui::BulletText(
				"%s",
				T(TKEY("raindrops_help"), "At every interval, a raindrop is placed within each grid cell.\nOnly a set portion of raindrops will actually trigger splashes and ripples.\n"));

			ImGui::SliderFloat(T(TKEY("grid_size"), "Grid Size"), &settings.RaindropGridSize, 1.0f, 10.0f, "%.1f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					T(TKEY("grid_size_tooltip_0"), "Spatial grid size for raindrop placement (smaller = more grid cells, higher GPU cost)"),
					T(TKEY("grid_size_tooltip_1"), "This is the most performance-sensitive setting. Lower only if needed for realism."),
					Util::Units::FormatDistance(settings.RaindropGridSize)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}
			ImGui::SliderFloat(T(TKEY("interval"), "Interval"), &settings.RaindropInterval, 0.1f, 2.0f, "%.1f sec");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("interval_tooltip"), "How often raindrop effects are checked (lower = more frequent, moderate performance impact)"));
			}
			ImGui::SliderFloat(T(TKEY("chance"), "Chance"), &settings.RaindropChance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("chance_tooltip"), "Portion of raindrops that will actually cause splashes and ripples. Higher values increase effect density but have the least performance impact."));
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx(T(TKEY("splashes"), "Splashes"))) {
			ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.SplashesStrength, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("min_radius"), "Min Radius"), &settings.SplashesMinRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("max_radius"), "Max Radius"), &settings.SplashesMaxRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("lifetime"), "Lifetime"), &settings.SplashesLifetime, 0.1f, 20.f, "%.1f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx(T(TKEY("ripples"), "Ripples"))) {
			ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.RippleStrength, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("radius"), "Radius"), &settings.RippleRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("breadth"), "Breadth"), &settings.RippleBreadth, 0.f, 1.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("lifetime"), "Lifetime"), &settings.RippleLifetime, 0.f, settings.RaindropInterval, "%.2f sec", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}

		ImGui::EndDisabled();

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("advanced"), "Advanced"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("weather_transition_speed"), "Weather transition speed"), &settings.WeatherTransitionSpeed, 0.2f, 8.0f);
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("weather_transition_speed_tooltip"), "How fast wetness appears when raining and how quickly it dries after rain has stopped."));
		}

		ImGui::SliderFloat(T(TKEY("min_rain_wetness"), "Min Rain Wetness"), &settings.MinRainWetness, 0.0f, 0.9f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("min_rain_wetness_tooltip"), "The minimum amount an object gets wet from rain."));
		}

		ImGui::SliderFloat(T(TKEY("skin_wetness"), "Skin Wetness"), &settings.SkinWetness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("skin_wetness_tooltip"), "How wet character skin and hair get during rain."));
		}
		ImGui::SliderInt(T(TKEY("shore_range"), "Shore Range"), (int*)&settings.ShoreRange, 1, 64);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto meters = Util::Units::GameUnitsToMeters(static_cast<float>(settings.ShoreRange));
			std::vector<std::string> tooltipLines = {
				T(TKEY("shore_range_tooltip"), "The maximum distance from a body of water that Shore Wetness affects"),
				Util::Units::FormatDistance(static_cast<float>(settings.ShoreRange)),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(meters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}
		ImGui::SliderFloat(T(TKEY("puddle_radius"), "Puddle Radius"), &settings.PuddleRadius, 0.3f, 3.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto puddleMeters = Util::Units::GameUnitsToMeters(settings.PuddleRadius);
			std::vector<std::string> tooltipLines = {
				T(TKEY("puddle_radius_tooltip"), "The radius used to determine puddle size and location"),
				Util::Units::FormatDistance(settings.PuddleRadius),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(puddleMeters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}

		ImGui::SliderFloat(T(TKEY("puddle_max_angle"), "Puddle Max Angle"), &settings.PuddleMaxAngle, 0.6f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("puddle_max_angle_tooltip"), "How flat a surface needs to be for puddles to form on it."));
		}

		ImGui::SliderFloat(T(TKEY("puddle_min_wetness"), "Puddle Min Wetness"), &settings.PuddleMinWetness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("puddle_min_wetness_tooltip"), "The wetness value at which puddles start to form."));
		}

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();
	auto& csEditor = globals::features::csEditor;
	if (csEditor.loaded) {
		if (ImGui::SmallButton(T(TKEY("open_weather_picker"), "Open Weather Picker"))) {
			// Navigate to the replacement feature in the menu
			Menu::GetSingleton()->SelectFeatureMenu(csEditor.GetShortName());
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("open_weather_picker_tooltip"), "Open the Weather Picker in CS Utility"));
		}
	}

	if (ImGui::TreeNodeEx(T(TKEY("debug"), "Debug"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_wetness_override"), "Enable Wetness Override"), &debugSettings.EnableWetnessOverride);
		ImGui::Checkbox(T(TKEY("enable_puddle_override"), "Enable Puddle Override"), &debugSettings.EnablePuddleOverride);
		ImGui::Checkbox(T(TKEY("enable_rain_override"), "Enable Rain Override"), &debugSettings.EnableRainOverride);
		ImGui::Checkbox(T(TKEY("enable_interior_exterior_override"), "Enable Interior/Exterior Override"), &debugSettings.EnableIntExOverride);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s", T(TKEY("interior_exterior_override_tooltip"), "If disabled, will only use the exterior value. "));
		}

		if (debugSettings.EnableWetnessOverride) {
			ImGui::SliderFloat2(T(TKEY("wetness_in_exterior"), "Wetness In/Exterior"), &debugSettings.WetnessOverride.x, 0.0f, 2.0f);
		}

		if (debugSettings.EnablePuddleOverride) {
			ImGui::SliderFloat2(T(TKEY("puddle_wetness_in_exterior"), "Puddle Wetness In/Exterior"), &debugSettings.PuddleWetnessOverride.x, 0.0f, 2.0f);
		}

		if (debugSettings.EnableRainOverride) {
			ImGui::SliderFloat2(T(TKEY("rain_in_exterior"), "Rain In/Exterior"), &debugSettings.RainOverride.x, 0.0f, 1.0f);
		}
		ImGui::TreePop();
	}
}

// =====================
// UI/ImGui Helper Functions
// =====================

// Helper for meteorological rain type classification
static void DrawRainTypeLabel(const char* prefix, float rate)
{
	// Meteorological categories (mm/hr):
	// Light: <2.5, Moderate: 2.5-7.5, Heavy: 7.5-15, Extreme: >15
	const char* label = "";
	ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	Util::ColorCodedValueConfig config;
	config.format = "%.2f mm/hr";
	config.sameLine = true;
	config.tooltipText = nullptr;
	config.thresholds = {
		{ 2.5f, ImVec4(0.5f, 0.7f, 1.0f, 1.0f) },    // Light (Blue)
		{ 7.5f, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) },    // Moderate (Green)
		{ 15.0f, ImVec4(1.0f, 0.7f, 0.2f, 1.0f) },   // Heavy (Orange)
		{ FLT_MAX, ImVec4(1.0f, 0.2f, 0.2f, 1.0f) }  // Extreme (Red)
	};
	if (rate < 2.5f) {
		label = "Light Rain";
		valueColor = config.thresholds[0].color;
	} else if (rate < 7.5f) {
		label = "Moderate Rain";
		valueColor = config.thresholds[1].color;
	} else if (rate < 15.0f) {
		label = "Heavy Rain";
		valueColor = config.thresholds[2].color;
	} else {
		label = "Extreme Rain";
		valueColor = config.thresholds[3].color;
	}
	// Print prefix (uncolored), then value (colored), then meteorological label (colored, after value)
	ImGui::Text("%s:", prefix);
	ImGui::SameLine();
	ImGui::TextColored(valueColor, config.format, rate);
	ImGui::SameLine();
	ImGui::TextColored(valueColor, "(%s)", label);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Meteorological rain types:");
		ImGui::BulletText("Light: <2.5 mm/hr");
		ImGui::BulletText("Moderate: 2.5 - 7.5 mm/hr");
		ImGui::BulletText("Heavy: 7.5 - 15 mm/hr");
		ImGui::BulletText("Extreme: >15 mm/hr");
	}
}

// =====================
// Weather/Precipitation Analysis Helpers
// =====================

static float linearstep(float edge0, float edge1, float x)
{
	if (edge0 >= edge1)
		return x >= edge1 ? 1.0f : 0.0f;
	return std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
}

float WetnessEffects::GetRainIntensity(RE::NiPointer<RE::BSGeometry> precipObject, RE::TESWeather* weather)
{
	if (!precipObject || !weather || !weather->precipitationData) {
		return 0.0f;
	}

	auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
	auto shaderProp = effect.get();
	auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);

	if (!particleShaderProperty || !particleShaderProperty->particleEmitter) {
		return 0.0f;
	}

	auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
	if (!rain->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain)) {
		return 0.0f;
	}

	auto maxDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;  // Use weather particle density as authoritative source for rain intensity
	// This provides consistent intensity scaling based on weather type (1-3 scale)
	// Note: rain->density equals maxDensity when fully active
	return (maxDensity > 0.0f) ? std::min(1.0f, maxDensity / MAX_RAIN_PARTICLE_DENSITY) : 0.0f;
}

WetnessEffects::WeatherWetnessResult WetnessEffects::CalculateWeatherWetness(RE::TESWeather* weather, float weatherPct, bool isCurrentWeather) const
{
	WeatherWetnessResult result{};

	if (!weather || !weather->precipitationData || !weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
		return result;
	}

	if (isCurrentWeather) {
		// Current weather uses fade-in logic
		float fadeValue = weather->data.precipitationBeginFadeIn;
		float fadeNormalized = fadeValue / 255.0f;
		float fadeThreshold = 255.0f * (1.0f - fadeNormalized);
		float weatherProgress = weatherPct * 255.0f;

		if (fadeNormalized == 0.0f) {
			// No fade-in period, use immediate wetness
			result.wetness = (weatherPct > 0.1f) ? 1.0f : 0.0f;
		} else {
			result.wetness = linearstep(fadeThreshold, 255.0f, weatherProgress);
		}
		result.puddleWetness = pow(result.wetness, 2.0f);
	} else {
		// Last weather uses fade-out logic
		float fadeValue = weather->data.precipitationEndFadeOut;
		float fadeNormalized = fadeValue / 255.0f;
		float fadeThreshold = 255.0f * fadeNormalized;
		float weatherProgress = weatherPct * 255.0f;

		result.wetness = 1.0f - linearstep(fadeThreshold, 255.0f, weatherProgress);
		result.puddleWetness = pow(std::max(result.wetness, 1.0f - weatherPct), 0.25f);
	}
	return result;
}

// Helper function to calculate precipitation rate from shader data and settings
float WetnessEffects::CalculatePrecipitationRate(float raindropChance, float raindropGridSizeGameUnits, float raindropIntervalSeconds, float mlPerDrop) const
{
	// Validate inputs to prevent division by zero and invalid calculations
	if (raindropGridSizeGameUnits <= 0.0f || raindropIntervalSeconds <= 0.0f) {
		logger::warn("[WetnessEffects] Invalid parameters: gridSize={}, interval={}", raindropGridSizeGameUnits, raindropIntervalSeconds);
		return 0.0f;
	}

	if (raindropChance < 0.0f || raindropChance > 1.0f) {
		logger::warn("[WetnessEffects] Invalid raindrop chance: {}, clamping to [0,1]", raindropChance);
		raindropChance = std::clamp(raindropChance, 0.0f, 1.0f);
	}
	// Use physically realistic default if not specified (10 microliters typical for large raindrop)
	if (mlPerDrop <= 0.0f)
		mlPerDrop = 0.01f;
	// Convert grid size from game units to meters
	float gridSizeMeters = Util::Units::GameUnitsToMeters(raindropGridSizeGameUnits);
	float gridAreaSqMeters = gridSizeMeters * gridSizeMeters;
	// Calculate drops per second per grid cell
	float dropsPerSecond = raindropChance / raindropIntervalSeconds;
	float dropsPerSqMeterPerSec = dropsPerSecond / gridAreaSqMeters;
	// Convert to equivalent mm/hr precipitation rate
	// 1 mm/hr = 1 L/m^2/hr = 1 mL/cm^2/hr = 1 mm depth
	const float SEC_PER_HOUR = 3600.0f;
	const float SQ_M_TO_SQ_CM = 10000.0f;
	return dropsPerSqMeterPerSec * mlPerDrop * SEC_PER_HOUR / SQ_M_TO_SQ_CM;
}

// =====================
// Preset/Configuration Helpers
// =====================

const WetnessEffects::ClimateSettings& WetnessEffects::GetClimateSettings(ClimatePreset preset)
{
	auto index = static_cast<size_t>(preset);
	if (index >= CLIMATE_PRESETS.size()) {
		index = magic_enum::enum_integer<ClimatePreset>(defaultPreset);  // Use defaultPreset
	}
	return CLIMATE_PRESETS[index];
}

void WetnessEffects::ApplyClimatePreset(ClimatePreset preset)
{
	const auto& climate = GetClimateSettings(preset);

	// Update the climate preset
	climatePreset = preset;

	// Set settings to preset base values instead of multiplying existing values
	// This ensures consistent, predictable behavior
	Settings defaultSettings{};  // Get default values

	settings.MaxRainWetness = defaultSettings.MaxRainWetness * climate.wetnessMultiplier;
	settings.MaxPuddleWetness = defaultSettings.MaxPuddleWetness * climate.puddleMultiplier;
	settings.WeatherTransitionSpeed = defaultSettings.WeatherTransitionSpeed * climate.transitionSpeed;

	// Use the preset's raindrop chance, grid size, and interval as the base values
	settings.RaindropChance = climate.raindropChance;
	settings.RaindropGridSize = climate.raindropGridSize;
	settings.RaindropInterval = climate.raindropInterval;

	// Removed clamping for all settings to allow full preset range
}

WetnessEffects::PerFrame WetnessEffects::GetCommonBufferData() const
{
	PerFrame data{};

	data.Raining = 0.0f;
	data.Wetness = 0.0f;
	data.PuddleWetness = 0.0f;

	if (settings.EnableWetnessEffects) {
		if (auto sky = globals::game::sky) {
			if (sky->mode.get() == RE::Sky::Mode::kFull) {
				if (auto precip = sky->precip) {
					{
						auto precipObject = precip->currentPrecip;
						if (!precipObject) {
							precipObject = precip->lastPrecip;
						}
						if (precipObject) {
							auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
							auto shaderProp = effect.get();
							auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);
							auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
							data.OcclusionViewProj = rain->occlusionProjection;
						}
					}

					// Extract rain intensities using helper function to avoid code duplication
					float currentRaining = 0.0f;
					float lastRaining = 0.0f;

					if (precip->currentPrecip && sky->currentWeather) {
						currentRaining = GetRainIntensity(precip->currentPrecip, sky->currentWeather);
					}

					if (precip->lastPrecip && sky->lastWeather) {
						lastRaining = GetRainIntensity(precip->lastPrecip, sky->lastWeather);
					}

					// Apply the same fade-in/fade-out thresholds the game uses in
					// Sky::IsRaining() to prevent CS effects appearing before the
					// game's own rain particles become visible, with a smooth ramp
					// from the threshold to full intensity.
					float currentWeight = 0.0f;
					if (sky->currentWeather && sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
						float fadeInThreshold = sky->currentWeather->data.precipitationBeginFadeIn * (1.0f / 255.0f);
						currentWeight = linearstep(fadeInThreshold, 1.0f, sky->currentWeatherPct);
					}

					float lastWeight = 0.0f;
					if (sky->lastWeather && sky->lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
						float fadeOutThreshold = sky->lastWeather->data.precipitationEndFadeOut * (1.0f / 255.0f);
						lastWeight = 1.0f - linearstep(0.0f, fadeOutThreshold, sky->currentWeatherPct);
					}

					data.Raining = (currentRaining * currentWeight) + (lastRaining * lastWeight);
				}

				WeatherWetnessResult currentWeatherResult = CalculateWeatherWetness(sky->currentWeather, sky->currentWeatherPct, true);
				WeatherWetnessResult lastWeatherResult = CalculateWeatherWetness(sky->lastWeather, sky->currentWeatherPct, false);
				float combinedWetness = std::min(1.0f, currentWeatherResult.wetness + lastWeatherResult.wetness);
				float combinedPuddleWetness = std::min(1.0f, currentWeatherResult.puddleWetness + lastWeatherResult.puddleWetness);
				data.Wetness = combinedWetness;
				data.PuddleWetness = combinedPuddleWetness;
				if (debugSettings.EnableWetnessOverride) {
					data.Wetness = debugSettings.WetnessOverride.y;
				}
				if (debugSettings.EnablePuddleOverride) {
					data.PuddleWetness = debugSettings.PuddleWetnessOverride.y;
				}
				if (debugSettings.EnableRainOverride) {
					data.Raining = debugSettings.RainOverride.y;
				}
			} else {
				if (debugSettings.EnableWetnessOverride) {
					data.Wetness = debugSettings.EnableIntExOverride ? debugSettings.WetnessOverride.x : debugSettings.WetnessOverride.y;
				}
				if (debugSettings.EnablePuddleOverride) {
					data.PuddleWetness = debugSettings.EnableIntExOverride ? debugSettings.PuddleWetnessOverride.x : debugSettings.PuddleWetnessOverride.y;
				}
				if (debugSettings.EnableRainOverride) {
					data.Raining = debugSettings.EnableIntExOverride ? debugSettings.RainOverride.x : debugSettings.RainOverride.y;
				}
			}
		}
	}

	static size_t rainTimer = 0;  // size_t for precision
	if (!globals::game::ui->GameIsPaused())
		rainTimer += (size_t)(RE::GetSecondsSinceLastFrame() * 1000);  // BSTimer::delta is always 0 for some reason
	data.Time = rainTimer / 1000.f;

	data.settings = settings;
	data.settings.MaxShoreWetness = settings.EnableWetnessEffects ? settings.MaxShoreWetness : 0.0f;
	data.settings.RaindropChance *= data.Raining * data.Raining;
	data.settings.RaindropGridSize = 1.0f / settings.RaindropGridSize;
	data.settings.RaindropInterval = 1.0f / settings.RaindropInterval;
	data.settings.RippleLifetime = settings.RaindropInterval / settings.RippleLifetime;

	return data;
}

void WetnessEffects::Prepass()
{
	static auto renderer = globals::game::renderer;
	static auto& precipOcclusionTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];

	auto context = globals::d3d::context;

	context->PSSetShaderResources(70, 1, &precipOcclusionTexture.depthSRV);
}

void WetnessEffects::LoadSettings(json& o_json)
{
	settings = o_json;

	// Auto-detect which preset matches the loaded settings
	DetectCurrentPreset();

	Ripples::UpdateSettings();  // Sync cached values after loading

	if (o_json.contains("DebugSettings")) {
		debugSettings = o_json["DebugSettings"].get<DebugSettings>();
	}
}

void WetnessEffects::SaveSettings(json& o_json)
{
	o_json = settings;

	o_json["DebugSettings"] = debugSettings;
}

void WetnessEffects::RestoreDefaultSettings()
{
	settings = {};
	climatePreset = defaultPreset;

	// Apply the default climate preset to ensure settings reflect the preset values
	ApplyClimatePreset(climatePreset);

	Ripples::UpdateSettings();  // Sync cached values after restoring defaults
}

void WetnessEffects::DrawWeatherAnalysis() const
{
	// Only show rain system analysis when it's raining and wetness effects are enabled
	if (!settings.EnableWetnessEffects)
		return;

	auto sky = globals::game::sky;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->IsRaining())
		return;

	// Get the current frame data (reuses already calculated values)
	auto frameData = GetCommonBufferData();

	// Get weather particle density for precipitation calculations
	float weatherMaxParticleDensity = 0.0f;
	if (sky->currentWeather && sky->currentWeather->precipitationData) {
		weatherMaxParticleDensity = sky->currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	}

	// Check last weather if transitioning
	if (weatherMaxParticleDensity <= 0.0f && sky->lastWeather && sky->lastWeather->precipitationData) {
		weatherMaxParticleDensity = sky->lastWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	}
	// Consolidated Shader & Weather Analysis
	{
		// Climate Preset Information Section
		DrawWeatherAnalysisLabel(T(TKEY("current_climate_preset"), "Current Climate Preset"));
		{
			// const auto& climate = GetClimateSettings(climatePreset); // Unused, remove to fix warning treated as error
			const auto& presetInfo = CLIMATE_PRESET_INFO[static_cast<size_t>(climatePreset)];

			ImGui::Text("Active Preset: %s", presetInfo.name);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", presetInfo.shortDescription);
			}

			ImGui::Text("Precipitation Rate Calculation");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				Util::DrawMultiLineTooltip({ "Precipitation rates are calculated using shader mechanics:",
					"- Raindrop chance (probability per interval)",
					"- Grid size (spatial density)",
					"- Interval (time between attempts)",
					"- All values reflect what is sent to the shader.",
					"Rates are shown in mm/hr, based on drops/sec and grid size." });
			}

			// Show current preset-applied values vs defaults
			Settings defaultSettings{};
			ImGui::Text("Current Settings (applied from preset):");
			ImGui::Indent();
			ImGui::Text("Rain Wetness: %.2f (default %.2f × %.1fx)", settings.MaxRainWetness, defaultSettings.MaxRainWetness, presetInfo.settings.wetnessMultiplier);
			ImGui::Text("Puddle Wetness: %.2f (default %.2f × %.1fx)", settings.MaxPuddleWetness, defaultSettings.MaxPuddleWetness, presetInfo.settings.puddleMultiplier);
			ImGui::Text("Transition Speed: %.2f (default %.2f × %.1fx)", settings.WeatherTransitionSpeed, defaultSettings.WeatherTransitionSpeed, presetInfo.settings.transitionSpeed);
			ImGui::Text("Raindrop Chance: %.1f%% (preset value)", settings.RaindropChance * 100.0f);
			ImGui::Unindent();
		}
		ImGui::Spacing();
		DrawWeatherAnalysisLabel(T(TKEY("rain_system_state"), "Rain System State"));
		if (sky->currentWeather) {
			float gridSizeGameUnits = 1.0f / frameData.settings.RaindropGridSize;
			float gridSizeMeters = Util::Units::GameUnitsToMeters(gridSizeGameUnits);
			float intervalSeconds = 1.0f / frameData.settings.RaindropInterval;
			float weatherBasedRainRate = CalculatePrecipitationRate(frameData.settings.RaindropChance, gridSizeGameUnits, intervalSeconds);
			float actualRainRate = weatherBasedRainRate;

			// Theoretical max using preset values and intensity = 1.0
			const auto& presetSettings = GetClimateSettings(climatePreset);
			float theoreticalMaxRainRate = CalculatePrecipitationRate(
				presetSettings.raindropChance, presetSettings.raindropGridSize, presetSettings.raindropInterval);

			if (ImGui::BeginTable("RainAnalysis", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
				ImGui::TableSetupColumn("Current Shader State", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableSetupColumn("Precipitation Analysis", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				Util::DrawColorCodedValue("Rain Intensity", frameData.Raining * 100.0f, std::format("{:.1f}%", frameData.Raining * 100.0f), Util::ColorCodedValueConfig::HighIsGood(10.0f, 50.0f, 80.0f));
				Util::DrawColorCodedValue("Wetness", frameData.Wetness * 100.0f, std::format("{:.1f}%", frameData.Wetness * 100.0f), Util::ColorCodedValueConfig::HighIsGood(25.0f, 60.0f, 85.0f));
				Util::DrawColorCodedValue("Puddle Wetness", frameData.PuddleWetness * 100.0f, std::format("{:.1f}%", frameData.PuddleWetness * 100.0f), Util::ColorCodedValueConfig::HighIsGood(15.0f, 40.0f, 70.0f));
				ImGui::Text("Puddle Formation: %.1f%% min wetness", frameData.settings.PuddleMinWetness * 100.0f);
				ImGui::Text("Weather Transition: %.1f%%", sky->currentWeatherPct * 100.0f);
				ImGui::Text("Raindrop Chance: %.1f%%", frameData.settings.RaindropChance * 100.0f);
				ImGui::Text("Grid Size: %.2f m (%.1f units)", gridSizeMeters, gridSizeGameUnits);
				ImGui::Text("Interval: %.1f sec", intervalSeconds);

				ImGui::TableNextColumn();
				// Live (Current):
				DrawRainTypeLabel("Current", actualRainRate);
				// Max (in Heavy Rain):
				DrawRainTypeLabel("Max (in Heavy Rain)", theoreticalMaxRainRate);
				ImGui::EndTable();
			}
		}
	}
}

// Helper function to auto-detect which preset matches current settings
void WetnessEffects::DetectCurrentPreset()
{
	if (DoesCurrentSettingsMatchPreset(ClimatePreset::Legacy)) {
		climatePreset = ClimatePreset::Legacy;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::NordicStandard)) {
		climatePreset = ClimatePreset::NordicStandard;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::ArcticTundra)) {
		climatePreset = ClimatePreset::ArcticTundra;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::TemperateCoastal)) {
		climatePreset = ClimatePreset::TemperateCoastal;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::MonsoonExtreme)) {
		climatePreset = ClimatePreset::MonsoonExtreme;
	} else {
		climatePreset = ClimatePreset::Custom;
	}
}

bool WetnessEffects::DoesCurrentSettingsMatchPreset(ClimatePreset preset) const
{
	// Custom preset never matches (it means user has customized settings)
	if (preset == ClimatePreset::Custom) {
		return false;
	}

	const auto& climate = GetClimateSettings(preset);
	Settings defaultSettings{};  // Get default values

	// Calculate what the settings should be for this preset
	float expectedMaxRainWetness = defaultSettings.MaxRainWetness * climate.wetnessMultiplier;
	float expectedMaxPuddleWetness = defaultSettings.MaxPuddleWetness * climate.puddleMultiplier;
	float expectedWeatherTransitionSpeed = defaultSettings.WeatherTransitionSpeed * climate.transitionSpeed;
	float expectedRaindropChance = climate.raindropChance;

	const float tolerance = 0.001f;
	return (std::abs(settings.MaxRainWetness - expectedMaxRainWetness) < tolerance &&
			std::abs(settings.MaxPuddleWetness - expectedMaxPuddleWetness) < tolerance &&
			std::abs(settings.WeatherTransitionSpeed - expectedWeatherTransitionSpeed) < tolerance &&
			std::abs(settings.RaindropChance - expectedRaindropChance) < tolerance);
}

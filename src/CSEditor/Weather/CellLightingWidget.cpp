#include "CellLightingWidget.h"
#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	namespace CellLightingTab
	{
		constexpr const char* kBasic = "Basic";
		constexpr const char* kFog = "Fog";
		constexpr const char* kDalc = "DALC";
		constexpr const char* kInheritance = "Inheritance";
	}

	namespace CellLightingSetting
	{
		constexpr const char* kAmbientColor = "Ambient Color";
		constexpr const char* kDirectionalColor = "Directional Color";
		constexpr const char* kDirectionalFade = "Directional Fade";
		constexpr const char* kFogNearColor = "Fog Near Color";
		constexpr const char* kFogFarColor = "Fog Far Color";
		constexpr const char* kFogNear = "Fog Near";
		constexpr const char* kFogFar = "Fog Far";
		constexpr const char* kFogPower = "Fog Power";
		constexpr const char* kFogClampMax = "Fog Clamp (Max)";
		constexpr const char* kLightFadeStart = "Light Fade Start";
		constexpr const char* kLightFadeEnd = "Light Fade End";
		constexpr const char* kClipDistance = "Clip Distance";
		constexpr const char* kXYRotation = "XY Rotation";
		constexpr const char* kZRotation = "Z Rotation";
		constexpr const char* kInheritAmbientColor = "Inherit Ambient Color";
		constexpr const char* kInheritDirectionalColor = "Inherit Directional Color";
		constexpr const char* kInheritFogColor = "Inherit Fog Color";
		constexpr const char* kInheritFogNear = "Inherit Fog Near";
		constexpr const char* kInheritFogFar = "Inherit Fog Far";
		constexpr const char* kInheritDirectionalRotation = "Inherit Directional Rotation";
		constexpr const char* kInheritDirectionalFade = "Inherit Directional Fade";
		constexpr const char* kInheritClipDistance = "Inherit Clip Distance";
		constexpr const char* kInheritFogPower = "Inherit Fog Power";
		constexpr const char* kInheritFogMaxClamp = "Inherit Fog Max (Clamp)";
		constexpr const char* kInheritLightFadeDistances = "Inherit Light Fade Distances";
	}

	using Inherit = RE::INTERIOR_DATA::Inherit;

	/** @brief A lighting-template inheritance flag: its game bit, setting, saved key and checkbox label. */
	struct InheritFlag
	{
		Inherit bit;
		bool CellLightingSettings::* setting;
		const char* key;
		const char* label;
	};

	constexpr InheritFlag kInheritFlags[] = {
		{ Inherit::kAmbientColor, &CellLightingSettings::inheritAmbientColor, "ambientColor", CellLightingSetting::kInheritAmbientColor },
		{ Inherit::kDirectionalColor, &CellLightingSettings::inheritDirectionalColor, "directionalColor", CellLightingSetting::kInheritDirectionalColor },
		{ Inherit::kFogColor, &CellLightingSettings::inheritFogColor, "fogColor", CellLightingSetting::kInheritFogColor },
		{ Inherit::kFogNear, &CellLightingSettings::inheritFogNear, "fogNear", CellLightingSetting::kInheritFogNear },
		{ Inherit::kFogFar, &CellLightingSettings::inheritFogFar, "fogFar", CellLightingSetting::kInheritFogFar },
		{ Inherit::kDirectionalRotation, &CellLightingSettings::inheritDirectionalRotation, "directionalRotation", CellLightingSetting::kInheritDirectionalRotation },
		{ Inherit::kDirectionalFade, &CellLightingSettings::inheritDirectionalFade, "directionalFade", CellLightingSetting::kInheritDirectionalFade },
		{ Inherit::kClipDistance, &CellLightingSettings::inheritClipDistance, "clipDistance", CellLightingSetting::kInheritClipDistance },
		{ Inherit::kFogPower, &CellLightingSettings::inheritFogPower, "fogPower", CellLightingSetting::kInheritFogPower },
		{ Inherit::kFogMax, &CellLightingSettings::inheritFogMax, "fogMax", CellLightingSetting::kInheritFogMaxClamp },
		{ Inherit::kLightFadeDistances, &CellLightingSettings::inheritLightFadeDistances, "lightFadeDistances", CellLightingSetting::kInheritLightFadeDistances },
	};

	/** @brief Calls visit(section, key, field) for every saved setting; section names the nested object, or is null at the top level. */
	template <class Visit>
	void ForEachSavedField(CellLightingSettings& settings, Visit&& visit)
	{
		visit(nullptr, "ambient", settings.ambient);
		visit(nullptr, "directional", settings.directional);
		visit(nullptr, "fogColorNear", settings.fogColorNear);
		visit(nullptr, "fogColorFar", settings.fogColorFar);
		visit(nullptr, "fogNear", settings.fogNear);
		visit(nullptr, "fogFar", settings.fogFar);
		visit(nullptr, "fogPower", settings.fogPower);
		visit(nullptr, "fogClamp", settings.fogClamp);
		visit(nullptr, "directionalFade", settings.directionalFade);
		visit(nullptr, "clipDist", settings.clipDist);
		visit(nullptr, "lightFadeStart", settings.lightFadeStart);
		visit(nullptr, "lightFadeEnd", settings.lightFadeEnd);
		visit(nullptr, "directionalXY", settings.directionalXY);
		visit(nullptr, "directionalZ", settings.directionalZ);

		// Cell saves predate DALCSettings, so they keep their own key names.
		auto& dalc = settings.dalc;
		visit("dalc", "xPlus", dalc.directional[0].max);
		visit("dalc", "xMinus", dalc.directional[0].min);
		visit("dalc", "yPlus", dalc.directional[1].max);
		visit("dalc", "yMinus", dalc.directional[1].min);
		visit("dalc", "zPlus", dalc.directional[2].max);
		visit("dalc", "zMinus", dalc.directional[2].min);
		visit("dalc", "specular", dalc.specular);
		visit("dalc", "fresnelPower", dalc.fresnelPower);

		for (const auto& flag : kInheritFlags)
			visit("inherit", flag.key, settings.*flag.setting);
	}
}

void CellLightingWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow(true, true, false, nullptr, "##CellLightingSearch")) {
		DrawWidgetHeader("##CellLightingSearch", true, true);
		DrawSearchDropdown();
	}

	if (!cell || !cell->IsInteriorCell()) {
		Util::Text::Warning("%s", T(TKEY("not_interior_cell"), "This cell is not an interior cell."));
		ImGui::TextWrapped("%s", T(TKEY("cell_lighting_interior_only"), "Cell Lighting is only available for interior cells."));
	} else if (!cell->GetLighting()) {
		Util::Text::Error("%s", T(TKEY("no_lighting_data"), "No lighting data available for this cell."));
	} else {
		bool changed = false;

		if (Util::BeginPillTabBar("CellLightingTabs")) {
			const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride(CellLightingTab::kBasic);
			const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride(CellLightingTab::kFog);
			const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride(CellLightingTab::kDalc);
			const ImGuiTabItemFlags inheritFlags = GetTabFlagsForOverride(CellLightingTab::kInheritance);

			const char* inheritedTooltip = T(TKEY("inherited_from_lighting_template"), "Inherited from lighting template");
			auto drawInherited = [inheritedTooltip](bool inherited, auto draw) {
				return DrawInherited(inherited ? inheritedTooltip : nullptr, draw);
			};

			if (Util::BeginPillTabItem(T(TKEY("tab_basic"), "Basic"), nullptr, basicFlags)) {
				BeginScrollableContent("##BasicScroll");

				auto drawMatchedHeader = [&](bool matches, const char* label, auto draw) {
					if (matches && SearchHeader(label))
						draw();
				};

				drawMatchedHeader(MatchesAnySearch({ CellLightingSetting::kAmbientColor, CellLightingSetting::kDirectionalColor }), T(TKEY("ambient_directional"), "Ambient & Directional"), [&]() {
					changed |= drawInherited(settings.inheritAmbientColor, [&]() {
						return WeatherUtils::DrawColorEdit(CellLightingSetting::kAmbientColor, settings.ambient);
					});
					changed |= drawInherited(settings.inheritDirectionalColor, [&]() {
						return WeatherUtils::DrawColorEdit(CellLightingSetting::kDirectionalColor, settings.directional);
					});
				});

				drawMatchedHeader(MatchesAnySearch({ CellLightingSetting::kXYRotation, CellLightingSetting::kZRotation, CellLightingSetting::kDirectionalFade }), T(TKEY("directional_settings"), "Directional Settings"), [&]() {
					int xyDegrees = settings.directionalXY;
					int zDegrees = settings.directionalZ;
					if (DrawIfMatchesSearch(CellLightingSetting::kXYRotation, [&](const char* label) {
							return drawInherited(settings.inheritDirectionalRotation, [&]() {
								return DrawWithHighlight(label, [&]() {
									return ImGui::SliderInt(std::format("{}##{}", T(TKEY("xy_rotation"), "XY Rotation"), CellLightingSetting::kXYRotation).c_str(), &xyDegrees, 0, 360);
								});
							});
						})) {
						settings.directionalXY = static_cast<uint32_t>(xyDegrees);
						changed = true;
					}
					if (DrawIfMatchesSearch(CellLightingSetting::kZRotation, [&](const char* label) {
							return drawInherited(settings.inheritDirectionalRotation, [&]() {
								return DrawWithHighlight(label, [&]() {
									return ImGui::SliderInt(std::format("{}##{}", T(TKEY("z_rotation"), "Z Rotation"), CellLightingSetting::kZRotation).c_str(), &zDegrees, 0, 360);
								});
							});
						})) {
						settings.directionalZ = static_cast<uint32_t>(zDegrees);
						changed = true;
					}
					changed |= drawInherited(settings.inheritDirectionalFade, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kDirectionalFade, settings.directionalFade, 0.0f, 1.0f);
					});
				});

				drawMatchedHeader(MatchesAnySearch({ CellLightingSetting::kLightFadeStart, CellLightingSetting::kLightFadeEnd }), T(TKEY("light_fade"), "Light Fade"), [&]() {
					changed |= drawInherited(settings.inheritLightFadeDistances, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kLightFadeStart, settings.lightFadeStart, 0.0f, 163840.0f);
					});
					changed |= drawInherited(settings.inheritLightFadeDistances, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kLightFadeEnd, settings.lightFadeEnd, 0.0f, 163840.0f);
					});
				});

				drawMatchedHeader(MatchesAnySearch({ CellLightingSetting::kClipDistance }), T(TKEY("other"), "Other"), [&]() {
					changed |= drawInherited(settings.inheritClipDistance, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kClipDistance, settings.clipDist, 0.0f, 163840.0f);
					});
				});

				EndScrollableContent();
				Util::EndPillTabItem();
			}

			if (Util::BeginPillTabItem(T(TKEY("tab_fog"), "Fog"), nullptr, fogFlags)) {
				BeginScrollableContent("##FogScroll");

				DrawSearchSectionIfMatches(CellLightingSetting::kFogNearColor, [&](const char*) {
					changed |= drawInherited(settings.inheritFogColor, [&]() {
						return WeatherUtils::DrawColorEdit(CellLightingSetting::kFogNearColor, settings.fogColorNear);
					});
				});

				DrawSearchSectionIfMatches(CellLightingSetting::kFogFarColor, [&](const char*) {
					changed |= drawInherited(settings.inheritFogColor, [&]() {
						return WeatherUtils::DrawColorEdit(CellLightingSetting::kFogFarColor, settings.fogColorFar);
					});
				});

				DrawSearchSectionIfMatches(CellLightingSetting::kFogNear, [&](const char*) {
					changed |= drawInherited(settings.inheritFogNear, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogNear, settings.fogNear, 0.0f, 163840.0f);
					});
				});

				DrawSearchSectionIfMatches(CellLightingSetting::kFogFar, [&](const char*) {
					changed |= drawInherited(settings.inheritFogFar, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogFar, settings.fogFar, 0.0f, 163840.0f);
					});
				});

				DrawSearchSectionIfMatches(CellLightingSetting::kFogPower, [&](const char*) {
					changed |= drawInherited(settings.inheritFogPower, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogPower, settings.fogPower, 0.0f, 10.0f);
					});
				});

				DrawSearchSectionIfMatches(CellLightingSetting::kFogClampMax, [&](const char*) {
					changed |= drawInherited(settings.inheritFogMax, [&]() {
						return WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogClampMax, settings.fogClamp, 0.0f, 1.0f);
					});
				});

				EndScrollableContent();
				Util::EndPillTabItem();
			}

			if (Util::BeginPillTabItem(T(TKEY("tab_dalc"), "DALC"), nullptr, dalcFlags)) {
				BeginScrollableContent("##DALCScroll");
				changed |= DrawDALCControls(*this, settings.dalc, settings.inheritAmbientColor ? inheritedTooltip : nullptr);
				EndScrollableContent();
				Util::EndPillTabItem();
			}

			if (Util::BeginPillTabItem(T(TKEY("tab_inheritance"), "Inheritance"), nullptr, inheritFlags)) {
				BeginScrollableContent("##InheritanceScroll");
				ImGui::TextWrapped("%s", T(TKEY("inherit_flags_desc"), "These flags control which lighting properties are inherited from the cell's lighting template."));
				ImGui::Separator();
				for (const auto& flag : kInheritFlags)
					changed |= WeatherUtils::DrawCheckbox(flag.label, settings.*flag.setting);
				EndScrollableContent();
				Util::EndPillTabItem();
			}

			Util::EndPillTabBar();
		}

		AutoApply(changed);
	}
	ImGui::End();
}

void CellLightingWidget::LoadSettings()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	LoadSavedSettings([this] {
		ForEachSavedField(settings, [this](const char* section, const char* key, auto& field) {
			const json* node = &js;
			if (section) {
				const auto it = js.find(section);
				node = it != js.end() ? &*it : nullptr;
			}
			if (node && node->contains(key))
				node->at(key).get_to(field);
		});
	});
}

void CellLightingWidget::LoadFromGameSettings()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	ColorToFloat3(lighting->ambient, settings.ambient);
	ColorToFloat3(lighting->directional, settings.directional);
	ColorToFloat3(lighting->fogColorNear, settings.fogColorNear);
	ColorToFloat3(lighting->fogColorFar, settings.fogColorFar);

	settings.fogNear = lighting->fogNear;
	settings.fogFar = lighting->fogFar;
	settings.fogPower = lighting->fogPower;
	settings.fogClamp = lighting->fogClamp;

	settings.directionalFade = lighting->directionalFade;
	settings.clipDist = lighting->clipDist;
	settings.lightFadeStart = lighting->lightFadeStart;
	settings.lightFadeEnd = lighting->lightFadeEnd;
	settings.directionalXY = lighting->directionalXY;
	settings.directionalZ = lighting->directionalZ;

	settings.dalc.Read(lighting->directionalAmbientLightingColors);

	for (const auto& flag : kInheritFlags)
		settings.*flag.setting = lighting->lightingTemplateInheritanceFlags.any(flag.bit);
}

void CellLightingWidget::SaveSettings()
{
	ForEachSavedField(settings, [this](const char* section, const char* key, const auto& field) {
		(section ? js[section] : js)[key] = field;
	});
	originalSettings = settings;
}

void CellLightingWidget::ApplyChanges()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	// Apply basic colors
	Float3ToColor(settings.ambient, lighting->ambient);
	Float3ToColor(settings.directional, lighting->directional);
	Float3ToColor(settings.fogColorNear, lighting->fogColorNear);
	Float3ToColor(settings.fogColorFar, lighting->fogColorFar);

	// Apply fog properties
	lighting->fogNear = settings.fogNear;
	lighting->fogFar = settings.fogFar;
	lighting->fogPower = settings.fogPower;
	lighting->fogClamp = settings.fogClamp;

	// Apply advanced properties
	lighting->directionalFade = settings.directionalFade;
	lighting->clipDist = settings.clipDist;
	lighting->lightFadeStart = settings.lightFadeStart;
	lighting->lightFadeEnd = settings.lightFadeEnd;
	lighting->directionalXY = settings.directionalXY;
	lighting->directionalZ = settings.directionalZ;

	settings.dalc.Write(lighting->directionalAmbientLightingColors);

	lighting->lightingTemplateInheritanceFlags.reset();
	for (const auto& flag : kInheritFlags) {
		if (settings.*flag.setting)
			lighting->lightingTemplateInheritanceFlags.set(flag.bit);
	}
}

std::vector<Widget::SearchResult> CellLightingWidget::CollectSearchableSettings() const
{
	std::vector<std::string> inheritLabels;
	for (const auto& flag : kInheritFlags)
		inheritLabels.emplace_back(flag.label);
	return WeatherUtils::LabelSearchResults({
		{ CellLightingTab::kBasic, { CellLightingSetting::kAmbientColor, CellLightingSetting::kDirectionalColor,
									   CellLightingSetting::kXYRotation, CellLightingSetting::kZRotation, CellLightingSetting::kDirectionalFade,
									   CellLightingSetting::kLightFadeStart, CellLightingSetting::kLightFadeEnd, CellLightingSetting::kClipDistance } },
		{ CellLightingTab::kFog, { CellLightingSetting::kFogNearColor, CellLightingSetting::kFogFarColor,
									 CellLightingSetting::kFogNear, CellLightingSetting::kFogFar, CellLightingSetting::kFogPower, CellLightingSetting::kFogClampMax } },
		{ CellLightingTab::kDalc, { DALCSetting::kAll.begin(), DALCSetting::kAll.end() } },
		{ CellLightingTab::kInheritance, std::move(inheritLabels) },
	});
}

#undef I18N_KEY_PREFIX

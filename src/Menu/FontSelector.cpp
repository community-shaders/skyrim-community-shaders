#include "FontSelector.h"
#include "CSEditor/Browser/BrowserWidgets.h"
#include "Menu/FontAtlasState.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "ThemeManager.h"
#include "Utils/UI.h"

#include <format>

namespace MenuFonts::Selector
{
	namespace
	{
		/// "<Role> <Suffix>" label with a unique ID suffix, e.g. "Body Family##3".
		std::string RoleLabel(const char* key, const char* fallback, std::string_view roleDisplayName, size_t roleIndex)
		{
			return std::format("{}##{}", I18n::GetSingleton()->Format(key, { { "role", std::string(roleDisplayName) } }, fallback), roleIndex);
		}

		void ApplyRoleFontSelection(
			Menu::ThemeSettings& themeSettings,
			Menu::ThemeSettings::FontRoleSettings& roleSettings,
			Menu::FontRole role,
			const std::string& familyName,
			const Util::Fonts::StyleInfo& style)
		{
			roleSettings.Family = familyName;
			roleSettings.Style = style.style;
			roleSettings.File = style.file;
			if (role == Menu::FontRole::Body) {
				themeSettings.FontName = roleSettings.File;
			}
			MenuFonts::GetAtlasState().pendingFontReload = true;
		}

		const Util::Fonts::StyleInfo* ResolveFamilyDefaultStyle(const Util::Fonts::FamilyInfo& family)
		{
			if (const auto* regular = Util::Fonts::FindRegularStyle(family)) {
				return regular;
			}
			return family.styles.empty() ? nullptr : &family.styles.front();
		}

		bool FontFamilyPreviewSelectable(const Util::Fonts::FamilyInfo& family, bool isSelected)
		{
			ImGui::PushID(family.name.c_str());
			const Util::Fonts::StyleInfo* previewStyle = ResolveFamilyDefaultStyle(family);
			ImFontGuard previewFont(previewStyle ? GetPreviewFont(previewStyle->file) : nullptr);
			const bool clicked = Util::Selectable(family.displayName.c_str(), isSelected);
			ImGui::PopID();
			return clicked;
		}

		bool FontStylePreviewSelectable(const Util::Fonts::StyleInfo& styleInfo, bool isSelected)
		{
			ImGui::PushID(styleInfo.file.c_str());
			ImFontGuard previewFont(GetPreviewFont(styleInfo.file));
			const bool clicked = Util::Selectable(styleInfo.displayName.c_str(), isSelected);
			ImGui::PopID();
			return clicked;
		}

		void RenderFamilyCombo(
			Menu& menu,
			Menu::ThemeSettings& themeSettings,
			Menu::ThemeSettings::FontRoleSettings& roleSettings,
			const Util::Fonts::Catalog& catalog,
			Menu::FontRole role,
			size_t roleIndex,
			std::string_view roleDisplayName,
			int& familyIndex)
		{
			const char* familyPreview = catalog.families.empty() ? T("menu.settings.no_families", "No families") : catalog.families[familyIndex].displayName.c_str();
			const std::string familyLabel = RoleLabel("menu.settings.font_family_label", "{role} Family", roleDisplayName, roleIndex);

			ImFontGuard rolePreviewFont(menu.GetFont(role));
			ImGui::PushID("Family");
			if (!ImGui::BeginCombo(familyLabel.c_str(), familyPreview)) {
				ImGui::PopID();
				return;
			}

			if (catalog.families.empty()) {
				Util::Text::Disabled("%s", T("menu.settings.no_font_families_available", "No font families available"));
			} else {
				for (int i = 0; i < static_cast<int>(catalog.families.size()); ++i) {
					const bool isSelected = (i == familyIndex);
					if (FontFamilyPreviewSelectable(catalog.families[i], isSelected)) {
						familyIndex = i;
						if (!isSelected) {
							const auto& newFamily = catalog.families[i];
							if (const auto* defaultStyle = ResolveFamilyDefaultStyle(newFamily)) {
								ApplyRoleFontSelection(
									themeSettings,
									roleSettings,
									role,
									newFamily.name,
									*defaultStyle);
							} else {
								roleSettings.Family = newFamily.name;
								roleSettings.Style.clear();
								roleSettings.File.clear();
								MenuFonts::GetAtlasState().pendingFontReload = true;
							}
						}
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
			}

			ImGui::EndCombo();
			ImGui::PopID();
		}

		void RenderStyleCombo(
			Menu& menu,
			Menu::ThemeSettings& themeSettings,
			Menu::ThemeSettings::FontRoleSettings& roleSettings,
			const Util::Fonts::FamilyInfo& selectedFamily,
			Menu::FontRole role,
			size_t roleIndex,
			std::string_view roleDisplayName,
			int& styleIndex)
		{
			const char* stylePreview = selectedFamily.styles.empty() ? T("menu.settings.no_styles", "No styles") : selectedFamily.styles[styleIndex].displayName.c_str();
			const std::string styleLabel = RoleLabel("menu.settings.font_style_label", "{role} Style", roleDisplayName, roleIndex);

			ImFontGuard rolePreviewFont(menu.GetFont(role));
			ImGui::PushID("Style");
			if (!ImGui::BeginCombo(styleLabel.c_str(), stylePreview)) {
				ImGui::PopID();
				return;
			}

			for (int s = 0; s < static_cast<int>(selectedFamily.styles.size()); ++s) {
				const bool isSelected = (s == styleIndex);
				if (FontStylePreviewSelectable(selectedFamily.styles[s], isSelected)) {
					if (!isSelected) {
						ApplyRoleFontSelection(
							themeSettings,
							roleSettings,
							role,
							selectedFamily.name,
							selectedFamily.styles[s]);
					}
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
			ImGui::PopID();
		}
	}  // namespace

	void RenderRolePickers(
		Menu& menu,
		Menu::ThemeSettings& themeSettings,
		const Util::Fonts::Catalog& catalog,
		Menu::FontRole role,
		size_t roleIndex)
	{
		auto& roleSettings = themeSettings.FontRoles[roleIndex];
		const auto& descriptor = Menu::FontRoleDescriptors[roleIndex];

		int familyIndex = Util::Fonts::FindFamilyIndex(catalog, roleSettings.Family);
		RenderFamilyCombo(menu, themeSettings, roleSettings, catalog, role, roleIndex, descriptor.displayName, familyIndex);

		const Util::Fonts::FamilyInfo* selectedFamily =
			catalog.families.empty() ? nullptr : &catalog.families[familyIndex];

		if (selectedFamily && selectedFamily->styles.empty()) {
			Util::Text::Warning("%s", T("menu.settings.no_style_variants", "No style variants found for this family."));
		} else if (selectedFamily) {
			int styleIndex = Util::Fonts::FindStyleIndex(*selectedFamily, roleSettings.Style);
			RenderStyleCombo(menu, themeSettings, roleSettings, *selectedFamily, role, roleIndex, descriptor.displayName, styleIndex);
		}

		const std::string scaleLabel = RoleLabel("menu.settings.font_scale_label", "{role} Scale", descriptor.displayName, roleIndex);
		if (ImGui::SliderFloat(scaleLabel.c_str(), &roleSettings.SizeScale, ThemeManager::Constants::FONT_ROLE_SCALE_MIN, ThemeManager::Constants::FONT_ROLE_SCALE_MAX, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
			MenuFonts::GetAtlasState().pendingFontReload = true;
		}
		if (roleSettings.SizeScale != Menu::GetFontRoleDefaultScale(role)) {
			ImGui::SameLine();
			if (BrowserUI::IconButton("##ResetScale", Icons::TI(MenuIcons::kRestore), T("menu.settings.font_scale_reset_tooltip", "Reset to the default scale"))) {
				roleSettings.SizeScale = Menu::GetFontRoleDefaultScale(role);
				MenuFonts::GetAtlasState().pendingFontReload = true;
			}
		}

		if (role == Menu::FontRole::Title) {
			ImGui::SliderFloat(
				T("menu.settings.feature_header_scale", "Feature Header Scale"),
				&themeSettings.FeatureHeading.FeatureTitleScale,
				ThemeManager::Constants::SETTINGS_TITLE_SCALE_MIN,
				ThemeManager::Constants::SETTINGS_TITLE_SCALE_MAX,
				"%.1fx",
				ImGuiSliderFlags_AlwaysClamp);
			Util::HoverTip(T("menu.settings.feature_header_scale_tooltip", "Scale multiplier for feature title text in the Settings tab."));
			if (themeSettings.FeatureHeading.FeatureTitleScale != ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE) {
				ImGui::SameLine();
				if (BrowserUI::IconButton("##ResetFeatureHeaderScale", Icons::TI(MenuIcons::kRestore), T("menu.settings.font_scale_reset_tooltip", "Reset to the default scale")))
					themeSettings.FeatureHeading.FeatureTitleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
			}
		}
	}
}  // namespace MenuFonts::Selector

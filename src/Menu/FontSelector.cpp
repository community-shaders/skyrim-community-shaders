#include "FontSelector.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "ThemeManager.h"

#include <format>

namespace MenuFonts::Selector
{
	namespace
	{
		void ApplyRoleFontSelection(
			Menu::ThemeSettings& themeSettings,
			Menu::ThemeSettings::FontRoleSettings& roleSettings,
			Menu::FontRole role,
			const std::string& familyName,
			const Util::Fonts::StyleInfo& style,
			Menu& menu)
		{
			roleSettings.Family = familyName;
			roleSettings.Style = style.style;
			roleSettings.File = style.file;
			if (role == Menu::FontRole::Body) {
				themeSettings.FontName = roleSettings.File;
			}
			menu.pendingFontReload = true;
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
			const bool clicked = ImGui::Selectable(family.displayName.c_str(), isSelected);
			ImGui::PopID();
			return clicked;
		}

		bool FontStylePreviewSelectable(const Util::Fonts::StyleInfo& styleInfo, bool isSelected)
		{
			ImGui::PushID(styleInfo.file.c_str());
			ImFontGuard previewFont(GetPreviewFont(styleInfo.file));
			const bool clicked = ImGui::Selectable(styleInfo.displayName.c_str(), isSelected);
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
			const char* familyPreview = catalog.families.empty()
				? T("menu.settings.no_families", "No families")
				: catalog.families[familyIndex].displayName.c_str();
			const std::string familyLabel = std::format("{} Family##{}", roleDisplayName, roleIndex);

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
									*defaultStyle,
									menu);
							} else {
								roleSettings.Family = newFamily.name;
								roleSettings.Style.clear();
								roleSettings.File.clear();
								menu.pendingFontReload = true;
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
			const char* stylePreview = selectedFamily.styles.empty()
				? T("menu.settings.no_styles", "No styles")
				: selectedFamily.styles[styleIndex].displayName.c_str();
			const std::string styleLabel = std::format("{} Style##{}", roleDisplayName, roleIndex);

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
							selectedFamily.styles[s],
							menu);
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

		{
			FontRoleGuard sampleFont(role);
			ImGui::TextWrapped(
				"%s",
				T("menu.settings.font_preview_sample", "The quick brown fox jumps over the lazy dog."));
		}

		ImGui::TextDisabled(T("menu.settings.file_label", "File: %s"), roleSettings.File.c_str());

		const std::string scaleLabel = std::format("{} Scale##{}", descriptor.displayName, roleIndex);
		if (ImGui::SliderFloat(scaleLabel.c_str(), &roleSettings.SizeScale, 0.5f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
			menu.pendingFontReload = true;
		}
		ImGui::SameLine();
		const std::string resetLabel = std::format("Reset##Scale{}", roleIndex);
		if (ImGui::Button(resetLabel.c_str())) {
			roleSettings.SizeScale = Menu::GetFontRoleDefaultScale(role);
			menu.pendingFontReload = true;
		}

		if (role == Menu::FontRole::Title) {
			ImGui::SliderFloat(
				T("menu.settings.feature_header_scale", "Feature Header Scale"),
				&themeSettings.FeatureHeading.FeatureTitleScale,
				1.0f,
				3.0f,
				"%.1fx",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"%s",
					T("menu.settings.feature_header_scale_tooltip", "Scale multiplier for feature title text in the Settings tab."));
			}
			ImGui::SameLine();
			const std::string resetBtnLabel = std::string(T("menu.settings.reset", "Reset")) + "##FeatureHeaderScale";
			if (ImGui::Button(resetBtnLabel.c_str())) {
				themeSettings.FeatureHeading.FeatureTitleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
			}
		}
	}
}  // namespace MenuFonts::Selector

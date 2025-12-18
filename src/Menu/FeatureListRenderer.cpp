#include "FeatureListRenderer.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <ranges>

#include "Feature.h"
#include "FeatureIssues.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/ThemeManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Util.h"

namespace
{
	// Core built-in menu names that always appear first in the menu list
	constexpr std::array<const char*, 4> CORE_MENU_NAMES = { "Home", "General", "Advanced", "Display" };

	bool IsCoreMenu(const std::string& menuName)
	{
		return std::find(CORE_MENU_NAMES.begin(), CORE_MENU_NAMES.end(), menuName) != CORE_MENU_NAMES.end();
	}

	void SeparatorTextWithFont(const char* text, Menu::FontRole role)
	{
		MenuFonts::FontRoleGuard guard(role);
		ImGui::SeparatorText(text);
	}

	void SeparatorTextWithFont(const std::string& text, Menu::FontRole role)
	{
		SeparatorTextWithFont(text.c_str(), role);
	}

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}
}

void FeatureListRenderer::RenderFeatureList(
	float footerHeight,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));

	auto menuList = BuildMenuList(featureSearch, categoryExpansionStates, drawGeneralSettings, drawAdvancedSettings);

	HandlePendingFeatureSelection(pendingFeatureSelection, menuList, selectedMenu);

	// Create the table with two columns
	if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
		ImGui::TableSetupColumn("##MenuConfig", 0, 8);

		RenderLeftColumn(menuList, selectedMenu, featureSearch, categoryExpansionStates);
		RenderRightColumn(menuList, selectedMenu);

		ImGui::EndTable();
	}

	ImGui::EndChild();
}

std::vector<FeatureListRenderer::MenuFuncInfo> FeatureListRenderer::BuildMenuList(
	const std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	// Build the menu list
	auto& featureList = Feature::GetFeatureList();
	auto sortedFeatureList{ featureList };  // need a copy so the load order is not lost
	std::ranges::sort(sortedFeatureList, [](Feature* a, Feature* b) {
		return a->GetName() < b->GetName();
	});

	// Filter features by search string
	if (!featureSearch.empty()) {
		auto it = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
			[&featureSearch](Feature* feat) { return !Util::FeatureMatchesSearch(feat, featureSearch); });
		sortedFeatureList.erase(it, sortedFeatureList.end());
	}

	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ "Home", []() { HomePageRenderer::RenderHomePage(); } },
		BuiltInMenu{ "General", drawGeneralSettings },
		BuiltInMenu{ "Advanced", drawAdvancedSettings }
	};  // NOTE: The menu list is rebuilt every frame, so category expansion states
	// persist correctly. This is acceptable since the list is small and built
	// infrequently, but could be optimized if performance becomes an issue.

	// Group features by category
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;
	for (Feature* feat : sortedFeatureList) {
		if (feat->IsInMenu() && feat->loaded) {
			std::string category(feat->GetCategory());
			categorizedFeatures[category].push_back(feat);
		}
	}

	// Sort features within each category
	for (auto& [category, features] : categorizedFeatures) {
		std::ranges::sort(features, [](Feature* a, Feature* b) {
			return a->GetName() < b->GetName();
		});
	}

	// Define category order
	std::vector<std::string> categoryOrder = { "Display", "Debug", "Characters", "Grass", "Lighting", "Materials", "Post-Processing", "Sky & Weather", "Landscape & Textures", "Water", "Other" };
	// Add categorized features to menu with collapsible headers
	for (const std::string& category : categoryOrder) {
		if (categorizedFeatures.find(category) != categorizedFeatures.end() && !categorizedFeatures[category].empty()) {
			// Initialize expansion state if not exists
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;  // Default to expanded
			}

			// Add category header
			menuList.push_back(CategoryHeader{ category });

			// Add features only if category is expanded
			if (categoryExpansionStates[category]) {
				std::ranges::copy(categorizedFeatures[category], std::back_inserter(menuList));
			}
		}
	}

	// Add any categories not in the predefined order
	for (const auto& [category, features] : categorizedFeatures) {
		if (std::find(categoryOrder.begin(), categoryOrder.end(), category) == categoryOrder.end() && !features.empty()) {
			// Initialize expansion state if not exists
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;  // Default to expanded
			}

			// Add category header
			menuList.push_back(CategoryHeader{ category });

			// Add features only if category is expanded
			if (categoryExpansionStates[category]) {
				std::ranges::copy(features, std::back_inserter(menuList));
			}
		}
	}

	auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
		return !feat->loaded && feat->IsInMenu() && (!FeatureIssues::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
	});
	if (std::ranges::distance(unloadedFeatures) != 0) {
		menuList.push_back("Unloaded Features"s);
		std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
	}
	// Add top section for feature issues (rejected features, obsolete info, etc.)
	if (FeatureIssues::HasFeatureIssues()) {
		menuList.insert(menuList.begin(), BuiltInMenu{ "Feature Issues", []() {
														  FeatureIssues::DrawFeatureIssuesUI();
													  } });
	}

	return menuList;
}

void FeatureListRenderer::HandlePendingFeatureSelection(
	std::string& pendingFeatureSelection,
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu)
{
	if (!pendingFeatureSelection.empty()) {
		for (size_t i = 0; i < menuList.size(); ++i) {
			if (std::holds_alternative<Feature*>(menuList[i])) {
				Feature* feature = std::get<Feature*>(menuList[i]);
				if (feature->GetShortName() == pendingFeatureSelection) {
					selectedMenu = i;
					logger::info("Navigated to {} feature menu", pendingFeatureSelection);
					break;
				}
			}
		}
		pendingFeatureSelection.clear();  // Clear after processing
	}
}

void FeatureListRenderer::RenderLeftColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates)
{
	ImGui::TableNextColumn();
	// Draw the feature list
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
	if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
		// Find where core built-in menus end (Home, General, Advanced, Display)
		size_t coreMenuCount = 0;
		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (IsCoreMenu(menu.name)) {
					coreMenuCount++;
				}
			}
		}

		// First render the core built-in menus (Home, General, Advanced, Display)
		size_t renderedCoreMenus = 0;
		for (size_t i = 0; i < menuList.size() && renderedCoreMenus < CORE_MENU_NAMES.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (IsCoreMenu(menu.name)) {
					std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
					renderedCoreMenus++;
				}
			}
		}

		// Add Features header and search bar after built-in settings
		Util::DrawSectionHeader("Features", true);
		Util::DrawFeatureSearchBar(featureSearch);

		// Then render the rest (features and categories, but skip already rendered core menus)
		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (IsCoreMenu(menu.name)) {
					continue;  // Skip, already rendered
				}
			}
			std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
		}

		ImGui::EndListBox();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void FeatureListRenderer::RenderRightColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t selectedMenu)
{
	ImGui::TableNextColumn();
	ImGui::Dummy(ImVec2(0, ThemeManager::Constants::BUTTON_SPACING));  // spacing

	if (selectedMenu < menuList.size()) {
		std::visit(DrawMenuVisitor{}, menuList[selectedMenu]);
	} else {
		ImGui::TextDisabled("Please select an item on the left.");
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const BuiltInMenu& menu)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

	// Use error color for Feature Issues menu item
	bool isFeatureIssues = (menu.name == "Feature Issues");
	if (isFeatureIssues) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);

		if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
			selectedMenuRef = listId;

		ImGui::PopStyleColor();
	} else {
		if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
			selectedMenuRef = listId;
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const std::string& label)
{
	// Style "Unloaded Features" to match category headers
	if (label == "Unloaded Features") {
		Util::DrawSectionHeader(label.c_str(), true);
	} else {
		// Use default separator text for other labels - should be themed via ImGuiCol_Separator
		SeparatorTextWithFont(label, Menu::FontRole::Subheading);
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const CategoryHeader& header)
{
	// Get expansion state from static map
	bool isExpanded = categoryExpansionStates[header.name];

	// Draw category header with custom styling using util:UI function
	// Use Heading font for category headers
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
		int count = Menu::categoryCounts[std::string(header.name)];
		Util::DrawCategoryHeader(header.name.c_str(), isExpanded, count);
	}

	// Update expansion state
	categoryExpansionStates[header.name] = isExpanded;
}

void FeatureListRenderer::ListMenuVisitor::operator()(Feature* feat)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

	const auto featureName = feat->GetShortName();
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	auto& themeSettings = globals::menu->GetSettings().Theme;

	ImVec4 textColor;

	// Determine the text color based on the state
	if (isDisabled) {
		textColor = themeSettings.StatusPalette.Disable;
	} else if (isLoaded) {
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	} else if (hasFailedMessage) {
		textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : themeSettings.StatusPalette.Error;
	} else {
		// No failed message but not loaded - check if INI file exists
		if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
			// INI file missing - treat as missing feature (grey)
			textColor = themeSettings.StatusPalette.Disable;
		} else {
			// INI file exists but feature not loaded - truly pending restart (green)
			textColor = themeSettings.StatusPalette.RestartNeeded;
		}
	}

	// Create selectable item with semantic color
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	if (ImGui::Selectable(fmt::format(" {} ", feat->GetName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns)) {
		selectedMenuRef = listId;
	}
	ImGui::PopStyleColor();

	// Display version if loaded
	if (isLoaded) {
		ImGui::SameLine();
		std::string formattedVersion = feat->version;
		std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');
		ImGui::TextDisabled(fmt::format("({})", formattedVersion).c_str());
	}
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const BuiltInMenu& menu)
{
	if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
		menu.func();
	}
	ImGui::EndChild();
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const std::string&)
{
	// std::unreachable() from c++23
	// you are not supposed to have selected a label!
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
{
	// Category headers are not selectable in the right panel
	ImGui::TextDisabled("Please select a feature from the left.");
}

void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* feat)
{
	const auto featureName = feat->GetShortName();
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();

	float buttonPadding = ThemeManager::Constants::BUTTON_PADDING;
	float buttonSpacing = ThemeManager::Constants::BUTTON_SPACING;

	MenuFonts::TabBarPaddingGuard tabPaddingGuard(Menu::FontRole::Subheading);
	if (ImGui::BeginTabBar("##FeatureTabs", ImGuiTabBarFlags_Reorderable)) {
		// Render Settings and About tabs
		RenderFeatureSettingsTab(feat, isDisabled, isLoaded, hasFailedMessage);
		RenderFeatureAboutTab(feat, isDisabled, isLoaded, hasFailedMessage);

		// Render action buttons positioned on the right side of the tab bar
		RenderFeatureActionButtons(feat, isDisabled, isLoaded, buttonPadding, buttonSpacing);
	}
	ImGui::EndTabBar();
}

bool FeatureListRenderer::DrawMenuVisitor::IsFeatureInstalled(const std::string& featureName)
{
	return std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(featureName));
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureSettingsTab(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage)
{
	if (!BeginTabItemWithFont("Settings", Menu::FontRole::Subheading)) {
		return;
	}

	if (ImGui::BeginChild("##FeatureSettingsFrame", { 0, 0 }, true)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;

		SeparatorTextWithFont("Feature Settings", Menu::FontRole::Subheading);
		if (isDisabled) {
			ImGui::TextColored(themeSettings.StatusPalette.Disable, "Feature settings are hidden because this feature is disabled at boot.");
			ImGui::Spacing();
			ImGui::Text("Enable the feature above to access its configuration options.");
		} else {
			if (isLoaded) {
				ImVec2 cursorPosBefore = ImGui::GetCursorPos();
				feat->DrawSettings();
				ImVec2 cursorPosAfter = ImGui::GetCursorPos();

				const float epsilon = 0.1f;
				bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > epsilon ||
									std::abs(cursorPosAfter.y - cursorPosBefore.y) > epsilon);
				if (!cursorMoved) {
					ImGui::TextColored(themeSettings.StatusPalette.Disable, "There are no settings available for this feature.");
				}
			} else {
				if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
					feat->DrawUnloadedUI();
				} else if (IsFeatureInstalled(feat->GetShortName())) {
					ImGui::Text("This feature will be available after restart.");
				} else {
					feat->DrawUnloadedUI();
					if (!feat->GetFeatureModLink().empty()) {
						ImGui::Spacing();
						const auto downloadText = fmt::format("Click here to download this feature ({})", feat->GetFeatureModLink());
						if (ImGui::Selectable(downloadText.c_str())) {
							ShellExecuteA(NULL, "open", feat->GetFeatureModLink().c_str(), NULL, NULL, SW_SHOWNORMAL);
						}
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("Download the feature from the mod page.");
						}
					}
				}
			}
		}

		if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
			ImGui::Spacing();
			SeparatorTextWithFont("Error", Menu::FontRole::Subheading);
			ImGui::TextColored(themeSettings.StatusPalette.Error, feat->failedLoadedMessage.c_str());
		}

		if (!isDisabled && isLoaded) {
			// Position button in screen coordinates so it stays fixed in viewport when scrolling
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 windowSize = ImGui::GetWindowSize();
			float scrollbarWidth = ImGui::GetScrollMaxY() > 0 ? ImGui::GetStyle().ScrollbarSize : 0.0f;

			float iconDimension = ImGui::GetFrameHeight() * 1.2f;
			ImVec2 iconSize = ImVec2(iconDimension, iconDimension);

			float padding = 10.0f;
			ImVec2 buttonPos = ImVec2(
				windowPos.x + windowSize.x - iconSize.x - padding - scrollbarWidth,
				windowPos.y + windowSize.y - iconSize.y - padding);
			ImGui::SetCursorScreenPos(buttonPos);
			auto& theme = globals::menu->GetTheme().Palette;
			ImVec4 iconColor = theme.Text;
			iconColor.w *= 0.7f;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));

			auto& menu = *globals::menu;
			if (menu.uiIcons.featureSettingRevert.texture) {
				if (ImGui::ImageButton("##RestoreDefaults", menu.uiIcons.featureSettingRevert.texture, iconSize)) {
					feat->RestoreDefaultSettings();
				}
			} else {
				if (ImGui::Button("R##RestoreDefaults", iconSize)) {
					feat->RestoreDefaultSettings();
				}
			}

			ImGui::PopStyleColor(3);

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Restore default settings for this feature");
			}
		}
	}
	ImGui::EndChild();
	ImGui::EndTabItem();
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureAboutTab(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage)
{
	if (!BeginTabItemWithFont("About", Menu::FontRole::Subheading)) {
		return;
	}

	if (ImGui::BeginChild("##FeatureAboutFrame", { 0, 0 }, true)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;

		SeparatorTextWithFont("Status", Menu::FontRole::Subheading);

		ImVec4 statusColor;
		const char* statusText;
		if (isDisabled) {
			statusColor = themeSettings.StatusPalette.Disable;
			statusText = "Disabled at boot.";
		} else if (hasFailedMessage) {
			statusColor = themeSettings.StatusPalette.Error;
			statusText = "Failed to load.";
		} else if (!isLoaded) {
			if (!IsFeatureInstalled(feat->GetShortName())) {
				statusColor = themeSettings.StatusPalette.Error;
				statusText = "Not installed.";
			} else {
				statusColor = themeSettings.StatusPalette.RestartNeeded;
				statusText = "Pending restart.";
			}
		} else {
			statusColor = themeSettings.StatusPalette.SuccessColor;
			statusText = "Active.";
		}

		ImGui::TextColored(statusColor, "Current State: %s", statusText);

		if (isLoaded) {
			auto [description, keyFeatures] = feat->GetFeatureSummary();
			if (!description.empty()) {
				ImGui::Spacing();
				SeparatorTextWithFont("Description", Menu::FontRole::Subheading);
				ImGui::TextWrapped("%s", description.c_str());

				if (!keyFeatures.empty()) {
					ImGui::Spacing();
					SeparatorTextWithFont("Key Features", Menu::FontRole::Subheading);
					for (const auto& feature : keyFeatures) {
						ImGui::BulletText("%s", feature.c_str());
					}
				}
			}
		} else {
			ImGui::Spacing();
			SeparatorTextWithFont("Information", Menu::FontRole::Subheading);
			if (hasFailedMessage) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
			} else if (!IsFeatureInstalled(feat->GetShortName())) {
				ImGui::Text("Feature installation details are available in the Settings tab.");
			} else {
				ImGui::Text("This feature is pending restart.");
			}
		}
	}
	ImGui::EndChild();
	ImGui::EndTabItem();
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureActionButtons(Feature* feat, bool isDisabled, bool isLoaded, float buttonPadding, float buttonSpacing)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	const auto featureName = feat->GetShortName();

	// Calculate button widths based on text content
	const char* overrideButtonText = "Apply Override";

	// Toggle is more compact without label - just the toggle width
	float bootToggleWidth = ImGui::GetFrameHeight() * 1.6f;
	float overrideButtonWidth = ImGui::CalcTextSize(overrideButtonText).x + buttonPadding;

	// Check if override is available for this feature
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	bool hasOverrides = overrideManager && overrideManager->HasFeatureOverrides(featureName);

	float totalButtonWidth = bootToggleWidth;
	if (!isDisabled && isLoaded && hasOverrides) {
		totalButtonWidth += overrideButtonWidth + buttonSpacing;
	}

	// Position buttons on the right side of the tab bar
	ImGui::SameLine();
	float availableSpace = ImGui::GetContentRegionAvail().x;
	float rightOffset = availableSpace - totalButtonWidth;
	if (rightOffset > 0) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rightOffset);
	}

	// Enable/Disable at boot toggle
	bool bootEnabled = !isDisabled;

	// Apply disabled styling if feature has failed to load
	if (!feat->failedLoadedMessage.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);
	}

	if (Util::FeatureToggle("##BootToggle", &bootEnabled)) {
		bool newState = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", featureName, newState ? "Enabled" : "Disabled");
	}

	if (!feat->failedLoadedMessage.empty()) {
		ImGui::PopStyleColor();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Toggle feature loading at boot.\n"
			"Current state: %s\n"
			"Restart required for changes to take effect.\n"
			"Disabling removes performance impact.",
			bootEnabled ? "Enabled" : "Disabled");
	}

	// Apply Override button (when feature has available overrides)
	if (!isDisabled && isLoaded && hasOverrides) {
		ImGui::SameLine();
		if (ImGui::Button(overrideButtonText, { overrideButtonWidth, 0 })) {
			if (feat->ReapplyOverrideSettings()) {
				logger::info("Successfully reapplied override settings for {}", featureName);
			} else {
				logger::warn("Failed to reapply override settings for {}", featureName);
			}
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Reapplies override settings from mod override JSON files. "
				"This will overwrite current settings with override values. "
				"You will still need to Save Settings to make these changes permanent.");
		}
	}
}
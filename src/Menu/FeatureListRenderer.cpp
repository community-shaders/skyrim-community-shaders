#include "FeatureListRenderer.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <ranges>

#include "Feature.h"
#include "FeatureIssues.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Util.h"

void FeatureListRenderer::RenderFeatureList(
	float footerHeight,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings,
	const std::function<void()>& drawDisplaySettings)
{
	ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));

	auto menuList = BuildMenuList(featureSearch, categoryExpansionStates, drawGeneralSettings, drawAdvancedSettings, drawDisplaySettings);

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
	const std::function<void()>& drawAdvancedSettings,
	const std::function<void()>& drawDisplaySettings)
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
		BuiltInMenu{ "General", drawGeneralSettings },
		BuiltInMenu{ "Advanced", drawAdvancedSettings },
		BuiltInMenu{ "Display", drawDisplaySettings }
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
	std::vector<std::string> categoryOrder = { "Debug", "Characters", "Grass", "Lighting", "Materials", "Post-Processing", "Sky", "Landscape & Textures", "Water", "Other" };
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
		// Find where built-in menus end (General, Advanced, Display)
		size_t builtInMenuCount = 0;
		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
					builtInMenuCount++;
				}
			}
		}

		// First render the built-in menus (General, Advanced, Display)
		size_t renderedBuiltIns = 0;
		for (size_t i = 0; i < menuList.size() && renderedBuiltIns < 3; i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
					std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
					renderedBuiltIns++;
				}
			}
		}

		// Add Features header and search bar after built-in settings
		Util::DrawSectionHeader("Features", true);
		Util::DrawFeatureSearchBar(featureSearch);

		// Then render the rest (features and categories, but skip already rendered built-ins)
		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
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
	// Use error color for Feature Issues menu item
	bool isFeatureIssues = (menu.name == "Feature Issues");
	if (isFeatureIssues) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);
	}

	if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
		selectedMenuRef = listId;

	if (isFeatureIssues) {
		ImGui::PopStyleColor();
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const std::string& label)
{
	// Style "Unloaded Features" to match category headers
	if (label == "Unloaded Features") {
		Util::DrawSectionHeader(label.c_str(), true);
	} else {
		// Use default separator text for other labels
		ImGui::SeparatorText(label.c_str());
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const CategoryHeader& header)
{
	// Get expansion state from static map
	bool isExpanded = categoryExpansionStates[header.name];

	// Draw category header with custom styling using util:UI function
	int count = Menu::categoryCounts[std::string(header.name)];
	Util::DrawCategoryHeader(header.name.c_str(), isExpanded, count);

	// Update expansion state
	categoryExpansionStates[header.name] = isExpanded;
}

void FeatureListRenderer::ListMenuVisitor::operator()(Feature* feat)
{
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

	// Set text color
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);

	// Create selectable item
	if (ImGui::Selectable(fmt::format(" {} ", feat->GetName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns)) {
		selectedMenuRef = listId;
	}

	// Restore original text color
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
	if (ImGui::BeginTabItem("Settings")) {
		if (ImGui::BeginChild("##FeatureSettingsFrame", { 0, 0 }, true)) {
			auto& themeSettings = globals::menu->GetSettings().Theme;

			// Feature-specific settings section
			ImGui::SeparatorText("Feature Settings");
			if (isDisabled) {
				// Show disabled message
				ImGui::TextColored(themeSettings.StatusPalette.Disable, "Feature settings are hidden because this feature is disabled at boot.");
				ImGui::Spacing();
				ImGui::Text("Enable the feature above to access its configuration options.");
			} else {
				if (isLoaded) {
					// Check if the feature has any settings by monitoring cursor position
					ImVec2 cursorPosBefore = ImGui::GetCursorPos();
					feat->DrawSettings();
					ImVec2 cursorPosAfter = ImGui::GetCursorPos();

					// If cursor position hasn't changed significantly, no visible settings were drawn
					const float epsilon = 0.1f;
					bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > epsilon ||
										std::abs(cursorPosAfter.y - cursorPosBefore.y) > epsilon);
					if (!cursorMoved) {
						ImGui::TextColored(themeSettings.StatusPalette.Disable, "There are no settings available for this feature.");
					}
				} else {
					// Check if feature is obsolete first - always show error for obsolete features
					if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
						// Obsolete feature - show detailed unloaded UI with error info
						feat->DrawUnloadedUI();
					} else if (IsFeatureInstalled(feat->GetShortName())) {
						// INI file exists - show simple pending restart message
						ImGui::Text("This feature will be available after restart.");
					} else {
						// INI file missing - show detailed unloaded UI with installation info
						feat->DrawUnloadedUI();
						// Add download link if available
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

			// Error Messages (Not for obsolete features as this is already covered by DrawUnloadedUI)
			if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
				ImGui::Spacing();
				ImGui::SeparatorText("Error");
				ImGui::TextColored(themeSettings.StatusPalette.Error, feat->failedLoadedMessage.c_str());
			}
		}
		ImGui::EndChild();
		ImGui::EndTabItem();
	}
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureAboutTab(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage)
{
	if (ImGui::BeginTabItem("About")) {
		if (ImGui::BeginChild("##FeatureAboutFrame", { 0, 0 }, true)) {
			auto& themeSettings = globals::menu->GetSettings().Theme;

			// Status Section
			ImGui::SeparatorText("Status");

			ImVec4 statusColor;
			const char* statusText;
			if (isDisabled) {
				statusColor = themeSettings.StatusPalette.Disable;
				statusText = "Disabled at boot.";
			} else if (hasFailedMessage) {
				statusColor = themeSettings.StatusPalette.Error;
				statusText = "Failed to load.";
			} else if (!isLoaded) {
				// Check if INI file exists to determine actual status
				if (!IsFeatureInstalled(feat->GetShortName())) {
					// INI file missing - feature not installed
					statusColor = themeSettings.StatusPalette.Error;
					statusText = "Not installed.";
				} else {
					// INI file exists but feature not loaded - truly pending restart
					statusColor = themeSettings.StatusPalette.RestartNeeded;
					statusText = "Pending restart.";
				}
			} else {
				statusColor = themeSettings.StatusPalette.SuccessColor;
				statusText = "Active.";
			}

			ImGui::TextColored(statusColor, "Current State: %s", statusText);

			// Feature Info - Description and key features
			if (isLoaded) {
				auto [description, keyFeatures] = feat->GetFeatureSummary();
				if (!description.empty()) {
					ImGui::Spacing();
					ImGui::SeparatorText("Description");
					ImGui::TextWrapped("%s", description.c_str());

					if (!keyFeatures.empty()) {
						ImGui::Spacing();
						ImGui::SeparatorText("Key Features");
						for (const auto& feature : keyFeatures) {
							ImGui::BulletText("%s", feature.c_str());
						}
					}
				}
			} else {
				// For unloaded features, show basic info if available
				ImGui::Spacing();
				ImGui::SeparatorText("Information");
				if (hasFailedMessage) {
					ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
				} else {
					// For features that are pending restart or not installed,
					// the detailed information is shown in the Settings tab.
					// Here we just show a simple message directing users there.
					if (!IsFeatureInstalled(feat->GetShortName())) {
						ImGui::Text("Feature installation details are available in the Settings tab.");
					} else {
						// INI file exists but feature not loaded - truly pending restart
						ImGui::Text("This feature is pending restart.");
					}
				}
			}
		}
		ImGui::EndChild();
		ImGui::EndTabItem();
	}
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureActionButtons(Feature* feat, bool isDisabled, bool isLoaded, float buttonPadding, float buttonSpacing)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	const auto featureName = feat->GetShortName();

	// Calculate button widths based on text content
	const char* bootButtonText = isDisabled ? "Enable at Boot" : "Disable at Boot";
	const char* defaultsButtonText = "Restore Defaults";
	const char* overrideButtonText = "Apply Override";

	float bootButtonWidth = ImGui::CalcTextSize(bootButtonText).x + buttonPadding;
	float defaultsButtonWidth = ImGui::CalcTextSize(defaultsButtonText).x + buttonPadding;
	float overrideButtonWidth = ImGui::CalcTextSize(overrideButtonText).x + buttonPadding;

	// Check if override is available for this feature
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	bool hasOverrides = overrideManager && overrideManager->HasFeatureOverrides(featureName);

	float totalButtonWidth = bootButtonWidth;
	if (!isDisabled && isLoaded) {
		totalButtonWidth += defaultsButtonWidth + buttonSpacing;
		if (hasOverrides) {
			totalButtonWidth += overrideButtonWidth + buttonSpacing;
		}
	}

	// Position buttons on the right side of the tab bar
	ImGui::SameLine();
	float availableSpace = ImGui::GetContentRegionAvail().x;
	float rightOffset = availableSpace - totalButtonWidth;
	if (rightOffset > 0) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rightOffset);
	}

	// Disable/Enable at boot button
	ImVec4 textColor;
	if (isDisabled) {
		textColor = themeSettings.StatusPalette.Disable;
	} else if (!feat->failedLoadedMessage.empty()) {
		textColor = themeSettings.StatusPalette.Error;
	} else {
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	if (ImGui::Button(bootButtonText, { bootButtonWidth, 0 })) {
		bool newState = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", featureName, newState ? "Enabled" : "Disabled");
	}
	ImGui::PopStyleColor();

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Current State: %s\n"
			"%s the feature settings at boot. "
			"Restart will be required to reenable. "
			"This is the same as deleting the ini file. "
			"This should remove any performance impact for the feature.",
			isDisabled ? "Disabled" : "Enabled",
			isDisabled ? "Enable" : "Disable");
	}

	// Restore Defaults button (when feature is not disabled and is loaded)
	if (!isDisabled && isLoaded) {
		ImGui::SameLine();
		if (ImGui::Button(defaultsButtonText, { defaultsButtonWidth, 0 })) {
			feat->RestoreDefaultSettings();
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Restores the feature's settings back to their default values. "
				"You will still need to Save Settings to make these changes permanent.");
		}

		// Apply Override button (when feature has available overrides)
		if (hasOverrides) {
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
}
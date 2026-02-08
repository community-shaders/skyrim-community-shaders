#include "HomePageRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include "FeatureConstraints.h"
#include "Globals.h"
#include "Menu.h"
#include "Plugin.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

// Static member definitions
bool HomePageRenderer::isFirstTimeSetupShown = false;
uint32_t HomePageRenderer::keyThatClosedDialog = 0;

bool HomePageRenderer::ShouldSkipKeyRelease(uint32_t key)
{
	if (keyThatClosedDialog && key == keyThatClosedDialog) {
		keyThatClosedDialog = 0;
		return true;
	}
	return false;
}

void HomePageRenderer::RenderHomePage()
{
	ImGui::BeginChild("HomePage", ImVec2(0, 0), false);

	RenderWelcomeSection();
	ImGui::Spacing();

	RenderActiveConstraintsSection();

	RenderQuickLinksSection();
	ImGui::Spacing();

	RenderFAQSection();

	ImGui::EndChild();
}

void HomePageRenderer::RenderWelcomeSection()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

	// Main title - centered with safe font handling
	ImGuiIO& io = ImGui::GetIO();
	ImFont* titleFont = nullptr;

	// Safely check if we have multiple fonts and the second one is valid
	if (io.Fonts && io.Fonts->Fonts.Size > 1 && io.Fonts->Fonts[1] != nullptr) {
		titleFont = io.Fonts->Fonts[1];
	}

	// Scale the text to make it larger (2.0x size)
	ImGui::SetWindowFontScale(TITLE_FONT_SCALE);

	// Only push font if we have a valid one, otherwise use default scaled
	if (titleFont) {
		ImGui::PushFont(titleFont);
	}

	ImVec2 windowSize = ImGui::GetWindowSize();
	std::string titleWithVersion = "Welcome to Community Shaders " + Util::GetFormattedVersion(Plugin::VERSION);
	ImVec2 titleSize = ImGui::CalcTextSize(titleWithVersion.c_str());
	ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
	ImGui::Text("%s", titleWithVersion.c_str());

	// Only pop font if we pushed one
	if (titleFont) {
		ImGui::PopFont();
	}

	// Reset text scale back to normal
	ImGui::SetWindowFontScale(1.0f);

	ImGui::Spacing();

	// Intro text - centered
	const char* introText =
		"Community Shaders provides advanced graphics enhancements for Skyrim.\n"
		"This comprehensive collection of features brings modern rendering techniques\n"
		"to enhance your visual experience.";
	ImVec2 introSize = ImGui::CalcTextSize(introText);
	ImGui::SetCursorPosX((windowSize.x - introSize.x) * 0.5f);
	ImGui::TextWrapped("%s", introText);

	ImGui::Spacing();

	// Discord banner - centered with proper error checking
	auto menu = Menu::GetSingleton();
	bool discordIconAvailable = false;

	// Check if menu exists, has icons, and Discord icon is loaded
	if (menu && menu->uiIcons.discord.texture != nullptr &&
		menu->uiIcons.discord.size.x > 0 && menu->uiIcons.discord.size.y > 0) {
		discordIconAvailable = true;
	}

	if (discordIconAvailable) {
		// Calculate scaled icon size based on window width, with min/max constraints
		ImVec2 originalSize = ImVec2(menu->uiIcons.discord.size.x, menu->uiIcons.discord.size.y);

		// Compute width based on window size with constraints and padding (handles very small windows)
		float ratioWidth = windowSize.x * DISCORD_BANNER_TARGET_WIDTH_RATIO;
		float aspectRatio = originalSize.y / originalSize.x;
		float maxAllowed = std::max(1.0f, windowSize.x - DISCORD_BANNER_PADDING_MARGIN);
		float upperBound = std::min(DISCORD_BANNER_MAX_WIDTH, maxAllowed);
		float lowerBound = std::min(DISCORD_BANNER_MIN_WIDTH, upperBound);
		float targetWidth = std::clamp(ratioWidth, lowerBound, upperBound);

		ImVec2 iconSize = ImVec2(targetWidth, targetWidth * aspectRatio);
		ImGui::SetCursorPosX((windowSize.x - iconSize.x) * 0.5f);

		// Push style to remove border
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                     // Transparent background
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.1f, 0.1f, 0.3f));  // Subtle hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));   // Subtle click

		if (ImGui::ImageButton("##DiscordButton", menu->uiIcons.discord.texture, iconSize)) {
			ShellExecuteA(NULL, "open", DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
		}

		// Pop the style changes
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Join Community Shaders Discord Server");
		}
	} else {
		// Fallback centered button when Discord icon is not available
		float buttonWidth = 200.0f;
		ImGui::SetCursorPosX((windowSize.x - buttonWidth) * 0.5f);
		if (ImGui::Button("Join Discord Server", ImVec2(buttonWidth, 0))) {
			ShellExecuteA(NULL, "open", DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Join Community Shaders Discord Server - Icon not found, using fallback button");
		}
	}

	ImGui::PopStyleVar();
}

void HomePageRenderer::RenderQuickLinksSection()
{
	// Quick Links title - centered
	ImVec2 windowSize = ImGui::GetWindowSize();
	ImVec2 titleSize = ImGui::CalcTextSize("Quick Links");
	ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
	ImGui::Text("Quick Links");

	// Center the button layout
	float buttonWidth = QUICK_LINKS_BUTTON_WIDTH;
	float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;  // 3 buttons with spacing
	ImGui::SetCursorPosX((windowSize.x - totalWidth) * 0.5f);

	// External links in a row
	if (ImGui::Button("Nexus Mods", ImVec2(buttonWidth, 0))) {
		ShellExecuteA(NULL, "open", "https://www.nexusmods.com/skyrimspecialedition/mods/86492", NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::SameLine();
	if (ImGui::Button("GitHub Repository", ImVec2(buttonWidth, 0))) {
		ShellExecuteA(NULL, "open", "https://github.com/doodlum/skyrim-community-shaders", NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::SameLine();
	if (ImGui::Button("GitHub Wiki", ImVec2(buttonWidth, 0))) {
		ShellExecuteA(NULL, "open", "https://github.com/doodlum/skyrim-community-shaders/wiki", NULL, NULL, SW_SHOWNORMAL);
	}
}

void HomePageRenderer::RenderFAQSection()
{
	// FAQ title - centered
	ImVec2 windowSize = ImGui::GetWindowSize();
	ImVec2 titleSize = ImGui::CalcTextSize("Frequently Asked Questions");
	ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
	ImGui::Text("Frequently Asked Questions");
	ImGui::Separator();

	// FAQ items with collapsible headers
	if (ImGui::CollapsingHeader("What is Community Shaders?")) {
		ImGui::TextWrapped(
			"Community Shaders is a comprehensive graphics enhancement framework for Skyrim that "
			"provides advanced lighting, materials, and visual effects. It's designed to be modular, "
			"allowing you to enable only the features you want while maintaining good performance.");
	}

	if (ImGui::CollapsingHeader("How do I configure features?")) {
		ImGui::TextWrapped(
			"Each feature can be found in the left sidebar menu. Click on any feature to access its "
			"settings. Most features include presets and detailed tooltips to help you understand "
			"what each setting does.");
	}

	if (ImGui::CollapsingHeader("Why are some features not loading?")) {
		ImGui::TextWrapped(
			"Features may fail to load due to hardware incompatibility, missing dependencies, or "
			"conflicts with other mods. Check the 'Feature Issues' tab for detailed information "
			"about any problematic features.");
	}

	if (ImGui::CollapsingHeader("I have \"Failed Shaders\" when compiling?")) {
		ImGui::TextWrapped(
			"Failed shaders are usually caused by mixed file versions. Ensure all features are up to date "
			"and avoid mixing files from test builds or outdated versions.");
		ImGui::Spacing();
		ImGui::Text("Remove these outdated pre-1.0 CS features:");
		ImGui::BulletText("Vanilla HDR");
		ImGui::BulletText("Tree LOD Lighting");
		ImGui::BulletText("Complex Parallax Materials");
		ImGui::BulletText("Water Blending");
		ImGui::BulletText("Water Caustics");
		ImGui::BulletText("Water Parallax");
		ImGui::BulletText("Dynamic Cubemaps");
		ImGui::Spacing();
		ImGui::TextWrapped("Note: All of these features are now included in the base Community Shaders install.");
	}

	if (ImGui::CollapsingHeader("How do I improve performance?")) {
		ImGui::TextWrapped(
			"Start by enabling the Performance Overlay to monitor your FPS. Consider disabling "
			"expensive features like Screen Space GI or reducing quality settings. The 'Display' "
			"tab also includes upscaling options that can improve performance.");
	}

	if (ImGui::CollapsingHeader("Is Community Shaders compatible with ENB?")) {
		ImGui::TextWrapped(
			"No, Community Shaders is not compatible with ENB. Community Shaders will automatically "
			"disable itself if ENB is detected.");
	}

	if (ImGui::CollapsingHeader("The menu hotkey isn't working!")) {
		ImGui::TextWrapped(
			"By default, Community Shaders uses the END key to open this menu. If your keyboard "
			"doesn't have an END key or it's not working, you can change it in the General > Keybindings tab. "
			"You can also edit the hotkey in the JSON configuration files.");
	}

	if (ImGui::CollapsingHeader("I would like to help develop Community Shaders.")) {
		ImGui::TextWrapped(
			"We're always looking for talented developers to join the team! Check out our GitHub wiki "
			"for contribution guidelines and join our Discord server to connect with the development team. "
			"Whether you're interested in shader programming, C++ development, or documentation, there's "
			"always something to contribute.");
	}

	if (ImGui::CollapsingHeader("Is Community Shaders open source?")) {
		ImGui::TextWrapped(
			"Yes! Community Shaders is completely open source and available on GitHub. You can view "
			"the source code, report issues, suggest features, and contribute to the project. "
			"The project is licensed under GPL, ensuring it remains free and open for everyone."
			" Branding materials and assets (icons, nexus branding, typography, etc) are not covered by the GPL Licence."
			" Any included assets may not be used without explicit permission.");
	}
}

void HomePageRenderer::RenderActiveConstraintsSection()
{
	auto constraints = FeatureConstraints::GetAllActiveConstraints();
	if (constraints.empty()) {
		return;  // Don't show section if there are no active constraints
	}

	ImGui::Spacing();

	// Use warning color for the header to draw attention
	auto menu = Menu::GetSingleton();
	ImVec4 warningColor = menu ? menu->GetTheme().StatusPalette.Warning : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);

	ImGui::PushStyleColor(ImGuiCol_Text, warningColor);
	bool headerOpen = ImGui::CollapsingHeader("Active Setting Constraints", ImGuiTreeNodeFlags_None);
	ImGui::PopStyleColor();

	if (headerOpen) {
		ImGui::TextWrapped(
			"Some settings are constrained by other features. Hover over rows for details.");

		ImGui::Spacing();

		// Prepare data for table
		struct ConstraintRow
		{
			std::string setting;
			std::string forcedTo;
			std::string constrainedBy;
			std::string firstSourceShortName;  // For "navigate to feature" on click
			std::string tooltip;
		};

		std::vector<ConstraintRow> rows;
		for (const auto& [settingId, result] : constraints) {
			ConstraintRow row;
			row.setting = std::format("{}.{}", settingId.featureShortName, settingId.settingPath);
			row.forcedTo = FeatureConstraints::FormatConstraintValue(result.forcedValue);
			for (size_t i = 0; i < result.sources.size(); ++i) {
				if (i > 0)
					row.constrainedBy += ", ";
				row.constrainedBy += result.sources[i].featureName;
			}
			if (!result.sources.empty()) {
				row.firstSourceShortName = result.sources[0].featureShortName;
			}
			// Build tooltip
			for (const auto& src : result.sources) {
				if (!row.tooltip.empty())
					row.tooltip += "\n";
				row.tooltip += std::format("{}: {}", src.featureName, src.reason);
				if (src.recommendDisableAtBoot) {
					row.tooltip += "\nConsider disabling at boot.";
				}
			}
			rows.push_back(row);
		}

		// Define headers
		std::vector<std::string> headers = { "Setting", "Forced To", "Constrained By" };

		// Custom sorts (string comparators for each column)
		std::vector<std::function<bool(const ConstraintRow&, const ConstraintRow&, bool)>> customSorts = {
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.setting, b.setting, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.forcedTo, b.forcedTo, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.constrainedBy, b.constrainedBy, asc); }
		};

		// Cell render -- column 2 ("Constrained By") is clickable to navigate
		// to the first source feature's settings page.
		auto cellRender = [warningColor](int rowIdx, int colIdx, const ConstraintRow& row) {
			if (colIdx == 0) {
				Util::RenderTableCell(row.setting, "", "", nullptr, ImVec4(1, 1, 1, 1), true, warningColor);
			} else if (colIdx == 1) {
				Util::RenderTableCell(row.forcedTo, "", "", nullptr, ImVec4(1, 1, 1, 1), true);
			} else if (colIdx == 2) {
				if (!row.firstSourceShortName.empty()) {
					if (ImGui::Selectable(std::format("{}##nav{}", row.constrainedBy, rowIdx).c_str())) {
						if (auto* menu = Menu::GetSingleton()) {
							menu->SelectFeatureMenu(row.firstSourceShortName);
						}
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Click to navigate to %s", row.constrainedBy.c_str());
						if (!row.tooltip.empty()) {
							ImGui::Separator();
							ImGui::Text("%s", row.tooltip.c_str());
						}
					}
				} else {
					Util::RenderTableCell(row.constrainedBy, "", row.tooltip, nullptr, ImVec4(1, 1, 1, 1), true);
				}
			}
		};

		// Render table
		Util::ShowSortedStringTableCustom<ConstraintRow>(
			"ConstraintsTable",
			headers,
			rows,
			0,     // sortColumn
			true,  // ascending
			customSorts,
			cellRender);
	}

	ImGui::Spacing();
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	if (!ShouldShowFirstTimeSetup()) {
		return;
	}

	// Block input to the game and make cursor visible - input blocking is handled by ShouldSwallowInput()
	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;  // Show ImGui cursor

	// Draw semi-transparent dark overlay behind the dialog for depth
	Util::DrawModalBackground();

	// Center the window properly with rounded corners and thin border
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	// Set a minimum width for better layout, but allow auto-sizing for height
	ImGui::SetNextWindowSizeConstraints(ImVec2(500, 0), ImVec2(600, FLT_MAX));

	// Style for rounded window with thin border
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

	if (!ImGui::Begin("##FirstTimeSetup", nullptr, flags)) {
		ImGui::PopStyleVar(2);
		ImGui::End();
		return;
	}

	// Set absolute font size for better readability in this welcome dialog
	float targetFontSize = 27.0f;
	float currentFontSize = io.FontDefault ? io.FontDefault->FontSize : io.FontGlobalScale * 13.0f;
	float fontScale = targetFontSize / currentFontSize;
	ImGui::SetWindowFontScale(fontScale);

	auto menu = Menu::GetSingleton();

	// Render CS logo as background watermark with proper aspect ratio
	if (menu && menu->uiIcons.logo.texture) {
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();

		// Get the original texture size to maintain aspect ratio
		ImVec2 textureSize = menu->uiIcons.logo.size;
		float aspectRatio = textureSize.x / textureSize.y;

		// Set desired height and calculate width to maintain aspect ratio
		float logoHeight = LOGO_WATERMARK_HEIGHT;
		float logoWidth = logoHeight * aspectRatio;

		ImVec2 logoMin(windowPos.x + (windowSize.x - logoWidth) * 0.5f,
			windowPos.y + (windowSize.y - logoHeight) * 0.5f);
		ImVec2 logoMax(logoMin.x + logoWidth, logoMin.y + logoHeight);

		// Determine watermark color based on monochrome logo setting
		ImU32 watermarkColor;
		if (menu->GetSettings().Theme.UseMonochromeLogo) {
			ImVec4 textColor = menu->GetSettings().Theme.Palette.Text;
			textColor.w = 0.24f;  // Low alpha for watermark effect
			watermarkColor = ImGui::GetColorU32(textColor);
		} else {
			watermarkColor = IM_COL32(255, 255, 255, 60);
		}

		// Render as subtle watermark background
		ImGui::GetWindowDrawList()->AddImage(menu->uiIcons.logo.texture, logoMin, logoMax,
			ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
	}

	// Center all content
	float windowWidth = ImGui::GetWindowWidth();
	auto centerText = [windowWidth](const char* text) {
		ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(text).x) * 0.5f);
	};
	auto centerWidth = [windowWidth](float width) {
		ImGui::SetCursorPosX((windowWidth - width) * 0.5f);
	};

	// Version text - two lines, both centered (reduced spacing between lines)
	const char* versionLine1 = "This appears to be a new install, update, or";
	const char* versionLine2 = "reinstallation of Community Shaders.";

	centerText(versionLine1);
	ImGui::Text("%s", versionLine1);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);
	centerText(versionLine2);
	ImGui::Text("%s", versionLine2);

	ImGui::Spacing();

	// Description - centered
	const char* description = "Please choose a hotkey to access the menu:";
	centerText(description);
	ImGui::Text("%s", description);

	// Hotkey selection - clickable hotkey text
	// Show current toggle key and allow user to change it by clicking on it
	auto& themeSettings = menu->GetTheme();
	bool isCapturing = menu->settingToggleKey;

	// Increase font size for hotkey text - bigger when capturing
	ImGui::SetWindowFontScale(fontScale * (isCapturing ? HOTKEY_TEXT_SCALE_CAPTURING : HOTKEY_TEXT_SCALE));

	// Format hotkey with brackets to make it look like a button
	std::string hotkeyStr;
	if (isCapturing) {
		hotkeyStr = "[ ... ]";
	} else {
		auto& keys = menu->GetSettings().ToggleKey;
		hotkeyStr = std::string("[ ") + Util::Input::KeyIdToString(keys) + " ]";
	}

	ImVec2 hotkeyTextSize = ImGui::CalcTextSize(hotkeyStr.c_str());

	centerWidth(hotkeyTextSize.x);
	ImVec2 buttonPos = ImGui::GetCursorScreenPos();

	// Create invisible button for hover detection and clicking
	ImGui::PushID("HotkeyButton");
	bool clicked = ImGui::InvisibleButton("##HotkeyClick", hotkeyTextSize);
	bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	// Set cursor position back for text rendering
	ImGui::SetCursorScreenPos(buttonPos);

	// Choose color based on state
	ImVec4 hotkeyColor;
	if (isCapturing) {
		// Pulsing effect using theme's hotkey color
		hotkeyColor = Util::GetPulsingColor(themeSettings.StatusPalette.CurrentHotkey);
	} else if (hovered) {
		hotkeyColor = ImVec4(themeSettings.StatusPalette.CurrentHotkey.x * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.y * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.z * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.w);
	} else {
		hotkeyColor = themeSettings.StatusPalette.CurrentHotkey;
	}

	ImGui::TextColored(hotkeyColor, "%s", hotkeyStr.c_str());

	// Reset font scale
	ImGui::SetWindowFontScale(fontScale);

	// Handle click to start hotkey capture
	if (clicked && !isCapturing) {
		// Prevent starting capture if this click was caused by Enter key,
		// because we want Enter to close the dialog instead.
		if (!ImGui::IsKeyPressed(ImGuiKey_Enter))
			menu->settingToggleKey = true;
	}

	// Show hotkey capture message when in capture mode
	if (isCapturing) {
		const char* pressKeyText = "Press any key to set as toggle key...";
		centerText(pressKeyText);
		ImGui::TextDisabled("%s", pressKeyText);
	}

	ImGui::Spacing();

	const char* laterText = "You can change this later in General > Keybindings.";
	centerText(laterText);
	ImGui::Text("%s", laterText);

	ImGui::Spacing();

	// Check for Enter or Escape key to close, but only if not capturing a hotkey
	bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || escapePressed) && !isCapturing) {
		MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : VK_RETURN);
	}

	// Help text with breathing animation
	const char* helpText = "Press Escape or Enter to continue";

	ImGui::SetWindowFontScale(fontScale * HELP_TEXT_SCALE);
	centerText(helpText);
	Util::DrawBreathingText(helpText);

	// Reset font scale
	ImGui::SetWindowFontScale(fontScale);

	ImGui::End();
	ImGui::PopStyleVar(2);
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
	// Never show first-time setup in VR mode
	if (REL::Module::IsVR()) {
		return false;
	}

	// Check if already completed this session
	if (isFirstTimeSetupShown) {
		return false;
	}

	// Check if first-time setup has been completed using the Menu settings
	auto menu = Menu::GetSingleton();
	return !menu->GetSettings().FirstTimeSetupCompleted;
}

void HomePageRenderer::MarkFirstTimeSetupComplete(uint32_t closingKey)
{
	// Set the flag in the Menu settings
	auto menu = Menu::GetSingleton();
	menu->GetSettings().FirstTimeSetupCompleted = true;
	// Ensure we are not capturing a hotkey when closing the dialog
	menu->settingToggleKey = false;

	// Immediately save settings to ensure the flag is persisted
	// This prevents the welcome screen from showing again even if user doesn't manually save
	globals::state->Save();

	isFirstTimeSetupShown = true;
	keyThatClosedDialog = closingKey;
}

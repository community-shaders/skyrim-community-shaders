#include "HomePageRenderer.h"
#include "PCH.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <ranges>
#include <sstream>

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Plugin.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Util.h"

using json = nlohmann::json;

// Static member definitions
bool HomePageRenderer::isFirstTimeSetupShown = false;

void HomePageRenderer::RenderHomePage()
{
	ImGui::BeginChild("HomePage", ImVec2(0, 0), false);

	RenderWelcomeSection();
	ImGui::Spacing();

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
		ImVec2 iconSize = ImVec2(menu->uiIcons.discord.size.x, menu->uiIcons.discord.size.y);
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
			"The project is licensed under GPL, ensuring it remains free and open for everyone.");
	}
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	// Block input to the game and make cursor visible - input blocking is handled by ShouldSwallowInput()
	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;  // Show ImGui cursor

	// Center the window properly with rounded corners and thin border
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Always);

	// Style for rounded window with thin border
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar;  // Prevent scrolling and remove title

	if (!ImGui::Begin("##FirstTimeSetup", nullptr, flags)) {
		ImGui::PopStyleVar(2);
		ImGui::End();
		return;
	}

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

		// Render as subtle watermark background
		ImU32 watermarkColor = IM_COL32(255, 255, 255, 60);
		ImGui::GetWindowDrawList()->AddImage(menu->uiIcons.logo.texture, logoMin, logoMax,
			ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
	}

	// Center all content
	float windowWidth = ImGui::GetWindowWidth();

	// Welcome title - centered
	const char* welcomeTitle = "Welcome to Community Shaders!";
	float welcomeTitleWidth = ImGui::CalcTextSize(welcomeTitle).x;
	ImGui::SetCursorPosX((windowWidth - welcomeTitleWidth) * 0.5f);
	ImGui::Text("%s", welcomeTitle);

	ImGui::Spacing();

	// Version text - wrapped and centered
	const char* versionText = "This appears to be a new install, update, or reinstallation of Community Shaders.";
	float textPadding = 40.0f;  // Padding from window edges

	// Use a centered region for wrapped text
	ImGui::SetCursorPosX(textPadding);
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(windowWidth - textPadding);

	// Calculate the wrapped text size to center it
	ImVec2 textSize = ImGui::CalcTextSize(versionText, nullptr, true, windowWidth - textPadding * 2);
	float centerOffset = (windowWidth - textPadding * 2 - textSize.x) * 0.5f;
	if (centerOffset > 0) {
		ImGui::SetCursorPosX(textPadding + centerOffset);
	}

	ImGui::TextWrapped("%s", versionText);
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();

	ImGui::Spacing();

	// Description - centered
	const char* description = "Please select a hotkey to access the menu:";
	float descWidth = ImGui::CalcTextSize(description).x;
	ImGui::SetCursorPosX((windowWidth - descWidth) * 0.5f);
	ImGui::Text("%s", description);

	// Hotkey selection - clickable hotkey text
	// Show current toggle key and allow user to change it by clicking on it
	auto& themeSettings = menu->GetTheme();
	const char* currentKeyName = Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);

	// Calculate text dimensions for centering and button area
	float hotkeyWidth = ImGui::CalcTextSize(currentKeyName).x;
	float centerX = (windowWidth - hotkeyWidth) * 0.5f;
	ImGui::SetCursorPosX(centerX);

	// Create invisible button for hover detection and clicking
	ImVec2 buttonPos = ImGui::GetCursorScreenPos();
	ImVec2 hotkeyTextSize = ImGui::CalcTextSize(currentKeyName);
	bool hovered = false;
	bool clicked = false;

	ImGui::PushID("HotkeyButton");
	if (ImGui::InvisibleButton("##HotkeyClick", hotkeyTextSize)) {
		clicked = true;
	}
	hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	// Set cursor position back for text rendering
	ImGui::SetCursorScreenPos(buttonPos);

	// Choose color based on hover state - darken when hovered.
	ImVec4 hotkeyColor = hovered ?
	                         ImVec4(themeSettings.StatusPalette.CurrentHotkey.x * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.y * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.z * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.w) :
	                         themeSettings.StatusPalette.CurrentHotkey;

	ImGui::TextColored(hotkeyColor, "%s", currentKeyName);

	// Handle click to start hotkey capture
	if (clicked) {
		menu->settingToggleKey = true;
	}

	// Show hotkey capture message or hotkey text
	if (menu->settingToggleKey) {
		const char* pressKeyText = "Press any key to set as toggle key...";
		float pressKeyWidth = ImGui::CalcTextSize(pressKeyText).x;
		ImGui::SetCursorPosX((windowWidth - pressKeyWidth) * 0.5f);
		ImGui::Text("%s", pressKeyText);
	}

	ImGui::Spacing();

	// "You can change this later" text - wrapped and centered
	const char* laterText = "You can change this later in General > Keybindings.";
	float laterWidth = ImGui::CalcTextSize(laterText).x;
	if (laterWidth > windowWidth - 40.0f) {
		// Text is too wide, use wrapped text with centering
		float laterTextPadding = 40.0f;

		ImGui::SetCursorPosX(laterTextPadding);
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(windowWidth - laterTextPadding);

		// Calculate the wrapped text size to center it
		ImVec2 laterTextSize = ImGui::CalcTextSize(laterText, nullptr, true, windowWidth - laterTextPadding * 2);
		float laterCenterOffset = (windowWidth - laterTextPadding * 2 - laterTextSize.x) * 0.5f;
		if (laterCenterOffset > 0) {
			ImGui::SetCursorPosX(laterTextPadding + laterCenterOffset);
		}

		ImGui::TextWrapped("%s", laterText);
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
	} else {
		// Text fits, center it normally
		ImGui::SetCursorPosX((windowWidth - laterWidth) * 0.5f);
		ImGui::Text("%s", laterText);
	}

	ImGui::Spacing();

	// Center the continue button
	float continueButtonWidth = 140.0f;
	ImGui::SetCursorPosX((windowWidth - continueButtonWidth) * 0.5f);

	// Check for Enter or Escape key first
	bool shouldClose = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape);

	if (ImGui::Button("Continue", ImVec2(continueButtonWidth, 30)) || shouldClose) {
		// No need to apply any hotkey - user has already set it or it defaults to VK_END
		MarkFirstTimeSetupComplete();
	}

	// Center the help text
	const char* helpText = "(Press Enter or Escape to continue)";
	float helpWidth = ImGui::CalcTextSize(helpText).x;
	ImGui::SetCursorPosX((windowWidth - helpWidth) * 0.5f);
	ImGui::TextDisabled("%s", helpText);

	ImGui::PopStyleVar(2);  // Pop WindowRounding and WindowBorderSize
	ImGui::End();
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
	// Check if already completed this session
	if (isFirstTimeSetupShown) {
		return false;
	}

	// Check if first-time setup has been completed by looking at UserSettings.json
	std::filesystem::path userSettingsPath = Util::PathHelpers::GetUserSettingsPath();

	// If UserSettings.json doesn't exist at all, this is definitely a first-time launch
	if (!std::filesystem::exists(userSettingsPath)) {
		return true;
	}

	// If UserSettings.json exists, check if FirstTimeSetupCompleted flag is set
	try {
		std::ifstream file(userSettingsPath);
		if (!file.is_open()) {
			return true;  // If we can't read the file, assume first time
		}

		nlohmann::json settings;
		file >> settings;
		file.close();

		// Check if FirstTimeSetupCompleted exists and is true
		if (settings.contains("FirstTimeSetupCompleted") &&
			settings["FirstTimeSetupCompleted"].is_boolean() &&
			settings["FirstTimeSetupCompleted"] == true) {
			return false;  // Setup already completed
		}

		return true;  // Field doesn't exist or is false, show setup

	} catch (const std::exception&) {
		// If there's any error reading the file, assume first time
		return true;
	}
}

void HomePageRenderer::MarkFirstTimeSetupComplete()
{
	std::filesystem::path userSettingsPath = Util::PathHelpers::GetUserSettingsPath();

	try {
		nlohmann::json settings;

		// Read existing settings if file exists
		if (std::filesystem::exists(userSettingsPath)) {
			std::ifstream file(userSettingsPath);
			if (file.is_open()) {
				file >> settings;
				file.close();
			}
		}

		// Set the FirstTimeSetupCompleted flag
		settings["FirstTimeSetupCompleted"] = true;

		// Write back to file
		std::filesystem::create_directories(userSettingsPath.parent_path());
		std::ofstream outFile(userSettingsPath);
		if (outFile.is_open()) {
			outFile << settings.dump(2);
			outFile.close();
		}

	} catch (const std::exception&) {
		// If we can't write the file, just mark as shown this session to avoid repeated popups
	}

	isFirstTimeSetupShown = true;  // Mark as shown this session
}

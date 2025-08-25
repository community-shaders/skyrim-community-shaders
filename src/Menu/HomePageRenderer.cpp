#include "PCH.h"
#include "HomePageRenderer.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <magic_enum.hpp>

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"
#include "SettingsOverrideManager.h"

using json = nlohmann::json;

// Static member definitions
bool HomePageRenderer::isFirstTimeSetupShown = false;

void HomePageRenderer::RenderHomePage()
{
	ImGui::BeginChild("HomePage", ImVec2(0, 0), false);

	// Check if we should show first-time setup dialog
	if (ShouldShowFirstTimeSetup()) {
		RenderFirstTimeSetupDialog();
	}

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
	
	// Only push font if we have a valid one, otherwise use default
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
	
	ImGui::Spacing();
	
	// Intro text - centered
	const char* introText = "Community Shaders provides advanced graphics enhancements for Skyrim.\n"
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
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Transparent background
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.1f, 0.1f, 0.3f)); // Subtle hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f)); // Subtle click
		
		if (ImGui::ImageButton("##DiscordButton", menu->uiIcons.discord.texture, iconSize)) {
			ShellExecuteA(NULL, "open", "https://discord.com/invite/nkrQybAsyy", NULL, NULL, SW_SHOWNORMAL);
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
			ShellExecuteA(NULL, "open", "https://discord.com/invite/nkrQybAsyy", NULL, NULL, SW_SHOWNORMAL);
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
	float buttonWidth = 180.0f;
	float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2; // 3 buttons with spacing
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
			"allowing you to enable only the features you want while maintaining good performance."
		);
	}
	
	if (ImGui::CollapsingHeader("How do I configure features?")) {
		ImGui::TextWrapped(
			"Each feature can be found in the left sidebar menu. Click on any feature to access its "
			"settings. Most features include presets and detailed tooltips to help you understand "
			"what each setting does."
		);
	}
	
	if (ImGui::CollapsingHeader("Why are some features not loading?")) {
		ImGui::TextWrapped(
			"Features may fail to load due to hardware incompatibility, missing dependencies, or "
			"conflicts with other mods. Check the 'Feature Issues' tab for detailed information "
			"about any problematic features."
		);
	}

	if (ImGui::CollapsingHeader("I have \"Failed Shaders\" when compiling?")) {
		ImGui::TextWrapped(
			"Failed shaders are usually caused by mixed file versions. Ensure all features are up to date "
			"and avoid mixing files from test builds or outdated versions."
		);
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
			"tab also includes upscaling options that can improve performance."
		);
	}
	
	if (ImGui::CollapsingHeader("Is Community Shaders compatible with ENB?")) {
		ImGui::TextWrapped(
			"No, Community Shaders is not compatible with ENB. Community Shaders will automatically "
			"disable itself if ENB is detected."
		);
	}
	
	if (ImGui::CollapsingHeader("The menu hotkey isn't working!")) {
		ImGui::TextWrapped(
			"By default, Community Shaders uses the END key to open this menu. If your keyboard "
			"doesn't have an END key or it's not working, you can change it in the General > Keybindings tab. "
			"You can also edit the hotkey in the JSON configuration files."
		);
	}
	
	if (ImGui::CollapsingHeader("I would like to help develop Community Shaders.")) {
		ImGui::TextWrapped(
			"We're always looking for talented developers to join the team! Check out our GitHub wiki "
			"for contribution guidelines and join our Discord server to connect with the development team. "
			"Whether you're interested in shader programming, C++ development, or documentation, there's "
			"always something to contribute."
		);
	}
	
	if (ImGui::CollapsingHeader("Is Community Shaders open source?")) {
		ImGui::TextWrapped(
			"Yes! Community Shaders is completely open source and available on GitHub. You can view "
			"the source code, report issues, suggest features, and contribute to the project. "
			"The project is licensed under GPL, ensuring it remains free and open for everyone."
		);
	}
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	// Center the dialog
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Appearing);
	
	if (ImGui::BeginPopupModal("Welcome to Community Shaders!", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
		ImGui::Text("Thank you for installing Community Shaders!");
		ImGui::Spacing();
		
		ImGui::TextWrapped(
			"Community Shaders provides advanced graphics enhancements for Skyrim. "
			"This appears to be your first time running this version."
		);
		
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		
		ImGui::Text("Menu Hotkey Configuration");
		ImGui::TextWrapped(
			"By default, the Community Shaders menu opens with the END key. "
			"If you don't have an END key or prefer a different hotkey, you can change it below:"
		);
		
		ImGui::Spacing();
		
		static int selectedKey = VK_END; // Default to END key
		static bool keyChanged = false;
		
		auto menu = Menu::GetSingleton();
		static uint32_t currentMenuKey = menu ? menu->GetSettings().ToggleKey : VK_END;
		
		// Key selection
		const char* keyNames[] = {
			"END (Default)", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"INSERT", "DELETE", "HOME", "PAGE UP", "PAGE DOWN", "PAUSE", "SCROLL LOCK"
		};
		
		const int keyValues[] = {
			VK_END, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
			VK_INSERT, VK_DELETE, VK_HOME, VK_PRIOR, VK_NEXT, VK_PAUSE, VK_SCROLL
		};
		
		static int currentItem = 0;
		if (ImGui::Combo("Menu Hotkey", &currentItem, keyNames, IM_ARRAYSIZE(keyNames))) {
			selectedKey = keyValues[currentItem];
			keyChanged = true;
		}
		
		ImGui::Spacing();
		ImGui::TextWrapped("You can also change this later in General > Keybindings.");
		
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		
		// Action buttons
		if (ImGui::Button("Continue with Default Settings", ImVec2(200, 0))) {
			if (!keyChanged) {
				selectedKey = VK_END; // Ensure default
			}
			
			// Apply the selected key
			if (menu) {
				menu->GetSettings().ToggleKey = selectedKey;
			}
			
			MarkFirstTimeSetupComplete();
			ImGui::CloseCurrentPopup();
		}
		
		ImGui::SameLine();
		
		if (ImGui::Button("Apply and Continue", ImVec2(120, 0))) {
			// Apply the selected key
			if (menu) {
				menu->GetSettings().ToggleKey = selectedKey;
			}
			
			MarkFirstTimeSetupComplete();
			ImGui::CloseCurrentPopup();
		}
		
		ImGui::Spacing();
		
		if (ImGui::Button("Skip Setup", ImVec2(100, 0))) {
			MarkFirstTimeSetupComplete();
			ImGui::CloseCurrentPopup();
		}
		
		ImGui::EndPopup();
	}
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
	if (isFirstTimeSetupShown) {
		return false;
	}
	
	// Use the same hash-based system as SettingsOverrideManager
	// Check if we have a setup completion marker
	std::filesystem::path setupPath = std::filesystem::path(globals::state->folderPath) / "setup_complete.json";
	
	if (!std::filesystem::exists(setupPath)) {
		// First time installation
		isFirstTimeSetupShown = true;
		ImGui::OpenPopup("Welcome to Community Shaders!");
		return true;
	}
	
	// Check if version has changed (update scenario)
	try {
		std::ifstream file(setupPath);
		if (file.is_open()) {
			json setupData;
			file >> setupData;
			file.close();
			
			std::string lastVersion = setupData.value("version", std::string(""));
			std::string currentVersion = Plugin::VERSION.string();
			
			if (lastVersion != currentVersion) {
				// Version changed, show setup again
				isFirstTimeSetupShown = true;
				ImGui::OpenPopup("Welcome to Community Shaders!");
				return true;
			}
		}
	} catch (const std::exception& e) {
		// If we can't read the file, assume first time
		logger::warn("Could not read setup completion file: {}", e.what());
		isFirstTimeSetupShown = true;
		ImGui::OpenPopup("Welcome to Community Shaders!");
		return true;
	}
	
	return false;
}

void HomePageRenderer::MarkFirstTimeSetupComplete()
{
	// Create setup completion marker
	std::filesystem::path setupPath = std::filesystem::path(globals::state->folderPath) / "setup_complete.json";
	
	try {
		// Ensure directory exists
		std::filesystem::create_directories(setupPath.parent_path());
		
		json setupData;
		setupData["version"] = Plugin::VERSION.string();
		setupData["completed_at"] = static_cast<int64_t>(std::time(nullptr));
		setupData["description"] = "Community Shaders first-time setup completion marker";
		
		std::ofstream file(setupPath);
		if (file.is_open()) {
			file << setupData.dump(2);
			file.close();
			logger::info("First-time setup completed for version {}", Plugin::VERSION.string());
		} else {
			logger::warn("Could not write setup completion file");
		}
		
		isFirstTimeSetupShown = false;
	} catch (const std::exception& e) {
		// If we can't write the file, just mark as shown to avoid repeated popups
		logger::warn("Error writing setup completion file: {}", e.what());
		isFirstTimeSetupShown = false;
	}
}

#include "ThemeManager.h"
#include "../Menu.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>

#include <imgui_impl_dx11.h>

#include "RE/Skyrim.h"
#include "../Utils/FileSystem.h"
#include "../Util.h"

using namespace SKSE;

namespace
{
	/**
	 * @brief Gets file modification time
	 */
	std::time_t GetFileModTime(const std::filesystem::path& filePath)
	{
		try {
			auto fileTime = std::filesystem::last_write_time(filePath);
			auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			return std::chrono::system_clock::to_time_t(systemTime);
		} catch (...) {
			return 0;
		}
	}
}

// Static UI helper methods
void ThemeManager::SetupImGuiStyle(const Menu& menu)
{
	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Theme based on https://github.com/powerof3/DialogueHistory
	auto& themeSettings = menu.GetTheme();

	// rescale here
	auto styleCopy = themeSettings.Style;
	styleCopy.ScaleAllSizes(exp2(themeSettings.GlobalScale));
	styleCopy.MouseCursorScale = 1.f;
	style = styleCopy;
	style.HoverDelayNormal = themeSettings.TooltipHoverDelay;

	// Always use the unified FullPalette system instead of switching between simple/full
	// This ensures consistent behavior regardless of UI presentation mode
	for (size_t i = 0; i < std::min(themeSettings.FullPalette.size(), static_cast<size_t>(ImGuiCol_COUNT)); ++i) {
		colors[i] = themeSettings.FullPalette[i];
	}
	
	// Apply simple palette overrides to the FullPalette for key colors
	// This allows the simple palette controls to work by updating the FullPalette
	colors[ImGuiCol_WindowBg] = themeSettings.Palette.Background;
	colors[ImGuiCol_Text] = themeSettings.Palette.Text;
	colors[ImGuiCol_Border] = themeSettings.Palette.Border;
	colors[ImGuiCol_Separator] = themeSettings.Palette.Border;
	colors[ImGuiCol_ResizeGrip] = themeSettings.Palette.Border;
	
	// Apply derived colors based on simple palette
	ImVec4 textDisabled = themeSettings.Palette.Text;
	textDisabled.w = 0.3f;
	colors[ImGuiCol_TextDisabled] = textDisabled;
	
	ImVec4 resizeGripHovered = themeSettings.Palette.Border;
	resizeGripHovered.w = 0.1f;
	colors[ImGuiCol_ResizeGripHovered] = resizeGripHovered;
	colors[ImGuiCol_ResizeGripActive] = resizeGripHovered;

	// Auto-adjust text colors for better contrast on selection backgrounds
	// This fixes white-on-white text issues in high contrast themes
	auto calculateLuminance = [](const ImVec4& color) {
		auto toLinear = [](float c) { return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
		float r = toLinear(color.x), g = toLinear(color.y), b = toLinear(color.z);
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	};

	auto getContrastingTextColor = [&](const ImVec4& bgColor) {
		float luminance = calculateLuminance(bgColor);
		return luminance > 0.5f ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	};

	// Apply contrast-aware text for selection states
	if (calculateLuminance(colors[ImGuiCol_HeaderActive]) > 0.5f) {
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);  // Black text on light selection
	}
	if (calculateLuminance(colors[ImGuiCol_HeaderHovered]) > 0.5f) {
		// For hovered items, we can't directly change text color, but we can adjust the hover background
		// to ensure better contrast with the current text color
		float textLum = calculateLuminance(colors[ImGuiCol_Text]);
		if (textLum > 0.5f) {  // If text is light, darken the hover background
			ImVec4 darkerHover = colors[ImGuiCol_HeaderHovered];
			darkerHover.x *= 0.3f; darkerHover.y *= 0.3f; darkerHover.z *= 0.3f;
			colors[ImGuiCol_HeaderHovered] = darkerHover;
		}
	}

	// Apply scrollbar opacity settings
	colors[ImGuiCol_ScrollbarBg].w = themeSettings.ScrollbarOpacity.Background;
	colors[ImGuiCol_ScrollbarGrab].w = themeSettings.ScrollbarOpacity.Thumb;
	colors[ImGuiCol_ScrollbarGrabHovered].w = themeSettings.ScrollbarOpacity.ThumbHovered;
	colors[ImGuiCol_ScrollbarGrabActive].w = themeSettings.ScrollbarOpacity.ThumbActive;
}

void ThemeManager::ReloadFont(const Menu& menu, float& cachedFontSize)
{
	auto& themeSettings = menu.GetTheme();

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	ImFontConfig font_config;

	font_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;
	font_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;
	font_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;
	font_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;

	float fontSize = themeSettings.FontSize;
	fontSize = std::clamp(fontSize, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);

	auto fontPath = Util::PathHelpers::GetFontsPath() / "Jost-Regular.ttf";
	if (!io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),
			std::round(fontSize), &font_config)) {
		logger::warn("ThemeManager::ReloadFont() - Failed to load custom font. Using default font.");
		io.Fonts->AddFontDefault();
	}

	io.Fonts->Build();

	ImGui_ImplDX11_InvalidateDeviceObjects();

	io.FontGlobalScale = exp2(themeSettings.GlobalScale);

	cachedFontSize = themeSettings.FontSize;
}

// Theme management methods
size_t ThemeManager::DiscoverThemes()
{
	if (discovered) {
		return themes.size();
	}

	themes.clear();

	auto themesDir = GetThemesDirectory();
	if (!std::filesystem::exists(themesDir)) {
		logger::info("Themes directory does not exist: {}", themesDir.string());
		discovered = true;
		return 0;
	}

	logger::info("Discovering themes in: {}", themesDir.string());

	try {
		for (const auto& entry : std::filesystem::directory_iterator(themesDir)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			// Check file size
			auto fileSize = entry.file_size();
			if (fileSize > MAX_FILE_SIZE) {
				logger::warn("Theme file too large, skipping: {} ({}MB)", 
					entry.path().filename().string(), fileSize / (1024 * 1024));
				continue;
			}

			if (themes.size() >= MAX_THEMES) {
				logger::warn("Maximum number of themes ({}) reached, skipping remaining files", MAX_THEMES);
				break;
			}

			auto themeInfo = LoadThemeFile(entry.path());
			if (themeInfo && themeInfo->isValid) {
				themes.push_back(std::move(*themeInfo));
				logger::info("Discovered theme: {} ({})", themes.back().name, themes.back().displayName);
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error discovering themes: {}", e.what());
	}

	// Sort themes alphabetically by display name
	std::sort(themes.begin(), themes.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
		return a.displayName < b.displayName;
	});

	discovered = true;
	logger::info("Theme discovery complete. Found {} themes", themes.size());
	return themes.size();
}

std::vector<std::string> ThemeManager::GetThemeNames() const
{
	std::vector<std::string> names;
	names.reserve(themes.size());
	
	for (const auto& theme : themes) {
		names.push_back(theme.name);
	}
	
	return names;
}

bool ThemeManager::LoadTheme(const std::string& themeName, json& themeSettings)
{
	if (!discovered) {
		DiscoverThemes();
	}

	if (themeName.empty()) {
		// Empty theme name means use current/custom theme
		return true;
	}

	auto it = std::find_if(themes.begin(), themes.end(), 
		[&themeName](const ThemeInfo& theme) { return theme.name == themeName; });

	if (it == themes.end()) {
		logger::warn("Theme not found: {}", themeName);
		return false;
	}

	if (!it->isValid) {
		logger::warn("Theme is invalid: {}", themeName);
		return false;
	}

	try {
		if (it->themeData.contains("Theme") && it->themeData["Theme"].is_object()) {
			themeSettings = it->themeData["Theme"];
			logger::info("Loaded theme: {} ({})", it->name, it->displayName);
			return true;
		} else {
			logger::warn("Theme file missing 'Theme' object: {}", themeName);
			return false;
		}
	} catch (const std::exception& e) {
		logger::warn("Error loading theme {}: {}", themeName, e.what());
		return false;
	}
}

bool ThemeManager::SaveTheme(const std::string& themeName, const json& themeSettings, 
                            const std::string& displayName, const std::string& description)
{
	if (themeName.empty()) {
		logger::warn("Cannot save theme with empty name");
		return false;
	}

	// Create the full theme JSON structure
	json fullTheme = {
		{"DisplayName", displayName.empty() ? themeName : displayName},
		{"Description", description.empty() ? "Custom user theme" : description},
		{"Version", "1.0.0"},
		{"Author", "User"},
		{"Theme", themeSettings}
	};

	// Generate safe filename (remove invalid characters)
	std::string safeFileName = themeName;
	std::replace_if(safeFileName.begin(), safeFileName.end(), 
		[](char c) { return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'; }, 
		'_');

	auto themesDir = GetThemesDirectory();
	auto filePath = themesDir / (safeFileName + ".json");

	try {
		// Ensure themes directory exists
		std::filesystem::create_directories(themesDir);

		// Write the theme file
		std::ofstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Failed to create theme file: {}", filePath.string());
			return false;
		}

		file << fullTheme.dump(4);  // Pretty print with 4-space indentation
		file.close();

		logger::info("Saved theme: {} to {}", themeName, filePath.string());

		// Refresh themes to include the new one
		RefreshThemes();
		
		return true;
	} catch (const std::exception& e) {
		logger::warn("Error saving theme {}: {}", themeName, e.what());
		return false;
	}
}

const ThemeManager::ThemeInfo* ThemeManager::GetThemeInfo(const std::string& themeName) const
{
	auto it = std::find_if(themes.begin(), themes.end(), 
		[&themeName](const ThemeInfo& theme) { return theme.name == themeName; });
	
	return (it != themes.end()) ? &(*it) : nullptr;
}

void ThemeManager::RefreshThemes()
{
	discovered = false;
	DiscoverThemes();
}

std::filesystem::path ThemeManager::GetThemesDirectory() const
{
	return Util::PathHelpers::GetThemesPath();
}

void ThemeManager::CreateDefaultThemeFiles()
{
	auto themesDir = GetThemesDirectory();
	
	try {
		std::filesystem::create_directories(themesDir);
		logger::info("Ensured themes directory exists: {}", themesDir.string());
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create themes directory: {}", e.what());
		return;
	}

	// Check if any theme files exist - if so, use those instead of creating defaults
	bool hasThemes = false;
	try {
		for (const auto& entry : std::filesystem::directory_iterator(themesDir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				hasThemes = true;
				break;
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to check for existing themes: {}", e.what());
	}

	if (hasThemes) {
		logger::info("Theme files already exist, skipping default creation");
		return;
	}

	// Only create a minimal default theme if no themes exist at all (rare fallback)
	auto defaultThemeFile = themesDir / "Default.json";
	try {
		std::ofstream file(defaultThemeFile);
		if (!file.is_open()) {
			logger::warn("Failed to create default theme file: {}", defaultThemeFile.string());
			return;
		}

		file << R"({
	"DisplayName": "Default Theme",
	"Description": "Default community shaders theme",
	"Version": "1.0",
	"Author": "Community Shaders",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.05, 0.05, 0.05, 1.0],
			"Text": [1.0, 1.0, 1.0, 1.0],
			"Border": [0.4, 0.4, 0.4, 1.0]
		},
		"FontSize": 27.0,
		"GlobalScale": 0.0,
		"TooltipHoverDelay": 0.5
	}
})";

		file.close();
		logger::info("Created default theme file: {}", defaultThemeFile.string());
	} catch (const std::exception& e) {
		logger::warn("Failed to create default theme file: {}", e.what());
	}
}

std::unique_ptr<ThemeManager::ThemeInfo> ThemeManager::LoadThemeFile(const std::filesystem::path& filePath)
{
	auto themeInfo = std::make_unique<ThemeInfo>();
	themeInfo->name = filePath.stem().string();
	themeInfo->filePath = filePath.string();
	themeInfo->lastModified = GetFileModTime(filePath);

	try {
		std::ifstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Failed to open theme file: {}", filePath.string());
			return themeInfo;
		}

		json data;
		file >> data;

		if (!ValidateThemeData(data)) {
			logger::warn("Invalid theme data in file: {}", filePath.string());
			return themeInfo;
		}

		themeInfo->themeData = data;
		
		// Extract metadata
		if (data.contains("DisplayName") && data["DisplayName"].is_string()) {
			themeInfo->displayName = data["DisplayName"].get<std::string>();
		} else {
			themeInfo->displayName = themeInfo->name;
		}

		if (data.contains("Description") && data["Description"].is_string()) {
			themeInfo->description = data["Description"].get<std::string>();
		}

		if (data.contains("Version") && data["Version"].is_string()) {
			themeInfo->version = data["Version"].get<std::string>();
		}

		if (data.contains("Author") && data["Author"].is_string()) {
			themeInfo->author = data["Author"].get<std::string>();
		}

		themeInfo->isValid = true;
		
	} catch (const std::exception& e) {
		logger::warn("Error parsing theme file {}: {}", filePath.string(), e.what());
	}

	return themeInfo;
}

bool ThemeManager::ValidateThemeData(const json& themeData) const
{
	// Basic validation - ensure Theme object exists
	if (!themeData.contains("Theme") || !themeData["Theme"].is_object()) {
		return false;
	}

	// Could add more detailed validation here if needed
	return true;
}
#include "ThemeManager.h"
#include "../Menu.h"

#include "Fonts.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <d3dcompiler.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <unordered_map>
#include <vector>

#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include "RE/Skyrim.h"
#include "State.h"
#include "Util.h"

#include "../Globals.h"
#include "../Util.h"
#include "../Utils/FileSystem.h"
#include "../Utils/UI.h"

using namespace SKSE;

/**
 * THEME MANAGER IMPLEMENTATION NOTES
 * ===================================
 * 
 * BLUR SHADER PARAMETERS:
 * -----------------------
 * The background blur system uses constant buffers to pass parameters to HLSL shaders:
 * 
 * BlurBuffer (cbuffer b0):
 *   - TexelSize.xy: Inverse texture dimensions (1/width, 1/height) for UV calculations
 *   - TexelSize.z:  Blur strength multiplier (0.0-1.0 from BackgroundBlur theme setting)
 *   - BlurParams.x: Number of blur samples (default: 13, must be odd for centered kernel)
 * 
 * The blur uses a separable Gaussian kernel split into two passes:
 *   1. Horizontal pass: Samples along X-axis, outputs to intermediate texture
 *   2. Vertical pass:   Samples along Y-axis from intermediate, outputs final result
 * 
 * Performance scales with sample count (O(width*height*samples)).
 * Higher sample counts = smoother blur but lower FPS.
 * Sub-pixel jitter reduces banding artifacts at low sample counts.
 * 
 * FONT ATLAS REBUILDING:
 * ----------------------
 * Font changes require rebuilding ImGui's texture atlas, which invalidates GPU resources.
 * Must flush GPU pipeline before invalidation to prevent use-after-free crashes.
 * Emergency fallback loads Default.json if user font fails validation.
 * 
 * THREAD SAFETY:
 * --------------
 * - blurResourcesMutex protects all D3D11 blur resources (textures, shaders, buffers)
 * - Font reloading uses atomic flag with compare_exchange_strong to prevent re-entry
 * - Theme discovery caches are protected per-access basis
 */

namespace
{
	// Theme System Constants
	// ======================
	
	// Text Contrast and Opacity
	// -------------------------
	// Disabled text alpha: Makes inactive UI elements visually distinct but still readable
	// Value calibrated for accessibility - too low = invisible, too high = looks enabled
	constexpr float DISABLED_TEXT_ALPHA = 0.3f;  // 30% opacity for disabled elements
	
	// Resize grip hover alpha: Subtle hover effect to avoid visual clutter
	// Low value maintains minimalist aesthetic while providing hover feedback
	constexpr float RESIZE_GRIP_HOVER_ALPHA = 0.1f;  // 10% opacity for hover state
	
	// Blur System Constants
	// ---------------------
	// Text contrast boost per unit blur: Compensates for reduced clarity behind blurred backgrounds
	// Small value preserves theme colors while improving readability
	// Reduced from 0.15f after user testing showed excessive brightness on light themes
	constexpr float BLUR_TEXT_CONTRAST_FACTOR = 0.05f;  // 5% brightness boost at max blur
	
	// Gaussian blur sigma: Controls blur kernel spread (standard deviation)
	// Value 0.5 provides smooth blur without over-blurring fine details
	// Based on Unrimp rendering engine's empirically tested value
	// Lower = sharper (more detail, more banding), Higher = softer (less detail, smoother)
	constexpr float GAUSSIAN_BLUR_SIGMA = 0.5f;
	
	// Contrast Adjustment Constants
	// ------------------------------
	// Luminance threshold for background/text contrast (sRGB middle gray)
	// 0.5 represents perceptual midpoint between black and white
	constexpr float LUMINANCE_THRESHOLD = 0.5f;
	
	// Background darkening factor for light-on-light contrast issues
	// Multiplies RGB by 0.4 = 60% darker, prevents white text on white background
	constexpr float CONTRAST_DARKEN_FACTOR = 0.4f;
	
	// Background lightening offset for dark-on-dark contrast issues
	// Adds 0.3 to RGB = 30% brighter, prevents black text on black background
	constexpr float CONTRAST_LIGHTEN_OFFSET = 0.3f;

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

	// Safety check: If theme appears corrupted/empty, force reload Default.json
	// This prevents fallback to ImGui's hardcoded defaults
	bool isThemeCorrupted = (themeSettings.FullPalette.size() < ImGuiCol_COUNT / 2) ||
	                        (themeSettings.Palette.Background.w == 0.0f && themeSettings.Palette.Text.w == 0.0f);

	if (isThemeCorrupted) {
		logger::warn("Theme appears corrupted, attempting to reload Default.json to prevent ImGui defaults");
		// Note: This violates const correctness but is necessary for emergency theme recovery
		// TODO: Refactor to use separate recovery path that doesn't require const_cast
		if (const_cast<Menu*>(&menu)->LoadThemePreset("Default")) {
			logger::info("Successfully reloaded Default.json theme");
		} else {
			logger::error("Failed to reload Default.json - ImGui may show hardcoded defaults");
		}
	}

	// rescale here
	auto styleCopy = themeSettings.Style;

	float globalScale = themeSettings.GlobalScale;

	// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
	if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
		globalScale = Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
	}

	styleCopy.ScaleAllSizes(exp2(globalScale));
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
	colors[ImGuiCol_Border] = themeSettings.Palette.WindowBorder;
	colors[ImGuiCol_Separator] = themeSettings.Palette.Separator;
	colors[ImGuiCol_ResizeGrip] = themeSettings.Palette.ResizeGrip;

	// Apply frame border to UI elements with frames/borders
	colors[ImGuiCol_FrameBg] = themeSettings.Palette.FrameBorder;
	colors[ImGuiCol_CheckMark] = themeSettings.Palette.Text;
	colors[ImGuiCol_SliderGrab] = themeSettings.Palette.FrameBorder;
	colors[ImGuiCol_SliderGrabActive] = themeSettings.Palette.FrameBorder;

	// Apply derived colors based on simple palette
	ImVec4 textDisabled = themeSettings.Palette.Text;
	textDisabled.w = DISABLED_TEXT_ALPHA;
	colors[ImGuiCol_TextDisabled] = textDisabled;

	ImVec4 resizeGripHovered = themeSettings.Palette.ResizeGrip;
	resizeGripHovered.w = RESIZE_GRIP_HOVER_ALPHA;
	colors[ImGuiCol_ResizeGripHovered] = resizeGripHovered;
	colors[ImGuiCol_ResizeGripActive] = resizeGripHovered;

	// Auto-adjust text colors for better contrast on selection backgrounds
	// This fixes white-on-white text issues in high contrast themes
	// Use centralized color utilities from Utils/UI.h instead of duplicating logic

	// Apply contrast-aware adjustments for headers and tabs
	float textLum = Util::ColorUtils::CalculateLuminance(colors[ImGuiCol_Text]);

	// Apply contrast adjustments for all header and tab backgrounds using unified logic
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_Header], textLum, 
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_HeaderHovered], textLum,
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_HeaderActive], textLum,
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_Tab], textLum,
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_TabActive], textLum,
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);
	Util::ColorUtils::AdjustBackgroundForTextContrast(colors[ImGuiCol_TabHovered], textLum,
		LUMINANCE_THRESHOLD, CONTRAST_DARKEN_FACTOR, CONTRAST_LIGHTEN_OFFSET);

	// Apply contrast-aware text for selection states (TextSelectedBg is used when text is selected)
	if (Util::ColorUtils::CalculateLuminance(colors[ImGuiCol_HeaderActive]) > LUMINANCE_THRESHOLD) {
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);  // Black text on light selection
	} else {
		colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White text on dark selection
	}

	// Apply scrollbar opacity settings
	colors[ImGuiCol_ScrollbarBg].w = themeSettings.ScrollbarOpacity.Background;
	colors[ImGuiCol_ScrollbarGrab].w = themeSettings.ScrollbarOpacity.Thumb;
	colors[ImGuiCol_ScrollbarGrabHovered].w = themeSettings.ScrollbarOpacity.ThumbHovered;
	colors[ImGuiCol_ScrollbarGrabActive].w = themeSettings.ScrollbarOpacity.ThumbActive;

	// Apply background blur effect
	ApplyBackgroundBlur(themeSettings.BackgroundBlur, colors);
}

void ThemeManager::ApplyBackgroundBlur(float blurIntensity, ImVec4* colors)
{
	if (blurIntensity <= 0.0f) {
		isBlurEnabled = false;
		currentBlurIntensity = 0.0f;
		return;
	}

	// Clamp blur intensity to valid range
	blurIntensity = std::clamp(blurIntensity, 0.0f, 1.0f);

	// Store blur parameters for backdrop rendering
	currentBlurIntensity = blurIntensity;
	isBlurEnabled = true;

	// NOTE: Window transparency is now controlled by the background alpha setting
	// The blur intensity only affects the backdrop effect strength, not window alpha

	// Enhance text contrast slightly for better readability over blurred backgrounds
	// Small boost preserves theme colors while compensating for reduced clarity
	ImVec4& text = colors[ImGuiCol_Text];
	float contrastBoost = 1.0f + (blurIntensity * BLUR_TEXT_CONTRAST_FACTOR);
	text.x = std::min(1.0f, text.x * contrastBoost);
	text.y = std::min(1.0f, text.y * contrastBoost);
	text.z = std::min(1.0f, text.z * contrastBoost);
}

void ThemeManager::RenderBackgroundBlur()
{
	// This function should be called after ImGui::Render() but before presenting
	// It renders blur behind visible ImGui windows only

	if (!isBlurEnabled || currentBlurIntensity <= 0.0f) {
		return;
	}

	// Get current theme to check if blur is enabled
	auto menu = globals::menu;
	if (!menu || !menu->IsEnabled) {
		return;
	}

	float blurIntensity = menu->GetTheme().BackgroundBlur;
	if (blurIntensity <= 0.0f) {
		return;
	}

	// Update blur intensity from theme settings
	currentBlurIntensity = blurIntensity;

	// Initialize blur shaders if needed
	if (!InitializeBlurShaders()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(blurResourcesMutex);
		
		// Ensure resources are initialized
		if (!blurVertexShader || !blurHorizontalPixelShader || !blurVerticalPixelShader) {
			return;
		}
	}

	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!device || !context) {
		return;
	}

	// Get current render target
	ID3D11RenderTargetView* currentRTV = nullptr;
	context->OMGetRenderTargets(1, &currentRTV, nullptr);

	if (!currentRTV) {
		return;
	}

	// Get render target texture and its dimensions
	ID3D11Resource* currentRT = nullptr;
	currentRTV->GetResource(&currentRT);

	ID3D11Texture2D* currentTexture = nullptr;
	HRESULT hr = currentRT->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&currentTexture);

	if (FAILED(hr) || !currentTexture) {
		if (currentRT)
			currentRT->Release();
		if (currentRTV)
			currentRTV->Release();
		return;
	}

	D3D11_TEXTURE2D_DESC texDesc;
	currentTexture->GetDesc(&texDesc);

	// Create blur textures if needed
	CreateBlurTextures(texDesc.Width, texDesc.Height, texDesc.Format);

	// Find ImGui windows that need blur
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (!ctx || ctx->Windows.Size == 0) {
		currentTexture->Release();
		currentRT->Release();
		currentRTV->Release();
		return;
	}

	// Apply blur behind each visible ImGui window
	for (int i = 0; i < ctx->Windows.Size; i++) {
		ImGuiWindow* window = ctx->Windows[i];
		if (!window || window->Hidden || !window->WasActive || window->SkipItems) {
			continue;
		}

		// Skip if window has no background (fully transparent)
		if (window->Flags & ImGuiWindowFlags_NoBackground) {
			continue;
		}

		// Get window bounds
		ImVec2 windowMin = window->Pos;
		ImVec2 windowMax = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);

		// Perform blur for this window area
		PerformGaussianBlur(currentTexture, currentRTV, windowMin, windowMax);
	}

	// Cleanup
	currentTexture->Release();
	currentRT->Release();
	currentRTV->Release();
}

void ThemeManager::ForceApplyDefaultTheme()
{
	// This function applies Default.json colors directly to ImGui, bypassing any hardcoded defaults
	// It's used when the theme system fails or ImGui resets to defaults unexpectedly

	auto* themeManager = GetSingleton();
	json defaultThemeSettings;

	if (!themeManager->LoadTheme("Default", defaultThemeSettings)) {
		logger::warn("ForceApplyDefaultTheme: Could not load Default.json theme");
		return;
	}

	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Apply the Default.json theme's FullPalette directly to ImGui colors
	if (defaultThemeSettings.contains("FullPalette") && defaultThemeSettings["FullPalette"].is_array()) {
		auto& palette = defaultThemeSettings["FullPalette"];

		for (size_t i = 0; i < std::min(palette.size(), static_cast<size_t>(ImGuiCol_COUNT)); ++i) {
			if (palette[i].is_array() && palette[i].size() >= 4) {
				colors[i] = ImVec4(
					palette[i][0].get<float>(),
					palette[i][1].get<float>(),
					palette[i][2].get<float>(),
					palette[i][3].get<float>());
			}
		}
		logger::info("ForceApplyDefaultTheme: Applied Default.json colors directly to ImGui");
	} else {
		logger::warn("ForceApplyDefaultTheme: Default.json missing FullPalette - applying basic dark theme");

		// Fallback: Apply a basic dark theme that matches Default.json style
		colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);    // Dark background
		colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // White text
		colors[ImGuiCol_Border] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);         // Gray border
		colors[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);     // Slightly darker child background
		colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);     // Popup background
		colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);         // Header background
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);  // Header hover
		colors[ImGuiCol_HeaderActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);   // Header active
		colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);         // Button background
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);  // Button hover
		colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);   // Button active
	}
}

void ThemeManager::ReloadFont(const Menu& menu, float& cachedFontSize)
{
	// Thread-safe reentrancy guard using atomic flag
	static std::atomic<bool> isReloading{ false };
	bool expected = false;
	if (!isReloading.compare_exchange_strong(expected, true)) {
		logger::warn("ThemeManager::ReloadFont() - Font reload already in progress, skipping");
		return;
	}

	auto& themeSettings = menu.GetTheme();

	logger::info("ThemeManager::ReloadFont() - Starting font reload...");

	ImGuiIO& io = ImGui::GetIO();

	// Additional safety checks: ensure ImGui is in a valid state
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (!ctx) {
		logger::error("ThemeManager::ReloadFont() - No valid ImGui context!");
		isReloading = false;
		return;
	}

	// Ensure we're not in the middle of a frame
	if (ctx->WithinFrameScope) {
		logger::error("ThemeManager::ReloadFont() - Cannot reload font within frame scope!");
		isReloading = false;
		return;
	}

	// Additional rendering state checks
	if (ctx->CurrentWindow || ctx->CurrentTable) {
		logger::error("ThemeManager::ReloadFont() - ImGui has active window/table state!");
		isReloading = false;
		return;
	}

	// Additional check: make sure font atlas exists
	if (!io.Fonts) {
		logger::error("ThemeManager::ReloadFont() - No font atlas available!");
		isReloading = false;
		return;
	}

	// Verify D3D11 device is valid
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!device || !context) {
		logger::error("ThemeManager::ReloadFont() - D3D11 device or context is null!");
		isReloading = false;
		return;
	}

	// Clear existing fonts from the atlas
	io.Fonts->Clear();
	io.Fonts->TexGlyphPadding = 1;

	ImFontConfig font_config;

	font_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;
	font_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;
	font_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;
	font_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;

	float fontSize = ResolveFontSize(menu);
	auto fontsRoot = Util::PathHelpers::GetFontsPath();
	menu.loadedFontRoles.fill(nullptr);

	std::unordered_map<std::string, ImFont*> atlasCache;
	std::vector<size_t> rolesNeedingFallback;

	for (size_t i = 0; i < static_cast<size_t>(Menu::FontRole::Count); ++i) {
		Menu::FontRole role = static_cast<Menu::FontRole>(i);
		auto& mutableRoleSettings = const_cast<Menu&>(menu).GetFontRoleSettings(role);
		Menu::ThemeSettings::FontRoleSettings effective = themeSettings.FontRoles[i];

		if (effective.SizeScale <= 0.f) {
			effective.SizeScale = Menu::GetFontRoleDefaultScale(role);
		}

		if (effective.File.empty()) {
			logger::warn("ThemeManager::ReloadFont() - No font specified for role '{}', using default.", Menu::GetFontRoleKey(role));
			effective = Menu::GetDefaultFontRole(role);
		}

		float scaledSize = std::clamp(fontSize * effective.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		float roundedSize = std::round(scaledSize);
		menu.cachedFontPixelSizesByRole[i] = roundedSize;

		ImFont* loadedFont = nullptr;
		if (!effective.File.empty()) {
			auto fontPath = fontsRoot / effective.File;
			
			// Security: Validate font path stays within fonts directory
			if (!Util::IsPathWithinDirectory(fontsRoot, fontPath)) {
				logger::error("Security: Font path traversal attempt detected for role '{}': {}", 
					Menu::GetFontRoleKey(role), effective.File);
				effective = Menu::GetDefaultFontRole(role);
				fontPath = fontsRoot / effective.File;
			}
			
			if (std::filesystem::exists(fontPath)) {
				std::string cacheKey = std::format("{}|{}", effective.File, static_cast<int>(roundedSize));
				auto cached = atlasCache.find(cacheKey);
				if (cached != atlasCache.end()) {
					loadedFont = cached->second;
				} else {
					ImFontConfig cfg = font_config;
					auto* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), roundedSize, &cfg);
					if (font) {
						atlasCache.emplace(cacheKey, font);
						loadedFont = font;
					} else {
						logger::warn("ThemeManager::ReloadFont() - Failed to load '{}' for role '{}'.", fontPath.string(), Menu::GetFontRoleKey(role));
					}
				}
			} else {
				logger::warn("ThemeManager::ReloadFont() - Font file '{}' missing for role '{}'.", fontPath.string(), Menu::GetFontRoleKey(role));
			}
		}

		if (!loadedFont) {
			rolesNeedingFallback.push_back(i);
		} else {
			menu.loadedFontRoles[i] = loadedFont;
			mutableRoleSettings = effective;
			menu.cachedFontFilesByRole[i] = effective.File;
		}
	}

	const size_t bodyIndex = static_cast<size_t>(Menu::FontRole::Body);
	if (!menu.loadedFontRoles[bodyIndex]) {
		const auto& defaults = Menu::GetDefaultFontRole(Menu::FontRole::Body);
		float bodySize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		float roundedBodySize = std::round(bodySize);
		menu.cachedFontPixelSizesByRole[bodyIndex] = roundedBodySize;
		logger::warn("ThemeManager::ReloadFont() - Falling back to default body font '{}'.", defaults.File);

		ImFont* bodyFont = nullptr;
		auto defaultPath = fontsRoot / defaults.File;
		if (std::filesystem::exists(defaultPath)) {
			std::string cacheKey = std::format("{}|{}", defaults.File, static_cast<int>(roundedBodySize));
			ImFontConfig cfg = font_config;
			bodyFont = io.Fonts->AddFontFromFileTTF(defaultPath.string().c_str(), roundedBodySize, &cfg);
			if (bodyFont) {
				atlasCache.emplace(cacheKey, bodyFont);
			}
		}
		if (!bodyFont) {
			bodyFont = io.Fonts->AddFontDefault();
		}

		menu.loadedFontRoles[bodyIndex] = bodyFont;
		const_cast<Menu&>(menu).GetFontRoleSettings(Menu::FontRole::Body) = defaults;
		menu.cachedFontFilesByRole[bodyIndex] = defaults.File;
		menu.cachedFontName = defaults.File;
		const_cast<Menu&>(menu).GetSettings().Theme.FontName = defaults.File;
	}

	ImFont* bodyFont = menu.loadedFontRoles[bodyIndex];
	for (size_t idx : rolesNeedingFallback) {
		if (idx == bodyIndex) {
			continue;
		}
		Menu::FontRole role = static_cast<Menu::FontRole>(idx);
		const auto& defaults = Menu::GetDefaultFontRole(role);
		float fallbackSize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		menu.cachedFontPixelSizesByRole[idx] = std::round(fallbackSize);
		menu.loadedFontRoles[idx] = bodyFont;
		const_cast<Menu&>(menu).GetFontRoleSettings(role) = defaults;
		menu.cachedFontFilesByRole[idx] = defaults.File;
		logger::warn("ThemeManager::ReloadFont() - Falling back to '{}' for role '{}'.", defaults.File, Menu::GetFontRoleKey(role));
	}

	if (!bodyFont) {
		bodyFont = io.Fonts->AddFontDefault();
		menu.loadedFontRoles[bodyIndex] = bodyFont;
	}

	io.FontDefault = bodyFont ? bodyFont : io.Fonts->AddFontDefault();
	menu.cachedFontName = const_cast<Menu&>(menu).GetFontRoleSettings(Menu::FontRole::Body).File;
	cachedFontSize = fontSize;
	const_cast<Menu&>(menu).GetSettings().Theme.FontName = menu.cachedFontName;
	const_cast<Menu&>(menu).cachedFontSignature = const_cast<Menu&>(menu).BuildFontSignature(fontSize);

	// Build the font atlas - this bakes all fonts into the texture
	if (!io.Fonts->Build()) {
		logger::error("ThemeManager::ReloadFont() - Failed to build font atlas!");
		
		// Emergency fallback: try to restore with default font before giving up
		logger::warn("ThemeManager::ReloadFont() - Attempting emergency fallback to default font...");
		io.Fonts->Clear();
		ImFont* fallbackFont = io.Fonts->AddFontDefault();
		if (fallbackFont && io.Fonts->Build()) {
			logger::info("ThemeManager::ReloadFont() - Emergency fallback successful");
			menu.loadedFontRoles.fill(fallbackFont);
			io.FontDefault = fallbackFont;
		} else {
			logger::error("ThemeManager::ReloadFont() - Emergency fallback also failed!");
			isReloading = false;
			return;
		}
	}

	// Recreate device objects - this is where crashes can occur
	// Must be done between frames with no active rendering state

	logger::debug("ThemeManager::ReloadFont() - Invalidating DX11 device objects...");
	
	// Flush any pending GPU operations before invalidating
	context->Flush();
	
	ImGui_ImplDX11_InvalidateDeviceObjects();

	logger::debug("ThemeManager::ReloadFont() - Creating DX11 device objects...");
	if (!ImGui_ImplDX11_CreateDeviceObjects()) {
		logger::error("ThemeManager::ReloadFont() - Failed to create device objects!");

		// Emergency fallback: restore with default font and retry device objects
		logger::warn("ThemeManager::ReloadFont() - Attempting emergency device object recovery...");
		io.Fonts->Clear();
		ImFont* fallbackFont = io.Fonts->AddFontDefault();
		
		bool recoverySucceeded = false;
		if (fallbackFont && io.Fonts->Build()) {
			ImGui_ImplDX11_InvalidateDeviceObjects();
			if (ImGui_ImplDX11_CreateDeviceObjects()) {
				logger::warn("ThemeManager::ReloadFont() - Emergency recovery successful with default font");
				menu.loadedFontRoles.fill(fallbackFont);
				io.FontDefault = fallbackFont;
				menu.cachedFontName = "ImGui Default";
				recoverySucceeded = true;
			}
		}
		
		if (!recoverySucceeded) {
			logger::error("ThemeManager::ReloadFont() - Critical failure: unable to recover device objects!");
		}

		isReloading = false;
		return;
	}

	logger::debug("ThemeManager::ReloadFont() - Device objects recreated successfully");

	// Verify font texture was created successfully
	if (!io.Fonts->TexID) {
		logger::error("ThemeManager::ReloadFont() - Font texture not created!");
		isReloading = false;
		return;
	}

	float globalScale = themeSettings.GlobalScale;

	// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
	if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
		globalScale = Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
	}

	io.FontGlobalScale = exp2(globalScale);

	cachedFontSize = fontSize;
	// Also update cached font name in the menu instance
	menu.cachedFontName = themeSettings.FontName;

	logger::info("ThemeManager::ReloadFont() - Font reload completed successfully");
	isReloading = false;
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
		{ "DisplayName", displayName.empty() ? themeName : displayName },
		{ "Description", description.empty() ? "Custom user theme" : description },
		{ "Version", "1.0.0" },
		{ "Author", "User" },
		{ "Theme", themeSettings }
	};

	// Generate safe filename (remove invalid characters)
	std::string safeFileName = themeName;
	std::replace_if(safeFileName.begin(), safeFileName.end(), [](char c) { return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'; }, '_');

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
		// Security: Verify path is within themes directory
		auto themesDir = GetThemesDirectory();
		if (!Util::IsPathWithinDirectory(themesDir, filePath)) {
			logger::error("Security: Theme file outside allowed directory: {}", filePath.string());
			return themeInfo;
		}
		
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
	return themeData.contains("Theme") && themeData["Theme"].is_object();
}

float ThemeManager::ResolveFontSize(const Menu& menu)
{
	const auto& themeSettings = menu.GetTheme();
	float configured = themeSettings.FontSize;

	// If user configured a positive size, use it (clamped)
	if (std::round(configured) > 0) {
		return std::clamp(configured, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
	}

	// Otherwise, compute dynamic default based on current screen resolution
	float dynamicSize = Constants::DEFAULT_FONT_SIZE;
	if (globals::state && globals::state->screenSize.y > 0) {
		dynamicSize = globals::state->screenSize.y * Constants::DEFAULT_FONT_RATIO;
	} else {
		logger::warn("ThemeManager::ResolveFontSize() - Falling back to DEFAULT_FONT_SIZE due to missing screen height.");
	}
	return std::clamp(dynamicSize, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
}

// Blur shader implementation
// https://github.com/cofenberg/unrimp/
bool ThemeManager::InitializeBlurShaders()
{
	std::lock_guard<std::mutex> lock(blurResourcesMutex);
	
	static bool initialized = false;
	static bool initializationFailed = false;

	if (initialized || initializationFailed) {
		return initialized;
	}

	auto device = globals::d3d::device;
	if (!device) {
		initializationFailed = true;
		return false;
	}

	try {
		// Baked-in HLSL shaders for reliable Gaussian blur implementation
		// Based on Unrimp rendering engine's separable blur architecture

		const char* horizontalBlurShader = R"(
// Horizontal Gaussian Blur Shader - Baked into ThemeManager.cpp
cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;       // x = 1/width, y = 1/height, z = blur strength, w = unused
    int4   BlurParams;      // x = samples, y = unused, z = unused, w = unused
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Vertex Shader - Fullscreen triangle (no input needed)
VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    // Generate fullscreen triangle from vertex ID
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y; // Flip Y for correct orientation
    return output;
}

// Gaussian weight calculation based on Unrimp's implementation
// SIGMA = 0.5 provides good balance between smoothness and detail preservation
// Lower values = sharper blur (more detail but more banding artifacts)
// Higher values = softer blur (less detail but smoother gradients)
float GaussianWeight(float offset)
{
    const float SIGMA = 0.5f;  // Empirically tested optimal value from Unrimp engine
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

// Pixel Shader - Horizontal Gaussian Blur
float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    const int samples = min(BlurParams.x, 15);
    const int halfSamples = samples / 2;

    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(i * TexelSize.x, 0.0f);
        float weight = GaussianWeight(float(i));

        if (sampleCoord.x >= 0.0f && sampleCoord.x <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f)
        result /= totalWeight;

    return result;
}
)";

		const char* verticalBlurShader = R"(
// Vertical Gaussian Blur Shader - Baked into ThemeManager.cpp
cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;       // x = 1/width, y = 1/height, z = blur strength, w = unused
    int4   BlurParams;      // x = samples, y = unused, z = unused, w = unused
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Vertex Shader - Fullscreen triangle (no input needed)
VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    // Generate fullscreen triangle from vertex ID
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y; // Flip Y for correct orientation
    return output;
}

// Gaussian weight calculation based on Unrimp's implementation
// SIGMA = 0.5 provides good balance between smoothness and detail preservation
// Lower values = sharper blur (more detail but more banding artifacts)
// Higher values = softer blur (less detail but smoother gradients)
float GaussianWeight(float offset)
{
    const float SIGMA = 0.5f;  // Empirically tested optimal value from Unrimp engine
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

// Pixel Shader - Vertical Gaussian Blur
float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    const int samples = min(BlurParams.x, 15);
    const int halfSamples = samples / 2;

    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(0.0f, i * TexelSize.y);
        float weight = GaussianWeight(float(i));

        if (sampleCoord.y >= 0.0f && sampleCoord.y <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f)
        result /= totalWeight;

    return result;
}
)";

		// Compile vertex shader using D3DCompile with baked-in HLSL
		ID3DBlob* vsBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;

		HRESULT hr = D3DCompile(horizontalBlurShader, strlen(horizontalBlurShader),
			"InlineHorizontalBlurShader", nullptr, nullptr,
			"VS_Main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);

		if (FAILED(hr)) {
			if (errorBlob) {
				logger::error("Failed to compile baked Gaussian blur vertex shader: {}", (char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			initializationFailed = true;
			return false;
		}

		hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, blurVertexShader.put());
		vsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create Gaussian blur vertex shader");
			initializationFailed = true;
			return false;
		}

		// Compile horizontal blur pixel shader
		ID3DBlob* hpsBlob = nullptr;
		hr = D3DCompile(horizontalBlurShader, strlen(horizontalBlurShader),
			"InlineHorizontalBlurShader", nullptr, nullptr,
			"PS_Main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &hpsBlob, &errorBlob);

		if (FAILED(hr)) {
			if (errorBlob) {
				logger::error("Failed to compile baked horizontal Gaussian blur pixel shader: {}", (char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			initializationFailed = true;
			return false;
		}

		hr = device->CreatePixelShader(hpsBlob->GetBufferPointer(), hpsBlob->GetBufferSize(), nullptr, blurHorizontalPixelShader.put());
		hpsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create horizontal Gaussian blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Compile vertical blur pixel shader
		ID3DBlob* vpsBlob = nullptr;
		hr = D3DCompile(verticalBlurShader, strlen(verticalBlurShader),
			"InlineVerticalBlurShader", nullptr, nullptr,
			"PS_Main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vpsBlob, &errorBlob);

		if (FAILED(hr)) {
			if (errorBlob) {
				logger::error("Failed to compile baked vertical Gaussian blur pixel shader: {}", (char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			initializationFailed = true;
			return false;
		}

		hr = device->CreatePixelShader(vpsBlob->GetBufferPointer(), vpsBlob->GetBufferSize(), nullptr, blurVerticalPixelShader.put());
		vpsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create vertical Gaussian blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Create constant buffer for blur parameters based on Unrimp architecture
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = 32;  // Match our BlurConstants struct: float4 texelSize + int4 blurParams
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		hr = device->CreateBuffer(&bufferDesc, nullptr, blurConstantBuffer.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur constant buffer");
			initializationFailed = true;
			return false;
		}

		// Create sampler state for blur
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = device->CreateSamplerState(&samplerDesc, blurSamplerState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur sampler state");
			initializationFailed = true;
			return false;
		}

		// Create blend state for proper alpha blending
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = FALSE;
		blendDesc.IndependentBlendEnable = FALSE;
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState(&blendDesc, blurBlendState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur blend state");
			initializationFailed = true;
			return false;
		}

		initialized = true;
		logger::info("Gaussian blur shaders initialized successfully with Unrimp architecture");
		return true;

	} catch (const std::exception& e) {
		logger::error("Exception during Gaussian blur shader initialization: {}", e.what());
		initializationFailed = true;
		return false;
	} catch (...) {
		logger::error("Unknown exception during Gaussian blur shader initialization");
		initializationFailed = true;
		return false;
	}
}

void ThemeManager::CreateBlurTextures(UINT width, UINT height, DXGI_FORMAT format)
{
	std::lock_guard<std::mutex> lock(blurResourcesMutex);
	
	// Check if textures need to be recreated
	if (blurTexture1 && blurTextureWidth == width && blurTextureHeight == height) {
		return;
	}

	// Clean up existing textures (com_ptr handles Release automatically on reset)
	blurTexture1 = nullptr;
	blurTexture2 = nullptr;
	blurRTV1 = nullptr;
	blurRTV2 = nullptr;
	blurSRV1 = nullptr;
	blurSRV2 = nullptr;

	auto device = globals::d3d::device;
	if (!device)
		return;

	// Use full resolution textures for better quality
	UINT blurWidth = width;
	UINT blurHeight = height;

	// Create intermediate blur textures
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = blurWidth;
	textureDesc.Height = blurHeight;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = format;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, blurTexture1.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur texture 1");
		return;
	}

	hr = device->CreateTexture2D(&textureDesc, nullptr, blurTexture2.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur texture 2");
		return;
	}

	// Create render target views
	hr = device->CreateRenderTargetView(blurTexture1.get(), nullptr, blurRTV1.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur RTV 1");
		return;
	}

	hr = device->CreateRenderTargetView(blurTexture2.get(), nullptr, blurRTV2.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur RTV 2");
		return;
	}

	// Create shader resource views
	hr = device->CreateShaderResourceView(blurTexture1.get(), nullptr, blurSRV1.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur SRV 1");
		return;
	}

	hr = device->CreateShaderResourceView(blurTexture2.get(), nullptr, blurSRV2.put());
	if (FAILED(hr)) {
		logger::error("Failed to create blur SRV 2");
		return;
	}

	blurTextureWidth = width;
	blurTextureHeight = height;
}

void ThemeManager::PerformGaussianBlur(ID3D11Texture2D* sourceTexture, ID3D11RenderTargetView* targetRTV, ImVec2 menuMin, ImVec2 menuMax)
{
	std::lock_guard<std::mutex> lock(blurResourcesMutex);
	
	auto context = globals::d3d::context;
	if (!context || !sourceTexture || !targetRTV)
		return;
	
	// Ensure resources exist before using
	if (!blurVertexShader || !blurHorizontalPixelShader || !blurVerticalPixelShader) {
		return;
	}

	// Get source texture description
	D3D11_TEXTURE2D_DESC sourceDesc;
	sourceTexture->GetDesc(&sourceDesc);

	// Create shader resource view for source
	ID3D11ShaderResourceView* sourceSRV = nullptr;
	auto device = globals::d3d::device;
	HRESULT hr = device->CreateShaderResourceView(sourceTexture, nullptr, &sourceSRV);
	if (FAILED(hr)) {
		logger::error("Failed to create source SRV for blur");
		return;
	}

	// Save current state
	ID3D11RenderTargetView* originalRTV = nullptr;
	ID3D11DepthStencilView* originalDSV = nullptr;
	context->OMGetRenderTargets(1, &originalRTV, &originalDSV);

	D3D11_VIEWPORT originalViewport;
	UINT numViewports = 1;
	context->RSGetViewports(&numViewports, &originalViewport);

	// Calculate menu area in texture coordinates (for scissor testing)
	FLOAT menuLeft = std::max(0.0f, menuMin.x);
	FLOAT menuTop = std::max(0.0f, menuMin.y);
	FLOAT menuRight = std::min(static_cast<FLOAT>(sourceDesc.Width), menuMax.x);
	FLOAT menuBottom = std::min(static_cast<FLOAT>(sourceDesc.Height), menuMax.y);

	// Set scissor rectangle to limit blur to menu area
	D3D11_RECT scissorRect;
	scissorRect.left = static_cast<LONG>(menuLeft);
	scissorRect.top = static_cast<LONG>(menuTop);
	scissorRect.right = static_cast<LONG>(menuRight);
	scissorRect.bottom = static_cast<LONG>(menuBottom);
	context->RSSetScissorRects(1, &scissorRect);

	// Set up blur parameters matching our Unrimp-based HLSL shader structure
	struct BlurConstants
	{
		float texelSize[4];  // x = 1/width, y = 1/height, z = blur strength, w = unused
		int blurParams[4];   // x = samples, y = unused, z = unused, w = unused
	} constants;

	// Calculate blur parameters based on intensity slider
	float blurRadius = currentBlurIntensity * 5.0f;                                               // Scale blur radius by intensity
	int sampleCount = std::max(3, std::min(15, static_cast<int>(7 + currentBlurIntensity * 8)));  // 3-15 samples based on intensity

	constants.texelSize[0] = blurRadius / static_cast<float>(blurTextureWidth);
	constants.texelSize[1] = blurRadius / static_cast<float>(blurTextureHeight);
	constants.texelSize[2] = currentBlurIntensity;  // Blur strength multiplier
	constants.texelSize[3] = 0.0f;                  // Unused

	constants.blurParams[0] = sampleCount;  // Dynamic sample count based on intensity
	constants.blurParams[1] = 0;            // Unused
	constants.blurParams[2] = 0;            // Unused
	constants.blurParams[3] = 0;            // Unused

	context->UpdateSubresource(blurConstantBuffer.get(), 0, nullptr, &constants, 0, 0);

	// Set up viewport for blur textures
	D3D11_VIEWPORT blurViewport = {};
	blurViewport.Width = static_cast<FLOAT>(blurTextureWidth);
	blurViewport.Height = static_cast<FLOAT>(blurTextureHeight);
	blurViewport.MinDepth = 0.0f;
	blurViewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &blurViewport);

	// Set shaders and states
	auto constantBufferPtr = blurConstantBuffer.get();
	auto samplerStatePtr = blurSamplerState.get();
	auto rtv1Ptr = blurRTV1.get();
	auto rtv2Ptr = blurRTV2.get();
	
	context->VSSetShader(blurVertexShader.get(), nullptr, 0);
	context->PSSetConstantBuffers(0, 1, &constantBufferPtr);
	context->PSSetSamplers(0, 1, &samplerStatePtr);

	// First pass: Horizontal blur (source -> blur texture 1)
	context->OMSetRenderTargets(1, &rtv1Ptr, nullptr);
	context->PSSetShader(blurHorizontalPixelShader.get(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &sourceSRV);
	context->Draw(3, 0);  // Draw fullscreen triangle

	// Second pass: Vertical blur (blur texture 1 -> blur texture 2)
	context->OMSetRenderTargets(1, &rtv2Ptr, nullptr);
	context->PSSetShader(blurVerticalPixelShader.get(), nullptr, 0);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	auto srv1Ptr = blurSRV1.get();
	auto srv2Ptr = blurSRV2.get();
	context->PSSetShaderResources(0, 1, &nullSRV);  // Clear previous SRV
	context->PSSetShaderResources(0, 1, &srv1Ptr);
	context->Draw(3, 0);

	// Final composition: Blend blurred result back to main render target (only in menu area)
	context->RSSetViewports(1, &originalViewport);
	context->OMSetRenderTargets(1, &targetRTV, nullptr);

	// Enable scissor test to limit blur to menu area
	ID3D11RasterizerState* originalRS = nullptr;
	context->RSGetState(&originalRS);

	// Create rasterizer state with scissor test enabled for final composition
	ID3D11RasterizerState* scissorRS = nullptr;
	D3D11_RASTERIZER_DESC rsDesc = {};
	if (originalRS) {
		originalRS->GetDesc(&rsDesc);
	} else {
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_BACK;
		rsDesc.FrontCounterClockwise = FALSE;
		rsDesc.DepthBias = 0;
		rsDesc.DepthBiasClamp = 0.0f;
		rsDesc.SlopeScaledDepthBias = 0.0f;
		rsDesc.DepthClipEnable = TRUE;
		rsDesc.MultisampleEnable = FALSE;
		rsDesc.AntialiasedLineEnable = FALSE;
	}
	rsDesc.ScissorEnable = TRUE;

	device->CreateRasterizerState(&rsDesc, &scissorRS);
	if (scissorRS) {
		context->RSSetState(scissorRS);
	}

	// Set blend state for proper compositing
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, currentBlurIntensity * 0.8f };
	context->OMSetBlendState(blurBlendState.get(), blendFactor, 0xFFFFFFFF);

	context->PSSetShaderResources(0, 1, &nullSRV);  // Clear previous SRV
	context->PSSetShaderResources(0, 1, &srv2Ptr);
	context->Draw(3, 0);

	// Restore original state
	context->OMSetRenderTargets(1, &originalRTV, originalDSV);
	context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	context->PSSetShaderResources(0, 1, &nullSRV);
	context->RSSetState(originalRS);
	context->RSSetScissorRects(0, nullptr);  // Disable scissor test

	// Clean up
	if (sourceSRV)
		sourceSRV->Release();
	if (originalRTV)
		originalRTV->Release();
	if (originalDSV)
		originalDSV->Release();
	if (originalRS)
		originalRS->Release();
	if (scissorRS)
		scissorRS->Release();
}

void ThemeManager::CleanupBlurResources()
{
	std::lock_guard<std::mutex> lock(blurResourcesMutex);
	
	// com_ptr automatically calls Release() when reset to nullptr
	blurVertexShader = nullptr;
	blurHorizontalPixelShader = nullptr;
	blurVerticalPixelShader = nullptr;
	blurConstantBuffer = nullptr;
	blurSamplerState = nullptr;
	blurBlendState = nullptr;

	blurTexture1 = nullptr;
	blurTexture2 = nullptr;
	blurRTV1 = nullptr;
	blurRTV2 = nullptr;
	blurSRV1 = nullptr;
	blurSRV2 = nullptr;

	blurTextureWidth = 0;
	blurTextureHeight = 0;
	isBlurEnabled = false;
	currentBlurIntensity = 0.0f;
}
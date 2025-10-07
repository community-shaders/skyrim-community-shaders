#include "ThemeManager.h"
#include "../Menu.h"

#include <algorithm>
#include <cmath>
#include <imgui_impl_dx11.h>

#include "RE/Skyrim.h"
#include "State.h"
#include "Util.h"

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

	if (themeSettings.UseSimplePalette) {
		float hoveredAlpha{ 0.1f };

		ImVec4 resizeGripHovered = themeSettings.Palette.Border;
		resizeGripHovered.w = hoveredAlpha;

		ImVec4 textDisabled = themeSettings.Palette.Text;
		textDisabled.w = 0.3f;

		ImVec4 header{ 1.0f, 1.0f, 1.0f, 0.15f };
		ImVec4 headerHovered = header;
		headerHovered.w = hoveredAlpha;

		ImVec4 tabHovered{ 0.2f, 0.2f, 0.2f, 1.0f };

		ImVec4 sliderGrab{ 1.0f, 1.0f, 1.0f, 0.245f };
		ImVec4 sliderGrabActive{ 1.0f, 1.0f, 1.0f, 0.531f };

		ImVec4 scrollbarGrab{ 0.31f, 0.31f, 0.31f, 1.0f };
		ImVec4 scrollbarGrabHovered{ 0.41f, 0.41f, 0.41f, 1.0f };
		ImVec4 scrollbarGrabActive{ 0.51f, 0.51f, 0.51f, 1.0f };

		colors[ImGuiCol_WindowBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_ChildBg] = ImVec4();
		colors[ImGuiCol_ScrollbarBg] = ImVec4();
		colors[ImGuiCol_TableHeaderBg] = ImVec4();
		colors[ImGuiCol_TableRowBg] = ImVec4();
		colors[ImGuiCol_TableRowBgAlt] = ImVec4();

		colors[ImGuiCol_Border] = themeSettings.Palette.Border;
		colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
		colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_Border];
		colors[ImGuiCol_ResizeGripHovered] = resizeGripHovered;
		colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ResizeGripHovered];

		colors[ImGuiCol_Text] = themeSettings.Palette.Text;
		colors[ImGuiCol_TextDisabled] = textDisabled;

		colors[ImGuiCol_FrameBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_FrameBgHovered] = headerHovered;
		colors[ImGuiCol_FrameBgActive] = colors[ImGuiCol_FrameBg];

		colors[ImGuiCol_DockingEmptyBg] = themeSettings.Palette.Border;
		colors[ImGuiCol_DockingPreview] = themeSettings.Palette.Border;

		colors[ImGuiCol_PlotHistogram] = themeSettings.Palette.Border;

		colors[ImGuiCol_SliderGrab] = sliderGrab;
		colors[ImGuiCol_SliderGrabActive] = sliderGrabActive;

		colors[ImGuiCol_Header] = header;
		colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_Header];
		colors[ImGuiCol_HeaderHovered] = headerHovered;

		colors[ImGuiCol_Button] = ImVec4();
		colors[ImGuiCol_ButtonHovered] = headerHovered;
		colors[ImGuiCol_ButtonActive] = ImVec4();

		colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
		colors[ImGuiCol_ScrollbarGrabHovered] = scrollbarGrabHovered;
		colors[ImGuiCol_ScrollbarGrabActive] = scrollbarGrabActive;

		colors[ImGuiCol_TitleBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
		colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];

		colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_TitleBg];

		colors[ImGuiCol_CheckMark] = themeSettings.Palette.Text;

		colors[ImGuiCol_Tab] = themeSettings.FullPalette[ImGuiCol_Tab];
		colors[ImGuiCol_TabActive] = themeSettings.FullPalette[ImGuiCol_TabActive];
		colors[ImGuiCol_TabHovered] = tabHovered;
		colors[ImGuiCol_TabUnfocused] = themeSettings.FullPalette[ImGuiCol_TabUnfocused];
		colors[ImGuiCol_TabUnfocusedActive] = themeSettings.FullPalette[ImGuiCol_TabUnfocusedActive];

		colors[ImGuiCol_PopupBg] = themeSettings.Palette.Background;

		colors[ImGuiCol_TableBorderStrong] = colors[ImGuiCol_Border];
		colors[ImGuiCol_TableBorderLight] = colors[ImGuiCol_Border];

		colors[ImGuiCol_TextSelectedBg] = header;
	} else {
		std::copy(themeSettings.FullPalette.begin(), themeSettings.FullPalette.end(), std::span(colors).begin());
	}
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

	// Compute effective font size (user value or dynamic default)
	float fontSize = ResolveFontSize(menu);

	auto fontPath = Util::PathHelpers::GetFontsPath() / "Jost-Regular.ttf";
	if (!io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),
			std::round(fontSize), &font_config)) {
		logger::warn("ThemeManager::ReloadFont() - Failed to load custom font. Using default font.");
		io.Fonts->AddFontDefault();
	}

	io.Fonts->Build();

	ImGui_ImplDX11_InvalidateDeviceObjects();

	io.FontGlobalScale = exp2(themeSettings.GlobalScale);

	// Cache the effective size so we can detect changes accurately
	cachedFontSize = fontSize;
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
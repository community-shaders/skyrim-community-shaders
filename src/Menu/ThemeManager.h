#pragma once

#include <imgui.h>

class ThemeManager
{
public:
	static void SetupImGuiStyle(const class Menu& menu);
	static void ReloadFont(const class Menu& menu, float& cachedFontSize);

	struct Constants
	{
		// Font configuration constants
		static constexpr float MIN_FONT_SIZE = 8.0f;
		static constexpr float MAX_FONT_SIZE = 32.0f;
		static constexpr int FCONF_OVERSAMPLE_H = 3;
		static constexpr int FCONF_OVERSAMPLE_V = 1;
		static constexpr bool FCONF_PIXELSNAP_H = true;
		static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f;

		// Header rendering constants
		static constexpr float HEADER_BASE_TEXT_SCALE = 1.7f;
		static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.5f;
		static constexpr float HEADER_FALLBACK_TEXT_SCALE = 1.5f;
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.25f;
		static constexpr float DOCKED_ICON_SPACING = 8.0f;
		static constexpr float DOCKED_RIGHT_MARGIN = 45.0f;
		static constexpr float WATERMARK_HEIGHT_PERCENT = 0.50f;
		static constexpr float UNDOCKED_ICON_PADDING_REDUCTION = 4.0f;
		static constexpr float DOCKED_ICON_PADDING_REDUCTION = 2.0f;

		// UI Layout constants
		static constexpr float BUTTON_PADDING = 16.0f;
		static constexpr float BUTTON_SPACING = 8.0f;
		static constexpr float OVERLAY_WINDOW_POSITION = 10.0f;
		static constexpr float FONT_CACHE_EPSILON = 0.01f;
		static constexpr float CURSOR_POSITION_PADDING = 5.0f;
		static constexpr float SEPARATOR_THICKNESS = 3.0f;
		static constexpr float UNDOCKED_ICON_ITEM_SPACING = 6.0f;
	};
};
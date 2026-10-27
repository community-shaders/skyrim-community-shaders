#pragma once

#include "IconsTabler.h"

/**
 * @brief The one Tabler glyph for each concept the menu shows.
 *
 * Every surface that names one of these concepts (nav, header, Home cards, palette,
 * status bar, toasts) uses the glyph from here so the same idea always looks the same.
 */
namespace MenuIcons
{
	inline constexpr const char* kHome = ICON_TI_HOME;
	inline constexpr const char* kPresets = ICON_TI_PALETTE;
	inline constexpr const char* kCSEditor = ICON_TI_ADJUSTMENTS_HORIZONTAL;
	inline constexpr const char* kSettings = ICON_TI_SETTINGS;
	inline constexpr const char* kProfiling = ICON_TI_CHART_BAR;
	inline constexpr const char* kIssues = ICON_TI_ALERT_TRIANGLE;
	inline constexpr const char* kShaders = ICON_TI_APERTURE;
	inline constexpr const char* kLocations = ICON_TI_MAP_PIN;
	inline constexpr const char* kTestScene = ICON_TI_PHOTO;
	inline constexpr const char* kSave = ICON_TI_DEVICE_FLOPPY;
	inline constexpr const char* kRestore = ICON_TI_RESTORE;
	inline constexpr const char* kClearCache = ICON_TI_TRASH;
	inline constexpr const char* kSearch = ICON_TI_SEARCH;
	inline constexpr const char* kHelp = ICON_TI_HELP;
	inline constexpr const char* kPin = ICON_TI_PIN;
	inline constexpr const char* kExternal = ICON_TI_EXTERNAL_LINK;
	inline constexpr const char* kFolder = ICON_TI_FOLDER_OPEN;
	inline constexpr const char* kLogs = ICON_TI_FILE_TEXT;
}

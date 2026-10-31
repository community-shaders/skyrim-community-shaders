// Curated Tabler Icons macros - only glyphs referenced from src/.
// To add an icon: copy one #define (UTF-8 + U+ comment) from IconFontCppHeaders / webfont CSS,
// then add a singleton pair for that codepoint in ICON_TI_GLYPH_RANGES.
// Only codepoints <= U+FFFF load: ImWchar is 16-bit (IMGUI_USE_WCHAR32 is off), so Tabler
// icons above U+FFFF (e.g. tag, U+10096) can't be used; pick an alternative glyph instead.

#pragma once

#define FONT_ICON_FILE_NAME_TI "tabler-icons.ttf"

#define ICON_MIN_TI 0xea06
#define ICON_MAX_16_TI 0xfea8
#define ICON_MAX_TI 0xfea8

#define ICON_TI_ALERT_TRIANGLE "\xee\xa8\x86"	// U+ea06
#define ICON_TI_ARROW_LEFT "\xee\xa8\x99"	// U+ea19
#define ICON_TI_BORDER_ALL "\xee\xa8\xbb"	// U+ea3b
#define ICON_TI_BULB "\xee\xa9\x91"	// U+ea51
#define ICON_TI_CHART_BAR "\xee\xa9\x99"	// U+ea59
#define ICON_TI_CHECK "\xee\xa9\x9e"	// U+ea5e
#define ICON_TI_CHEVRON_DOWN "\xee\xa9\x9f"	// U+ea5f
#define ICON_TI_CHEVRON_LEFT "\xee\xa9\xa0"	// U+ea60
#define ICON_TI_CHEVRON_RIGHT "\xee\xa9\xa1"	// U+ea61
#define ICON_TI_CHEVRON_UP "\xee\xa9\xa2"	// U+ea62
#define ICON_TI_CIRCLE_X "\xee\xa9\xaa"	// U+ea6a
#define ICON_TI_CLOCK "\xee\xa9\xb0"	// U+ea70
#define ICON_TI_CLOUD_RAIN "\xee\xa9\xb2"	// U+ea72
#define ICON_TI_CLOUD_STORM "\xee\xa9\xb4"	// U+ea74
#define ICON_TI_CLOUD "\xee\xa9\xb6"	// U+ea76
#define ICON_TI_COPY "\xee\xa9\xba"	// U+ea7a
#define ICON_TI_DOTS "\xee\xaa\x95"	// U+ea95
#define ICON_TI_EXTERNAL_LINK "\xee\xaa\x99"	// U+ea99
#define ICON_TI_EYE "\xee\xaa\x9a"	// U+ea9a
#define ICON_TI_HELP "\xee\xaa\xbf"	// U+eabf
#define ICON_TI_HOME "\xee\xab\x81"	// U+eac1
#define ICON_TI_LAYOUT_COLUMNS "\xee\xab\x94"	// U+ead4
#define ICON_TI_LOCK "\xee\xab\xa2"	// U+eae2
#define ICON_TI_MAP_PIN "\xee\xab\xa8"	// U+eae8
#define ICON_TI_PALETTE "\xee\xac\x81"	// U+eb01
#define ICON_TI_PENCIL "\xee\xac\x84"	// U+eb04
#define ICON_TI_PLUS "\xee\xac\x8b"	// U+eb0b
#define ICON_TI_REFRESH "\xee\xac\x93"	// U+eb13
#define ICON_TI_SEARCH "\xee\xac\x9c"	// U+eb1c
#define ICON_TI_SETTINGS "\xee\xac\xa0"	// U+eb20
#define ICON_TI_SUN "\xee\xac\xb0"	// U+eb30
#define ICON_TI_TRASH "\xee\xad\x81"	// U+eb41
#define ICON_TI_X "\xee\xad\x95"	// U+eb55
#define ICON_TI_APERTURE "\xee\xad\x98"	// U+eb58
#define ICON_TI_DEVICE_FLOPPY "\xee\xad\xa2"	// U+eb62
#define ICON_TI_ARROW_BACK_UP "\xee\xad\xb7"	// U+eb77
#define ICON_TI_ARROW_FORWARD_UP "\xee\xad\xb8"	// U+eb78
#define ICON_TI_BRUSH "\xee\xae\xb8"	// U+ebb8
#define ICON_TI_TYPOGRAPHY "\xee\xaf\x85"	// U+ebc5
#define ICON_TI_FLASK "\xee\xaf\x92"	// U+ebd2
#define ICON_TI_SNOWFLAKE "\xee\xb0\x8b"	// U+ec0b
#define ICON_TI_ADJUSTMENTS_HORIZONTAL "\xee\xb0\xb8"	// U+ec38
#define ICON_TI_MENU_2 "\xee\xb1\x82"	// U+ec42
#define ICON_TI_CHART_BUBBLE "\xee\xb1\xb5"	// U+ec75
#define ICON_TI_WALK "\xee\xb2\x87"	// U+ec87
#define ICON_TI_PHOTO "\xee\xac\x8a"	// U+eb0a
#define ICON_TI_PIN "\xee\xb2\x9c"	// U+ec9c
#define ICON_TI_CLOUD_FOG "\xee\xb3\x99"	// U+ecd9
#define ICON_TI_MOON_2 "\xee\xb3\xa6"	// U+ece6
#define ICON_TI_PLAYER_PAUSE "\xee\xb5\x85"	// U+ed45
#define ICON_TI_PLAYER_PLAY "\xee\xb5\x86"	// U+ed46
#define ICON_TI_PINNED_OFF "\xee\xb5\x9f"	// U+ed5f
#define ICON_TI_FILE_TEXT "\xee\xaa\xa2"	// U+eaa2
#define ICON_TI_FILE_EXPORT "\xee\xb7\xa9"	// U+ede9
#define ICON_TI_STACK_2 "\xee\xbb\xb7"	// U+eef7
#define ICON_TI_SORT_ASCENDING_LETTERS "\xee\xbc\x98"	// U+ef18
#define ICON_TI_LAYOUT_SIDEBAR_LEFT_COLLAPSE "\xef\x80\x84"	// U+f004
#define ICON_TI_LAYOUT_SIDEBAR_LEFT_EXPAND "\xef\x80\x85"	// U+f005
#define ICON_TI_ROBOT "\xef\x80\x8b"	// U+f00b
#define ICON_TI_NORTH_STAR "\xef\x80\x94"	// U+f014
#define ICON_TI_PLUG_OFF "\xef\x86\x80"	// U+f180
#define ICON_TI_ARROWS_EXCHANGE "\xef\x87\xb4"	// U+f1f4
#define ICON_TI_CATEGORY "\xef\x87\xb6"	// U+f1f6
#define ICON_TI_SCAN_EYE "\xef\x87\xbf"	// U+f1ff
#define ICON_TI_LOADER_2 "\xef\x88\xa6"	// U+f226
#define ICON_TI_SUN_LOW "\xef\x88\xb7"	// U+f237
#define ICON_TI_TEXT_SIZE "\xef\x8a\xb1"	// U+f2b1
#define ICON_TI_CLOCK_PAUSE "\xef\x95\x88"	// U+f548
#define ICON_TI_SPARKLES "\xef\x9b\x97"	// U+f6d7
#define ICON_TI_SHARE_2 "\xef\x9e\x99"	// U+f799
#define ICON_TI_VOLCANO "\xef\x9e\x9c"	// U+f79c
#define ICON_TI_CLOUD_COG "\xef\xa1\x8f"	// U+f84f
#define ICON_TI_FOLDER_OPEN "\xef\xab\xb7"	// U+faf7
#define ICON_TI_RESTORE "\xef\xab\xbd"	// U+fafd
#define ICON_TI_LOCK_OPEN_2 "\xef\xba\xa8"	// U+fea8

// Inclusive [min,max] pairs for ImFontAtlas::AddFontFromFileTTF glyphRanges (0-terminated).
inline constexpr unsigned short ICON_TI_GLYPH_RANGES[] = {
	0xea06, 0xea06,
	0xea19, 0xea19,
	0xea3b, 0xea3b,
	0xea51, 0xea51,
	0xea59, 0xea59,
	0xea5e, 0xea5e,
	0xea5f, 0xea5f,
	0xea60, 0xea60,
	0xea61, 0xea61,
	0xea62, 0xea62,
	0xea6a, 0xea6a,
	0xea70, 0xea70,
	0xea72, 0xea72,
	0xea74, 0xea74,
	0xea76, 0xea76,
	0xea7a, 0xea7a,
	0xea95, 0xea95,
	0xea99, 0xea99,
	0xea9a, 0xea9a,
	0xeaa2, 0xeaa2,
	0xeabf, 0xeabf,
	0xeac1, 0xeac1,
	0xead4, 0xead4,
	0xeae2, 0xeae2,
	0xeae8, 0xeae8,
	0xeb01, 0xeb01,
	0xeb04, 0xeb04,
	0xeb0a, 0xeb0a,
	0xeb0b, 0xeb0b,
	0xeb13, 0xeb13,
	0xeb1c, 0xeb1c,
	0xeb20, 0xeb20,
	0xeb30, 0xeb30,
	0xeb41, 0xeb41,
	0xeb55, 0xeb55,
	0xeb58, 0xeb58,
	0xeb62, 0xeb62,
	0xeb77, 0xeb77,
	0xeb78, 0xeb78,
	0xebb8, 0xebb8,
	0xebc5, 0xebc5,
	0xebd2, 0xebd2,
	0xec0b, 0xec0b,
	0xec38, 0xec38,
	0xec42, 0xec42,
	0xec75, 0xec75,
	0xec87, 0xec87,
	0xec9c, 0xec9c,
	0xecd9, 0xecd9,
	0xece6, 0xece6,
	0xed45, 0xed45,
	0xed46, 0xed46,
	0xed5f, 0xed5f,
	0xede9, 0xede9,
	0xeef7, 0xeef7,
	0xef18, 0xef18,
	0xf004, 0xf004,
	0xf005, 0xf005,
	0xf00b, 0xf00b,
	0xf014, 0xf014,
	0xf180, 0xf180,
	0xf1f4, 0xf1f4,
	0xf1f6, 0xf1f6,
	0xf1ff, 0xf1ff,
	0xf226, 0xf226,
	0xf237, 0xf237,
	0xf2b1, 0xf2b1,
	0xf548, 0xf548,
	0xf6d7, 0xf6d7,
	0xf799, 0xf799,
	0xf79c, 0xf79c,
	0xf84f, 0xf84f,
	0xfaf7, 0xfaf7,
	0xfafd, 0xfafd,
	0xfea8, 0xfea8,
	0
};


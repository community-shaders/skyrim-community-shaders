// Curated Lucide macros - only glyphs referenced from src/.
// To add an icon: copy one #define (UTF-8 + U+ comment) from IconFontCppHeaders / webfont CSS,
// then add a singleton pair for that codepoint in ICON_LC_GLYPH_RANGES.

#pragma once

#define FONT_ICON_FILE_NAME_LC "lucide.ttf"

#define ICON_MIN_LC 0xe040
#define ICON_MAX_16_LC 0xe702
#define ICON_MAX_LC 0xe702

#define ICON_LC_APERTURE "\xee\x81\x80"	// U+e040
#define ICON_LC_CLOUD_LIGHTNING "\xee\x82\x8c"	// U+e08c
#define ICON_LC_SHARE "\xee\x85\x95"	// U+e155
#define ICON_LC_SNOWFLAKE "\xee\x85\xa5"	// U+e165
#define ICON_LC_CLOUD_FOG "\xee\x88\x94"	// U+e214
#define ICON_LC_PIN "\xee\x89\x99"	// U+e259
#define ICON_LC_SUN_DIM "\xee\x8a\x99"	// U+e299
#define ICON_LC_PIN_OFF "\xee\x8a\xb6"	// U+e2b6
#define ICON_LC_CLOUD_COG "\xee\x8c\x8a"	// U+e30a
#define ICON_LC_CASTLE "\xee\x8f\xa0"	// U+e3e0
#define ICON_LC_STARS "\xee\x90\x92"	// U+e412
#define ICON_LC_ARROW_DOWN_A_Z "\xee\x90\x95"	// U+e415
#define ICON_LC_SCAN_EYE "\xee\x94\xb6"	// U+e536
#define ICON_LC_ECLIPSE "\xee\x96\x9d"	// U+e59d
#define ICON_LC_BUBBLES "\xee\x99\x94"	// U+e654
#define ICON_LC_LIST_SORT_ASCENDING "\xee\x9c\x82"	// U+e702

// Inclusive [min,max] pairs for ImFontAtlas::AddFontFromFileTTF glyphRanges (0-terminated).
inline constexpr unsigned short ICON_LC_GLYPH_RANGES[] = {
	0xe040, 0xe040,
	0xe08c, 0xe08c,
	0xe155, 0xe155,
	0xe165, 0xe165,
	0xe214, 0xe214,
	0xe259, 0xe259,
	0xe299, 0xe299,
	0xe2b6, 0xe2b6,
	0xe30a, 0xe30a,
	0xe3e0, 0xe3e0,
	0xe412, 0xe412,
	0xe415, 0xe415,
	0xe536, 0xe536,
	0xe59d, 0xe59d,
	0xe654, 0xe654,
	0xe702, 0xe702,
	0
};

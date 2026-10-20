// Curated Font Awesome 5 macros - only glyphs referenced from src/.
// To add an icon: copy one #define (UTF-8 + U+ comment) from IconFontCppHeaders / webfont CSS,
// then add a singleton pair for that codepoint in ICON_FA_GLYPH_RANGES.

#pragma once

#define FONT_ICON_FILE_NAME_FAS "fa-solid-900.ttf"

#define ICON_MIN_FA 0xe065
#define ICON_MAX_16_FA 0xf84c
#define ICON_MAX_FA 0xf84c

#define ICON_FA_HOUSE_USER "\xee\x81\xa5"	// U+e065
#define ICON_FA_SAILBOAT "\xee\x91\x85"	// U+e445
#define ICON_FA_HANDCUFFS "\xee\x93\xb8"	// U+e4f8
#define ICON_FA_HOME "\xef\x80\x95"	// U+f015
#define ICON_FA_FLAG "\xef\x80\xa4"	// U+f024
#define ICON_FA_TAG "\xef\x80\xab"	// U+f02b
#define ICON_FA_LEAF "\xef\x81\xac"	// U+f06c
#define ICON_FA_USERS "\xef\x83\x80"	// U+f0c0
#define ICON_FA_SQUARE "\xef\x83\x88"	// U+f0c8
#define ICON_FA_UNIVERSITY "\xef\x86\x9c"	// U+f19c
#define ICON_FA_PAW "\xef\x86\xb0"	// U+f1b0
#define ICON_FA_TREE "\xef\x86\xbb"	// U+f1bb
#define ICON_FA_BED "\xef\x88\xb6"	// U+f236
#define ICON_FA_MAP "\xef\x89\xb9"	// U+f279
#define ICON_FA_MAP_MARKER_ALT "\xef\x8f\x85"	// U+f3c5
#define ICON_FA_SHIELD_ALT "\xef\x8f\xad"	// U+f3ed
#define ICON_FA_CHESS_ROOK "\xef\x91\x87"	// U+f447
#define ICON_FA_CROW "\xef\x94\xa0"	// U+f520
#define ICON_FA_CROWN "\xef\x94\xa1"	// U+f521
#define ICON_FA_ROBOT "\xef\x95\x84"	// U+f544
#define ICON_FA_SKULL "\xef\x95\x8c"	// U+f54c
#define ICON_FA_STORE "\xef\x95\x8e"	// U+f54e
#define ICON_FA_HOTEL "\xef\x96\x94"	// U+f594
#define ICON_FA_LANDMARK "\xef\x99\xaf"	// U+f66f
#define ICON_FA_PLACE_OF_WORSHIP "\xef\x99\xbf"	// U+f67f
#define ICON_FA_CAMPGROUND "\xef\x9a\xbb"	// U+f6bb
#define ICON_FA_DRAGON "\xef\x9b\x95"	// U+f6d5
#define ICON_FA_DUNGEON "\xef\x9b\x99"	// U+f6d9
#define ICON_FA_HAT_WIZARD "\xef\x9b\xa8"	// U+f6e8
#define ICON_FA_MOUNTAIN "\xef\x9b\xbc"	// U+f6fc
#define ICON_FA_SKULL_CROSSBONES "\xef\x9c\x94"	// U+f714
#define ICON_FA_CARROT "\xef\x9e\x87"	// U+f787
#define ICON_FA_GLOBE_EUROPE "\xef\x9e\xa2"	// U+f7a2
#define ICON_FA_HARD_HAT "\xef\xa0\x87"	// U+f807
#define ICON_FA_BORDER_ALL "\xef\xa1\x8c"	// U+f84c

// Inclusive [min,max] pairs for ImFontAtlas::AddFontFromFileTTF glyphRanges (0-terminated).
inline constexpr unsigned short ICON_FA_GLYPH_RANGES[] = {
	0xe065, 0xe065,
	0xe445, 0xe445,
	0xe4f8, 0xe4f8,
	0xf015, 0xf015,
	0xf024, 0xf024,
	0xf02b, 0xf02b,
	0xf06c, 0xf06c,
	0xf0c0, 0xf0c0,
	0xf0c8, 0xf0c8,
	0xf19c, 0xf19c,
	0xf1b0, 0xf1b0,
	0xf1bb, 0xf1bb,
	0xf236, 0xf236,
	0xf279, 0xf279,
	0xf3c5, 0xf3c5,
	0xf3ed, 0xf3ed,
	0xf447, 0xf447,
	0xf520, 0xf520,
	0xf521, 0xf521,
	0xf544, 0xf544,
	0xf54c, 0xf54c,
	0xf54e, 0xf54e,
	0xf594, 0xf594,
	0xf66f, 0xf66f,
	0xf67f, 0xf67f,
	0xf6bb, 0xf6bb,
	0xf6d5, 0xf6d5,
	0xf6d9, 0xf6d9,
	0xf6e8, 0xf6e8,
	0xf6fc, 0xf6fc,
	0xf714, 0xf714,
	0xf787, 0xf787,
	0xf7a2, 0xf7a2,
	0xf807, 0xf807,
	0xf84c, 0xf84c,
	0
};


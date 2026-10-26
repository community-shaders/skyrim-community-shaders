// Curated Game Icons macros - only glyphs referenced from src/.
// To add an icon: copy one #define (UTF-8 + U+ comment) from IconFontCppHeaders / webfont CSS,
// then add a singleton pair for that codepoint in ICON_GI_GLYPH_RANGES.

#pragma once

#define FONT_ICON_FILE_NAME_GI "game-icons.ttf"

#define ICON_MIN_GI 0xe2e3
#define ICON_MAX_16_GI 0xeed3
#define ICON_MAX_GI 0xeed3

#define ICON_GI_CAVE_ENTRANCE "\xee\x8b\xa3"	// U+e2e3
#define ICON_GI_ELF_EAR "\xee\x93\x96"	// U+e4d6
#define ICON_GI_FANGS "\xee\x94\xb7"	// U+e537
#define ICON_GI_SHIP_WRECK "\xee\xae\x9c"	// U+eb9c
#define ICON_GI_TOMBSTONE "\xee\xb6\xa8"	// U+eda8
#define ICON_GI_VILLAGE "\xee\xb9\x9d"	// U+ee5d
#define ICON_GI_WOLF_HOWL "\xee\xbb\x93"	// U+eed3

// Inclusive [min,max] pairs for ImFontAtlas::AddFontFromFileTTF glyphRanges (0-terminated).
inline constexpr unsigned short ICON_GI_GLYPH_RANGES[] = {
	0xe2e3, 0xe2e3,
	0xe4d6, 0xe4d6,
	0xe537, 0xe537,
	0xeb9c, 0xeb9c,
	0xeda8, 0xeda8,
	0xee5d, 0xee5d,
	0xeed3, 0xeed3,
	0
};


#pragma once

#include <d3d11.h>
#include <filesystem>
#include <imgui.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <winrt/base.h>

using json = nlohmann::json;

/**
 * @brief Manages hot-swappable theme system for Community Shaders menu
 *
 * THEME JSON SCHEMA:
 * ==================
 * Theme files use JSON format with the following structure:
 *
 * {
 *   "DisplayName": "Human-readable theme name",
 *   "Description": "Theme description",
 *   "Version": "1.0.0",
 *   "Author": "Your name",
 *   "Theme": {
 *     "FontSize": 27.0,                    // Base font size (16-108px range)
 *     "FontName": "Jost/Jost-Regular.ttf", // Legacy font path (use FontRoles instead)
 *     "GlobalScale": 0.0,                   // UI scale exponent (-2.0 to 2.0, 0.0=100%)
 *
 *     // Font Role System (4 roles: Body, Heading, Subheading, Subtitle)
 *     "FontRoles": [
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 }
 *     ],
 *
 *     "TooltipHoverDelay": 0.5,            // Seconds before tooltip appears
 *     "ShowActionIcons": true,             // Obsolete: still read, ignored (header icons always show)
 *     "UseCustomCursor": false,
 *     "Cursor": {
 *       "Scale": 1.0,
 *       "Types": {
 *         "Arrow": { "File": "cursor.png", "HotspotX": 0, "HotspotY": 0 },
 *         "TextInput": { "File": "cursor_text.png", "HotspotX": 8, "HotspotY": 12 }
 *       }
 *     },
 *
 *     // Simple color palette (6 key colors)
 *     "Palette": {
 *       "Background": [0.03, 0.03, 0.03, 0.39],  // Window background RGBA
 *       "Text": [1.0, 1.0, 1.0, 1.0],            // Primary text color
 *       "WindowBorder": [0.5, 0.5, 0.5, 0.8],    // Outer window borders
 *       "FrameBorder": [0.4, 0.4, 0.4, 0.7],     // Button/input borders
 *       "Separator": [0.5, 0.5, 0.5, 0.6],       // Divider lines
 *       "ResizeGrip": [0.6, 0.6, 0.6, 0.8]       // Window resize handle
 *     },
 *
 *     // Status indicator colors
 *     "StatusPalette": {
 *       "Disable": [0.5, 0.5, 0.5, 1.0],         // Disabled elements
 *       "Error": [1.0, 0.4, 0.4, 1.0],           // Error messages
 *       "Warning": [1.0, 0.6, 0.2, 1.0],         // Warning messages
 *       "RestartNeeded": [0.45, 0.78, 0.48, 1.0], // Restart required indicator
 *       "CurrentHotkey": [1.0, 1.0, 0.0, 1.0],   // Active hotkey highlight
 *       "SuccessColor": [0.32, 0.68, 0.42, 1.0], // Success / positive (muted for text & button contrast)
 *       "InfoColor": [0.2, 0.6, 1.0, 1.0]        // Info messages
 *     },
 *
 *     // Feature header styling
 *     "FeatureHeading": {
 *       "ColorDefault": [0.8, 0.8, 0.8, 1.0],    // Default header color
 *       "ColorHovered": [0.6, 0.6, 0.6, 1.0],    // Hovered header color
 *       "MinimizedFactor": 0.7                    // Alpha multiplier when minimized
 *     },
 *
 *     // Scrollbar transparency
 *     "ScrollbarOpacity": {
 *       "Background": 0.0,                        // Scrollbar track alpha
 *       "Thumb": 0.5,                             // Scroll handle alpha
 *       "ThumbHovered": 0.75,                     // Hovered handle alpha
 *       "ThumbActive": 0.9                        // Dragged handle alpha
 *     },
 *
 *     // ImGui style settings (spacing, rounding, etc.)
 *     "Style": {
 *       "WindowBorderSize": 2.0,
 *       "WindowPadding": [8.0, 8.0],
 *       "WindowRounding": 12.0,
 *       "FrameRounding": 4.0,
 *       // ... see ImGuiStyle for all available fields
 *     },
 *
 *     // Named color map (resilient to ImGui enum changes)
 *     "Colors": { "Text": [r,g,b,a], "WindowBg": [r,g,b,a], ... }
 *     // Legacy: positional array (auto-migrated on load)
 *     "FullPalette": [ [r,g,b,a], [r,g,b,a], ... ]
 *   }
 * }
 *
 * FONT ROLE SYSTEM:
 * =================
 * Replaces legacy single-font system with semantic font roles:
 * - Role 0 (Body):       Default UI text, settings labels
 * - Role 1 (Heading):    Feature section headers
 * - Role 2 (Subheading): Subsection headers
 * - Role 3 (Subtitle):   Secondary text, descriptions
 *
 * Each role can have different font family, style, and size scale.
 * UI fonts live under Data\Interface\CommunityShaders\Fonts\
 * Icon fonts (FA / Tabler / Game Icons) are standalone ImFonts under Icons\glyphs\
 * (see Icons::FA / LC / TI helpers); they are not merged into UI role fonts.
 *
 * BLUR SHADER SYSTEM:
 * ===================
 * Separable Gaussian blur (horizontal + vertical passes) rendered at eighth resolution.
 * Hardcoded intensity (0.04) for consistent appearance. Toggle via BackgroundBlurEnabled.
 * Based on Unrimp rendering engine: https://github.com/cofenberg/unrimp
 *
 * MIGRATION FROM OLD CONFIGS:
 * ===========================
 * Legacy "FontName" field still supported for backward compatibility.
 * New themes should use "FontRoles" array instead.
 * Old themes without FontRoles auto-populate with defaults on load.
 *
 * Theme files location: Data\SKSE\Plugins\CommunityShaders\Themes\
 * File naming: {ThemeName}.json (e.g., "Default.json", "DragonBlood.json")
 */
class ThemeManager
{
public:
	struct ThemeInfo
	{
		std::string name;         // Filename without extension
		std::string displayName;  // Human-readable name from JSON
		std::string description;  // Theme description from JSON
		std::string filePath;     // Full path to theme file
		json themeData;           // Complete theme settings
		bool isValid = false;     // Whether theme loaded successfully

		// Metadata
		std::string version;
		std::string author;
		std::time_t lastModified = 0;
	};

	// Returns the effective font size to use. If the user setting is <= 0, a dynamic
	// default based on current screen resolution is returned; otherwise the user value.
	static float ResolveFontSize(const class Menu& menu);

	// Static UI helper methods
	static void SetupImGuiStyle(const class Menu& menu);
	static void InitDefaultFontConfig(ImFontConfig& config);
	static bool ReloadFont(const class Menu& menu, float& cachedFontSize);
	static void ForceApplyDefaultTheme();  // Force Default.json colors to ImGui (bypass hardcoded defaults)

	struct Constants
	{
		// Font size constants
		static constexpr float DEFAULT_SCREEN_HEIGHT = 1080.0f;       // Default screen resolution to use for subsequent calculations
		static constexpr float DEFAULT_FONT_RATIO = (7.0f / 360.0f);  // 21px @ 1080p, 28px @ 1440p, 42px @ 4K
		static constexpr float MIN_FONT_SIZE = 16.0f;                 // ~1.5% @ 1080px height
		static constexpr float MAX_FONT_SIZE = 108.0f;                // 5.0% @ 2160px height
		static constexpr float DEFAULT_FONT_SIZE = 27.0f;

		// Global scale constants
		static constexpr float DEFAULT_GLOBAL_SCALE = 0.0f;  // Default global scale for built-in themes

		// Font configuration constants
		static constexpr int FCONF_OVERSAMPLE_H = 3;              // ImGui default = 2
		static constexpr int FCONF_OVERSAMPLE_V = 2;              // ImGui default = 1
		static constexpr bool FCONF_PIXELSNAP_H = true;           // ImGui default = false
		static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f;  // ImGui default = 1.0f

		// Header rendering constants
		static constexpr float HEADER_BASE_TEXT_SCALE = 1.7f;
		static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.85f;
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.5f;
		static constexpr float DOCKED_ICON_SPACING = 8.0f;
		static constexpr float DOCKED_RIGHT_MARGIN = 45.0f;
		static constexpr float WATERMARK_HEIGHT_PERCENT = 0.50f;
		static constexpr float UNDOCKED_ICON_PADDING_REDUCTION = 4.0f;
		static constexpr float DOCKED_ICON_PADDING_REDUCTION = 2.0f;
		static constexpr float HEADER_GLYPH_ICON_SCALE = 0.85f;      // Glyph size relative to the icon box, for actions drawn from the icon font
		static constexpr float STATUS_BAR_SEGMENT_GAP_SCALE = 3.0f;  // Gap between status bar parts, in item spacings

		// Menu UX: header Home button, status bar, Home card rhythm
		static constexpr float HEADER_ICON_SCALE = 0.92f;                              // Shrinks every header icon button (actions, Home, issues, Go to) alike; the logo keeps its size
		static constexpr float ICON_MIP_SELECT_BIAS = 0.3f;                            // Added to log2(downscale) when picking an icon mip: higher keeps a larger, crisper level
		static constexpr float HEADER_UNSAVED_BOX_FILL_ALPHA = 0.22f;                  // Fill of the box behind the save icon while changes are unsaved
		static constexpr float HEADER_UNSAVED_BOX_BORDER_ALPHA = 0.55f;                // Its border
		static constexpr float WATERMARK_MONO_ALPHA = 0.18f;                           // Monochrome watermark logo opacity
		static constexpr ImU32 HEADER_COLOR_ICON_IDLE = IM_COL32(220, 220, 220, 220);  // Docked colour icon tint while not hovered
		static constexpr ImU32 WATERMARK_COLOR = IM_COL32(255, 255, 255, 45);          // Full-colour watermark logo tint
		static constexpr float HEADER_ICON_IDLE_ALPHA = 0.85f;                         // Header icon opacity while neither hovered nor current (docked monochrome icons, Home button)
		static constexpr float STATUS_BAR_NAV_BUTTON_FONT_RATIO = 1.8f;                // Settings / Profiling button edge in the status bar, in font sizes
		static constexpr float STATUS_BAR_DIVIDER_FONT_RATIO = 1.0f;                   // Height of the thin dividers between status bar groups, in font sizes
		static constexpr float HOME_CARD_PADDING = 12.0f;                              // Inner padding on every side of a Home card (1080p baseline)
		static constexpr float HOME_CARD_SPACING = 8.0f;                               // Vertical gap below each Home card (1080p baseline)
		static constexpr float ATTENTION_FOCUS_SECONDS = 2.5f;                         // How long the Needs Attention card stays outlined after a click-through
		static constexpr float ATTENTION_FOCUS_FADE_SECONDS = 0.8f;                    // Outline fade-out time at the end of that window
		static constexpr float ATTENTION_FOCUS_THICKNESS = 2.0f;                       // Outline thickness (1080p baseline)
		static constexpr float HOME_CARD_ICON_BOX_SCALE = 1.2f;                        // Card icon button glyph box, in font sizes
		static constexpr float UNDO_TOAST_SECONDS = 2.0f;                              // How long the main menu's undo / redo toast stays

		// UI Layout constants
		static constexpr float BUTTON_SPACING = 8.0f;
		static constexpr float OVERLAY_WINDOW_POSITION = 10.0f;
		static constexpr float FONT_CACHE_EPSILON = 0.01f;
		static constexpr float CURSOR_POSITION_PADDING = 14.0f;
		static constexpr float SEPARATOR_THICKNESS = 3.0f;
		static constexpr float UNDOCKED_ICON_ITEM_SPACING = 6.0f;
		static constexpr float POPUP_BUTTON_WIDTH = 180.0f;
		static constexpr float EDITOR_VIEWPORT_BACKGROUND_DIM_ALPHA = 0.35f;  // Extra backdrop dim while the CS Editor viewport is open
		static constexpr float UI_FADE_SECONDS = 0.15f;                       // Time for a menu or editor widgets fade to run end to end

		// Feature header constants
		static constexpr float DEFAULT_FEATURE_TITLE_SCALE = 1.5f;         // Default scale for feature title text
		static constexpr float OPTION_CARD_TITLE_SCALE = 1.25f;            // Option card titles, on the Heading font role
		static constexpr float COLORS_PAGE_CELL_WIDTH_FONT_SIZES = 14.0f;  // Narrowest colour cell on the Colours page, in font sizes
		static constexpr float FONTS_PAGE_ROLE_LIST_FONT_SIZES = 12.0f;    // Width of the Fonts page's role list, in font sizes
		static constexpr float NAV_PILL_MIN_CONTENT_SCALE = 0.7f;          // Smallest a narrow nav pill shrinks its icon and label; below it the label hides
		static constexpr float VERSION_TEXT_OPACITY = 0.6f;                // Opacity for version text next to feature title
		static constexpr float FEATURE_DESCRIPTION_OPACITY = 0.7f;         // Opacity for the description under a feature title

		// Main menu feature list pane (sizes are 1080p baseline, scaled by Util::GetUIScale())
		static constexpr float NAV_ROW_ICON_GAP = 8.0f;                  // Gap between a page row's icon and its label
		static constexpr float CATEGORY_HEADER_PAD = 6.0f;               // Category header: space above and below the label row
		static constexpr float CATEGORY_HEADER_COLLAPSED_ALPHA = 0.7f;   // ... label alpha while the category is collapsed
		static constexpr float CATEGORY_HEADER_COUNT_ALPHA = 0.6f;       // ... count alpha relative to the label
		static constexpr float CATEGORY_HEADER_LINE_ALPHA = 0.5f;        // ... trailing rule alpha relative to the separator colour
		static constexpr float SEGMENT_TRACK_PAD = 3.0f;                 // Page switcher: inset between the track and its pills
		static constexpr float SEGMENT_PILL_GAP_RATIO = 2.0f;            // Gap between neighbouring pills, in track pads, so their fills never touch
		static constexpr float LEFT_PANE_EDGE_INSET = 3.0f;              // Room kept around the left pane's content so its edges and borders are not clipped (1080p baseline)
		static constexpr float LIST_HIGHLIGHT_HOVER_RATIO = 0.7f;        // Hover look of the list highlight, as a share of its selected strength
		static constexpr float LIST_HIGHLIGHT_SELECTED_DARKEN = 0.8f;    // Colour multiplier of the highlight once it rests on the selected row
		static constexpr float LIST_HIGHLIGHT_PRESS_DARKEN = 0.85f;      // Extra colour multiplier while the row is pressed
		static constexpr float LIST_HIGHLIGHT_PRESS_BOOST = 1.5f;        // Strength multiplier while pressed
		static constexpr float LIST_HIGHLIGHT_SNAP_ROWS = 6.0f;          // A move of more rows than this appears at the target instead of gliding
		static constexpr float SEGMENT_SELECTED_FILL_ALPHA = 0.22f;      // ... selected pill accent fill alpha (page switcher and palette scope pill)
		static constexpr float SEGMENT_SELECTED_OUTLINE_ALPHA = 0.7f;    // ... selected pill accent outline alpha
		static constexpr float SORT_FILTER_DOT_RATIO = 0.12f;            // Radius of the sort button's active-filter dot relative to the button height
		static constexpr float AUTOHIDE_PANEL_WIDTH_RATIO = 0.2f;        // Auto-hide list width as a share of the pane (the pinned list's column ratio)
		static constexpr float AUTOHIDE_PANEL_MIN_WIDTH = 240.0f;        // ... never narrower than this
		static constexpr float AUTOHIDE_ACTIVATION_ZONE_WIDTH = 28.0f;   // Width of the left edge strip that reveals the auto-hide list
		static constexpr float AUTOHIDE_OPEN_DELAY = 0.12f;              // Seconds the cursor must rest on the edge before the list opens
		static constexpr float AUTOHIDE_CLOSE_DELAY = 0.4f;              // Seconds the cursor must be away before the list closes
		static constexpr float AUTOHIDE_ANIM_SPEED = 16.0f;              // Slide smoothing rate per second (higher is snappier)
		static constexpr float AUTOHIDE_GRIP_WIDTH = 3.0f;               // Width of the grip drawn on the edge while the list is hidden
		static constexpr float AUTOHIDE_GRIP_ALPHA = 0.5f;               // Grip opacity as a share of the secondary text colour
		static constexpr float AUTOHIDE_EDGE_SLOP = 2.0f;                // Auto-hide edge zone also catches a cursor this far outside the pane
		static constexpr float FEATURE_ROW_CATEGORY_ICON_SCALE = 0.95f;  // Category icon edge in the Alphabetical list, relative to the line height
		static constexpr float FEATURE_ROW_CATEGORY_ICON_ALPHA = 0.55f;  // ... and its dimming
		static constexpr float FEATURE_ROW_PIN_SCALE = 0.75f;            // Pin glyph beside a pinned feature's name, as a share of the font size

		// Settings page constants (sizes are 1080p baseline, scaled by Util::GetUIScale())
		static constexpr float SETTINGS_NAV_GROUP_SPACING = 6.0f;  // Gap above each group label after the first
		static constexpr float SETTINGS_TITLE_SCALE_MIN = 1.0f;    // Section title scale range, as on feature pages
		static constexpr float SETTINGS_TITLE_SCALE_MAX = 3.0f;

		// "Go to..." palette and header search pill (sizes are 1080p baseline, scaled by Util::GetUIScale())
		static constexpr float PALETTE_WIDTH_RATIO = 0.42f;         // Palette width as a share of the viewport
		static constexpr float PALETTE_HEIGHT_RATIO = 0.6f;         // Palette height as a share of the viewport
		static constexpr size_t PALETTE_MAX_RESULTS = 200;          // Results listed for a query or scope
		static constexpr size_t PALETTE_RECENT_COUNT = 8;           // Recent picks remembered for the empty query
		static constexpr float PALETTE_BADGE_PAD_X = 6.0f;          // Horizontal padding inside a type badge
		static constexpr float PALETTE_BADGE_ROUNDING = 4.0f;       // Type badge corner radius
		static constexpr float PALETTE_BADGE_FILL_ALPHA = 0.22f;    // Type badge fill opacity
		static constexpr float PALETTE_ROW_PAD_Y = 4.0f;            // Vertical padding of a result row
		static constexpr float PALETTE_TOAST_SECONDS = 4.0f;        // How long the "Look under" toast stays
		static constexpr float PALETTE_MIN_FILL_ALPHA = 0.97f;      // Palette panel fill opacity floor, so the menu behind never shows through
		static constexpr size_t PALETTE_GROUP_MAX = 12;             // Results per kind on the All scope; the rest wait behind the kind's scope
		static constexpr float PALETTE_GAP = 8.0f;                  // Space between the palette's field, scope row, list and footer
		static constexpr float PALETTE_FIELD_HEIGHT_RATIO = 1.6f;   // Search field height as a multiple of a frame height
		static constexpr float PALETTE_FIELD_ROUNDING = 10.0f;      // Search field corner radius
		static constexpr float PALETTE_FIELD_PAD_X = 12.0f;         // Space left of the search glyph
		static constexpr float PALETTE_FIELD_INSET = 6.0f;          // Space around the sort button inside the field
		static constexpr float PALETTE_FIELD_BORDER_ALPHA = 0.55f;  // Accent border opacity of the focused search field
		static constexpr float PALETTE_SCOPE_PAD = 3.0f;            // Padding of the scope track around its pill
		static constexpr float PALETTE_SCOPE_ITEM_PAD_X = 12.0f;    // Horizontal padding of a scope's label
		static constexpr float PALETTE_SCOPE_COUNT_PAD_X = 5.0f;    // Horizontal padding inside a scope's count chip
		static constexpr float PALETTE_SCOPE_ROUNDING = 8.0f;       // Scope track and pill corner radius
		static constexpr float PALETTE_PILL_RATE = 22.0f;           // How fast the scope pill glides to the chosen scope, per second
		static constexpr float PALETTE_PILL_FILL_ALPHA = 0.22f;     // Scope pill accent fill opacity
		static constexpr float PALETTE_PILL_BORDER_ALPHA = 0.55f;   // Scope pill accent border opacity
		static constexpr float PALETTE_COUNT_FILL_ALPHA = 0.12f;    // Count chip fill opacity of an unchosen scope
		static constexpr float PALETTE_ROW_ROUNDING = 8.0f;         // Result row highlight corner radius
		static constexpr float PALETTE_ROW_RATE = 26.0f;            // How fast the row highlight glides between rows, per second
		static constexpr float PALETTE_SELECT_FILL_ALPHA = 0.14f;   // Selected row accent fill opacity
		static constexpr float PALETTE_HOVER_FILL_ALPHA = 0.06f;    // Hovered row fill opacity
		static constexpr float PALETTE_HEADER_PAD_Y = 6.0f;         // Vertical padding of a group header
		static constexpr float PALETTE_KEYCAP_PAD_X = 6.0f;         // Horizontal padding inside a keycap
		static constexpr float PALETTE_KEYCAP_ROUNDING = 4.0f;      // Keycap corner radius
		static constexpr float PALETTE_KEYCAP_FILL_ALPHA = 0.10f;   // Keycap fill opacity
		static constexpr float PALETTE_KEYCAP_EDGE_ALPHA = 0.18f;   // Opacity of a keycap's lower edge
		static constexpr float PALETTE_EMPTY_ICON_RATIO = 1.5f;     // Radius of the empty-state disc as a multiple of the line height
		static constexpr float PALETTE_EMPTY_DISC_ALPHA = 0.10f;    // Empty-state disc opacity
		static constexpr float PALETTE_DIMMED_ALPHA = 0.5f;         // Opacity multiplier for rows of features that are not loaded
		static constexpr float PALETTE_EMPTY_DISC_Y = 0.3f;         // Empty-state disc centre as a share of the list height
		static constexpr float PALETTE_SEARCH_GLYPH_RATIO = 1.5f;   // Search glyph slot width as a multiple of the line height
		static constexpr float PALETTE_SCOPE_HOVER_LABEL = 0.6f;    // How far a hovered scope label brightens toward the chosen one
		static constexpr float PALETTE_SELECT_BAR_INSET = 0.2f;     // Selected-row accent bar inset from the row's top and bottom, as a share of its height
		static constexpr float REVEAL_HIGHLIGHT_SECONDS = 1.0f;     // How long a revealed setting stays outlined
		static constexpr float REVEAL_EXPIRY_SECONDS = 3.0f;        // A reveal request older than this is dropped
		static constexpr float REVEAL_SCROLL_RATIO = 0.3f;          // Where in the view a revealed control lands (0 top, 1 bottom)
		static constexpr float REVEAL_OUTLINE_THICKNESS = 2.0f;     // Reveal outline stroke
		static constexpr float REVEAL_FADE_HOLD = 1.5f;             // Reveal outline stays at full strength for the first third of the highlight
		static constexpr float REVEAL_OUTLINE_PADDING = 3.0f;       // Reveal outline margin around the control

		// Home dashboard (sizes are 1080p baseline, scaled by Util::GetUIScale())
		static constexpr float HOME_TWO_COLUMN_MIN_CHARS = 60.0f;                           // Content width, in average character widths, for two card columns
		static constexpr float HOME_CARD_ACTION_GAP = 4.0f;                                 // Extra gap above a card's action buttons
		static constexpr float HOME_PRESET_THUMB_SIZE = 64.0f;                              // Active preset thumbnail edge
		static constexpr float HOME_DISCORD_BANNER_MAX_HEIGHT = 110.0f;                     // Discord banner height cap; its width follows the aspect ratio
		static constexpr float ISSUES_REFRESH_SECONDS = 0.5f;                               // How often the Issues nav entry and Home recount active constraints
		static constexpr float HOME_STRIP_FIRST_FRAME_FRAMES = 4.0f;                        // Bottom strip height guess for the first frame, in frame heights
		static constexpr ImVec4 HOME_DISCORD_BANNER_HOVER_COLOR{ 0.1f, 0.1f, 0.1f, 0.3f };  // Subtle hover behind the transparent banner button
		static constexpr ImVec4 HOME_DISCORD_BANNER_ACTIVE_COLOR{ 0.2f, 0.2f, 0.2f, 0.5f };
		static constexpr float HOME_SETUP_WATERMARK_MONO_ALPHA = 0.24f;                   // First-time setup dialog logo watermark opacity, monochrome logo
		static constexpr ImU32 HOME_SETUP_WATERMARK_COLOR = IM_COL32(255, 255, 255, 60);  // ... and its tint, full-colour logo

		static constexpr float SCENE_INDICATOR_RADIUS_RATIO = 0.25f;  // Scene indicator dot radius relative to the text line height

		// Icon drawing constants
		static constexpr int ICON_CIRCLE_SEGMENTS = 16;        // Segments for small icon circles
		static constexpr float ICON_OUTLINE_THICKNESS = 1.5f;  // Outline stroke for hollow icons, before UI scale

		// Search input constants
		static constexpr float SEARCH_BASELINE_SCREEN_HEIGHT = 1440.0f;  // Search chrome is authored for 2K.
		static constexpr float SEARCH_ICON_SIZE = 20.0f;                 // Default search icon size
		static constexpr float SEARCH_ICON_ALPHA = 0.7f;                 // Default search icon opacity
		static constexpr float SEARCH_ICON_OFFSET_X = 8.0f;              // Search icon offset from input edge
		static constexpr float SEARCH_INPUT_PADDING_EXTRA = 14.0f;       // Extra input padding after icon
		static constexpr float SEARCH_INPUT_FRAME_PADDING_Y = 6.0f;      // Search input vertical padding
		static constexpr float COMBO_SEARCH_ICON_ALPHA = 0.5f;           // Icon alpha for subtle appearance
		static constexpr float COMBO_SEARCH_ICON_OFFSET_X = 5.0f;        // Icon horizontal offset from input edge

		// Window overlap readability constants
		static constexpr float OVERLAP_MIN_ALPHA = 0.85f;       // Background alpha when windows overlap
		static constexpr float OVERLAP_FADEIN_SPEED = 8.0f;     // Fade-in speed (units/sec)
		static constexpr float OVERLAP_FADEOUT_SPEED = 4.0f;    // Fade-out speed (units/sec)
		static constexpr float OVERLAP_ALPHA_EPSILON = 0.005f;  // Below this alpha is clamped to zero

		// Toast notification cards (Util::Notifications); sizes are 1080p baseline, scaled by the UI scale
		static constexpr float TOAST_MAX_TEXT_WIDTH_FONTS = 48.0f;          // Text wrap width, in font sizes, so it scales with the font and UI scale
		static constexpr float TOAST_MAX_TEXT_WIDTH_VIEWPORT_RATIO = 0.8f;  // Wrap width cap as a share of the viewport width
		static constexpr float TOAST_PADDING_X = 16.0f;                     // Horizontal text padding inside the card
		static constexpr float TOAST_PADDING_Y = 12.0f;                     // Vertical text padding inside the card
		static constexpr float TOAST_BOTTOM_MARGIN = 32.0f;                 // Gap between the lowest card and the viewport bottom
		static constexpr float TOAST_STACK_SPACING = 8.0f;                  // Gap between stacked cards
		static constexpr float TOAST_FADE_IN_SECONDS = 0.18f;               // Fade-in time after a card appears
		static constexpr float TOAST_FADE_OUT_SECONDS = 0.55f;              // Fade-out time before a card expires
		static constexpr float TOAST_ROUNDING_SCALE = 1.25f;                // Card corner radius relative to FrameRounding
		static constexpr float TOAST_ACCENT_ALPHA = 0.65f;                  // Alpha of the message-coloured border and stripe
		static constexpr float TOAST_BORDER_THICKNESS = 1.15f;              // Card outline thickness
		static constexpr float TOAST_STRIPE_WIDTH = 3.0f;                   // Accent stripe on the card's left edge

		// UI animation (Util::Anim): rates are per second for 1 - exp(-rate * dt) approach, durations in seconds
		static constexpr float ANIM_HOVER_RATE = 18.0f;                   // Hover / press colour blend speed
		static constexpr float ANIM_SELECT_RATE = 14.0f;                  // Selection highlight blend speed
		static constexpr float ANIM_EPSILON = 0.002f;                     // Blend within this of its target snaps to it
		static constexpr float ANIM_POPUP_SECONDS = 0.14f;                // Popup and palette fade / scale-in time
		static constexpr float ANIM_POPUP_MIN_VISIBILITY = 0.002f;        // Floor for a fading popup's alpha, so it is still submitted on the frame that closes it
		static constexpr float ANIM_POPUP_GHOST_MAX_GAP_SECONDS = 0.15f;  // A closed popup only fades out if it was open this recently, so hiding the whole menu leaves no ghost
		static constexpr float ANIM_POPUP_SCALE_FROM = 0.97f;             // Palette panel scale at the start of its fade-in
		static constexpr float ANIM_PAGE_ENTER_SECONDS = 0.18f;           // Page, feature page and Home card fade-in time
		static constexpr float ANIM_CARD_STAGGER_SECONDS = 0.04f;         // Delay added per Home card
		static constexpr float ANIM_PAGE_FADE_FROM = 0.4f;                // Page alpha at the start of that fade (never fully hidden, so there is no flash)
		static constexpr float ANIM_GLIDE_RATE = 26.0f;                   // How fast a selection highlight glides to the newly selected item (nav, palette rows), per second
		static constexpr float ANIM_SLIDER_RATE = 22.0f;                  // How fast a slider's grab glides to its value when not held, per second
		static constexpr float ANIM_SLIDER_DRAG_RATE = 50.0f;             // The same while the slider is held, so a drag still feels direct
		static constexpr float ANIM_TOGGLE_RATE = 14.0f;                  // How fast a toggle's knob slides, per second
		static constexpr float ANIM_CHECK_RATE = 50.0f;                   // How fast a check mark fades in and out, per second
		static constexpr float ANIM_CHECK_POP_FROM = 0.7f;                // Check mark size, as a share of full, at the start of its fade-in
		static constexpr float TOGGLE_TRACK_HEIGHT_RATIO = 0.62f;         // Toggle track thickness, as a share of the knob's diameter (Material: 16 of 21)
		static constexpr float TOGGLE_TRACK_INSET_RATIO = 0.35f;          // How far the track stops short of each end, in knob radii, so the knob overhangs it
		static constexpr float TOGGLE_TRACK_ON_ALPHA = 0.5f;              // Track strength while on: the dim accent of the Material style
		static constexpr float TOGGLE_KNOB_HOVER_LIFT = 0.2f;             // How far the on-knob lightens towards white while hovered
		static constexpr float TOGGLE_SHADOW_OFFSET_RATIO = 0.12f;        // Knob shadow drop, in knob radii
		static constexpr float TOGGLE_SHADOW_ALPHA = 0.3f;                // Knob shadow strength
		static constexpr float ANIM_SLIDER_SNAP_FRACTION = 0.0008f;       // A slider grab within this share of its track of the value snaps to it
		static constexpr float ANIM_SPEED_MIN = 0.5f;                     // Animation speed multiplier range (Settings > Behavior)
		static constexpr float ANIM_SPEED_MAX = 2.0f;
		static constexpr float ANIM_PULSE_PERIOD = 2.6f;        // Seconds per attention pulse
		static constexpr float ANIM_PULSE_AMPLITUDE = 0.25f;    // Attention pulse swing, as a share of full alpha
		static constexpr float NAV_SELECTED_BAR_INSET = 0.2f;   // Accent bar inset from the row's top and bottom, as a share of its height (nav and palette rows)
		static constexpr float ANIM_SELECTED_BAR_WIDTH = 3.0f;  // Accent bar on the left edge of a selected row: nav, palette, editor lists (1080p baseline)

		// Flat surfaces (Util::Surface): one subtle step above the window colour, a hairline border, one soft shadow when floating
		static constexpr float SURFACE_FILL_LIFT = 0.04f;          // Window colour lifted toward white so cards read above the page
		static constexpr float SURFACE_BORDER_ALPHA = 0.08f;       // Hairline border opacity (white)
		static constexpr float SURFACE_SHADOW_SPREAD = 6.0f;       // Floating surfaces' shadow growth (1080p baseline)
		static constexpr float SURFACE_SHADOW_ALPHA = 0.22f;       // Shadow opacity (black)
		static constexpr float SURFACE_SHADOW_OFFSET_X = 0.0f;     // Shadow sideways shift (1080p baseline)
		static constexpr float SURFACE_SHADOW_OFFSET_Y = 2.0f;     // Shadow drop (1080p baseline)
		static constexpr float SURFACE_SHADOW_OFFSET_MAX = 16.0f;  // Shadow offset range either way, each axis (1080p baseline)
		static constexpr float NAV_SELECTED_FILL_ALPHA = 0.14f;    // Accent fill behind a selected nav or palette row

		// Tinted control frame: marks where a value comes from (inherited, or a scene override's winning layer)
		static constexpr float TINTED_FRAME_BG_ALPHA = 0.25f;
		static constexpr float TINTED_FRAME_BG_HOVERED_ALPHA = 0.35f;
		static constexpr float TINTED_FRAME_BG_ACTIVE_ALPHA = 0.45f;
		static constexpr float TINTED_FRAME_BORDER_SIZE = 1.0f;

		// Status button brightness adjustment offsets. Bright colors are darkened by the same amounts for contrast.
		static constexpr float BUTTON_MIN_COLOR_CHANNEL = 0.0f;
		static constexpr float BUTTON_MAX_COLOR_CHANNEL = 1.0f;
		static constexpr float BUTTON_HOVER_BRIGHTEN = 0.2f;
		static constexpr float BUTTON_ACTIVE_BRIGHTEN = 0.3f;
		static constexpr float BUTTON_STATUS_TEXT_HOVER_ALPHA = 0.8f;
		static constexpr float BUTTON_STATUS_TEXT_ACTIVE_ALPHA = 1.0f;

		// Shared widgets: buttons and banners (Util::Button, Util::StatusBanner, lock and flash effects)
		static constexpr float BUTTON_STATE_HOVER_ALPHA = 0.4f;     // Hover fill of a button tinted by its state colour (unsaved save, lock, delete)
		static constexpr float BUTTON_STATE_PRESS_ALPHA = 0.6f;     // ... its press fill
		static constexpr float PILL_ROUNDING = 999.0f;              // Frame rounding that turns a button into a full pill
		static constexpr float STATUS_BANNER_FILL_ALPHA = 0.16f;    // Status banner fill, as a share of the status colour's alpha
		static constexpr float STATUS_BANNER_GAP = 8.0f;            // Gap between status banner icon and text (px at baseline scale)
		static constexpr float LOCK_BUTTON_FILL_ALPHA = 0.22f;      // Lock toggle button fill
		static constexpr float LOCK_BUTTON_BORDER_ALPHA = 0.55f;    // Lock toggle button border
		static constexpr float LOCK_BUTTON_ROUNDING_RATIO = 0.35f;  // Lock toggle button corner radius per button size
		static constexpr float UNSAVED_ALPHA = 0.75f;               // Alpha of the unsaved-changes colour
		static constexpr float FLASH_BRIGHTEN = 0.2f;               // Button brighten while a click flash plays
		static constexpr float FLASH_HOVER_SCALE = 1.1f;            // Flash colour scale when hovered
		static constexpr float FLASH_ACTIVE_SCALE = 0.9f;           // Flash colour scale when pressed

		// Shared widgets: feature toggle (Util::FeatureToggle; also the Load at Boot toggles)
		static constexpr ImVec4 FEATURE_TOGGLE_ACTIVE_BG = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);  // Active frame of the feature toggle row
		static constexpr float FEATURE_TOGGLE_WIDTH_RATIO = 1.6f;                           // Feature toggle width per frame height
		static constexpr float FEATURE_TOGGLE_HEIGHT_RATIO = 0.8f;                          // Feature toggle height per frame height

		// Shared widgets: tooltips, badges, section headers and editor chrome
		static constexpr float TOOLTIP_WRAP_FONT_SIZES = 35.0f;                     // Wrapped tooltip width in font sizes
		static constexpr ImVec4 BADGE_HDR_BG = ImVec4(0.40f, 0.58f, 0.78f, 0.30f);  // HDR badge background
		static constexpr ImVec4 BADGE_HDR_FG = ImVec4(0.72f, 0.86f, 1.00f, 0.95f);  // HDR badge text
		static constexpr ImVec4 BADGE_SDR_BG = ImVec4(0.78f, 0.42f, 0.42f, 0.30f);  // SDR badge background
		static constexpr ImVec4 BADGE_SDR_FG = ImVec4(1.00f, 0.72f, 0.72f, 0.95f);  // SDR badge text
		static constexpr float LOGO_TEXT_GAP = 8.0f;                                // Gap between a logo and the text beside it (px at baseline scale)
		static constexpr float EDITOR_CHROME_PAD_FACTOR = 4.0f;                     // Editor header height: frame paddings above and below plus outer air
		static constexpr float SECTION_ICON_SCALE = 1.2f;                           // Section header icon size per font size
		static constexpr float SECTION_ICON_SPACING_SCALE = 0.3f;                   // Section header icon spacing per font size
		static constexpr float SECTION_HEADER_GAP = 8.0f;                           // Gap below a centred section header (px at baseline scale)
		static constexpr float INVALID_INPUT_BORDER_SIZE = 2.0f;                    // Border width on an input with a validation error

		// Shared widgets: segmented control glass look (Util::SegmentedControl)
		static constexpr float GLASS_TRACK_IDLE_ALPHA = 0.07f;      // Segmented control track when the frame colour is transparent
		static constexpr float GLASS_TRACK_FRAME_MIX = 0.35f;       // Share of the frame colour mixed into the white track
		static constexpr float GLASS_TRACK_ALPHA_SCALE = 0.45f;     // Track alpha per unit of frame alpha
		static constexpr float GLASS_TRACK_ALPHA_MIN = 0.06f;       // Track alpha floor (also its additive base)
		static constexpr float GLASS_TRACK_ALPHA_MAX = 0.22f;       // Track alpha ceiling
		static constexpr float GLASS_SELECTED_ALPHA_FLOOR = 0.35f;  // Minimum header alpha before scaling the selected fill
		static constexpr float GLASS_SELECTED_ALPHA_SCALE = 0.85f;  // Selected fill alpha scale
		static constexpr float GLASS_SELECTED_ALPHA_MIN = 0.28f;    // Selected fill alpha floor
		static constexpr float GLASS_SELECTED_ALPHA_MAX = 0.55f;    // Selected fill alpha ceiling
		static constexpr float GLASS_HOVER_ALPHA_SCALE = 0.45f;     // Hover fill alpha relative to the selected fill
		static constexpr float GLASS_MUTED_SELECTED_SCALE = 0.4f;   // Selected fill alpha scale in muted mode
		static constexpr float GLASS_MUTED_TEXT_SCALE = 0.75f;      // Idle text alpha scale in muted mode
		static constexpr float GLASS_SEGMENT_MIN_PAD_X = 8.0f;      // Minimum horizontal segment padding (px at baseline scale)
		static constexpr float GLASS_SEGMENT_PAD_X_SCALE = 1.35f;   // Segment padding per unit of frame padding
		static constexpr float GLASS_MARK_MIN_RADIUS = 1.5f;        // Marked-segment dot minimum radius (px)
		static constexpr float GLASS_MARK_RADIUS_RATIO = 0.1f;      // Marked-segment dot radius per font size

		// Settings, Advanced and Presets pages (sizes are 1080p baseline unless noted)
		static constexpr float FONT_ROLE_SCALE_MIN = 0.5f;  // Per-role font scale slider range (Settings > Fonts)
		static constexpr float FONT_ROLE_SCALE_MAX = 2.5f;
		static constexpr float THEME_DESC_BOX_WIDTH = 400.0f;                 // Create-theme popup description box width, at 1080p
		static constexpr float THEME_DESC_BOX_HEIGHT = 80.0f;                 // Create-theme popup description box height, at 1080p
		static constexpr float SURFACE_PREVIEW_MARGIN = 14.0f;                // Margin around the surface preview sample, at 1080p
		static constexpr float SURFACE_PREVIEW_MAX_WIDTH_FONT_SIZES = 18.0f;  // Surface preview width cap, in font sizes
		static constexpr float DISABLE_AT_BOOT_MIN_SEARCH_FRAMES = 4.0f;      // Disable at Boot search field never narrower than this many frame heights
		static constexpr float BLOCKED_SHADER_BOX_ROUNDING = 8.0f;            // Corner radius of the blocked-shader info box, at 1080p
		static constexpr float BLOCKED_SHADER_BOX_BG_ALPHA = 0.15f;           // Error-tinted background opacity of that box
		static constexpr float BLOCKED_SHADER_BOX_MAX_HEIGHT_RATIO = 0.3f;    // Max box height as a share of the space left, keeping Active Shaders visible

		// Presets page
		static constexpr float PRESET_ROW_PADDING = 6.0f;                                      // Vertical padding inside a preset pack row, at 1080p
		static constexpr float PRESET_ROW_LOGO_SIZE = 40.0f;                                   // Preset pack logo edge, at 1080p
		static constexpr float PRESET_ROW_INSET = 8.0f;                                        // Horizontal inset of preset row content, at 1080p
		static constexpr float PRESET_ROW_STATUS_GAP = 6.0f;                                   // Gap before a preset row's status badge, at 1080p
		static constexpr float PRESET_TITLE_TOP_PAD = 2.0f;                                    // Space above the preset detail title, at 1080p
		static constexpr float PRESET_TITLE_FONT_SCALE = 1.35f;                                // Preset detail title size relative to the heading font
		static constexpr float PRESET_VIEWER_ARROW_SCALE = 1.35f;                              // Screenshot viewer arrow button size, in frame heights
		static constexpr float PRESET_VIEWER_MAX_SIZE_RATIO = 0.8f;                            // Screenshot viewer max size as a share of the viewport
		static constexpr float PRESET_LIST_WIDTH_RATIO = 0.34f;                                // Preset list column share of the page width
		static constexpr float PRESET_LIST_MIN_WIDTH = 260.0f;                                 // Preset list column minimum width, at 1080p
		static constexpr float PRESET_LIST_MAX_WIDTH = 380.0f;                                 // Preset list column maximum width, at 1080p
		static constexpr ImVec4 PRESET_VIEWER_BG_COLOR{ 0.02f, 0.02f, 0.04f, 0.72f };          // Screenshot viewer backdrop
		static constexpr ImVec4 PRESET_VIEWER_ARROW_COLOR{ 0.0f, 0.0f, 0.0f, 0.45f };          // Viewer arrow button fill
		static constexpr ImVec4 PRESET_VIEWER_ARROW_HOVER_COLOR{ 0.0f, 0.0f, 0.0f, 0.7f };     // Viewer arrow button hover
		static constexpr ImVec4 PRESET_VIEWER_ARROW_ACTIVE_COLOR{ 0.0f, 0.0f, 0.0f, 0.85f };   // Viewer arrow button pressed
		static constexpr float PRESET_SEARCH_WIDTH = 220.0f;                                   // Presets search field width, at 1080p
		static constexpr float PRESET_BUTTON_HOVER_LIFT = 0.1f;                                // Per-channel brighten of the Apply button on hover
		static constexpr ImVec4 PRESET_APPLY_TEXT_COLOR{ 0.05f, 0.07f, 0.1f, 1.0f };           // Apply button label (dark on the accent fill)
		static constexpr float PRESET_BADGE_INFO_ALPHA = 0.9f;                                 // Effects 11 badge fill at rest
		static constexpr float PRESET_BADGE_ALPHA = 0.85f;                                     // CS Presets and Baseline badge fill at rest
		static constexpr ImVec4 PRESET_BADGE_INFO_TEXT{ 0.05f, 0.05f, 0.08f, 1.0f };           // Label over the Effects 11 badge
		static constexpr ImVec4 PRESET_BADGE_WARNING_TEXT{ 0.08f, 0.06f, 0.02f, 1.0f };        // Label over the CS Presets badge
		static constexpr ImVec4 PRESET_BADGE_SUCCESS_TEXT{ 0.02f, 0.08f, 0.04f, 1.0f };        // Label over the Baseline badge
		static constexpr ImVec4 PRESET_LOGO_PLACEHOLDER_COLOR{ 0.15f, 0.15f, 0.18f, 1.0f };    // Row logo fill when a pack has none
		static constexpr ImVec4 PRESET_POSTER_PLACEHOLDER_COLOR{ 0.12f, 0.12f, 0.14f, 1.0f };  // Detail poster fill when a pack has no cover
		static constexpr float PRESET_POSTER_HEIGHT_FRAMES = 6.0f;                             // Detail poster height, in frame heights
		static constexpr float PRESET_POSTER_ASPECT = 2.0f / 3.0f;                             // Detail poster width over height (portrait)
		static constexpr float PRESET_THUMB_HEIGHT_FRAMES = 4.0f;                              // Screenshot thumbnail width, in frame heights
		static constexpr float SCREENSHOT_ASPECT = 16.0f / 9.0f;                               // Screenshot width over height

		// Shader compile breakdown bar (ShaderCacheStatusWidget)
		static constexpr float SHADER_BREAKDOWN_BAR_HEIGHT = 14.0f;  // Bar height, 1080p baseline
		static constexpr ImU32 SHADER_BAR_BACKGROUND = IM_COL32(40, 40, 40, 255);
		static constexpr ImU32 SHADER_BAR_DEDUPLICATED = IM_COL32(120, 120, 120, 255);
		static constexpr ImU32 SHADER_BAR_DISK_CACHE = IM_COL32(70, 130, 200, 255);
		static constexpr ImU32 SHADER_BAR_FAST = IM_COL32(80, 180, 80, 255);
		static constexpr ImU32 SHADER_BAR_SLOW = IM_COL32(220, 180, 50, 255);
		static constexpr ImU32 SHADER_BAR_VERY_SLOW = IM_COL32(220, 60, 60, 255);
		static constexpr ImU32 SHADER_BAR_FAILED = IM_COL32(160, 30, 30, 255);

		// CS Editor (sizes are before UI scale unless noted)
		static constexpr float CSE_PALETTE_BUTTON_SIZE = 24.0f;                                // Palette swatch button edge, before UI scale
		static constexpr float CSE_PALETTE_SPACING = 4.0f;                                     // Gap between palette swatches, before UI scale
		static constexpr float CSE_PALETTE_STAR_RATIO = 0.4f;                                  // Empty-favourite star size as a share of the swatch
		static constexpr float CSE_PALETTE_COLOR_EPSILON = 0.001f;                             // Channel tolerance when matching a colour to a palette entry
		static constexpr float CSE_PALETTE_FRAME_PADDING = 2.0f;                               // Frame padding inside the palette window, before UI scale
		static constexpr int CSE_PALETTE_RECENT_COUNT = 5;                                     // Recently used colours listed under the favourites
		static constexpr ImVec4 CSE_PALETTE_EMPTY_SLOT_COLOR{ 0.2f, 0.2f, 0.2f, 1.0f };        // Empty favourite slot fill
		static constexpr ImVec4 CSE_PALETTE_EMPTY_STAR_COLOR{ 0.627f, 0.627f, 0.627f, 1.0f };  // Empty favourite slot star outline
		static constexpr float CSE_TITLE_APPLY_FILL_ALPHA = 0.6f;                              // Fill alpha of the text-only Apply title-bar action
		static constexpr float CSE_LP_BADGE_ALPHA = 0.22f;                                     // Fill alpha of the LP badge in the light list
		static constexpr float CSE_WEATHER_COLOR_PICKER_WIDTH = 225.0f;                        // Weather colour/form picker width, before UI scale
		static constexpr float CSE_CLOUD_PREVIEW_SIZE = 128.0f;                                // Cloud texture preview edge, before UI scale
		static constexpr float CSE_PICKER_ITEM_SPACING = 4.0f;                                 // Weather picker item spacing, before UI scale
		static constexpr float CSE_PICKER_FRAME_PADDING_Y = 2.0f;                              // Weather picker vertical frame padding, before UI scale
		static constexpr float CSE_PICKER_COMBO_WIDTH = 180.0f;                                // Weather picker combo and list width, before UI scale
		static constexpr float CSE_PICKER_LIST_ROWS = 12.0f;                                   // Rows the weather picker list shows before scrolling
		static constexpr float CSE_PICKER_CHIP_PAD = 6.0f;                                     // Padding around a weather filter chip label, before UI scale
		static constexpr float CSE_PICKER_CHIP_GAP = 3.0f;                                     // Gap between the two filter chip columns, before UI scale
		static constexpr float CSE_PICKER_GROUP_GAP = 2.0f;                                    // Gap between the picker list and its filter chips, before UI scale
		static constexpr float CSE_SETTINGS_WINDOW_RATIO = 0.25f;                              // First-use size of the editor settings window vs the display
		static constexpr float CSE_TIME_SCALE_SYNC_EPSILON = 0.01f;                            // Time scale drift before the slider re-reads the game value
		static constexpr float CSE_BROWSER_MAX_WIDTH = 960.0f;                                 // Widest the first-use Browser window gets, before UI scale, so ultrawide gives the rest to the viewport
		static constexpr float CSE_EDITOR_HEIGHT_RATIO = 0.85f;                                // First-use height of the Browser window as a share of the space under the action bar
		static constexpr float CSE_ACTION_BAR_BG_ALPHA_RATIO = 0.7f;                           // Action bar fill alpha relative to overlapping windows
		static constexpr float CSE_ACTION_BAR_SLIDER_WIDTH = 300.0f;                           // Game time plus time speed sliders in the action bar, before UI scale
		static constexpr float CSE_ACTION_BAR_DIVIDER_INSET_RATIO = 0.2f;                      // Vertical inset of the action bar dividers, as a share of its height
		static constexpr float CSE_DISABLED_ICON_ALPHA = 0.5f;                                 // Glyph alpha of a disabled action bar icon
		static constexpr float CSE_UI_SCALE_MIN = 0.5f;                                        // Editor UI scale slider range
		static constexpr float CSE_UI_SCALE_MAX = 2.0f;
		static constexpr float CSE_TIME_SLIDER_MIN_WIDTH = 40.0f;       // Narrowest a time control slider shrinks to, before UI scale
		static constexpr float CSE_ADD_LIGHT_POPUP_WIDTH = 520.0f;      // Add-light dialog width, before UI scale
		static constexpr float CSE_RECORD_TOD_LABEL_OFFSET = 100.0f;    // Records tab: time-of-day label column offset, before UI scale
		static constexpr float CSE_RECORD_FORM_LABEL_OFFSET = 150.0f;   // Records tab: form label column offset, before UI scale
		static constexpr float CSE_RECORD_PARENT_COLUMN_WIDTH = 20.0f;  // Records tab: extra offset for the inherit checkbox column
		static constexpr float CSE_CLOUD_LAYER_INDENT = 10.0f;          // Indent of a cloud layer's controls, before UI scale
		static constexpr float CSE_CLOUD_SLIDER_WIDTH_RATIO = 0.3f;     // Cloud layer speed slider width as a share of the available width
		static constexpr float CSE_CLOUD_TOD_LABEL_WIDTH = 120.0f;      // Cloud layer colour table label column, before UI scale
		static constexpr float CSE_FOG_PARAM_COLUMN_WIDTH = 80.0f;      // Fog table parameter column, before UI scale
		static constexpr float CSE_TOD_BUTTON_HEIGHT_RATIO = 1.5f;      // Time-of-day row height as a multiple of a frame height
		static constexpr float CSE_INHERIT_CHECKBOX_WIDTH = 20.0f;      // Space reserved for an inherit checkbox beside a slider, before UI scale
		static constexpr float CSE_CONFIRM_BUTTON_WIDTH = 120.0f;       // Delete confirmation button width, before UI scale
		static constexpr float CSE_FORM_LIST_NAME_WEIGHT = 3.0f;        // Browser list: Editor ID column stretch weight
		static constexpr float CSE_FORM_LIST_FILE_WEIGHT = 1.4f;        // Browser list: File column stretch weight
		static constexpr float CSE_USAGE_SLOTS_MAX_RATIO = 0.45f;       // Inspector usage row: most of the width the slot names may take
		static constexpr float CSE_PINNED_TINT_ALPHA = 0.14f;           // Tint behind a pinned feature row, as a share of the accent's opacity
		static constexpr float CSE_PINNED_SELECTED_TINT_ALPHA = 0.26f;  // ... while that row is selected
		static constexpr float CSE_ADD_LIGHT_POPUP_TOP_RATIO = 0.1f;    // Add-light dialog top edge as a share of the display, so combos open below it
	};

	static ThemeManager* GetSingleton()
	{
		static ThemeManager instance;
		return &instance;
	}

	// Static UI helper methods

	/**
	 * @brief Discovers all theme files in the themes directory
	 * @return Number of theme files discovered
	 */
	size_t DiscoverThemes();

	/**
	 * @brief Gets list of all discovered themes
	 * @return Vector of theme information
	 */
	const std::vector<ThemeInfo>& GetThemes() const { return themes; }

	/**
	 * @brief Gets theme names for dropdown display
	 * @return Vector of theme names
	 */
	std::vector<std::string> GetThemeNames() const;

	/**
	 * @brief Loads a specific theme by name
	 * @param themeName Name of the theme to load
	 * @param themeSettings Output parameter for loaded theme settings
	 * @return True if theme was loaded successfully
	 */
	bool LoadTheme(const std::string& themeName, json& themeSettings);

	/**
	 * @brief Saves current theme settings to a new theme file
	 * @param themeName Name for the new theme file
	 * @param themeSettings Theme settings to save
	 * @param displayName Display name for the theme
	 * @param description Description for the theme
	 * @return True if theme was saved successfully
	 */
	bool SaveTheme(const std::string& themeName, const json& themeSettings,
		const std::string& displayName, const std::string& description);

	/**
	 * @brief Gets theme info by name
	 * @param themeName Name of the theme
	 * @return Pointer to theme info or nullptr if not found
	 */
	const ThemeInfo* GetThemeInfo(const std::string& themeName) const;

	/**
	 * @brief Refreshes theme discovery (for runtime updates)
	 */
	void RefreshThemes();

	/**
	 * @brief Checks if themes have been discovered
	 */
	bool IsDiscovered() const { return discovered; }

	/**
	 * @brief Returns true if the theme name is a shipped preset
	 */
	bool IsPresetTheme(const std::string& themeName) const;

	/**
	 * @brief Gets the themes directory path
	 */
	std::filesystem::path GetThemesDirectory() const;

	/**
	 * @brief Creates default theme files if they don't exist
	 */
	void CreateDefaultThemeFiles();

private:
	ThemeManager() = default;
	~ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	/**
	 * @brief Loads a single theme file
	 * @param filePath Path to the theme file
	 * @return Theme info if successful, nullptr otherwise
	 */
	std::unique_ptr<ThemeInfo> LoadThemeFile(const std::filesystem::path& filePath);

	/**
	 * @brief Validates theme data structure
	 * @param themeData JSON data to validate
	 * @return True if theme data is valid
	 */
	bool ValidateThemeData(const json& themeData) const;

	std::vector<ThemeInfo> themes;
	bool discovered = false;

	// Constants
	static constexpr size_t MAX_THEMES = 100;             // Prevent excessive theme loading
	static constexpr size_t MAX_FILE_SIZE = 1024 * 1024;  // 1MB max theme file size
};

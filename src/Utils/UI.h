#pragma once
#include <algorithm>
#include <cfloat>  // For FLT_MAX
#include <cstdio>
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>  // For WPARAM and virtual key constants

#include "../FeatureConstraints.h"
#include "../Menu/Fonts.h"
#include "../Menu/ThemeManager.h"
#include "Utils/Input.h"

// Forward declarations
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ImGuiContext;
struct ImGuiWindow;
struct ImRect;
struct ImVec2;
class Menu;
class Feature;

// Helper macro for displaying texture buffers in ImGui with resolution info
#define BUFFER_VIEWER_NODE_IMPL(a_value, a_label, a_scale)                                                       \
	if (a_value && a_value->srv.get()) {                                                                         \
		char buf[128];                                                                                           \
		snprintf(buf, sizeof(buf), "%s (%ux%u)", a_label, a_value->desc.Width, a_value->desc.Height);            \
		if (ImGui::TreeNode(buf)) {                                                                              \
			ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale }); \
			ImGui::TreePop();                                                                                    \
		}                                                                                                        \
	}

#define BUFFER_VIEWER_NODE(a_value, a_scale) \
	BUFFER_VIEWER_NODE_IMPL(a_value, #a_value, a_scale)

#define BUFFER_VIEWER_NODE_TITLE(a_value, a_title, a_scale) \
	BUFFER_VIEWER_NODE_IMPL(a_value, a_title, a_scale)

#define BUFFER_VIEWER_NODE_BULLET(a_value, a_scale) \
	ImGui::BulletText(#a_value);                    \
	ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale });

#define ADDRESS_NODE(a_value) Util::AddressButton(#a_value, reinterpret_cast<uintptr_t>(a_value))

namespace Util
{
	/** @brief Button labelled `a_name` that copies `a_address` to the clipboard as hex, with a tooltip saying so. */
	void AddressButton(const char* a_name, uintptr_t a_address);

	/** @brief Feeds ImGui the display size and the mouse position, mapped from window to render-target pixels. */
	void UpdateImGuiInput(HWND hwnd, float bufferWidth, float bufferHeight);
	/**
	 * Represents a single line and its color for any colored text rendering (tooltips, legends, etc.).
	 */
	struct ColoredTextLine
	{
		std::string text;
		ImVec4 color;
	};
	using ColoredTextLines = std::vector<ColoredTextLine>;

	// Text rendering constants
	constexpr float DefaultHeaderTextScale = 1.5f;  // Larger scale for header text to improve readability

	// Baseline font size for UI layout scaling (1080p dynamic font: DEFAULT_SCREEN_HEIGHT * DEFAULT_FONT_RATIO).
	// Theme style values and pixel constants are designed for this size.
	constexpr float kBaselineFontSize = ThemeManager::Constants::DEFAULT_SCREEN_HEIGHT * ThemeManager::Constants::DEFAULT_FONT_RATIO;

	/** @brief Font size relative to a baseline font size, the factor pixel sizes are scaled by. */
	inline float GetUIScaleForBaseline(float baselineFontSize) { return ImGui::GetFontSize() / baselineFontSize; }

	/** @brief Returns a scale factor relative to the 1080p baseline font size. */
	inline float GetUIScale() { return GetUIScaleForBaseline(kBaselineFontSize); }

	/** @brief Returns a scale factor for search controls authored against the 2K baseline. */
	inline float GetSearchUIScale() { return GetUIScaleForBaseline(ThemeManager::Constants::SEARCH_BASELINE_SCREEN_HEIGHT * ThemeManager::Constants::DEFAULT_FONT_RATIO); }

	/**
	 * Usage:
	 * if (auto _tt = Util::HoverTooltipWrapper()){
	 *     ImGui::Text("What the tooltip says.");
	 * }
	 *
	 * Automatically applies the Subtext font role for consistent tooltip styling.
	*/
	class HoverTooltipWrapper
	{
	private:
		bool hovered;
		ImFont* previousFont;

	public:
		HoverTooltipWrapper();
		~HoverTooltipWrapper();
		inline operator bool() { return hovered; }
	};

	/**
	 * RAII wrapper for centered popup modals. Positions the window before BeginPopupModal
	 * (prevents first-frame stretch) and calls EndPopup() automatically on destruction.
	 *
	 * By default centers to the viewport center. Pass an explicit pos/pivot to override.
	 * Pass kPopupCenter as pos to use the default viewport-center behavior.
	 *
	 * Usage:
	 * // Centered (default):
	 * if (auto popup = Util::CenteredPopupModal("Title")) { ... }
	 *
	 * // Custom position:
	 * if (auto popup = Util::CenteredPopupModal("Title", nullptr, ImGuiWindowFlags_AlwaysAutoResize,
	 *                                            ImVec2(x, y), ImVec2(0.0f, 0.0f))) { ... }
	 */
	class CenteredPopupModal
	{
	public:
		static constexpr ImVec2 kPopupCenter{ -FLT_MAX, -FLT_MAX };

		CenteredPopupModal(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize, ImVec2 pos = kPopupCenter, ImVec2 pivot = ImVec2(0.5f, 0.5f));
		~CenteredPopupModal();
		operator bool() const { return isOpen; }

		CenteredPopupModal(const CenteredPopupModal&) = delete;
		CenteredPopupModal& operator=(const CenteredPopupModal&) = delete;
		CenteredPopupModal(CenteredPopupModal&&) = delete;
		CenteredPopupModal& operator=(CenteredPopupModal&&) = delete;

	private:
		bool isOpen;
	};

	/**
	 * Usage:
	 * {
	 *      auto _ = DisableGuard(disableThis);
	 *      ... Some settings ...
	 * }
	*/
	class DisableGuard
	{
	private:
		bool disable;

	public:
		DisableGuard(bool disable);
		~DisableGuard();
	};

	/**
	 * @brief Whether a mouse or keyboard interaction finished this frame: a click, drag or text entry was
	 *        released, or focus moved on from it. Buttons, sliders and pickers all end this way, so it is
	 *        the moment to check whether what they edit changed.
	 */
	bool InteractionEndedThisFrame();

	/** @brief Hides feature Debug sections while alive (CS Editor Features window); restores the previous state. */
	class HideDebugSectionsGuard
	{
	public:
		explicit HideDebugSectionsGuard(bool hide);
		~HideDebugSectionsGuard();

		HideDebugSectionsGuard(const HideDebugSectionsGuard&) = delete;
		HideDebugSectionsGuard& operator=(const HideDebugSectionsGuard&) = delete;

	private:
		bool previous;
	};

	/** @brief False while a HideDebugSectionsGuard hides them; gate each feature's Debug header on this. */
	bool ShowDebugSections();

	/** @brief Eases fade toward 1 (visible) or 0 (hidden) over ThemeManager::Constants::UI_FADE_SECONDS. */
	void StepFade(float& fade, bool visible, float deltaTime);

	/** @brief Scales ImGui alpha by fade while alive; below 1 it also hides the mouse from windows drawn in scope. */
	class FadeScope
	{
	public:
		explicit FadeScope(float fade);
		~FadeScope();

		FadeScope(const FadeScope&) = delete;
		FadeScope& operator=(const FadeScope&) = delete;

	private:
		ImGuiContext& context;
		ImGuiWindow* hoveredWindow;
		ImGuiWindow* hoveredWindowUnderMoving;
	};

	/**
	 * Renders text using the disabled text color.
	 * @param a_text Start of the text
	 * @param a_textEnd Optional end pointer (nullptr for null-terminated strings)
	 */
	void TextUnformattedDisabled(const char* a_text, const char* a_textEnd = nullptr);

	/**
	 * Full-row hover/selection highlight for ImGui tables.
	 * Makes the entire table row highlight on hover/active/selected instead of just the selectable cell.
	 * @param label The selectable label text
	 * @param selected Whether the row is currently selected
	 * @param flags ImGuiSelectableFlags to pass through
	 * @return True if the row was pressed
	 */
	bool TableRowSelectable(const char* label, bool selected, ImGuiSelectableFlags flags);

	/**
	 * Positions the next tooltip window near the mouse cursor, clamped to viewport bounds.
	 * Automatically flips above the cursor when it would overflow the bottom.
	 * Call this before BeginTooltip().
	 * @param estimatedHeight Estimated tooltip height in pixels
	 * @param estimatedWidth Estimated tooltip width in pixels (0 to skip horizontal clamping)
	 */
	void SetTooltipPositionNearMouse(float estimatedHeight, float estimatedWidth = 0.0f);

	/** @brief Hover flags that still show tooltips on greyed controls, which must explain why they are greyed. */
	inline constexpr ImGuiHoveredFlags kTooltipWhenDisabled =
		ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;

	/**
	 * Shows a positioned tooltip with wrapped text when the previous item is hovered.
	 * Uses SetTooltipPositionNearMouse for viewport-aware placement.
	 * @param a_desc Tooltip text
	 * @param a_flags Hover flags (default: ImGuiHoveredFlags_DelayNormal)
	 */
	void AddTooltip(const char* a_desc, ImGuiHoveredFlags a_flags = ImGuiHoveredFlags_DelayNormal);

	/** @brief Plain-text hover tooltip (HoverTooltipWrapper styling) for the item just drawn. */
	inline void HoverTip(const char* a_text)
	{
		if (auto _tt = HoverTooltipWrapper())
			ImGui::TextUnformatted(a_text);
	}

	/** @brief Checkbox followed by a hover tooltip; returns true when the value changed. */
	inline bool CheckboxTip(const char* a_label, bool* a_value, const char* a_tooltip)
	{
		const bool changed = ImGui::Checkbox(a_label, a_value);
		HoverTip(a_tooltip);
		return changed;
	}

	/** @brief Float slider followed by a hover tooltip; returns true when the value changed. */
	inline bool SliderFloatTip(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format, const char* a_tooltip, ImGuiSliderFlags a_flags = 0)
	{
		const bool changed = ImGui::SliderFloat(a_label, a_value, a_min, a_max, a_format, a_flags);
		HoverTip(a_tooltip);
		return changed;
	}

	/** @brief Text HelpMarker draws, exposed so callers can measure it. */
	inline constexpr const char* kHelpMarkerLabel = "(?)";

	/**
	 * Draws a "(?)" help marker with a tooltip on hover.
	 * @param a_desc Tooltip text to show
	 */
	void HelpMarker(const char* a_desc);

	/**
	 * @brief Compact segmented control: one rounded track, label-sized segments, the selection in the accent colour.
	 * @param a_selected In/out selected index
	 * @param a_marked Index to flag with a dot when it is not the selection, or -1
	 * @param a_muted Dims the selection so the bar still looks active while representing an off/flat mode
	 * @return True on the frame a segment is clicked
	 */
	bool SegmentedControl(const char* a_id, const char* const* a_labels, int a_count, int& a_selected,
		int a_marked = -1, bool a_muted = false);

	/**
	 * @brief Glass/mica pill tab bar: drop-in replacement for ImGui::BeginTabBar / BeginTabItem.
	 * Draws a compact translucent track with a selected pill instead of classic folder tabs.
	 * Nesting is supported (stack). Tab labels from the previous frame drive the strip layout.
	 */
	bool BeginPillTabBar(const char* str_id);
	bool BeginPillTabItem(const char* label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0);
	void EndPillTabItem();
	void EndPillTabBar();
	/** @brief True while a BeginPillTabBar has not yet been closed (for font helpers). */
	bool IsInsidePillTabBar();

	/** @brief Full-width tinted bar with an icon glyph, for a state that changes what the user can touch.
	 *  @return True when the banner is left-clicked. */
	bool StatusBanner(const char* a_icon, const char* a_message, const ImVec4& a_color);

	/** @brief Side length of DrawLockStatusBadge's rounded chip (scales with UI). */
	float GetLockStatusBadgeSize();

	/**
	 * @brief Pale green/red rounded lock/unlock chip used by the CS Editor action bar and weather headers.
	 * @param a_min Top-left of the badge in screen space.
	 * @param a_locked True draws a lock in SuccessColor; false draws unlock in Error.
	 * @param a_drawList Optional; defaults to the current window draw list.
	 * @param a_hovered Draws the transparent-icon-button hover overlay under the badge.
	 * @param a_held Draws the pressed overlay instead.
	 */
	void DrawLockStatusBadge(ImVec2 a_min, bool a_locked, ImDrawList* a_drawList = nullptr, bool a_hovered = false, bool a_held = false);

	/**
	 * @brief Interactive lock badge that toggles via ButtonBehavior.
	 * @return True on the frame the badge is clicked.
	 */
	bool LockStatusBadgeButton(const char* a_id, bool a_locked, const char* a_tooltip = nullptr);

	/** @brief Width of a DrawBadgeAt badge for the given label. */
	float MeasureBadgeWidth(const char* a_label);

	/**
	 * @brief Soft rectangular label badge, vertically centred in a row.
	 * @param a_min Top-left of the row in screen space.
	 * @param a_rowHeight Height of the row the badge is centred in.
	 * @param a_bg Badge fill; usually the tint at a low alpha.
	 * @param a_fg Label colour.
	 * @param a_drawList Optional; defaults to the current window draw list.
	 * @return The badge width.
	 */
	float DrawBadgeAt(ImVec2 a_min, float a_rowHeight, const char* a_label, const ImVec4& a_bg, const ImVec4& a_fg,
		ImDrawList* a_drawList = nullptr);

	/** @brief Badge tinted from one colour (soft fill, readable label), drawn at the cursor; advances layout with Dummy. */
	void Badge(const char* a_label, const ImVec4& a_tint);

	/**
	 * @brief Soft rectangular HDR/SDR capability pill (muted blue / muted red).
	 * @param a_supportsHDR True draws "HDR"; false draws "SDR".
	 */
	float MeasureHdrSdrCapabilityPillWidth(bool a_supportsHDR);
	void DrawHdrSdrCapabilityPillAt(ImVec2 a_min, float a_rowHeight, bool a_supportsHDR, ImDrawList* a_drawList = nullptr);
	/** @brief Draws the pill at the current cursor and advances layout with Dummy. */
	void DrawHdrSdrCapabilityPill(bool a_supportsHDR);

	/** @brief When locked, draws a lock banner and greys every control until the scope ends.
	 *  @param a_outBannerClicked Optional; set when the banner is left-clicked (e.g. to navigate). */
	class LockedSection
	{
	public:
		LockedSection(bool a_locked, const char* a_message, bool* a_outBannerClicked = nullptr);
		~LockedSection();

		LockedSection(const LockedSection&) = delete;
		LockedSection& operator=(const LockedSection&) = delete;

	private:
		bool m_locked;
	};

	/** @brief Explainer text behind a collapsed-by-default disclosure row. */
	void Explainer(const char* a_label, const char* a_text);

	/**
	 * @brief Thin vertical rule separating groups of related toolbar buttons, used in place of SameLine().
	 * @param a_continueLine False when the next item calls SameLine() itself.
	 */
	void ToolbarDivider(bool a_continueLine = true);

	/**
	 * Confirmation popup for clearing shader cache.
	 * Call RequestClearShaderCacheConfirmation() when the clear button is clicked.
	 * Call DrawClearShaderCacheConfirmation() every frame to render the popup.
	 * The popup respects the "don't ask me again" setting.
	 */
	void RequestClearShaderCacheConfirmation();
	/** @brief Renders the shader-cache clear confirmation popup; call every frame. */
	void DrawClearShaderCacheConfirmation();
	/** @brief Draws the banner telling the user that shaders were cleared and will be fully recompiled; click dismisses it. */
	void DrawRecacheBanner();

	/**
	 * @brief Reloads the saved settings, first asking for confirmation when there are unsaved changes.
	 * Pair with DrawRestoreSavedSettingsConfirmation(), called every frame while the menu is open.
	 */
	void RequestRestoreSavedSettingsConfirmation();

	/** @brief Draws the restore confirmation requested by RequestRestoreSavedSettingsConfirmation() and restores on confirm. */
	void DrawRestoreSavedSettingsConfirmation();

	/**
	 * Reusable confirmation popup. Call RequestConfirmation() to trigger, DrawConfirmationPopup() each frame.
	 * Returns true on the frame the user confirms. Supports optional "Don't ask again" checkbox.
	 */
	struct ConfirmationPopup
	{
		/** @brief The button pressed on a frame; Cancel also covers closing the popup. */
		enum class Choice
		{
			None,
			Confirm,
			Alternate,
			Cancel
		};

		/** @brief Which button keyboard navigation starts on, so Enter picks it. */
		enum class Focus
		{
			None,
			Confirm,
			Cancel
		};

		std::string title;
		std::string message;
		/// Empty hides the button.
		std::string confirmLabel;
		/// Empty falls back to the translated "Cancel".
		std::string cancelLabel;
		/// Optional destructive button between Confirm and Cancel; empty hides it.
		std::string alternateLabel;
		/// Styles Confirm as the primary action rather than a plain button.
		bool primaryConfirm = false;
		Focus focus = Focus::None;
		bool showDontAskAgain = false;
		bool* dontAskAgainPersist = nullptr;  // Optional external bool to persist preference
		/// Empty uses the default "Don't ask me again".
		std::string dontAskAgainLabel;
		/// The button a ticked "Don't ask again" remembers; later requests answer with it without showing.
		Choice rememberedChoice = Choice::Confirm;

		ConfirmationPopup() = default;
		ConfirmationPopup(std::string title, std::string message, std::string confirmLabel, std::string cancelLabel) :
			title(std::move(title)), message(std::move(message)), confirmLabel(std::move(confirmLabel)), cancelLabel(std::move(cancelLabel)) {}

		/** @brief Opens the popup on the next Draw, or resolves it at once when the user chose not to be asked again. */
		void Request();
		/** @brief Draws the popup while requested. @return True on the frame the user confirms. */
		bool Draw();
		/** @brief Draws the popup while requested. @return The button pressed this frame, else Choice::None. */
		Choice DrawChoice();

		/** @brief True while the popup is requested or showing. */
		bool IsOpen() const { return show; }

	private:
		bool show = false;
		/// The remembered answer to a request that skipped the popup, returned by the next DrawChoice.
		Choice pendingChoice = Choice::None;
		bool dontAskCheckbox = false;
	};

	/**
	 * @brief A fixed-width modal with a close button, asked for with Request() from anywhere and drawn each
	 * frame with Draw() until it closes.
	 */
	class DeferredModal
	{
	public:
		/** @brief Opens the modal on the next Draw. */
		void Request() { active = pendingOpen = true; }

		/** @brief Whether the modal is open or about to open. */
		bool IsActive() const { return active; }

		/**
		 * @brief Opens the modal when requested, then calls drawContents inside it while it stays open.
		 * @param title Visible title; give it a ### id so it survives a language switch.
		 */
		template <class DrawContents>
		void Draw(const char* title, float width, DrawContents&& drawContents)
		{
			if (!active)
				return;
			const ImGuiID id = ImGui::GetID(title);
			if (std::exchange(pendingOpen, false)) {
				openedId = id;
				ImGui::OpenPopup(title);
			}
			// Callers under another ID stack would see it as closed, so only the one that opened it draws it.
			if (id != openedId)
				return;
			ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
			bool open = true;
			if (auto popup = CenteredPopupModal(title, &open))
				drawContents();
			if (!ImGui::IsPopupOpen(title))
				active = false;
		}

	private:
		ImGuiID openedId = 0;
		bool active = false;
		bool pendingOpen = false;
	};

	/**
	 * RAII wrapper for styled ImGui buttons that automatically applies and restores styling.
	 * Use this to ensure consistent button styling without forgetting to pop styles.
	 */
	class StyledButtonWrapper
	{
	public:
		/**
		 * Creates a styled button wrapper with custom colors.
		 * @param normalColor Color when button is not hovered/pressed
		 * @param hoveredColor Color when button is hovered
		 * @param activeColor Color when button is pressed
		 */
		StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor);

		/**
		 * Destructor automatically pops the applied styles
		 */
		~StyledButtonWrapper();

		// Delete copy and move operations to prevent double pops
		StyledButtonWrapper(const StyledButtonWrapper&) = delete;
		StyledButtonWrapper& operator=(const StyledButtonWrapper&) = delete;
		StyledButtonWrapper(StyledButtonWrapper&&) = delete;
		StyledButtonWrapper& operator=(StyledButtonWrapper&&) = delete;

	private:
		int m_pushedStyles;
	};

	/**
	 * Creates a StyledButtonWrapper using a status color with shared hover/active adjustment.
	 * Use when a caller needs a custom status color instead of one of the semantic helpers below.
	 */
	StyledButtonWrapper StatusButtonStyle(const ImVec4& color);

	/**
	 * Style for destructive or critical actions such as Delete, Clear, Remove, or irreversible confirms.
	 * Uses the theme error color as the button fill and adjusts hover/active brightness for contrast.
	 */
	StyledButtonWrapper DestructiveButtonStyle();

	/**
	 * Creates a StyledButtonWrapper using alpha-based hover/active transitions.
	 * Used for status text buttons where the color itself communicates intent.
	 * Prefer the named helpers below.
	 */
	StyledButtonWrapper StatusTextButtonStyle(const ImVec4& color);

	/** Style for confirmatory or positive actions such as Apply, Confirm, or Accept. */
	StyledButtonWrapper SuccessButtonStyle();

	/** Draws a theme success-colored button for confirmatory or positive actions. */
	bool SuccessButton(const char* label, const ImVec2& size = ImVec2(0, 0));

	/** Style for cautionary or reversible actions such as Revert, Undo, or Reset to saved values. */
	StyledButtonWrapper WarningButtonStyle();

	/** Draws a theme warning-colored button for cautionary or reversible actions. */
	bool WarningButton(const char* label, const ImVec2& size = ImVec2(0, 0));

	/**
	 * Alpha-based error-color button — use in toolbar rows alongside SuccessButton/WarningButton
	 * for visual consistency. For standalone destructive actions (delete icons, close buttons),
	 * prefer ErrorButton which uses the brightness-based DestructiveButtonStyle.
	 */
	bool ErrorTextButton(const char* label, const ImVec2& size = ImVec2(0, 0));

	/** Draws a destructive theme error-colored button for delete, clear, remove, or irreversible actions. */
	bool ErrorButton(const char* label, const ImVec2& size = ImVec2(0, 0));

	/** Draws a destructive button with ButtonWithFlash click feedback. */
	bool ErrorButtonWithFlash(const char* label, const ImVec2& size = ImVec2(0, 0), int flashDurationMs = 200);

	/**
	 * Creates a transparent button with theme text color hover. Caller must push/pop FrameBorderSize=0 separately.
	 */
	StyledButtonWrapper TransparentIconButtonStyle();

	/** Returns theme text color if monochrome icons enabled, otherwise white. */
	ImVec4 GetIconTint();

	/** @brief Draws a theme-rounded hover/active fill over a button rect. */
	bool DrawRoundedButtonHighlight(const ImRect& rect, bool hovered, bool active, ImDrawList* drawList = nullptr);
	bool DrawRoundedButtonHighlight(const ImVec2& min, const ImVec2& max, bool hovered, bool active, ImDrawList* drawList = nullptr);
	bool DrawRoundedButtonHighlight(const ImVec2& min, const ImVec2& max, bool hovered, bool active, float rounding, ImDrawList* drawList);

	/**
	 * @brief DrawRoundedButtonHighlight with the fill easing in and out (Util::Anim) instead of switching on and off.
	 * For buttons hit-tested by hand, where there is no ImGui item whose id could key the animation.
	 * @param id Stable key for this button (e.g. ImGui::GetID("##Home")).
	 * @param held True while the button is pressed; draws the stronger fill.
	 * @param current Keeps the hover fill, for the button of the current page.
	 * @return True when anything was drawn.
	 */
	bool DrawAnimatedButtonHighlight(const ImRect& rect, ImGuiID id, bool hovered, bool held, bool current, ImDrawList* drawList = nullptr);

	/** @brief Draws a circle icon, filled or outlined, on the window draw list. */
	void DrawIconCircle(ImVec2 center, float radius, ImU32 color, bool filled);

	/** @brief Reserves one text-line-high square and draws a dot centred in it, so callers can test ImGui::IsItemHovered(). */
	void DrawInlineIndicatorDot(ImU32 color, bool filled);

	/** @brief ImGui::Begin() wrappers that replace native title-bar button highlights with rounded ones. */
	bool BeginWithRoundedClose(const char* name, bool* p_open, ImGuiWindowFlags flags = 0);
	bool BeginPopupModalWithRoundedClose(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);

	/**
	 * @brief Like BeginWithRoundedClose, but floating windows get a flat custom header that doubles as the
	 * drag handle; docked windows keep their native tab. Collapsing is always disabled.
	 * @param p_open      Close flag cleared by the header's close button; nullptr omits the button.
	 * @param drawExtras  Optional compact controls between the title and close button, floating only.
	 * @param flags       Extra window flags; NoTitleBar is managed per dock state.
	 * @param drawLeading Optional icon drawn left of the title (floating custom header only).
	 *                    Receives the icon's top-left and the reserved square size (font size).
	 */
	bool BeginWithCustomHeader(const char* name, bool* p_open,
		const std::function<void()>& drawExtras = nullptr, ImGuiWindowFlags flags = 0,
		const std::function<void(ImVec2 iconMin, float iconSize)>& drawLeading = nullptr);

	/**
	 * @brief Shared height for CS Editor floating headers and the top action bar.
	 * Native GetFrameHeight() is only FontSize + FramePadding.y*2, which makes glyphs look glued
	 * to the top edge; this doubles the vertical pad so content sits dead-centre with equal air.
	 */
	float GetEditorChromeHeaderHeight();

	/**
	 * @brief Y offset from a chrome row's top to the ImGui text cursor so capital-letter ink is centred.
	 * Uses FontBaked::Ascent (line-box top → bottom of 'A'), not the full FontSize em-box.
	 */
	float GetEditorChromeTextCursorOffsetY(float rowHeight);

	/**
	 * @brief Distance from the top of a text line to the vertical centre of capital-letter ink in the current font
	 * (at its current size, window font scale included). ImGui lays a line out as ascent + descent, so the line box's
	 * centre sits below the ink's: text centred by its line box looks too high, and a glyph or image centred in the
	 * same row looks too low next to it. Centre by ink instead; this is the one place that measures it.
	 */
	float GetTextInkCenterY();

	/**
	 * @brief Screen y at which to draw a line of text (cursor / AddText position) so its ink is centred in a row.
	 * Boxes (images, icon buttons, pills) are centred by their rect as usual; text goes through this.
	 */
	float CenterTextTopY(float rowTop, float rowHeight);

	/**
	 * Button with simple flash feedback (matches action icon hover effect style)
	 * @param label Button text
	 * @param size Button size (optional)
	 * @param flashDurationMs How long to show flash effect in milliseconds (default 200ms)
	 * @return True if the button was clicked
	 */
	bool ButtonWithFlash(const char* label, const ImVec2& size = ImVec2(0, 0), int flashDurationMs = 200);

	/**
	 * @brief Lets a fill be drawn beneath the widgets submitted while the scope is alive, by drawing them on a
	 * foreground channel of the window's draw list. Inactive (the fill then lands on top) when the list is already
	 * split, as inside a table.
	 */
	class UnderlayScope
	{
	public:
		UnderlayScope();
		~UnderlayScope();
		UnderlayScope(const UnderlayScope&) = delete;
		UnderlayScope& operator=(const UnderlayScope&) = delete;

		/** @brief Switches to the background channel, drawn first, and returns the window's draw list. */
		ImDrawList* Background();

	private:
		ImDrawList* list;
		bool split = false;
	};

	/**
	 * @brief Drop-in for ImGui::Button whose hover and press colours ease in and out. Colours come from the style
	 * stack, so pushed overrides still apply; behaviour is otherwise identical to ImGui::Button.
	 */
	bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));

	/**
	 * @brief ImGui::Selectable. Every Selectable is hooked (UIHooks, Anim::GlideSelectable), so rows of any list get
	 * the gliding hover / selected highlight whichever way they are called; these exist for call sites that prefer
	 * the Util namespace.
	 */
	bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
	/** @brief Selectable() overload that toggles `*selected` when clicked, like its ImGui counterpart. */
	bool Selectable(const char* label, bool* selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));

	/**
	 * Animated toggle switch in the Material style (after cmdwtf/imgui_toggle's Material preset): a thin pill track
	 * with a larger round knob that overhangs its ends and slides between the two states. Colours come from the theme's
	 * accent; animations follow the UI animation settings.
	 * @param label Label text to display next to the toggle
	 * @param enabled Reference to the boolean state to toggle
	 * @param size Toggle size (optional, defaults to automatic sizing)
	 * @return True if the toggle state was changed
	 */
	bool FeatureToggle(const char* label, bool* enabled, const ImVec2& size = ImVec2(0, 0));

	/**
	 * RAII wrapper for creating collapsible UI sections.
	 * Automatically handles the TreeNode creation, styling, and cleanup.
	 */
	class SectionWrapper
	{
	public:
		/**
		 * Creates a section wrapper for organizing UI content.
		 * @param title The section title
		 * @param description Optional description text shown below the title
		 * @param titleColor Color for the section title
		 * @param isVisible Whether the section should be visible (used for conditional sections)
		 * @param ruleBelow Whether a separator closes the section; off for the last of a run
		 */
		SectionWrapper(const char* title, const char* description = nullptr,
			const ImVec4& titleColor = ImVec4(1, 1, 1, 1), bool isVisible = true, bool ruleBelow = true);

		/** @brief Closes the section with its separator, if it has one. */
		~SectionWrapper();

		/**
		 * Conversion operator to check if section should be drawn
		 */
		operator bool() const;

		// Delete copy and move operations to prevent double pops
		SectionWrapper(const SectionWrapper&) = delete;
		SectionWrapper& operator=(const SectionWrapper&) = delete;
		SectionWrapper(SectionWrapper&&) = delete;
		SectionWrapper& operator=(SectionWrapper&&) = delete;

	private:
		bool m_shouldDraw;
		bool m_ruleBelow;
	};

	/** @brief Slider that shows a 0..1 member as a percentage (lb..ub) and writes back only on edit. */
	bool PercentageSlider(const char* label, float* data, float lb = 0.f, float ub = 100.f, const char* format = "%.1f %%", ImGuiSliderFlags flags = 0);

	/**
	 * Draws a 2-4 component float slider. Holding Shift edits all components from the first component.
	 */
	template <int Num = 3>
	bool ShiftSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
	{
		static_assert(Num > 1 && Num < 5);

		if (ImGui::GetIO().KeyShift) {
			auto changed = ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
			if (changed)
				for (int i = 1; i < Num; i++)
					v[i] = v[0];
			return changed;
		}

		if constexpr (Num == 2)
			return ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
		else if constexpr (Num == 3)
			return ImGui::SliderFloat3(label, v, v_min, v_max, format, flags);
		else
			return ImGui::SliderFloat4(label, v, v_min, v_max, format, flags);
	}

	/** @brief Main viewport size multiplied by scale. */
	ImVec2 GetNativeViewportSizeScaled(float scale);

	// Icon loading functions
	// `device` must remain alive for the SRV lifetime. Caller owns *out_srv and must `Release()` it.
	bool LoadTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size);

	/** @brief Loads a DDS file into a shader resource view; same ownership rules as LoadTextureFromFile. */
	bool LoadDDSTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size);

	/** @brief Loads the menu icon textures; forwards to IconLoader. */
	bool InitializeMenuIcons(Menu* menu);

	// Text rendering helpers for clearer title text
	// These functions modify ImGui rendering state and should be called within ImGui context
	ImVec2 DrawSharpText(const char* text, bool alignToPixelGrid = true, float scale = 1.0f);
	/** @brief Draws a logo followed by text on one line. @return Size of the drawn row. */
	ImVec2 DrawAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale = DefaultHeaderTextScale, ImU32 logoTint = IM_COL32_WHITE);

	/**
	 * Constraint-aware UI helpers
	 * These functions automatically check if a setting is constrained by another feature
	 * and disable the control with an informative tooltip explaining why
	 */
	namespace ConstrainedUI
	{
		/**
		 * Constraint-aware checkbox that greys out when constrained by another feature
		 * @param label The label for the checkbox
		 * @param value Pointer to the bool value
		 * @param settingId The setting identifier for constraint lookup
		 * @return True if value was changed (only possible when not constrained)
		 */
		bool Checkbox(const char* label, bool* value, const FeatureConstraints::SettingId& settingId);

		/**
		 * Constraint-aware slider float that greys out when constrained by another feature
		 * @param label The label for the slider
		 * @param value Pointer to the float value
		 * @param min Minimum value
		 * @param max Maximum value
		 * @param settingId The setting identifier for constraint lookup
		 * @param format Display format
		 * @return True if value was changed (only possible when not constrained)
		 */
		bool SliderFloat(const char* label, float* value, float min, float max,
			const FeatureConstraints::SettingId& settingId, const char* format = "%.3f");

		/**
		 * Constraint-aware slider int that greys out when constrained by another feature
		 * @param label The label for the slider
		 * @param value Pointer to the int value
		 * @param min Minimum value
		 * @param max Maximum value
		 * @param settingId The setting identifier for constraint lookup
		 * @param format Display format
		 * @return True if value was changed (only possible when not constrained)
		 */
		bool SliderInt(const char* label, int* value, int min, int max,
			const FeatureConstraints::SettingId& settingId, const char* format = "%d");
	}

	/**
	 * Draws a custom styled collapsible category header with lines extending from both sides
	 * @param categoryName The name of the category to display
	 * @param isExpanded Reference to the expansion state
	 * @param categoryCount Number of features in the category
	 * @return true if the expansion state was toggled
	 */
	/** @brief The icon texture of a feature category (its stable key, e.g. "Lighting"); null when it has none. */
	ID3D11ShaderResourceView* GetCategoryIcon(const char* categoryKey);

	/** @brief Draws a feature-category header with its icon and count; a click toggles isExpanded. @return True when clicked. */
	bool DrawCategoryHeader(const char* categoryKey, const char* displayName, bool& isExpanded, int categoryCount);

	/**
	 * Draws a custom styled section header with lines extending from both sides
	 * @param sectionName The name of the section to display
	 * @param useWhiteText Whether to use white text (for differentiation)
	 * @param isCollapsible Whether the header should be collapsible
	 * @param isExpanded Reference to the expansion state (only used if collapsible)
	 * @return true if the expansion state was toggled (only relevant if collapsible)
	 */
	bool DrawSectionHeader(const char* sectionName, bool useWhiteText = false, bool isCollapsible = true, bool* isExpanded = nullptr);

	/**
	 * Configuration for color-coded value display with flexible thresholds and colors.
	 * Supports variable number of thresholds and corresponding colors.
	 */
	struct ColorCodedValueConfig
	{
		struct ThresholdColor
		{
			float threshold;
			ImVec4 color;

			ThresholdColor(float t, const ImVec4& c) :
				threshold(t), color(c) {}
		};

		std::vector<ThresholdColor> thresholds;  // Thresholds in ascending order with their colors
		const char* format = "%.1f%%";           // Printf-style format string for the value
		const char* tooltipText = nullptr;       // Optional tooltip text
		bool sameLine = true;                    // Whether to put value on same line as label

		// Helper methods for common patterns (implemented in UI.cpp to avoid header dependencies)
		// Use when higher values indicate problems/danger (intensity, errors, warnings)
		static ColorCodedValueConfig HighIsBad(float low, float med, float high);
		// Use when higher values indicate good things (performance, quality, progress)
		static ColorCodedValueConfig HighIsGood(float low, float med, float high);
	};
	/**
	 * Color-codes a value based on flexible thresholds and displays it with optional tooltip.
	 * Common pattern for showing status values (percentages, intensities, etc.) with color feedback.
	 *
	 * @param label The label to display next to the value.
	 * @param valueToCheck The numeric value to use for color-coding (compared to thresholds).
	 * @param valueStr The string to display (can be formatted, units, or descriptive text).
	 * @param config The configuration for thresholds, colors, formatting, and tooltip.
	 * @param useBullet If true (default), use ImGui::BulletText for the label; if false, use ImGui::Text.
	 */
	void DrawColorCodedValue(
		const std::string& label,
		float valueToCheck,
		const std::string& valueStr,
		const ColorCodedValueConfig& config,
		bool useBullet = true);

	/**
	 * @brief Draws a multi-line tooltip with optional per-line coloring.
	 *
	 * IMPORTANT: This function should only be called from within a tooltip context
	 * (e.g., from within a HoverTooltipWrapper or BeginTooltip/EndTooltip block).
	 * Do not call this function directly without proper tooltip context.
	 *
	 * @param lines The lines of text to display in the tooltip (as std::vector<std::string>).
	 * @param colors Optional per-line colors (if empty, default color is used for all lines).
	 */
	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors = {});

	/**
	 * @brief Draws a multi-line tooltip with optional per-line coloring.
	 *
	 * Expects a vector of {text, color} pairs. Should be called from within a tooltip context.
	 */
	void DrawColoredMultiLineTooltip(const ColoredTextLines& lines);

	// Table sort function for string columns
	using TableSortFunc = std::function<bool(const std::string&, const std::string&, bool)>;
	using TableCellRenderFunc = std::function<void(int row, int col, const std::string& value)>;
	/** @brief Row comparator for one column of a sortable table: (a, b, ascending) -> a before b. */
	template <typename T>
	using TableRowSortFunc = std::function<bool(const T&, const T&, bool)>;

	/** @brief The column a sortable table is sorted by and the direction. column -1 = leave the rows as they are. */
	struct TableSortSpec
	{
		int column = -1;
		bool ascending = true;
	};

	/**
	 * @brief Reads the current table's sort specs (between BeginTable and EndTable).
	 * Falls back to the defaults when the table has no active sort: a table that is not
	 * sortable, or a tristate one the user has un-sorted. Pass -1 to leave the rows as they are then.
	 */
	TableSortSpec ReadTableSortSpec(int defaultColumn = -1, bool defaultAscending = true);

	/** @brief Stable-sorts rows with one comparator, unless the spec says to leave them as they are. */
	template <typename T>
	void SortTableRowsWith(std::vector<T>& rows, const TableSortSpec& spec, const TableRowSortFunc<T>& comparator)
	{
		if (spec.column < 0 || !comparator)
			return;
		const bool ascending = spec.ascending;
		std::stable_sort(rows.begin(), rows.end(), [&comparator, ascending](const T& a, const T& b) {
			return comparator(a, b, ascending);
		});
	}

	/** @brief Stable-sorts rows by the spec's column with that column's comparator; a column without one leaves the rows as they are. */
	template <typename T>
	void SortTableRows(std::vector<T>& rows, const TableSortSpec& spec, const std::vector<TableRowSortFunc<T>>& customSorts)
	{
		if (spec.column < 0 || static_cast<size_t>(spec.column) >= customSorts.size())
			return;
		SortTableRowsWith(rows, spec, customSorts[spec.column]);
	}

	/**
	 * Renders a sortable ImGui table for string tables (vector<vector<string>>).
	 * Always sorts a copy if sorting is needed. Never modifies the input.
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param rows Table data, each row is a vector of strings.
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column (nullptr for default string sort).
	 *        Each function should compare two strings and return true if the first should come before the second.
	 * @param cellRender Optional cell renderer function for custom cell rendering. Signature: (row, col, value)
	 */
	void ShowSortedStringTableStrings(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<std::string>>& rows,
		size_t sortColumn = 0,
		bool ascending = true,
		const std::vector<TableSortFunc>& customSorts = {},
		TableCellRenderFunc cellRender = nullptr);

	/**
	 * Renders a sortable ImGui table for custom row types (vector<T>), sorts in-place.
	 * @tparam T The row type. Must be copyable and compatible with the provided cellRender and customSorts functions.
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param rows Table data, each row is of type T (will be sorted in-place).
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column.
	 *        Each function should compare two rows and return true if the first should come before the second.
	 * @param cellRender Function to render a cell: (rowIdx, colIdx, const T& row).
	 * @param footerRows Optional static footer rows (not sorted, rendered after main rows).
	 * @param outerSize Optional outer size for the table (0,0 = auto-size).
	 */
	template <typename T>
	void ShowSortedStringTableCustom(
		const char* table_id,
		const std::vector<std::string>& headers,
		std::vector<T>& rows,
		size_t sortColumn,
		bool ascending,
		const std::vector<std::function<bool(const T&, const T&, bool)>>& customSorts,
		std::function<void(int, int, const T&)> cellRender,
		const std::vector<T>& footerRows = {},
		const ImVec2& outerSize = ImVec2(0, 0))
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		ImVec2 tableSize = outerSize;
		if (outerSize.y == 0.0f) {
			size_t totalRows = rows.size() + footerRows.size();
			tableSize.y = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>((totalRows < 15) ? totalRows : 15) + 1.2f);
		}
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags, tableSize)) {
			// Set up columns with content-based sizing
			for (size_t i = 0; i < headers.size(); ++i) {
				ImGui::TableSetupColumn(headers[i].c_str());
			}
			ImGui::TableHeadersRow();

			SortTableRows(rows, ReadTableSortSpec(static_cast<int>(sortColumn), ascending), customSorts);

			// Render main (sorted) rows
			for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
				const auto& row = rows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						cellRender(static_cast<int>(rowIdx), static_cast<int>(col), row);
					}
				}
			}

			// Add separator between main rows and footer rows if there are footer rows
			if (!footerRows.empty() && !rows.empty()) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Separator();
			}

			// Render static footer rows (not sorted)
			for (size_t rowIdx = 0; rowIdx < footerRows.size(); ++rowIdx) {
				const auto& row = footerRows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						cellRender(static_cast<int>(rows.size() + rowIdx), static_cast<int>(col), row);
					}
				}
			}
			ImGui::EndTable();
		}
	}

	/**
	 * @brief Compares two version strings (e.g., "1.2.3") numerically.
	 * @param a First version string.
	 * @param b Second version string.
	 * @param ascending True for ascending, false for descending.
	 * @return True if a < b (or a > b if ascending is false).
	 */
	bool VersionStringLess(const std::string& a, const std::string& b, bool ascending = true);

	/**
	 * @brief TableSortFunc for version strings, using VersionStringLess.
	 */
	bool VersionSortComparator(const std::string& a, const std::string& b, bool ascending);

	// A standard string comparator for use with ShowSortedStringTable
	bool StringSortComparator(const std::string& a, const std::string& b, bool ascending);
	/** @brief Draws text with case-insensitive matches of searchTerm drawn in highlightColor. */
	void RenderTextWithHighlights(const std::string& text, const std::string& searchTerm, ImVec4 highlightColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

	// Performance overlay formatting and color helpers
	ImVec4 GetThresholdColor(float value, float good, float warn, ImVec4 goodColor, ImVec4 warnColor, ImVec4 badColor);

	// Search functionality
	/**
	 * @brief Checks if a feature matches the search query.
	 * Case-insensitively (ASCII) searches the short name, English name, translated display name and
	 * translated category label.
	 * @param feat The feature to check
	 * @param searchQuery The search query string
	 * @return True if the feature matches the search query
	 */
	bool FeatureMatchesSearch(Feature* feat, const std::string& searchQuery);

	/**
	 * @brief Generic case-insensitive string matching for search functionality.
	 * @param text The text to search in
	 * @param searchQuery The search query string
	 * @return True if the text matches the search query (case-insensitive)
	 */
	bool StringMatchesSearch(const std::string& text, const std::string& searchQuery);

	/**
	 * @brief Draws a search input field with icon inside a combo dropdown.
	 *
	 * Reusable helper for any combo that needs search/filter functionality.
	 * Draws a text input with a search icon overlay and separator, and
	 * auto-focuses the input when the combo first opens.
	 * Returns an owned copy of the current search text for safe use after
	 * ClearComboSearch may mutate the underlying buffer.
	 *
	 * @param id Stable string literal identifying this search input. Must be a
	 *           finite, static set of IDs — one persistent map entry is created
	 *           per unique id and never removed.
	 * @return Current search text (empty string when no filter is active)
	 */
	std::string DrawComboSearchInput(const char* id);

	/**
	 * @brief Clears the search buffer for a given combo search ID.
	 * Call when selecting an item or when the combo closes.
	 * @param id The same ID passed to DrawComboSearchInput
	 */
	void ClearComboSearch(const char* id);

	/**
	 * @brief Draws text with a breathing/pulsing alpha animation.
	 * @param text The text to display
	 * @param color Base color (default: theme text color)
	 * @param speed Animation speed multiplier (default: 2.5f)
	 * @param minAlpha Minimum alpha value (default: 0.7f)
	 * @param maxAlpha Maximum alpha value (default: 1.0f)
	 */
	void DrawBreathingText(const char* text, std::optional<ImVec4> color = std::nullopt, float speed = 2.5f, float minAlpha = 0.7f, float maxAlpha = 1.0f);

	/**
	 * @brief Returns a color with pulsing brightness animation applied.
	 * @param baseColor The base color to pulse
	 * @param speed Animation speed multiplier (default: 4.0f)
	 * @param minBrightness Minimum brightness multiplier (default: 0.7f)
	 * @param maxBrightness Maximum brightness multiplier (default: 1.0f)
	 * @return The color with pulsing brightness applied (alpha unchanged)
	 */
	ImVec4 GetPulsingColor(const ImVec4& baseColor, float speed = 4.0f, float minBrightness = 0.7f, float maxBrightness = 1.0f);

	/** @brief Tints the next control's frame and border with an opaque marker color to show where its value comes from. */
	void PushTintedFrameStyle(const ImVec4& color);

	/** @brief Pops the style pushed by PushTintedFrameStyle. Always call after the corresponding push. */
	void PopTintedFrameStyle();

	/**
	 * @brief Draws the feature search bar with magnifying glass icon.
	 * @param searchString Reference to the search string to modify
	 * @param availableWidth The available width for the search bar
	 */
	void DrawFeatureSearchBar(std::string& searchString, float availableWidth = 0.0f);

	/**
	 * Provides access to theme-aware UI colors for consistent styling.
	 * These functions return colors from the active theme's StatusPalette,
	 * ensuring consistency with the overall application theme.
	 */
	namespace Colors
	{
		/**
		 * Get standard theme UI colors for consistent theming.
		 * @return Theme colors from StatusPalette
		 */
		ImVec4 GetDefault();   // White - default text (from theme Text)
		ImVec4 GetSuccess();   // Green - success/positive (from theme SuccessColor)
		ImVec4 GetWarning();   // Orange - warning (from theme Warning)
		ImVec4 GetError();     // Red - error/negative (from theme Error)
		ImVec4 GetInfo();      // Blue - informational (from theme InfoColor)
		ImVec4 GetDisabled();  // Gray - disabled items (from theme Disable)
		/** @brief Muted but legible text for live controls; inactive controls use GetDisabled() instead. */
		ImVec4 GetSecondary();
		/** @brief The shared "active/selected" colour for rows, segments, and tabs. */
		ImVec4 GetAccent();
		/** @brief Muted error colour for a save button whose target has unsaved changes. */
		ImVec4 GetUnsaved();

	}

	/**
	 * @brief Glyph colour of a quiet (transparent) icon button: secondary text, brightening to the text colour on
	 * hover and shifting to the accent while selected.
	 * @param hover Eased hover amount, 0-1.
	 * @param selected Eased selected amount, 0-1.
	 */
	ImU32 QuietIconColor(float hover, float selected);

	/** Theme-colored text rendering — self-contained push/text/pop per call. */
	namespace Text
	{
		void Warning(const char* fmt, ...) IM_FMTARGS(1);
		void WrappedWarning(const char* fmt, ...) IM_FMTARGS(1);
		void Error(const char* fmt, ...) IM_FMTARGS(1);
		void WrappedError(const char* fmt, ...) IM_FMTARGS(1);
		void Success(const char* fmt, ...) IM_FMTARGS(1);
		void Info(const char* fmt, ...) IM_FMTARGS(1);
		void WrappedInfo(const char* fmt, ...) IM_FMTARGS(1);
		void Disabled(const char* fmt, ...) IM_FMTARGS(1);
		void WrappedDisabled(const char* fmt, ...) IM_FMTARGS(1);
		void Secondary(const char* fmt, ...) IM_FMTARGS(1);
		void WrappedSecondary(const char* fmt, ...) IM_FMTARGS(1);
	}

	/**
	 * @brief Input handling utilities for ImGui integration
	 *
	 * This namespace provides input mapping functions for converting between different
	 * input systems (Windows Virtual Keys, DirectInput, ImGui) and generating
	 * human-readable key representations for UI display.
	 *
	 * These utilities were extracted from Menu.cpp to improve reusability and
	 * separation of concerns. They are designed to be stateless and thread-safe.
	 */
	namespace Input
	{
		/**
		 * @brief Converts Windows virtual key codes to ImGui key codes
		 *
		 * Translates Windows input events from the VK_* constant format to ImGui's
		 * ImGuiKey enum format for proper input handling in ImGui interfaces.
		 * Supports the full range of keyboard keys, function keys, numpad, and
		 * special keys (modifiers, navigation, etc.).
		 *
		 * @param vkKey Windows virtual key code (VK_* constants from winuser.h)
		 * @return Corresponding ImGuiKey value, or ImGuiKey_None if unmapped
		 *
		 * @note This function handles all standard keyboard keys including:
		 *       - Alphanumeric keys (A-Z, 0-9)
		 *       - Function keys (F1-F12)
		 *       - Modifier keys (Shift, Ctrl, Alt, Windows)
		 *       - Navigation keys (arrows, page up/down, home/end)
		 *       - Numpad keys and operations
		 *       - Special OEM keys (punctuation, brackets, etc.)
		 *
		 * @example
		 * @code
		 * ImGuiKey key = Util::Input::VirtualKeyToImGuiKey(VK_SPACE);
		 * if (key != ImGuiKey_None) {
		 *     ImGui::GetIO().AddKeyEvent(key, true);
		 * }
		 * @endcode
		 */
		ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey);

		/**
		 * @brief Converts DirectInput key codes to Windows virtual key codes
		 *
		 * Translates DirectInput device key codes (DIK_* constants) to standard
		 * Windows virtual key codes (VK_* constants). This is particularly useful
		 * for handling input from DirectInput devices and normalizing them to
		 * the Windows input system.
		 *
		 * @param dikKey DirectInput key code (DIK_* constants from dinput.h)
		 * @return Corresponding Windows virtual key code, or original dikKey if unmapped
		 *
		 * @note This function handles common DirectInput keys including:
		 *       - Arrow keys and navigation
		 *       - Numpad keys and operations
		 *       - Modifier keys (Alt, Ctrl, Windows)
		 *       - Special keys (Delete, Insert, Home, End, Page Up/Down)
		 *
		 * @note For unmapped keys, the function returns the original dikKey value
		 *       as a fallback, allowing for pass-through behavior.
		 *
		 * @example
		 * @code
		 * uint32_t vkKey = Util::Input::DIKToVK(DIK_LEFTARROW);
		 * // vkKey will be VK_LEFT
		 * @endcode
		 */
		uint32_t DIKToVK(uint32_t dikKey);

		/** @brief True when a typed character must be dropped because a slider/drag text entry is active and it isn't a digit, '.' or '-'. */
		bool IsRejectedByNumericTextEntry(uint32_t character);

		/**
		 * @brief Converts key codes to human-readable string representations
		 *
		 * Provides localized, user-friendly key names for display in UI elements
		 * such as settings panels, tooltips, and configuration dialogs. The strings
		 * are suitable for direct display to users.
		 *
		 * @param key Virtual key code to convert (0-255 range)
		 * @return Human-readable key name string, or empty string if key >= 256
		 *
		 * @note Key names include proper formatting and descriptions:
		 *       - "Left Mouse", "Right Mouse", "Middle Mouse" for mouse buttons
		 *       - "Numpad 0", "Numpad +", "Numpad Enter" for numpad keys
		 *       - "Left Shift", "Right Ctrl" for specific modifier keys
		 *       - "Page Up", "Page Down" instead of "Prior", "Next"
		 *       - "Left Arrow", "Up Arrow" for navigation keys
		 *
		 * @note Returns empty string for invalid key codes (>= 256) to prevent
		 *       buffer overrun and provide safe fallback behavior.
		 *
		 * @example
		 * @code
		 * const char* keyName = Util::Input::KeyIdToString(VK_SPACE);
		 * // keyName will be "Space"
		 *
		 * const char* mouseName = Util::Input::KeyIdToString(VK_LBUTTON);
		 * // mouseName will be "Left Mouse"
		 * @endcode
		 */
		const char* KeyIdToString(uint32_t key);

		/**
		 * @brief Converts a key combo (vector of InputCombo) to a human-readable string
		 *
		 * For keyboard-only combos, produces strings like "Ctrl + Shift + A".
		 * For non-keyboard inputs, formats using the packed device+key representation.
		 *
		 * @param combo Vector of InputCombo representing the key combination
		 * @return Human-readable string representation of the combo, or "None" if empty
		 *
		 * @example
		 * @code
		 * std::vector<InputCombo> combo = { InputCombo::Keyboard(VK_CONTROL), InputCombo::Keyboard('A') };
		 * std::string comboStr = Util::Input::KeyIdToString(combo);
		 * // comboStr will be "Control + A"
		 * @endcode
		 */
		std::string KeyIdToString(const std::vector<InputCombo>& combo);
	}

	/**
	 * @brief Renders a searchable combo box with case-insensitive filtering
	 *
	 * Provides a reusable ImGui combo box with built-in search functionality.
	 * When opened, automatically focuses a search input that filters items as you type.
	 * The search is case-insensitive and clears automatically on selection or close.
	 *
	 * @tparam T The value type stored in the map
	 * @param label The label for the combo box
	 * @param selectedName Reference to the currently selected item's name (will be updated on selection)
	 * @param itemMap The map of items to display (key = item name, value = item data)
	 * @return true if a new item was selected, false otherwise
	 *
	 * @note Each combo is identified by its label for independent search state
	 *
	 * @example
	 * @code
	 * std::unordered_map<std::string, MyData> myItems;
	 * std::string selectedName;
	 * MyData* selectedItem = nullptr;
	 *
	 * if (Util::SearchableCombo("Choose Item", selectedName, myItems)) {
	 *     selectedItem = &myItems[selectedName];
	 * }
	 * @endcode
	 */
	template <typename T>
	bool SearchableCombo(const char* label, std::string& selectedName, std::unordered_map<std::string, T>& itemMap)
	{
		bool valueChanged = false;

		if (ImGui::BeginCombo(label, selectedName.c_str())) {
			auto searchText = DrawComboSearchInput(label);

			for (auto& [itemName, item] : itemMap) {
				if (searchText.empty() || StringMatchesSearch(itemName, searchText)) {
					if (ImGui::Selectable(itemName.c_str(), itemName == selectedName)) {
						selectedName = itemName;
						valueChanged = true;
						ClearComboSearch(label);
						break;
					}
				}
			}

			ImGui::EndCombo();
		} else {
			ClearComboSearch(label);
		}

		return valueChanged;
	}

	/**
	 * @brief Renders a table cell with automatic text highlighting and optional tooltip/fallback.
	 * Convenience function for table cell renderers that combines text rendering with highlighting,
	 * tooltips, and fallback text for empty content.
	 * @param text The text to render in the cell (if empty, uses fallbackText)
	 * @param filterText The search filter text for highlighting
	 * @param tooltipText Optional tooltip text (if provided, shows on hover)
	 * @param fallbackText Text to show if primary text is empty (default: "--")
	 * @param highlightColor Color for highlighting (default: yellow)
	 * @param enableWrapping Whether to enable text wrapping for multi-line content (default: true)
	 * @param textColor Optional text color override (default: use default text color)
	 */

	inline void RenderTableCell(const std::string& text, const std::string& filterText,
		const std::string& tooltipText = "", const char* fallbackText = nullptr,
		ImVec4 highlightColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f), bool enableWrapping = true,
		ImVec4 textColor = ImVec4(0, 0, 0, 0))
	{
		const std::string& displayText = text.empty() && fallbackText ? std::string(fallbackText) : text;

		// Apply custom text color if provided
		if (textColor.w > 0.0f) {
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		}

		// Enable text wrapping for the cell content
		if (enableWrapping) {
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		}

		RenderTextWithHighlights(displayText, filterText, highlightColor);

		if (enableWrapping) {
			ImGui::PopTextWrapPos();
		}

		if (!tooltipText.empty() && ImGui::IsItemHovered()) {
			if (auto _tt = HoverTooltipWrapper()) {
				ImGui::Text("%s", tooltipText.c_str());
			}
		}

		// Pop text color if we pushed one
		if (textColor.w > 0.0f) {
			ImGui::PopStyleColor();
		}
	}

	/**
	 * @brief Configuration for a table column (text-only, click handling is row-level)
	 */
	template <typename T>
	struct TableColumnConfig
	{
		std::string header;
		std::string tooltip;
		std::function<std::string(const T&)> getValue;
	};

	/**
	 * @brief Represents different types of input events that can occur on table rows
	 */
	enum class TableInputEventType
	{
		MouseClick,
		MouseDoubleClick,
		KeyPress,
		ContextMenu
	};

	/**
	 * @brief Configuration for a specific input event handler
	 * @tparam T The row type
	 */
	template <typename T>
	struct TableInputEvent
	{
		TableInputEventType type;
		int mouseButton = 0;                     // For mouse events (0=left, 1=right, 2=middle)
		ImGuiKey key = ImGuiKey_None;            // For keyboard events
		std::string label;                       // Display label for context menus
		std::function<void(const T&)> callback;  // Action to perform
		bool enabled = true;                     // Whether this event is currently enabled

		TableInputEvent(TableInputEventType t, std::function<void(const T&)> cb,
			const std::string& lbl = "", int btn = 0, ImGuiKey k = ImGuiKey_None) :
			type(t), mouseButton(btn), key(k), label(lbl), callback(cb) {}
	};

	/**
	 * @brief Manages the state and logic for table filtering
	 * @tparam T The row type
	 */
	template <typename T>
	struct TableFilterState
	{
		std::string filterText;
		int searchColumn = 0;  // 0 = All Columns, 1+ = specific column
		std::function<std::vector<std::string>(const T&)> getFilterableFields;

		TableFilterState(std::function<std::vector<std::string>(const T&)> fieldsFunc) :
			getFilterableFields(fieldsFunc) {}

		/**
		 * @brief Apply filtering to the original rows and return filtered results
		 */
		std::vector<T> ApplyFilter(const std::vector<T>& originalRows) const
		{
			if (filterText.empty()) {
				return originalRows;
			}

			std::vector<T> filteredRows;
			std::string filterLower = filterText;
			std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

			for (const auto& row : originalRows) {
				bool passesFilter = false;
				auto filterableFields = getFilterableFields(row);

				if (searchColumn == 0) {  // All Columns
					for (const auto& field : filterableFields) {
						std::string fieldLower = field;
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						if (fieldLower.find(filterLower) != std::string::npos) {
							passesFilter = true;
							break;
						}
					}
				} else {  // Specific column (searchColumn is 1-indexed for columns)
					int columnIndex = searchColumn - 1;
					if (columnIndex >= 0 && static_cast<size_t>(columnIndex) < filterableFields.size()) {
						std::string fieldLower = filterableFields[columnIndex];
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						passesFilter = fieldLower.find(filterLower) != std::string::npos;
					}
				}

				if (passesFilter) {
					filteredRows.push_back(row);
				}
			}

			return filteredRows;
		}

		/**
		 * @brief Render the filter UI controls
		 */
		void RenderControls(const std::vector<std::string>& columnHeaders)
		{
			char filterBuffer[256] = { 0 };
			strncpy_s(filterBuffer, filterText.c_str(), sizeof(filterBuffer) - 1);

			ImGui::InputText("Filter", filterBuffer, IM_ARRAYSIZE(filterBuffer));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Filter table by the selected column. Case-insensitive.");
			}

			// Create search column options
			std::vector<std::string> searchOptions = { "All Columns" };
			for (const auto& col : columnHeaders) {
				searchOptions.push_back(col);
			}
			std::vector<const char*> searchOptionsCStr;
			for (const auto& option : searchOptions) {
				searchOptionsCStr.push_back(option.c_str());
			}

			ImGui::Combo("Search In", &searchColumn, searchOptionsCStr.data(), static_cast<int>(searchOptionsCStr.size()));

			// Update filter text from buffer
			filterText = filterBuffer;
		}
	};

	/**
	 * @brief Enhanced filtered table with general input event support and theme integration
	 * @tparam T The row type
	 * @param table_id Unique ImGui table ID
	 * @param columns Column configurations (text-only, click handling is row-level)
	 * @param originalRows Original table data (not modified - filtering creates a copy)
	 * @param sortColumn Default sort column index
	 * @param ascending Default sort direction
	 * @param customSorts Vector of custom comparator functions, one per column
	 * @param filterState Filter state management
	 * @param inputEvents Vector of input event handlers for row interactions
	 * @param getRowTooltip Optional function to get tooltip for entire row
	 * @param getRowBgColor Optional function to get background color for row (for highlighting blocked/disabled items)
	 * @param getRowTextColor Optional function to get text color for row (for highlighting blocked/disabled items)
	 * @param tableHeight Maximum height for the table container (0 = auto)
	 */
	template <typename T>
	void ShowInteractiveTable(
		const char* table_id,
		const std::vector<TableColumnConfig<T>>& columns,
		const std::vector<T>& originalRows,
		size_t sortColumn,
		bool ascending,
		const std::vector<std::function<bool(const T&, const T&, bool)>>& customSorts,
		TableFilterState<T>& filterState,
		const std::vector<TableInputEvent<T>>& inputEvents = {},
		std::function<std::string(const T&)> getRowTooltip = nullptr,
		std::function<ImVec4(const T&)> getRowBgColor = nullptr,
		std::function<ImVec4(const T&)> getRowTextColor = nullptr,
		float tableHeight = 400.0f)
	{
		// Render filter controls
		filterState.RenderControls([&]() {
			std::vector<std::string> headers;
			for (const auto& col : columns) {
				headers.push_back(col.header);
			}
			return headers;
		}());

		// Apply filtering
		auto filteredRows = filterState.ApplyFilter(originalRows);

		// Handle filter change scrolling
		static std::string previousFilterText = "";
		bool filterChanged = (filterState.filterText != previousFilterText);
		if (filterChanged) {
			ImGui::SetScrollHereY(0.5f);
			previousFilterText = filterState.filterText;
		}

		// Constrain table height to prevent infinite scrolling appearance
		std::string containerId = std::string(table_id) + "_Container";
		ImGui::BeginChild(containerId.c_str(), ImVec2(0, tableHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 2));

		// The table scrolls inside the container, so its header row stays in view
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable(table_id, static_cast<int>(columns.size()), flags)) {
			// Set up columns
			for (size_t i = 0; i < columns.size(); ++i) {
				ImGui::TableSetupColumn(columns[i].header.c_str());
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			SortTableRows(filteredRows, ReadTableSortSpec(static_cast<int>(sortColumn), ascending), customSorts);

			// Render rows with input event support
			for (size_t rowIdx = 0; rowIdx < filteredRows.size(); ++rowIdx) {
				const auto& row = filteredRows[rowIdx];
				ImGui::TableNextRow();

				// Set custom row background color if provided (for blocked/disabled items)
				if (getRowBgColor) {
					ImVec4 bgColor = getRowBgColor(row);
					if (bgColor.w > 0.0f) {  // Only set if color has alpha > 0
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(bgColor));
					}
				}

				// Render all columns first to establish proper row layout
				for (size_t col = 0; col < columns.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					const auto& column = columns[col];

					// All columns are now text-only with highlighting
					std::string value = column.getValue(row);
					ImVec4 textColor = getRowTextColor ? getRowTextColor(row) : ImVec4(0, 0, 0, 0);
					Util::RenderTableCell(value, filterState.filterText, column.tooltip, nullptr, ImVec4(1.0f, 1.0f, 0.0f, 1.0f), true, textColor);
				}

				// Now create the invisible button that covers the entire rendered row
				// Get the position after all cells are rendered
				ImVec2 rowMin = ImGui::GetItemRectMin();
				ImVec2 rowMax = ImGui::GetItemRectMax();

				// Find the actual row boundaries by checking all columns
				float minY = FLT_MAX;
				float maxY = -FLT_MAX;
				float minX = FLT_MAX;
				float maxX = -FLT_MAX;

				for (size_t col = 0; col < columns.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					ImVec2 cellMin = ImGui::GetItemRectMin();
					ImVec2 cellMax = ImGui::GetItemRectMax();

					minX = std::min(minX, cellMin.x);
					maxX = std::max(maxX, cellMax.x);
					minY = std::min(minY, cellMin.y);
					maxY = std::max(maxY, cellMax.y);
				}

				ImVec2 rowStartPos = ImVec2(minX, minY);
				ImVec2 rowSize = ImVec2(maxX - minX, maxY - minY);

				// Position the button absolutely over the rendered row
				ImGui::SetCursorScreenPos(rowStartPos);
				ImGui::PushID(static_cast<int>(rowIdx));

				std::string buttonId = "##row_" + std::to_string(rowIdx);
				ImGui::InvisibleButton(buttonId.c_str(), rowSize);

				// Handle input events on the invisible button
				for (const auto& event : inputEvents) {
					if (!event.enabled)
						continue;

					bool shouldTrigger = false;
					switch (event.type) {
					case TableInputEventType::MouseClick:
						shouldTrigger = ImGui::IsItemClicked() && event.mouseButton == 0;  // Left click
						break;
					case TableInputEventType::MouseDoubleClick:
						shouldTrigger = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(event.mouseButton);
						break;
					case TableInputEventType::KeyPress:
						shouldTrigger = ImGui::IsItemFocused() && ImGui::IsKeyPressed(event.key);
						break;
					case TableInputEventType::ContextMenu:
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(event.mouseButton)) {
							std::string popupId = "row_context_" + std::to_string(rowIdx);
							ImGui::OpenPopup(popupId.c_str());
						}
						break;
					}

					if (shouldTrigger && event.callback) {
						event.callback(row);
					}
				}

				// Render context menus
				for (const auto& event : inputEvents) {
					if (event.type == TableInputEventType::ContextMenu) {
						std::string popupId = "row_context_" + std::to_string(rowIdx);
						if (ImGui::BeginPopup(popupId.c_str())) {
							if (ImGui::MenuItem(event.label.c_str()) && event.callback) {
								event.callback(row);
							}
							ImGui::EndPopup();
						}
					}
				}

				// Row tooltip
				if (getRowTooltip && ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						std::string tooltip = getRowTooltip(row);
						ImGui::Text("%s", tooltip.c_str());
					}
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::PopStyleVar();
		ImGui::EndChild();
	}

	/**
	 * @brief Unified input recording widget
	 *
	 * Handles recording of multi-key sequences for keyboard and mouse.
	 * Supports modifiers, combo sequences, and device-specific rendering.
	 *
	 * @param label The label for the input setting
	 * @param combo The vector of InputCombo to record into
	 * @param isRecording Reference to boolean tracking active recording state
	 * @param recordingLabel Unique label ID for the recording button
	 *
	 * @return true if the combo was modified
	 */
	bool InputComboWidget(
		const char* label,
		std::vector<InputCombo>& combo,
		bool& isRecording,
		const char* recordingLabel);

	/**
	 * @brief Displays a DLL version information table with a clickable folder link.
	 *
	 * Shows a selectable label that opens the given directory when clicked,
	 * followed by a sortable table of DLL names and their versions.
	 * This is a general-purpose utility for any feature that distributes DLLs
	 * and wants to expose their versions in the settings UI.
	 *
	 * @param label  Display label for the clickable folder link.
	 * @param pluginDir  Wide string path to the plugin directory (opened on click).
	 * @param dllVersions  Vector of (name, version) pairs to display.
	 * @param tableId  Unique ImGui table identifier.
	 */
	void DrawDllVersionTable(
		const char* label,
		const wchar_t* pluginDir,
		const std::vector<std::pair<std::string, std::string>>& dllVersions,
		const char* tableId);

	/**
	 * @brief Down-arrow joined to the field just drawn that opens a popup anchored under both.
	 * Call right after the field (leave GetFrameHeight() of room for the arrow), then BeginPopup(popupId) immediately.
	 * @param disabledTooltip Shown instead of tooltip while disabled
	 */
	void DropdownArrowButton(const char* popupId, const char* tooltip, bool disabled = false, const char* disabledTooltip = nullptr);
}  // namespace Util

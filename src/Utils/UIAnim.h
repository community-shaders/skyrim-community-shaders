#pragma once

#include <cstdint>
#include <imgui.h>
#include <limits>

struct ImRect;

/**
 * @brief Small UI animation toolkit: per-ID animated floats kept in ImGui's state storage, easing curves, a
 * hover / press / selected blend for items and colour helpers. Every function honours the "UI animations" setting
 * by snapping to the target, and the animation speed setting by scaling rates and durations. Values are stored in the current window's state storage, so call
 * the same helper with the same ID from the same window each frame.
 */
namespace Util::Anim
{
	/** @brief True when the user turned UI animations off (Settings > Behavior > UI animations). */
	bool ReducedMotion();

	/** @brief The user's animation speed multiplier, clamped to the supported range; scales every rate and duration here. */
	float Speed();

	/** @brief Cubic ease-out of t in [0, 1]: quick start, soft landing. */
	float EaseOutCubic(float t);
	/** @brief Smooth ease-in-out (smoothstep) of t in [0, 1]. */
	float EaseInOut(float t);

	/**
	 * @brief One frame-rate independent smoothing step of a value you keep yourself: `current` moves toward `target`
	 * by 1 - exp(-rate * dt), and snaps once within the animation epsilon (or at once under reduced motion).
	 */
	float Smooth(float current, float target, float rate);

	/**
	 * @brief Moves the stored value for `id` toward `target` with frame-rate independent exponential smoothing.
	 * @param id Key in the current window's state storage; build it with ImGui::GetID.
	 * @param target Value to approach.
	 * @param rate Approach speed per second; higher is snappier.
	 * @param initial Value used the first time the id is seen; NaN starts at the target (no animation on first sight).
	 * @return The new value.
	 */
	float Approach(ImGuiID id, float target, float rate, float initial = std::numeric_limits<float>::quiet_NaN());

	/** @brief Approach() toward 1 or 0 using the standard hover rate. */
	float Toggle(ImGuiID id, bool on, float rate = 0.0f);

	/** @brief Animated hover / press / selected state of one item, each in [0, 1]. */
	struct Blend
	{
		float hover = 0.0f;
		float press = 0.0f;
		float selected = 0.0f;
		/** @brief Strongest of the three, for a single highlight alpha. */
		float Highlight() const { return hover > press ? (hover > selected ? hover : selected) : (press > selected ? press : selected); }
	};

	/**
	 * @brief Animated hover / press / selected blend for an item.
	 * @param id Item id (e.g. the one the widget was created with).
	 */
	Blend ItemBlend(ImGuiID id, bool hovered, bool held, bool selected);

	/**
	 * @brief Seconds-based fade / slide-in for something that appears: 0 until `delay` has passed since the item
	 * (re)appeared, then eased up to 1 over `duration`. "Appeared" means it was not asked for in the previous frame,
	 * so it restarts every visit.
	 * @param key Stable id of the page or card.
	 * @param delay Seconds to wait before starting (stagger).
	 * @param duration Seconds to run; defaults to the page-enter time.
	 */
	float EnterProgress(ImGuiID key, float delay = 0.0f, float duration = 0.0f);

	/** @brief Animated rectangle of a selection highlight, in screen space. */
	struct GlideRect
	{
		ImVec2 min;
		ImVec2 max;
	};

	/**
	 * @brief One highlight shape that eases between the rectangles of a list's selected items, so a changed selection
	 * glides instead of jumping. Call once per frame, with the selected item's rect, while the list is shown; it
	 * snaps to the target on the first call and after any frame without a call, and under reduced motion. The shape
	 * is stored relative to the window's scrolled content, so scrolling does not drag it.
	 * @param listId Stable id of the list (build it with ImGui::GetID in the list's window).
	 * @param rate Approach speed per second; 0 uses the standard glide rate.
	 */
	GlideRect SelectionGlide(ImGuiID listId, const ImVec2& targetMin, const ImVec2& targetMax, float rate = 0.0f);

	/**
	 * @brief Staggered entrance for a group of siblings (cards, sections): each Next() returns the eased 0..1
	 * progress of the next member, which starts a stagger step after the previous one and restarts every time the
	 * group is shown again.
	 */
	class EnterGroup
	{
	public:
		/**
		 * @param groupId Stable id of the group.
		 * @param stagger Seconds between members; 0 uses the standard card stagger.
		 */
		explicit EnterGroup(ImGuiID groupId, float stagger = 0.0f);
		/** @brief Progress of the next member; `memberId` must be stable for it (e.g. its child id). */
		float Next(ImGuiID memberId);

	private:
		ImGuiID groupId;
		float stagger;
		int index = 0;
	};

	/** @brief Slow 0..1 sine pulse for drawing attention; stays 0 under reduced motion. */
	float Pulse(float periodSeconds = 0.0f);

	/** @brief Linear blend of two colours. */
	ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t);
	/** @brief Linear blend of two packed colours. */
	ImU32 LerpU32(ImU32 a, ImU32 b, float t);
	/** @brief `color` with its alpha multiplied by `alpha`. */
	ImVec4 ScaleAlpha(const ImVec4& color, float alpha);
	/** @brief `color` with its alpha multiplied by `alpha`, packed. */
	ImU32 ScaleAlphaU32(ImU32 color, float alpha);

	/** @brief Fills a rect with a style colour at `strength` (a 0-1 alpha multiplier); draws nothing at zero. */
	void FillLayer(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, ImGuiCol color, float strength);

	/** @brief The hover / press / selected layers of a Blend as three stacked fills, each in its own style colour. */
	void FillBlend(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, const Blend& blend,
		ImGuiCol hoverColor = ImGuiCol_FrameBgHovered, ImGuiCol pressColor = ImGuiCol_ButtonActive, ImGuiCol selectedColor = ImGuiCol_Header);

	/**
	 * @brief Pushes ImGuiStyleVar_Alpha for something fading in, restarting each time `key` appears.
	 * @param duration Seconds to run; 0 uses the popup time.
	 * @param fromAlpha Alpha factor at the start; 0 fades in from nothing.
	 * @return The eased progress, for scaling a panel as well.
	 */
	float BeginFadeIn(ImGuiID key, float duration = 0.0f, float fromAlpha = 0.0f);
	/** @brief Pops what BeginFadeIn pushed. */
	void EndFadeIn();

	/**
	 * @brief Fades a page in whenever it is opened: a short fade from a partial alpha, with no movement, so layout
	 * and input are untouched. Scope it around the page's drawing.
	 */
	class PageFade
	{
	public:
		/** @param pageId Stable id of the page; a different id starts a new fade. */
		explicit PageFade(ImGuiID pageId);
		~PageFade();
		PageFade(const PageFade&) = delete;
		PageFade& operator=(const PageFade&) = delete;
	};

	/** @brief Signature of ImGui::SliderScalar, which every slider widget funnels into. */
	using SliderScalarFn = bool (*)(const char*, ImGuiDataType, void*, const void*, const void*, const char*, ImGuiSliderFlags);

	/**
	 * @brief Slider body whose grab glides. The value itself changes the instant the user acts, so nothing is delayed;
	 * ImGui's grab is hidden and redrawn gliding to the value's position, which covers a click that jumps a long way
	 * on a wide slider, a drag (followed closely) and a value changed from outside (a preset, restore, undo). Hooked
	 * into ImGui::SliderScalar, so every slider variant gets it with no per-widget opt-in. Float and int sliders glide;
	 * logarithmic sliders, other data types, and everything while UI animations are off, pass straight through.
	 */
	bool EasedSliderScalar(SliderScalarFn real, const char* label, ImGuiDataType dataType, void* data, const void* min,
		const void* max, const char* format, ImGuiSliderFlags flags);

	/** @brief Signature of ImGui::Checkbox. */
	using CheckboxFn = bool (*)(const char*, bool*);

	/**
	 * @brief Checkbox whose check mark fades and pops in and out instead of switching. ImGui's own mark is hidden and
	 * redrawn eased; the frame, hover and click behaviour are ImGui's. Hooked into ImGui::Checkbox (see
	 * SceneWidgetInterceptor), so every checkbox gets it with no per-widget opt-in. Mixed-value checkboxes, and
	 * everything while UI animations are off, pass straight through.
	 */
	bool EasedCheckbox(CheckboxFn real, const char* label, bool* value);

	/** @brief Signature of ImGui::Selectable, which every list row (combo items, menu items, list boxes) funnels into. */
	using SelectableFn = bool (*)(const char*, bool, ImGuiSelectableFlags, const ImVec2&);

	/**
	 * @brief Selectable body that draws the list highlights instead of ImGui's per-row fills. The selected row keeps its
	 * own fill and accent bar in place; the hover highlight is one shape per list that glides after the mouse, darkens
	 * while pressed, and fades out where it is when the mouse is over nothing clickable. ImGui's own hover and selected
	 * fills are hidden so nothing doubles up. Rows of one list are the
	 * Selectables of one window, so every list gets it with no opt-in. Hooked into ImGui::Selectable (see UIHooks);
	 * passes straight through while UI animations are off.
	 */
	bool GlideSelectable(SelectableFn real, const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size);

	/**
	 * @brief Scope for rows that draw their own highlight (a custom pill, say): Selectables inside it skip the list
	 * highlight. Everything else gets it automatically.
	 */
	class NoListHighlight
	{
	public:
		NoListHighlight();
		~NoListHighlight();
		NoListHighlight(const NoListHighlight&) = delete;
		NoListHighlight& operator=(const NoListHighlight&) = delete;
	};

	/** @brief Signature of ImGui::BeginPopupEx, which BeginPopup, BeginPopupContextItem and BeginMenu funnel into. */
	using BeginPopupExFn = bool (*)(ImGuiID, ImGuiWindowFlags);
	/** @brief Signature of ImGui::BeginComboPopup, the popup of every combo. */
	using BeginComboPopupFn = bool (*)(ImGuiID, const ImRect&, ImGuiComboFlags);
	/** @brief Signature of ImGui::EndPopup. */
	using EndPopupFn = void (*)();

	/**
	 * @brief BeginPopupEx body that fades a popup in while it is open, and keeps a copy of what it drew so the copy
	 * can fade out over the screen once ImGui closes the popup (replayed whenever the same popup is asked for while
	 * closed, which BeginPopup and BeginPopupContextItem callers do every frame). Hooked into ImGui::BeginPopupEx
	 * (see UIHooks), so every popup and menu gets it with no opt-in; modals, which draw their own entrance, never
	 * pass through it. Pass straight through while UI animations are off.
	 */
	bool FadedBeginPopup(BeginPopupExFn real, ImGuiID id, ImGuiWindowFlags flags);
	/** @brief BeginComboPopup body that fades the dropdown in; the fade-out copy is not kept for combos. */
	bool FadedBeginComboPopup(BeginComboPopupFn real, ImGuiID id, const ImRect& bb, ImGuiComboFlags flags);
	/** @brief EndPopup body that unwinds the fade FadedBeginPopup or FadedBeginComboPopup started, if any. */
	void FadedEndPopup(EndPopupFn real);
}

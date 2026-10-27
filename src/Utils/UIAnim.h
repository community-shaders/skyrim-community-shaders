#pragma once

#include <cstdint>
#include <imgui.h>
#include <limits>

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

	/**
	 * @brief Counter that advances whenever the content pane changes page. Widgets that ease their values drop their
	 * state when it changes, so a different page's controls never animate from the previous page's values.
	 */
	std::uint32_t PageEpoch();

	/** @brief Signature of ImGui::SliderScalar, which every slider widget funnels into. */
	using SliderScalarFn = bool (*)(const char*, ImGuiDataType, void*, const void*, const void*, const char*, ImGuiSliderFlags);

	/**
	 * @brief Slider body with automatic easing. When the bound value changes from outside (a preset, restore, undo,
	 * paste), the slider's grab and number ease to the new value instead of jumping. Direct edits are never delayed:
	 * while the user holds the slider, or when the value is at rest, @p real runs on the caller's value untouched.
	 * Hooked into ImGui::SliderScalar, so every slider variant gets it with no per-widget opt-in. Float and int
	 * sliders ease; other data types, and everything while UI animations are off, pass straight through.
	 */
	bool EasedSliderScalar(SliderScalarFn real, const char* label, ImGuiDataType dataType, void* data, const void* min,
		const void* max, const char* format, ImGuiSliderFlags flags);

	/** @brief Signature of ImGui::Selectable, which every list row (combo items, menu items, list boxes) funnels into. */
	using SelectableFn = bool (*)(const char*, bool, ImGuiSelectableFlags, const ImVec2&);

	/**
	 * @brief Selectable body that draws one gliding highlight per list instead of a fill per row. The highlight eases
	 * to whichever row the mouse is over, darkens while pressed, and rests on the selected row when the mouse is
	 * elsewhere; ImGui's own hover and selected fills are hidden so nothing doubles up. Rows of one list are the
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

	/**
	 * @brief Fades a popup out after it closes. ImGui drops a popup's window the frame it closes, so while the popup is
	 * open this keeps a copy of what it drew, and once it closes the copy is replayed over the screen, fading away.
	 * Declare it before BeginPopup / BeginPopupModal in the same ID scope, every frame, whether or not the popup is
	 * open. PopupFade includes it; use it alone for a popup that animates its own entrance.
	 */
	class PopupGhost
	{
	public:
		/**
		 * @param popupId The id string given to BeginPopup / BeginPopupModal.
		 * @param modal True for BeginPopupModal, whose window is named by the string itself rather than by its id.
		 */
		explicit PopupGhost(const char* popupId, bool modal = false);
		~PopupGhost();
		PopupGhost(const PopupGhost&) = delete;
		PopupGhost& operator=(const PopupGhost&) = delete;

	private:
		ImGuiID id = 0;
		bool modal = false;
		const char* popupId = nullptr;
	};

	/**
	 * @brief Fades a popup in while it is open, and out once it closes. Declare it before BeginPopup (which may
	 * return false); it pushes nothing while the popup is closed, and pops when it leaves scope.
	 */
	class PopupFade
	{
	public:
		/** @param popupId The id string given to BeginPopup, in the same ID scope. */
		explicit PopupFade(const char* popupId);
		~PopupFade();
		PopupFade(const PopupFade&) = delete;
		PopupFade& operator=(const PopupFade&) = delete;

	private:
		PopupGhost ghost;
		bool pushed = false;
	};
}

#pragma once

#include "../I18n/I18n.h"
#include "Util.h"
#include "Widget.h"
#include <cctype>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Forward declarations
class EditorWindow;

/**
 * @brief Case-insensitive substring search.
 * @param a_string    The string to search within.
 * @param a_substring The substring to look for.
 * @return True if a_substring appears in a_string (case-insensitive).
 */
bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring);

// ============================================================================
// DebouncedTracker - Consolidates debounced value tracking with undo support
// ============================================================================
template <typename T>
class DebouncedTracker
{
public:
	static constexpr double DefaultDebounceDelay = 2.0;

	/**
	 * @brief Record that a tracked value has changed.
	 * @param key         Unique identifier for the tracked value.
	 * @param value       The new value.
	 * @param currentTime Current time in seconds.
	 * @return True if this is a new interaction session.
	 */
	bool OnValueChanged(const std::string& key, const T& value, double currentTime)
	{
		pendingValues[key] = value;
		lastChangeTime[key] = currentTime;
		return true;
	}

	/**
	 * @brief Update active/inactive state for a tracked control. Returns true when an undo snapshot should be pushed.
	 * @param key           Unique identifier for the tracked value.
	 * @param isNowActive   Whether the control (e.g. slider) is currently being interacted with.
	 * @param currentTime   Current time in seconds.
	 * @param debounceDelay Idle time before resetting the undo session, in seconds.
	 * @return True if undo state should be pushed (first activation of a new session).
	 */
	bool UpdateActiveState(const std::string& key, bool isNowActive, double currentTime, double debounceDelay = DefaultDebounceDelay)
	{
		bool isPreviouslyActive = wasActive[key];
		wasActive[key] = isNowActive;

		// Push undo state only once when slider becomes active
		if (isNowActive && !isPreviouslyActive && !undoPushedForSession[key]) {
			undoPushedForSession[key] = true;
			return true;  // Signal to push undo
		}

		// Reset undo flag when slider is released and idle
		if (!isNowActive && undoPushedForSession[key]) {
			auto it = lastChangeTime.find(key);
			if (it == lastChangeTime.end() || currentTime - it->second >= debounceDelay) {
				undoPushedForSession[key] = false;
			}
		}

		return false;
	}

	/**
	 * @brief Retrieve values that have been idle for the debounce delay and are ready for tracking.
	 * @param currentTime   Current time in seconds.
	 * @param debounceDelay Minimum idle time before a value is considered complete.
	 * @return Vector of (key, value) pairs that have settled.
	 */
	std::vector<std::pair<std::string, T>> GetCompletedEntries(double currentTime, double debounceDelay = DefaultDebounceDelay)
	{
		std::vector<std::pair<std::string, T>> completed;
		std::vector<std::string> keysToRemove;

		for (const auto& [key, changeTime] : lastChangeTime) {
			if (currentTime - changeTime >= debounceDelay) {
				auto it = pendingValues.find(key);
				if (it != pendingValues.end()) {
					completed.emplace_back(key, it->second);
					keysToRemove.push_back(key);
				}
			}
		}

		for (const auto& key : keysToRemove) {
			pendingValues.erase(key);
			lastChangeTime.erase(key);
		}

		return completed;
	}

	/** @brief Discard all pending values, timestamps, and session state. */
	void Clear()
	{
		pendingValues.clear();
		lastChangeTime.clear();
		wasActive.clear();
		undoPushedForSession.clear();
	}

private:
	std::map<std::string, T> pendingValues;
	std::map<std::string, double> lastChangeTime;
	std::map<std::string, bool> wasActive;
	std::map<std::string, bool> undoPushedForSession;
};

// ============================================================================
// Sticky Header Utilities - Keep widget headers fixed while content scrolls
// ============================================================================

/**
 * @brief Window flags to disable scrolling on the parent window when using a sticky header.
 *
 * Add to ImGui::Begin() or ImGui::BeginChild() window_flags parameter.
 */
constexpr ImGuiWindowFlags kStickyHeaderFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

/**
 * @brief Begin a scrollable content region below a sticky header. Fills remaining space.
 * @param id ImGui child window ID.
 * @return True if the child region is visible.
 */
inline bool BeginScrollableContent(const char* id = "##ScrollContent") { return ImGui::BeginChild(id, ImVec2(0, 0)); }

/** @brief End the scrollable content region started with BeginScrollableContent. */
inline void EndScrollableContent() { ImGui::EndChild(); }

// ============================================================================
// Widget Window Defaults - DPI-aware sizing for all editor widget windows
// ============================================================================

namespace WidgetDefaults
{
	constexpr float kMinWidth = 200.0f;
	constexpr float kMinHeight = 150.0f;
	constexpr float kInitialWidth = 580.0f;
	constexpr float kInitialHeight = 580.0f;
	constexpr float kSliderWidthRatio = 0.6f;  ///< Fraction of available width for slider/input controls
	constexpr float kTODLabelWidth = 150.0f;   ///< Default label column width for time-of-day tables
}

/**
 * @brief Apply standard DPI-scaled size constraints and initial size for widget windows.
 *
 * Call before ImGui::Begin(). Uses per-widget-type size tracking so all instances of a
 * widget type share window dimensions. Respects EditorWindow::resetLayout to re-apply defaults on demand.
 * @param widgetType The widget type name used as a key for size tracking.
 */
void SetupWidgetWindowDefaults(const char* widgetType);

/**
 * @brief Update stored per-widget-type size from the current ImGui window.
 *
 * Call after ImGui::Begin(). Only updates when the window is focused so resize
 * applies to future windows of the same type.
 * @param widgetType The widget type name used as a key for size tracking.
 */
void UpdateWidgetTypeSize(const char* widgetType);

/** @brief Reset all per-widget-type sizes to defaults. Called when resetLayout is triggered. */
void ResetWidgetTypeSizes();

/** @brief Serialize per-widget-type sizes into a JSON object for persistence. */
json GetWidgetTypeSizesJson();

/**
 * @brief Restore per-widget-type sizes from a previously serialized JSON object.
 * @param j The JSON object containing widget type sizes.
 */
void SetWidgetTypeSizesFromJson(const json& j);

// ============================================================================
// PropertyDrawer - Unified table-based property drawing with search support
// ============================================================================
namespace PropertyDrawer
{
	/**
	 * @brief Begin a two-column property table. Call before drawing properties.
	 * @param tableId    ImGui ID for the table.
	 * @param labelWidth Fixed width for the label column in pixels.
	 * @return True if the table was created successfully.
	 */
	bool BeginTable(const char* tableId, float labelWidth = 200.0f);

	/** @brief End the property table started with BeginTable. */
	void EndTable();

	/** @brief Draw a separator row in the property table. */
	void DrawSeparator();

	/**
	 * @brief Draw a vertically-centered label in the first table column.
	 * @param label The label text. Call between TableNextRow and drawing the value.
	 */
	void DrawLabel(const char* label);

	/**
	 * @brief Draw a float slider property row.
	 * @param label  Display label for the property.
	 * @param value  Reference to the float value to edit.
	 * @param minVal Minimum slider value.
	 * @param maxVal Maximum slider value.
	 * @param format Printf-style format string for display.
	 * @return True if the value was changed.
	 */
	bool DrawFloat(const char* label, float& value, float minVal, float maxVal,
		const char* format = "%.3f");

	/**
	 * @brief Draw an integer slider property row.
	 * @param label  Display label for the property.
	 * @param value  Reference to the int value to edit.
	 * @param minVal Minimum slider value.
	 * @param maxVal Maximum slider value.
	 * @return True if the value was changed.
	 */
	bool DrawInt(const char* label, int& value, int minVal, int maxVal);

	/**
	 * @brief Draw a colour picker property row.
	 * @param label Display label for the property.
	 * @param value Reference to the RGB colour to edit.
	 * @return True if the colour was changed.
	 */
	bool DrawColor(const char* label, float3& value);

	/**
	 * @brief Draw a checkbox property row.
	 * @param label Display label for the property.
	 * @param value Reference to the bool to edit.
	 * @return True if the value was changed.
	 */
	bool DrawCheckbox(const char* label, bool& value);
}  // namespace PropertyDrawer

// ============================================================================
// WidgetFactory - Template-based widget creation for EditorWindow::SetupResources
// ============================================================================
namespace WidgetFactory
{
	/**
	 * @brief Translate a widget type name to its localized display string.
	 * @param widgetTypeName The internal widget type name (e.g. "Weather", "ImageSpace").
	 * @return Localized display name for the widget type.
	 */
	inline const char* TranslateWidgetTypeName(std::string_view widgetTypeName)
	{
		if (widgetTypeName == "Weather")
			return T("cs_editor.widget_type_weather", "Weather");
		if (widgetTypeName == "ImageSpace")
			return T("cs_editor.widget_type_imagespace", "ImageSpace");
		if (widgetTypeName == "Lighting")
			return T("cs_editor.widget_type_lighting", "Lighting");
		if (widgetTypeName == "Cell Lighting")
			return T("cs_editor.widget_type_cell_lighting", "Cell Lighting");
		if (widgetTypeName == "Volumetric Lighting")
			return T("cs_editor.widget_type_volumetric_lighting", "Volumetric Lighting");
		if (widgetTypeName == "Precipitation")
			return T("cs_editor.widget_type_precipitation", "Precipitation");
		if (widgetTypeName == "Lens Flare")
			return T("cs_editor.widget_type_lens_flare", "Lens Flare");
		if (widgetTypeName == "Visual Effect")
			return T("cs_editor.widget_type_visual_effect", "Visual Effect");

		// Fallback: use T() to cache a stable null-terminated copy
		return T(std::string(widgetTypeName).c_str(), std::string(widgetTypeName).c_str());
	}

	/**
	 * @brief Populate a widget container from the game's form array.
	 *
	 * WidgetType must have a constructor taking FormType*.
	 * @tparam WidgetType The widget class to instantiate per form.
	 * @tparam FormType   The game form type to enumerate.
	 * @param  widgets    The container to append new widget instances to.
	 */
	template <typename WidgetType, typename FormType>
	void PopulateWidgets(std::vector<std::unique_ptr<Widget>>& widgets)
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return;

		auto& formArray = dataHandler->GetFormArray<FormType>();
		widgets.reserve(widgets.size() + formArray.size());

		for (auto form : formArray) {
			if (form) {
				auto widget = std::make_unique<WidgetType>(form);
				widget->CacheFormData();
				widget->Load(false);
				widgets.push_back(std::move(widget));
			}
		}
	}

	/**
	 * @brief Populate a widget container with SimpleFormWidget instances for cache-only purposes.
	 * @tparam FormType The game form type to enumerate.
	 * @param  widgets  The container to append new SimpleFormWidget instances to.
	 */
	template <typename FormType>
	void PopulateSimpleWidgets(std::vector<std::unique_ptr<Widget>>& widgets)
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return;

		auto& formArray = dataHandler->GetFormArray<FormType>();
		widgets.reserve(widgets.size() + formArray.size());

		for (auto form : formArray) {
			if (form) {
				auto widget = std::make_unique<SimpleFormWidget>();
				widget->form = form;
				widget->CacheFormData();
				widgets.push_back(std::move(widget));
			}
		}
	}

	/**
	 * @brief Draw all open widgets from a container and track which one has focus.
	 * @tparam Container    A range of unique_ptr<Widget>.
	 * @param  widgets          The widget container to iterate.
	 * @param  lastFocusedWidget Updated to the most recently focused widget.
	 */
	template <typename Container>
	void DrawOpenWidgets(Container& widgets, Widget*& lastFocusedWidget)
	{
		for (auto& widget : widgets) {
			if (widget->IsOpen()) {
				widget->DrawWidget();
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
					lastFocusedWidget = widget.get();
				}
			}
		}
	}

	/**
	 * @brief Draw menu items for open widgets using their type name.
	 * @tparam Container A range of unique_ptr<Widget>.
	 * @param  widgets   The widget container to iterate.
	 * @param  count     Running count of open widgets (accumulated across calls).
	 * @return Updated open widget count.
	 */
	template <typename Container>
	int DrawOpenWidgetMenuItems(const Container& widgets, int count)
	{
		for (auto& widget : widgets) {
			if (widget->IsOpen()) {
				++count;
				if (ImGui::MenuItem(std::format("{}: {}", TranslateWidgetTypeName(widget->GetWidgetTypeName()), widget->GetEditorID()).c_str()))
					ImGui::SetWindowFocus(widget->GetWindowTitle().c_str());
			}
		}
		return count;
	}

	/**
	 * @brief Draw save menu items for each open widget in a container.
	 * @tparam Container A range of unique_ptr<Widget>.
	 * @param  widgets   The widget container to iterate.
	 * @param  hasOpen   Accumulator flag; true if any previous container had open widgets.
	 * @return True if any open widget exists across all calls.
	 */
	template <typename Container>
	bool DrawSaveWidgetMenuItems(Container& widgets, bool hasOpen = false)
	{
		for (auto& widget : widgets) {
			if (widget->IsOpen()) {
				hasOpen = true;
				auto editorId = widget->GetEditorID();
				if (ImGui::MenuItem(std::vformat(T("cs_editor.save_widget", "Save {}"), std::make_format_args(editorId)).c_str()))
					widget->Save();
			}
		}
		return hasOpen;
	}

	/**
	 * @brief Draw a "Close All <Type> Widgets" menu item for a widget container.
	 * @tparam Container A range of unique_ptr<Widget>.
	 * @param  widgets   The widget container whose type name is used in the label.
	 */
	template <typename Container>
	void DrawCloseAllMenuItem(Container& widgets)
	{
		if (widgets.empty())
			return;
		auto typeName = TranslateWidgetTypeName(widgets[0]->GetWidgetTypeName());
		if (ImGui::MenuItem(std::vformat(T("cs_editor.close_all_widgets", "Close All {} Widgets"), std::make_format_args(typeName)).c_str())) {
			for (auto& widget : widgets)
				widget->SetOpen(false);
		}
	}
}  // namespace WidgetFactory

/**
 * @brief Convert a normalized float3 colour to an RE::Color (0-255 range).
 * @param newColor Source colour with components in [0, 1].
 * @param color    Destination RE::Color.
 */
void Float3ToColor(const float3& newColor, RE::Color& color);

/**
 * @brief Convert a normalized float3 colour to a weather Data::Color3.
 * @param newColor Source colour with components in [0, 1].
 * @param color    Destination weather colour struct.
 */
void Float3ToColor(const float3& newColor, RE::TESWeather::Data::Color3& color);

/**
 * @brief Convert an RE::Color (0-255 range) to a normalized float3.
 * @param color    Source RE::Color.
 * @param newColor Destination float3 with components in [0, 1].
 */
void ColorToFloat3(const RE::Color& color, float3& newColor);

/**
 * @brief Convert a weather Data::Color3 to a normalized float3.
 * @param color    Source weather colour struct.
 * @param newColor Destination float3 with components in [0, 1].
 */
void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& newColor);

/**
 * @brief Get a localized label for a time-of-day colour slot index.
 * @param i Time slot index (0 = Sunrise, 1 = Day, 2 = Sunset, 3 = Night).
 * @return Translated label string.
 */
std::string ColorTimeLabel(const int i);

/**
 * @brief Get a localized label for a weather colour type index.
 * @param i Colour type index.
 * @return Translated label string.
 */
std::string ColorTypeLabel(const int i);

enum ControlType
{
	INT8_SLIDER = 0,
	COLOR3_PICKER,
	UINT8_SLIDER,
	FLOAT_SLIDER
};

/** @brief Push warning-coloured FrameBg and border styles to mark a control as inherited from a parent. */
void PushInheritedStyle();

/** @brief Pop the inherited style pushed by PushInheritedStyle. Always call after the corresponding push. */
void PopInheritedStyle();

// Time of Day (TOD) helper functions
namespace TOD
{
	// Time period indices
	enum Period : int
	{
		Sunrise = 0,
		Day = 1,
		Sunset = 2,
		Night = 3,
		Count = 4
	};

	/**
	 * @brief Get the display name of a time-of-day period.
	 * @param index Period index (use TOD::Period enum values).
	 * @return Localized period name string.
	 */
	const char* GetPeriodName(int index);

	/** @brief Get current game time in hours (0-24). */
	float GetCurrentGameTime();

	/**
	 * @brief Calculate blend factors for each time period based on current game time.
	 * @param outFactors Output array of 4 floats (Sunrise, Day, Sunset, Night), each in [0, 1].
	 */
	void GetTimeOfDayFactors(float outFactors[4]);

	/** @brief Get the primary active time period index (highest blend factor). */
	int GetActivePeriod();

	/** @brief Render the TOD header row showing period names with current activity highlighting. */
	void RenderTODHeader();

	/**
	 * @brief Draw a horizontal row of four time-of-day sliders.
	 * @param label    Row label.
	 * @param values   Array of 4 float values (one per time period).
	 * @param minValue Minimum slider value.
	 * @param maxValue Maximum slider value.
	 * @param format   Printf-style format string for display.
	 * @return True if any slider value changed.
	 */
	bool DrawTODSliderRow(const char* label, float values[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	/**
	 * @brief Draw a horizontal row of four time-of-day sliders with per-period inherit flags.
	 * @param label        Row label.
	 * @param values       Array of 4 float values (one per time period).
	 * @param inheritFlags Array of 4 bools indicating which periods inherit from parent.
	 * @param parentValues Array of 4 parent float values for inherited periods.
	 * @param minValue     Minimum slider value.
	 * @param maxValue     Maximum slider value.
	 * @param format       Printf-style format string for display.
	 * @return True if any slider value changed.
	 */
	bool DrawTODSliderRow(const char* label, float values[4], bool inheritFlags[4], const float parentValues[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	/**
	 * @brief Draw a horizontal row of four time-of-day colour pickers.
	 * @param label  Row label.
	 * @param colors Array of 4 float3 colours (one per time period).
	 * @return True if any colour changed.
	 */
	bool DrawTODColorRow(const char* label, float3 colors[4]);

	/**
	 * @brief Draw a horizontal row of four time-of-day colour pickers with inherit support.
	 * @param label        Row label.
	 * @param colors       Array of 4 float3 colours (one per time period).
	 * @param inheritFlag  Reference to a bool controlling parent inheritance for all periods.
	 * @param parentColors Array of 4 parent float3 colours for inherited periods.
	 * @return True if any colour changed.
	 */
	bool DrawTODColorRow(const char* label, float3 colors[4], bool& inheritFlag, const float3 parentColors[4]);

	/**
	 * @brief Draw a horizontal row of four time-of-day float inputs.
	 * @param label    Row label.
	 * @param values   Array of 4 float values (one per time period).
	 * @param minValue Minimum value.
	 * @param maxValue Maximum value.
	 * @param format   Printf-style format string for display.
	 * @return True if any value changed.
	 */
	bool DrawTODFloatRow(const char* label, float values[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	/**
	 * @brief Draw a horizontal row of four time-of-day float inputs with inherit support.
	 * @param label        Row label.
	 * @param values       Array of 4 float values (one per time period).
	 * @param inheritFlag  Reference to a bool controlling parent inheritance for all periods.
	 * @param parentValues Array of 4 parent float values for inherited periods.
	 * @param minValue     Minimum value.
	 * @param maxValue     Maximum value.
	 * @param format       Printf-style format string for display.
	 * @return True if any value changed.
	 */
	bool DrawTODFloatRow(const char* label, float values[4], bool& inheritFlag, const float parentValues[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	/**
	 * @brief Draw a horizontal row of four time-of-day int8 sliders.
	 * @param label  Row label.
	 * @param values Array of 4 int values (one per time period), clamped to int8 range.
	 * @return True if any slider value changed.
	 */
	bool DrawTODInt8Row(const char* label, int values[4]);

	/**
	 * @brief Begin a time-of-day table with two columns (Parameter | Values).
	 * @param tableId          ImGui table ID.
	 * @param paramColumnWidth Fixed width for the label column (0 = default 200px).
	 * @return True if the table was created successfully.
	 */
	bool BeginTODTable(const char* tableId, float paramColumnWidth = 0.0f);

	/** @brief End the TOD table started with BeginTODTable. */
	void EndTODTable();

	/** @brief Draw a separator row in a TOD table. */
	void DrawTODSeparator();
}  // namespace TOD

namespace WeatherUtils
{
	// Texture path helpers shared by precipitation and cloud layer widgets.
	namespace TexturePath
	{
		inline constexpr std::string_view kTexturePrefix = "textures\\";
		inline constexpr std::string_view kResourcePrefix = "Textures\\";
		inline constexpr std::string_view kDdsExtension = ".dds";

		/**
		 * @brief Normalize a texture path: lowercase and convert forward slashes to backslashes.
		 * @param path The texture path to normalize.
		 * @return The normalized path string.
		 */
		std::string Normalize(std::string_view path);

		/**
		 * @brief Check if a normalized path ends with ".dds".
		 * @param path The texture path to check.
		 * @return True if the path has a .dds extension.
		 */
		bool HasDdsExtension(std::string_view path);

		/**
		 * @brief Check if a texture file exists under Data/textures/.
		 * @param path Texture path (with or without the leading "textures\" prefix).
		 * @return True if the file exists on disk.
		 */
		bool ExistsOnDisk(std::string_view path);

		/**
		 * @brief Build a BSA-style resource path ("Textures\\<path>" with .dds appended if missing).
		 * @param path The relative texture path.
		 * @return Full resource path string suitable for BSA lookup.
		 */
		std::string BuildResourcePath(std::string_view path);
	}

	/**
	 * @brief Find a game form by its editor ID using the widget cache (load-order independent).
	 * @param editorID The editor ID string to look up.
	 * @param widgets  The widget container to search.
	 * @return Pointer to the matching form, or nullptr if not found.
	 */
	RE::TESForm* FindFormByEditorID(std::string_view editorID, const std::vector<std::unique_ptr<Widget>>& widgets);

	/**
	 * @brief Find the editor ID for a given form using the widget cache.
	 * @param form    The form to look up.
	 * @param widgets The widget container to search.
	 * @return The editor ID string, or empty string if not found.
	 */
	std::string FindEditorIDByForm(const RE::TESForm* form, const std::vector<std::unique_ptr<Widget>>& widgets);

	/**
	 * @brief Set the current widget context for undo tracking. Call at the start of widget Draw().
	 * @param widget The widget being drawn.
	 */
	void SetCurrentWidget(Widget* widget);

	/**
	 * @brief Translate a control label to its localized equivalent via the i18n system.
	 * @param label The English control label.
	 * @return Localized label string (stable pointer valid for the session).
	 */
	const char* TranslateControlLabel(std::string_view label);

	/**
	 * @brief Draw an int8-range slider with undo and palette tracking.
	 * @param label    Display label for the slider.
	 * @param property Reference to the int property to edit (clamped to [-128, 127]).
	 * @return True if the value changed.
	 */
	bool DrawSliderInt8(const std::string& label, int& property);

	/**
	 * @brief Draw an RGB colour picker with undo and palette tracking.
	 * @param l        Display label for the colour picker.
	 * @param property Reference to the float3 colour to edit.
	 * @param widget   Optional owning widget for search highlight support.
	 * @return True if the colour changed.
	 */
	bool DrawColorEdit(const std::string& l, float3& property, Widget* widget = nullptr);

	/**
	 * @brief Draw a uint8-range slider with undo and palette tracking.
	 * @param label    Display label for the slider.
	 * @param property Reference to the int property to edit (clamped to [0, 255]).
	 * @return True if the value changed.
	 */
	bool DrawSliderUint8(const std::string& label, int& property);

	/**
	 * @brief Draw a float slider with undo and palette tracking.
	 * @param label  Display label for the slider.
	 * @param property Reference to the float property to edit.
	 * @param min    Minimum slider value.
	 * @param max    Maximum slider value.
	 * @param widget Optional owning widget for search highlight support.
	 * @param format Printf-style format string for display.
	 * @return True if the value changed.
	 */
	bool DrawSliderFloat(const std::string& label, float& property, float min = 0.0f, float max = 1.0f, Widget* widget = nullptr, const char* format = "%.3f");

	/**
	 * @brief Draw a checkbox with optional undo tracking.
	 * @param label  Display label for the checkbox.
	 * @param value  Reference to the bool to edit.
	 * @param widget Optional owning widget for search highlight support.
	 * @return True if the value changed.
	 */
	bool DrawCheckbox(const std::string& label, bool& value, Widget* widget = nullptr);

	namespace detail
	{
		// Shared body for form-picker combo boxes. ForEachEntry receives a callback
		// invoked with (T* form, std::string editorID) for each enumerable entry.
		template <typename T, typename ForEachEntry>
		bool DrawFormComboBody(const char* label, T*& currentForm, const std::string& currentEditorID,
			bool showFormID, bool allowNone, float width, ForEachEntry forEachEntry)
		{
			bool changed = false;

			std::string previewText;
			if (currentForm) {
				const std::string& effectiveID = currentEditorID.empty() ?
				                                     std::format("{:08X}", currentForm->GetFormID()) :
				                                     currentEditorID;
				previewText = showFormID ?
				                  std::format("{} (0x{:08X})", effectiveID, currentForm->GetFormID()) :
				                  effectiveID;
			} else {
				previewText = ::T("cs_editor.none_filter", "None");
			}

			if (width > 0.0f)
				ImGui::SetNextItemWidth(width);

			if (ImGui::BeginCombo(label, previewText.c_str())) {
				if (allowNone && ImGui::Selectable(::T("cs_editor.none_filter", "None"), currentForm == nullptr)) {
					currentForm = nullptr;
					changed = true;
				}

				forEachEntry([&](T* form, const std::string& editorID) {
					std::string comboLabel = showFormID ?
					                             std::format("{} (0x{:08X})", editorID, form->GetFormID()) :
					                             editorID;
					bool isSelected = (currentForm == form);
					if (ImGui::Selectable(comboLabel.c_str(), isSelected)) {
						currentForm = form;
						changed = true;
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				});
				ImGui::EndCombo();
			}

			return changed;
		}
	}

	/**
	 * @brief Draw a form picker combo box using cached widget EditorIDs for performance.
	 * @tparam T               The game form type.
	 * @tparam WidgetContainer  A range of unique_ptr<Widget>.
	 * @param  label       ImGui label for the combo box.
	 * @param  currentForm Reference to the currently selected form pointer (updated on selection).
	 * @param  widgets     Widget container providing cached EditorIDs.
	 * @param  showFormID  If true, display FormIDs alongside editor IDs.
	 * @param  allowNone   If true, include a "None" option.
	 * @param  width       Combo box width in pixels.
	 * @return True if the selection changed.
	 */
	template <typename T, typename WidgetContainer>
	bool DrawFormPickerCached(const char* label, T*& currentForm, const WidgetContainer& widgets, bool showFormID = true, bool allowNone = true, float width = 450.0f)
	{
		std::string currentEditorID;
		if (currentForm) {
			for (const auto& widget : widgets) {
				if (widget->form == currentForm) {
					currentEditorID = widget->GetEditorID();
					break;
				}
			}
		}

		return detail::DrawFormComboBody<T>(label, currentForm, currentEditorID, showFormID, allowNone, width,
			[&](auto&& visit) {
				for (const auto& widget : widgets) {
					if (widget && widget->form)
						visit(static_cast<T*>(widget->form), widget->GetEditorID());
				}
			});
	}

	/**
	 * @brief Draw a legacy form picker combo box (slow -- prefer DrawFormPickerCached when widgets are available).
	 * @tparam T         The game form type.
	 * @tparam Container A range of T* form pointers.
	 * @param  label       ImGui label for the combo box.
	 * @param  currentForm Reference to the currently selected form pointer (updated on selection).
	 * @param  formArray   The raw form array to iterate.
	 * @param  showFormID  If true, display FormIDs alongside editor IDs.
	 * @param  allowNone   If true, include a "None" option.
	 * @param  width       Combo box width in pixels.
	 * @return True if the selection changed.
	 */
	template <typename T, typename Container>
	bool DrawFormPicker(const char* label, T*& currentForm, const Container& formArray, bool showFormID = true, bool allowNone = true, float width = 450.0f)
	{
		auto getEditorID = [](T* form) -> std::string {
			if (!form)
				return "";
			const char* editorID = form->GetFormEditorID();
			if (editorID && editorID[0] != '\0')
				return std::string(editorID);
			return std::format("{:08X}", form->GetFormID());
		};

		return detail::DrawFormComboBody<T>(label, currentForm, getEditorID(currentForm), showFormID, allowNone, width,
			[&](auto&& visit) {
				for (auto form : formArray) {
					if (form)
						visit(form, getEditorID(form));
				}
			});
	}
}

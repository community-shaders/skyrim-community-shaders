#pragma once

#include "Util.h"
#include "Widget.h"
#include <cctype>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Forward declarations
class EditorWindow;

// Case-insensitive substring search helper
bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring);

// ============================================================================
// DebouncedTracker - Consolidates debounced value tracking with undo support
// ============================================================================
template <typename T>
class DebouncedTracker
{
public:
	static constexpr double DefaultDebounceDelay = 2.0;

	// Call when a value changes. Returns true if this is a new interaction session.
	bool OnValueChanged(const std::string& key, const T& value, double currentTime)
	{
		pendingValues[key] = value;
		lastChangeTime[key] = currentTime;
		return true;
	}

	// Call every frame with current item active state. Returns true if undo should be pushed.
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

	// Get pending values that have been idle for debounceDelay and should be tracked
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

/// Window flags to disable scrolling on the parent window when using a sticky header.
/// Add to ImGui::Begin() or ImGui::BeginChild() window_flags parameter.
constexpr ImGuiWindowFlags kStickyHeaderFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

/// Begin a scrollable content region below a sticky header. Fills remaining space.
inline bool BeginScrollableContent(const char* id = "##ScrollContent") { return ImGui::BeginChild(id, ImVec2(0, 0)); }

/// End the scrollable content region.
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

/// Apply standard DPI-scaled size constraints and initial size for widget windows. Call before ImGui::Begin().
/// Uses per-widget-type size tracking so all instances of a widget type share window dimensions.
/// Respects EditorWindow::resetLayout to re-apply defaults on demand.
void SetupWidgetWindowDefaults(const char* widgetType);

/// Update stored per-widget-type size from the current ImGui window. Call after ImGui::Begin().
/// Only updates when the window is focused so resize applies to future windows of the same type.
void UpdateWidgetTypeSize(const char* widgetType);

/// Reset all per-widget-type sizes to defaults. Called when resetLayout is triggered.
void ResetWidgetTypeSizes();

/// Serialize per-widget-type sizes into a JSON object for persistence.
json GetWidgetTypeSizesJson();

/// Restore per-widget-type sizes from a previously serialized JSON object.
void SetWidgetTypeSizesFromJson(const json& j);

// ============================================================================
// PropertyDrawer - Unified table-based property drawing with search support
// ============================================================================
namespace PropertyDrawer
{
	// Begin a property table. Call before drawing properties.
	bool BeginTable(const char* tableId, float labelWidth = 200.0f);
	void EndTable();

	// Draw a table separator row
	void DrawSeparator();

	// Draw a vertically-centered label in the first table column. Call between TableNextRow and drawing the value.
	void DrawLabel(const char* label);

	// Returns true if value was changed.
	bool DrawFloat(const char* label, float& value, float minVal, float maxVal,
		const char* format = "%.3f");

	bool DrawInt(const char* label, int& value, int minVal, int maxVal);

	bool DrawColor(const char* label, float3& value);

	bool DrawCheckbox(const char* label, bool& value);
}  // namespace PropertyDrawer

// ============================================================================
// WidgetFactory - Template-based widget creation for EditorWindow::SetupResources
// ============================================================================
namespace WidgetFactory
{
	// Populate a widget container from a form array
	// WidgetType must have a constructor taking FormType*
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

	// Populate a widget container with SimpleFormWidget for cache-only purposes
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

	// Draw all open widgets from a container, tracking focus
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

	// Draw menu items for open widgets in a container using widget type name. Returns updated open count.
	template <typename Container>
	int DrawOpenWidgetMenuItems(const Container& widgets, int count)
	{
		for (auto& widget : widgets) {
			if (widget->IsOpen()) {
				++count;
				if (ImGui::MenuItem(std::format("{}: {}", widget->GetWidgetTypeName(), widget->GetEditorID()).c_str()))
					ImGui::SetWindowFocus(widget->GetWindowTitle().c_str());
			}
		}
		return count;
	}

	// Draw save menu items for open widgets in a container. Returns true if any open widget exists.
	template <typename Container>
	bool DrawSaveWidgetMenuItems(Container& widgets, bool hasOpen = false)
	{
		for (auto& widget : widgets) {
			if (widget->IsOpen()) {
				hasOpen = true;
				if (ImGui::MenuItem(std::format("Save {}", widget->GetEditorID()).c_str()))
					widget->Save();
			}
		}
		return hasOpen;
	}

	// Draw "Close All <Type> Widgets" menu item for a widget container.
	template <typename Container>
	void DrawCloseAllMenuItem(Container& widgets)
	{
		if (widgets.empty())
			return;
		if (ImGui::MenuItem(std::format("Close All {} Widgets", widgets[0]->GetWidgetTypeName()).c_str())) {
			for (auto& widget : widgets)
				widget->SetOpen(false);
		}
	}
}  // namespace WidgetFactory

void Float3ToColor(const float3& newColor, RE::Color& color);
void Float3ToColor(const float3& newColor, RE::TESWeather::Data::Color3& color);

void ColorToFloat3(const RE::Color& color, float3& newColor);
void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& newColor);

std::string ColorTimeLabel(const int i);
std::string ColorTypeLabel(const int i);

enum ControlType
{
	INT8_SLIDER = 0,
	COLOR3_PICKER,
	UINT8_SLIDER,
	FLOAT_SLIDER
};

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

	// Get the name of a time period
	const char* GetPeriodName(int index);

	// Get current game time in hours (0-24)
	float GetCurrentGameTime();

	// Calculate blend factor for each time period based on current game time
	// Returns array of 4 floats (Sunrise, Day, Sunset, Night)
	void GetTimeOfDayFactors(float outFactors[4]);

	// Get the primary active time period (highest blend factor)
	int GetActivePeriod();

	// Render TOD header row (shows period names with current activity)
	void RenderTODHeader();

	// Draw a horizontal row of TOD sliders
	// Returns true if any slider changed
	bool DrawTODSliderRow(const char* label, float values[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");
	bool DrawTODSliderRow(const char* label, float values[4], bool inheritFlags[4], const float parentValues[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	// Draw a horizontal row of TOD color pickers
	// Returns true if any color changed
	bool DrawTODColorRow(const char* label, float3 colors[4]);
	bool DrawTODColorRow(const char* label, float3 colors[4], bool& inheritFlag, const float3 parentColors[4]);
	bool DrawTODFloatRow(const char* label, float values[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");
	bool DrawTODFloatRow(const char* label, float values[4], bool& inheritFlag, const float parentValues[4], float minValue = 0.0f, float maxValue = 1.0f, const char* format = "%.2f");

	// Draw a horizontal row of TOD int8 sliders
	// Returns true if any slider changed
	bool DrawTODInt8Row(const char* label, int values[4]);

	// Helper to begin a TOD table (2 columns: Parameter | Values)
	// Returns true if table was created successfully
	// paramColumnWidth: fixed width for the label column (0 = default 200px)
	bool BeginTODTable(const char* tableId, float paramColumnWidth = 0.0f);

	// End the TOD table
	void EndTODTable();

	// Draw a separator row in a TOD table
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

		// Lowercase + convert forward slashes to backslashes.
		std::string Normalize(std::string_view path);

		// True if the normalized path ends with ".dds".
		bool HasDdsExtension(std::string_view path);

		// True if the file exists under Data/textures/ (accepts paths with or without the leading "textures\").
		bool ExistsOnDisk(std::string_view path);

		// Build a BSA-style resource path ("Textures\\<path>" with .dds appended if missing).
		std::string BuildResourcePath(std::string_view path);
	}

	// Lookup helpers for form↔widget ref serialization (load-order independent).
	RE::TESForm* FindFormByEditorID(std::string_view editorID, const std::vector<std::unique_ptr<Widget>>& widgets);
	std::string FindEditorIDByForm(const RE::TESForm* form, const std::vector<std::unique_ptr<Widget>>& widgets);

	// Set the current widget for undo tracking (should be called at start of widget Draw())
	void SetCurrentWidget(Widget* widget);

	// UI helper functions
	bool DrawSliderInt8(const std::string& label, int& property);
	bool DrawColorEdit(const std::string& l, float3& property, Widget* widget = nullptr);
	bool DrawSliderUint8(const std::string& label, int& property);
	bool DrawSliderFloat(const std::string& label, float& property, float min = 0.0f, float max = 1.0f, Widget* widget = nullptr, const char* format = "%.3f");
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
				previewText = "None";
			}

			if (width > 0.0f)
				ImGui::SetNextItemWidth(width);

			if (ImGui::BeginCombo(label, previewText.c_str())) {
				if (allowNone && ImGui::Selectable("None", currentForm == nullptr)) {
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

	// Generic form picker combo box using cached widget EditorIDs for performance
	// Returns true if selection changed
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

	// Legacy form picker (slow - only use if widgets not available)
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

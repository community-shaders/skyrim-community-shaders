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
// PropertyDrawer - Unified table-based property drawing with search support
// ============================================================================
namespace PropertyDrawer
{
	// Begin a property table. Call before drawing properties.
	bool BeginTable(const char* tableId, float labelWidth = 200.0f);
	void EndTable();

	// Draw a table separator row
	void DrawSeparator();

	// Draw properties with optional search filtering.
	// searchBuffer can be nullptr to skip search filtering.
	// Returns true if value was changed.
	bool DrawFloat(const char* label, float& value, float minVal, float maxVal,
		const char* searchBuffer = nullptr, const char* format = "%.3f");

	bool DrawInt(const char* label, int& value, int minVal, int maxVal,
		const char* searchBuffer = nullptr);

	bool DrawColor(const char* label, float3& value, const char* searchBuffer = nullptr);

	bool DrawCheckbox(const char* label, bool& value, const char* searchBuffer = nullptr);

	// Check if a label matches the current search (convenience wrapper)
	bool MatchesSearch(const char* label, const char* searchBuffer);
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
				widget->Load();
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
	bool BeginTODTable(const char* tableId);

	// End the TOD table
	void EndTODTable();

	// Draw a separator row in a TOD table
	void DrawTODSeparator();
}  // namespace TOD

// Widget search bar helpers
bool BeginWidgetSearchBar(char* searchBuffer, size_t bufferSize, bool& searchActive);
void EndWidgetSearchBar();

namespace WeatherUtils
{
	// Set the current widget for undo tracking (should be called at start of widget Draw())
	void SetCurrentWidget(Widget* widget);

	// UI helper functions
	bool DrawSliderInt8(const std::string& label, int& property);
	bool DrawColorEdit(const std::string& l, float3& property, Widget* widget = nullptr);
	bool DrawSliderUint8(const std::string& label, int& property);
	bool DrawSliderFloat(const std::string& label, float& property, float min = 0.0f, float max = 1.0f, Widget* widget = nullptr);

	// Generic form picker combo box using cached widget EditorIDs for performance
	// Returns true if selection changed
	template <typename T, typename WidgetContainer>
	bool DrawFormPickerCached(const char* label, T*& currentForm, const WidgetContainer& widgets, bool showFormID = true, bool allowNone = true, float width = 450.0f)
	{
		bool changed = false;

		std::string previewText;
		if (currentForm) {
			// Find the widget for current form
			std::string editorID;
			for (const auto& widget : widgets) {
				if (widget->form == currentForm) {
					editorID = widget->GetEditorID();
					break;
				}
			}
			if (editorID.empty()) {
				editorID = std::format("{:08X}", currentForm->GetFormID());
			}

			if (showFormID) {
				previewText = std::format("{} (0x{:08X})", editorID, currentForm->GetFormID());
			} else {
				previewText = editorID;
			}
		} else {
			previewText = "None";
		}

		if (width > 0.0f) {
			ImGui::SetNextItemWidth(width);
		}

		if (ImGui::BeginCombo(label, previewText.c_str())) {
			if (allowNone && ImGui::Selectable("None", currentForm == nullptr)) {
				currentForm = nullptr;
				changed = true;
			}

			for (const auto& widget : widgets) {
				if (widget && widget->form) {
					T* form = static_cast<T*>(widget->form);
					std::string editorID = widget->GetEditorID();
					std::string comboLabel;
					if (showFormID) {
						comboLabel = std::format("{} (0x{:08X})", editorID, form->GetFormID());
					} else {
						comboLabel = editorID;
					}

					bool isSelected = (currentForm == form);
					if (ImGui::Selectable(comboLabel.c_str(), isSelected)) {
						currentForm = form;
						changed = true;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
			}
			ImGui::EndCombo();
		}

		return changed;
	}

	// Legacy form picker (slow - only use if widgets not available)
	template <typename T, typename Container>
	bool DrawFormPicker(const char* label, T*& currentForm, const Container& formArray, bool showFormID = true, bool allowNone = true, float width = 450.0f)
	{
		bool changed = false;

		auto GetFormEditorIDSafe = [](T* form) -> std::string {
			if (!form)
				return "";

			const char* editorID = form->GetFormEditorID();
			if (editorID && editorID[0] != '\0')
				return std::string(editorID);

			return std::format("{:08X}", form->GetFormID());
		};

		std::string previewText;
		if (currentForm) {
			std::string editorID = GetFormEditorIDSafe(currentForm);
			if (showFormID) {
				previewText = std::format("{} (0x{:08X})", editorID, currentForm->GetFormID());
			} else {
				previewText = editorID;
			}
		} else {
			previewText = "None";
		}

		if (width > 0.0f) {
			ImGui::SetNextItemWidth(width);
		}

		if (ImGui::BeginCombo(label, previewText.c_str())) {
			if (allowNone && ImGui::Selectable("None", currentForm == nullptr)) {
				currentForm = nullptr;
				changed = true;
			}

			for (auto form : formArray) {
				if (form) {
					std::string editorID = GetFormEditorIDSafe(form);
					std::string comboLabel;
					if (showFormID) {
						comboLabel = std::format("{} (0x{:08X})", editorID, form->GetFormID());
					} else {
						comboLabel = editorID;
					}

					bool isSelected = (currentForm == form);
					if (ImGui::Selectable(comboLabel.c_str(), isSelected)) {
						currentForm = form;
						changed = true;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
			}
			ImGui::EndCombo();
		}

		return changed;
	}
}
#pragma once

#include "Util.h"
#include <cctype>
#include <functional>

// Case-insensitive substring search helper
bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring);

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
}  // namespace TOD

// Widget search bar helpers
bool BeginWidgetSearchBar(char* searchBuffer, size_t bufferSize, bool& searchActive);
void EndWidgetSearchBar();

namespace WeatherUtils
{
	// UI helper functions
	bool DrawSliderInt8(const std::string& label, int& property);
	bool DrawColorEdit(const std::string& l, float3& property);
	bool DrawSliderUint8(const std::string& label, int& property);
	bool DrawSliderFloat(const std::string& label, float& property, float min = 0.0f, float max = 50000.0f);

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
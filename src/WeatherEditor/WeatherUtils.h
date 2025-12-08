#pragma once

#include "Util.h"
#include <functional>

// Case-insensitive substring search helper
bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring);

void Float3ToColor(const float3& newColor, RE::Color& color);
void Float3ToColor(const float3& newColor, RE::TESWeather::Data::Color3& color);

void ColorToFloat3(const RE::Color& color, float3& newColor);
void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& newColor);

std::string ColorTimeLabel(const int i);
std::string ColorTypeLabel(const int i);

bool DrawSliderInt8(const std::string& label, int& property);
bool DrawColorEdit(const std::string& l, float3& property);
bool DrawSliderUint8(const std::string& label, int& property);
bool DrawSliderFloat(const std::string& label, float& property);

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

	// Draw a horizontal row of TOD color pickers
	// Returns true if any color changed
	bool DrawTODColorRow(const char* label, float3 colors[4]);

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
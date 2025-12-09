#include "WeatherUtils.h"
#include "PaletteWindow.h"
#include "EditorWindow.h"

bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring)
{
	if (a_substring.empty())
		return true;

	const auto it = std::ranges::search(a_string, a_substring, [](const char a_a, const char a_b) {
		return std::tolower(static_cast<unsigned char>(a_a)) == std::tolower(static_cast<unsigned char>(a_b));
	});
	return !it.empty();
}

float Int8ToFloat(const int8_t& value)
{
	return ((float)(value + 128) / 255.0f);
}

float Uint8ToFloat(const uint8_t& value)
{
	return ((float)(value) / 255.0f);
}

int8_t FloatToInt8(const float& value)
{
	return (int8_t)std::lerp(-128, 127, std::clamp(value, 0.0f, 1.0f));
}

uint8_t FloatToUint8(const float& value)
{
	return (uint8_t)std::lerp(0, 255, std::clamp(value, 0.0f, 1.0f));
}

void Float3ToColor(const float3& f3, RE::Color& color)
{
	color.red = FloatToUint8(f3.x);
	color.green = FloatToUint8(f3.y);
	color.blue = FloatToUint8(f3.z);
}

void Float3ToColor(const float3& f3, RE::TESWeather::Data::Color3& color)
{
	color.red = FloatToInt8(f3.x);
	color.green = FloatToInt8(f3.y);
	color.blue = FloatToInt8(f3.z);
}

void ColorToFloat3(const RE::Color& color, float3& f3)
{
	f3.x = Uint8ToFloat(color.red);
	f3.y = Uint8ToFloat(color.green);
	f3.z = Uint8ToFloat(color.blue);
}

void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& f3)
{
	f3.x = Int8ToFloat(color.red);
	f3.y = Int8ToFloat(color.green);
	f3.z = Int8ToFloat(color.blue);
}

std::string ColorTimeLabel(const int i)
{
	std::string label = "";
	switch (i) {
	case 0:
		label = "Sunrise";
		break;
	case 1:
		label = "Day";
		break;
	case 2:
		label = "Sunset";
		break;
	case 3:
		label = "Night";
		break;
	default:
		break;
	}
	return label;
}

std::string ColorTypeLabel(const int i)
{
	std::string label = "";
	switch (i) {
	case 0:
		label = "Sky Upper";
		break;
	case 1:
		label = "Fog Near";
		break;
	case 2:
		label = "Unknown";
		break;
	case 3:
		label = "Ambient";
		break;
	case 4:
		label = "Sunlight";
		break;
	case 5:
		label = "Sun";
		break;
	case 6:
		label = "Stars";
		break;
	case 7:
		label = "Sky Lower";
		break;
	case 8:
		label = "Horizon";
		break;
	case 9:
		label = "Effect Lighting";
		break;
	case 10:
		label = "Cloud LOD Diffuse";
		break;
	case 11:
		label = "Cloud LOD Ambient";
		break;
	case 12:
		label = "Fog Far";
		break;
	case 13:
		label = "Sky Statics";
		break;
	case 14:
		label = "Water Multiplier";
		break;
	case 15:
		label = "Sun Glare";
		break;
	case 16:
		label = "Moon Glare";
		break;
	default:
		break;
	}
	return label;
}

namespace WeatherUtils
{
	bool DrawSliderInt8(const std::string& label, int& property)
	{
		static std::map<std::string, int> pendingValues;
		static std::map<std::string, double> lastChangeTime;
		const double debounceDelay = 2.0;
		
		bool changed = ImGui::SliderInt(label.c_str(), &property, -128, 127);
		if (changed) {
			pendingValues[label] = property;
			lastChangeTime[label] = ImGui::GetTime();
		}
		
		// Check for any pending values that should be tracked
		std::vector<std::string> toTrack;
		for (const auto& [key, changeTime] : lastChangeTime) {
			if (ImGui::GetTime() - changeTime >= debounceDelay) {
				toTrack.push_back(key);
			}
		}
		
		// Track and remove completed entries
		for (const auto& key : toTrack) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, static_cast<float>(pendingValues[key]));
			pendingValues.erase(key);
			lastChangeTime.erase(key);
		}
		
		return changed;
	}

	bool DrawColorEdit(const std::string& l, float3& property)
	{
		static std::map<std::string, float3> colorCache;
		static std::string activeColorId;
		static std::map<std::string, bool> wasPickerOpen;
		
		std::string cacheId = l;
		bool isActive = ImGui::IsPopupOpen(l.c_str(), ImGuiPopupFlags_AnyPopupId);
		bool wasActive = wasPickerOpen[cacheId];
		
		// Cache the original color when picker is first activated
		if (isActive && activeColorId != cacheId) {
			colorCache[cacheId] = property;
			activeColorId = cacheId;
		} else if (!isActive && activeColorId == cacheId) {
			activeColorId.clear();
		}
		
		// Check for Ctrl+Z while picker is active
		if (isActive && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z)) {
			if (colorCache.contains(cacheId)) {
				property = colorCache[cacheId];
				wasPickerOpen[cacheId] = isActive;
				return true;
			}
		}
		
		bool changed = ImGui::ColorEdit3(l.c_str(), (float*)&property);
		
		// Track color usage only when picker closes
		if (wasActive && !isActive) {
			PaletteWindow::GetSingleton()->TrackColorUsage(property);
		}
		
		wasPickerOpen[cacheId] = isActive;
		
		// Drag-and-drop source
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
			ImGui::SetDragDropPayload("COLOR_DND", &property, sizeof(float3));
			ImGui::ColorButton("##preview", ImVec4(property.x, property.y, property.z, 1.0f), ImGuiColorEditFlags_NoAlpha);
			ImGui::EndDragDropSource();
		}
		
		// Drag-and-drop target
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLOR_DND")) {
				if (payload->DataSize == sizeof(float3)) {
					float3 droppedColor = *(const float3*)payload->Data;
					property = droppedColor;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		
		return changed;
	}

	bool DrawSliderUint8(const std::string& label, int& property)
	{
		return ImGui::SliderInt(label.c_str(), &property, 0, 255);
	}

	bool DrawSliderFloat(const std::string& label, float& property, float min, float max)
	{
		static std::map<std::string, float> pendingValues;
		static std::map<std::string, double> lastChangeTime;
		const double debounceDelay = 2.0;
		
		bool changed = ImGui::SliderFloat(label.c_str(), &property, min, max);
		if (changed) {
			pendingValues[label] = property;
			lastChangeTime[label] = ImGui::GetTime();
		}
		
		// Check for any pending values that should be tracked
		std::vector<std::string> toTrack;
		for (const auto& [key, changeTime] : lastChangeTime) {
			if (ImGui::GetTime() - changeTime >= debounceDelay) {
				toTrack.push_back(key);
			}
		}
		
		// Track and remove completed entries
		for (const auto& key : toTrack) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, pendingValues[key]);
			pendingValues.erase(key);
			lastChangeTime.erase(key);
		}
		
		return changed;
	}
}

// Time of Day (TOD) helper implementation
namespace TOD
{
	const char* GetPeriodName(int index)
	{
		static const char* names[Count] = { "Sunrise", "Day", "Sunset", "Night" };
		if (index >= 0 && index < Count)
			return names[index];
		return "Unknown";
	}

	float GetCurrentGameTime()
	{
		auto sky = globals::game::sky;
		if (sky) {
			return std::clamp(sky->currentGameHour, 0.0f, 24.0f);
		}
		return 12.0f;  // Default to noon
	}

	void GetTimeOfDayFactors(float outFactors[4])
	{
		// Initialize all to 0
		for (int i = 0; i < 4; ++i)
			outFactors[i] = 0.0f;

		float currentTime = GetCurrentGameTime();

		// Simplified time periods (matching Skyrim's 4-period system)
		// Sunrise: 5-9, Day: 9-17, Sunset: 17-21, Night: 21-5
		const float sunriseStart = 5.0f;
		const float sunriseEnd = 9.0f;
		const float dayStart = 9.0f;
		const float dayEnd = 17.0f;
		const float sunsetStart = 17.0f;
		const float sunsetEnd = 21.0f;

		if (currentTime >= sunriseStart && currentTime < sunriseEnd) {
			// Sunrise period
			float t = (currentTime - sunriseStart) / (sunriseEnd - sunriseStart);
			outFactors[Sunrise] = 1.0f - t;
			outFactors[Day] = t;
		} else if (currentTime >= dayStart && currentTime < dayEnd) {
			// Day period
			outFactors[Day] = 1.0f;
		} else if (currentTime >= sunsetStart && currentTime < sunsetEnd) {
			// Sunset period
			float t = (currentTime - sunsetStart) / (sunsetEnd - sunsetStart);
			outFactors[Day] = 1.0f - t;
			outFactors[Sunset] = t;
		} else if (currentTime >= sunsetEnd || currentTime < sunriseStart) {
			// Night period
			outFactors[Night] = 1.0f;
		}
	}

	int GetActivePeriod()
	{
		float factors[4];
		GetTimeOfDayFactors(factors);

		int maxIndex = 0;
		float maxValue = factors[0];
		for (int i = 1; i < 4; ++i) {
			if (factors[i] > maxValue) {
				maxValue = factors[i];
				maxIndex = i;
			}
		}
		return maxIndex;
	}

	void RenderTODHeader()
	{
		float factors[4];
		GetTimeOfDayFactors(factors);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float sliderWidth = (totalWidth - 3 * spacing) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			ImGui::BeginChild(("##todheader_" + std::to_string(i)).c_str(),
				ImVec2(sliderWidth, ImGui::GetTextLineHeight()), false, ImGuiWindowFlags_NoScrollbar);

			const char* name = GetPeriodName(i);
			float labelWidth = ImGui::CalcTextSize(name).x;
			float centerOffset = (sliderWidth - labelWidth) * 0.5f;
			if (centerOffset > 0)
				ImGui::SetCursorPosX(centerOffset);

			bool isActive = factors[i] > 0.01f;
			if (!isActive)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));

			ImGui::Text("%s", name);

			if (!isActive)
				ImGui::PopStyleColor();

			ImGui::EndChild();
		}
	}

	bool DrawTODSliderRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		static std::map<std::string, float> pendingValues;
		static std::map<std::string, double> lastChangeTime;
		const double debounceDelay = 2.0;
		
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float sliderWidth = (totalWidth - 3 * 8.0f) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			bool isActive = factors[i] > 0.0f;
			if (!isActive)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		ImGui::PushItemWidth(sliderWidth);
		std::string id = std::string("##") + label + std::to_string(i);
		if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
			changed = true;
			std::string valueName = std::string(label) + " " + GetPeriodName(i);
			pendingValues[valueName] = values[i];
			lastChangeTime[valueName] = ImGui::GetTime();
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%.0f%%", factors[i] * 100.0f);			ImGui::PopItemWidth();

			if (!isActive)
				ImGui::PopStyleVar();
		}
		
		// Check for any pending values that should be tracked
		std::vector<std::string> toTrack;
		for (const auto& [key, changeTime] : lastChangeTime) {
			if (ImGui::GetTime() - changeTime >= debounceDelay) {
				toTrack.push_back(key);
			}
		}
		
		// Track and remove completed entries
		for (const auto& key : toTrack) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, pendingValues[key]);
			pendingValues.erase(key);
			lastChangeTime.erase(key);
		}

		return changed;
	}

	bool DrawTODColorRow(const char* label, float3 colors[4])
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		
		// Only highlight the title text based on active time of day
		bool anyActive = false;
		for (int i = 0; i < Count; ++i) {
			if (factors[i] > 0.0f) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
		
		ImGui::Text("%s", label);
		
		if (!anyActive)
			ImGui::PopStyleVar();
		
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		// Match the header calculation exactly
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		// Use a fixed button size
		const float buttonSize = ImGui::GetFrameHeight() * 1.5f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			// Create a child region matching the column width to ensure proper alignment
			ImGui::BeginChild(("##colorcolumn_" + std::string(label) + std::to_string(i)).c_str(),
				ImVec2(columnWidth, buttonSize), false, ImGuiWindowFlags_NoScrollbar);

			// Center the button within this column
			float centerOffset = (columnWidth - buttonSize) * 0.5f;
			if (centerOffset > 0.0f)
				ImGui::SetCursorPosX(centerOffset);

			std::string id = std::string("##") + label + std::to_string(i);
			ImVec4 color = ImVec4(colors[i].x, colors[i].y, colors[i].z, 1.0f);
			
			static std::map<std::string, float3> colorCache;
			static std::string activeColorId;
			
			// Use ColorButton with fixed size - no alpha styling on the button itself
			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				colorCache[id] = colors[i];
				activeColorId = id;
				ImGui::OpenPopup(id.c_str());
			}
			
			// Drag-and-drop source
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &colors[i], sizeof(float3));
				ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}
			
			// Drag-and-drop target
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLOR_DND")) {
					if (payload->DataSize == sizeof(float3)) {
						float3 droppedColor = *(const float3*)payload->Data;
						colors[i] = droppedColor;
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			
			// Color picker popup
			static std::map<std::string, bool> wasPopupOpen;
			bool isPopupOpen = ImGui::BeginPopup(id.c_str());
			bool wasOpen = wasPopupOpen[id];
			
			if (isPopupOpen) {
				// Check for Ctrl+Z while picker is active
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z)) {
					if (colorCache.contains(id)) {
						colors[i] = colorCache[id];
						changed = true;
					}
				}
				
				if (ImGui::ColorPicker3((id + "_picker").c_str(), (float*)&colors[i], ImGuiColorEditFlags_NoAlpha)) {
					changed = true;
				}
				ImGui::EndPopup();
			} else if (activeColorId == id) {
				activeColorId.clear();
			}
			
			// Track color usage only when popup closes
			if (wasOpen && !isPopupOpen) {
				PaletteWindow::GetSingleton()->TrackColorUsage(colors[i]);
			}
			
			wasPopupOpen[id] = isPopupOpen;

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s - %.0f%%", GetPeriodName(i), factors[i] * 100.0f);

			ImGui::EndChild();
		}

		return changed;
	}

	bool DrawTODSliderRow(const char* label, float values[4], bool inheritFlags[4], const float parentValues[4], float minValue, float maxValue, const char* format)
	{
		static std::map<std::string, float> pendingSliderValues;
		static std::map<std::string, double> sliderLastChangeTime;
		const double debounceDelay = 2.0;
		
	float factors[4];
	GetTimeOfDayFactors(factors);
	bool changed = false;

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("%s", label);
	ImGui::TableSetColumnIndex(1);

	float totalWidth = ImGui::GetContentRegionAvail().x;
	float checkboxWidth = 20.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float sliderWidth = (totalWidth - (static_cast<int>(Count) - 1) * spacing - (parentValues ? static_cast<int>(Count) * checkboxWidth : 0)) / static_cast<float>(Count);

	for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			ImGui::BeginGroup();
			
			// Per-column inherit checkbox
			if (parentValues) {
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
				ImGui::SetNextItemWidth(checkboxWidth);
				std::string inheritId = std::string("##inherit_") + label + std::to_string(i);
				if (ImGui::Checkbox(inheritId.c_str(), &inheritFlags[i])) {
					if (inheritFlags[i]) {
						values[i] = parentValues[i];
						changed = true;
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Inherit from parent");
				ImGui::PopStyleVar();
				ImGui::SameLine(0, 2);
			}

			// Slider (disabled if inheriting)
			bool isActive = factors[i] > 0.0f;
			if (!isActive || (inheritFlags && inheritFlags[i]))
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			if (inheritFlags && inheritFlags[i]) {
				values[i] = parentValues[i];
			}

			ImGui::PushItemWidth(sliderWidth);
			std::string id = std::string("##") + label + std::to_string(i);
			ImGui::BeginDisabled(inheritFlags && inheritFlags[i]);
			if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
				changed = true;
				if (inheritFlags)
					inheritFlags[i] = false;
				std::string valueName = std::string(label) + " " + GetPeriodName(i);
				pendingSliderValues[valueName] = values[i];
				sliderLastChangeTime[valueName] = ImGui::GetTime();
			}
			ImGui::EndDisabled();

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%.0f%%", factors[i] * 100.0f);
			ImGui::PopItemWidth();

			if (!isActive || (inheritFlags && inheritFlags[i]))
				ImGui::PopStyleVar();
			
			ImGui::EndGroup();
		}
		
		// Check for any pending values that should be tracked
		std::vector<std::string> toTrack;
		for (const auto& [key, changeTime] : sliderLastChangeTime) {
			if (ImGui::GetTime() - changeTime >= debounceDelay) {
				toTrack.push_back(key);
			}
		}
		
		// Track and remove completed entries
		for (const auto& key : toTrack) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, pendingSliderValues[key]);
			pendingSliderValues.erase(key);
			sliderLastChangeTime.erase(key);
		}

		return changed;
	}

	bool DrawTODColorRow(const char* label, float3 colors[4], bool& inheritFlag, const float3 parentColors[4])
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		
		bool anyActive = false;
		for (int i = 0; i < Count; ++i) {
			if (factors[i] > 0.0f) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		// Draw label text
		ImGui::Text("%s", label);
		
		// Draw inherit checkbox right under the label
		if (parentColors) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			
		std::string inheritId = std::string("##inherit_") + label;
		if (ImGui::Checkbox(inheritId.c_str(), &inheritFlag)) {
				if (inheritFlag) {
					// Copy all parent values
					for (int i = 0; i < Count; ++i) {
						colors[i] = parentColors[i];
					}
					changed = true;
				}
				// Allow unchecking
			}
			
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Inherit from parent weather");
			}
		}
		
		if (!anyActive)
			ImGui::PopStyleVar();

		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;
		const float buttonSize = ImGui::GetFrameHeight() * 1.5f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			ImGui::BeginChild(("##colorcolumn_" + std::string(label) + std::to_string(i)).c_str(),
				ImVec2(columnWidth, buttonSize), false, ImGuiWindowFlags_NoScrollbar);

			float centerOffset = (columnWidth - buttonSize) * 0.5f;
			if (centerOffset > 0.0f)
				ImGui::SetCursorPosX(centerOffset);

			// Apply inherited color if flag is set
			if (inheritFlag && parentColors) {
				colors[i] = parentColors[i];
			}

			std::string id = std::string("##") + label + std::to_string(i);
			ImVec4 color = ImVec4(colors[i].x, colors[i].y, colors[i].z, 1.0f);

			static std::map<std::string, float3> colorCache;
			static std::string activeColorId;

			// Disable editing when inherited
			ImGui::BeginDisabled(inheritFlag);
			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				colorCache[id] = colors[i];
				activeColorId = id;
				ImGui::OpenPopup(id.c_str());
			}

			// Drag-and-drop source (only when not inherited)
			if (!inheritFlag) {
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
					ImGui::SetDragDropPayload("COLOR_DND", &colors[i], sizeof(float3));
					ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoAlpha);
					ImGui::EndDragDropSource();
				}

				// Drag-and-drop target
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLOR_DND")) {
						if (payload->DataSize == sizeof(float3)) {
							float3 droppedColor = *(const float3*)payload->Data;
							colors[i] = droppedColor;
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			// Color picker popup
			if (ImGui::BeginPopup(id.c_str())) {
				if (colorCache.find(id) == colorCache.end()) {
					colorCache[id] = colors[i];
				}

				float3& cachedColor = colorCache[id];
				bool colorChanged = false;

				if (ImGui::ColorPicker3("##picker", &cachedColor.x, ImGuiColorEditFlags_NoAlpha)) {
					colors[i] = cachedColor;
					colorChanged = true;
					changed = true;
				}

				ImGui::EndPopup();

				if (!ImGui::IsPopupOpen(id.c_str()) && activeColorId == id) {
					activeColorId = "";
				}
			}
			ImGui::EndDisabled();

			ImGui::EndChild();
		}

		return changed;
	}

	bool DrawTODFloatRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(columnWidth);
			if (ImGui::SliderFloat("##value", &values[i], minValue, maxValue, format)) {
				changed = true;
			}
			ImGui::PopID();
		}

		return changed;
	}

	bool DrawTODFloatRow(const char* label, float values[4], bool& inheritFlag, const float parentValues[4], float minValue, float maxValue, const char* format)
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		
		ImGui::Text("%s", label);
		
		// Draw inherit checkbox
		if (parentValues) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			
			std::string inheritId = std::string("##inherit_") + label;
			if (ImGui::Checkbox(inheritId.c_str(), &inheritFlag)) {
				if (inheritFlag) {
					for (int i = 0; i < Count; ++i) {
						values[i] = parentValues[i];
					}
					changed = true;
				}
			}
			
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Inherit from parent weather");
			}
		}

		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		ImGui::BeginDisabled(inheritFlag);
		for (int i = 0; i < Count; ++i) {
			if (i > 0) ImGui::SameLine();
			
			// Apply inherited value if flag is set
			if (inheritFlag && parentValues) {
				values[i] = parentValues[i];
			}
			
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(columnWidth);
			if (ImGui::SliderFloat("##value", &values[i], minValue, maxValue, format)) {
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndDisabled();

		return changed;
	}

	bool DrawTODInt8Row(const char* label, int values[4])
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float sliderWidth = (totalWidth - 3 * 8.0f) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			bool isActive = factors[i] > 0.0f;
			if (!isActive)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			ImGui::PushItemWidth(sliderWidth);
			std::string id = std::string("##") + label + std::to_string(i);
			if (ImGui::SliderInt(id.c_str(), &values[i], -128, 127))
				changed = true;

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%.0f%%", factors[i] * 100.0f);

			ImGui::PopItemWidth();

			if (!isActive)
				ImGui::PopStyleVar();
		}

		return changed;
	}

	bool BeginTODTable(const char* tableId)
	{
		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		}
		return false;
	}

	void EndTODTable()
	{
		ImGui::EndTable();
	}
}

bool BeginWidgetSearchBar(char* searchBuffer, size_t bufferSize, bool& searchActive)
{
	// Check for Ctrl+F to activate search
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		ImGui::IsKeyPressed(ImGuiKey_F, false) && ImGui::GetIO().KeyCtrl) {
		searchActive = true;
	}

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.25f, 0.3f, 1.0f));
	ImGui::SetNextItemWidth(-100.0f);

	if (searchActive) {
		ImGui::SetKeyboardFocusHere();
		searchActive = false;
	}

	if (ImGui::InputTextWithHint("##WidgetSearch", "Search parameters... (Ctrl+F)", searchBuffer, bufferSize)) {
		// Text changed
	}

	// Clear button
	ImGui::SameLine();
	if (ImGui::Button("Clear", ImVec2(90, 0))) {
		searchBuffer[0] = '\0';
	}

	ImGui::PopStyleColor();
	ImGui::Separator();

	return searchBuffer[0] != '\0';  // Return true if search is active
}

void EndWidgetSearchBar()
{
	// Currently no cleanup needed, but keeping for symmetry and future use
}
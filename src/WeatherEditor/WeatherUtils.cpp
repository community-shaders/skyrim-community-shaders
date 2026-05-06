#include "WeatherUtils.h"
#include "EditorWindow.h"
#include "PaletteWindow.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"

namespace WeatherUtils::TexturePath
{
	std::string Normalize(std::string_view path)
	{
		std::string result(path);
		std::transform(result.begin(), result.end(), result.begin(),
			[](unsigned char c) { return std::tolower(c); });
		std::replace(result.begin(), result.end(), '/', '\\');
		return result;
	}

	bool HasDdsExtension(std::string_view path)
	{
		return Normalize(path).ends_with(kDdsExtension);
	}

	bool ExistsOnDisk(std::string_view path)
	{
		const std::string lower = Normalize(path);
		if (!lower.ends_with(kDdsExtension))
			return false;

		// Reject absolute paths and ".." traversal
		const std::filesystem::path fsPath(lower);
		if (fsPath.is_absolute())
			return false;
		for (const auto& part : fsPath)
			if (part == "..")
				return false;

		const std::filesystem::path dataPath = Util::PathHelpers::GetDataPath();
		const std::filesystem::path fullPath = lower.starts_with(kTexturePrefix) ?
		                                           dataPath / lower :
		                                           dataPath / kTexturePrefix / lower;

		std::error_code ec;
		return std::filesystem::exists(fullPath, ec) && !ec;
	}

	std::string BuildResourcePath(std::string_view path)
	{
		std::string result(kResourcePrefix);
		result.append(path);
		if (!Normalize(result).ends_with(kDdsExtension))
			result += kDdsExtension;
		return result;
	}
}

namespace WeatherUtils
{
	RE::TESForm* FindFormByEditorID(std::string_view editorID, const std::vector<std::unique_ptr<Widget>>& widgets)
	{
		if (editorID.empty())
			return nullptr;
		for (const auto& w : widgets)
			if (w->GetEditorID() == editorID)
				return w->form;
		return nullptr;
	}

	std::string FindEditorIDByForm(const RE::TESForm* form, const std::vector<std::unique_ptr<Widget>>& widgets)
	{
		if (!form)
			return "";
		for (const auto& w : widgets)
			if (w->form == form)
				return w->GetEditorID();
		return "";
	}
}

// Global widget context for undo tracking
static Widget* g_currentWidget = nullptr;

// Per-widget-type window sizes — shared across all instances of the same widget type
static std::unordered_map<std::string, ImVec2> s_widgetTypeSizes;

void SetupWidgetWindowDefaults(const char* widgetType)
{
	const bool resetting = EditorWindow::GetSingleton()->resetLayout;
	const auto cond = resetting ? ImGuiCond_Always : ImGuiCond_Appearing;
	const ImVec2 defaultSize(WidgetDefaults::kInitialWidth * Util::GetUIScale(), WidgetDefaults::kInitialHeight * Util::GetUIScale());
	auto it = s_widgetTypeSizes.find(widgetType);
	ImGui::SetNextWindowSize(resetting || it == s_widgetTypeSizes.end() ? defaultSize : it->second, cond);
}

void UpdateWidgetTypeSize(const char* widgetType)
{
	if (!EditorWindow::GetSingleton()->resetLayout && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		s_widgetTypeSizes[widgetType] = ImGui::GetWindowSize();
}

void ResetWidgetTypeSizes()
{
	s_widgetTypeSizes.clear();
}

json GetWidgetTypeSizesJson()
{
	json j;
	for (const auto& [type, size] : s_widgetTypeSizes)
		j[type] = { size.x, size.y };
	return j;
}

void SetWidgetTypeSizesFromJson(const json& j)
{
	s_widgetTypeSizes.clear();
	for (auto& [key, val] : j.items()) {
		if (val.is_array() && val.size() == 2 && val[0].is_number() && val[1].is_number()) {
			float w = std::max(val[0].get<float>(), WidgetDefaults::kMinWidth);
			float h = std::max(val[1].get<float>(), WidgetDefaults::kMinHeight);
			s_widgetTypeSizes[key] = ImVec2(w, h);
		}
	}
}

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
	color.red = FloatToUint8(f3.x);
	color.green = FloatToUint8(f3.y);
	color.blue = FloatToUint8(f3.z);
}

void ColorToFloat3(const RE::Color& color, float3& f3)
{
	f3.x = Uint8ToFloat(color.red);
	f3.y = Uint8ToFloat(color.green);
	f3.z = Uint8ToFloat(color.blue);
}

void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& f3)
{
	f3.x = Uint8ToFloat(color.red);
	f3.y = Uint8ToFloat(color.green);
	f3.z = Uint8ToFloat(color.blue);
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
	void SetCurrentWidget(Widget* widget)
	{
		g_currentWidget = widget;
	}

	// Static debounced trackers for undo and palette tracking
	static DebouncedTracker<int> s_int8Tracker;
	static DebouncedTracker<float> s_floatTracker;

	bool DrawSliderInt8(const std::string& label, int& property)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		bool changed = ImGui::SliderInt(label.c_str(), &property, -127, 127);
		bool isNowActive = ImGui::IsItemActive();

		// Push undo state when slider becomes active
		if (s_int8Tracker.UpdateActiveState(label, isNowActive, currentTime, debounceDelay)) {
			if (g_currentWidget) {
				EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
			}
		}

		if (changed) {
			s_int8Tracker.OnValueChanged(label, property, currentTime);
		}

		// Track completed values to palette
		auto completed = s_int8Tracker.GetCompletedEntries(currentTime, debounceDelay);
		for (const auto& [key, value] : completed) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, static_cast<float>(value));
		}

		return changed;
	}

	bool DrawColorEdit(const std::string& l, float3& property, Widget* widget)
	{
		static std::map<std::string, float3> colorCache;
		static std::string activeColorId;
		static std::map<std::string, bool> wasPickerOpen;

		std::string cacheId = l;
		bool isActive = ImGui::IsPopupOpen(l.c_str(), ImGuiPopupFlags_AnyPopupId);
		bool wasActive = wasPickerOpen[cacheId];

		// Cache the original color and push undo state when picker is first activated
		if (isActive && activeColorId != cacheId) {
			colorCache[cacheId] = property;
			activeColorId = cacheId;
			// Push undo state before change (prefer parameter, fallback to global)
			Widget* w = widget ? widget : g_currentWidget;
			if (w) {
				EditorWindow::GetSingleton()->PushUndoState(w);
			}
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

	bool DrawSliderFloat(const std::string& label, float& property, float min, float max, Widget* widget)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		bool changed = ImGui::SliderFloat(label.c_str(), &property, min, max);
		bool isNowActive = ImGui::IsItemActive();

		// Push undo state when slider becomes active
		if (s_floatTracker.UpdateActiveState(label, isNowActive, currentTime, debounceDelay)) {
			Widget* w = widget ? widget : g_currentWidget;
			if (w) {
				EditorWindow::GetSingleton()->PushUndoState(w);
			}
		}

		if (changed) {
			s_floatTracker.OnValueChanged(label, property, currentTime);
		}

		// Track completed values to palette
		auto completed = s_floatTracker.GetCompletedEntries(currentTime, debounceDelay);
		for (const auto& [key, value] : completed) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, value);
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

	// Static debounced tracker for TOD slider rows
	static DebouncedTracker<float> s_todSliderTracker;

	static void DrawCenteredLabel(const char* label)
	{
		ImGui::AlignTextToFramePadding();
		float colWidth = ImGui::GetColumnWidth();
		float textWidth = ImGui::CalcTextSize(label).x;
		float offset = (colWidth - textWidth) * 0.5f;
		if (offset > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
		ImGui::Text("%s", label);
	}

	bool DrawTODSliderRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawCenteredLabel(label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float sliderWidth = (totalWidth - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			bool isActive = factors[i] > 0.0f;
			if (!isActive)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			ImGui::PushItemWidth(sliderWidth);
			std::string id = std::string("##") + label + std::to_string(i);
			std::string valueName = std::string(label) + " " + GetPeriodName(i);

			if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
				changed = true;
				s_todSliderTracker.OnValueChanged(valueName, values[i], currentTime);
			}

			Util::AddTooltip(std::format("{:.0f}%", factors[i] * 100.0f).c_str());
			ImGui::PopItemWidth();

			if (!isActive)
				ImGui::PopStyleVar();
		}

		// Track completed entries to palette
		for (const auto& [key, value] : s_todSliderTracker.GetCompletedEntries(currentTime, debounceDelay)) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, value);
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

		DrawCenteredLabel(label);

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

			// Push undo state when popup first opens
			if (isPopupOpen && !wasOpen) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			if (isPopupOpen) {
				// Check for Ctrl+Z while picker is active
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z)) {
					if (colorCache.contains(id)) {
						colors[i] = colorCache[id];
						changed = true;
					}
				}

				// Use ColorPicker4 with ref_col to show original color preview
				float col4[4] = { colors[i].x, colors[i].y, colors[i].z, 1.0f };
				float refCol[4] = { colorCache[id].x, colorCache[id].y, colorCache[id].z, 1.0f };
				if (ImGui::ColorPicker4((id + "_picker").c_str(), col4, ImGuiColorEditFlags_NoAlpha, refCol)) {
					colors[i] = { col4[0], col4[1], col4[2] };
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

			Util::AddTooltip(std::format("{} - {:.0f}%", GetPeriodName(i), factors[i] * 100.0f).c_str());

			ImGui::EndChild();
		}

		return changed;
	}

	// Static debounced tracker for TOD slider rows with inheritance
	static DebouncedTracker<float> s_todSliderInheritTracker;

	bool DrawTODSliderRow(const char* label, float values[4], bool inheritFlags[4], const float parentValues[4], float minValue, float maxValue, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawCenteredLabel(label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		const float scale = Util::GetUIScale();
		float checkboxWidth = 20.0f * scale;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float sliderWidth = (totalWidth - (static_cast<int>(Count) - 1) * spacing - (parentValues ? static_cast<int>(Count) * checkboxWidth : 0)) / static_cast<float>(Count);

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			ImGui::BeginGroup();

			// Per-column inherit checkbox
			if (parentValues) {
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1 * scale, 1 * scale));
				ImGui::SetNextItemWidth(checkboxWidth);
				std::string inheritId = std::string("##inherit_") + label + std::to_string(i);
				if (ImGui::Checkbox(inheritId.c_str(), &inheritFlags[i])) {
					if (inheritFlags[i]) {
						values[i] = parentValues[i];
						changed = true;
					}
				}
				Util::AddTooltip("Inherit from parent");
				ImGui::PopStyleVar();
				ImGui::SameLine(0, 2 * scale);
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
			std::string itemKey = std::string(label) + "_slider_" + std::to_string(i);

			ImGui::BeginDisabled(inheritFlags && inheritFlags[i]);
			if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
				changed = true;
				if (inheritFlags)
					inheritFlags[i] = false;
				std::string valueName = std::string(label) + " " + GetPeriodName(i);
				s_todSliderInheritTracker.OnValueChanged(valueName, values[i], currentTime);
			}

			// Push undo state when slider becomes active
			bool isNowActive = ImGui::IsItemActive();
			if (s_todSliderInheritTracker.UpdateActiveState(itemKey, isNowActive, currentTime, debounceDelay)) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			ImGui::EndDisabled();

			Util::AddTooltip(std::format("{:.0f}%", factors[i] * 100.0f).c_str());
			ImGui::PopItemWidth();

			if (!isActive || (inheritFlags && inheritFlags[i]))
				ImGui::PopStyleVar();

			ImGui::EndGroup();
		}

		// Track completed entries to palette
		for (const auto& [key, value] : s_todSliderInheritTracker.GetCompletedEntries(currentTime, debounceDelay)) {
			PaletteWindow::GetSingleton()->TrackValueUsage(key, value);
		}

		return changed;
	}

	bool DrawTODColorRow(const char* label, float3 colors[4], bool& inheritFlag, const float3 parentColors[4])
	{
		const float scale = Util::GetUIScale();
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
		DrawCenteredLabel(label);

		// Draw inherit checkbox right under the label
		if (parentColors) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * scale, 2 * scale));

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

			Util::AddTooltip("Inherit from parent weather");
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
			static std::map<std::string, float3> originalColorCache;
			static std::string activeColorId;

			// Disable editing when inherited
			ImGui::BeginDisabled(inheritFlag);
			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				colorCache[id] = colors[i];
				originalColorCache[id] = colors[i];
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
			static std::map<std::string, bool> wasPopupOpenInherit;
			bool isPopupOpen = ImGui::BeginPopup(id.c_str());
			bool wasOpen = wasPopupOpenInherit[id];

			// Push undo state when popup first opens
			if (isPopupOpen && !wasOpen) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			if (isPopupOpen) {
				if (colorCache.find(id) == colorCache.end()) {
					colorCache[id] = colors[i];
				}
				if (originalColorCache.find(id) == originalColorCache.end()) {
					originalColorCache[id] = colors[i];
				}

				float3& cachedColor = colorCache[id];
				bool colorChanged = false;

				// Use ColorPicker4 with ref_col to show original color preview
				float col4[4] = { cachedColor.x, cachedColor.y, cachedColor.z, 1.0f };
				float refCol[4] = { originalColorCache[id].x, originalColorCache[id].y, originalColorCache[id].z, 1.0f };
				if (ImGui::ColorPicker4("##picker", col4, ImGuiColorEditFlags_NoAlpha, refCol)) {
					cachedColor = { col4[0], col4[1], col4[2] };
					colors[i] = cachedColor;
					colorChanged = true;
					changed = true;
				}

				ImGui::EndPopup();

				if (!ImGui::IsPopupOpen(id.c_str()) && activeColorId == id) {
					activeColorId = "";
				}
			}

			wasPopupOpenInherit[id] = isPopupOpen;
			ImGui::EndDisabled();

			ImGui::EndChild();
		}

		return changed;
	}

	// Static debounced tracker for TOD float rows
	static DebouncedTracker<float> s_todFloatTracker;

	bool DrawTODFloatRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawCenteredLabel(label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);

			std::string itemId = std::string(label) + "_" + std::to_string(i);

			ImGui::SetNextItemWidth(columnWidth);
			if (ImGui::SliderFloat("##value", &values[i], minValue, maxValue, format)) {
				changed = true;
			}

			// Push undo state when slider becomes active
			bool isNowActive = ImGui::IsItemActive();
			if (s_todFloatTracker.UpdateActiveState(itemId, isNowActive, currentTime, debounceDelay)) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			ImGui::PopID();
		}

		return changed;
	}

	bool DrawTODFloatRow(const char* label, float values[4], bool& inheritFlag, const float parentValues[4], float minValue, float maxValue, const char* format)
	{
		const float scale = Util::GetUIScale();
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		DrawCenteredLabel(label);

		// Draw inherit checkbox
		if (parentValues) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * scale, 2 * scale));

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

			Util::AddTooltip("Inherit from parent weather");
		}

		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		ImGui::BeginDisabled(inheritFlag);
		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

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
		DrawCenteredLabel(label);
		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float sliderWidth = (totalWidth - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;

		for (int i = 0; i < Count; ++i) {
			if (i > 0)
				ImGui::SameLine();

			bool isActive = factors[i] > 0.0f;
			if (!isActive)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			ImGui::PushItemWidth(sliderWidth);
			std::string id = std::string("##") + label + std::to_string(i);
			if (ImGui::SliderInt(id.c_str(), &values[i], -127, 127))
				changed = true;

			Util::AddTooltip(std::format("{:.0f}%", factors[i] * 100.0f).c_str());

			ImGui::PopItemWidth();

			if (!isActive)
				ImGui::PopStyleVar();
		}

		return changed;
	}

	bool BeginTODTable(const char* tableId, float paramColumnWidth)
	{
		if (paramColumnWidth <= 0.0f)
			paramColumnWidth = WidgetDefaults::kTODLabelWidth;
		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, paramColumnWidth * Util::GetUIScale());
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		}
		return false;
	}

	void EndTODTable()
	{
		ImGui::EndTable();
	}

	void DrawTODSeparator()
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();
	}
}

bool BeginWidgetSearchBar(char* searchBuffer, size_t bufferSize, bool& searchActive)
{
	const float scale = Util::GetUIScale();

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		ImGui::IsKeyPressed(ImGuiKey_F, false) && ImGui::GetIO().KeyCtrl) {
		searchActive = true;
	}

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.25f, 0.3f, 1.0f));
	ImGui::SetNextItemWidth(-100.0f * scale);

	if (searchActive) {
		ImGui::SetKeyboardFocusHere();
		searchActive = false;
	}

	if (ImGui::InputTextWithHint("##WidgetSearch", "Search parameters... (Ctrl+F)", searchBuffer, bufferSize)) {
		// Text changed
	}

	// Clear button
	ImGui::SameLine();
	if (Util::ButtonWithFlash("Clear", ImVec2(90 * scale, 0))) {
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

// ============================================================================
// PropertyDrawer Implementation - Consolidates repeated table property drawing
// ============================================================================
namespace PropertyDrawer
{
	bool BeginTable(const char* tableId, float labelWidth)
	{
		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, labelWidth * Util::GetUIScale());
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		}
		return false;
	}

	void EndTable()
	{
		ImGui::EndTable();
	}

	void DrawSeparator()
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();
	}

	void DrawLabel(const char* label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", label);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);
	}

	bool MatchesSearch(const char* label, const char* searchBuffer)
	{
		if (!searchBuffer || searchBuffer[0] == '\0')
			return true;
		return ContainsStringIgnoreCase(label, searchBuffer);
	}

	bool DrawFloat(const char* label, float& value, float minVal, float maxVal,
		const char* searchBuffer, const char* format)
	{
		if (!MatchesSearch(label, searchBuffer))
			return false;

		DrawLabel(label);

		std::string id = std::string("##") + label;
		return ImGui::SliderFloat(id.c_str(), &value, minVal, maxVal, format);
	}

	bool DrawInt(const char* label, int& value, int minVal, int maxVal, const char* searchBuffer)
	{
		if (!MatchesSearch(label, searchBuffer))
			return false;

		DrawLabel(label);

		std::string id = std::string("##") + label;
		return ImGui::SliderInt(id.c_str(), &value, minVal, maxVal);
	}

	bool DrawColor(const char* label, float3& value, const char* searchBuffer)
	{
		if (!MatchesSearch(label, searchBuffer))
			return false;

		DrawLabel(label);

		return WeatherUtils::DrawColorEdit(label, value);
	}

	bool DrawCheckbox(const char* label, bool& value, const char* searchBuffer)
	{
		if (!MatchesSearch(label, searchBuffer))
			return false;

		DrawLabel(label);

		std::string id = std::string("##") + label;
		return ImGui::Checkbox(id.c_str(), &value);
	}
}  // namespace PropertyDrawer
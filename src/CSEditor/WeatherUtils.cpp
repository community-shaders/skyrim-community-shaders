#include "WeatherUtils.h"
#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "PaletteWindow.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

#include <algorithm>
#include <cassert>

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

template <class DrawFn>
auto DrawWithWidgetHighlight(Widget* widget, const std::string& settingId, DrawFn draw)
{
	return widget ? widget->DrawWithHighlight(settingId, draw) : draw();
}

// Compose a per-widget-scoped key so global static maps in this file
// (color caches, popup-open trackers, debounced trackers) don't collide
// when two widgets share a label string (e.g. "Specular", "Fresnel Power").
constexpr std::string_view kScopeSep = "::";

static std::string ScopedKey(std::string_view label)
{
	return std::format("{}{}{}", static_cast<const void*>(g_currentWidget), kScopeSep, label);
}

// Recover the original label from a scoped key for use as a palette/UI key.
static std::string_view UnscopeKey(std::string_view key)
{
	const auto pos = key.find(kScopeSep);
	return pos == std::string_view::npos ? key : key.substr(pos + kScopeSep.size());
}

const char* WeatherUtils::TranslateControlLabel(std::string_view label)
{
	if (label == "Ambient Color")
		return T(TKEY("ambient_color"), "Ambient Color");
	if (label == "Directional Color")
		return T(TKEY("directional_color"), "Directional Color");
	if (label == "Directional XY")
		return T(TKEY("directional_xy"), "Directional XY");
	if (label == "Directional Z")
		return T(TKEY("directional_z"), "Directional Z");
	if (label == "Directional Fade")
		return T(TKEY("directional_fade"), "Directional Fade");
	if (label == "Light Fade Start")
		return T(TKEY("light_fade_start"), "Light Fade Start");
	if (label == "Light Fade End")
		return T(TKEY("light_fade_end"), "Light Fade End");
	if (label == "Clip Distance")
		return T(TKEY("clip_distance"), "Clip Distance");
	if (label == "Fog Color Near" || label == "Fog Near Color")
		return T(TKEY("fog_color_near"), "Fog Color Near");
	if (label == "Fog Color Far" || label == "Fog Far Color")
		return T(TKEY("fog_color_far"), "Fog Color Far");
	if (label == "Fog Near")
		return T(TKEY("color_fog_near"), "Fog Near");
	if (label == "Fog Far")
		return T(TKEY("color_fog_far"), "Fog Far");
	if (label == "Fog Power")
		return T(TKEY("fog_power"), "Fog Power");
	if (label == "Fog Clamp" || label == "Fog Clamp (Max)")
		return T(TKEY("fog_clamp"), "Fog Clamp");
	if (label == "Specular")
		return T(TKEY("dalc_specular"), "Specular");
	if (label == "Fresnel Power")
		return T(TKEY("dalc_fresnel_power"), "Fresnel Power");
	if (label == "X+ (Right)")
		return T(TKEY("direction_x_plus"), "X+ (Right)");
	if (label == "X- (Left)")
		return T(TKEY("direction_x_minus"), "X- (Left)");
	if (label == "Y+ (Front)")
		return T(TKEY("direction_y_plus"), "Y+ (Front)");
	if (label == "Y- (Back)")
		return T(TKEY("direction_y_minus"), "Y- (Back)");
	if (label == "Z+ (Up)")
		return T(TKEY("direction_z_plus"), "Z+ (Up)");
	if (label == "Z- (Down)")
		return T(TKEY("direction_z_minus"), "Z- (Down)");
	if (label == "XY Rotation")
		return T(TKEY("xy_rotation"), "XY Rotation");
	if (label == "Z Rotation")
		return T(TKEY("z_rotation"), "Z Rotation");
	if (label == "Inherit Ambient Color")
		return T(TKEY("inherit_ambient_color"), "Inherit Ambient Color");
	if (label == "Inherit Directional Color")
		return T(TKEY("inherit_directional_color"), "Inherit Directional Color");
	if (label == "Inherit Fog Color")
		return T(TKEY("inherit_fog_color"), "Inherit Fog Color");
	if (label == "Inherit Fog Near")
		return T(TKEY("inherit_fog_near"), "Inherit Fog Near");
	if (label == "Inherit Fog Far")
		return T(TKEY("inherit_fog_far"), "Inherit Fog Far");
	if (label == "Inherit Directional Rotation")
		return T(TKEY("inherit_directional_rotation"), "Inherit Directional Rotation");
	if (label == "Inherit Directional Fade")
		return T(TKEY("inherit_directional_fade"), "Inherit Directional Fade");
	if (label == "Inherit Clip Distance")
		return T(TKEY("inherit_clip_distance"), "Inherit Clip Distance");
	if (label == "Inherit Fog Power")
		return T(TKEY("inherit_fog_power"), "Inherit Fog Power");
	if (label == "Inherit Fog Max (Clamp)")
		return T(TKEY("inherit_fog_max_clamp"), "Inherit Fog Max (Clamp)");
	if (label == "Inherit Light Fade Distances")
		return T(TKEY("inherit_light_fade_distances"), "Inherit Light Fade Distances");
	if (label == "Type")
		return T(TKEY("type"), "Type");
	if (label == "Size X")
		return T(TKEY("size_x"), "Size X");
	if (label == "Size Y")
		return T(TKEY("size_y"), "Size Y");
	if (label == "Gravity Velocity")
		return T(TKEY("gravity_velocity"), "Gravity Velocity");
	if (label == "Rotation Velocity")
		return T(TKEY("rotation_velocity"), "Rotation Velocity");
	if (label == "Center Offset Min")
		return T(TKEY("center_offset_min"), "Center Offset Min");
	if (label == "Center Offset Max")
		return T(TKEY("center_offset_max"), "Center Offset Max");
	if (label == "Start Rotation Range")
		return T(TKEY("start_rotation_range"), "Start Rotation Range");
	if (label == "Box Size")
		return T(TKEY("box_size"), "Box Size");
	if (label == "Particle Density")
		return T(TKEY("particle_density_label"), "Particle Density");
	if (label == "Num Subtextures X")
		return T(TKEY("num_subtextures_x"), "Num Subtextures X");
	if (label == "Num Subtextures Y")
		return T(TKEY("num_subtextures_y"), "Num Subtextures Y");
	if (label == "Particle Texture")
		return T(TKEY("particle_texture_label"), "Particle Texture");
	if (label == "Art Object")
		return T(TKEY("art_object"), "Art Object");
	if (label == "Effect Shader")
		return T(TKEY("effect_shader"), "Effect Shader");
	if (label == "Face Target")
		return T(TKEY("face_target"), "Face Target");
	if (label == "Attach To Camera")
		return T(TKEY("attach_to_camera"), "Attach To Camera");
	if (label == "Inherit Rotation")
		return T(TKEY("inherit_rotation"), "Inherit Rotation");
	if (label == "Intensity")
		return T(TKEY("intensity"), "Intensity");
	if (label == "Contribution")
		return T(TKEY("contribution"), "Contribution");
	if (label == "Color")
		return T(TKEY("color"), "Color");
	if (label == "Size")
		return T(TKEY("size"), "Size");
	if (label == "Wind Speed")
		return T(TKEY("wind_speed"), "Wind Speed");
	if (label == "Falling Speed")
		return T(TKEY("falling_speed"), "Falling Speed");
	if (label == "Scattering")
		return T(TKEY("scattering"), "Scattering");
	if (label == "Range Factor")
		return T(TKEY("range_factor"), "Range Factor");
	if (label == "Cloud Layer Speed X")
		return T(TKEY("cloud_layer_speed_x"), "Cloud Layer Speed X");
	if (label == "Cloud Layer Speed Y")
		return T(TKEY("cloud_layer_speed_y"), "Cloud Layer Speed Y");

	// Fallback: return the original label via T() which caches a stable null-terminated copy
	return T(std::string(label).c_str(), std::string(label).c_str());
}

static std::string BuildLocalizedControlLabel(const std::string& label)
{
	if (label.starts_with("##"))
		return label;

	const auto idPos = label.find("##");
	const auto visible = idPos == std::string::npos ? label : label.substr(0, idPos);
	const auto id = idPos == std::string::npos ? visible : label.substr(idPos + 2);
	const bool numericId = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); });
	const auto key = numericId ? visible : id;
	const auto* translated = WeatherUtils::TranslateControlLabel(key);
	if (std::string_view(translated) == key)
		return label;

	return std::format("{}##{}", translated, id);
}

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

void PushInheritedStyle()
{
	const auto w = Util::Colors::GetWarning();
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(w.x, w.y, w.z, 0.25f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(w.x, w.y, w.z, 0.35f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(w.x, w.y, w.z, 0.45f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, w);
}

void PopInheritedStyle()
{
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
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
		label = T(TKEY("tod_sunrise"), "Sunrise");
		break;
	case 1:
		label = T(TKEY("tod_day"), "Day");
		break;
	case 2:
		label = T(TKEY("tod_sunset"), "Sunset");
		break;
	case 3:
		label = T(TKEY("tod_night"), "Night");
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
		label = T(TKEY("color_sky_upper"), "Sky Upper");
		break;
	case 1:
		label = T(TKEY("color_fog_near"), "Fog Near");
		break;
	case 2:
		label = T(TKEY("unknown"), "Unknown");
		break;
	case 3:
		label = T(TKEY("color_ambient"), "Ambient");
		break;
	case 4:
		label = T(TKEY("color_sunlight"), "Sunlight");
		break;
	case 5:
		label = T(TKEY("color_sun"), "Sun");
		break;
	case 6:
		label = T(TKEY("color_stars"), "Stars");
		break;
	case 7:
		label = T(TKEY("color_sky_lower"), "Sky Lower");
		break;
	case 8:
		label = T(TKEY("color_horizon"), "Horizon");
		break;
	case 9:
		label = T(TKEY("color_effect_lighting"), "Effect Lighting");
		break;
	case 10:
		label = T(TKEY("color_cloud_lod_diffuse"), "Cloud LOD Diffuse");
		break;
	case 11:
		label = T(TKEY("color_cloud_lod_ambient"), "Cloud LOD Ambient");
		break;
	case 12:
		label = T(TKEY("color_fog_far"), "Fog Far");
		break;
	case 13:
		label = T(TKEY("color_sky_statics"), "Sky Statics");
		break;
	case 14:
		label = T(TKEY("color_water_multiplier"), "Water Multiplier");
		break;
	case 15:
		label = T(TKEY("color_sun_glare"), "Sun Glare");
		break;
	case 16:
		label = T(TKEY("color_moon_glare"), "Moon Glare");
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
		const auto displayLabel = BuildLocalizedControlLabel(label);

		bool changed = DrawWithWidgetHighlight(g_currentWidget, label, [&]() {
			return ImGui::SliderInt(displayLabel.c_str(), &property, -127, 127);
		});
		bool isNowActive = ImGui::IsItemActive();

		const std::string trackerKey = ScopedKey(label);

		// Push undo state when slider becomes active
		if (s_int8Tracker.UpdateActiveState(trackerKey, isNowActive, currentTime, debounceDelay)) {
			if (g_currentWidget) {
				EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
			}
		}

		if (changed) {
			s_int8Tracker.OnValueChanged(trackerKey, property, currentTime);
		}

		// Track completed values to palette (strip widget prefix from palette key)
		auto completed = s_int8Tracker.GetCompletedEntries(currentTime, debounceDelay);
		for (const auto& [key, value] : completed) {
			PaletteWindow::GetSingleton()->TrackValueUsage(std::string(UnscopeKey(key)), static_cast<float>(value));
		}

		return changed;
	}

	bool DrawColorEdit(const std::string& l, float3& property, Widget* widget)
	{
		static std::map<std::string, float3> colorCache;
		static std::string activeColorId;
		static std::map<std::string, bool> wasPickerOpen;

		// Strip leading "##" so hidden-id callers still match highlight/search ids.
		const std::string hid = l.starts_with("##") ? l.substr(2) : l;

		Widget* effectiveWidget = widget ? widget : g_currentWidget;
		if (effectiveWidget && !effectiveWidget->MatchesSearch(hid))
			return false;

		const std::string cacheId = effectiveWidget ?
		                                std::format("{}{}{}", static_cast<const void*>(effectiveWidget), kScopeSep, hid) :
		                                hid;
		const auto displayLabel = BuildLocalizedControlLabel(l);
		bool isActive = ImGui::IsPopupOpen(displayLabel.c_str(), ImGuiPopupFlags_AnyPopupId);
		bool wasActive = wasPickerOpen[cacheId];

		// Cache the original color and push undo state when picker is first activated
		if (isActive && activeColorId != cacheId) {
			colorCache[cacheId] = property;
			activeColorId = cacheId;
			// Push undo state before change
			if (effectiveWidget) {
				EditorWindow::GetSingleton()->PushUndoState(effectiveWidget);
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

		bool changed = DrawWithWidgetHighlight(effectiveWidget, hid, [&]() {
			return ImGui::ColorEdit3(displayLabel.c_str(), (float*)&property);
		});

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
		const auto displayLabel = BuildLocalizedControlLabel(label);
		bool changed = DrawWithWidgetHighlight(g_currentWidget, label, [&]() {
			return ImGui::SliderInt(displayLabel.c_str(), &property, 0, 255);
		});
		return changed;
	}

	bool DrawSliderFloat(const std::string& label, float& property, float min, float max, Widget* widget, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		// Strip leading "##" so hidden-label sliders still match highlight/search ids.
		std::string hid = label.starts_with("##") ? label.substr(2) : label;
		const auto displayLabel = BuildLocalizedControlLabel(label);
		Widget* w = widget ? widget : g_currentWidget;
		if (w && !w->MatchesSearch(hid))
			return false;

		bool changed = DrawWithWidgetHighlight(w, hid, [&]() {
			return ImGui::SliderFloat(displayLabel.c_str(), &property, min, max, format);
		});
		bool isNowActive = ImGui::IsItemActive();

		const std::string trackerKey = w ?
		                                   std::format("{}{}{}", static_cast<const void*>(w), kScopeSep, hid) :
		                                   hid;

		// Push undo state when slider becomes active
		if (s_floatTracker.UpdateActiveState(trackerKey, isNowActive, currentTime, debounceDelay)) {
			if (w) {
				EditorWindow::GetSingleton()->PushUndoState(w);
			}
		}

		if (changed) {
			s_floatTracker.OnValueChanged(trackerKey, property, currentTime);
		}

		// Track completed values to palette (strip widget prefix from palette key)
		auto completed = s_floatTracker.GetCompletedEntries(currentTime, debounceDelay);
		for (const auto& [key, value] : completed) {
			PaletteWindow::GetSingleton()->TrackValueUsage(std::string(UnscopeKey(key)), value);
		}

		return changed;
	}

	bool DrawCheckbox(const std::string& label, bool& value, Widget* widget)
	{
		Widget* w = widget ? widget : g_currentWidget;
		const std::string hid = label.starts_with("##") ? label.substr(2) : label;
		const auto displayLabel = BuildLocalizedControlLabel(label);
		if (w && !w->MatchesSearch(hid))
			return false;

		return DrawWithWidgetHighlight(w, hid, [&]() {
			return ImGui::Checkbox(displayLabel.c_str(), &value);
		});
	}
}

// Time of Day (TOD) helper implementation
namespace TOD
{
	const char* GetPeriodName(int index)
	{
		switch (index) {
		case Sunrise:
			return T(TKEY("tod_sunrise"), "Sunrise");
		case Day:
			return T(TKEY("tod_day"), "Day");
		case Sunset:
			return T(TKEY("tod_sunset"), "Sunset");
		case Night:
			return T(TKEY("tod_night"), "Night");
		default:
			return T(TKEY("unknown"), "Unknown");
		}
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
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetDisabled());

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

	static bool PushTODHighlight(const char* label)
	{
		return g_currentWidget && g_currentWidget->PushHighlightIfNeeded(label);
	}

	static void PopTODHighlight(const char* label, bool pushed)
	{
		if (g_currentWidget)
			g_currentWidget->PopHighlightIfNeeded(label, pushed);
	}

	bool DrawTODSliderRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();

		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

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
			std::string trackerKey = ScopedKey(valueName);

			if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
				changed = true;
				s_todSliderTracker.OnValueChanged(trackerKey, values[i], currentTime);
			}

			Util::AddTooltip(std::format("{:.0f}%", factors[i] * 100.0f).c_str());
			ImGui::PopItemWidth();

			if (!isActive)
				ImGui::PopStyleVar();
		}

		// Track completed entries to palette
		for (const auto& [key, value] : s_todSliderTracker.GetCompletedEntries(currentTime, debounceDelay)) {
			PaletteWindow::GetSingleton()->TrackValueUsage(std::string(UnscopeKey(key)), value);
		}

		PopTODHighlight(label, highlighted);
		return changed;
	}

	bool DrawTODColorRow(const char* label, float3 colors[4])
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

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
			std::string scopedId = ScopedKey(id);
			ImVec4 color = ImVec4(colors[i].x, colors[i].y, colors[i].z, 1.0f);

			static std::map<std::string, float3> colorCache;
			static std::string activeColorId;

			// Use ColorButton with fixed size - no alpha styling on the button itself
			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				colorCache[scopedId] = colors[i];
				activeColorId = scopedId;
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
			bool wasOpen = wasPopupOpen[scopedId];

			// Push undo state when popup first opens
			if (isPopupOpen && !wasOpen) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			if (isPopupOpen) {
				// Check for Ctrl+Z while picker is active
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z)) {
					if (colorCache.contains(scopedId)) {
						colors[i] = colorCache[scopedId];
						changed = true;
					}
				}

				// Use ColorPicker4 with ref_col to show original color preview
				float col4[4] = { colors[i].x, colors[i].y, colors[i].z, 1.0f };
				float refCol[4] = { colorCache[scopedId].x, colorCache[scopedId].y, colorCache[scopedId].z, 1.0f };
				if (ImGui::ColorPicker4((id + "_picker").c_str(), col4, ImGuiColorEditFlags_NoAlpha, refCol)) {
					colors[i] = { col4[0], col4[1], col4[2] };
					changed = true;
				}
				ImGui::EndPopup();
			} else if (activeColorId == scopedId) {
				activeColorId.clear();
			}

			// Track color usage only when popup closes
			if (wasOpen && !isPopupOpen) {
				PaletteWindow::GetSingleton()->TrackColorUsage(colors[i]);
			}

			wasPopupOpen[scopedId] = isPopupOpen;

			Util::AddTooltip(std::format("{} - {:.0f}%", GetPeriodName(i), factors[i] * 100.0f).c_str());

			ImGui::EndChild();
		}

		PopTODHighlight(label, highlighted);
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
		const bool highlighted = PushTODHighlight(label);

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
				Util::AddTooltip(T(TKEY("inherit_from_parent"), "Inherit from parent"));
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

			const bool isInherited = inheritFlags && inheritFlags[i];
			if (isInherited)
				PushInheritedStyle();

			ImGui::PushItemWidth(sliderWidth);
			std::string id = std::string("##") + label + std::to_string(i);
			std::string itemKey = ScopedKey(std::string(label) + "_slider_" + std::to_string(i));

			ImGui::BeginDisabled(isInherited);
			if (ImGui::SliderFloat(id.c_str(), &values[i], minValue, maxValue, format)) {
				changed = true;
				if (inheritFlags)
					inheritFlags[i] = false;
				std::string valueName = ScopedKey(std::string(label) + " " + GetPeriodName(i));
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

			Util::AddTooltip(isInherited ? T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather") : std::format("{:.0f}%", factors[i] * 100.0f).c_str());
			ImGui::PopItemWidth();

			if (isInherited)
				PopInheritedStyle();

			if (!isActive || (inheritFlags && inheritFlags[i]))
				ImGui::PopStyleVar();

			ImGui::EndGroup();
		}

		// Track completed entries to palette
		for (const auto& [key, value] : s_todSliderInheritTracker.GetCompletedEntries(currentTime, debounceDelay)) {
			PaletteWindow::GetSingleton()->TrackValueUsage(std::string(UnscopeKey(key)), value);
		}

		PopTODHighlight(label, highlighted);
		return changed;
	}

	bool DrawTODColorRow(const char* label, float3 colors[4], bool& inheritFlag, const float3 parentColors[4])
	{
		const float scale = Util::GetUIScale();
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

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
			ImGui::PushStyleColor(ImGuiCol_FrameBg, WidgetUI::kInheritCheckboxFrameBg);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, WidgetUI::kInheritCheckboxMark);
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

			Util::AddTooltip(T(TKEY("inherit_from_parent_weather"), "Inherit from parent weather"));
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
			std::string scopedId = ScopedKey(id);
			ImVec4 color = ImVec4(colors[i].x, colors[i].y, colors[i].z, 1.0f);

			static std::map<std::string, float3> colorCache;
			static std::map<std::string, float3> originalColorCache;
			static std::string activeColorId;

			if (inheritFlag)
				PushInheritedStyle();

			// Disable editing when inherited
			ImGui::BeginDisabled(inheritFlag);
			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				colorCache[scopedId] = colors[i];
				originalColorCache[scopedId] = colors[i];
				activeColorId = scopedId;
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
			bool wasOpen = wasPopupOpenInherit[scopedId];

			// Push undo state when popup first opens
			if (isPopupOpen && !wasOpen) {
				if (g_currentWidget) {
					EditorWindow::GetSingleton()->PushUndoState(g_currentWidget);
				}
			}

			if (isPopupOpen) {
				if (colorCache.find(scopedId) == colorCache.end()) {
					colorCache[scopedId] = colors[i];
				}
				if (originalColorCache.find(scopedId) == originalColorCache.end()) {
					originalColorCache[scopedId] = colors[i];
				}

				float3& cachedColor = colorCache[scopedId];

				// Use ColorPicker4 with ref_col to show original color preview
				float col4[4] = { cachedColor.x, cachedColor.y, cachedColor.z, 1.0f };
				float refCol[4] = { originalColorCache[scopedId].x, originalColorCache[scopedId].y, originalColorCache[scopedId].z, 1.0f };
				if (ImGui::ColorPicker4("##picker", col4, ImGuiColorEditFlags_NoAlpha, refCol)) {
					cachedColor = { col4[0], col4[1], col4[2] };
					colors[i] = cachedColor;
					changed = true;
				}

				ImGui::EndPopup();

				if (!ImGui::IsPopupOpen(id.c_str()) && activeColorId == scopedId) {
					activeColorId = "";
				}
			}

			wasPopupOpenInherit[scopedId] = isPopupOpen;
			ImGui::EndDisabled();

			if (inheritFlag) {
				Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
				PopInheritedStyle();
			}

			ImGui::EndChild();
		}

		PopTODHighlight(label, highlighted);
		return changed;
	}

	// Static debounced tracker for TOD float rows
	static DebouncedTracker<float> s_todFloatTracker;

	bool DrawTODFloatRow(const char* label, float values[4], float minValue, float maxValue, const char* format)
	{
		const double debounceDelay = 2.0;
		double currentTime = ImGui::GetTime();
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

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

			std::string itemId = ScopedKey(std::string(label) + "_" + std::to_string(i));

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

		PopTODHighlight(label, highlighted);
		return changed;
	}

	bool DrawTODFloatRow(const char* label, float values[4], bool& inheritFlag, const float parentValues[4], float minValue, float maxValue, const char* format)
	{
		const float scale = Util::GetUIScale();
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		DrawCenteredLabel(label);

		// Draw inherit checkbox
		if (parentValues) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, WidgetUI::kInheritCheckboxFrameBg);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, WidgetUI::kInheritCheckboxMark);
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

			Util::AddTooltip(T(TKEY("inherit_from_parent_weather"), "Inherit from parent weather"));
		}

		ImGui::TableSetColumnIndex(1);

		float totalWidth = ImGui::GetContentRegionAvail().x;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float columnWidth = (totalWidth - 3 * spacing) / 4.0f;

		if (inheritFlag)
			PushInheritedStyle();

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
			if (inheritFlag)
				Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
			ImGui::PopID();
		}
		ImGui::EndDisabled();

		if (inheritFlag)
			PopInheritedStyle();

		PopTODHighlight(label, highlighted);
		return changed;
	}

	bool DrawTODInt8Row(const char* label, int values[4])
	{
		float factors[4];
		GetTimeOfDayFactors(factors);
		bool changed = false;
		const bool highlighted = PushTODHighlight(label);

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

		PopTODHighlight(label, highlighted);
		return changed;
	}

	bool BeginTODTable(const char* tableId, float paramColumnWidth)
	{
		if (paramColumnWidth <= 0.0f)
			paramColumnWidth = WidgetDefaults::kTODLabelWidth;
		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T(TKEY("parameter"), "Parameter"), ImGuiTableColumnFlags_WidthFixed, paramColumnWidth * Util::GetUIScale());
			ImGui::TableSetupColumn(T(TKEY("value"), "Value"), ImGuiTableColumnFlags_WidthStretch);
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

// ============================================================================
// PropertyDrawer Implementation - Consolidates repeated table property drawing
// ============================================================================
namespace PropertyDrawer
{
	bool MatchesCurrentWidgetSearch(const char* label)
	{
		assert(label);
		return !g_currentWidget || g_currentWidget->MatchesSearch(label);
	}

	bool BeginTable(const char* tableId, float labelWidth)
	{
		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T(TKEY("parameter"), "Parameter"), ImGuiTableColumnFlags_WidthFixed, labelWidth * Util::GetUIScale());
			ImGui::TableSetupColumn(T(TKEY("value"), "Value"), ImGuiTableColumnFlags_WidthStretch);
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

	bool DrawFloat(const char* label, float& value, float minVal, float maxVal, const char* format)
	{
		if (!MatchesCurrentWidgetSearch(label))
			return false;
		DrawLabel(label);
		std::string id = std::string("##") + label;
		return DrawWithWidgetHighlight(g_currentWidget, label, [&]() {
			return ImGui::SliderFloat(id.c_str(), &value, minVal, maxVal, format);
		});
	}

	bool DrawInt(const char* label, int& value, int minVal, int maxVal)
	{
		if (!MatchesCurrentWidgetSearch(label))
			return false;
		DrawLabel(label);
		std::string id = std::string("##") + label;
		return DrawWithWidgetHighlight(g_currentWidget, label, [&]() {
			return ImGui::SliderInt(id.c_str(), &value, minVal, maxVal);
		});
	}

	bool DrawColor(const char* label, float3& value)
	{
		if (!MatchesCurrentWidgetSearch(label))
			return false;
		DrawLabel(label);
		return WeatherUtils::DrawColorEdit(std::string("##") + label, value);
	}

	bool DrawCheckbox(const char* label, bool& value)
	{
		if (!MatchesCurrentWidgetSearch(label))
			return false;
		DrawLabel(label);
		std::string id = std::string("##") + label;
		return DrawWithWidgetHighlight(g_currentWidget, label, [&]() {
			return ImGui::Checkbox(id.c_str(), &value);
		});
	}
}  // namespace PropertyDrawer

#undef I18N_KEY_PREFIX

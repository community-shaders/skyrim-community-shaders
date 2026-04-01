#include "SnowCover.h"

#include "Util.h"
#include "Utils/FileSystem.h"
#include "Utils/Serialize.h"
#include <DDSTextureLoader.h>
#include <imgui_stdlib.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::UserSettings,
	EnableExpensiveFoliage,
	AffectHavok,
	SnowHeightOffset)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::WorldConfig,
	AffectGrassTint, AffectTreeTint, FoliageHeightOffset, UVScale,
	MaxSummerMonth, MaxWinterMonth, SummerHeightOffset, WinterHeightOffset,
	MapTexture, MapZscale, BlendSmoothness,
	ScreenSpaceScale, LogMicrofacetDensity, MicrofacetRoughness, DensityRandomization,
	MapMin, MapMax, MainTexture, AltTexture,
	MainTint, AltTint, SnowingSpeed, MeltingSpeed,
	PeakMainAngle, PeakAltAngle, MinAngle, MaxAngle,
	MainSpec, AltSpec)

SnowCover::WorldConfig SnowCover::ToWorldConfig() const
{
	WorldConfig wc;
	wc.AffectGrassTint = wsettings.AffectGrassTint;
	wc.AffectTreeTint = wsettings.AffectTreeTint;
	wc.FoliageHeightOffset = wsettings.FoliageHeightOffset;
	wc.UVScale = wsettings.UVScale;
	wc.MaxSummerMonth = MaxSummerMonth;
	wc.MaxWinterMonth = MaxWinterMonth;
	wc.SummerHeightOffset = SummerHeightOffset;
	wc.WinterHeightOffset = WinterHeightOffset;
	wc.MapTexture = map_tex.generic_string();
	wc.MapZscale = wsettings.mapZscale;
	wc.BlendSmoothness = wsettings.BlendSmoothness;
	wc.ScreenSpaceScale = wsettings.ScreenSpaceScale;
	wc.LogMicrofacetDensity = wsettings.LogMicrofacetDensity;
	wc.MicrofacetRoughness = wsettings.MicrofacetRoughness;
	wc.DensityRandomization = wsettings.DensityRandomization;
	wc.MapMin = mapMin;
	wc.MapMax = mapMax;
	wc.MainTexture = main_tex.generic_string();
	wc.AltTexture = alt_tex.generic_string();
	wc.MainTint = wsettings.MainTint;
	wc.AltTint = wsettings.AltTint;
	wc.SnowingSpeed = snowing_speed;
	wc.MeltingSpeed = melting_speed;
	wc.PeakMainAngle = wsettings.PeakMainAngle;
	wc.PeakAltAngle = wsettings.PeakAltAngle;
	wc.MinAngle = wsettings.MinAngle;
	wc.MaxAngle = wsettings.MaxAngle;
	wc.MainSpec = wsettings.MainSpec;
	wc.AltSpec = wsettings.AltSpec;
	return wc;
}

void SnowCover::ApplyWorldConfig(const WorldConfig& wc)
{
	wsettings.AffectGrassTint = wc.AffectGrassTint;
	wsettings.AffectTreeTint = wc.AffectTreeTint;
	wsettings.FoliageHeightOffset = wc.FoliageHeightOffset;
	wsettings.UVScale = wc.UVScale;
	MaxSummerMonth = wc.MaxSummerMonth;
	MaxWinterMonth = wc.MaxWinterMonth;
	SummerHeightOffset = wc.SummerHeightOffset;
	WinterHeightOffset = wc.WinterHeightOffset;
	mapMin = wc.MapMin;
	mapMax = wc.MapMax;
	float2 delta = mapMax - mapMin;
	constexpr float epsilon = 1e-6f;
	if (std::abs(delta.x) < epsilon) {
		logger::error("[Snow Cover] MapMin.x and MapMax.x are nearly equal ({}, {}), using fallback delta 1.0", mapMin.x, mapMax.x);
		delta.x = 1.0f;
	}
	if (std::abs(delta.y) < epsilon) {
		logger::error("[Snow Cover] MapMin.y and MapMax.y are nearly equal ({}, {}), using fallback delta 1.0", mapMin.y, mapMax.y);
		delta.y = 1.0f;
	}
	wsettings.mapScale = float2(1.0f) / delta;
	wsettings.mapOffset = -mapMin * wsettings.mapScale;
	wsettings.mapZscale = wc.MapZscale;
	wsettings.BlendSmoothness = wc.BlendSmoothness;
	wsettings.ScreenSpaceScale = wc.ScreenSpaceScale;
	wsettings.LogMicrofacetDensity = wc.LogMicrofacetDensity;
	wsettings.MicrofacetRoughness = wc.MicrofacetRoughness;
	wsettings.DensityRandomization = wc.DensityRandomization;
	wsettings.MainTint = wc.MainTint;
	wsettings.AltTint = wc.AltTint;
	snowing_speed = wc.SnowingSpeed;
	melting_speed = wc.MeltingSpeed;
	wsettings.PeakMainAngle = wc.PeakMainAngle;
	wsettings.PeakAltAngle = wc.PeakAltAngle;
	wsettings.MinAngle = wc.MinAngle;
	wsettings.MaxAngle = wc.MaxAngle;
	wsettings.MainSpec = wc.MainSpec;
	wsettings.AltSpec = wc.AltSpec;
	main_tex = wc.MainTexture;
	tbuf = main_tex.generic_string();
	alt_tex = wc.AltTexture;
	altbuf = alt_tex.generic_string();
	map_tex = wc.MapTexture;
	mapbuf = map_tex.generic_string();
}

void SnowCover::DrawSettings()
{
	ImGui::Checkbox("Enable Nicer Foliage", (bool*)&settings.EnableExpensiveFoliage);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Uses one more texture sample to put snow on edges of tree lods and grass.");
	}
	ImGui::Checkbox("Snow on Mobile Objects", (bool*)&settings.AffectHavok);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Moving objects (Havok rigidbodies), such as clutter, will receive snow when they are not moving. Snow appears and disappears instantly.");
	}
	ImGui::SliderFloat("Snow Offset", &settings.SnowHeightOffset, HEIGHT_OFFSET_SLIDER_MIN, HEIGHT_OFFSET_SLIDER_MAX);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Moves the altitude that snow appears at. For testing purposes.");
	}
	ImGui::Separator();
	ImGui::Text("Each config applies to one worldspace or interior cell.");
	ImGui::Text("Saved config will be applied when you enter the worldspace.");

	if (ImGui::TreeNodeEx("Worldspace Config", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Current worldspace/cell: %s", last_worldspace.c_str());
		ImGui::Text("Config status: %s", status.c_str());

		ImGui::SameLine();
		if (ImGui::Button("Reload")) {
			last_worldspace = "";
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Reloads the config for current worldspace from file. "
				"Reload is required to load new texture paths.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			SaveConfig();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Saves the current config to a file.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Defaults")) {
			ApplyWorldConfig(WorldConfig{});
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Resets the current config to default values.");
		}
		ImGui::Separator();
		ImGui::Checkbox("Enable Snow Cover", (bool*)&wsettings.EnableSnowCover);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables the feature. This is set automatically when a config is found.");
		}
		if (wsettings.EnableSnowCover) {
			ImGui::Checkbox("Affect Grass Tint", (bool*)&wsettings.AffectGrassTint);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Should grass turn yellow?");
			}
			ImGui::Checkbox("Affect Tree Tint", (bool*)&wsettings.AffectTreeTint);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Should trees turn yellow?");
			}
			ImGui::SliderFloat("Foliage Color Height Offset", &wsettings.FoliageHeightOffset, -2000.0f, 2000.0f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("How far below/above the snow line should the foliage color start changing?");
			}
			ImGui::SliderFloat("Blend smoothness", &wsettings.BlendSmoothness, 1.f, 10000.0f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("How gradual the snow transition is.");
			}
			if (ImGui::TreeNodeEx("Weather and Seasons")) {
				ImGui::SliderInt("Maximum Summer Month", (int*)&MaxSummerMonth, 0, 11);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("In which month is the snow line highest?");
				}
				ImGui::SliderInt("Maximum Winter Month", (int*)&MaxWinterMonth, 0, 11);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("In which month is the snow line lowest?");
				}
				ImGui::SliderFloat("Summer Height Offset", &SummerHeightOffset, HEIGHT_OFFSET_SLIDER_MIN, HEIGHT_OFFSET_SLIDER_MAX);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("What is the snow line altitude in summer?");
				}
				ImGui::SliderFloat("Winter Height Offset", &WinterHeightOffset, HEIGHT_OFFSET_SLIDER_MIN, HEIGHT_OFFSET_SLIDER_MAX);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("What is the snow line altitude in winter?");
				}
				ImGui::SliderFloat("Snowing speed", &snowing_speed, 0.01f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("How fast snowy weather accumulates snow. Also depends on the weather settings.");
				}
				ImGui::SliderFloat("Melting speed", &melting_speed, 0.01f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("How fast snow (under snow line) disappears when it is not snowing.");
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Snow Map")) {
				ImGui::InputText("Map texture", &mapbuf);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Path to the map texture relative to Data folder. Interpreted as grayscale.");
				}
				ImGui::InputFloat("Min X", &mapMin.x, 0.0f, 10.0f);
				ImGui::InputFloat("Min Y", &mapMin.y, 0.0f, 10.0f);
				ImGui::InputFloat("Max X", &mapMax.x, 0.0f, 10.0f);
				ImGui::InputFloat("Max Y", &mapMax.y, 0.0f, 10.0f);

				map_tex = mapbuf;
				ImGui::SliderFloat("Snow Map Z Scale", &wsettings.mapZscale, 0.1f, 10000.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Vertical scale of the height map offset.");
				}
				ImGui::TreePop();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("A grayscale map of the worldspace that offsets the altitude snow appears at. Relative to game Data folder, without '.dds' ");
			}

			if (ImGui::TreeNodeEx("Material")) {
				ImGui::SliderFloat("Min Angle", &wsettings.MinAngle, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The angle snow starts appearing at. 0 = horizontal, 1 = vertical.");
				}
				ImGui::SliderFloat("Max Angle", &wsettings.MaxAngle, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The angle snow is fully visible at. 0 = horizontal, 1 = vertical.");
				}
				ImGui::InputText("Main texture", &tbuf);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Path to the main texture relative to Data folder, without '.dds'. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
				}
				main_tex = tbuf;
				ImGui::InputText("Alt texture", &altbuf);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Optional path to the alternative texture relative to Data folder, without '.dds'. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
				}
				alt_tex = altbuf;
				ImGui::ColorEdit4("Main Tint", &wsettings.MainTint.x);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Tint for the main texture. Alpha only affects the color but not roughness etc.");
				}
				ImGui::InputFloat("Main Specular", &wsettings.MainSpec, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The main specular multiplier. Most materials = 0.04, snow = 0.02");
				}
				ImGui::ColorEdit4("Alt Tint", &wsettings.AltTint.x);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Tint for the alternative texture. Alpha only affects the color but not roughness etc.");
				}
				ImGui::InputFloat("Alt Specular", &wsettings.AltSpec, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The alternative specular multiplier. Most materials = 0.04, snow = 0.02");
				}
				ImGui::SliderFloat("Peak Main Angle", &wsettings.PeakMainAngle, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The angle at which the main texture is the strongest. The stronger wins. 0 = horizontal, 1 = vertical.");
				}
				ImGui::SliderFloat("Peak Alt Angle", &wsettings.PeakAltAngle, 0.0f, 1.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The angle at which the alternative texture is the strongest. The stronger wins. 0 = horizontal, 1 = vertical.");
				}
				ImGui::SliderFloat("UV Scale", &wsettings.UVScale, 0.1f, 10.f, "%.1f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("The UV scale of both textures.");
				}
				ImGui::Text("Glint");
				ImGui::SliderFloat("Screenspace Scale", &wsettings.ScreenSpaceScale, 0.f, 3.f, "%.3f");
				ImGui::SliderFloat("Log Microfacet Density", &wsettings.LogMicrofacetDensity, 0.f, 40.f, "%.3f");
				ImGui::SliderFloat("Microfacet Roughness", &wsettings.MicrofacetRoughness, 0.f, 1.f, "%.3f");
				ImGui::SliderFloat("Density Randomization", &wsettings.DensityRandomization, 0.f, 5.f, "%.3f");
				ImGui::TreePop();
			}
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Debug Info")) {
		ImGui::Text("Month: %f", perFrame.Month);
		ImGui::Text("TimeSnowing: %f", perFrame.TimeSnowing);
		ImGui::Text("SnowingDensity: %f", perFrame.SnowingDensity);

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

float SnowCover::GetSeasonalAltitude()
{
	if (MaxSummerMonth == MaxWinterMonth)
		return -(SummerHeightOffset + WinterHeightOffset) * 0.5f;

	float maxMonth = static_cast<float>(std::max(MaxSummerMonth, MaxWinterMonth));
	float minMonth = static_cast<float>(std::min(MaxSummerMonth, MaxWinterMonth));
	float summerToWinter;
	auto month = (maxMonth + minMonth) / 2.0f;  // fallback value if calendar doesn't exist
	if (auto calendar = RE::Calendar::GetSingleton()) {
		auto time = calendar->GetTime();
		month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 60.0) / 60.0) / 24.0) / 32.0);
	}
	if (month > maxMonth) {
		summerToWinter = (month - maxMonth) / (minMonth + 12.0f - maxMonth);
		if (MaxWinterMonth > MaxSummerMonth)
			summerToWinter = 1.0f - summerToWinter;
	} else if (month < minMonth) {
		summerToWinter = (12.0f - maxMonth + month) / (minMonth + 12.0f - maxMonth);
		if (MaxSummerMonth > MaxWinterMonth)
			summerToWinter = 1.0f - summerToWinter;
	} else {
		summerToWinter = (month - minMonth) / (maxMonth - minMonth);
		if (MaxSummerMonth > MaxWinterMonth)
			summerToWinter = 1.0f - summerToWinter;
	}

	return -std::lerp(SummerHeightOffset, WinterHeightOffset, summerToWinter);
}

SnowCover::PerFrame SnowCover::GetCommonBufferData()
{
	Reload();

	bool snowing = false;
	bool raining = false;
	snowingDensity = 0;
	if (wsettings.EnableSnowCover) {
		if (auto sky = RE::Sky::GetSingleton()) {
			if (auto currentWeather = sky->currentWeather) {
				if (currentWeather->precipitationData) {
					float particleDensity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
					float particleGravity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
					snowingDensity = particleDensity * particleGravity;
				}
				if (currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					snowing = true;
				} else if (currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					raining = true;
				}
			}
		}
	}
	if (auto calendar = RE::Calendar::GetSingleton()) {
		auto h = calendar->GetHour();
		auto diff = h < lastHour ? h + 24 - lastHour : h - lastHour;
		if (snowing)
			timeSnowing += diff * snowing_speed;
		else if (raining) {
			timeSnowing -= 2 * diff * melting_speed;
		} else {
			if (timeSnowing > 0) {
				timeSnowing = std::max(timeSnowing - diff * melting_speed, 0.0f);
			} else if (timeSnowing < 0) {
				timeSnowing = std::min(timeSnowing + diff * melting_speed, 0.0f);
			}
		}

		timeSnowing = std::clamp(timeSnowing, -2.0f, 2.0f);
		lastHour = h;

		auto time = calendar->GetTime();
		perFrame.Month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 60.0) / 60.0) / 24.0) / 32.0);
	}
	perFrame.SnowingDensity = snowingDensity;
	perFrame.TimeSnowing = timeSnowing;
	perFrame.SeasonalAltitude = GetSeasonalAltitude();
	perFrame.settings = settings;
	perFrame.wsettings = wsettings;
	perFrame.wsettings.BlendSmoothness = std::max(1.0f, perFrame.wsettings.BlendSmoothness);

	return perFrame;
}

void SnowCover::SetupResources()
{
	Reload();
	if (auto calendar = RE::Calendar::GetSingleton())
		lastHour = calendar->GetHour();
}

namespace
{
	const char* GetWorldspace()
	{
		auto curr_worldspace = "none";
		auto tes = RE::TES::GetSingleton();
		if (tes) {
			auto worldspace = tes->GetRuntimeData2().worldSpace;
			if (tes->interiorCell) {
				curr_worldspace = tes->interiorCell->GetFormEditorID();
			} else if (worldspace) {
				curr_worldspace = worldspace->GetFormEditorID();
			}
		}
		return curr_worldspace;
	}
}

void SnowCover::SaveConfig()
{
	if (last_worldspace.empty())
		return;
	json config = ToWorldConfig();

	try {
		auto curr_worldspace = GetWorldspace();
		auto path = (Util::PathHelpers::GetShadersPath() / "SnowCover" / curr_worldspace).replace_extension(std::filesystem::path(".json"));
		std::ofstream file(path);
		file << config.dump(4);
	} catch (const std::system_error& e) {
		logger::error("[Snow Cover] Error saving file: {}", e.what());
	}
	last_worldspace.clear();
	Reload();
}

void SnowCover::Reload()
{
	std::ifstream fileStream;
	std::filesystem::path path;
	try {
		auto curr_worldspace = GetWorldspace();
		if (curr_worldspace == last_worldspace)
			return;
		last_worldspace = curr_worldspace;
		path = (Util::PathHelpers::GetShadersPath() / "SnowCover" / curr_worldspace).replace_extension(std::filesystem::path(".json"));
		if (!std::filesystem::exists(path)) {
			status = std::format("Config doesn't exist {}", path.generic_string());
			wsettings.EnableSnowCover = false;
			return;
		}
		fileStream = std::ifstream(path);
		if (!fileStream.is_open()) {
			status = std::string("Cannot open config.");
			wsettings.EnableSnowCover = false;
			logger::error("[Snow Cover] Cannot open config at {}", path.generic_string());
			return;
		}
	} catch (const std::system_error& e) {
		logger::error("[Snow Cover] Error opening file: {}", e.what());
		return;
	}

	auto whitelist_path = Util::PathHelpers::GetShadersPath() / "SnowCover" / "whitelist.txt";
	auto blacklist_path = Util::PathHelpers::GetShadersPath() / "SnowCover" / "blacklist.txt";

	whitelist = FormIdParser::parseTriNameFile(whitelist_path);
	blacklist = FormIdParser::parseTriNameFile(blacklist_path);

	json config;
	try {
		fileStream >> config;
		WorldConfig wc = config.get<WorldConfig>();
		ApplyWorldConfig(wc);
		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (views[0])
			views[0]->Release();
		if (views[1])
			views[1]->Release();
		if (views[2])
			views[2]->Release();
		auto data_path = Util::PathHelpers::GetDataPath();
		HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / main_tex).replace_extension(".dds").native().c_str(), nullptr, &views.at(0));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}.dds texture: {}", main_tex.generic_string(), hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetShadersPath() / "SnowCover" / "default" / "main.dds").native().c_str(), nullptr, &views.at(0));
			if (FAILED(hr)) {
				logger::error("Snow Cover: Fallback main texture also failed, disabling snow cover");
				wsettings.EnableSnowCover = false;
				return;
			}
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / main_tex).concat("_n.dds").native().c_str(), nullptr, &views.at(1));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", main_tex.generic_string(), hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetShadersPath() / "SnowCover" / "default" / "main_n.dds").native().c_str(), nullptr, &views.at(1));
			if (FAILED(hr)) {
				logger::error("Snow Cover: Fallback normal texture also failed, disabling snow cover");
				wsettings.EnableSnowCover = false;
				return;
			}
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / main_tex).concat("_rmaos.dds").native().c_str(), nullptr, &views.at(2));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", main_tex.generic_string(), hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetShadersPath() / "SnowCover" / "default" / "main_rmaos.dds").native().c_str(), nullptr, &views.at(2));
			if (FAILED(hr)) {
				logger::error("Snow Cover: Fallback rmaos texture also failed, disabling snow cover");
				wsettings.EnableSnowCover = false;
				return;
			}
		}
		if (!wc.AltTexture.empty()) {
			if (views[3])
				views[3]->Release();
			if (views[4])
				views[4]->Release();
			if (views[5])
				views[5]->Release();
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).replace_extension(".dds").native().c_str(), nullptr, &views.at(3));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}.dds texture: {}", alt_tex.generic_string(), hr);
				views[0]->AddRef();
				views[3] = views[0];
			}
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).concat("_n.dds").native().c_str(), nullptr, &views.at(4));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", alt_tex.generic_string(), hr);
				views[1]->AddRef();
				views[4] = views[1];
			}
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).concat("_rmaos.dds").native().c_str(), nullptr, &views.at(5));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", alt_tex.generic_string(), hr);
				views[2]->AddRef();
				views[5] = views[2];
			}

		} else {
			if (views[3])
				views[3]->Release();
			if (views[4])
				views[4]->Release();
			if (views[5])
				views[5]->Release();
			views[0]->AddRef();
			views[3] = views[0];
			views[1]->AddRef();
			views[4] = views[1];
			views[2]->AddRef();
			views[5] = views[2];
		}

		if (views[6])
			views[6]->Release();
		hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / map_tex).concat(".dds").native().c_str(), nullptr, &views.at(6));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}.dds texture: {}", map_tex.generic_string(), hr);
			DirectX::CreateDDSTextureFromFile(device, context, (data_path / "textures" / "gray.dds").native().c_str(), nullptr, &views.at(6));
		}
		wsettings.EnableSnowCover = true;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path.generic_string(), e.what());
		status = e.what();
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (const nlohmann::json::exception& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path.generic_string(), e.what());
		status = e.what();
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (...) {
		logger::error("[Snow Cover] unknown error when loading a config: {}", path.generic_string());
		status = std::string("Unknown error.");
		return;
	}
	status = std::string("Loaded.");
}

void SnowCover::Prepass()
{
	if (globals::features::snowCover.wsettings.EnableSnowCover) {
		auto context = globals::d3d::context;
		context->PSSetShaderResources(38, (uint)views.size(), views.data());
	}
}

void SnowCover::Reset()
{
}

void SnowCover::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SnowCover::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SnowCover::RestoreDefaultSettings()
{
	settings = {};
}

void SnowCover::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	if (globals::features::snowCover.wsettings.EnableSnowCover) {
		globals::features::snowCover.BSLightingShader_Setup(Pass);
	}
	func(This, Pass, RenderFlags);
}

void SnowCover::BSLightingShader_Setup(RE::BSRenderPass* a_pass)
{
	auto state = globals::state;
	auto userData = a_pass->geometry->GetUserData();
	auto name = a_pass->geometry->name.c_str();
	if (blacklist.contains(FormIdParser::fnv_hash(name))) {
		state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else if ((a_pass->geometry->HasAnimation() || (userData && ((userData->GetObjectReference() && userData->GetObjectReference()->IsBoundAnimObject()) || userData->CanBeMoved()))) && !whitelist.contains(FormIdParser::fnv_hash(name))) {
		if (settings.AffectHavok && userData && userData->CanBeMoved()) {
			RE::NiPoint3 vel;
			userData->GetLinearVelocity(vel);
			if (vel.SqrLength() < 10000.0f)
				state->permutationData.ExtraShaderDescriptor &= ~(uint)State::ExtraShaderDescriptors::NoSnow;
			else
				state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
		} else
			state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else {
		state->permutationData.ExtraShaderDescriptor &= ~(uint)State::ExtraShaderDescriptors::NoSnow;
	}
}

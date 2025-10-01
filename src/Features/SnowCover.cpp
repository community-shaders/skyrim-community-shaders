#include "SnowCover.h"

#include "Util.h"
#include "Utils/FileSystem.h"
#include <DDSTextureLoader.h>
#include <cstdlib>
#include <cstring>
#include <string.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::UserSettings,
	EnableExpensiveFoliage,
	SnowHeightOffset)

void copyString(const std::string& input, char* dst, size_t dst_size)
{
	strncpy(dst, input.c_str(), dst_size - 1);
	dst[dst_size - 1] = '\0';
}

void SnowCover::DrawSettings()
{
	ImGui::Checkbox("Enable Nicer Foliage", (bool*)&settings.EnableExpensiveFoliage);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Uses one more texture sample to put snow on edges of tree lods and grass.");
	}
	ImGui::SliderFloat("Snow Offset", &settings.SnowHeightOffset, -20000.0f, 20000.0f);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Moves the altitude that snow appears at. For testing purposes.");
	}
	ImGui::Separator();
	ImGui::Text("Each config applies to one worldspace or interior cell.");
	ImGui::Text("Saved config will be applied when you enter the worldspace.");

	if (ImGui::TreeNodeEx("Worldspace Config", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(fmt::format("Current worldspace/cell: {}", last_worldspace).c_str());
		ImGui::Text(fmt::format("Config status: {}", status).c_str());

		ImGui::SameLine();
		if (ImGui::Button("Reload")) {
			last_worldspace = "";
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Reloads the config for current worldspace from file."
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
			wsettings = WorldSettings();
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
				ImGui::SliderFloat("Summer Height Offset", &SummerHeightOffset, -20000.0f, 20000.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("What is the snow line altitude in summer?");
				}
				ImGui::SliderFloat("Winter Height Offset", &WinterHeightOffset, -20000.0f, 20000.0f);
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
				ImGui::InputText("Map texture", mapbuf, 128);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Path to the map texture relative to Data folder. Interpreted as grayscale.");
				}
				ImGui::InputFloat("Min X", &mapMin.x, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Min Y", &mapMin.y, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Max X", &mapMax.x, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Max Y", &mapMax.y, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}

				map_tex = std::filesystem::path(mapbuf);
				ImGui::SliderFloat("Snow Map Z Scale", &wsettings.mapZscale, 0.1f, 10000.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
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
				ImGui::InputText("Main texture", tbuf, 128);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Path to the main texture relative to Data folder, without '.dds'. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
				}
				main_tex = std::string(tbuf);
				ImGui::InputText("Alt texture", altbuf, 128);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Optional path to the alternative texture relative to Data folder, without '.dds'. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
				}
				alt_tex = std::string(altbuf);
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
		ImGui::Text(fmt::format("Month: {}", perFrame.Month).c_str());
		ImGui::Text(fmt::format("TimeSnowing: {}", perFrame.TimeSnowing).c_str());
		ImGui::Text(fmt::format("SnowingDensity: {}", perFrame.SnowingDensity).c_str());
		if (debug_text != nullptr)
			ImGui::Text(fmt::format("Debug text: {}", debug_text).c_str());

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

//void SnowCover::Draw(const RE::BSShader*, const uint32_t){}

SnowCover::PerFrame SnowCover::GetCommonBufferData()
{
	Reload();

	static float delta = 0;                       // size_t for precision
	if (!RE::UI::GetSingleton()->GameIsPaused())  // from lightlimitfix
		delta = RE::GetSecondsSinceLastFrame();   // BSTimer::delta is always 0 for some reason
	bool snowing = false;
	bool raining = false;
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
	} else {
		snowingDensity = 0;
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

	return perFrame;
}

void SnowCover::SetupResources()
{
	Reload();
	if (auto calendar = RE::Calendar::GetSingleton())
		lastHour = calendar->GetHour();
}

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

void SnowCover::SaveConfig()
{
	if (last_worldspace == nullptr)
		return;
	json config = {
		{ "AffectGrassTint", wsettings.AffectGrassTint },
		{ "AffectTreeTint", wsettings.AffectTreeTint },
		{ "FoliageHeightOffset", wsettings.FoliageHeightOffset },
		{ "UVScale", wsettings.UVScale },
		{ "MaxSummerMonth", MaxSummerMonth },
		{ "MaxWinterMonth", MaxWinterMonth },
		{ "SummerHeightOffset", SummerHeightOffset },
		{ "WinterHeightOffset", WinterHeightOffset },
		{ "MapTexture", map_tex.c_str() },
		{ "MapZscale", wsettings.mapZscale },
		{ "BlendSmoothness", wsettings.BlendSmoothness },
		{ "ScreenSpaceScale", wsettings.ScreenSpaceScale },
		{ "LogMicrofacetDensity", wsettings.LogMicrofacetDensity },
		{ "MicrofacetRoughness", wsettings.MicrofacetRoughness },
		{ "DensityRandomization", wsettings.DensityRandomization },
		{ "MapMin", json::array({ mapMin.x, mapMin.y }) },
		{ "MapMax", json::array({ mapMax.x, mapMax.y }) },
		{ "MainTexture", main_tex.c_str() },
		{ "AltTexture", alt_tex.c_str() },
		{ "MainTint", json::array({ wsettings.MainTint.x,
						  wsettings.MainTint.y,
						  wsettings.MainTint.z,
						  wsettings.MainTint.w }) },
		{ "AltTint", json::array({ wsettings.AltTint.x,
						 wsettings.AltTint.y,
						 wsettings.AltTint.z,
						 wsettings.AltTint.w }) },
		{ "SnowingSpeed", snowing_speed },
		{ "MeltingSpeed", melting_speed },
		{ "PeakMainAngle", wsettings.PeakMainAngle },
		{ "PeakAltAngle", wsettings.PeakAltAngle },
		{ "MinAngle", wsettings.MinAngle },
		{ "MaxAngle", wsettings.MaxAngle },
		{ "MainSpec", wsettings.MainSpec },
		{ "AltSpec", wsettings.AltSpec },
	};

	if (!alt_tex.empty())
		config["AltTexture"] = alt_tex;

	try {
		auto curr_worldspace = GetWorldspace();
		auto path = (Util::PathHelpers::GetFeatureShaderPath("SnowCover") / curr_worldspace).replace_extension(std::filesystem::path(".json"));
		std::ofstream file(path);
		file << config.dump(4);
	} catch (const std::system_error& e) {
		logger::error("[Snow Cover] Error saving file: {}", e.what());
	}
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
		path = (Util::PathHelpers::GetFeatureShaderPath("SnowCover") / curr_worldspace).replace_extension(std::filesystem::path(".json"));
		if (!std::filesystem::exists(path)) {
			status = std::string("Config doesn't exist.");
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
	}

	auto whitelist_path = Util::PathHelpers::GetFeatureShaderPath("SnowCover") / "whitelist.txt";
	auto blacklist_path = Util::PathHelpers::GetFeatureShaderPath("SnowCover") / "blacklist.txt";

	whitelist = FormIdParser::parseTriNameFile(whitelist_path);
	blacklist = FormIdParser::parseTriNameFile(blacklist_path);

	json config;
	try {
		fileStream >> config;
		wsettings.AffectGrassTint = config["AffectGrassTint"];
		wsettings.AffectTreeTint = config["AffectTreeTint"];
		wsettings.FoliageHeightOffset = config["FoliageHeightOffset"];
		wsettings.UVScale = config["UVScale"];
		MaxSummerMonth = config["MaxSummerMonth"];
		MaxWinterMonth = config["MaxWinterMonth"];
		SummerHeightOffset = config["SummerHeightOffset"];
		WinterHeightOffset = config["WinterHeightOffset"];
		mapMin = float2(config["MapMin"][0], config["MapMin"][1]);
		mapMax = float2(config["MapMax"][0], config["MapMax"][1]);
		wsettings.mapScale = float2(1.0) / (mapMax - mapMin);
		wsettings.mapOffset = -mapMin * wsettings.mapScale;
		wsettings.mapZscale = config["MapZscale"];
		wsettings.BlendSmoothness = config["BlendSmoothness"];
		wsettings.ScreenSpaceScale = config["ScreenSpaceScale"];
		wsettings.LogMicrofacetDensity = config["LogMicrofacetDensity"];
		wsettings.MicrofacetRoughness = config["MicrofacetRoughness"];
		wsettings.DensityRandomization = config["DensityRandomization"];
		wsettings.MainTint = float4(config["MainTint"][0], config["MainTint"][1], config["MainTint"][2], config["MainTint"][3]);
		wsettings.AltTint = float4(config["AltTint"][0], config["AltTint"][1], config["AltTint"][2], config["AltTint"][3]);
		snowing_speed = config["SnowingSpeed"];
		melting_speed = config["MeltingSpeed"];
		wsettings.PeakMainAngle = config["PeakMainAngle"];
		wsettings.PeakAltAngle = config["PeakAltAngle"];
		wsettings.MinAngle = config["MinAngle"];
		wsettings.MaxAngle = config["MaxAngle"];
		wsettings.MainSpec = config["MainSpec"];
		wsettings.AltSpec = config["AltSpec"];
		main_tex = std::filesystem::path(config["MainTexture"].get<std::string>());
		//std::wstring tname = strtowstr(main_tex);
		copyString(main_tex.generic_string().c_str(), tbuf, 256);
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
			DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetFeatureShaderPath("SnowCover") / "default" / "main.dds").native().c_str(), nullptr, &views.at(0));
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / main_tex).concat("_n.dds").native().c_str(), nullptr, &views.at(1));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", main_tex.generic_string(), hr);
			DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetFeatureShaderPath("SnowCover") / "default" / "main_n.dds").native().c_str(), nullptr, &views.at(1));
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / main_tex).concat("_rmaos.dds").native().c_str(), nullptr, &views.at(2));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", main_tex.generic_string(), hr);
			DirectX::CreateDDSTextureFromFile(device, context, (Util::PathHelpers::GetFeatureShaderPath("SnowCover") / "default" / "main_rmaos.dds").native().c_str(), nullptr, &views.at(2));
		}
		if (config.contains("AltTexture") && config["AltTexture"] != "") {
			if (views[3])
				views[3]->Release();
			if (views[4])
				views[4]->Release();
			if (views[5])
				views[5]->Release();
			alt_tex = std::filesystem::path(config["AltTexture"].get<std::string>());
			copyString(alt_tex.generic_string().c_str(), altbuf, 256);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).replace_extension(".dds").native().c_str(), nullptr, &views.at(3));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}.dds texture: {}", alt_tex.generic_string(), hr);
				views[3] = views[0];
			}
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).concat("_n.dds").native().c_str(), nullptr, &views.at(4));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", alt_tex.generic_string(), hr);
				views[4] = views[1];
			}
			hr = DirectX::CreateDDSTextureFromFile(device, context, (data_path / alt_tex).concat("_rmaos.dds").native().c_str(), nullptr, &views.at(5));
			if (hr != S_OK) {
				logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", alt_tex.generic_string(), hr);
				views[5] = views[2];
			}

		} else {
			views[3] = views[0];
			views[4] = views[1];
			views[5] = views[2];
		}

		if (views[6])
			views[6]->Release();
		map_tex = std::filesystem::path(config["MapTexture"].get<std::string>());
		//std::wstring mname = strtowstr(map_tex);
		copyString(map_tex.generic_string().c_str(), mapbuf, 256);
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

void SnowCover::Load(json& o_json)
{
	settings = o_json;
}

void SnowCover::Save(json& o_json)
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
	if ((a_pass->geometry->HasAnimation() || (userData && (userData->GetObjectReference()->IsBoundAnimObject() || userData->CanBeMoved()))) && !whitelist.contains(FormIdParser::fnv_hash(name))) {
		state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else if (blacklist.contains(FormIdParser::fnv_hash(name))) {
		state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else {
		state->permutationData.ExtraShaderDescriptor &= ~(uint)State::ExtraShaderDescriptors::NoSnow;
	}
}

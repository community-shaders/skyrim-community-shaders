#include "SnowCover.h"

#include "Util.h"
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
			ImGui::Checkbox("Foliage Color Affected", (bool*)&wsettings.AffectFoliageColor);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Should grass and trees turn yellow?");
			}
			ImGui::SliderFloat("Foliage Color Height Offset", &wsettings.FoliageHeightOffset, -2000.0f, 2000.0f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("How far below/above the snow line should the foliage color start changing?");
			}
			ImGui::SliderFloat("Blend smoothness", &wsettings.blendSmoothness, 1.f, 10000.0f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("How gradual the snow transition is.");
			}
			if (ImGui::TreeNodeEx("Weather and Seasons")) {
				ImGui::SliderInt("Maximum Summer Month", (int*)&wsettings.MaxSummerMonth, 0, 11);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("In which month is the snow line highest?");
				}
				ImGui::SliderInt("Maximum Winter Month", (int*)&wsettings.MaxWinterMonth, 0, 11);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("In which month is the snow line lowest?");
				}
				ImGui::SliderFloat("Summer Height Offset", &wsettings.SummerHeightOffset, -20000.0f, 20000.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("What is the snow line altitude in summer?");
				}
				ImGui::SliderFloat("Winter Height Offset", &wsettings.WinterHeightOffset, -20000.0f, 20000.0f);
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
				ImGui::InputFloat("Min X", &wsettings.mapMin.x, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Min Y", &wsettings.mapMin.y, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Max X", &wsettings.mapMax.x, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}
				ImGui::InputFloat("Max Y", &wsettings.mapMax.y, 0.0f, 10.0f);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("");
				}

				map_tex = std::string(mapbuf);
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
		perFrame.Month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 61.0) / 60.0) / 24.0) / 32.0);
	}
	perFrame.SnowingDensity = snowingDensity;
	perFrame.TimeSnowing = timeSnowing;
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

std::string wstrtostr(std::wstring& wide)
{
	std::string str(wide.length(), 0);
	std::transform(wide.begin(), wide.end(), str.begin(), [](wchar_t c) {
		return (char)c;
	});
	return str;
}

std::wstring strtowstr(std::string& wide)
{
	std::wstring str(wide.length(), 0);
	std::transform(wide.begin(), wide.end(), str.begin(), [](char c) {
		return (wchar_t)c;
	});
	return str;
}

std::string GetWorldspace()
{
	std::string curr_worldspace = "none";
	auto tes = globals::game::tes;
	if (tes) {
		auto worldspace = tes->GetRuntimeData2().worldSpace;
		if (tes->interiorCell) {
			curr_worldspace = std::string(tes->interiorCell->GetFullName());
		} else if (worldspace) {
			curr_worldspace = worldspace->GetFormEditorID();
		}
	}
	return curr_worldspace;
}

void SnowCover::SaveConfig()
{
	if (last_worldspace.length() == 0)
		return;
	json config = {
		{ "AffectFoliageColor", wsettings.AffectFoliageColor },
		{ "FoliageHeightOffset", wsettings.FoliageHeightOffset },
		{ "UVScale", wsettings.UVScale },
		{ "MaxSummerMonth", wsettings.MaxSummerMonth },
		{ "MaxWinterMonth", wsettings.MaxWinterMonth },
		{ "SummerHeightOffset", wsettings.SummerHeightOffset },
		{ "WinterHeightOffset", wsettings.WinterHeightOffset },
		{ "MapTexture", map_tex },
		{ "MapZscale", wsettings.mapZscale },
		{ "BlendSmoothness", wsettings.blendSmoothness },
		{ "ScreenSpaceScale", wsettings.ScreenSpaceScale },
		{ "LogMicrofacetDensity", wsettings.LogMicrofacetDensity },
		{ "MicrofacetRoughness", wsettings.MicrofacetRoughness },
		{ "DensityRandomization", wsettings.DensityRandomization },
		{ "MapMin", json::array({ wsettings.mapMin.x, wsettings.mapMin.y }) },
		{ "MapMax", json::array({ wsettings.mapMax.x, wsettings.mapMax.y }) },
		{ "MainTexture", main_tex },
		{ "AltTexture", alt_tex },
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

	if (alt_tex.length() != 0)
		config["AltTexture"] = alt_tex;

	std::string curr_worldspace = GetWorldspace();
	auto path = std::string("Data\\Shaders\\SnowCover\\") + curr_worldspace + ".json";
	std::ofstream file(path);
	file << config.dump(4);
	Reload();
}

void SnowCover::Reload()
{
	std::string curr_worldspace = GetWorldspace();
	if (curr_worldspace == last_worldspace)
		return;
	last_worldspace = curr_worldspace;
	auto path = std::string("Data\\Shaders\\SnowCover\\") + curr_worldspace + ".json";
	if (!std::filesystem::exists(path)) {
		status = std::string("Config doesn't exist.");
		wsettings.EnableSnowCover = false;
		return;
	}
	std::ifstream fileStream(path);
	if (!fileStream.is_open()) {
		status = std::string("Cannot open config.");
		wsettings.EnableSnowCover = false;
		logger::error("[Snow Cover] Cannot open config at  {}", path);
		return;
	}

	auto whitelist_path = std::string("Data\\Shaders\\SnowCover\\whitelist.txt");
	auto blacklist_path = std::string("Data\\Shaders\\SnowCover\\blacklist.txt");

	whitelist = FormIdParser::parseTriNameFile(whitelist_path);
	blacklist = FormIdParser::parseTriNameFile(blacklist_path);

	json config;
	try {
		fileStream >> config;
		wsettings.AffectFoliageColor = config["AffectFoliageColor"];
		wsettings.FoliageHeightOffset = config["FoliageHeightOffset"];
		wsettings.UVScale = config["UVScale"];
		wsettings.MaxSummerMonth = config["MaxSummerMonth"];
		wsettings.MaxWinterMonth = config["MaxWinterMonth"];
		wsettings.SummerHeightOffset = config["SummerHeightOffset"];
		wsettings.WinterHeightOffset = config["WinterHeightOffset"];
		wsettings.mapMin = float2(config["MapMin"][0], config["MapMin"][1]);
		wsettings.mapMax = float2(config["MapMax"][0], config["MapMax"][1]);
		wsettings.mapZscale = config["MapZscale"];
		wsettings.blendSmoothness = config["BlendSmoothness"];
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

		main_tex = config["MainTexture"];
		std::wstring tname = strtowstr(main_tex);
		copyString(main_tex, tbuf, 256);
		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (views[0])
			views[0]->Release();
		if (views[1])
			views[1]->Release();
		if (views[2])
			views[2]->Release();
		HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L".dds").c_str(), nullptr, &views.at(0));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}.dds texture: {}", main_tex, hr);
			DirectX::CreateDDSTextureFromFile(device, context, L"Data\\textures\\defaultdiffuse.dds", nullptr, &views.at(0));
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L"_n.dds").c_str(), nullptr, &views.at(1));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", main_tex, hr);
			DirectX::CreateDDSTextureFromFile(device, context, L"Data\\textures\\default_n.dds", nullptr, &views.at(1));
		}
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L"_rmaos.dds").c_str(), nullptr, &views.at(2));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", main_tex, hr);
			DirectX::CreateDDSTextureFromFile(device, context, L"Data\\textures\\white.dds", nullptr, &views.at(2));
		}
		if (config.contains("AltTexture") && config["AltTexture"] != "") {
			if (views[3])
				views[3]->Release();
			if (views[4])
				views[4]->Release();
			if (views[5])
				views[5]->Release();
			alt_tex = config["AltTexture"];
			std::wstring altname = strtowstr(alt_tex);
			copyString(alt_tex, altbuf, 256);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + altname + L".dds").c_str(), nullptr, &views.at(3));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}.dds texture: {}", alt_tex, hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + altname + L"_n.dds").c_str(), nullptr, &views.at(4));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", alt_tex, hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + altname + L"_rmaos.dds").c_str(), nullptr, &views.at(5));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", alt_tex, hr);

		} else {
			views[3] = views[0];
			views[4] = views[1];
			views[5] = views[2];
		}

		if (views[6])
			views[6]->Release();
		map_tex = config["MapTexture"];
		std::wstring mname = strtowstr(map_tex);
		copyString(map_tex, mapbuf, 256);
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + mname + L".dds").c_str(), nullptr, &views.at(6));
		if (hr != S_OK) {
			logger::warn("Snow Cover: Error loading {}.dds texture: {}", map_tex, hr);
			DirectX::CreateDDSTextureFromFile(device, context, L"Data\\textures\\gray.dds", nullptr, &views.at(6));
		}
		wsettings.EnableSnowCover = true;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path, e.what());
		status = e.what();
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (const nlohmann::json::exception& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path, e.what());
		status = e.what();
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (...) {
		logger::error("[Snow Cover] unknown error when loading a config: {}", path);
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
	if ((a_pass->geometry->HasAnimation() || (userData && userData->GetObjectReference()->IsBoundAnimObject())) && !whitelist.contains(FormIdParser::fnv_hash(name))) {
		state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else if (blacklist.contains(FormIdParser::fnv_hash(name))) {
		state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::NoSnow;
	} else {
		state->permutationData.ExtraShaderDescriptor &= ~(uint)State::ExtraShaderDescriptors::NoSnow;
	}
}

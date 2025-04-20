#include "SnowCover.h"

#include "Util.h"
#include <DDSTextureLoader.h>
#include <cstdlib>
#include <cstring>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::UserSettings,
	SnowHeightOffset)

void copyString(const std::string& input, char* dst, size_t dst_size)
{
	strncpy(dst, input.c_str(), dst_size - 1);
	dst[dst_size - 1] = '\0';
}

void SnowCover::DrawSettings()
{
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
			ImGui::Text("Enables the feature. This is enabled automatically when a config is found.");
		}
		ImGui::Checkbox("Foliage Color Affected", (bool*)&wsettings.AffectFoliageColor);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Should grass and trees turn yellow?");
		}
		ImGui::SliderFloat("Foliage Color Height Offset", &wsettings.FoliageHeightOffset, -2000.0f, 2000.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("How far below/above the snow line should the foliage color start changing?");
		}
		ImGui::Separator();
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
		if (ImGui::TreeNodeEx("Height Equation")) {
			ImGui::InputFloat("x", &wsettings.Equation[0], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("y", &wsettings.Equation[1], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("xx", &wsettings.Equation[2], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("xy", &wsettings.Equation[3], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("yy", &wsettings.Equation[4], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("xxx", &wsettings.Equation[5], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("xxy", &wsettings.Equation[6], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("xyy", &wsettings.Equation[7], 0.0f, 0.0f, "%.20f");
			ImGui::InputFloat("yyy", &wsettings.Equation[8], 0.0f, 0.0f, "%.20f");
			ImGui::TreePop();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"The 'snow line' is a curved plane controlled by a cubic equation. "
				"These parameters control how the plane is curved.");
		}
		if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
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
				ImGui::Text("Path to the main texture relative to Data folder. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
			}
			main_tex = std::string(tbuf);
			ImGui::InputText("Alt texture", altbuf, 128);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Path to the alternative texture relative to Data folder. Needs to be a PBR texture with a diffuse, _n, and _rmaos.");
			}
			alt_tex = std::string(altbuf);
			ImGui::ColorEdit4("Main Tint", &wsettings.MainTint.x);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Tint for the main texture. Alpha only affects the color but not roughness etc.");
			}
			ImGui::ColorEdit4("Alt Tint", &wsettings.AltTint.x);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Tint for the alternative texture. Alpha only affects the color but not roughness etc.");
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
			//data.Sky = static_cast<uint>(sky->mode.get());
			if (auto currentWeather = sky->currentWeather) {
				if (currentWeather->precipitationData && currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					float particleDensity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
					float particleGravity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
					snowingDensity = particleDensity * particleGravity;
					snowing = true;
				} else if (currentWeather->precipitationData && currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					raining = true;
				}
			}
		}
	} else {
		snowingDensity = 0;
	}
	PerFrame data{};
	if (auto calendar = RE::Calendar::GetSingleton()) {
		auto h = calendar->GetHour();
		auto diff = h < lastHour ? h + 24 - lastHour : h - lastHour;
		if (snowing)
			timeSnowing += diff * snowing_speed;
		else {
			if (raining) {
				timeSnowing -= 4 * diff * melting_speed;
			} else {
				if (timeSnowing > 0) {
					timeSnowing -= diff * melting_speed;
				} else {
					timeSnowing += diff * melting_speed;
				}
			}
		}
		timeSnowing = std::clamp(timeSnowing, -1.0f, 2.0f);
		lastHour = h;

		auto time = calendar->GetTime();
		data.Month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 61.0) / 60.0) / 24.0) / 32.0);
	}
	data.SnowingDensity = snowingDensity;
	data.TimeSnowing = timeSnowing;
	data.settings = settings;
	data.wsettings = wsettings;

	return data;
}

void SnowCover::SetupResources()
{
	Reload();
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
		{ "Equation", wsettings.Equation },
		{ "ScreenSpaceScale", wsettings.ScreenSpaceScale },
		{ "LogMicrofacetDensity", wsettings.LogMicrofacetDensity },
		{ "MicrofacetRoughness", wsettings.MicrofacetRoughness },
		{ "DensityRandomization", wsettings.DensityRandomization },
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
		for (auto i = 0; i < 9; ++i)
			wsettings.Equation[i] = config["Equation"][i];
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

		main_tex = config["MainTexture"];
		std::wstring tname = strtowstr(main_tex);
		copyString(main_tex, tbuf, 256);
		logger::warn("Snow cover: {}, {}", main_tex, tbuf);
		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (views[0])
			views[0]->Release();
		if (views[1])
			views[1]->Release();
		if (views[2])
			views[2]->Release();
		HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L".dds").c_str(), nullptr, &views.at(0));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}.dds texture: {}", main_tex, hr);
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L"_n.dds").c_str(), nullptr, &views.at(1));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", main_tex, hr);
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\") + tname + L"_rmaos.dds").c_str(), nullptr, &views.at(2));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_rmaos.dds texture: {}", main_tex, hr);
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
		wsettings.EnableSnowCover = true;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path, e.what());
		status = e.what();
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (const nlohmann::json::exception e) {
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
	if (globals::features::snowCover->wsettings.EnableSnowCover) {
		auto context = globals::d3d::context;
		context->PSSetShaderResources(73, (uint)views.size(), views.data());
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
	if (globals::features::snowCover->wsettings.EnableSnowCover) {
		globals::features::snowCover->BSLightingShader_Setup(Pass);
	}
	func(This, Pass, RenderFlags);
}

void SnowCover::BSLightingShader_Setup(RE::BSRenderPass* a_pass)
{
	auto state = globals::state;
	auto userData = a_pass->geometry->GetUserData();
	if (!userData)
		return;
	if (userData && userData->CanBeMoved() && !userData->As<RE::Actor>())
		state->currentExtraDescriptor |= (uint)State::ExtraShaderDescriptors::IsMobile;
}
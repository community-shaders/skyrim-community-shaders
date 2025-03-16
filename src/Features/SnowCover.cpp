#include "SnowCover.h"

#include "Util.h"
#include <DDSTextureLoader.h>
#include <cstdlib>
#include <cstring>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::UserSettings,
	AffectFoliageColor,
	SnowHeightOffset)

void copyString(const std::string& input, char* dst, size_t dst_size)
{
	strncpy(dst, input.c_str(), dst_size - 1);
	dst[dst_size - 1] = '\0';
}

void SnowCover::DrawSettings()
{
	ImGui::Checkbox("Affect Foliage Color", (bool*)&settings.AffectFoliageColor);
	ImGui::SliderFloat("Snow Line Height Offset", &settings.SnowHeightOffset, -20000.0f, 20000.0f);
	ImGui::Separator();

	if (ImGui::TreeNodeEx("Worldspace Config", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(fmt::format("Current worldspace/cell: {}", last_worldspace).c_str());
		ImGui::Text(fmt::format("Config status: {}", status).c_str());
		if (ImGui::Button("Defaults")) {
			wsettings = WorldSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload")) {
			last_worldspace = "";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			SaveConfig();
		}
		ImGui::Separator();
		ImGui::Checkbox("Enable Snow Cover", (bool*)&wsettings.EnableSnowCover);
		ImGui::SliderFloat("Foliage Color Height Offset", &wsettings.FoliageHeightOffset, -10000.0f, 10000.0f);
		ImGui::Separator();
		ImGui::SliderInt("Maximum Summer Month", (int*)&wsettings.MaxSummerMonth, 0, 11);
		ImGui::SliderInt("Maximum Winter Month", (int*)&wsettings.MaxWinterMonth, 0, 11);
		ImGui::SliderFloat("Summer Height Offset", &wsettings.SummerHeightOffset, -20000.0f, 20000.0f);
		ImGui::SliderFloat("Winter Height Offset", &wsettings.WinterHeightOffset, -20000.0f, 20000.0f);
		ImGui::SliderFloat("Snowing speed", &snowing_speed, 0.01f, 10.0f);
		ImGui::SliderFloat("Melting speed", &melting_speed, 0.01f, 10.0f);
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
		if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::InputText("Main texture", tbuf, 128);
			main_tex = std::string(tbuf);
			ImGui::InputText("Alt texture", altbuf, 128);
			alt_tex = std::string(altbuf);
			ImGui::ColorEdit4("Main Tint", &wsettings.MainTint.x);
			ImGui::ColorEdit4("Alt Tint", &wsettings.AltTint.x);
			ImGui::SliderFloat("UV Scale", &wsettings.UVScale, 0.1f, 10.f, "%.1f");
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
					snowAmount = particleDensity * particleGravity;
					snowing = true;
				} else if (currentWeather->precipitationData && currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					raining = true;
				}
			}
		}
	} else {
		snowAmount = 0;
	}
	PerFrame data{};
	if (auto calendar = RE::Calendar::GetSingleton()) {
		auto h = calendar->GetHour();
		auto diff = h < lastHour ? h + 24 - lastHour : h - lastHour;
		if (snowing)
			timeSnowing += diff * snowing_speed;
		else {
			if (raining)
				diff *= 4;
			timeSnowing -= diff * melting_speed;
		}
		timeSnowing = std::clamp(timeSnowing, 0.0f, 2.0f);
		lastHour = h;

		auto time = calendar->GetTime();
		data.Month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 61.0) / 60.0) / 24.0) / 32.0);
	}
	data.SnowAmount = snowAmount;
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
		if (worldspace) {
			curr_worldspace = worldspace->GetFormEditorID();
		} else if (tes->interiorCell) {
			curr_worldspace = std::string(tes->interiorCell->GetFullName());
		}
	}
	return curr_worldspace;
}

void SnowCover::SaveConfig()
{
	if (last_worldspace.length() == 0)
		return;
	json config = {
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
		logger::error("[Snow Cover] Cannot open config at  {}", path);
		return;
	}
	json config;
	try {
		fileStream >> config;
		wsettings.EnableSnowCover = true;
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

		main_tex = config["MainTexture"];
		std::wstring tname = strtowstr(main_tex);
		copyString(main_tex, tbuf, 128);
		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (views[0])
			views[0]->Release();
		if (views[1])
			views[1]->Release();
		HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + tname + L"_rdao.dds").c_str(), nullptr, &views.at(0));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_rdao.dds texture: {}", main_tex, hr);
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + tname + L"_n.dds").c_str(), nullptr, &views.at(1));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", main_tex, hr);
		if (config.contains("AltTexture")) {
			if (views[2])
				views[2]->Release();
			if (views[3])
				views[3]->Release();
			alt_tex = config["AltTexture"];
			std::wstring altname = strtowstr(alt_tex);
			copyString(alt_tex, altbuf, 128);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + altname + L"_rdao.dds").c_str(), nullptr, &views.at(2));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_rdao.dds texture: {}", alt_tex, hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + altname + L"_n.dds").c_str(), nullptr, &views.at(3));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", alt_tex, hr);

		} else {
			views[2] = views[0];
			views[3] = views[1];
		}

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
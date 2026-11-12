#include "TreeWind.h"

#include <imgui.h>

#include "../I18n/I18n.h"
#include "Globals.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.tree_wind."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TreeWind::Settings,
	Enabled,
	Amplitude,
	BaseSpeed,
	WindSpeedGain,
	BendExponent,
	TreeSpread,
	Flutter,
	FlutterShare,
	FlutterSpeed,
	MinDistance,
	NearScale,
	NearRampEnd,
	GustStrengthCalm,
	GustRarityCalm,
	GustStrengthStorm,
	GustRarityStorm,
	GustWindCalm,
	GustWindStorm,
	GustRainy,
	GustSnow,
	GustCloudy,
	TreeModSupport)

namespace
{
	void SliderWithTooltip(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format, const char* a_tooltip)
	{
		ImGui::SliderFloat(a_label, a_value, a_min, a_max, a_format, ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", a_tooltip);
		}
	}
}

void TreeWind::DrawSettings()
{
	ImGui::Text("%s", std::vformat(T(TKEY("status"), "Weather wind {:.2f}   storminess {:.2f}   gust strength {:.2f}"),
						  std::make_format_args(statusWind, statusStorminess, statusGust))
						  .c_str());
	const size_t shapeCount = bendWeights.Count();
	ImGui::Text("%s", std::vformat(T(TKEY("status_shapes"), "Tree LOD shapes with bend data: {}"), std::make_format_args(shapeCount)).c_str());

	ImGui::SeparatorText(T(TKEY("presets"), "Presets"));
	if (ImGui::Button(T(TKEY("preset_gentle"), "Gentle")))
		ApplyPreset(Preset::Gentle);
	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("preset_default"), "Default")))
		ApplyPreset(Preset::Default);
	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("preset_windy"), "Windy")))
		ApplyPreset(Preset::Windy);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("presets_tooltip"), "Set the sway, speed, flutter and gust values. Distances and weather thresholds stay as they are."));
	}

	ImGui::SeparatorText(T(TKEY("sway"), "Sway"));
	ImGui::Checkbox(T(TKEY("enabled"), "Trees Sway"), &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enabled_tooltip"), "Distant (LOD) trees sway in the wind. Off = still, as without this feature."));
	}
	SliderWithTooltip(T(TKEY("amplitude"), "Sway"), &settings.Amplitude, 0.0f, 200.0f, "%.0f",
		T(TKEY("amplitude_tooltip"), "How far the top of a tall tree moves. Shorter trees move less."));
	SliderWithTooltip(T(TKEY("base_speed"), "Speed"), &settings.BaseSpeed, 0.0f, 2.0f, "%.2f",
		T(TKEY("base_speed_tooltip"), "Sway speed in calm air."));
	SliderWithTooltip(T(TKEY("wind_speed_gain"), "Speed in Wind"), &settings.WindSpeedGain, 0.0f, 4.0f, "%.2f",
		T(TKEY("wind_speed_gain_tooltip"), "Extra sway speed as the weather wind rises."));
	SliderWithTooltip(T(TKEY("bend_exponent"), "Trunk Stiffness"), &settings.BendExponent, 0.5f, 3.0f, "%.2f",
		T(TKEY("bend_exponent_tooltip"), "1 = the whole tree leans. Higher = the lower trunk stays stiffer and the top does more."));
	SliderWithTooltip(T(TKEY("tree_spread"), "Tree Individuality"), &settings.TreeSpread, 0.0f, 6.3f, "%.2f",
		T(TKEY("tree_spread_tooltip"), "0 = neighbouring trees move together. Higher = each tree on its own timing."));

	ImGui::SeparatorText(T(TKEY("branch_flutter"), "Branch Flutter"));
	SliderWithTooltip(T(TKEY("flutter"), "Flutter"), &settings.Flutter, 0.0f, 1.0f, "%.2f",
		T(TKEY("flutter_tooltip"), "Quick small movement of the branch tips. 0 = off."));
	SliderWithTooltip(T(TKEY("flutter_share"), "Trees That Flutter"), &settings.FlutterShare, 0.0f, 1.0f, "%.2f",
		T(TKEY("flutter_share_tooltip"), "Share of the trees with branch flutter."));
	SliderWithTooltip(T(TKEY("flutter_speed"), "Flutter Speed"), &settings.FlutterSpeed, 1.0f, 15.0f, "%.1f",
		T(TKEY("flutter_speed_tooltip"), "How fast the branch tips flutter."));

	ImGui::SeparatorText(T(TKEY("gusts"), "Gusts"));
	SliderWithTooltip(T(TKEY("gust_strength_calm"), "Gusts in Calm Weather"), &settings.GustStrengthCalm, 0.0f, 6.0f, "%.2f",
		T(TKEY("gust_strength_calm_tooltip"), "Extra sway during a tree's gust in clear weather. 1 = double."));
	SliderWithTooltip(T(TKEY("gust_rarity_calm"), "Gust Rarity, Calm"), &settings.GustRarityCalm, 2.0f, 60.0f, "%.0f",
		T(TKEY("gust_rarity_calm_tooltip"), "Higher = rarer, shorter gusts in clear weather."));
	SliderWithTooltip(T(TKEY("gust_strength_storm"), "Gusts in Storms"), &settings.GustStrengthStorm, 0.0f, 6.0f, "%.2f",
		T(TKEY("gust_strength_storm_tooltip"), "Extra sway during a gust in rain or snow."));
	SliderWithTooltip(T(TKEY("gust_rarity_storm"), "Gust Rarity, Storms"), &settings.GustRarityStorm, 2.0f, 60.0f, "%.0f",
		T(TKEY("gust_rarity_storm_tooltip"), "Higher = rarer, shorter gusts in storms."));

	ImGui::SeparatorText(T(TKEY("tree_mods"), "Tree Mods"));
	ImGui::Checkbox(T(TKEY("tree_mod_support"), "Tree Mod Support"), &settings.TreeModSupport);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("tree_mod_support_tooltip"),
							  "Also sways 3D tree LOD from tree mods built differently from DynDOLOD's own models, such as Happy Little Trees, Nature of the Wild Lands and Ulvenwald."));
	}

	if (ImGui::CollapsingHeader(T(TKEY("advanced"), "Advanced: Distances and Weather Types"))) {
		SliderWithTooltip(T(TKEY("near_scale"), "Near-Band Sway"), &settings.NearScale, 0.0f, 1.0f, "%.2f",
			T(TKEY("near_scale_tooltip"), "Sway of the LOD trees right next to the loaded trees, to match their animation."));
		SliderWithTooltip(T(TKEY("min_distance"), "Near Band Starts"), &settings.MinDistance, 0.0f, 40000.0f, "%.0f",
			T(TKEY("min_distance_tooltip"), "Distance where the near band starts."));
		SliderWithTooltip(T(TKEY("near_ramp_end"), "Full Sway From"), &settings.NearRampEnd, 10000.0f, 100000.0f, "%.0f",
			T(TKEY("near_ramp_end_tooltip"), "Distance where LOD trees reach full sway."));
		SliderWithTooltip(T(TKEY("gust_rainy"), "Rainy Weather"), &settings.GustRainy, 0.0f, 1.0f, "%.2f",
			T(TKEY("gust_rainy_tooltip"), "How stormy rainy weathers count, 0 calm to 1 storm."));
		SliderWithTooltip(T(TKEY("gust_snow"), "Snowy Weather"), &settings.GustSnow, 0.0f, 1.0f, "%.2f",
			T(TKEY("gust_snow_tooltip"), "How stormy snowy weathers count."));
		SliderWithTooltip(T(TKEY("gust_cloudy"), "Cloudy Weather"), &settings.GustCloudy, 0.0f, 1.0f, "%.2f",
			T(TKEY("gust_cloudy_tooltip"), "How stormy cloudy weathers count."));
		SliderWithTooltip(T(TKEY("gust_wind_calm"), "Calm Wind Below"), &settings.GustWindCalm, 0.0f, 1.0f, "%.2f",
			T(TKEY("gust_wind_calm_tooltip"), "Weather wind at or below this counts as calm."));
		SliderWithTooltip(T(TKEY("gust_wind_storm"), "Storm Wind Above"), &settings.GustWindStorm, 0.0f, 1.0f, "%.2f",
			T(TKEY("gust_wind_storm_tooltip"), "Weather wind at or above this counts as a storm."));
	}
}

void TreeWind::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TreeWind::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TreeWind::RestoreDefaultSettings()
{
	settings = {};
}

void TreeWind::ApplyPreset(Preset a_preset)
{
	const Settings defaults{};
	settings.Amplitude = defaults.Amplitude;
	settings.BaseSpeed = defaults.BaseSpeed;
	settings.WindSpeedGain = defaults.WindSpeedGain;
	settings.BendExponent = defaults.BendExponent;
	settings.TreeSpread = defaults.TreeSpread;
	settings.Flutter = defaults.Flutter;
	settings.FlutterShare = defaults.FlutterShare;
	settings.FlutterSpeed = defaults.FlutterSpeed;
	settings.GustStrengthCalm = defaults.GustStrengthCalm;
	settings.GustRarityCalm = defaults.GustRarityCalm;
	settings.GustStrengthStorm = defaults.GustStrengthStorm;
	settings.GustRarityStorm = defaults.GustRarityStorm;

	switch (a_preset) {
	case Preset::Gentle:
		settings.Amplitude = 50.0f;
		settings.BaseSpeed = 0.45f;
		settings.Flutter = 0.15f;
		settings.FlutterShare = 0.4f;
		settings.GustStrengthCalm = 0.5f;
		settings.GustStrengthStorm = 2.0f;
		settings.GustRarityStorm = 10.0f;
		break;
	case Preset::Windy:
		settings.Amplitude = 110.0f;
		settings.BaseSpeed = 0.8f;
		settings.Flutter = 0.35f;
		settings.FlutterShare = 0.8f;
		settings.GustStrengthCalm = 2.0f;
		settings.GustRarityCalm = 20.0f;
		settings.GustStrengthStorm = 5.0f;
		break;
	case Preset::Default:
		break;
	}
}

struct TreeWind::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			func(This, Pass, RenderFlags);
			globals::features::treeWind.BeginDraw(Pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			globals::features::treeWind.EndDraw();
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSUtilityShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			func(This, Pass, RenderFlags);
			globals::features::treeWind.BeginDraw(Pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSUtilityShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			globals::features::treeWind.EndDraw();
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x7, BSLightingShader_RestoreGeometry>(RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x6, BSUtilityShader_SetupGeometry>(RE::VTABLE_BSUtilityShader[0]);
		stl::write_vfunc<0x7, BSUtilityShader_RestoreGeometry>(RE::VTABLE_BSUtilityShader[0]);
		logger::info("[Tree Wind] Installed hooks - BSLightingShader and BSUtilityShader Setup/RestoreGeometry");
	}
};

void TreeWind::PostPostLoad()
{
	if (GetModuleHandleW(L"K747_LodWind.dll")) {
		loaded = false;
		failedLoadedMessage = T(TKEY("old_plugin_installed"),
			"The Windswept Tree LOD plugin (K747_LodWind.dll) is installed. This feature replaces it: remove Windswept Tree LOD to use it.");
		logger::warn("[Tree Wind] {}", failedLoadedMessage);
		return;
	}
	Hooks::Install();
}

void TreeWind::BeginDraw(RE::BSRenderPass* a_pass)
{
	drawBendWeights = nullptr;
	if (!swayActive)
		return;
	const auto kind = TreeShapes::Classify(a_pass, settings.TreeModSupport);
	drawBendWeights = kind == TreeShapes::Kind::None ? nullptr : bendWeights.Get(a_pass->geometry, kind, globals::state->frameCount, settings.TreeModSupport);
}

void TreeWind::EndDraw()
{
	drawBendWeights = nullptr;
}

void TreeWind::SetupResources()
{
	windOnCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<WindCB>(), "TreeWind::WindOn");
	windOffCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<WindCB>(), "TreeWind::WindOff");
}

float TreeWind::GetStorminess(const RE::TESWeather* a_weather) const
{
	if (!a_weather)
		return 0.0f;

	// Vanilla weather wind barely differs between clear and storm, so the weather type counts too
	using Flag = RE::TESWeather::WeatherDataFlag;
	float typeStorminess = 0.0f;
	if (a_weather->data.flags.any(Flag::kRainy))
		typeStorminess = settings.GustRainy;
	else if (a_weather->data.flags.any(Flag::kSnow))
		typeStorminess = settings.GustSnow;
	else if (a_weather->data.flags.any(Flag::kCloudy))
		typeStorminess = settings.GustCloudy;

	const float wind = static_cast<float>(a_weather->data.windSpeed) / 255.0f;
	const float windStorminess = std::clamp((wind - settings.GustWindCalm) / std::max(settings.GustWindStorm - settings.GustWindCalm, 0.01f), 0.0f, 1.0f);
	return std::clamp(std::max(typeStorminess, windStorminess), 0.0f, 1.0f);
}

void TreeWind::Reset()
{
	ZoneScoped;
	float windSpeed = 0.0f;
	float windAngle = 0.0f;
	float storminess = 0.0f;
	if (auto sky = globals::game::sky) {
		windSpeed = sky->windSpeed;
		windAngle = sky->windAngle;
		const float weatherPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		storminess = sky->lastWeather ?
		                 GetStorminess(sky->currentWeather) * weatherPct + GetStorminess(sky->lastWeather) * (1.0f - weatherPct) :
		                 GetStorminess(sky->currentWeather);
	}

	const float gustStrength = std::lerp(settings.GustStrengthCalm, settings.GustStrengthStorm, storminess);
	const float gustRarity = std::lerp(settings.GustRarityCalm, settings.GustRarityStorm, storminess);
	statusWind = windSpeed;
	statusStorminess = storminess;
	statusGust = gustStrength;

	// Capped so a hitch or a load does not jump the trees
	const bool paused = globals::game::ui && globals::game::ui->GameIsPaused();
	const double deltaTime = paused ? 0.0 : std::min(static_cast<double>(RE::GetSecondsSinceLastFrame()), 0.25);
	prevPhase = phase;
	phase += deltaTime * (settings.BaseSpeed + settings.WindSpeedGain * std::clamp(windSpeed, 0.0f, 1.0f));
	// Wraps at 200 pi, a whole number of cycles for every sway frequency in TreeWind.hlsli, and keeps float
	// precision in the shader. Flutter stays continuous for flutter speeds with up to two decimals.
	constexpr double kPhaseWrap = 200.0 * std::numbers::pi;
	if (phase > kPhaseWrap) {
		phase -= kPhaseWrap;
		prevPhase -= kPhaseWrap;
	}

	// The map menus draw the LOD too, and it should stand still there. While off, the draws skip all
	// per-draw work; both buffers then read as off, whatever is still bound at VS b3.
	swayActive = settings.Enabled && !globals::state->isMapMenuOpen;

	WindCB data{};
	data.Wind = { static_cast<float>(phase), static_cast<float>(prevPhase), windSpeed, windAngle };
	data.Params = { settings.Amplitude, settings.MinDistance, swayActive ? 1.0f : 0.0f, 0.0f };
	data.Shape = { settings.BendExponent, 1.0f, settings.Flutter, settings.TreeSpread };
	data.Detail = { settings.FlutterShare, settings.FlutterSpeed, settings.NearScale, settings.NearRampEnd };
	data.Gusts = { gustStrength, gustRarity, 0.0f, 0.0f };
	windOnCB->Update(data);

	data.Params.z = 0.0f;
	windOffCB->Update(data);

	if (bendWeightsTreeModSupport != settings.TreeModSupport) {
		bendWeights.Clear();
		bendWeightsTreeModSupport = settings.TreeModSupport;
	} else if (globals::state->frameCount % 300 == 0) {
		bendWeights.Evict(globals::state->frameCount);
	}
}

void TreeWind::SetShaderResources(ID3D11DeviceContext* a_context)
{
	if (!swayActive)
		return;
	const auto shader = globals::state->currentShader;
	if (!shader)
		return;

	const auto type = shader->shaderType.get();
	// Standard tree LOD draws only trees, and sways from its instance data without bend data
	if (type == RE::BSShader::Type::DistantTree) {
		ID3D11Buffer* buffer = windOnCB->CB();
		a_context->VSSetConstantBuffers(3, 1, &buffer);
		return;
	}
	if (type != RE::BSShader::Type::Lighting && type != RE::BSShader::Type::Utility)
		return;

	ID3D11Buffer* buffer = drawBendWeights ? windOnCB->CB() : windOffCB->CB();
	a_context->VSSetConstantBuffers(3, 1, &buffer);
	if (drawBendWeights)
		a_context->VSSetShaderResources(120, 1, &drawBendWeights);
}

#undef I18N_KEY_PREFIX

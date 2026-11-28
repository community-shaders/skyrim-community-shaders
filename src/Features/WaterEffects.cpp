#include "WaterEffects.h"

#include <DDSTextureLoader.h>

#include "Utils/D3D.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.water_effects."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WaterEffects::Settings,
	EnableCaustics,
	EnableParallax,
	ParallaxFadeStart,
	ParallaxFadeEnd)

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	winrt::com_ptr<ID3D11Resource> causticsTexture;
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", causticsTexture.put(), causticsView.put());
	Util::SetResourceName(causticsTexture.get(), "WaterEffects::Caustics");
	Util::SetResourceName(causticsView.get(), "WaterEffects::Caustics SRV");
}

void WaterEffects::Prepass()
{
	auto context = globals::d3d::context;
	auto srv = causticsView.get();
	context->PSSetShaderResources(65, 1, &srv);
}

bool WaterEffects::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

void WaterEffects::DrawSettings()
{
	auto& s = settings;
	Util::CheckboxTip(T(TKEY("enable_parallax"), "Enable Parallax"), &s.EnableParallax,
		T(TKEY("enable_parallax_tooltip"), "Ray-marched relief of the water's normal maps. The costliest part of vanilla water shading; off flattens the waves' shading detail."));
	{
		Util::DisableGuard disableGuard(!s.EnableParallax);
		Util::SliderFloatTip(T(TKEY("parallax_fade_start"), "Parallax Fade Start"), &s.ParallaxFadeStart, 0.0f, 300.0f, "%.0f m",
			T(TKEY("parallax_fade_start_tooltip"), "Distance beyond which the parallax starts to fade out. The relief is blurred away by the normal maps' mips there anyway. Default 30 m."));
		Util::SliderFloatTip(T(TKEY("parallax_fade_end"), "Parallax Fade End"), &s.ParallaxFadeEnd, 1.0f, 600.0f, "%.0f m",
			T(TKEY("parallax_fade_end_tooltip"), "Distance beyond which there is no parallax, and none of its cost. Default 90 m."));
	}
	Util::CheckboxTip(T(TKEY("enable_caustics"), "Enable Caustics"), &s.EnableCaustics,
		T(TKEY("enable_caustics_tooltip"), "Caustic light patterns on the terrain, objects and grass under the water."));
}

void WaterEffects::LoadSettings(json& o_json)
{
	settings = o_json;
	// Malformed values must never reach the shaders (a non-finite or inverted fade range).
	const Settings d{};
	auto clampTo = [](float& v, float lo, float hi, float fallback) { v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
	clampTo(settings.ParallaxFadeStart, 0.0f, 10000.0f, d.ParallaxFadeStart);
	clampTo(settings.ParallaxFadeEnd, 0.0f, 10000.0f, d.ParallaxFadeEnd);
	settings.ParallaxFadeEnd = std::max(settings.ParallaxFadeEnd, settings.ParallaxFadeStart + 1.0f);
}

void WaterEffects::SaveSettings(json& o_json)
{
	o_json = settings;
}

void WaterEffects::RestoreDefaultSettings()
{
	settings = {};
}

WaterEffects::SharedSettings WaterEffects::GetCommonBufferData() const
{
	SharedSettings data{};
	// Metres to units; the fade end is 0 with the parallax off (WaterEffects::ParallaxFade).
	constexpr float unitsPerMetre = 70.0f;
	data.Params = { settings.EnableCaustics ? 1.0f : 0.0f, settings.ParallaxFadeStart * unitsPerMetre,
		settings.EnableParallax ? settings.ParallaxFadeEnd * unitsPerMetre : 0.0f, 0.0f };
	return data;
}

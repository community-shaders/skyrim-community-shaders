#include "InverseSquareLighting.h"
#include "Features/InverseSquareLighting/Common.h"
#include "LightLimitFix.h"

void InverseSquareLighting::DrawSettings()
{
	editor.DrawSettings();
}

void InverseSquareLighting::EarlyPrepass()
{
	editor.GatherLights();
}

void InverseSquareLighting::PostPostLoad()
{
	stl::detour_thunk<CreatePointLight>(REL::RelocationID(17208, 17610));
	stl::detour_thunk<BSLight_GetLuminance>(REL::RelocationID(101303, 108292));

	logger::info("[InverseSquareLighting] Installed hooks");
}

RE::NiPointLight* InverseSquareLighting::CreatePointLight::thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly)
{
	const auto niLight = func(ligh, refr, root, forceDynamic, useLightRadius, affectRequesterOnly);

	if (ligh && root && niLight)
		SetExtLightData(niLight, ligh);

	return niLight;
}

void InverseSquareLighting::SetExtLightData(RE::NiLight* niLight, const RE::TESObjectLIGH* ligh)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	runtimeData->flags.set(LightLimitFix::LightFlags::Initialised);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	runtimeData->cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	runtimeData->lighFormId = ligh->formID;
}

void InverseSquareLighting::ProcessLight(LightLimitFix::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (light.lightFlags.none(LightLimitFix::LightFlags::Initialised)) {
		const auto userData = niLight->GetUserData();
		logger::debug("[InverseSquareLighting] FormID: 0x{:08X} | Light*: {:p} | Name: {} - light uninitialised", userData ? userData->formID : 0, static_cast<void*>(niLight), niLight->name);
		runtimeData->flags.set(LightLimitFix::LightFlags::Initialised);
	}

	const bool isInvSq = light.lightFlags.any(LightLimitFix::LightFlags::InverseSquare);
	if (bsLight->pointLight && editor.enabled && ((isInvSq && editor.disableInvSqLights) || (!isInvSq && editor.disableRegularLights)))
		light.lightFlags.set(LightLimitFix::LightFlags::Disabled);

	if (bsLight->pointLight && isInvSq) {
		const float intensity = runtimeData->fade * 4;
		light.radius = CalculateRadius(intensity, bsLight->IsShadowLight(), runtimeData->cutoffOverride);
		light.invRadius = 1.f / light.radius;
		light.fadeZone = 1.f / (light.radius * std::clamp(FadeZoneBase * light.invRadius, 0.f, 1.f));
		runtimeData->radius.x = light.radius;
		runtimeData->radius.y = light.radius;
		runtimeData->radius.z = light.radius;
		light.color /= std::max(0.001f, std::max(light.color.x, std::max(light.color.y, light.color.z)));
		light.color *= intensity;
	} else {
		light.radius = runtimeData->radius.x;
		light.invRadius = 1.f / light.radius;
		light.color *= runtimeData->fade;
	}
}

float InverseSquareLighting::CalculateRadius(const float intensity, const bool shadowCaster, const float cutoffOverride)
{
	float cutoff = shadowCaster ? DefaultShadowCasterCutoff : DefaultCutoff;
	cutoff = cutoffOverride == 1.f ? cutoff : cutoffOverride;
	const float radius = std::sqrt(ScaledUnitsSq * ((intensity - cutoff) / cutoff));
	return isnan(radius) ? 1.f : radius;
}

inline float InverseSquareLighting::SmoothStep(const float edge0, const float edge1, const float x)
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float InverseSquareLighting::GetAttenuation(const float distance, const float radius)
{
	const float attenuation = ScaledUnitsSq / (distance * distance + ScaledUnitsSq);
	const float fadeZone = std::clamp(FadeZoneBase / radius, 0.0f, 1.0f);
	const float fade = SmoothStep(0, radius * fadeZone, radius - distance);
	return attenuation * fade;
}

float InverseSquareLighting::BSLight_GetLuminance::thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight)
{
	auto* niLight = bsLight->light.get();
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (refLight == niLight || runtimeData->flags.any(LightLimitFix::LightFlags::Disabled))
		return 0.0f;

	if (!bsLight->pointLight || runtimeData->flags.none(LightLimitFix::LightFlags::InverseSquare))
		return func(bsLight, targetPosition, refLight);

	const float dist = niLight->world.translate.GetDistance(*targetPosition);
	const float attenuation = GetAttenuation(dist, runtimeData->radius.x);
	const float luminance = (runtimeData->diffuse.red + runtimeData->diffuse.green + runtimeData->diffuse.blue) * runtimeData->fade * attenuation * (1.0f / 3.0f);
	bsLight->luminance = luminance;

	return luminance;
}

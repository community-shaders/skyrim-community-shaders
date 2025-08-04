#include "LinearLighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	enableLinearLighting,
	enableGammaCorrection,
	lightGamma,
	colorGamma,
	ambientGamma,
	fogGamma,
	effectGamma,
	effectAlphaGamma,
	skyGamma,
	waterGamma,
	vlGamma,
	vanillaDiffuseMult,
	vanillaSpecularMult,
	lightMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

void LinearLighting::DrawSettings()
{
	ImGui::Checkbox("Enable Linear Lighting", (bool*)&settings.enableLinearLighting);
	ImGui::Checkbox("Enable Gamma Correction", (bool*)&settings.enableGammaCorrection);
	ImGui::Text("Gamma Settings");
	ImGui::SliderFloat("Light Gamma", &settings.lightGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Color Gamma", &settings.colorGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Ambient Gamma", &settings.ambientGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Fog Gamma", &settings.fogGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Effect Gamma", &settings.effectGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Effect Transparency Gamma", &settings.effectAlphaGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Sky Gamma", &settings.skyGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Water Gamma", &settings.waterGamma, 1.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Volumetric Lighting Gamma", &settings.vlGamma, 1.0f, 3.0f, "%.2f");

	ImGui::SeparatorText("Multipliers");
	ImGui::SliderFloat("Vanilla Diffuse Multiplier", &settings.vanillaDiffuseMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Vanilla Specular Multiplier", &settings.vanillaSpecularMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Light Multiplier", &settings.lightMult, 0.0f, 10.0f, "%.2f");
	if (ImGui::TreeNode("Effects")) {
		ImGui::SliderFloat("Membrane Effects Multiplier", &settings.membraneEffectMult, 0.0f, 10.0f, "%.2f");
		ImGui::SliderFloat("Blood Effects Multiplier", &settings.bloodEffectMult, 0.0f, 10.0f, "%.2f");
		ImGui::SliderFloat("Projected Effects Multiplier", &settings.projectedEffectMult, 0.0f, 10.0f, "%.2f");
		ImGui::SliderFloat("Deferred Effects Multiplier", &settings.deferredEffectMult, 0.0f, 10.0f, "%.2f");
		ImGui::SliderFloat("Other Effects Multiplier", &settings.otherEffectMult, 0.0f, 10.0f, "%.2f");
		ImGui::TreePop();
	}
}

void LinearLighting::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LinearLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LinearLighting::RestoreDefaultSettings()
{
	settings = {};
}

void LinearLighting::PostPostLoad()
{
	MenuOpenCloseEventHandler::Register();
}

void LinearLighting::Prepass()
{
	if (!settings.enableLinearLighting || tempDisable)
		return;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (!imageSpaceManager)
		return;

	dirLightMult = !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;
}

LinearLighting::PerFrameData LinearLighting::GetCommonBufferData()
{
	auto data = PerFrameData{};
	data.enableLinearLighting = settings.enableLinearLighting && !tempDisable;
	data.enableGammaCorrection = settings.enableGammaCorrection;
	data.dirLightMult = dirLightMult;
	data.lightGamma = settings.lightGamma;
	data.colorGamma = settings.colorGamma;
	data.ambientGamma = settings.ambientGamma;
	data.fogGamma = settings.fogGamma;
	data.effectGamma = settings.effectGamma;
	data.effectAlphaGamma = settings.effectAlphaGamma;
	data.skyGamma = settings.skyGamma;
	data.waterGamma = settings.waterGamma;
	data.vlGamma = settings.vlGamma;
	data.vanillaDiffuseMult = settings.vanillaDiffuseMult;
	data.vanillaSpecularMult = settings.vanillaSpecularMult;
	data.lightMult = settings.lightMult;
	data.membraneEffectMult = settings.membraneEffectMult;
	data.bloodEffectMult = settings.bloodEffectMult;
	data.projectedEffectMult = settings.projectedEffectMult;
	data.deferredEffectMult = settings.deferredEffectMult;
	data.otherEffectMult = settings.otherEffectMult;
	data.pad = 0.0f;
	return data;
}
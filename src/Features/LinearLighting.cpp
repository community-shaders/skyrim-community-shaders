#include "LinearLighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	enableLinearLighting,
	enableGammaCorrection,
	lightGamma,
	colorGamma,
	emitColorGamma,
	glowmapGamma,
	ambientGamma,
	fogGamma,
	fogAlphaGamma,
	effectGamma,
	effectAlphaGamma,
	skyGamma,
	waterGamma,
	vlGamma,
	vanillaDiffuseMult,
	vanillaSpecularMult,
	grassDiffuseMult,
	grassSpecularMult,
	vanillaDiffuseColorMult,
	lightMult,
	directionalLightMult,
	pointLightMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
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
	ImGui::SliderFloat("Light Gamma", &settings.lightGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Color Gamma", &settings.colorGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Emissive Color Gamma", &settings.emitColorGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Glowmap Gamma", &settings.glowmapGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Ambient Gamma", &settings.ambientGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Fog Gamma", &settings.fogGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Fog Transparency Gamma", &settings.fogAlphaGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Effect Gamma", &settings.effectGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Effect Transparency Gamma", &settings.effectAlphaGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Sky Gamma", &settings.skyGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Water Gamma", &settings.waterGamma, 0.1f, 3.0f, "%.2f");
	ImGui::SliderFloat("Volumetric Lighting Gamma", &settings.vlGamma, 0.1f, 3.0f, "%.2f");

	ImGui::SeparatorText("Multipliers");
	ImGui::SliderFloat("Vanilla Diffuse Multiplier", &settings.vanillaDiffuseMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Vanilla Specular Multiplier", &settings.vanillaSpecularMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Grass Diffuse Multiplier", &settings.grassDiffuseMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Grass Specular Multiplier", &settings.grassSpecularMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Vanilla Diffuse Color Multiplier", &settings.vanillaDiffuseColorMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Light Multiplier", &settings.lightMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Directional Light Multiplier", &settings.directionalLightMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Point Light Multiplier", &settings.pointLightMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Emissive Color Multiplier", &settings.emitColorMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Glowmap Multiplier", &settings.glowmapMult, 0.0f, 10.0f, "%.2f");
	if (ImGui::TreeNodeEx("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Effect Lighting Multiplier", &settings.effectLightingMult, 0.0f, 10.0f, "%.2f");
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

void LinearLighting::Prepass()
{
	bool isMainLoadingMenu = globals::game::ui && (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	dirLightMult = 1.0f;
	if (!settings.enableLinearLighting || isMainLoadingMenu)
		return;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (!imageSpaceManager)
		return;

	dirLightMult = !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;
}

LinearLighting::PerFrameData LinearLighting::GetCommonBufferData()
{
	bool isMainLoadingMenu = globals::game::ui && (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	auto data = PerFrameData{};
	data.enableLinearLighting = settings.enableLinearLighting && !isMainLoadingMenu;
	data.enableGammaCorrection = settings.enableGammaCorrection;
	data.isDirLightLinear = isDirLightLinear;
	data.dirLightMult = dirLightMult;
	data.lightGamma = settings.lightGamma;
	data.colorGamma = settings.colorGamma;
	data.emitColorGamma = settings.emitColorGamma;
	data.glowmapGamma = settings.glowmapGamma;
	data.ambientGamma = settings.ambientGamma;
	data.fogGamma = settings.fogGamma;
	data.fogAlphaGamma = settings.fogAlphaGamma;
	data.effectGamma = settings.effectGamma;
	data.effectAlphaGamma = settings.effectAlphaGamma;
	data.skyGamma = settings.skyGamma;
	data.waterGamma = settings.waterGamma;
	data.vlGamma = settings.vlGamma;
	data.vanillaDiffuseMult = settings.vanillaDiffuseMult;
	data.vanillaSpecularMult = settings.vanillaSpecularMult;
	data.grassDiffuseMult = settings.grassDiffuseMult;
	data.grassSpecularMult = settings.grassSpecularMult;
	data.vanillaDiffuseColorMult = settings.vanillaDiffuseColorMult;
	data.lightMult = settings.lightMult;
	data.directionalLightMult = settings.directionalLightMult;
	data.pointLightMult = settings.pointLightMult;
	data.emitColorMult = settings.emitColorMult;
	data.glowmapMult = settings.glowmapMult;
	data.effectLightingMult = settings.effectLightingMult;
	data.membraneEffectMult = settings.membraneEffectMult;
	data.bloodEffectMult = settings.bloodEffectMult;
	data.projectedEffectMult = settings.projectedEffectMult;
	data.deferredEffectMult = settings.deferredEffectMult;
	data.otherEffectMult = settings.otherEffectMult;
	return data;
}
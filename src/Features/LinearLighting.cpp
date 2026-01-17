#include "LinearLighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	enableLinearLighting,
	enableGammaCorrection,
	clearRenderGamma,
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
	ImGui::SliderFloat("Clear Render Gamma", &settings.clearRenderGamma, 0.1f, 3.0f, "%.2f");
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

void LinearLighting::SetupResources()
{
	PerGeometryCB = new ConstantBuffer(ConstantBufferDesc<PerGeometryData>());
}

void LinearLighting::EarlyPrepass()
{
	ConvertClearColor();
}

void LinearLighting::ReflectionsPrepass()
{
	ConvertClearColor();
}

void LinearLighting::ConvertClearColor()
{
	bool isMainLoadingMenu = globals::game::ui && (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	if (!settings.enableLinearLighting || isMainLoadingMenu)
		return;

	// Convert clear color from gamma space to linear space at the source
	// This affects the background color visible at worldspace edges and in water reflections
	auto renderer = globals::game::renderer;
	if (renderer) {
		auto& rendererData = renderer->GetRendererData();

		const bool isAlreadyConverted =
			rendererData.clearColor[0] == lastConvertedClearColor[0] &&
			rendererData.clearColor[1] == lastConvertedClearColor[1] &&
			rendererData.clearColor[2] == lastConvertedClearColor[2];

		// Only refresh the source color when the engine has changed it
		if (!isAlreadyConverted &&
			(rendererData.clearColor[0] != lastOriginalClearColor[0] ||
				rendererData.clearColor[1] != lastOriginalClearColor[1] ||
				rendererData.clearColor[2] != lastOriginalClearColor[2])) {
			lastOriginalClearColor[0] = rendererData.clearColor[0];
			lastOriginalClearColor[1] = rendererData.clearColor[1];
			lastOriginalClearColor[2] = rendererData.clearColor[2];
		}

		float gamma = std::clamp(settings.clearRenderGamma, 0.1f, 3.0f);
		if (isAlreadyConverted && gamma == lastConvertedClearGamma) {
			return;
		}

		rendererData.clearColor[0] = std::pow(lastOriginalClearColor[0], gamma);
		rendererData.clearColor[1] = std::pow(lastOriginalClearColor[1], gamma);
		rendererData.clearColor[2] = std::pow(lastOriginalClearColor[2], gamma);
		// Alpha (clearColor[3]) typically doesn't need gamma correction

		// Remember what we converted to
		lastConvertedClearColor[0] = rendererData.clearColor[0];
		lastConvertedClearColor[1] = rendererData.clearColor[1];
		lastConvertedClearColor[2] = rendererData.clearColor[2];
		lastConvertedClearGamma = gamma;
	}
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

struct LinearLighting::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			globals::features::linearLighting.BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("[LinearLighting] Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void LinearLighting::PostPostLoad()
{
	LinearLighting::Hooks::Install();
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

RE::NiColor LinearLighting::ColorToLinear(RE::NiColor inColor, float gamma)
{
	RE::NiColor outColor;
	outColor.red = std::pow(inColor.red, gamma);
	outColor.green = std::pow(inColor.green, gamma);
	outColor.blue = std::pow(inColor.blue, gamma);
	return outColor;
}

void LinearLighting::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	auto& property1 = a_pass->geometry->GetGeometryRuntimeData().properties[1];
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	if (lightProperty != nullptr) {
		float emissiveMult = 1.0f;
		if (settings.enableLinearLighting) {
			emissiveMult = lightProperty->emissiveMult;
			PerGeometryData perGeometryData{};
			perGeometryData.emissiveMult = emissiveMult;
			PerGeometryCB->Update(perGeometryData);

			ID3D11Buffer* buffer = { PerGeometryCB->CB() };
			auto context = globals::d3d::context;
			context->PSSetConstantBuffers(8, 1, &buffer);
		}
	}
}
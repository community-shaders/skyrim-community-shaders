#include "ENBDepthOfField.h"

#include "../EffectManager.h"
#include "../SettingManager.h"
#include "../TextureManager.h"
#include "State.h"

static constexpr std::array<std::string_view, 3> FocusTechniques = { "Aperture", "ReadFocus", "Focus" };

bool ENBDepthOfField::Apply()
{
	historyValid = false;
	const bool applied = EffectBase::Apply();

	// Lowest name keeps the fallback deterministic across unordered_map iteration
	fallbackTechnique.clear();
	for (const auto& name : techniques | std::views::keys)
		if (std::ranges::find(FocusTechniques, name) == FocusTechniques.end() && (fallbackTechnique.empty() || name < fallbackTechnique))
			fallbackTechnique = name;

	return applied;
}

void ENBDepthOfField::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto* renderer = globals::game::renderer;
	if (!renderer)
		return;

	auto& textureMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!textureMain.texture || !textureMain.SRV)
		return;

	auto* textureHDRTemp = textureManager.GetCommonTexture("TextureHDRTemp");
	auto* textureHDRTemp2 = textureManager.GetCommonTexture("TextureHDRTemp2");
	if (!textureHDRTemp || !textureHDRTemp2)
		return;

	const bool swap = (textureManager.GetTextureSwap() & 1) != 0;

	auto& textureApertureRead = effectTextureCache[swap ? "TextureApertureSwap" : "TextureAperture"];
	auto& textureApertureWrite = effectTextureCache[swap ? "TextureAperture" : "TextureApertureSwap"];
	auto& textureReadFocus = effectTextureCache["TextureReadFocus"];
	auto& textureFocusRead = effectTextureCache[swap ? "TextureFocusSwap" : "TextureFocus"];
	auto& textureFocusWrite = effectTextureCache[swap ? "TextureFocus" : "TextureFocusSwap"];

	if (!textureApertureRead.srv || !textureApertureWrite.rtv || !textureReadFocus.rtv ||
		!textureFocusRead.srv || !textureFocusWrite.rtv)
		return;

	// Fresh textures hold undefined data; UpdateEffectVariables also skips history blending this frame
	if (!historyValid) {
		static constexpr float clearColor[4] = {};
		globals::d3d::context->ClearRenderTargetView(textureApertureRead.rtv.get(), clearColor);
		globals::d3d::context->ClearRenderTargetView(textureFocusRead.rtv.get(), clearColor);
	}

	SetShaderResourceVariable("TexturePrevious", textureApertureRead.srv.get());
	// Publish the aperture only when the pass wrote it; an effect without a valid Aperture technique leaves it unwritten
	if (ExecuteTechnique("Aperture", textureApertureWrite)) {
		apertureSRV = textureApertureWrite.srv.get();
		apertureFrame = globals::state->frameCount;
	}

	SetShaderResourceVariable("TextureAperture", textureApertureWrite.srv.get());
	ExecuteTechnique("ReadFocus", textureReadFocus);

	SetShaderResourceVariable("TexturePrevious", textureFocusRead.srv.get());
	SetShaderResourceVariable("TextureCurrent", textureReadFocus.srv.get());
	ExecuteTechnique("Focus", textureFocusWrite);
	historyValid = true;

	SetShaderResourceVariable("TextureFocus", textureFocusWrite.srv.get());
	SetShaderResourceVariable("TextureOriginal", textureMain.SRV);

	const auto technique = selectedTechniqueIndex < uiTechniques.size() ? GetSelectedTechnique() : fallbackTechnique;
	auto [executed, inOutput, inTemp] = ExecuteTechniqueSequence(technique, textureMain.SRV, *textureHDRTemp, *textureHDRTemp2);

	if (executed && (inOutput || inTemp)) {
		auto* result = inOutput ? textureHDRTemp : textureHDRTemp2;
		EffectManager::GetSingleton().CopyToTarget(result->texture.get(), result->srv.get(), textureMain.texture, textureMain.RTV);
	}
}

void ENBDepthOfField::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	if (!idsCached) {
		idApertureTime = settingManager.GetSettingID("ApertureTime", "DEPTHOFFIELD");
		idFocusingTime = settingManager.GetSettingID("FocusingTime", "DEPTHOFFIELD");
		idEnableAdaptation = settingManager.GetSettingID("EnableAdaptation", "EFFECT");
		idsCached = true;
	}

	// DOF runs before adaptation, so only last frame's result exists yet; a failed
	// enbadaptation.fx never writes it
	ID3D11ShaderResourceView* adaptationSRV = nullptr;
	if (idEnableAdaptation != 0xFFFFFFFF && settingManager.GetValue<bool>(idEnableAdaptation) && EffectManager::GetSingleton().enbAdaptation.IsCompiled()) {
		auto& textureManager = TextureManager::GetSingleton();
		auto* texture = textureManager.FindCommonTexture((textureManager.GetTextureSwap() & 1) ? "TextureAdaptationSwap" : "TextureAdaptation");
		adaptationSRV = texture ? texture->srv.get() : nullptr;
	}
	SetShaderResourceVariable("TextureAdaptation", adaptationSRV);

	const float deltaTime = globals::game::deltaTime ? (*globals::game::deltaTime) : 0.0f;
	auto blendFactor = [&](uint32_t settingID) {
		const float time = settingManager.GetValue<float>(settingID);
		return historyValid ? std::clamp(time > 0.0f ? deltaTime / time : 1.0f, 0.0f, 1.0f) : 1.0f;
	};

	float4 dofParameters{};
	dofParameters.z = blendFactor(idApertureTime);
	dofParameters.w = blendFactor(idFocusingTime);

	SetVectorVariable("DofParameters", &dofParameters, sizeof(dofParameters));
}

ID3D11ShaderResourceView* ENBDepthOfField::GetApertureSRV() const
{
	return apertureFrame == globals::state->frameCount ? apertureSRV : nullptr;
}

void ENBDepthOfField::CreateEffectTextures()
{
	effectTextureCache["TextureAperture"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureAperture");
	effectTextureCache["TextureApertureSwap"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureApertureSwap");
	effectTextureCache["TextureReadFocus"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureReadFocus");
	effectTextureCache["TextureFocus"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureFocus");
	effectTextureCache["TextureFocusSwap"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureFocusSwap");
}

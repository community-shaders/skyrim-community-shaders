#include "ENBDepthOfField.h"

#include "../SettingManager.h"
#include "../TextureManager.h"

void ENBDepthOfField::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	const std::string texturePreviousApertureName = (textureManager.GetTextureSwap() & 1) ? "TextureApertureSwap" : "TextureAperture";
	const std::string textureApertureName = (textureManager.GetTextureSwap() & 1) ? "TextureAperture" : "TextureApertureSwap";

	SetShaderResourceVariable("TexturePrevious", effectTextureCache[texturePreviousApertureName].srv.get());
	ExecuteTechnique("Aperture", effectTextureCache[textureApertureName]);

	SetShaderResourceVariable("TextureAperture", effectTextureCache[textureApertureName].srv.get());
	ExecuteTechnique("ReadFocus", effectTextureCache["TextureReadFocus"]);

	const std::string texturePreviousFocusName = (textureManager.GetTextureSwap() & 1) ? "TextureFocusSwap" : "TextureFocus";
	const std::string textureFocusName = (textureManager.GetTextureSwap() & 1) ? "TextureFocus" : "TextureFocusSwap";

	SetShaderResourceVariable("TexturePrevious", effectTextureCache[texturePreviousFocusName].srv.get());
	SetShaderResourceVariable("TextureCurrent", effectTextureCache["TextureReadFocus"].srv.get());
	ExecuteTechnique("Focus", effectTextureCache[textureFocusName]);

	auto textureMain = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto textureHDRTemp = textureManager.GetCommonTexture("TextureHDRTemp");
	auto textureHDRTemp2 = textureManager.GetCommonTexture("TextureHDRTemp2");

	globals::d3d::context->CopyResource(textureHDRTemp->texture.get(), textureMain.texture);

	SetShaderResourceVariable("TextureFocus", effectTextureCache[textureFocusName].srv.get());
	SetShaderResourceVariable("TextureOriginal", textureMain.SRV);
	ExecuteTechniqueSequence(GetSelectedTechnique(), textureMain.SRV, *textureHDRTemp, *textureHDRTemp2);

	globals::d3d::context->CopyResource(textureMain.texture, textureHDRTemp->texture.get());
}

void ENBDepthOfField::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	float4 dofParameters{};
	dofParameters.z = (*globals::game::deltaTime) * settingManager.GetValue<float>("ApertureTime", "DEPTHOFFIELD");
	dofParameters.w = (*globals::game::deltaTime) * settingManager.GetValue<float>("FocusingTime", "DEPTHOFFIELD");

	auto DofParameters = effect->GetVariableByName("DofParameters")->AsVector();
	if (DofParameters && DofParameters->IsValid())
		DofParameters->SetRawValue(&dofParameters, 0, sizeof(dofParameters));
}

void ENBDepthOfField::CreateEffectTextures()
{
	effectTextureCache["TextureAperture"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureAperture");
	effectTextureCache["TextureApertureSwap"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureApertureSwap");
	effectTextureCache["TextureReadFocus"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureReadFocus");
	effectTextureCache["TextureFocus"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureFocus");
	effectTextureCache["TextureFocusSwap"] = CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "ENBDepthOfField::TextureFocusSwap");
}

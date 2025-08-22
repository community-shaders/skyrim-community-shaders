#include "ENBDepthOfField.h"

#include "../SettingManager.h"
#include "../TextureManager.h"

void ENBDepthOfField::Execute()
{
	auto renderer = globals::game::renderer;

	auto& settingManager = SettingManager::GetSingleton();

	const std::string texturePreviousApertureName = (settingManager.GetTextureSwap() & 1) ? "TextureApertureSwap" : "TextureAperture";
	const std::string textureApertureName = (settingManager.GetTextureSwap() & 1) ? "TextureAperture" : "TextureApertureSwap";

	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectTextureCache[texturePreviousApertureName].srv.Get());
	}

	ExecuteTechnique("Aperture", effectTextureCache[textureApertureName]);

	auto textureAperture = effect->GetVariableByName("TextureAperture")->AsShaderResource();
	if (textureAperture && textureAperture->IsValid()) {
		textureAperture->SetResource(effectTextureCache[textureApertureName].srv.Get());
	}

	ExecuteTechnique("ReadFocus", effectTextureCache["TextureReadFocus"]);

	const std::string texturePreviousFocusName = (settingManager.GetTextureSwap() & 1) ? "TextureFocusSwap" : "TextureFocus";
	const std::string textureFocusName = (settingManager.GetTextureSwap() & 1) ? "TextureFocus" : "TextureFocusSwap";

	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectTextureCache[texturePreviousFocusName].srv.Get());
	}

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureReadFocus"].srv.Get());
	}

	ExecuteTechnique("Focus", effectTextureCache[textureFocusName]);

	auto textureFocus = effect->GetVariableByName("TextureFocus")->AsShaderResource();
	if (textureFocus && textureFocus->IsValid()) {
		textureFocus->SetResource(effectTextureCache[textureFocusName].srv.Get());
	}

	auto textureMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	TextureManager::Texture textureOriginal2{};
	textureOriginal2.texture = textureMain.texture;
	textureOriginal2.srv = textureMain.SRV;
	textureOriginal2.rtv = textureMain.RTV;

	auto textureOriginal = effect->GetVariableByName("TextureOriginal")->AsShaderResource();
	if (textureOriginal && textureOriginal->IsValid()) {
		textureOriginal->SetResource(textureOriginal2.srv.Get());
	}

	auto& textureManager = TextureManager::GetSingleton();
	auto textureHDRTemp = textureManager.GetCommonTexture("TextureHDRTemp");

	globals::d3d::context->CopyResource(textureHDRTemp->texture.Get(), textureOriginal2.texture.Get());

	auto textureHDRTemp2 = textureManager.GetCommonTexture("TextureHDRTemp2");

	ExecuteTechniqueSequence(GetSelectedTechnique(), textureOriginal2, *textureHDRTemp, *textureHDRTemp2);

	globals::d3d::context->CopyResource(textureOriginal2.texture.Get(), textureHDRTemp->texture.Get());
}

void ENBDepthOfField::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	float4 dofParameters{};
	dofParameters.z = *globals::game::deltaTime / settingManager.GetValue<float>("ApertureTime", "DEPTHOFFIELD");
	dofParameters.w = *globals::game::deltaTime / settingManager.GetValue<float>("FocusingTime", "DEPTHOFFIELD");

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

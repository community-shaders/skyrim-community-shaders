#include "ENBEffectPostPass.h"

#include "../TextureManager.h"

void ENBEffectPostPass::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");
	auto scratchIt = effectTextureCache.find("TextureScratch");

	if (!textureSDRTemp || !textureSDRTemp2 || scratchIt == effectTextureCache.end() || !scratchIt->second.texture) {
		return;
	}

	auto& textureScratch = scratchIt->second;
	auto [executed, inOutput, inTemp] = ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.get(), *textureSDRTemp2, textureScratch);

	if (executed && inOutput) {
		textureManager.SwapTextures("TextureSDRTemp", "TextureSDRTemp2");
	} else if (executed && inTemp) {
		globals::d3d::context->CopyResource(textureSDRTemp->texture.get(), textureScratch.texture.get());
	}
}

void ENBEffectPostPass::UpdateEffectVariables()
{
	auto* textureSDRTemp = GetCachedCommonTexture("TextureSDRTemp");
	SetShaderResourceVariable("TextureOriginal", textureSDRTemp ? textureSDRTemp->srv.get() : nullptr);
}

void ENBEffectPostPass::CreateEffectTextures()
{
	// Match TextureSDRTemp exactly, since Execute copies the scratch result into it with CopyResource
	auto* textureSDRTemp = TextureManager::GetSingleton().GetCommonTexture("TextureSDRTemp");
	if (!textureSDRTemp || !textureSDRTemp->texture)
		return;

	D3D11_TEXTURE2D_DESC desc;
	textureSDRTemp->texture->GetDesc(&desc);
	effectTextureCache["TextureScratch"] = CreateTexture(desc.Width, desc.Height, desc.Format, "ENBEffectPostPass::TextureScratch");
}

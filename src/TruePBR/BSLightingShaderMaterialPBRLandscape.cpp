#include "BSLightingShaderMaterialPBRLandscape.h"

BSLightingShaderMaterialPBRLandscape::BSLightingShaderMaterialPBRLandscape()
{
	std::fill(isPbr.begin(), isPbr.end(), false);
	std::fill(roughnessScales.begin(), roughnessScales.end(), 1.f);
	std::fill(displacementScales.begin(), displacementScales.end(), 1.f);
	std::fill(specularLevels.begin(), specularLevels.end(), 0.04f);
}

BSLightingShaderMaterialPBRLandscape::~BSLightingShaderMaterialPBRLandscape()
{
	All.erase(this);
}

BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::Make()
{
	return new BSLightingShaderMaterialPBRLandscape;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::Create()
{
	return Make();
}

void BSLightingShaderMaterialPBRLandscape::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBRLandscape*>(that);

	pbrThat->numLandscapeTextures = numLandscapeTextures;

	for (uint32_t textureIndex = 0; textureIndex < NumTiles; ++textureIndex) {
		pbrThat->landscapeBaseColorTextures[textureIndex] = landscapeBaseColorTextures[textureIndex];
		pbrThat->landscapeNormalTextures[textureIndex] = landscapeNormalTextures[textureIndex];
		pbrThat->landscapeDisplacementTextures[textureIndex] = landscapeDisplacementTextures[textureIndex];
		pbrThat->landscapeRMAOSTextures[textureIndex] = landscapeRMAOSTextures[textureIndex];
	}
	pbrThat->terrainOverlayTexture = terrainOverlayTexture;
	pbrThat->terrainNoiseTexture = terrainNoiseTexture;
	pbrThat->landBlendParams = landBlendParams;
	pbrThat->isPbr = isPbr;
	pbrThat->roughnessScales = roughnessScales;
	pbrThat->displacementScales = displacementScales;
	pbrThat->specularLevels = specularLevels;
	pbrThat->terrainTexOffsetX = terrainTexOffsetX;
	pbrThat->terrainTexOffsetY = terrainTexOffsetY;
	pbrThat->terrainTexFade = terrainTexFade;
	pbrThat->glintParameters = glintParameters;

	All[this] = All[pbrThat];
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBRLandscape::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
	//return FEATURE;
}

void BSLightingShaderMaterialPBRLandscape::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	for (auto& texture : landscapeBaseColorTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeNormalTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeDisplacementTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeRMAOSTextures) {
		texture.reset();
	}
	terrainOverlayTexture.reset();
	terrainNoiseTexture.reset();
}

void BSLightingShaderMaterialPBRLandscape::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();
	if (terrainOverlayTexture == nullptr) {
		terrainOverlayTexture = stateData.defaultTextureNormalMap;
	}
	if (terrainNoiseTexture == nullptr) {
		terrainNoiseTexture = stateData.defaultTextureNormalMap;
	}
	for (uint32_t textureIndex = 0; textureIndex < numLandscapeTextures; ++textureIndex) {
		if (landscapeBaseColorTextures[textureIndex] == nullptr) {
			landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeNormalTextures[textureIndex] == nullptr) {
			landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
		}
		if (landscapeDisplacementTextures[textureIndex] == nullptr) {
			landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeRMAOSTextures[textureIndex] == nullptr) {
			landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
		}
	}
}

uint32_t BSLightingShaderMaterialPBRLandscape::GetTextures(RE::NiSourceTexture** textures)
{
	uint32_t textureIndex = 0;
	if (rimSoftLightingTexture != nullptr) {
		textures[textureIndex++] = rimSoftLightingTexture.get();
	}
	if (specularBackLightingTexture != nullptr) {
		textures[textureIndex++] = specularBackLightingTexture.get();
	}
	for (uint32_t tileIndex = 0; tileIndex < numLandscapeTextures; ++tileIndex) {
		if (landscapeBaseColorTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeBaseColorTextures[tileIndex].get();
		}
		if (landscapeNormalTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeNormalTextures[tileIndex].get();
		}
		if (landscapeDisplacementTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeDisplacementTextures[tileIndex].get();
		}
		if (landscapeRMAOSTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeRMAOSTextures[tileIndex].get();
		}
	}
	if (terrainOverlayTexture != nullptr) {
		textures[textureIndex++] = terrainOverlayTexture.get();
	}
	if (terrainNoiseTexture != nullptr) {
		textures[textureIndex++] = terrainNoiseTexture.get();
	}

	return textureIndex;
}

bool BSLightingShaderMaterialPBRLandscape::HasGlint() const
{
	for (uint32_t textureIndex = 0; textureIndex < numLandscapeTextures; ++textureIndex) {
		if (glintParameters[textureIndex].enabled) {
			return true;
		}
	}
	return false;
}
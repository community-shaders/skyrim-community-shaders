#include "BSLightingShaderMaterialPBRLandscape.h"

#include "PBRMaterialUtil.h"

BSLightingShaderMaterialPBRLandscape::BSLightingShaderMaterialPBRLandscape()
{
	unk30 = 0;
	unk34 = 0;
	std::fill(isPbr.begin(), isPbr.end(), false);
	std::fill(roughnessScales.begin(), roughnessScales.end(), 1.f);
	std::fill(displacementScales.begin(), displacementScales.end(), 1.f);
	std::fill(specularLevels.begin(), specularLevels.end(), 0.04f);
}

BSLightingShaderMaterialPBRLandscape::~BSLightingShaderMaterialPBRLandscape()
{
	const std::lock_guard lock(AllLock);
	All.erase(this);
}

BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::Make()
{
	auto* scrapHeap = globals::game::memoryManager->GetThreadScrapHeap();
	auto* material = static_cast<BSLightingShaderMaterialPBRLandscape*>(scrapHeap->Allocate(sizeof(BSLightingShaderMaterialPBRLandscape), 8));
	if (material) {
		std::memset(material, 0, sizeof(BSLightingShaderMaterialPBRLandscape));
		std::construct_at(material);
	}
	return material;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::Create()
{
	// Must use regular heap (not scrap heap like Make()). BSLightingShaderProperty::LinkObject
	// calls ScrapHeap::Free() after LinkMaterial — if Create() used scrap heap, it would pop
	// the canonical off the stack, causing immediate use-after-free in property->material.
	return new BSLightingShaderMaterialPBRLandscape();
}

void BSLightingShaderMaterialPBRLandscape::CopyMembers(RE::BSShaderMaterial* that)
{
	assert(IsPBR(that));
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBRLandscape*>(that);
	diffuseRenderTargetSourceIndex = pbrThat->diffuseRenderTargetSourceIndex;

	numLandscapeTextures = pbrThat->numLandscapeTextures;

	for (uint32_t textureIndex = 0; textureIndex < NumTiles; ++textureIndex) {
		landscapeBaseColorTextures[textureIndex] = pbrThat->landscapeBaseColorTextures[textureIndex];
		landscapeNormalTextures[textureIndex] = pbrThat->landscapeNormalTextures[textureIndex];
		landscapeDisplacementTextures[textureIndex] = pbrThat->landscapeDisplacementTextures[textureIndex];
		landscapeRMAOSTextures[textureIndex] = pbrThat->landscapeRMAOSTextures[textureIndex];
	}
	terrainOverlayTexture = pbrThat->terrainOverlayTexture;
	terrainNoiseTexture = pbrThat->terrainNoiseTexture;
	landBlendParams = pbrThat->landBlendParams;
	isPbr = pbrThat->isPbr;
	roughnessScales = pbrThat->roughnessScales;
	displacementScales = pbrThat->displacementScales;
	specularLevels = pbrThat->specularLevels;
	terrainTexOffsetX = pbrThat->terrainTexOffsetX;
	terrainTexOffsetY = pbrThat->terrainTexOffsetY;
	terrainTexFade = pbrThat->terrainTexFade;
	glintParameters = pbrThat->glintParameters;

	const std::lock_guard lock(AllLock);
	if (const auto it = All.find(pbrThat); it != All.end()) {
		All[this] = it->second;
	} else {
		All.erase(this);
	}
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBRLandscape::GetFeature() const
{
	return FEATURE;
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

bool BSLightingShaderMaterialPBRLandscape::IsPBR(const RE::BSShaderMaterial* material)
{
	return material && material->GetType() == Type::kLighting && material->GetFeature() == FEATURE;
}

std::vector<uint8_t> BSLightingShaderMaterialPBRLandscape::MaterialKey() const
{
	using namespace PBRMaterialUtil;
	auto key = BaseKey(*this);
	Append(key, numLandscapeTextures);
	for (uint32_t i = 0; i < NumTiles; ++i) {
		Append(key, landscapeBaseColorTextures[i]);
		Append(key, landscapeNormalTextures[i]);
		Append(key, landscapeDisplacementTextures[i]);
		Append(key, landscapeRMAOSTextures[i]);
		Append(key, isPbr[i]);
		Append(key, roughnessScales[i]);
		Append(key, displacementScales[i]);
		Append(key, specularLevels[i]);
		Append(key, glintParameters[i]);
	}
	Append(key, terrainOverlayTexture);
	Append(key, terrainNoiseTexture);
	Append(key, landBlendParams.red);
	Append(key, landBlendParams.green);
	Append(key, landBlendParams.blue);
	Append(key, landBlendParams.alpha);
	Append(key, terrainTexOffsetX);
	Append(key, terrainTexOffsetY);
	Append(key, terrainTexFade);
	return key;
}

bool BSLightingShaderMaterialPBRLandscape::DoIsCopy(RE::BSShaderMaterial* that)
{
	return IsPBR(that) && MaterialKey() == static_cast<BSLightingShaderMaterialPBRLandscape*>(that)->MaterialKey();
}

uint32_t BSLightingShaderMaterialPBRLandscape::ComputeCRC32(uint32_t srcHash)
{
	return PBRMaterialUtil::Hash(MaterialKey(), srcHash);
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::GetDefault()
{
	static BSLightingShaderMaterialPBRLandscape material;
	return &material;
}

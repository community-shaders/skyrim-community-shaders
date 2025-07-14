#include "BSLightingShaderMaterialPBR.h"

#include "TruePBR.h"

BSLightingShaderMaterialPBR::~BSLightingShaderMaterialPBR()
{
	All.erase(this);
}

BSLightingShaderMaterialPBR* BSLightingShaderMaterialPBR::Make()
{
	return new BSLightingShaderMaterialPBR;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBR::Create()
{
	return Make();
}

void BSLightingShaderMaterialPBR::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBR*>(that);

	loadedWithFeature = pbrThat->loadedWithFeature;
	pbrFlags = pbrThat->pbrFlags;
	coatRoughness = pbrThat->coatRoughness;
	coatSpecularLevel = pbrThat->coatSpecularLevel;
	fuzzColor = pbrThat->fuzzColor;
	fuzzWeight = pbrThat->fuzzWeight;
	glintParameters = pbrThat->glintParameters;
	projectedMaterialBaseColorScale = pbrThat->projectedMaterialBaseColorScale;
	projectedMaterialRoughness = pbrThat->projectedMaterialRoughness;
	projectedMaterialSpecularLevel = pbrThat->projectedMaterialSpecularLevel;
	projectedMaterialGlintParameters = pbrThat->projectedMaterialGlintParameters;

	rmaosTexture = pbrThat->rmaosTexture;
	emissiveTexture = pbrThat->emissiveTexture;
	displacementTexture = pbrThat->displacementTexture;
	featuresTexture0 = pbrThat->featuresTexture0;
	featuresTexture1 = pbrThat->featuresTexture1;

	All[this] = All[pbrThat];
}

std::uint32_t BSLightingShaderMaterialPBR::ComputeCRC32(uint32_t srcHash)
{
	struct HashContainer
	{
		uint32_t pbrFlags = 0;
		float coatRoughness = 0.f;
		float coatSpecularLevel = 0.f;
		std::array<float, 3> fuzzColor = { 0.f, 0.f, 0.f };
		float fuzzWeight = 0.f;
		float screenSpaceScale = 0.f;
		float logMicrofacetDensity = 0.f;
		float microfacetRoughness = 0.f;
		float densityRandomization = 0.f;
		std::array<float, 3> projectedMaterialBaseColorScale = { 0.f, 0.f, 0.f };
		float projectedMaterialRoughness = 0.f;
		float projectedMaterialSpecularLevel = 0.f;
		float projectedMaterialScreenSpaceScale = 0.f;
		float projectedMaterialLogMicrofacetDensity = 0.f;
		float projectedMaterialMicrofacetRoughness = 0.f;
		float projectedMaterialDensityRandomization = 0.f;
		uint32_t rmaodHash = 0;
		uint32_t emissiveHash = 0;
		uint32_t displacementHash = 0;
		uint32_t features0Hash = 0;
		uint32_t features1Hash = 0;
		uint32_t baseHash = 0;
	} hashes;

	hashes.pbrFlags = pbrFlags.underlying();
	hashes.coatRoughness = coatRoughness * 100.f;
	hashes.coatSpecularLevel = coatSpecularLevel * 100.f;
	hashes.fuzzColor[0] = fuzzColor[0] * 100.f;
	hashes.fuzzColor[1] = fuzzColor[1] * 100.f;
	hashes.fuzzColor[2] = fuzzColor[2] * 100.f;
	hashes.fuzzWeight = fuzzWeight * 100.f;
	hashes.screenSpaceScale = glintParameters.screenSpaceScale * 100.f;
	hashes.logMicrofacetDensity = glintParameters.logMicrofacetDensity * 100.f;
	hashes.microfacetRoughness = glintParameters.microfacetRoughness * 100.f;
	hashes.densityRandomization = glintParameters.densityRandomization * 100.f;
	hashes.projectedMaterialBaseColorScale[0] = projectedMaterialBaseColorScale[0] * 100.f;
	hashes.projectedMaterialBaseColorScale[1] = projectedMaterialBaseColorScale[1] * 100.f;
	hashes.projectedMaterialBaseColorScale[2] = projectedMaterialBaseColorScale[2] * 100.f;
	hashes.projectedMaterialRoughness = projectedMaterialRoughness * 100.f;
	hashes.projectedMaterialSpecularLevel = projectedMaterialSpecularLevel * 100.f;
	hashes.projectedMaterialScreenSpaceScale = projectedMaterialGlintParameters.screenSpaceScale * 100.f;
	hashes.projectedMaterialLogMicrofacetDensity = projectedMaterialGlintParameters.logMicrofacetDensity * 100.f;
	hashes.projectedMaterialMicrofacetRoughness = projectedMaterialGlintParameters.microfacetRoughness * 100.f;
	hashes.projectedMaterialDensityRandomization = projectedMaterialGlintParameters.densityRandomization * 100.f;
	if (textureSet != nullptr) {
		hashes.rmaodHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(RmaosTexture));
		hashes.emissiveHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(EmissiveTexture));
		hashes.displacementHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(DisplacementTexture));
		hashes.features0Hash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(FeaturesTexture0));
		hashes.features1Hash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(FeaturesTexture1));
	}

	hashes.baseHash = BSLightingShaderMaterialBase::ComputeCRC32(srcHash);

	return RE::detail::GenerateCRC32({ reinterpret_cast<const std::uint8_t*>(&hashes), sizeof(HashContainer) });
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBR::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kDefault;
	//return FEATURE;
}

void BSLightingShaderMaterialPBR::ApplyTextureSetData(const TruePBR::PBRTextureSetData& textureSetData)
{
	specularColorScale = textureSetData.roughnessScale;
	specularPower = textureSetData.specularLevel;
	rimLightPower = textureSetData.displacementScale;

	if (pbrFlags.any(PBRFlags::TwoLayer)) {
		specularColor = textureSetData.coatColor;
		subSurfaceLightRolloff = textureSetData.coatStrength;
		coatRoughness = textureSetData.coatRoughness;
		coatSpecularLevel = textureSetData.coatSpecularLevel;
	} else {
		if (pbrFlags.any(PBRFlags::Subsurface)) {
			specularColor = textureSetData.subsurfaceColor;
			subSurfaceLightRolloff = textureSetData.subsurfaceOpacity;
		}

		if (pbrFlags.any(PBRFlags::Fuzz)) {
			fuzzColor = textureSetData.fuzzColor;
			fuzzWeight = textureSetData.fuzzWeight;
		}
	}

	glintParameters = textureSetData.glintParameters;
}

void BSLightingShaderMaterialPBR::ApplyMaterialObjectData(const TruePBR::PBRMaterialObjectData& materialObjectData)
{
	projectedMaterialBaseColorScale = materialObjectData.baseColorScale;
	projectedMaterialRoughness = materialObjectData.roughness;
	projectedMaterialSpecularLevel = materialObjectData.specularLevel;
	projectedMaterialGlintParameters = materialObjectData.glintParameters;
}

void BSLightingShaderMaterialPBR::OnLoadTextureSet(std::uint64_t arg1, RE::BSTextureSet* inTextureSet)
{
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();

	if (diffuseTexture == nullptr || diffuseTexture == stateData.defaultTextureNormalMap) {
		BSLightingShaderMaterialBase::OnLoadTextureSet(arg1, inTextureSet);

		auto* lock = &unk98;
		while (_InterlockedCompareExchange(lock, 1, 0)) {
			Sleep(0);
		}
		_mm_mfence();

		if (inTextureSet != nullptr) {
			textureSet = RE::NiPointer(inTextureSet);
		}
		if (textureSet != nullptr) {
			textureSet->SetTexture(RmaosTexture, rmaosTexture);
			textureSet->SetTexture(EmissiveTexture, emissiveTexture);
			textureSet->SetTexture(DisplacementTexture, displacementTexture);
			textureSet->SetTexture(FeaturesTexture0, featuresTexture0);
			textureSet->SetTexture(FeaturesTexture1, featuresTexture1);

			auto* bgsTextureSet = globals::truePBR->currentTextureSet;
			if (bgsTextureSet == nullptr) {
				bgsTextureSet = skyrim_cast<RE::BGSTextureSet*>(inTextureSet);
			}
			if (bgsTextureSet) {
				if (auto* textureSetData = globals::truePBR->GetPBRTextureSetData(bgsTextureSet)) {
					ApplyTextureSetData(*textureSetData);
					All[this].textureSetData = textureSetData;
				}
			}
		}

		if (lock != nullptr) {
			*lock = 0;
			_mm_mfence();
		}
	}
}

void BSLightingShaderMaterialPBR::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	rmaosTexture.reset();
	emissiveTexture.reset();
	displacementTexture.reset();
	featuresTexture0.reset();
	featuresTexture1.reset();
}

void BSLightingShaderMaterialPBR::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();
	if (rmaosTexture == nullptr) {
		rmaosTexture = stateData.defaultTextureWhite;
	}
	if (emissiveTexture == nullptr) {
		emissiveTexture = stateData.defaultTextureBlack;
	}
	if (displacementTexture == nullptr) {
		displacementTexture = stateData.defaultTextureBlack;
	}
	if (featuresTexture0 == nullptr) {
		featuresTexture0 = stateData.defaultTextureWhite;
	}
	if (featuresTexture1 == nullptr) {
		featuresTexture1 = stateData.defaultTextureWhite;
	}
}

uint32_t BSLightingShaderMaterialPBR::GetTextures(RE::NiSourceTexture** textures)
{
	uint32_t textureIndex = 0;
	if (diffuseTexture != nullptr) {
		textures[textureIndex++] = diffuseTexture.get();
	}
	if (normalTexture != nullptr) {
		textures[textureIndex++] = normalTexture.get();
	}
	if (rimSoftLightingTexture != nullptr) {
		textures[textureIndex++] = rimSoftLightingTexture.get();
	}
	if (specularBackLightingTexture != nullptr) {
		textures[textureIndex++] = specularBackLightingTexture.get();
	}
	if (rmaosTexture != nullptr) {
		textures[textureIndex++] = rmaosTexture.get();
	}
	if (emissiveTexture != nullptr) {
		textures[textureIndex++] = emissiveTexture.get();
	}
	if (displacementTexture != nullptr) {
		textures[textureIndex++] = displacementTexture.get();
	}
	if (featuresTexture0 != nullptr) {
		textures[textureIndex++] = featuresTexture0.get();
	}
	if (featuresTexture1 != nullptr) {
		textures[textureIndex++] = featuresTexture1.get();
	}

	return textureIndex;
}

void BSLightingShaderMaterialPBR::LoadBinary(RE::NiStream& stream)
{
	BSLightingShaderMaterialBase::LoadBinary(stream);

	if (loadedWithFeature == RE::BSLightingShaderMaterial::Feature::kMultilayerParallax) {
		std::array<float, 4> parameters;
		stream.iStr->read(parameters.data(), 4);

		coatRoughness = parameters[0];
		coatSpecularLevel = parameters[1];

		fuzzColor = { parameters[0],
			parameters[1],
			parameters[2] };
		fuzzWeight = parameters[3];

		glintParameters.screenSpaceScale = parameters[0];
		glintParameters.logMicrofacetDensity = parameters[1];
		glintParameters.microfacetRoughness = parameters[2];
		glintParameters.densityRandomization = parameters[3];

		if (stream.header.version > 0x4A) {
			float dummy;
			stream.iStr->read(&dummy, 1);
		}
	}
}

float BSLightingShaderMaterialPBR::GetRoughnessScale() const
{
	return specularColorScale;
}

float BSLightingShaderMaterialPBR::GetSpecularLevel() const
{
	return specularPower;
}

float BSLightingShaderMaterialPBR::GetDisplacementScale() const
{
	return rimLightPower;
}

const RE::NiColor& BSLightingShaderMaterialPBR::GetSubsurfaceColor() const
{
	return specularColor;
}

float BSLightingShaderMaterialPBR::GetSubsurfaceOpacity() const
{
	return subSurfaceLightRolloff;
}

const RE::NiColor& BSLightingShaderMaterialPBR::GetCoatColor() const
{
	return specularColor;
}

float BSLightingShaderMaterialPBR::GetCoatStrength() const
{
	return subSurfaceLightRolloff;
}

float BSLightingShaderMaterialPBR::GetCoatRoughness() const
{
	return coatRoughness;
}

float BSLightingShaderMaterialPBR::GetCoatSpecularLevel() const
{
	return coatSpecularLevel;
}

const std::array<float, 3>& BSLightingShaderMaterialPBR::GetProjectedMaterialBaseColorScale() const
{
	return projectedMaterialBaseColorScale;
}

float BSLightingShaderMaterialPBR::GetProjectedMaterialRoughness() const
{
	return projectedMaterialRoughness;
}

float BSLightingShaderMaterialPBR::GetProjectedMaterialSpecularLevel() const
{
	return projectedMaterialSpecularLevel;
}

const GlintParameters& BSLightingShaderMaterialPBR::GetProjectedMaterialGlintParameters() const
{
	return projectedMaterialGlintParameters;
}

const RE::NiColor& BSLightingShaderMaterialPBR::GetFuzzColor() const
{
	return fuzzColor;
}

float BSLightingShaderMaterialPBR::GetFuzzWeight() const
{
	return fuzzWeight;
}

const GlintParameters& BSLightingShaderMaterialPBR::GetGlintParameters() const
{
	return glintParameters;
}
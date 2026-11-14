#include "BSLightingShaderMaterialPBR.h"

#include "PBRMaterialUtil.h"

std::array<float, PBRParameters::ValueCount> PBRParameters::Values() const
{
	return { roughnessScale, specularLevel, displacementScale,
		subsurfaceColor.red, subsurfaceColor.green, subsurfaceColor.blue, subsurfaceOpacity,
		coatColor.red, coatColor.green, coatColor.blue, coatStrength, coatRoughness, coatSpecularLevel,
		fuzzColor.red, fuzzColor.green, fuzzColor.blue, fuzzWeight,
		glintScreenSpaceScale, glintLogMicrofacetDensity, glintMicrofacetRoughness, glintDensityRandomization,
		subsurfaceBalance, thinSurfaceParameters[0], thinSurfaceParameters[1], thinSurfaceParameters[2] };
}

void PBRParameters::SetValues(const std::array<float, ValueCount>& values)
{
	roughnessScale = values[0];
	specularLevel = values[1];
	displacementScale = values[2];
	subsurfaceColor = { values[3], values[4], values[5] };
	subsurfaceOpacity = values[6];
	coatColor = { values[7], values[8], values[9] };
	coatStrength = values[10];
	coatRoughness = values[11];
	coatSpecularLevel = values[12];
	fuzzColor = { values[13], values[14], values[15] };
	fuzzWeight = values[16];
	glintScreenSpaceScale = values[17];
	glintLogMicrofacetDensity = values[18];
	glintMicrofacetRoughness = values[19];
	glintDensityRandomization = values[20];
	subsurfaceBalance = values[21];
	thinSurfaceParameters = { values[22], values[23], values[24] };
}

bool PBRParameters::IsValid() const
{
	const auto values = Values();
	if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value) && value >= 0.f; })) {
		return false;
	}
	return specularLevel <= 1.f && subsurfaceOpacity <= 1.f &&
	       subsurfaceBalance <= 1.f && thinSurfaceParameters[0] <= 1.f && thinSurfaceParameters[1] <= 1.f &&
	       thinSurfaceParameters[2] <= 3.f &&
	       coatStrength <= 1.f && coatRoughness <= 1.f && coatSpecularLevel <= 1.f && fuzzWeight <= 1.f &&
	       glintScreenSpaceScale >= 1.f && glintLogMicrofacetDensity >= 1.f && glintLogMicrofacetDensity <= 40.f &&
	       glintMicrofacetRoughness >= .005f && glintMicrofacetRoughness <= .3f && glintDensityRandomization <= 5.f;
}

bool BSLightingShaderMaterialPBR::ValidFeatures(uint32_t features)
{
	const stl::enumeration<PBRFlags> flags{ static_cast<PBRFlags>(features) };
	if (features & ~0x3ffu) {
		return false;
	}
	if (flags.any(PBRFlags::HairMarschner)) {
		return features == static_cast<uint32_t>(PBRFlags::HairMarschner);
	}
	if (flags.any(PBRFlags::ColoredCoat, PBRFlags::InterlayerParallax, PBRFlags::CoatNormal) && !flags.any(PBRFlags::TwoLayer)) {
		return false;
	}
	if (flags.any(PBRFlags::TwoLayer) && flags.any(PBRFlags::Subsurface, PBRFlags::Fuzz, PBRFlags::Glint, PBRFlags::Foliage, PBRFlags::ThinSurface)) {
		return false;
	}
	return !(flags.any(PBRFlags::Fuzz) && flags.any(PBRFlags::Glint));
}

BSLightingShaderMaterialPBR::BSLightingShaderMaterialPBR()
{
	unk30 = 0;
	unk34 = 0;
}

BSLightingShaderMaterialPBR::~BSLightingShaderMaterialPBR()
{
	const std::lock_guard lock(AllLock);
	All.erase(this);
}

bool BSLightingShaderMaterialPBR::IsPBR(const RE::BSShaderMaterial* material)
{
	return material && material->GetType() == Type::kLighting && material->GetFeature() == FEATURE;
}

BSLightingShaderMaterialPBR* BSLightingShaderMaterialPBR::Make()
{
	auto* heap = globals::game::memoryManager->GetThreadScrapHeap();
	auto* material = static_cast<BSLightingShaderMaterialPBR*>(heap->Allocate(sizeof(BSLightingShaderMaterialPBR), 8));
	return material ? std::construct_at(material) : nullptr;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBR::Create()
{
	return new BSLightingShaderMaterialPBR();
}

void BSLightingShaderMaterialPBR::CopyMembers(RE::BSShaderMaterial* that)
{
	assert(IsPBR(that));
	BSLightingShaderMaterialBase::CopyMembers(that);
	const auto* source = static_cast<BSLightingShaderMaterialPBR*>(that);
	diffuseRenderTargetSourceIndex = source->diffuseRenderTargetSourceIndex;
	pbrFlags = source->pbrFlags;
	nifThinSurfaceFlags = source->nifThinSurfaceFlags;
	nifThinSurfaceParameters = source->nifThinSurfaceParameters;
	nifFuzzParameters = source->nifFuzzParameters;
	fuzzRoughness = source->fuzzRoughness;
	fuzzRoughnessTexture = source->fuzzRoughnessTexture;
	parameters = source->parameters;
	rmaosTexture = source->rmaosTexture;
	emissiveTexture = source->emissiveTexture;
	displacementTexture = source->displacementTexture;
	featuresTexture0 = source->featuresTexture0;
	featuresTexture1 = source->featuresTexture1;
	projectedMaterialBaseColorScale = source->projectedMaterialBaseColorScale;
	projectedMaterialRoughness = source->projectedMaterialRoughness;
	projectedMaterialSpecularLevel = source->projectedMaterialSpecularLevel;
	projectedMaterialGlintParameters = source->projectedMaterialGlintParameters;
	inputFilePath = source->inputFilePath;
	valid = source->valid;
	const std::lock_guard lock(AllLock);
	if (const auto it = All.find(static_cast<BSLightingShaderMaterialPBR*>(that)); it != All.end()) {
		All[this] = it->second;
	} else {
		All.erase(this);
	}
}

std::vector<uint8_t> BSLightingShaderMaterialPBR::MaterialKey() const
{
	using namespace PBRMaterialUtil;
	auto key = BaseKey(*this);
	Append(key, pbrFlags.underlying());
	Append(key, nifThinSurfaceFlags.underlying());
	Append(key, nifThinSurfaceParameters.has_value());
	Append(key, nifFuzzParameters.has_value());
	Append(key, GetFuzzRoughness());
	Append(key, fuzzRoughnessTexture);
	for (const auto value : parameters.Values()) {
		Append(key, value);
	}
	Append(key, rmaosTexture);
	Append(key, emissiveTexture);
	Append(key, displacementTexture);
	Append(key, featuresTexture0);
	Append(key, featuresTexture1);
	for (const auto value : projectedMaterialBaseColorScale) {
		Append(key, value);
	}
	Append(key, projectedMaterialRoughness);
	Append(key, projectedMaterialSpecularLevel);
	Append(key, projectedMaterialGlintParameters);
	Append(key, valid);
	return key;
}

bool BSLightingShaderMaterialPBR::DoIsCopy(RE::BSShaderMaterial* that)
{
	return IsPBR(that) && MaterialKey() == static_cast<BSLightingShaderMaterialPBR*>(that)->MaterialKey();
}

uint32_t BSLightingShaderMaterialPBR::ComputeCRC32(uint32_t srcHash)
{
	return PBRMaterialUtil::Hash(MaterialKey(), srcHash);
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBR::GetDefault()
{
	static BSLightingShaderMaterialPBR material;
	return &material;
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBR::GetFeature() const
{
	return FEATURE;
}

void BSLightingShaderMaterialPBR::ApplyTextureSetData(const TruePBR::PBRTextureSetData& data)
{
	auto candidate = parameters;
	candidate.roughnessScale = data.roughnessScale;
	candidate.specularLevel = data.specularLevel;
	candidate.displacementScale = data.displacementScale;
	candidate.subsurfaceColor = data.subsurfaceColor;
	candidate.subsurfaceOpacity = data.subsurfaceOpacity;
	candidate.subsurfaceBalance = data.subsurfaceBalance;
	candidate.thinSurfaceParameters = { data.thinSurfaceSpecularWeight,
		data.thinSurfaceBackSpecularWeight, data.thinSurfaceBackRoughnessScale };
	candidate.coatColor = data.coatColor;
	candidate.coatStrength = data.coatStrength;
	candidate.coatRoughness = data.coatRoughness;
	candidate.coatSpecularLevel = data.coatSpecularLevel;
	candidate.fuzzColor = data.fuzzColor;
	candidate.fuzzWeight = data.fuzzWeight;
	candidate.glintScreenSpaceScale = data.glintParameters.screenSpaceScale;
	candidate.glintLogMicrofacetDensity = data.glintParameters.logMicrofacetDensity;
	candidate.glintMicrofacetRoughness = data.glintParameters.microfacetRoughness;
	candidate.glintDensityRandomization = data.glintParameters.densityRandomization;
	auto flags = pbrFlags;
	flags.reset(PBRFlags::Glint, PBRFlags::ThinSurface, PBRFlags::Foliage);
	flags.set(nifThinSurfaceFlags.get());
	if (data.thinSurface) {
		flags.set(PBRFlags::ThinSurface);
	}
	if (data.foliage) {
		flags.set(PBRFlags::Foliage);
	}
	if (data.glintParameters.enabled) {
		flags.set(PBRFlags::Glint);
	}
	if (!candidate.IsValid() || !ValidFeatures(flags.underlying())) {
		logger::error("[TruePBR] rejected invalid TXST parameters for {}", inputFilePath);
		return;
	}
	parameters = candidate;
	pbrFlags = flags;
	fuzzRoughness = data.fuzzRoughness;
	fuzzRoughnessTexture = data.fuzzRoughnessTexture;
	if (nifThinSurfaceParameters) {
		ApplyNifThinSurfaceParameters(*nifThinSurfaceParameters);
	}

	if (nifFuzzParameters) {
		ApplyNifFuzzParameters(*nifFuzzParameters);
	}
}

bool BSLightingShaderMaterialPBR::ApplyNifFuzzParameters(std::span<const float> values)
{
	if (values.size() != 2 || std::ranges::any_of(values, [](float value) { return !std::isfinite(value); })) {
		return false;
	}

	const float roughness = values[0];
	const float useRoughnessTexture = values[1];
	if (useRoughnessTexture != 0.f && useRoughnessTexture != 1.f) {
		return false;
	}

	fuzzRoughness = roughness < 0.f ? -1.f : std::min(roughness, 1.f);
	fuzzRoughnessTexture = useRoughnessTexture == 1.f;
	nifFuzzParameters = std::array{ fuzzRoughness, useRoughnessTexture };

	return true;
}

bool BSLightingShaderMaterialPBR::ApplyNifThinSurfaceParameters(std::span<const float> values)
{
	if (values.size() != 4 || std::ranges::any_of(values, [](float value) { return !std::isfinite(value); })) {
		return false;
	}

	auto candidate = parameters;
	candidate.subsurfaceBalance = std::clamp(values[0], 0.f, 1.f);
	candidate.thinSurfaceParameters = { std::clamp(values[1], 0.f, 1.f),
		std::clamp(values[2], 0.f, 1.f), std::clamp(values[3], 0.f, 3.f) };
	if (!candidate.IsValid()) {
		return false;
	}
	parameters = candidate;
	nifThinSurfaceParameters = std::array{ candidate.subsurfaceBalance, candidate.thinSurfaceParameters[0],
		candidate.thinSurfaceParameters[1], candidate.thinSurfaceParameters[2] };
	return true;
}

void BSLightingShaderMaterialPBR::ApplyMaterialObjectData(const TruePBR::PBRMaterialObjectData& data)
{
	PBRParameters candidate;
	candidate.roughnessScale = data.roughness;
	candidate.specularLevel = data.specularLevel;
	candidate.glintScreenSpaceScale = data.glintParameters.screenSpaceScale;
	candidate.glintLogMicrofacetDensity = data.glintParameters.logMicrofacetDensity;
	candidate.glintMicrofacetRoughness = data.glintParameters.microfacetRoughness;
	candidate.glintDensityRandomization = data.glintParameters.densityRandomization;
	if (!candidate.IsValid() || !std::ranges::all_of(data.baseColorScale, [](float value) { return std::isfinite(value) && value >= 0.f; })) {
		logger::error("[TruePBR] rejected invalid MATO parameters for {}", inputFilePath);
		return;
	}
	projectedMaterialBaseColorScale = data.baseColorScale;
	projectedMaterialRoughness = data.roughness;
	projectedMaterialSpecularLevel = data.specularLevel;
	projectedMaterialGlintParameters = data.glintParameters;
}

void BSLightingShaderMaterialPBR::ClearMaterialObjectData()
{
	projectedMaterialBaseColorScale = { 1.f, 1.f, 1.f };
	projectedMaterialRoughness = 1.f;
	projectedMaterialSpecularLevel = .04f;
	projectedMaterialGlintParameters = {};
}

bool BSLightingShaderMaterialPBR::ValidateTextureSet() const
{
	if (!PBRMaterialUtil::IsTextureSet(textureSet.get())) {
		return false;
	}
	const auto present = [this](RE::BSTextureSet::Texture slot) {
		const auto* path = textureSet->GetTexturePath(slot);
		return path && *path;
	};
	return present(PBRTextureIndex(PBRTextureSlot::BaseColor)) &&
	       present(PBRTextureIndex(PBRTextureSlot::Normal)) && present(RmaosTexture) &&
	       !present(PBRTextureIndex(PBRTextureSlot::Reserved4)) &&
	       !present(PBRTextureIndex(PBRTextureSlot::Reserved8)) &&
	       (!pbrFlags.any(PBRFlags::CoatNormal) || present(FeaturesTexture1)) &&
	       (!pbrFlags.any(PBRFlags::InterlayerParallax) || present(DisplacementTexture));
}

void BSLightingShaderMaterialPBR::OnLoadTextureSet(uint64_t, RE::BSTextureSet* inTextureSet)
{
	const PBRMaterialUtil::TextureLock textureLock(unk98);
	const auto& state = globals::game::graphicsState->GetRuntimeData();
	if (diffuseTexture && diffuseTexture != state.defaultTextureNormalMap && diffuseTexture != state.defaultTextureWhite) {
		return;
	}
	if (inTextureSet) {
		textureSet = RE::NiPointer(inTextureSet);
	}
	if (!valid || !ValidateTextureSet()) {
		valid = false;
		logger::error("[TruePBR] rejected invalid texture set for {}", inputFilePath);
	} else {
		textureSet->SetTexture(PBRTextureIndex(PBRTextureSlot::BaseColor), diffuseTexture);
		textureSet->SetTexture(PBRTextureIndex(PBRTextureSlot::Normal), normalTexture);
		textureSet->SetTexture(RmaosTexture, rmaosTexture);
		textureSet->SetTexture(EmissiveTexture, emissiveTexture);
		textureSet->SetTexture(DisplacementTexture, displacementTexture);
		textureSet->SetTexture(FeaturesTexture0, featuresTexture0);
		textureSet->SetTexture(FeaturesTexture1, featuresTexture1);
		const auto& truePBR = globals::features::truePBR;
		auto* bgsTextureSet = truePBR.currentShaderTextureSet.get() == textureSet.get() ? truePBR.currentTextureSet : nullptr;
		if (!bgsTextureSet) {
			bgsTextureSet = skyrim_cast<RE::BGSTextureSet*>(inTextureSet);
		}
		auto* data = bgsTextureSet ? globals::features::truePBR.GetPBRTextureSetData(bgsTextureSet) : nullptr;
		{
			const std::lock_guard lock(AllLock);
			All[this].textureSetData = data;
		}
		if (data) {
			ApplyTextureSetData(*data);
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

void BSLightingShaderMaterialPBR::ReceiveValuesFromRootMaterial(bool, bool, bool, bool, bool)
{
	const auto& state = globals::game::graphicsState->GetRuntimeData();
	if (!diffuseTexture) {
		diffuseTexture = state.defaultTextureWhite;
	}
	if (!normalTexture) {
		normalTexture = state.defaultTextureNormalMap;
	}
	if (!rmaosTexture) {
		rmaosTexture = state.defaultTextureWhite;
	}
	if (!emissiveTexture) {
		emissiveTexture = state.defaultTextureBlack;
	}
	if (!displacementTexture) {
		displacementTexture = state.defaultTextureBlack;
	}
	if (!featuresTexture0) {
		featuresTexture0 = state.defaultTextureWhite;
	}
	if (!featuresTexture1) {
		featuresTexture1 = state.defaultTextureWhite;
	}
}

uint32_t BSLightingShaderMaterialPBR::GetTextures(RE::NiSourceTexture** textures)
{
	uint32_t count = BSLightingShaderMaterialBase::GetTextures(textures);
	for (auto* texture : { rmaosTexture.get(), emissiveTexture.get(),
			 displacementTexture.get(), featuresTexture0.get(), featuresTexture1.get() }) {
		if (texture) {
			textures[count++] = texture;
		}
	}
	return count;
}

void BSLightingShaderMaterialPBR::LoadBinary(RE::NiStream& stream)
{
	inputFilePath = stream.inputFilePath;
	uint32_t version = 0;
	uint32_t features = 0;
	std::array<float, PBRParameters::ValueCount> values{};
	const bool read = stream.iStr->read(&textureClampMode, 1) &&
	                  stream.iStr->read(&materialAlpha, 1) &&
	                  stream.iStr->read(&refractionPower, 1) &&
	                  stream.iStr->read(&version, 1) &&
	                  stream.iStr->read(&features, 1) &&
	                  stream.iStr->read(values.data(), static_cast<uint32_t>(values.size()));
	parameters.SetValues(values);
	pbrFlags = static_cast<PBRFlags>(features);
	nifThinSurfaceFlags.reset();
	if (pbrFlags.any(PBRFlags::ThinSurface)) {
		nifThinSurfaceFlags.set(PBRFlags::ThinSurface);
	}
	if (pbrFlags.any(PBRFlags::Foliage)) {
		nifThinSurfaceFlags.set(PBRFlags::Foliage);
	}
	nifThinSurfaceParameters.reset();
	nifFuzzParameters.reset();
	fuzzRoughness = -1.f;
	fuzzRoughnessTexture = false;
	valid = read && version == Version && ValidFeatures(features) && parameters.IsValid() &&
	        textureClampMode >= 0 && textureClampMode <= 3 &&
	        std::isfinite(materialAlpha) && materialAlpha >= 0.f && materialAlpha <= 128.f &&
	        std::isfinite(refractionPower) && refractionPower >= 0.f && refractionPower <= 1.f;
	if (!valid) {
		logger::error("[TruePBR] rejected invalid material in {}", inputFilePath);
	}
}

void BSLightingShaderMaterialPBR::SaveBinary(RE::NiStream& stream)
{
	const auto values = parameters.Values();
	const auto features = pbrFlags.underlying();
	const bool written = stream.oStr->write(&textureClampMode, 1) &&
	                     stream.oStr->write(&materialAlpha, 1) &&
	                     stream.oStr->write(&refractionPower, 1) &&
	                     stream.oStr->write(&Version, 1) &&
	                     stream.oStr->write(&features, 1) &&
	                     stream.oStr->write(values.data(), static_cast<uint32_t>(values.size()));
	if (!written) {
		stream.lastError = 1;
		strcpy_s(stream.lastErrorMessage, "TruePBR material write failed");
	}
}

GlintParameters BSLightingShaderMaterialPBR::GetGlintParameters() const
{
	return { pbrFlags.any(PBRFlags::Glint), parameters.glintScreenSpaceScale, parameters.glintLogMicrofacetDensity,
		parameters.glintMicrofacetRoughness, parameters.glintDensityRandomization };
}

float BSLightingShaderMaterialPBR::GetFuzzRoughness() const
{
	return std::isfinite(fuzzRoughness) && fuzzRoughness >= 0.f ? std::min(fuzzRoughness, 1.f) : -1.f;
}

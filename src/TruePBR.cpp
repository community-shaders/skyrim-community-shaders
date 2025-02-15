#include "TruePBR.h"

#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "TruePBR/BSLightingShaderMaterialPBRLandscape.h"

#include "Hooks.h"
#include "ShaderCache.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GlintParameters,
	enabled,
	screenSpaceScale,
	logMicrofacetDensity,
	microfacetRoughness,
	densityRandomization);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TruePBR::PBRTextureSetData,
	roughnessScale,
	displacementScale,
	specularLevel,
	subsurfaceColor,
	subsurfaceOpacity,
	coatColor,
	coatStrength,
	coatRoughness,
	coatSpecularLevel,
	innerLayerDisplacementOffset,
	fuzzColor,
	fuzzWeight,
	glintParameters);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TruePBR::PBRMaterialObjectData,
	baseColorScale,
	roughness,
	specularLevel,
	glintParameters);

namespace PNState
{
	void ReadPBRRecordConfigs(const std::string& rootPath, std::function<void(const std::string&, const json&)> recordReader)
	{
		if (std::filesystem::exists(rootPath)) {
			auto configs = clib_util::distribution::get_configs(rootPath, "", ".json");

			if (configs.empty()) {
				logger::warn("[TruePBR] no .json files were found within the {} folder, aborting...", rootPath);
				return;
			}

			logger::info("[TruePBR] {} matching jsons found", configs.size());

			for (auto& path : configs) {
				logger::info("[TruePBR] loading json : {}", path);

				std::ifstream fileStream(path);
				if (!fileStream.is_open()) {
					logger::error("[TruePBR] failed to read {}", path);
					continue;
				}

				json config;
				try {
					fileStream >> config;
				} catch (const nlohmann::json::parse_error& e) {
					logger::error("[TruePBR] failed to parse {} : {}", path, e.what());
					continue;
				}

				const auto editorId = std::filesystem::path(path).stem().string();
				recordReader(editorId, config);
			}
		}
	}

	void SavePBRRecordConfig(const std::string& rootPath, const std::string& editorId, const json& config)
	{
		std::filesystem::create_directory(rootPath);

		const std::string outputPath = std::format("{}\\{}.json", rootPath, editorId);
		std::ofstream fileStream(outputPath);
		if (!fileStream.is_open()) {
			logger::error("[TruePBR] failed to write {}", outputPath);
			return;
		}
		try {
			fileStream << std::setw(4) << config;
		} catch (const nlohmann::json::type_error& e) {
			logger::error("[TruePBR] failed to serialize {} : {}", outputPath, e.what());
			return;
		}
	}
}

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const TruePBR::PBRTextureSetData& textureSetData, uint32_t textureIndex);

void TruePBR::DrawSettings()
{
	if (ImGui::CollapsingHeader("PBR", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		if (ImGui::TreeNodeEx("Texture Set Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginCombo("Texture Set", selectedPbrTextureSetName.c_str())) {
				for (auto& [textureSetName, textureSet] : pbrTextureSets) {
					if (ImGui::Selectable(textureSetName.c_str(), textureSetName == selectedPbrTextureSetName)) {
						selectedPbrTextureSetName = textureSetName;
						selectedPbrTextureSet = &textureSet;
					}
				}
				ImGui::EndCombo();
			}

			if (selectedPbrTextureSet != nullptr) {
				bool wasEdited = false;
				if (ImGui::SliderFloat("Displacement Scale", &selectedPbrTextureSet->displacementScale, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Roughness Scale", &selectedPbrTextureSet->roughnessScale, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Specular Level", &selectedPbrTextureSet->specularLevel, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::TreeNodeEx("Subsurface")) {
					if (ImGui::ColorPicker3("Subsurface Color", &selectedPbrTextureSet->subsurfaceColor.red)) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Subsurface Opacity", &selectedPbrTextureSet->subsurfaceOpacity, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}

					ImGui::TreePop();
				}
				if (ImGui::TreeNodeEx("Coat")) {
					if (ImGui::ColorPicker3("Coat Color", &selectedPbrTextureSet->coatColor.red)) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Strength", &selectedPbrTextureSet->coatStrength, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Roughness", &selectedPbrTextureSet->coatRoughness, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Specular Level", &selectedPbrTextureSet->coatSpecularLevel, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Inner Layer Displacement Offset", &selectedPbrTextureSet->innerLayerDisplacementOffset, 0.f, 3.f, "%.3f")) {
						wasEdited = true;
					}
					ImGui::TreePop();
				}
				if (ImGui::TreeNodeEx("Glint")) {
					if (ImGui::Checkbox("Enabled", &selectedPbrTextureSet->glintParameters.enabled)) {
						wasEdited = true;
					}
					if (selectedPbrTextureSet->glintParameters.enabled) {
						if (ImGui::SliderFloat("Screenspace Scale", &selectedPbrTextureSet->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Log Microfacet Density", &selectedPbrTextureSet->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Microfacet Roughness", &selectedPbrTextureSet->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Density Randomization", &selectedPbrTextureSet->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
							wasEdited = true;
						}
					}
					ImGui::TreePop();
				}
				if (wasEdited) {
					for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
						if (extensions.textureSetData == selectedPbrTextureSet) {
							material->ApplyTextureSetData(*extensions.textureSetData);
						}
					}
					for (auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
						for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
							if (textureSets[textureSetIndex] == selectedPbrTextureSet) {
								SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
							}
						}
					}
				}
				if (selectedPbrTextureSet != nullptr) {
					if (ImGui::Button("Save")) {
						PNState::SavePBRRecordConfig("Data\\PBRTextureSets", selectedPbrTextureSetName, *selectedPbrTextureSet);
					}
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Material Object Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginCombo("Material Object", selectedPbrMaterialObjectName.c_str())) {
				for (auto& [materialObjectName, materialObject] : pbrMaterialObjects) {
					if (ImGui::Selectable(materialObjectName.c_str(), materialObjectName == selectedPbrMaterialObjectName)) {
						selectedPbrMaterialObjectName = materialObjectName;
						selectedPbrMaterialObject = &materialObject;
					}
				}
				ImGui::EndCombo();
			}

			if (selectedPbrMaterialObject != nullptr) {
				bool wasEdited = false;
				if (ImGui::SliderFloat3("Base Color Scale", selectedPbrMaterialObject->baseColorScale.data(), 0.f, 10.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Roughness", &selectedPbrMaterialObject->roughness, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Specular Level", &selectedPbrMaterialObject->specularLevel, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::TreeNodeEx("Glint")) {
					if (ImGui::Checkbox("Enabled", &selectedPbrMaterialObject->glintParameters.enabled)) {
						wasEdited = true;
					}
					if (selectedPbrMaterialObject->glintParameters.enabled) {
						if (ImGui::SliderFloat("Screenspace Scale", &selectedPbrMaterialObject->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Log Microfacet Density", &selectedPbrMaterialObject->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Microfacet Roughness", &selectedPbrMaterialObject->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Density Randomization", &selectedPbrMaterialObject->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
							wasEdited = true;
						}
					}
					ImGui::TreePop();
				}
				if (wasEdited) {
					for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
						if (extensions.materialObjectData == selectedPbrMaterialObject) {
							material->ApplyMaterialObjectData(*extensions.materialObjectData);
						}
					}
				}
				if (selectedPbrMaterialObject != nullptr) {
					if (ImGui::Button("Save")) {
						PNState::SavePBRRecordConfig("Data\\PBRMaterialObjects", selectedPbrMaterialObjectName, *selectedPbrMaterialObject);
					}
				}
			}
			ImGui::TreePop();
		}
	}
}

void TruePBR::SetupResources()
{
	SetupTextureSetData();
	SetupMaterialObjectData();
}

void TruePBR::PrePass()
{
	auto context = globals::d3d::context;
	if (!glintsNoiseTexture)
		SetupGlintsTexture();
	ID3D11ShaderResourceView* srv = glintsNoiseTexture->srv.get();
	context->PSSetShaderResources(20, 1, &srv);
}

void TruePBR::SetupGlintsTexture()
{
	constexpr uint noiseTexSize = 64;

	D3D11_TEXTURE2D_DESC tex_desc{
		.Width = noiseTexSize,
		.Height = noiseTexSize,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
		.SampleDesc = { .Count = 1, .Quality = 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = 0
	};
	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
		.Format = tex_desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
		.Format = tex_desc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	glintsNoiseTexture = eastl::make_unique<Texture2D>(tex_desc);
	glintsNoiseTexture->CreateSRV(srv_desc);
	glintsNoiseTexture->CreateUAV(uav_desc);

	// Compile
	auto noiseGenProgram = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Common\\Glints\\noisegen.cs.hlsl", {}, "cs_5_0"));
	if (!noiseGenProgram) {
		logger::error("Failed to compile glints noise generation shader!");
		return;
	}

	// Generate the noise
	{
		auto context = globals::d3d::context;

		struct OldState
		{
			ID3D11ComputeShader* shader;
			ID3D11UnorderedAccessView* uav[1];
			ID3D11ClassInstance* instance;
			UINT numInstances;
		};

		OldState newer{}, old{};
		context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
		context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav);

		{
			newer.uav[0] = glintsNoiseTexture->uav.get();
			context->CSSetShader(noiseGenProgram, nullptr, 0);
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uav), newer.uav, nullptr);
			context->Dispatch((noiseTexSize + 31) >> 5, (noiseTexSize + 31) >> 5, 1);
		}

		context->CSSetShader(old.shader, &old.instance, old.numInstances);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav, nullptr);
	}

	noiseGenProgram->Release();
}

void TruePBR::SetupFrame()
{
	SetupDefaultPBRLandTextureSet();
}

void TruePBR::SetupTextureSetData()
{
	logger::info("[TruePBR] loading PBR texture set configs");

	pbrTextureSets.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			pbrTextureSets.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

void TruePBR::ReloadTextureSetData()
{
	logger::info("[TruePBR] reloading PBR texture set configs");

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			if (auto it = pbrTextureSets.find(editorId); it != pbrTextureSets.cend()) {
				it->second = config;
			}
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});

	for (const auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
		for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
			SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
		}
	}
}

TruePBR::PBRTextureSetData* TruePBR::GetPBRTextureSetData(const RE::TESForm* textureSet)
{
	if (textureSet == nullptr) {
		return nullptr;
	}

	auto it = pbrTextureSets.find(textureSet->GetFormEditorID());
	if (it == pbrTextureSets.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRTextureSet(const RE::TESForm* textureSet)
{
	return GetPBRTextureSetData(textureSet) != nullptr;
}

void TruePBR::SetupMaterialObjectData()
{
	logger::info("[TruePBR] loading PBR material object configs");

	pbrMaterialObjects.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRMaterialObjects", [this](const std::string& editorId, const json& config) {
		try {
			pbrMaterialObjects.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

TruePBR::PBRMaterialObjectData* TruePBR::GetPBRMaterialObjectData(const RE::TESForm* materialObject)
{
	if (materialObject == nullptr) {
		return nullptr;
	}

	auto it = pbrMaterialObjects.find(materialObject->GetFormEditorID());
	if (it == pbrMaterialObjects.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRMaterialObject(const RE::TESForm* materialObject)
{
	return GetPBRMaterialObjectData(materialObject) != nullptr;
}

namespace Permutations
{
	template <typename RangeType>
	std::unordered_set<uint32_t> GenerateFlagPermutations(const RangeType& flags, uint32_t constantFlags)
	{
		std::vector<uint32_t> flagValues;
		std::ranges::transform(flags, std::back_inserter(flagValues), [](auto flag) { return static_cast<uint32_t>(flag); });
		const uint32_t size = static_cast<uint32_t>(flagValues.size());

		std::unordered_set<uint32_t> result;
		for (uint32_t mask = 0; mask < (1u << size); ++mask) {
			uint32_t flag = constantFlags;
			for (size_t index = 0; index < size; ++index) {
				if (mask & (1 << index)) {
					flag |= flagValues[index];
				}
			}
			result.insert(flag);
		}

		return result;
	}

	uint32_t GetLightingShaderDescriptor(SIE::ShaderCache::LightingShaderTechniques technique, uint32_t flags)
	{
		return ((static_cast<uint32_t>(technique) & 0x3F) << 24) | flags;
	}

	void AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques technique, const std::unordered_set<uint32_t>& flags, std::unordered_set<uint32_t>& result)
	{
		for (uint32_t flag : flags) {
			result.insert(GetLightingShaderDescriptor(technique, flag));
		}
	}

	std::unordered_set<uint32_t> GeneratePBRLightingPixelPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array projectedUvFlags{ Deferred, AnisoLighting, DoAlphaTest, Snow };
		constexpr std::array lodObjectsFlags{ Deferred, WorldMap, DoAlphaTest, ProjectedUV };
		constexpr std::array treeFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array landFlags{ Deferred, AnisoLighting };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> lodObjectsFlagValues = GenerateFlagPermutations(lodObjectsFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = GenerateFlagPermutations(landFlags, defaultConstantFlags);

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjects, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjectHD, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}

	std::unordered_set<uint32_t> GeneratePBRGrassPixelPermutations()
	{
		using enum SIE::ShaderCache::GrassShaderTechniques;
		using enum SIE::ShaderCache::GrassShaderFlags;

		return { static_cast<uint32_t>(TruePbr),
			static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(AlphaTest) };
	}
}

void TruePBR::GenerateShaderPermutations(RE::BSShader* shader)
{
	auto state = globals::state;
	auto shaderCache = globals::shaderCache;
	if (shader->shaderType == RE::BSShader::Type::Lighting) {
		const auto pixelPermutations = Permutations::GeneratePBRLightingPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
		}
	} else if (shader->shaderType == RE::BSShader::Type::Grass) {
		const auto pixelPermutations = Permutations::GeneratePBRGrassPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
		}
	}
}

struct ExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 12;
	static constexpr uint32_t FirstPSTexture = 80;

	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, NumPSTextures> PSTexture;

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		ID3D11ShaderResourceView* resourceView = newTexture ? newTexture->resourceView : nullptr;
		//if (PSTexture[textureIndex] != resourceView)
		{
			PSTexture[textureIndex] = resourceView;
			PSResourceModifiedBits |= (1 << textureIndex);
		}
	}

	ExtendedRendererState()
	{
		std::fill(PSTexture.begin(), PSTexture.end(), nullptr);
	}
} extendedRendererState;

struct BSLightingShaderProperty_LoadBinary
{
	static void thunk(RE::BSLightingShaderProperty* property, RE::NiStream& stream)
	{
		using enum RE::BSShaderProperty::EShaderPropertyFlag;

		RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kDefault;
		stream.iStr->read(&feature, 1);

		{
			auto vtable = REL::Relocation<void***>(RE::NiShadeProperty::VTABLE[0]);
			auto baseMethod = reinterpret_cast<void (*)(RE::NiShadeProperty*, RE::NiStream&)>((vtable.get()[0x18]));
			baseMethod(property, stream);
		}

		stream.iStr->read(&property->flags, 1);

		bool isPbr = false;
		{
			RE::BSLightingShaderMaterialBase* material = nullptr;
			if (property->flags.any(kMenuScreen)) {
				auto* pbrMaterial = BSLightingShaderMaterialPBR::Make();
				pbrMaterial->loadedWithFeature = feature;
				material = pbrMaterial;
				isPbr = true;
			} else {
				material = RE::BSLightingShaderMaterialBase::CreateMaterial(feature);
			}
			property->LinkMaterial(nullptr, false);
			property->material = material;
		}

		{
			stream.iStr->read(&property->material->texCoordOffset[0].x, 1);
			stream.iStr->read(&property->material->texCoordOffset[0].y, 1);
			stream.iStr->read(&property->material->texCoordScale[0].x, 1);
			stream.iStr->read(&property->material->texCoordScale[0].y, 1);

			property->material->texCoordOffset[1] = property->material->texCoordOffset[0];
			property->material->texCoordScale[1] = property->material->texCoordScale[0];
		}

		stream.LoadLinkID();

		{
			RE::NiColor emissiveColor{};
			stream.iStr->read(&emissiveColor.red, 1);
			stream.iStr->read(&emissiveColor.green, 1);
			stream.iStr->read(&emissiveColor.blue, 1);

			if (property->emissiveColor != nullptr && property->flags.any(kOwnEmit)) {
				*property->emissiveColor = emissiveColor;
			}
		}

		stream.iStr->read(&property->emissiveMult, 1);

		static_cast<RE::BSLightingShaderMaterialBase*>(property->material)->LoadBinary(stream);

		if (isPbr) {
			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(property->material);
			if (property->flags.any(kMultiLayerParallax)) {
				pbrMaterial->pbrFlags.set(PBRFlags::TwoLayer);
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::InterlayerParallax);
				}
				if (property->flags.any(kBackLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::CoatNormal);
				}
				if (property->flags.any(kEffectLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::ColoredCoat);
				}
			} else if (property->flags.any(kBackLighting)) {
				pbrMaterial->pbrFlags.set(PBRFlags::HairMarschner);
			} else {
				if (property->flags.any(kRimLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Subsurface);
				}
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Fuzz);
				} else if (property->flags.any(kFitSlope)) {
					pbrMaterial->glintParameters.enabled = true;
				}
			}

			// It was a bad idea originally to use kMenuScreen flag to enable PBR since it's actually used for world map
			// meshes. But it was too late to move to use different flag so internally we use kVertexLighting instead
			// as it's not used for Lighting shader.
			property->flags.set(kVertexLighting);

			property->flags.reset(kMenuScreen, kSpecular, kGlowMap, kEnvMap, kMultiLayerParallax, kSoftLighting, kRimLighting, kBackLighting, kAnisotropicLighting, kEffectLighting, kFitSlope);
		} else {
			// There are some modded non-PBR meshes with kVertexLighting enabled.
			property->flags.reset(kVertexLighting);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
	{
		auto renderPasses = func(property, geometry, renderFlags, accumulator);
		if (renderPasses == nullptr) {
			return renderPasses;
		}

		bool isPbr = false;

		if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting) && (property->material->GetFeature() == RE::BSShaderMaterial::Feature::kDefault || property->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)) {
			isPbr = true;
		}

		auto currentPass = renderPasses->head;
		while (currentPass != nullptr) {
			if (currentPass->shader->shaderType == RE::BSShader::Type::Lighting) {
				constexpr uint32_t LightingTechniqueStart = 0x4800002D;
				auto lightingTechnique = currentPass->passEnum - LightingTechniqueStart;
				auto lightingFlags = lightingTechnique & ~(~0u << 24);
				auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((lightingTechnique >> 24) & 0x3F);
				lightingFlags &= ~0b111000u;
				if (isPbr) {
					lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr);
					lightingFlags &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Specular);
					if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
						auto* material = static_cast<BSLightingShaderMaterialPBRLandscape*>(property->material);
						if (material->HasGlint()) {
							lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
						}
					} else {
						auto* material = static_cast<BSLightingShaderMaterialPBR*>(property->material);
						if (material->glintParameters.enabled || (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kProjectedUV) && material->projectedMaterialGlintParameters.enabled)) {
							lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
						}
					}
				}
				lightingTechnique = (static_cast<uint32_t>(lightingType) << 24) | lightingFlags;
				currentPass->passEnum = lightingTechnique + LightingTechniqueStart;

				// Separate deferred and forward blended decals
				if (currentPass->accumulationHint == 3 && currentPass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) {
					currentPass->accumulationHint = 16;
				}
			}
			currentPass = currentPass->next;
		}

		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		using enum SIE::ShaderCache::LightingShaderTechniques;

		const auto& lightingPSConstants = ShaderConstants::LightingPS::Get();

		auto lightingFlags = shader->currentRawTechnique & ~(~0u << 24);
		auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((shader->currentRawTechnique >> 24) & 0x3F);
		if (!(lightingType == LODLand || lightingType == LODLandNoise) && (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr))) {
			auto shadowState = globals::game::shadowState;
			auto renderer = globals::game::renderer;
			auto graphicsState = globals::game::graphicsState;
			auto smState = globals::game::smState;

			RE::BSGraphics::Renderer::PrepareVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

			if (lightingType == MTLand || lightingType == MTLandLODBlend) {
				auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBRLandscape*>(material);

				constexpr size_t NormalStartIndex = 7;

				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
					if (pbrMaterial->landscapeBaseColorTextures[textureIndex] != nullptr) {
						shadowState->SetPSTexture(textureIndex, pbrMaterial->landscapeBaseColorTextures[textureIndex]->rendererTexture);
						shadowState->SetPSTextureAddressMode(textureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
						shadowState->SetPSTextureFilterMode(textureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
					}
					if (pbrMaterial->landscapeNormalTextures[textureIndex] != nullptr) {
						const uint32_t normalTextureIndex = NormalStartIndex + textureIndex;
						shadowState->SetPSTexture(normalTextureIndex, pbrMaterial->landscapeNormalTextures[textureIndex]->rendererTexture);
						shadowState->SetPSTextureAddressMode(normalTextureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
						shadowState->SetPSTextureFilterMode(normalTextureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
					}
					if (pbrMaterial->landscapeDisplacementTextures[textureIndex] != nullptr) {
						extendedRendererState.SetPSTexture(textureIndex, pbrMaterial->landscapeDisplacementTextures[textureIndex]->rendererTexture);
					}
					if (pbrMaterial->landscapeRMAOSTextures[textureIndex] != nullptr) {
						extendedRendererState.SetPSTexture(BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex, pbrMaterial->landscapeRMAOSTextures[textureIndex]->rendererTexture);
					}
				}

				if (pbrMaterial->terrainOverlayTexture != nullptr) {
					shadowState->SetPSTexture(13, pbrMaterial->terrainOverlayTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(13, RE::BSGraphics::TextureAddressMode::kClampSClampT);
					shadowState->SetPSTextureFilterMode(13, RE::BSGraphics::TextureFilterMode::kAnisotropic);
				}

				if (pbrMaterial->terrainNoiseTexture != nullptr) {
					shadowState->SetPSTexture(15, pbrMaterial->terrainNoiseTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(15, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
					shadowState->SetPSTextureFilterMode(15, RE::BSGraphics::TextureFilterMode::kBilinear);
				}

				{
					uint32_t flags = 0;
					for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
						if (pbrMaterial->isPbr[textureIndex]) {
							flags |= (1 << textureIndex);
							if (pbrMaterial->landscapeDisplacementTextures[textureIndex] != nullptr && pbrMaterial->landscapeDisplacementTextures[textureIndex] != graphicsState->GetRuntimeData().defaultTextureBlack) {
								flags |= (1 << (BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex));
							}
							if (pbrMaterial->glintParameters[textureIndex].enabled) {
								flags |= (1 << (2 * BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex));
							}
						}
					}
					shadowState->SetPSConstant(flags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRFlags);
				}

				{
					const size_t PBRParamsStartIndex = lightingPSConstants.PBRParams1;
					const size_t GlintParametersStartIndex = lightingPSConstants.LandscapeTexture1GlintParameters;

					for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
						std::array<float, 3> PBRParams;
						PBRParams[0] = pbrMaterial->roughnessScales[textureIndex];
						PBRParams[1] = pbrMaterial->displacementScales[textureIndex];
						PBRParams[2] = pbrMaterial->specularLevels[textureIndex];
						shadowState->SetPSConstant(PBRParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, PBRParamsStartIndex + textureIndex);

						std::array<float, 4> glintParameters;
						glintParameters[0] = pbrMaterial->glintParameters[textureIndex].screenSpaceScale;
						glintParameters[1] = 40.f - pbrMaterial->glintParameters[textureIndex].logMicrofacetDensity;
						glintParameters[2] = pbrMaterial->glintParameters[textureIndex].microfacetRoughness;
						glintParameters[3] = pbrMaterial->glintParameters[textureIndex].densityRandomization;
						shadowState->SetPSConstant(glintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, GlintParametersStartIndex + textureIndex);
					}
				}

				{
					std::array<float, 4> lodTexParams;
					lodTexParams[0] = pbrMaterial->terrainTexOffsetX;
					lodTexParams[1] = pbrMaterial->terrainTexOffsetY;
					lodTexParams[2] = 1.f;
					lodTexParams[3] = pbrMaterial->terrainTexFade;
					shadowState->SetPSConstant(lodTexParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.LODTexParams);
				}
			} else if (lightingType == None || lightingType == TreeAnim) {
				auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
				if (pbrMaterial->diffuseRenderTargetSourceIndex != -1) {
					shadowState->SetPSTexture(0, renderer->GetRuntimeData().renderTargets[pbrMaterial->diffuseRenderTargetSourceIndex]);
				} else {
					shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
				}
				shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(1, pbrMaterial->normalTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(1, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(5, pbrMaterial->rmaosTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(5, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(5, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				stl::enumeration<PBRShaderFlags> shaderFlags;
				if (pbrMaterial->pbrFlags.any(PBRFlags::TwoLayer)) {
					shaderFlags.set(PBRShaderFlags::TwoLayer);
					if (pbrMaterial->pbrFlags.any(PBRFlags::InterlayerParallax)) {
						shaderFlags.set(PBRShaderFlags::InterlayerParallax);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::CoatNormal)) {
						shaderFlags.set(PBRShaderFlags::CoatNormal);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::ColoredCoat)) {
						shaderFlags.set(PBRShaderFlags::ColoredCoat);
					}

					std::array<float, 4> PBRParams2;
					PBRParams2[0] = pbrMaterial->GetCoatColor().red;
					PBRParams2[1] = pbrMaterial->GetCoatColor().green;
					PBRParams2[2] = pbrMaterial->GetCoatColor().blue;
					PBRParams2[3] = pbrMaterial->GetCoatStrength();
					shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams2);

					std::array<float, 4> PBRParams3;
					PBRParams3[0] = pbrMaterial->GetCoatRoughness();
					PBRParams3[1] = pbrMaterial->GetCoatSpecularLevel();
					shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
				} else if (pbrMaterial->pbrFlags.any(PBRFlags::HairMarschner)) {
					shaderFlags.set(PBRShaderFlags::HairMarschner);
				} else {
					if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
						shaderFlags.set(PBRShaderFlags::Subsurface);

						std::array<float, 4> PBRParams2;
						PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
						PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
						PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
						PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
						shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams2);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::Fuzz)) {
						shaderFlags.set(PBRShaderFlags::Fuzz);

						std::array<float, 4> PBRParams3;
						PBRParams3[0] = pbrMaterial->GetFuzzColor().red;
						PBRParams3[1] = pbrMaterial->GetFuzzColor().green;
						PBRParams3[2] = pbrMaterial->GetFuzzColor().blue;
						PBRParams3[3] = pbrMaterial->GetFuzzWeight();
						shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
					} else {
						if (pbrMaterial->GetGlintParameters().enabled) {
							shaderFlags.set(PBRShaderFlags::Glint);

							std::array<float, 4> GlintParameters;
							GlintParameters[0] = pbrMaterial->GetGlintParameters().screenSpaceScale;
							GlintParameters[1] = 40.f - pbrMaterial->GetGlintParameters().logMicrofacetDensity;
							GlintParameters[2] = pbrMaterial->GetGlintParameters().microfacetRoughness;
							GlintParameters[3] = pbrMaterial->GetGlintParameters().densityRandomization;
							shadowState->SetPSConstant(GlintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
						}
						if ((lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV)) != 0 && pbrMaterial->GetProjectedMaterialGlintParameters().enabled) {
							shaderFlags.set(PBRShaderFlags::ProjectedGlint);

							std::array<float, 4> ProjectedGlintParameters;
							ProjectedGlintParameters[0] = pbrMaterial->GetProjectedMaterialGlintParameters().screenSpaceScale;
							ProjectedGlintParameters[1] = 40.f - pbrMaterial->GetProjectedMaterialGlintParameters().logMicrofacetDensity;
							ProjectedGlintParameters[2] = pbrMaterial->GetProjectedMaterialGlintParameters().microfacetRoughness;
							ProjectedGlintParameters[3] = pbrMaterial->GetProjectedMaterialGlintParameters().densityRandomization;
							shadowState->SetPSConstant(ProjectedGlintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.SparkleParams);
						}
					}
				}

				{
					std::array<float, 4> PBRProjectedUVParams1;
					PBRProjectedUVParams1[0] = pbrMaterial->GetProjectedMaterialBaseColorScale()[0];
					PBRProjectedUVParams1[1] = pbrMaterial->GetProjectedMaterialBaseColorScale()[1];
					PBRProjectedUVParams1[2] = pbrMaterial->GetProjectedMaterialBaseColorScale()[2];
					shadowState->SetPSConstant(PBRProjectedUVParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.EnvmapData);

					std::array<float, 4> PBRProjectedUVParams2;
					PBRProjectedUVParams2[0] = pbrMaterial->GetProjectedMaterialRoughness();
					PBRProjectedUVParams2[1] = pbrMaterial->GetProjectedMaterialSpecularLevel();
					shadowState->SetPSConstant(PBRProjectedUVParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.ParallaxOccData);
				}

				const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
				if (hasEmissive) {
					shadowState->SetPSTexture(6, pbrMaterial->emissiveTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(6, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(6, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasEmissive);
				}

				const bool hasDisplacement = pbrMaterial->displacementTexture != nullptr && pbrMaterial->displacementTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
				if (hasDisplacement) {
					shadowState->SetPSTexture(4, pbrMaterial->displacementTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasDisplacement);
				}

				const bool hasFeaturesTexture0 = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != graphicsState->GetRuntimeData().defaultTextureWhite;
				if (hasFeaturesTexture0) {
					shadowState->SetPSTexture(12, pbrMaterial->featuresTexture0->rendererTexture);
					shadowState->SetPSTextureAddressMode(12, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(12, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasFeaturesTexture0);
				}

				const bool hasFeaturesTexture1 = pbrMaterial->featuresTexture1 != nullptr && pbrMaterial->featuresTexture1 != graphicsState->GetRuntimeData().defaultTextureWhite;
				if (hasFeaturesTexture1) {
					shadowState->SetPSTexture(9, pbrMaterial->featuresTexture1->rendererTexture);
					shadowState->SetPSTextureAddressMode(9, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(9, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasFeaturesTexture1);
				}

				{
					shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRFlags);
				}

				{
					std::array<float, 3> PBRParams1;
					PBRParams1[0] = pbrMaterial->GetRoughnessScale();
					PBRParams1[1] = pbrMaterial->GetDisplacementScale();
					PBRParams1[2] = pbrMaterial->GetSpecularLevel();
					shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams1);
				}
			}

			{
				const uint32_t bufferIndex = smState->textureTransformCurrentBuffer;

				std::array<float, 4> texCoordOffsetScale;
				texCoordOffsetScale[0] = material->texCoordOffset[bufferIndex].x;
				texCoordOffsetScale[1] = material->texCoordOffset[bufferIndex].y;
				texCoordOffsetScale[2] = material->texCoordScale[bufferIndex].x;
				texCoordOffsetScale[3] = material->texCoordScale[bufferIndex].y;
				shadowState->SetVSConstant(texCoordOffsetScale, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 11);
			}

			if (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::CharacterLight)) {
				static const REL::Relocation<RE::ImageSpaceTexture*> characterLightTexture{ RELOCATION_ID(513464, 391302) };

				if (characterLightTexture->renderTarget >= RE::RENDER_TARGET::kFRAMEBUFFER) {
					shadowState->SetPSTexture(11, renderer->GetRuntimeData().renderTargets[characterLightTexture->renderTarget]);
					shadowState->SetPSTextureAddressMode(11, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				}

				std::array<float, 4> characterLightParams;
				if (smState->characterLightEnabled) {
					std::copy_n(smState->characterLightParams, 4, characterLightParams.data());
				} else {
					std::fill_n(characterLightParams.data(), 4, 0.f);
				}
				shadowState->SetPSConstant(characterLightParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.CharacterLightParams);
			}

			RE::BSGraphics::Renderer::FlushVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		} else {
			func(shader, material);
		}
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_SetupGeometry
{
	static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		const auto originalTechnique = shader->currentRawTechnique;

		if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
			shader->currentRawTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
			shader->currentRawTechnique ^= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
		}

		shader->currentRawTechnique &= ~0b111000u;
		shader->currentRawTechnique |= (std::min((pass->numLights - 1), 7) << 3);

		func(shader, pass, renderFlags);

		shader->currentRawTechnique = originalTechnique;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_GetPixelTechnique
{
	static uint32_t thunk(uint32_t rawTechnique)
	{
		uint32_t pixelTechnique = rawTechnique;

		pixelTechnique &= ~0b111000000u;
		if ((pixelTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals)) == 0) {
			pixelTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned);
		}
		pixelTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::VC);

		return pixelTechnique;
	}
};

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const TruePBR::PBRTextureSetData& textureSetData, uint32_t textureIndex)
{
	material.displacementScales[textureIndex] = textureSetData.displacementScale;
	material.roughnessScales[textureIndex] = textureSetData.roughnessScale;
	material.specularLevels[textureIndex] = textureSetData.specularLevel;
	material.glintParameters[textureIndex] = textureSetData.glintParameters;
}

void SetupLandscapeTexture(BSLightingShaderMaterialPBRLandscape& material, RE::TESLandTexture& landTexture, uint32_t textureIndex, std::array<TruePBR::PBRTextureSetData*, BSLightingShaderMaterialPBRLandscape::NumTiles>& textureSets)
{
	if (textureIndex >= 6) {
		return;
	}

	auto textureSet = landTexture.textureSet;
	if (textureSet == nullptr) {
		return;
	}

	auto truePBR = globals::truePBR;
	auto* textureSetData = truePBR->GetPBRTextureSetData(landTexture.textureSet);
	const bool isPbr = textureSetData != nullptr;

	textureSets[textureIndex] = textureSetData;

	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::BaseColorTexture, material.landscapeBaseColorTextures[textureIndex]);
	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::NormalTexture, material.landscapeNormalTextures[textureIndex]);

	if (isPbr) {
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::RmaosTexture, material.landscapeRMAOSTextures[textureIndex]);
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::DisplacementTexture, material.landscapeDisplacementTextures[textureIndex]);
		SetupPBRLandscapeTextureParameters(material, *textureSetData, textureIndex);
	}
	material.isPbr[textureIndex] = isPbr;

	if (material.landscapeBaseColorTextures[textureIndex] != nullptr) {
		material.numLandscapeTextures = std::max(material.numLandscapeTextures, textureIndex + 1);
	}
}

RE::TESLandTexture* GetDefaultLandTexture()
{
	static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
	return *defaultLandTextureAddress;
}

struct TESObjectLAND_SetupMaterial
{
	static bool thunk(RE::TESObjectLAND* land)
	{
		auto singleton = globals::truePBR;

		bool isPbr = false;
		if (land->loadedData != nullptr) {
			for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
				if (land->loadedData->defQuadTextures[quadIndex] != nullptr) {
					if (singleton->IsPBRTextureSet(land->loadedData->defQuadTextures[quadIndex]->textureSet)) {
						isPbr = true;
						break;
					}
				} else if (singleton->defaultPbrLandTextureSet != nullptr) {
					isPbr = true;
				}
				for (uint32_t textureIndex = 0; textureIndex < 6; ++textureIndex) {
					if (land->loadedData->quadTextures[quadIndex][textureIndex] != nullptr) {
						if (singleton->IsPBRTextureSet(land->loadedData->quadTextures[quadIndex][textureIndex]->textureSet)) {
							isPbr = true;
							break;
						}
					}
				}
			}
		}

		if (!isPbr) {
			return func(land);
		}

		if (land->loadedData != nullptr && land->loadedData->mesh[0] != nullptr) {
			land->data.flags.set(static_cast<RE::OBJ_LAND::Flag>(8));
			for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
				auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(globals::game::memoryManager->Allocate(REL::Module::IsVR() ? 0x178 : sizeof(RE::BSLightingShaderProperty), 0, false));
				shaderProperty->Ctor();

				{
					BSLightingShaderMaterialPBRLandscape srcMaterial;
					shaderProperty->LinkMaterial(&srcMaterial, true);
				}

				auto material = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderProperty->material);
				const auto& stateData = globals::game::graphicsState->GetRuntimeData();

				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
					material->landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
					material->landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
					material->landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
					material->landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
				}

				auto& textureSets = BSLightingShaderMaterialPBRLandscape::All[material];

				if (auto defTexture = land->loadedData->defQuadTextures[quadIndex]) {
					SetupLandscapeTexture(*material, *defTexture, 0, textureSets);
				} else {
					SetupLandscapeTexture(*material, *GetDefaultLandTexture(), 0, textureSets);
				}
				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles - 1; ++textureIndex) {
					if (auto landTexture = land->loadedData->quadTextures[quadIndex][textureIndex]) {
						SetupLandscapeTexture(*material, *landTexture, textureIndex + 1, textureSets);
					}
				}

				if (globals::game::bEnableLandFade->GetBool()) {
					shaderProperty->unk108 = false;
				}

				bool noLODLandBlend = false;
				auto tes = globals::game::tes;
				auto worldSpace = tes->GetRuntimeData2().worldSpace;
				if (worldSpace != nullptr) {
					if (auto terrainManager = worldSpace->GetTerrainManager()) {
						noLODLandBlend = reinterpret_cast<bool*>(terrainManager)[0x36];
					}
				}
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMultiTextureLandscape, true);
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kReceiveShadows, true);

				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, true);
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kNoLODLandBlend, noLODLandBlend);

				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kVertexLighting, true);

				const auto& children = land->loadedData->mesh[quadIndex]->GetChildren();
				auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
				shaderProperty->SetupGeometry(geometry);
				if (geometry != nullptr) {
					geometry->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
				}

				globals::game::smState->shadowSceneNode[0]->AttachObject(geometry);
			}

			return true;
		}

		return false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESForm_GetFormEditorID
{
	static const char* thunk(const RE::TESForm* form)
	{
		auto* singleton = globals::truePBR;
		auto it = singleton->editorIDs.find(form->GetFormID());
		if (it == singleton->editorIDs.cend()) {
			return "";
		}
		return it->second.c_str();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESForm_SetFormEditorID
{
	static bool thunk(RE::TESForm* form, const char* editorId)
	{
		auto* singleton = globals::truePBR;
		singleton->editorIDs[form->GetFormID()] = editorId;
		return true;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SetPerFrameBuffers
{
	static void thunk(void* renderer)
	{
		func(renderer);
		auto* singleton = globals::truePBR;
		singleton->SetupFrame();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectSimpleDecal_SetupGeometry
{
	static void thunk(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended)
	{
		func(decal, geometry, textureSet, blended);
		auto* singleton = globals::truePBR;

		if (auto* shaderProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get());
			shaderProperty != nullptr && singleton->IsPBRTextureSet(textureSet)) {
			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, textureSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;
				shaderProperty->SetFlags(kParallaxOcclusion, false);
				shaderProperty->SetFlags(kParallax, false);
				shaderProperty->SetFlags(kGlowMap, false);
				shaderProperty->SetFlags(kEnvMap, false);
				shaderProperty->SetFlags(kSpecular, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectGeometryDecal_Initialize
{
	static void thunk(RE::BSTempEffectGeometryDecal* decal)
	{
		func(decal);
		auto* singleton = globals::truePBR;

		if (decal->decal != nullptr && singleton->IsPBRTextureSet(decal->texSet)) {
			auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(globals::game::memoryManager->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
			shaderProperty->Ctor();

			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, decal->texSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;

				shaderProperty->SetFlags(kSkinned, true);
				shaderProperty->SetFlags(kDynamicDecal, true);
				shaderProperty->SetFlags(kZBufferTest, true);
				shaderProperty->SetFlags(kZBufferWrite, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}

			if (auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(decal->decal->GetGeometryRuntimeData().properties[0].get())) {
				alphaProperty->alphaFlags = (alphaProperty->alphaFlags & ~0x1FE) | 0xED;
			}

			shaderProperty->SetupGeometry(decal->decal.get());
			decal->decal->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShaderProperty_ctor
{
	static RE::BSLightingShaderProperty* thunk(RE::BSLightingShaderProperty* property)
	{
		const uint64_t stackPointer = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
		const uint64_t lightingPropertyAddress = stackPointer + (REL::Module::IsAE() ? 0x68 : 0x70);
		auto* lightingProperty = *reinterpret_cast<RE::BSLightingShaderProperty**>(lightingPropertyAddress);

		RE::BSLightingShaderProperty* grassProperty = func(property);

		if (lightingProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
			if (auto* pbrSrcMaterial = static_cast<BSLightingShaderMaterialPBR*>(lightingProperty->material)) {
				BSLightingShaderMaterialPBR srcMaterial;
				grassProperty->LinkMaterial(&srcMaterial, true);

				// Since grass actually uses kVertexLighting flag we need to switch to some flag which it does not
				// use, specifically kMenuScreen.
				grassProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMenuScreen, true);

				auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(grassProperty->material);
				pbrMaterial->pbrFlags = pbrSrcMaterial->pbrFlags;
				pbrMaterial->normalTexture = pbrSrcMaterial->normalTexture;
				pbrMaterial->rmaosTexture = pbrSrcMaterial->rmaosTexture;
				pbrMaterial->featuresTexture0 = pbrSrcMaterial->featuresTexture0;
				pbrMaterial->featuresTexture1 = pbrSrcMaterial->featuresTexture1;
				pbrMaterial->specularColorScale = pbrSrcMaterial->specularColorScale;
				pbrMaterial->specularPower = pbrSrcMaterial->specularPower;
				pbrMaterial->specularColor = pbrSrcMaterial->specularColor;
				pbrMaterial->subSurfaceLightRolloff = pbrSrcMaterial->subSurfaceLightRolloff;
				pbrMaterial->coatRoughness = pbrSrcMaterial->coatRoughness;
			}
		}

		return grassProperty;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
	{
		auto renderPasses = func(property, geometry, renderFlags, accumulator);
		if (renderPasses == nullptr) {
			return renderPasses;
		}

		const bool isPbr = property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
		if (isPbr) {
			auto currentPass = renderPasses->head;
			while (currentPass != nullptr) {
				if (currentPass->shader->shaderType == RE::BSShader::Type::Grass && currentPass->passEnum != 0x5C00005C) {
					currentPass->passEnum = 0x5C000042;
				}
				currentPass = currentPass->next;
			}
		}

		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShader_SetupTechnique
{
	static bool thunk(RE::BSShader* shader, uint32_t globalTechnique)
	{
		if (globalTechnique == 0x5C000042) {
			auto shadowState = globals::game::shadowState;
			auto graphicsState = globals::game::graphicsState;
			auto renderer = globals::game::renderer;

			const uint32_t localTechnique = static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr);
			uint32_t shaderDescriptor = localTechnique;
			if (graphicsState->useEarlyZ) {
				shaderDescriptor |= static_cast<uint32_t>(SIE::ShaderCache::GrassShaderFlags::AlphaTest);
			}

			const bool began = Hooks::BSShader_BeginTechnique::thunk(shader, shaderDescriptor, shaderDescriptor, false);
			if (!began) {
				return false;
			}

			static auto fogMethod = REL::Relocation<void (*)()>(REL::RelocationID(100000, 106707));
			fogMethod();

			if (!globals::game::bShadowsOnGrass->GetBool()) {
				shadowState->SetPSTexture(1, graphicsState->GetRuntimeData().defaultTextureWhite->rendererTexture);
				shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kNearest);
			} else {
				shadowState->SetPSTexture(1, renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK]);
				shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				shadowState->SetPSTextureFilterMode(1, globals::game::shadowMaskQuarter->GetSInt() != 4 ? RE::BSGraphics::TextureFilterMode::kBilinear : RE::BSGraphics::TextureFilterMode::kNearest);
			}

			return true;
		}

		return func(shader, globalTechnique);
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShader_SetupMaterial
{
	static void thunk(RE::BSShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		const auto state = globals::state;
		const auto technique = static_cast<SIE::ShaderCache::GrassShaderTechniques>(state->currentPixelDescriptor & 0b1111);

		const auto& grassPSConstants = ShaderConstants::GrassPS::Get();

		if (technique == SIE::ShaderCache::GrassShaderTechniques::TruePbr) {
			auto shadowState = globals::game::shadowState;

			RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

			auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
			shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(2, pbrMaterial->normalTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(2, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(2, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(3, pbrMaterial->rmaosTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(3, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(3, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			stl::enumeration<PBRShaderFlags> shaderFlags;
			if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
				shaderFlags.set(PBRShaderFlags::Subsurface);
			}

			const bool hasSubsurface = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != globals::game::graphicsState->GetRuntimeData().defaultTextureWhite;
			if (hasSubsurface) {
				shadowState->SetPSTexture(4, pbrMaterial->featuresTexture0->rendererTexture);
				shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasFeaturesTexture0);
			}

			{
				shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, grassPSConstants.PBRFlags);
			}

			{
				std::array<float, 3> PBRParams1;
				PBRParams1[0] = pbrMaterial->GetRoughnessScale();
				PBRParams1[1] = pbrMaterial->GetSpecularLevel();
				shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, grassPSConstants.PBRParams1);
			}

			{
				std::array<float, 4> PBRParams2;
				PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
				PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
				PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
				PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
				shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, grassPSConstants.PBRParams2);
			}

			RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		} else {
			func(shader, material);
		}
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESBoundObject_Clone3D
{
	static RE::NiAVObject* thunk(RE::TESBoundObject* object, RE::TESObjectREFR* ref, bool arg3)
	{
		auto truePBR = globals::truePBR;
		auto* result = func(object, ref, arg3);
		if (result != nullptr && ref != nullptr && ref->data.objectReference != nullptr && ref->data.objectReference->formType == RE::FormType::Static) {
			auto* stat = static_cast<RE::TESObjectSTAT*>(ref->data.objectReference);
			if (stat->data.materialObj != nullptr && stat->data.materialObj->directionalData.singlePass) {
				if (auto* pbrData = truePBR->GetPBRMaterialObjectData(stat->data.materialObj)) {
					RE::BSVisit::TraverseScenegraphGeometries(result, [pbrData](RE::BSGeometry* geometry) {
						if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get())) {
							if (shaderProperty->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting &&
								shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
								if (auto* material = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material)) {
									material->ApplyMaterialObjectData(*pbrData);
									BSLightingShaderMaterialPBR::All[material].materialObjectData = pbrData;
								}
							}
						}

						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
			}
		}
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BGSTextureSet_ToShaderTextureSet
{
	static RE::BSShaderTextureSet* thunk(RE::BGSTextureSet* textureSet)
	{
		auto truePBR = globals::truePBR;
		truePBR->currentTextureSet = textureSet;

		return func(textureSet);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_OnLoadTextureSet
{
	static void thunk(RE::BSLightingShaderProperty* property, void* a2)
	{
		func(property, a2);

		auto truePBR = globals::truePBR;
		truePBR->currentTextureSet = nullptr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void TruePBR::PostPostLoad()
{
	logger::info("Hooking BGSTextureSet");
	stl::detour_thunk<BGSTextureSet_ToShaderTextureSet>(REL::RelocationID(20905, 21361));

	logger::info("Hooking BSLightingShaderProperty");
	stl::write_vfunc<0x18, BSLightingShaderProperty_LoadBinary>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::detour_thunk<BSLightingShaderProperty_OnLoadTextureSet>(REL::RelocationID(99865, 106510));

	logger::info("Hooking BSLightingShader");
	stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::detour_thunk_ignore_func<BSLightingShader_GetPixelTechnique>(REL::RelocationID(101633, 108700));

	logger::info("Hooking TESObjectLAND");
	stl::detour_thunk<TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

	logger::info("Hooking TESLandTexture");
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESWeather[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESWeather[0]);

	logger::info("Hooking SetPerFrameBuffers");
	stl::detour_thunk<SetPerFrameBuffers>(REL::RelocationID(75570, 77371));

	logger::info("Hooking BSTempEffectSimpleDecal");
	stl::detour_thunk<BSTempEffectSimpleDecal_SetupGeometry>(REL::RelocationID(29253, 30108));

	logger::info("Hooking BSTempEffectGeometryDecal");
	stl::write_vfunc<0x25, BSTempEffectGeometryDecal_Initialize>(RE::VTABLE_BSTempEffectGeometryDecal[0]);

	logger::info("Hooking BSGrassShaderProperty::ctor");
	stl::write_thunk_call<BSGrassShaderProperty_ctor>(REL::RelocationID(15214, 15383).address() + REL::Relocate(0x45B, 0x4F5));

	logger::info("Hooking BSGrassShaderProperty");
	stl::write_vfunc<0x2A, BSGrassShaderProperty_GetRenderPasses>(RE::VTABLE_BSGrassShaderProperty[0]);

	logger::info("Hooking BSGrassShader");
	stl::write_vfunc<0x2, BSGrassShader_SetupTechnique>(RE::VTABLE_BSGrassShader[0]);
	stl::write_vfunc<0x4, BSGrassShader_SetupMaterial>(RE::VTABLE_BSGrassShader[0]);

	logger::info("Hooking TESObjectSTAT");
	stl::write_vfunc<0x4A, TESBoundObject_Clone3D>(RE::VTABLE_TESObjectSTAT[0]);
}

void TruePBR::DataLoaded()
{
	defaultPbrLandTextureSet = RE::TESForm::LookupByEditorID<RE::BGSTextureSet>("DefaultPBRLand");
	SetupDefaultPBRLandTextureSet();
}

void TruePBR::SetupDefaultPBRLandTextureSet()
{
	if (!defaultLandTextureSetReplaced && defaultPbrLandTextureSet != nullptr) {
		if (auto* defaultLandTexture = GetDefaultLandTexture()) {
			logger::info("[TruePBR] replacing default land texture set record with {}", defaultPbrLandTextureSet->GetFormEditorID());
			defaultLandTexture->textureSet = defaultPbrLandTextureSet;
			defaultLandTextureSetReplaced = true;
		}
	}
}

void TruePBR::SetShaderResouces(ID3D11DeviceContext* a_context)
{
	for (uint32_t textureIndex = 0; textureIndex < ExtendedRendererState::NumPSTextures; ++textureIndex) {
		if (extendedRendererState.PSResourceModifiedBits & (1 << textureIndex)) {
			a_context->PSSetShaderResources(ExtendedRendererState::FirstPSTexture + textureIndex, 1, &extendedRendererState.PSTexture[textureIndex]);
		}
	}
	extendedRendererState.PSResourceModifiedBits = 0;
}

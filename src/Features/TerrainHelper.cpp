#include "TerrainHelper.h"

#include "ShaderCache.h"
#include "State.h"

void TerrainHelper::DataLoaded()
{
	// Get the default landscape texture set for terrain helper
	const auto defaultLandTextureSet = RE::TESForm::LookupByEditorID<RE::BGSTextureSet>("LandscapeDefault");
	if (defaultLandTextureSet != nullptr) {
		logger::info("[Terrain Helper] LandscapeDefault EDID texture set found");
		defaultLandTexture = defaultLandTextureSet;
		// only enable if TerrainHelper.esp is loaded
		enabled = true;
	} else {
		logger::warn("[Terrain Helper] LandscapeDefault EDID texture set from TerrainHelper.esp not found. Terrain helper is disabled.");
		enabled = false;
	}
}

bool TerrainHelper::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (!enabled) {
		// terrain helper is not enabled
		return false;
	}

	if (land == nullptr || land->loadedData == nullptr || land->loadedData->mesh[0] == nullptr) {
		// this is not terrain or vanilla material failed
		return false;
	}

	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		// Get hash key of vanilla material
		uint32_t hashKey = 0;

		if (land->loadedData->mesh[quadI] == nullptr) {
			// continue if cannot find mesh
			continue;
		}

		const auto& children = land->loadedData->mesh[quadI]->GetChildren();
		auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
		if (geometry != nullptr) {
			const auto shaderProp = static_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get());
			if (shaderProp != nullptr) {
				hashKey = shaderProp->GetBaseMaterial()->hashKey;
			}
		}

		if (hashKey == 0) {
			// continue if cannot find hash key
			continue;
		}

		// Create array of texture sets (6 tiles)
		std::array<RE::BGSTextureSet*, 6> textureSets;
		auto defTexture = land->loadedData->defQuadTextures[quadI];
		if (defTexture != nullptr && defTexture->formID != 0) {
			textureSets[0] = Util::GetSeasonalSwap(defTexture->textureSet);
		} else {
			// this is a default texture
			textureSets[0] = Util::GetSeasonalSwap(defaultLandTexture);
		}
		for (uint32_t textureI = 0; textureI < 5; ++textureI) {
			auto curTexture = land->loadedData->quadTextures[quadI][textureI];
			if (curTexture == nullptr) {
				textureSets[textureI + 1] = nullptr;
				continue;
			}

			if (curTexture->formID == 0) {
				// this is a default texture
				textureSets[textureI + 1] = Util::GetSeasonalSwap(defaultLandTexture);
			} else {
				textureSets[textureI + 1] = Util::GetSeasonalSwap(land->loadedData->quadTextures[quadI][textureI]->textureSet);
			}
		}

		// Assign textures to material
		{
			const std::unique_lock lock(extendedSlotsMutex);
			auto& slot = extendedSlots.try_emplace(hashKey).first->second;

			for (uint32_t textureI = 0; textureI < 6; ++textureI) {
				if (textureSets[textureI] == nullptr) {
					continue;
				}

				auto txSet = textureSets[textureI];
				if (txSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(3)) != nullptr) {
					txSet->SetTexture(static_cast<RE::BSTextureSet::Texture>(3), slot.parallax[textureI]);
				}
			}
		}
	}

	return true;
}

struct THExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 6;
	static constexpr uint32_t FirstPSTexture = 92;

	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, NumPSTextures> PSTexture;

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		ID3D11ShaderResourceView* resourceView = newTexture ? newTexture->resourceView : nullptr;

		PSTexture[textureIndex] = resourceView;
		PSResourceModifiedBits |= (1 << textureIndex);
	}

	THExtendedRendererState()
	{
		std::fill(PSTexture.begin(), PSTexture.end(), nullptr);
	}
} thExtendedRendererState;

void TerrainHelper::SetShaderResouces(ID3D11DeviceContext* a_context)
{
	uint32_t mask = thExtendedRendererState.PSResourceModifiedBits;

	if (mask == 0) [[likely]] {
		return;  // Nothing to update
	}

	constexpr uint32_t firstTexture = THExtendedRendererState::FirstPSTexture;
	auto& textures = thExtendedRendererState.PSTexture;

	while (mask) {
		// Find the position of the first set bit
		uint32_t batchStart = std::countr_zero(mask);

		// Count consecutive 1s starting from batchStart
		uint32_t shiftedMask = mask >> batchStart;
		uint32_t batchCount = std::countr_one(shiftedMask);

		a_context->PSSetShaderResources(
			firstTexture + batchStart,
			batchCount,
			&textures[batchStart]);

		// Clear the processed bits
		uint32_t clearMask = ((1u << batchCount) - 1u) << batchStart;
		mask &= ~clearMask;
	}

	thExtendedRendererState.PSResourceModifiedBits = 0;
}

void TerrainHelper::BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material)
{
	if (!enabled) {
		// terrain helper is not enabled
		return;
	}

	if (material == nullptr) {
		return;
	}

	ExtendedSlots materialBase;
	{
		const std::shared_lock lock(extendedSlotsMutex);

		if (!extendedSlots.contains(material->hashKey)) {
			// hash does not exists
			return;
		}
		materialBase = extendedSlots[material->hashKey];
	}

	const auto state = globals::state;
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();
	
	state->permutationData.ExtraFeatureDescriptor &= ~(1 << 6);

	// Populate extended slots
	// Please update bits allocation in ExtraFeatureDescriptor/Permutation.hlsli and other feature code if you need to change the constant 6
	for (uint32_t textureI = 0; textureI < 6; ++textureI) {
		if (materialBase.parallax[textureI] != nullptr && materialBase.parallax[textureI] != stateData.defaultTextureNormalMap) {
			thExtendedRendererState.SetPSTexture(textureI, materialBase.parallax[textureI]->rendererTexture);
			state->permutationData.ExtraFeatureDescriptor |= 1 << textureI;
			state->permutationData.ExtraFeatureDescriptor |= (1 << 6);
		} else {
			thExtendedRendererState.SetPSTexture(textureI, nullptr);
			state->permutationData.ExtraFeatureDescriptor &= ~(1 << textureI);
		}
	}
}
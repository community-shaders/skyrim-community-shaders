#include "Features/SnowDeformation.h"

#include "Globals.h"
#include "State.h"

/** @brief True when a land texture is snow by material (the same classification the vanilla shader constants encode). */
static bool IsSnowLandTexture(RE::TESLandTexture* a_landTexture)
{
	if (!a_landTexture || a_landTexture->formID == 0)
		return false;

	return a_landTexture->materialType &&
	       (a_landTexture->materialType->materialID == RE::MATERIAL_ID::kSnow ||
			   a_landTexture->materialType->materialID == RE::MATERIAL_ID::kSnowStairs);
}

void SnowDeformation::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (land == nullptr || land->loadedData == nullptr || land->loadedData->mesh[0] == nullptr)
		return;

	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		if (land->loadedData->mesh[quadI] == nullptr)
			continue;

		// This hook is OUTER relative to TruePBR's, so the shader property here
		// is the final one used for drawing — vanilla or TruePBR-replaced.
		// (TruePBR allocates a whole new property + material per quad, so the
		// vanilla material would be the wrong cache key.)
		const auto& children = land->loadedData->mesh[quadI]->GetChildren();
		auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
		if (geometry == nullptr)
			continue;

		const auto shaderProp = static_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
		if (shaderProp == nullptr || shaderProp->material == nullptr)
			continue;

		// Bit 0 = base texture, bits 1-5 = the quad's layer textures.
		uint8_t mask = 0;
		if (IsSnowLandTexture(land->loadedData->defQuadTextures[quadI]))
			mask |= 1;
		for (uint32_t textureI = 0; textureI < 5; ++textureI) {
			if (IsSnowLandTexture(land->loadedData->quadTextures[quadI][textureI]))
				mask |= uint8_t(1 << (textureI + 1));
		}

		const std::unique_lock lock(snowMaskMutex);
		// Materials are freed on cell unload; bound the map so stale pointers
		// cannot accumulate over a long session.
		if (snowMasks.size() > 16384)
			snowMasks.clear();
		snowMasks[reinterpret_cast<uintptr_t>(shaderProp->material)] = mask;
	}
}

void SnowDeformation::BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material)
{
	auto state = globals::state;

	// Always clear first so bits never leak from the previous landscape draw.
	state->permutationData.ExtraFeatureDescriptor &= ~uint(State::ExtraFeatureDescriptors::SnowLandIsSnowMask);

	if (material == nullptr)
		return;

	uint8_t mask = 0;
	{
		const std::shared_lock lock(snowMaskMutex);
		auto it = snowMasks.find(reinterpret_cast<uintptr_t>(material));
		if (it == snowMasks.end()) {
			// Count misses only for landscape materials, where a miss is a bug.
			auto feature = material->GetFeature();
			if (feature == RE::BSShaderMaterial::Feature::kMultiTexLand || feature == static_cast<RE::BSShaderMaterial::Feature>(33))
				landMaskMisses.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		mask = it->second;
	}

	landMaskHits.fetch_add(1, std::memory_order_relaxed);
	state->permutationData.ExtraFeatureDescriptor |= uint32_t(mask) << 10;
}

struct SD_TESObjectLAND_SetupMaterial
{
	static bool thunk(RE::TESObjectLAND* land)
	{
		bool result = func(land);

		auto& snowDeformation = globals::features::snowDeformation;
		if (result && snowDeformation.loaded)
			snowDeformation.TESObjectLAND_SetupMaterial(land);

		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SD_BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		if (!material)
			return;

		func(shader, material);

		auto& snowDeformation = globals::features::snowDeformation;
		if (snowDeformation.loaded)
			snowDeformation.BSLightingShader_SetupMaterial(material);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void SnowDeformation::PostPostLoad()
{
	// Same detour target as TruePBR. TruePBR sits earlier in the feature list,
	// so its PostPostLoad detour is already installed; attaching now makes our
	// hook OUTER (detours are LIFO), i.e. we run after TruePBR has replaced
	// quad materials and can key the snow masks by the final material pointer.
	logger::info("[SNOW DEFORMATION] Hooking TESObjectLAND");
	stl::detour_thunk<SD_TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

	logger::info("[SNOW DEFORMATION] Hooking BSLightingShader::SetupMaterial");
	stl::write_vfunc<0x4, SD_BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
}

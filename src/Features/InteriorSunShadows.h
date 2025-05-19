#pragma once
#include "ShaderCache.h"

struct InteriorSunShadows : Feature
{
	static InteriorSunShadows* GetSingleton()
	{
		static InteriorSunShadows singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Interior Sun Shadows"; }
	virtual inline std::string GetShortName() override { return "InteriorSunShadows"; }
	virtual void DrawSettings() override;
	virtual bool SupportsVR() override { return true; }
	virtual void PostPostLoad() override;
	virtual void EarlyPrepass() override;

	struct Settings
	{
		bool ForceDoubleSidedRendering = true;
	};

	Settings settings;

	bool isInteriorWithSun = false;

	struct GetWorldSpace
	{
		static RE::TESWorldSpace* thunk(RE::TES* tes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DirShadowLightCulling
	{
		static void thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void UpdateRasterStateCullMode(const RE::BSRenderPass* pass, const uint32_t technique) const
	{
		if (isInteriorWithSun && settings.ForceDoubleSidedRendering && technique & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmap)) {
			const auto renderTwoSided = pass->shaderProperty->flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kAssumeShadowmask);
			if (renderTwoSided && *rasterStateCullMode != 0) {
				*rasterStateCullMode = 0;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			} else if (!renderTwoSided && *rasterStateCullMode != RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK) {
				*rasterStateCullMode = RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			}
		}
	}

private:
	float* gShadowDistance = nullptr;
	uint32_t* rasterStateCullMode = nullptr;

	RE::TESObjectCELL* currentCell = nullptr;

	bool arraysCleared = true;
	RE::BSTArray<RE::NiPointer<RE::NiAVObject>> currentCellRoomsAndPortals = {};
	RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>> replacementJobArrays = {};
	eastl::hash_set<RE::NiAVObject*> addedSet = {};

	static RE::TESWorldSpace* enableInteriorSunShadows;
	static RE::TESWorldSpace* disableInteriorSunShadows;

	void ClearArrays();

	void InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph);

	static bool IsInteriorWithSun(const RE::TESObjectCELL* cell);

	bool IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const;

	void PopulateReplacementJobArrays(RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays);
};
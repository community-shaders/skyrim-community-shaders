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
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Enables realistic sun shadows inside interior spaces that have openings to the exterior, such as windows and doors, bringing natural lighting indoors.",
			{ "Sun shadow casting through windows and openings",
				"Double-sided rendering for accurate interior shadows",
				"Automatic detection of interiors with sun exposure",
				"Enhanced directional light culling for interiors",
				"Seamless integration with existing shadow systems" }
		};
	}
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
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
			const auto flags = pass->shaderProperty->flags;
			const auto renderTwoSided = flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided) || flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kAssumeShadowmask, RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);
			if (renderTwoSided && *rasterStateCullMode != 0) {
				*rasterStateCullMode = 0;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			} else if (!renderTwoSided && *rasterStateCullMode != RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK) {
				*rasterStateCullMode = RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			}
		}
	}

	static bool IsInteriorWithSun(const RE::TESObjectCELL* cell);

private:
	enum class CellFlagExt : uint16_t
	{
		kSunlightShadows = 1 << 15,
	};

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

	bool IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const;

	void PopulateReplacementJobArrays(RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays);
};
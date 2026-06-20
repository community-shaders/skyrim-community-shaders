#pragma once
#include "OverlayFeature.h"
#include "UnifiedWater/Flowmap.h"
#include "UnifiedWater/WaterCache.h"

#include <atomic>

/** @brief Replaces distant water tiles with LOD0 water to eliminate water LOD mismatch. */
struct UnifiedWater : OverlayFeature
{
	virtual inline std::string GetName() override { return "Unified Water"; }
	virtual std::string GetDisplayName() override { return T("feature.unified_water.name", "Unified Water"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "UnifiedWater"; }
	virtual inline std::string_view GetShaderDefineName() override { return "UNIFIED_WATER"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }
	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.unified_water.description", "Unified Water provides a comprehensive fix to water LOD mismatch by replacing distant water tiles with LOD0 (Close Water)."),
			{ T("feature.unified_water.key_feature_1", "Unifies distant and close water appearance, streamlining all lighting visuals."),
				T("feature.unified_water.key_feature_2", "Completely and fundamentally resolves water LOD mismatch issues."),
				T("feature.unified_water.key_feature_3", "Provides background systems for water geometry rendering, allowing more advanced water effects."),
				T("feature.unified_water.key_feature_4", "Improves vanilla performance by using optimized water meshes for distant water.") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool UseOptimisedMeshes = true;
	};

	Settings settings;

	/** @brief Hook that overrides water shader material parameters during water initialization. */
	struct TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams
	{
		static void thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that modifies CRC32 computation for water shader materials to account for unified water. */
	struct BSWaterShaderMaterial_ComputeCRC32
	{
		static int32_t thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that intercepts terrain block attachment to inject unified water meshes. */
	struct BGSTerrainBlock_Attach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that intercepts terrain block detachment to clean up unified water meshes. */
	struct BGSTerrainBlock_Detach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that controls water mesh sub-visibility for terrain LOD nodes. */
	struct BGSTerrainNode_UpdateWaterMeshSubVisibility
	{
		static void thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that handles worldspace transitions to update water cache and flowmap state. */
	struct TES_SetWorldSpace
	{
		static void thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that handles sky cell destruction during worldspace cleanup. */
	struct TES_DestroySkyCell
	{
		static void thunk(RE::TES* tes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that sets up per-geometry water shader data during rendering. */
	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that updates displacement mesh position for water surface animation. */
	struct TESWaterSystem_UpdateDisplacementMeshPosition
	{
		static void thunk(RE::TESWaterSystem* waterSystem);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Event handler that tracks menu open/close state for water rendering decisions. */
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		/** @brief Processes menu open/close events to track map menu state. */
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		/** @brief Registers the event handler with the game's UI event source. */
		static bool Register();
	};

	/** @brief Draws the ImGui settings panel for unified water configuration. */
	virtual void DrawSettings() override;

	/** @brief Draws the debug overlay showing water cache build progress. */
	virtual void DrawOverlay() override;
	/** @brief Returns whether the overlay should be displayed. */
	virtual bool IsOverlayVisible() const override;

	/** @brief Handles post-data-load initialization including flowmap and cache setup. */
	virtual void DataLoaded() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }

	/** @brief Installs engine hooks for water mesh replacement and worldspace handling. */
	virtual void PostPostLoad() override;

private:
	RE::NiPointer<RE::BSTriShape> waterMesh;
	RE::NiPointer<RE::BSTriShape> optimisedWaterMesh;
	Flowmap* flowmap = nullptr;
	WaterCache* waterCache = nullptr;

	RE::NiNode** gWaterLOD = nullptr;
	RE::NiPointer<RE::NiSourceTexture>* gFlowMapSourceTex = nullptr;
	int32_t* gFlowMapSize = nullptr;
	float4* gDisplacementCellTexCoordOffset = nullptr;
	RE::NiPoint2* gDisplacementMeshPos = nullptr;
	RE::NiPoint2* gDisplacementMeshFlowCellOffset = nullptr;

	std::atomic_bool exteriorWorldspaceActive{ false };
	std::atomic_bool mapMenuOpen{ false };

	void SetFlowmapTex() const;
	bool IsExteriorWorldspaceActive() const;
	void UpdateWaterLODCull() const;
	static bool LoadOrderChanged();
};

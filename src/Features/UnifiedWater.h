#pragma once
#include "OverlayFeature.h"
#include "UnifiedWater/Flowmap.h"
#include "UnifiedWater/WaterCache.h"

// Ensure BGS terrain classes are available
#include "RE/B/BGSTerrainBlock.h"
#include "RE/B/BGSTerrainNode.h"
#include "RE/C/Console.h"

struct UnifiedWater : OverlayFeature
{
	virtual inline std::string GetName() override { return "Unified Water"; }
	virtual inline std::string GetShortName() override { return "UnifiedWater"; }
	virtual inline std::string_view GetShaderDefineName() override { return "UNIFIED_WATER"; }
	virtual std::string_view GetCategory() const override { return "Water"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Enhanced water rendering system with improved wave simulation and foam generation.",
			{ "Optimized water meshes for better performance",
				"Gerstner wave simulation for realistic water movement", 
				"Advanced depth-based foam generation",
				"Enhanced flowmap support for dynamic water flow",
				"Seamless integration with existing water effects" }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool UseOptimisedMeshes = false;
		
		float WaveIntensity = 0.3f;
		float WaveAmplitude = 1.0f;
		float WaveSpeed = 1.0f;
		
		float WaveSteepness = 1.0f;
		float FoamIntensity = 1.0f;
		float pad0[3];
	};

	struct alignas(16) PerFrame
	{
		float WaveIntensity;
		float WaveAmplitude;
		float WaveSpeed;
		float WaveSteepness;
		float GameTimeHours;
		float RealTimeSeconds;
		float TimeScale;
		float CellWorldSize;
		float PrevGameTimeHours;
		float PrevRealTimeSeconds;
		float PrevTimeScale;
		float FoamIntensity;
	};

	struct PerTile
	{
		// Current cell data
		float ShoreNormalX;
		float ShoreNormalY;
		float DistanceToShore;
		float PrevShoreNormalX;
		float PrevShoreNormalY;
		float PrevDistanceToShore;
		
		// Neighboring cell data for smooth blending
		// North (+Y)
		float ShoreNormalX_North;
		float ShoreNormalY_North;
		float DistanceToShore_North;
		
		// South (-Y)
		float ShoreNormalX_South;
		float ShoreNormalY_South;
		float DistanceToShore_South;
		
		// East (+X)
		float ShoreNormalX_East;
		float ShoreNormalY_East;
		float DistanceToShore_East;
		
		// West (-X)
		float ShoreNormalX_West;
		float ShoreNormalY_West;
		float DistanceToShore_West;
		
		// Tile's base cell coordinates for proper neighbor blending
		float TileCellX;
		float TileCellY;
		float pad0;
	};

	Settings settings;
	ConstantBuffer* perFrame = nullptr;
	ConstantBuffer* perTile = nullptr;
	float lastGameTimeHours = 0.0f;
	float lastRealTimeSeconds = 0.0f;
	float lastTimeScale = 1.0f;
	bool hasLastTimingSample = false;
	
	// Previous frame shoreline data for TAA consistency
	float lastShoreNormalX = 0.0f;
	float lastShoreNormalY = 0.0f;
	float lastDistanceToShore = 10000.0f;
	bool hasLastShorelineData = false;
	
	virtual void SetupResources() override;
	virtual void Reset() override;

	struct TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams
	{
		static void thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSWaterShaderMaterial_ComputeCRC32
	{
		static int32_t thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Attach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Detach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainNode_UpdateWaterMeshSubVisibility
	{
		static void thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TES_SetWorldSpace
	{
		static void thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TES_DestroySkyCell
	{
		static void thunk(RE::TES* tes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESWaterSystem_UpdateDisplacementMeshPosition
	{
		static void thunk(RE::TESWaterSystem* waterSystem);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	virtual void DrawSettings() override;

	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override;

	virtual void DataLoaded() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; }

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
	
	void SetFlowmapTex() const;
	static bool LoadOrderChanged();
};

#pragma once
#include "OverlayFeature.h"
#include "UnifiedWater/Flowmap.h"
#include "UnifiedWater/WaterCache.h"
#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

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
		bool EnableMeshSubdivision = true;
		bool ShowSubdivisionVisualizer = false;

		float WaveIntensity = 0.3f;
		float WaveAmplitude = 1.0f;
		float WaveSpeed = 1.0f;
		float WaveSteepness = 1.0f;

		float FoamIntensity = 1.0f;
		float FoamShoreStrength = 1.0f;
		float FoamCrestStrength = 1.0f;
		float FoamTurbulenceStrength = 1.0f;

		float FoamFlowSpeedBase = 0.06f;
		float FoamFlowSpeedRange = 0.11f;
		float FoamShoreBoost = 0.03f;
		float FoamSwirlStrength = 9.0f;
		float FoamSwirlEnergyScale = 12.0f;

		float WavePrimaryContribution = 1.0f;
		float WaveSecondaryContribution = 1.0f;
		float WaveDetailContribution = 1.0f;
		float WavePrimarySpeed = 1.0f;
		float WaveSecondarySpeed = 1.0f;
		float WaveDetailSpeed = 1.0f;
		float WaveDirectionBlend = 1.0f;
		
		// Wave 1 (Primary) - Large swells: 4.8m wavelength, 0.85m amplitude
		float Wave1Amplitude = 8.5f;
		float Wave1Wavelength = 4800.0f;
		float Wave1Steepness = 0.35f;
		float Wave1AngleOffset = 0.0f;
		
		// Wave 2 (Secondary) - Medium waves: 3.2m wavelength, 0.55m amplitude
		float Wave2Amplitude = 5.5f;
		float Wave2Wavelength = 3200.0f;
		float Wave2Steepness = 0.28f;
		float Wave2AngleOffset = 50.0f;
		
		// Wave 3 (Detail) - Small waves: 2.0m wavelength, 0.32m amplitude
		float Wave3Amplitude = 3.2f;
		float Wave3Wavelength = 2000.0f;
		float Wave3Steepness = 0.22f;
		float Wave3AngleOffset = -50.0f;
		
		// Wave 4 (Fine Ripple 1) - Sub-meter: 0.8m wavelength, 0.12m amplitude
		float Wave4Amplitude = 1.2f;
		float Wave4Wavelength = 800.0f;
		float Wave4Steepness = 0.18f;
		float Wave4AngleOffset = 25.0f;
		
		// Wave 5 (Fine Ripple 2) - Tiny ripples: 0.4m wavelength, 0.06m amplitude
		float Wave5Amplitude = 0.6f;
		float Wave5Wavelength = 400.0f;
		float Wave5Steepness = 0.15f;
		float Wave5AngleOffset = -25.0f;
		
		// Wave 6 (Fine Ripple 3) - Micro detail: 0.2m wavelength, 0.03m amplitude
		float Wave6Amplitude = 0.3f;
		float Wave6Wavelength = 200.0f;
		float Wave6Steepness = 0.12f;
		float Wave6AngleOffset = 70.0f;
	};

#pragma warning(push)
#pragma warning(disable: 4324)
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
		float FoamShoreStrength;
		float FoamCrestStrength;
		float FoamTurbulenceStrength;
		float FoamFlowSpeedBase;
		float FoamFlowSpeedRange;
		float FoamShoreBoost;
		float FoamSwirlStrength;
		float FoamSwirlEnergyScale;
		float WavePrimaryContribution;
		float WaveSecondaryContribution;
		float WaveDetailContribution;
		float WavePrimarySpeed;
		float WaveSecondarySpeed;
		float WaveDetailSpeed;
		float WaveDirectionBlend;
		float TriVisualizerEnabled;
		
		// Wave 1 (Primary) - Large swells
		float Wave1Amplitude;
		float Wave1Wavelength;
		float Wave1Steepness;
		float Wave1AngleOffset;
		
		// Wave 2 (Secondary) - Medium waves
		float Wave2Amplitude;
		float Wave2Wavelength;
		float Wave2Steepness;
		float Wave2AngleOffset;
		
		// Wave 3 (Detail) - Small waves
		float Wave3Amplitude;
		float Wave3Wavelength;
		float Wave3Steepness;
		float Wave3AngleOffset;
		
		// Wave 4 (Fine Ripple 1) - Sub-meter detail
		float Wave4Amplitude;
		float Wave4Wavelength;
		float Wave4Steepness;
		float Wave4AngleOffset;
		
		// Wave 5 (Fine Ripple 2) - Micro ripples
		float Wave5Amplitude;
		float Wave5Wavelength;
		float Wave5Steepness;
		float Wave5AngleOffset;
		
		// Wave 6 (Fine Ripple 3) - Tiny surface detail
		float Wave6Amplitude;
		float Wave6Wavelength;
		float Wave6Steepness;
		float Wave6AngleOffset;
	};
#pragma warning(pop)

	struct alignas(16) PerTile
	{
		float PrevData[4];  // x/y = prev normal, z = prev distance, w = prev segments per axis
		float TileData[4];  // x/y = tile cell coords, z = LOD level, w = tile span (cells)
	};

	Settings settings;
	ConstantBuffer* perFrame = nullptr;
	ConstantBuffer* perTile = nullptr;
	float lastGameTimeHours = 0.0f;
	float lastRealTimeSeconds = 0.0f;
	float lastTimeScale = 1.0f;
	float currentGameTimeHours = 0.0f;
	float currentRealTimeSeconds = 0.0f;
	float currentTimeScale = 1.0f;
	std::uint32_t lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	bool hasLastTimingSample = false;

	struct PrevTileData
	{
		float normalX = 0.0f;
		float normalY = 0.0f;
		float distance = 10000.0f;
		float segmentsPerAxis = 32.0f;
	};

	std::unordered_map<std::uint64_t, PrevTileData> prevTileData;
	std::unordered_set<std::uint64_t> loggedCells;
	
	virtual void SetupResources() override;
	virtual void Reset() override;

	struct TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams
	{
		static void thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESWaterSystem_InitializeWater
	{
		static void thunk(RE::TESWaterSystem* waterSystem, RE::BSTriShape* waterTri, RE::TESWaterForm* form, float waterHeight, void* unk4, bool noDisplacement, bool isProcedural);
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
	std::uint16_t baseVertexCount = 0;
	std::uint16_t baseTriangleCount = 0;
	std::uint16_t optimisedVertexCount = 0;
	std::uint16_t optimisedTriangleCount = 0;
	RE::NiPointer<RE::BSTriShape> optimisedWaterMesh;
	std::array<RE::NiPointer<RE::BSTriShape>, 3> subdividedWaterMeshVariants{};
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

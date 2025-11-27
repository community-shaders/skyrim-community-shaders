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
		bool ShowTriVisualizer = false;

		bool EnableTessellation = true;
		float TessellationMinDistance = 128.0f;
		float TessellationMaxDistance = 4096.0f;
		float TessellationMinFactor = 1.0f;
		float TessellationMaxFactor = 8.0f;
		float DetailHeightScale = 0.0f;

		float WaveIntensity = 0.3f;
		float WaveAmplitude = 1.0f;
		float WaveSpeed = 0.05f;
		float WaveSteepness = 1.0f;

		float WavePrimaryContribution = 1.0f;
		float WaveSecondaryContribution = 1.0f;
		float WaveDetailContribution = 1.0f;
		float WavePrimarySpeed = 1.0f;
		float WaveSecondarySpeed = 1.0f;
		float WaveDetailSpeed = 1.0f;
		float WaveDirectionBlend = 1.0f;
		
		// Wave 1 (Primary) - Lake swells: ~12m wavelength, ~10cm amplitude
		float Wave1Amplitude = 2.2f;
		float Wave1Wavelength = 260.0f;
		float Wave1Steepness = 0.25f;
		float Wave1AngleOffset = 0.0f;
		
		// Wave 2 (Secondary) - Wind chop: ~6m wavelength, ~6cm amplitude
		float Wave2Amplitude = 1.3f;
		float Wave2Wavelength = 130.0f;
		float Wave2Steepness = 0.22f;
		float Wave2AngleOffset = 35.0f;
		
		// Wave 3 (Detail) - Surface ripples: ~3m wavelength, ~3cm amplitude
		float Wave3Amplitude = 0.65f;
		float Wave3Wavelength = 65.0f;
		float Wave3Steepness = 0.18f;
		float Wave3AngleOffset = -40.0f;
		
		// Wave 4 (Fine Ripple 1) - Small ripples: ~1.2m wavelength, ~1.5cm amplitude
		float Wave4Amplitude = 0.32f;
		float Wave4Wavelength = 26.0f;
		float Wave4Steepness = 0.14f;
		float Wave4AngleOffset = 25.0f;
		
		// Wave 5 (Fine Ripple 2) - Tiny ripples: ~0.6m wavelength, ~0.8cm amplitude
		float Wave5Amplitude = 0.17f;
		float Wave5Wavelength = 13.0f;
		float Wave5Steepness = 0.10f;
		float Wave5AngleOffset = -25.0f;
		
		// Wave 6 (Fine Ripple 3) - Micro detail: ~0.3m wavelength, ~0.4cm amplitude
		float Wave6Amplitude = 0.09f;
		float Wave6Wavelength = 6.5f;
		float Wave6Steepness = 0.08f;
		float Wave6AngleOffset = 70.0f;

		bool DisableVanillaWaterFoam = true;
		
		// Water Lighting Overrides
		bool EnableLightingOverrides = false;
		float FresnelBias = 0.02f;          // F0 for water (~0.02 for IOR 1.33)
		float FresnelPower = 5.0f;          // Schlick exponent
		float ReflectionStrength = 1.0f;    // Multiplier for reflection intensity
		float RefractionStrength = 1.0f;    // Refraction distortion amount
		float WaterTransparency = 1.0f;     // Shallow water transparency
		float AbsorptionDensity = 0.15f;    // Light absorption rate with depth
		float ScatteringCoeff = 0.05f;      // Subsurface scattering amount
		float SpecularIntensity = 1.0f;     // Overall specular intensity multiplier
		
		// Sun Specular Overrides
		float SunSpecularPower = 250.0f;    // Sharpness of sun highlight (VarAmounts.x)
		float SunSpecularMagnitude = 1.0f;  // Sun specular intensity multiplier
		float SunSparklePower = 50.0f;      // Sparkle highlight sharpness
		float SunSparkleMagnitude = 1.0f;   // Sparkle intensity
		float SpecularRadius = 128.0f;      // Specular highlight radius
		float SpecularBrightness = 1.0f;    // Overall specular brightness
		
		// Fog Overrides
		float AboveWaterFogDistNear = 0.0f;     // Near fog distance
		float AboveWaterFogDistFar = 163840.0f; // Far fog distance
		float AboveWaterFogAmount = 1.0f;       // Fog intensity above water
		float UnderwaterFogDistNear = 0.0f;     // Underwater near fog
		float UnderwaterFogDistFar = 4096.0f;   // Underwater far fog
		float UnderwaterFogAmount = 1.0f;       // Underwater fog intensity
		
		// Depth Properties (from ESP DepthControl)
		float DepthReflections = 1.0f;      // Depth-based reflection factor
		float DepthRefractions = 1.0f;      // Depth-based refraction factor
		float DepthNormals = 1.0f;          // Depth-based normal intensity
		float DepthSpecularLighting = 1.0f; // Depth-based specular factor
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
		
		// Water Lighting Overrides (repurposed foam system slots)
		float EnableLightingOverrides;
		float FresnelBias;           // F0 for water (0.02 = IOR 1.33)
		float FresnelPower;          // Schlick exponent
		float ReflectionStrength;    // Reflection intensity multiplier
		float RefractionStrength;    // Refraction distortion amount
		float WaterTransparency;     // Shallow water transparency
		float AbsorptionDensity;     // Light absorption rate
		float ScatteringCoeff;       // Subsurface scattering
		float SpecularIntensity;     // Overall specular intensity
		
		// Sun Specular Overrides
		float SunSpecularPower;      // Sharpness of sun highlight
		float SunSpecularMagnitude;  // Sun specular intensity
		float SunSparklePower;       // Sparkle sharpness
		float SunSparkleMagnitude;   // Sparkle intensity
		float SpecularRadius;        // Specular highlight radius
		float SpecularBrightness;    // Overall specular brightness
		
		// Fog Overrides
		float AboveWaterFogDistNear;
		float AboveWaterFogDistFar;
		float AboveWaterFogAmount;
		float UnderwaterFogDistNear;
		float UnderwaterFogDistFar;
		float UnderwaterFogAmount;
		
		// Depth Properties
		float DepthReflections;
		float DepthRefractions;
		float DepthNormals;
		float DepthSpecularLighting;
		
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
		
		// Tessellation control
		float TessellationEnabled;
		float TessPadding1;
		float TessPadding2;
		float TessPadding3;
	};
#pragma warning(pop)

	struct alignas(16) PerTile
	{
		float PrevData[4];  // x/y = prev normal, z = prev distance, w = prev segments per axis
		float TileData[4];  // x/y = tile cell coords, z = LOD level, w = tile span (cells)
	};

	struct alignas(16) TessellationParams
	{
		float TessellationMinDistance;
		float TessellationMaxDistance;
		float TessellationMinFactor;
		float TessellationMaxFactor;
		float CameraWorldPosX;
		float CameraWorldPosY;
		float CameraWorldPosZ;
		float DetailHeightScale;
	};

	Settings settings;
	ConstantBuffer* perFrame = nullptr;
	ConstantBuffer* perTile = nullptr;
	ConstantBuffer* tessellationParams = nullptr;
	
	winrt::com_ptr<ID3D11HullShader> waterHullShader;
	winrt::com_ptr<ID3D11DomainShader> waterDomainShader;
	winrt::com_ptr<ID3D11GeometryShader> waterGeometryShader;
	
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

	struct BSWaterShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static inline thread_local bool tessellationActiveForPass = false;
	static inline thread_local D3D11_PRIMITIVE_TOPOLOGY originalTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

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

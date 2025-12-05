#pragma once
#include "OverlayFeature.h"
#include "UnifiedWater/Flowmap.h"
#include "UnifiedWater/WaterCache.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <future>
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

	static constexpr uint32_t MAX_ACTOR_RIPPLES = 32;

	struct GeneralSettings
	{
		bool UseOptimisedMeshes = false;
	bool ShowWireframe = false;
	bool WireframeRawMode = false;
	};

	struct TessellationSettings
	{
		bool EnableTessellation = true;
		float TessellationMinDistance = 256.0f;
		float TessellationMaxDistance = 6144.0f;
		float TessellationMinFactor = 0.1f;
		float TessellationMaxFactor = 16.0f;
	};

	struct WaveSettings
	{
		float WaveIntensity = 0.3f;
		float WaveAmplitude = 0.7f;
		float WaveSpeed = 0.025f;
		float WaveSteepness = 5.0f;
		float WaveFadeStart = 4096.0f;
		float WaveFadeEnd = 8192.0f;
		
		float Wave1Amplitude = 0.8f;
		float Wave1Wavelength = 60.0f;
		float Wave1Steepness = 0.4f;
		float Wave1AngleOffset = 0.0f;
		
		float Wave2Amplitude = 0.5f;
		float Wave2Wavelength = 35.0f;
		float Wave2Steepness = 0.35f;
		float Wave2AngleOffset = 0.6f;
		
		float Wave3Amplitude = 0.25f;
		float Wave3Wavelength = 18.0f;
		float Wave3Steepness = 0.3f;
		float Wave3AngleOffset = -0.7f;
		
		float Wave4Amplitude = 0.12f;
		float Wave4Wavelength = 8.0f;
		float Wave4Steepness = 0.25f;
		float Wave4AngleOffset = 0.44f;
		
		float Wave5Amplitude = 0.06f;
		float Wave5Wavelength = 4.0f;
		float Wave5Steepness = 0.2f;
		float Wave5AngleOffset = -0.44f;
		
		float Wave6Amplitude = 0.03f;
		float Wave6Wavelength = 2.0f;
		float Wave6Steepness = 0.15f;
		float Wave6AngleOffset = 1.22f;
		
		// Depth-based wave control
		float ShallowWaveDepthMin = 50.0f;     // Depth where waves start reducing (game units, ~0.7m)
		float ShallowWaveDepthMax = 500.0f;    // Depth where waves reach full strength (game units, ~7m)
		float ShoreWaveDepthThreshold = 300.0f; // Depth range for shore-directed waves (game units, ~4.3m)
		float ShoreWaveStrength = 1.0f;        // Strength of shore-directed wave influence (0-1)
	};

	struct LightingSettings
	{
		bool EnableLightingOverrides = false;
		float FresnelBias = 0.02f;
		float FresnelPower = 5.0f;
		float ReflectionStrength = 1.0f;
		float RefractionStrength = 1.0f;
		float WaterTransparency = 1.0f;
		float AbsorptionDensity = 0.15f;
		float ScatteringCoeff = 0.05f;
		float SpecularIntensity = 1.0f;
		float SunSpecularPower = 250.0f;
		float SunSpecularMagnitude = 1.0f;
		float SunSparklePower = 50.0f;
		float SunSparkleMagnitude = 1.0f;
		float SpecularRadius = 128.0f;
		float SpecularBrightness = 1.0f;
	};

	struct FogSettings
	{
		float AboveWaterFogDistNear = 0.0f;
		float AboveWaterFogDistFar = 163840.0f;
		float AboveWaterFogAmount = 1.0f;
		float UnderwaterFogDistNear = 0.0f;
		float UnderwaterFogDistFar = 4096.0f;
		float UnderwaterFogAmount = 1.0f;
	};

	struct DepthSettings
	{
		float DepthReflections = 1.0f;
		float DepthRefractions = 1.0f;
		float DepthNormals = 1.0f;
		float DepthSpecularLighting = 1.0f;
	};

	struct RippleSettings
	{
		bool EnableActorRipples = true;
		float RippleStrength = 1.0f;
		float RippleRadius = 512.0f;
		float RippleWaveSpeed = 4.0f;
		float RippleWaveFreq1 = 0.08f;
		float RippleWaveFreq2 = 0.12f;
		float RippleWaveFreq3 = 0.18f;
		float RippleNormalStrength = 2.0f;
	};

	struct FoamSettings
	{
		bool EnableFoam = true;
		float FoamIntensity = 1.5f;
		float FoamIntensityFlowmap = 0.3f;
		float FoamThreshold = 0.6f;
		float FoamSharpness = 2.0f;
	};

	struct Settings
	{
		GeneralSettings general;
		TessellationSettings tessellation;
		WaveSettings waves;
		LightingSettings lighting;
		FogSettings fog;
		DepthSettings depth;
		RippleSettings ripples;
		FoamSettings foam;
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
		
		// Water Lighting Overrides
		float EnableLightingOverrides;
		float FresnelBias;
		float FresnelPower;
		float ReflectionStrength;
		float RefractionStrength;
		float WaterTransparency;
		float AbsorptionDensity;
		float ScatteringCoeff;
		float SpecularIntensity;
		
		// Sun Specular Overrides
		float SunSpecularPower;
		float SunSpecularMagnitude;
		float SunSparklePower;
		float SunSparkleMagnitude;
		float SpecularRadius;
		float SpecularBrightness;
		
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
		
		// Debug visualizer
		float WireframeEnabled;
		float PerFramePad0;
		float PerFramePad1;
		float PerFramePad2;
		
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
		float WaveFadeStart;      // Distance where waves start fading
		float WaveFadeEnd;        // Distance where waves fully fade
		float TessPadding3;
		
		// Player ripples data
		float PlayerPosX;
		float PlayerPosY;
		float PlayerPosZ;
		float PlayerSpeed;
		float PlayerInWater;
		float PlayerVelocityX;  // Actual velocity for wake direction
		float PlayerVelocityY;
		float PlayerWaterDepth;  // Depth below water surface
		float RippleStrength;
		float RippleRadius;
		float RippleWaveSpeed;
		float RippleWaveFreq1;
		float RippleWaveFreq2;
		float RippleWaveFreq3;
		float RippleNormalStrength;
		
		// Foam System
		float FoamEnabled;
		float FoamIntensity;
		float FoamIntensityFlowmap;
		float FoamThreshold;
		float FoamSharpness;
		
		// Depth-based wave control
		float ShallowWaveDepthMin;
		float ShallowWaveDepthMax;
		float ShoreWaveDepthThreshold;
		float ShoreWaveStrength;
		
		// Terrain heightmap parameters (for vertex shader depth estimation)
		float TerrainHeightmapEnabled;
		float TerrainScaleX;
		float TerrainScaleY;
		float TerrainOffsetX;
		float TerrainOffsetY;
		float TerrainZRangeMin;
		float TerrainZRangeMax;
		float TerrainPad0;
	};

	struct alignas(16) ActorRippleData
	{
		float PosX;
		float PosY;
		float Speed;
		float InWater;  // 1.0 if actor is in water, 0.0 otherwise
		float VelocityX;  // Actual velocity for wake direction
		float VelocityY;
		float WaterDepth;  // Depth below water surface (negative = above)
		float pad0;
	};

	struct alignas(16) ActorRippleBuffer
	{
		ActorRippleData actors[MAX_ACTOR_RIPPLES];
		uint32_t numActors;
		uint32_t pad0[3];
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
	ConstantBuffer* actorRippleBuffer = nullptr;
	
	winrt::com_ptr<ID3D11HullShader> waterHullShader;
	winrt::com_ptr<ID3D11DomainShader> waterDomainShader;
	winrt::com_ptr<ID3D11GeometryShader> waterGeometryShader;
	
	// Async shader compilation state
	std::atomic<bool> tessellationShadersReady{ false };
	std::atomic<bool> tessellationShadersCompiling{ false };
	std::future<void> shaderCompileFuture;
	
	void CompileTessellationShadersAsync();
	bool AreTessellationShadersReady() const { return tessellationShadersReady.load(); }
	
	float lastGameTimeHours = 0.0f;
	float lastRealTimeSeconds = 0.0f;
	float lastTimeScale = 1.0f;
	float currentGameTimeHours = 0.0f;
	float currentRealTimeSeconds = 0.0f;
	float currentTimeScale = 1.0f;
	std::uint32_t lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	bool hasLastTimingSample = false;
	
	// Player movement tracking
	RE::NiPoint3 lastPlayerPos{ 0.0f, 0.0f, 0.0f };
	RE::NiPoint2 playerVelocity{ 0.0f, 0.0f };
	float lastPlayerUpdateTime = 0.0f;
	bool hasPlayerMovementData = false;

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

#pragma once

#include <array>

#include <d3d11_4.h>
#include <directx/d3d12.h>

#include "OverlayFeature.h"

#include "Features/Upscaling/DX12SwapChain.h"

#include "Util.h"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

#include "Features/CloudShadows.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/HairSpecular.h"
#include "Features/LinearLighting.h"
#include "Features/Upscaling.h"
#include "Features/WetnessEffects.h"

#define STATIC_ASSERT_ENUM_COUNT(EnumType, Array) \
	static_assert(_countof(Array) == magic_enum::enum_count<EnumType>(), "Array size must match enum count");

struct CreationEngineRaytracing
{
	enum class Mode
	{
		None,
		GlobalIllumination,
		PathTracing
	};

	enum class Denoiser
	{
		None,
		NRD_REBLUR,
		DLSS_RR
	};

	struct GeneralSettings
	{
		Denoiser Denoiser = Denoiser::None;
		Mode Mode = Mode::GlobalIllumination;
		bool RaytracedShadows = false;

		bool operator==(const GeneralSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GeneralSettings, Denoiser, Mode, RaytracedShadows)
	};

	struct RaytracingSettings
	{
		int Bounces = 2;
		int SamplesPerPixel = 1;
		bool RussianRoulette = true;

		bool operator==(const struct RaytracingSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RaytracingSettings, Bounces, SamplesPerPixel, RussianRoulette)
	};

	struct ReblurSettings
	{
		// [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames
		// Always accumulate in "seconds" not in "frames", use "GetMaxAccumulatedFrameNum" for conversion
		uint32_t maxAccumulatedFrameNum = 30;

		// [0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for fast history
		// Values ">= maxAccumulatedFrameNum" disable fast history
		// Usually 5x-7x times shorter than the main history (casting more rays, using SHARC or other signal improving techniques help to accumulate less)
		uint32_t maxFastAccumulatedFrameNum = 6;

		// [0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for stabilized radiance
		// "0" disables the stabilization pass
		// Values ">= maxAccumulatedFrameNum" get clamped to "maxAccumulatedFrameNum"
		uint32_t maxStabilizedFrameNum = 63;

		// [0; maxFastAccumulatedFrameNum) - number of reconstructed frames after history reset
		uint32_t historyFixFrameNum = 3;

		// (> 0) - base stride between pixels in 5x5 history reconstruction kernel
		uint32_t historyFixBasePixelStride = 14;
		uint32_t historyFixAlternatePixelStride = 14;  // see "historyFixAlternatePixelStrideMaterialID"

		// [1; 3] - standard deviation scale of the color box for clamping slow "main" history to responsive "fast" history
		// REBLUR clamps the spatially processed "main" history to the spatially unprocessed "fast" history. It implies using smaller variance scaling than in RELAX.
		// A bit smaller values (> 1) may be used with clean signals. The implementation will adjust this under the hood if spatial sampling is disabled
		float fastHistoryClampingSigmaScale = 2.0f;  // 2 is old default, 1.5 works well even for dirty signals, 1.1 is a safe value for occlusion denoising

		// (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of badly defined signals and probabilistic sampling)
		float diffusePrepassBlurRadius = 30.0f;
		float specularPrepassBlurRadius = 50.0f;

		// (0; 0.2] - bigger values reduce sensitivity to shadows in spatial passes, smaller values are recommended for signals with relatively clean hit distance (like RTXDI/RESTIR)
		float minHitDistanceWeight = 0.1f;

		// (pixels) - min denoising radius (for converged state)
		float minBlurRadius = 1.0f;

		// (pixels) - base (max) denoising radius (gets reduced over time)
		float maxBlurRadius = 30.0f;

		// (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
		float lobeAngleFraction = 0.15f;

		// (normalized %) - base fraction of center roughness used to drive roughness based rejection
		float roughnessFraction = 0.15f;

		// (normalized %) - represents maximum allowed deviation from the local tangent plane
		float planeDistanceSensitivity = 0.02f;

		// "IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))"
		std::array<float, 2> specularProbabilityThresholdsForMvModification = { 0.5f, 0.9f };

		// [1; 3] - undesired sporadic outliers suppression to keep output stable (smaller values maximize suppression in exchange of bias)
		float fireflySuppressorMinRelativeScale = 2.0f;

		// Helps to mitigate fireflies emphasized by DLSS. Very cheap and unbiased in most of the cases, better keep in enabled to maximize quality
		bool enableAntiFirefly = true;

		// In rare cases, when bright samples are so sparse that any other bright neighbor can't
		// be reached, pre-pass transforms a standalone bright pixel into a standalone bright blob,
		// worsening the situation. Despite that it's a problem of sampling, the denoiser needs to
		// handle it somehow on its side too. Diffuse pre-pass can be just disabled, but for specular
		// it's still needed to find optimal hit distance for tracking. This boolean allow to use
		// specular pre-pass for tracking purposes only (use with care)
		bool usePrepassOnlyForSpecularMotionEstimation = false;

		// Allows to get diffuse or specular history length in ".w" channel of the output instead of denoised ambient/specular occlusion (normalized hit distance).
		// Diffuse history length shows disocclusions, specular history length is more complex and includes accelerations of various kinds caused by specular tracking.
		// History length is measured in frames, it can be in "[0; maxAccumulatedFrameNum]" range
		bool returnHistoryLengthInsteadOfOcclusion = false;

		bool operator==(const ReblurSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ReblurSettings,
			maxAccumulatedFrameNum,
			maxFastAccumulatedFrameNum,
			maxStabilizedFrameNum,
			historyFixFrameNum,
			historyFixBasePixelStride,
			historyFixAlternatePixelStride,
			fastHistoryClampingSigmaScale,
			diffusePrepassBlurRadius,
			specularPrepassBlurRadius,
			minHitDistanceWeight,
			minBlurRadius,
			maxBlurRadius,
			lobeAngleFraction,
			roughnessFraction,
			planeDistanceSensitivity,
			specularProbabilityThresholdsForMvModification,
			fireflySuppressorMinRelativeScale,
			enableAntiFirefly,
			usePrepassOnlyForSpecularMotionEstimation,
			returnHistoryLengthInsteadOfOcclusion)
	};

	struct MaterialSettings
	{
		float2 Roughness = { 0.0f, 1.0f };
		float2 Metalness = { 0.0f, 1.0f };

		bool operator==(const MaterialSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MaterialSettings, Roughness, Metalness)
	};

	struct LightingSettings
	{
		float Directional = 1.0f;
		float Point = 1.0f;
		bool LodDimmer = false;
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;

		bool operator==(const LightingSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LightingSettings, Directional, Point, LodDimmer, Emissive, Effect, Sky)
	};

	struct SHaRCSettings
	{
		bool Enabled = true;
		float SceneScale = 1.0f;
		int AccumFrameNum = 10;
		int StaleFrameNum = 64;
		float RadianceScale = 1e3f;

		bool operator==(const SHaRCSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SHaRCSettings, Enabled, SceneScale, AccumFrameNum, StaleFrameNum)
	};

	// Resampled Importance Sampling
	struct RISSettings
	{
		bool Enabled = true;
		int MaxCandidates = 4;

		bool operator==(const RISSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RISSettings, Enabled, MaxCandidates)
	};

	// TODO: Rename to ReflectanceModel?
	enum struct DiffuseBRDF : int32_t
	{
		Lambert,
		Burley,
		OrenNayar,
		Gotanda,
		Chan
	};

	enum struct HairBSDF : int32_t
	{
		None,
		ChiangBSDF,
		FarFieldBCSDF
	};

	struct SSSSettings
	{
		bool Enabled = true;
		int SampleCount = 1;
		float MaxSampleRadius = 1.0f;
		bool EnableTransmission = true;

		bool MaterialOverride = false;
		float3 OverrideTransmissionColor = float3(1.0f, 0.735f, 0.612f);
		float3 OverrideScatteringColor = float3(1.0f, 1.0f, 1.0f);
		float OverrideScale = 40.0f;
		float OverrideAnisotropy = -0.5f;

		bool operator==(const SSSSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			SSSSettings,
			Enabled,
			SampleCount,
			MaxSampleRadius,
			EnableTransmission,
			MaterialOverride,
			OverrideTransmissionColor,
			OverrideScatteringColor,
			OverrideScale,
			OverrideAnisotropy)
	};

	struct AdvancedSettings
	{
		float TexLODBias = -1.0f;
		bool VariableUpdateRate = true;
		bool GGXEnergyConservation = true;
		bool PerLightTLAS = false;
		RISSettings RIS;
		HairBSDF HairBSDF = HairBSDF::FarFieldBCSDF;
		DiffuseBRDF DiffuseBRDF = DiffuseBRDF::Burley;
		SSSSettings SSSSettings;
		bool EnableWater = false;
		bool StablePlanes = false;

		bool operator==(const AdvancedSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			AdvancedSettings,
			TexLODBias,
			VariableUpdateRate,
			GGXEnergyConservation,
			PerLightTLAS,
			RIS,
			HairBSDF,
			DiffuseBRDF,
			SSSSettings,
			EnableWater,
			StablePlanes)
	};

	struct WaterSettings
	{
		float AbsorptionScale = 1.0f;

		bool operator==(const WaterSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WaterSettings, AbsorptionScale)
	};

	enum struct ReSTIRGIResamplingMode : int32_t
	{
		None = 0,
		Temporal = 1,
		Spatial = 2,
		TemporalAndSpatial = 3,
		FusedSpatiotemporal = 4,
	};

	enum struct ReSTIRGIBiasCorrection : int32_t
	{
		Off = 0,
		Basic = 1,
		Raytraced = 3
	};

	struct ReSTIRGISettings
	{
		bool Enabled = false;
		ReSTIRGIResamplingMode ResamplingMode = ReSTIRGIResamplingMode::TemporalAndSpatial;

		float TemporalDepthThreshold = 0.1f;
		float TemporalNormalThreshold = 0.5f;
		int MaxHistoryLength = 20;
		int MaxReservoirAge = 100;
		bool EnablePermutationSampling = true;
		bool EnableFallbackSampling = true;
		ReSTIRGIBiasCorrection TemporalBiasCorrection = ReSTIRGIBiasCorrection::Basic;

		float SpatialDepthThreshold = 0.1f;
		float SpatialNormalThreshold = 0.5f;
		int SpatialNumSamples = 2;
		float SpatialSamplingRadius = 32.0f;
		ReSTIRGIBiasCorrection SpatialBiasCorrection = ReSTIRGIBiasCorrection::Basic;

		bool EnableBoilingFilter = true;
		float BoilingFilterStrength = 0.4f;

		bool EnableFinalVisibility = true;
		bool EnableFinalMIS = false;

		bool operator==(const ReSTIRGISettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ReSTIRGISettings, Enabled, ResamplingMode,
			TemporalDepthThreshold, TemporalNormalThreshold, MaxHistoryLength, MaxReservoirAge,
			EnablePermutationSampling, EnableFallbackSampling, TemporalBiasCorrection,
			SpatialDepthThreshold, SpatialNormalThreshold, SpatialNumSamples, SpatialSamplingRadius,
			SpatialBiasCorrection, EnableBoilingFilter, BoilingFilterStrength,
			EnableFinalVisibility, EnableFinalMIS)
	};

	enum struct TextureMode : uint32_t
	{
		Share = 0,
		Exclusive = 1
	};

	struct ExperimentalSettings
	{
		bool PathTracingCull = false;
		TextureMode TextureMode = TextureMode::Share;
		uint32_t TextureCutOff = 0;

		bool operator==(const ExperimentalSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ExperimentalSettings, PathTracingCull, TextureMode, TextureCutOff)
	};

	struct DebugSettings
	{
		bool Markers = false;
		bool Timings = false;

		bool operator==(const DebugSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DebugSettings, Markers, Timings)
	};

	struct PassTiming
	{
		const char* name;
		float timing;
	};

	struct Settings
	{
		bool Enabled = true;
		GeneralSettings GeneralSettings;
		LightingSettings LightingSettings;
		RaytracingSettings RaytracingSettings;
		ReblurSettings ReblurSettings;
		MaterialSettings MaterialSettings;
		SHaRCSettings SHaRCSettings;
		AdvancedSettings AdvancedSettings;
		WaterSettings WaterSettings;
		ExperimentalSettings ExperimentalSettings;
		ReSTIRGISettings ReSTIRGI;
		DebugSettings DebugSettings;

		bool operator==(const Settings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			Settings,
			Enabled,
			GeneralSettings,
			LightingSettings,
			RaytracingSettings,
			ReblurSettings,
			MaterialSettings,
			SHaRCSettings,
			AdvancedSettings,
			WaterSettings,
			ExperimentalSettings,
			ReSTIRGI,
			DebugSettings)
	};

	HMODULE handle = nullptr;

	using InitializeRendererFn = bool (*)(ID3D11Device5*, ID3D12Device5*, ID3D12CommandQueue*, ID3D12CommandQueue*, ID3D12CommandQueue*);
	using InitializeFn = void (*)(Settings);
	using UpdateCameraFn = void (*)();
	using ExecuteFn = void (*)();
	using WaitExecutionFn = void (*)();
	using PostExecutionFn = void (*)();
	using GetResolutionFn = void (*)(uint32_t&, uint32_t&);
	using SetResolutionFn = void (*)(uint32_t, uint32_t);
	using SetCopyTargetFn = void (*)(ID3D12Resource*);
	using UpdateFeatureDataFn = void (*)(void*, uint32_t);
	using SetSkyHemisphereFn = void (*)(ID3D12Resource*);
	using GetFrameTimeFn = void (*)(PassTiming*&, uint32_t&);
	using UpdateSettingsFn = void (*)(Settings);
	using GetRRInputFn = void (*)(ID3D12Resource*&, ID3D12Resource*&);
	using SetSharedTexturesFn = void (*)(ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*);
	using UpdateJitterFn = void (*)(float2);
	using SetPTOutputTargetsFn = void (*)(ID3D12Resource*, ID3D12Resource*);

	InitializeRendererFn InitializeRenderer = nullptr;
	InitializeFn Initialize = nullptr;
	UpdateCameraFn UpdateCamera = nullptr;
	ExecuteFn Execute = nullptr;
	WaitExecutionFn WaitExecution = nullptr;
	PostExecutionFn PostExecution = nullptr;
	SetResolutionFn SetResolution = nullptr;
	SetCopyTargetFn SetCopyTarget = nullptr;
	UpdateFeatureDataFn UpdateFeatureData = nullptr;
	SetSkyHemisphereFn SetSkyHemisphere = nullptr;
	GetFrameTimeFn GetFrameTime = nullptr;
	UpdateSettingsFn UpdateSettings = nullptr;
	GetRRInputFn GetRRInput = nullptr;
	SetSharedTexturesFn SetSharedTextures = nullptr;
	UpdateJitterFn UpdateJitter = nullptr;
	SetPTOutputTargetsFn SetPTOutputTargets = nullptr;

	CreationEngineRaytracing()
	{
		GetModuleHandleEx(
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			L"CreationEngineRaytracing.dll",
			&handle);

		if (!handle) {
			logger::critical("[Raytracing] 'CreationEngineRaytracing.dll' not found, make sure Creation Engine Raytracing is enabled in your mod manager.");
			return;
		}

		InitializeRenderer = reinterpret_cast<InitializeRendererFn>(GetProcAddress(handle, "InitializeRenderer"));

		if (!InitializeRenderer)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' InitializeRenderer is nullptr");

		Initialize = reinterpret_cast<InitializeFn>(GetProcAddress(handle, "Initialize"));

		if (!Initialize)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Initialize is nullptr");

		UpdateCamera = reinterpret_cast<UpdateCameraFn>(GetProcAddress(handle, "UpdateCamera"));

		if (!UpdateCamera)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateCamera is nullptr");

		Execute = reinterpret_cast<ExecuteFn>(GetProcAddress(handle, "Execute"));

		if (!Execute)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Execute is nullptr");

		WaitExecution = reinterpret_cast<WaitExecutionFn>(GetProcAddress(handle, "WaitExecution"));

		if (!WaitExecution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' WaitExecution is nullptr");

		PostExecution = reinterpret_cast<PostExecutionFn>(GetProcAddress(handle, "PostExecution"));

		if (!PostExecution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' PostExecution is nullptr");

		SetResolution = reinterpret_cast<SetResolutionFn>(GetProcAddress(handle, "SetResolution"));

		if (!SetResolution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetResolution is nullptr");

		SetCopyTarget = reinterpret_cast<SetCopyTargetFn>(GetProcAddress(handle, "SetCopyTarget"));

		if (!SetCopyTarget)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetCopyTarget is nullptr");

		UpdateFeatureData = reinterpret_cast<UpdateFeatureDataFn>(GetProcAddress(handle, "UpdateFeatureData"));

		if (!UpdateFeatureData)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateFeatureData is nullptr");

		SetSkyHemisphere = reinterpret_cast<SetSkyHemisphereFn>(GetProcAddress(handle, "SetSkyHemisphere"));

		if (!SetSkyHemisphere)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetSkyHemisphere is nullptr");

		GetFrameTime = reinterpret_cast<GetFrameTimeFn>(GetProcAddress(handle, "GetFrameTime"));

		if (!GetFrameTime)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' GetFrameTime is nullptr");

		UpdateSettings = reinterpret_cast<UpdateSettingsFn>(GetProcAddress(handle, "UpdateSettings"));

		if (!UpdateSettings)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateSettings is nullptr");

		GetRRInput = reinterpret_cast<GetRRInputFn>(GetProcAddress(handle, "GetRRInput"));

		if (!GetRRInput)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' GetRRInput is nullptr");

		SetSharedTextures = reinterpret_cast<SetSharedTexturesFn>(GetProcAddress(handle, "SetSharedTextures"));

		if (!SetSharedTextures)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetSharedTextures is nullptr");

		UpdateJitter = reinterpret_cast<UpdateJitterFn>(GetProcAddress(handle, "UpdateJitter"));

		if (!UpdateJitter)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateJitter is nullptr");

		SetPTOutputTargets = reinterpret_cast<SetPTOutputTargetsFn>(GetProcAddress(handle, "SetPTOutputTargets"));

		if (!SetPTOutputTargets)
			logger::warn("[Raytracing] 'CreationEngineRaytracing.dll' SetPTOutputTargets is nullptr (older version?)");
	}
};

struct uint2
{
	uint x;
	uint y;

	bool operator==(const uint2&) const = default;
	bool operator!=(const uint2&) const = default;
};
static_assert(sizeof(uint2) == 8);

struct Raytracing : public OverlayFeature
{
	static constexpr uint SKY_CUBEMAP_SIZE = 256;
	static constexpr uint SKY_HEMI_SIZE = SKY_CUBEMAP_SIZE * 2;

	// Metadata
	virtual inline std::string GetName() override { return "Raytracing"; }
	virtual inline std::string GetShortName() override { return "Raytracing"; }
	virtual inline std::string_view GetCategory() const override { return "Lighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"This is a terse description.",
			{
				"This is a subfeature.",
				"This is another subfeature.",
				"Cheese.",
			}
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return false; }
	virtual inline std::string_view GetShaderDefineName() override { return "RAYTRACING"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	virtual bool IsInMenu() const override { return true; }

	virtual bool IsOverlayVisible() const override { return settings.PerfOverlay != OverlayMode::None; };

	virtual void DrawOverlay() override;

	bool Active() const;

	// Resources
	virtual void SetupResources() override;

	void Load() override;
	void PostPostLoad() override;
	void DataLoaded() override;

	void DrawGeneralSettings();
	void DrawReblurSettings();
	void DrawSHaRCSettings();
	void DrawSSSSettings();
	void DrawAdvancedSettings();
	void DrawReSTIRGISettings();
	void DrawExperimentalSettings();
	void DrawDebugSettings();

	void CompileShaders();

	void InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);
	bool UpdateResolution();
	void UpdateJitter(float2 jitter);
	void UpdateFeatureData();
	void SkyCubeToHemi() const;
	void ConvertTextures();
	void DeferredPasses();
	void GetRayReconstructionInputs(ID3D12Resource*& diffuseAlbedo, ID3D12Resource*& specularAlbedo, ID3D12Resource*& normalRoughness, ID3D12Resource*& specHitDistance);

	CreationEngineRaytracing::Settings GetSettings() const;

	void UpdateSettings();

	inline CreationEngineRaytracing::Mode Mode() const
	{
		return Active() ? settings.CreationEngineRaytracingSettings.GeneralSettings.Mode : CreationEngineRaytracing::Mode::None;
	}

	inline bool IsPathTracing() const
	{
		return Mode() == CreationEngineRaytracing::Mode::PathTracing;
	}

	enum struct OverlayMode
	{
		None,
		Simple,
		Complete
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		OverlayMode PerfOverlay = OverlayMode::None;
		bool ShowMainTexture = false;
		CreationEngineRaytracing::Settings CreationEngineRaytracingSettings;
	} settings;

	ImVec2 Position = ImVec2(10.f, 10.f);
	bool PositionSet = false;

	bool initialized = false;
	bool forcedDisabled = false;

	uint2 m_Resolution;

	enum DisableReason
	{
		None,
		UnsupportedGPU,
		OutdatedDrivers,
		MissingPlugin,
		InitFailed,
	} disableReason = DisableReason::None;

	struct alignas(16) FeatureData
	{
		ExtendedMaterials::Settings ExtendedMaterials;
		WetnessEffects::PerFrame WetnessEffects;
		CloudShadows::Settings CloudShadows;
		HairSpecular::Settings HairSpecular;
		ExtendedTranslucency::PerFrame ExtendedTranslucency;
		LinearLighting::PerFrameData LinearLighting;
	};

	eastl::unique_ptr<FeatureData> featureData;

	struct alignas(16) SharedData
	{
		float InteriorDirectional;
		float Ambient;
		float EnvMap;
		uint Albedo;
		uint PathTracing;
		uint _padding0;
		uint _padding1;
		uint _padding2;
	};
	static_assert(sizeof(SharedData) % 16 == 0);

	SharedData GetCommonBufferData() const;

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;

	eastl::unique_ptr<WrappedResource> mainTexture = nullptr;

	eastl::unique_ptr<WrappedResource> ptDepthTexture = nullptr;
	eastl::unique_ptr<WrappedResource> ptMotionVectorsTexture = nullptr;

	winrt::com_ptr<ID3D12Resource> albedoTexture = nullptr;
	eastl::unique_ptr<WrappedResource> normalRoughnessTexture = nullptr;
	winrt::com_ptr<ID3D12Resource> gnmaoTexture = nullptr;

	eastl::unique_ptr<WrappedResource> diffuseAlbedoTexture = nullptr;

	eastl::unique_ptr<WrappedResource> skyHemisphere = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cubeToHemiCS = nullptr;

	RE::NiPointer<RE::TESWaterReflections> waterReflections = nullptr;

	eastl::unique_ptr<CreationEngineRaytracing> creationEngineRaytracing = nullptr;

	struct alignas(16) ScreenData
	{
		uint2 Resolution;
		uint2 DynamicResolution;
	};
	static_assert(sizeof(ScreenData) % 16 == 0);

	eastl::unique_ptr<ConstantBuffer> screenCB = nullptr;

	eastl::unique_ptr<ScreenData> screenData;

	winrt::com_ptr<ID3D11ComputeShader> ptCompositeCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertTexturesCS[2];
	winrt::com_ptr<ID3D11ComputeShader> giCompositeCS = nullptr;

	struct Hooks
	{
		struct Main_RenderWorld
		{
			static void thunk(bool a1)
			{
				auto& rt = globals::features::raytracing;

				rt.UpdateFeatureData();
				rt.SkyCubeToHemi();

				rt.creationEngineRaytracing->UpdateCamera();

				// Executes the render graph for path tracing, no dependecy on any game render target so we start as early as possible
				if (rt.Mode() == CreationEngineRaytracing::Mode::PathTracing) {
					rt.creationEngineRaytracing->Execute();
				}

				func(a1);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWaterEffects
		{
			static void thunk()
			{
				auto* tes = RE::TES::GetSingleton();
				if (tes->interiorCell) {
					if (tes->interiorCell->cellFlags.none(RE::TESObjectCELL::Flag::kHasWater))
						tes->interiorCell->cellFlags.set(true, RE::TESObjectCELL::Flag::kHasWater);

					globals::features::raytracing.waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty);
				}

				func();
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSImagespaceShaderRefraction_Render
		{
			static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
			{
				auto& rt = globals::features::raytracing;
				if (rt.Active() && rt.Mode() == CreationEngineRaytracing::Mode::PathTracing)
					return;

				func(imageSpaceShader, shape, param);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
			stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
			stl::write_vfunc<0x1, BSImagespaceShaderRefraction_Render>(RE::VTABLE_BSImagespaceShaderRefraction[0]);
		}
	};

	class BGSActorCellEventHandler : public RE::BSTEventSink<RE::BGSActorCellEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*);

		static bool Register()
		{
			static BGSActorCellEventHandler singleton;

			auto* player = RE::PlayerCharacter::GetSingleton();
			player->AsBGSActorCellEventSource()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};

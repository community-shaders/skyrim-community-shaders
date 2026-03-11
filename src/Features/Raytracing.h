#pragma once

#include <directx/d3d12.h>
#include <d3d11_4.h>

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
#include "Features/WetnessEffects.h"
#include "Features/Upscaling.h"

struct CreationEngineRaytracing
{
	enum class Mode
	{
		GlobalIllumination,
		PathTracing
	};

	enum class Denoiser
	{
		None,
		DLSS_RR,
		Other
	};

	struct GeneralSettings
	{
		Denoiser Denoiser = Denoiser::None;
		Mode Mode = Mode::GlobalIllumination;
		bool RaytracedShadows = false;

		bool operator==(const GeneralSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GeneralSettings, Mode, RaytracedShadows)
	};

	struct RaytracingSettings
	{
		int Bounces = 2;
		int SamplesPerPixel = 1;
		bool RussianRoulette = true;
		float TexLODBias = -1.0f;

		bool operator==(const struct RaytracingSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RaytracingSettings, Bounces, SamplesPerPixel, RussianRoulette, TexLODBias)
	};

	struct MaterialSettings
	{
		float2 Roughness = { 0.0f, 1.0f };
		float2 Metalness = { 0.0f, 1.0f };

		bool operator==(const MaterialSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MaterialSettings, Roughness, Metalness)
	};

	struct LightSettings
	{
		float Directional = 1.0f;
		float Point = 1.0f;
		bool LodDimmer = false;

		bool operator==(const LightSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LightSettings, Directional, Point, LodDimmer)
	};

	struct LightingSettings
	{
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;

		bool operator==(const LightingSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LightingSettings, Emissive, Effect, Sky)
	};

	struct SHaRCSettings
	{
		float SceneScale = 1.0f;
		int AccumFrameNum = 10;
		int StaleFrameNum = 64;
		float RadianceScale = 1e3f;
		bool AntifireflyFilter = true;

		bool operator==(const SHaRCSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SHaRCSettings, SceneScale, AccumFrameNum, StaleFrameNum, AntifireflyFilter)
	};


	struct DebugSettings
	{
		bool PathTracingCull = false;

		bool operator==(const DebugSettings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DebugSettings, PathTracingCull)
	};

	struct Settings
	{
		bool Enabled = true;
		GeneralSettings GeneralSettings;
		LightSettings LightSettings;
		LightingSettings LightingSettings;
		RaytracingSettings RaytracingSettings;
		MaterialSettings MaterialSettings;
		SHaRCSettings SHaRCSettings;
		DebugSettings DebugSettings;

		bool operator==(const Settings&) const = default;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, Enabled, GeneralSettings, LightSettings, LightingSettings, RaytracingSettings, MaterialSettings, SHaRCSettings, DebugSettings)
	};

	HMODULE handle = nullptr;

	using InitializeFn = bool (*)(ID3D11Device5*, ID3D12Device5*, ID3D12CommandQueue*, ID3D12CommandQueue*, ID3D12CommandQueue*);
	using UpdateFn = void (*)();
	using ExecuteFn = void (*)();
	using WaitExecutionFn = void (*)();
	using PostExecutionFn = void (*)();
	using GetResolutionFn = void (*)(uint32_t&, uint32_t&);
	using SetResolutionFn = void (*)(uint32_t, uint32_t);
	using SetCopyTargetFn = void (*)(ID3D12Resource*);
	using UpdateFeatureDataFn = void (*)(void*, uint32_t);
	using SetSkyHemisphereFn = void (*)(ID3D12Resource*);
	using GetFrameTimeFn = float* (*)();
	using UpdateSettingsFn = void (*)(Settings);
	using GetRRInputFn = void(*)(ID3D12Resource*&, ID3D12Resource*&, ID3D12Resource*&, ID3D12Resource*&);
	using SetRenderTargetsFn = void(*)(ID3D12Resource*, ID3D12Resource*, ID3D12Resource*);
	using UpdateJitterFn = void(*)(float2);

	InitializeFn Initialize = nullptr;
	UpdateFn Update = nullptr;
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
	SetRenderTargetsFn SetRenderTargets = nullptr;
	UpdateJitterFn UpdateJitter = nullptr;

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

		Initialize = reinterpret_cast<InitializeFn>(GetProcAddress(handle, "Initialize"));

		if (!Initialize)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Initialize is nullptr");

		Update = reinterpret_cast<UpdateFn>(GetProcAddress(handle, "Update"));

		if (!Update)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Update is nullptr");

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

		SetRenderTargets = reinterpret_cast<SetRenderTargetsFn>(GetProcAddress(handle, "SetRenderTargets"));

		if (!SetRenderTargets)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetRenderTargets is nullptr");

		UpdateJitter = reinterpret_cast<UpdateJitterFn>(GetProcAddress(handle, "UpdateJitter"));

		if (!UpdateJitter)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateJitter is nullptr");	
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

	virtual bool IsOverlayVisible() const override { return settings.PerfOverlay; };

	virtual void DrawOverlay() override;

	bool Active();

	// Resources
	virtual void SetupResources() override;

	void Load() override;
	void PostPostLoad() override;
	void DataLoaded() override;

	void DrawGeneralSettings();
	void DrawSHaRCSettings();
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

	void SetUpscaler(Upscaling::UpscaleMethod method);

	WrappedResource* GetDiffuseAlbedoTexture();

	inline CreationEngineRaytracing::Mode Mode() const
	{
		return settings.CreationEngineRaytracingSettings.GeneralSettings.Mode;
	}

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool PerfOverlay = true;
		bool ShowMainTexture = false;
		CreationEngineRaytracing::Settings CreationEngineRaytracingSettings;
	} settings;

	ImVec2 Position = ImVec2(10.f, 10.f);
	bool PositionSet = false;

	bool initialized = false;
	bool forcedDisabled = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	bool pixCapture = false;
	bool pixCaptureStarted = false;

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

	float* frameTime;

	struct Hooks
	{
		struct Main_RenderWorld
		{
			static void thunk(bool a1)
			{
				auto& rt = globals::features::raytracing;

				rt.UpdateFeatureData();
				rt.SkyCubeToHemi();

				rt.creationEngineRaytracing->Update();

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

		static void Install() 
		{
			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
			stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
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
#pragma once

struct PhysicalSky final : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	static PhysicalSky* GetSingleton()
	{
		static PhysicalSky singleton;
		return &singleton;
	}

	// Metadata
	inline std::string GetName() override { return "Physical Sky"; }
	inline std::string GetShortName() override { return "PhysicalSky"; }
	inline std::string_view GetCategory() const override { return "Sky"; }
	inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Physically based sky models for photorealistic sky gradients, plus other astronomical effects.",
			{
				"Sky.",
				"Cheese.",
			}
		};
	}

	// Functionality
	bool inline SupportsVR() override { return true; }
	inline std::string_view GetShaderDefineName() override { return "PHYSICAL_SKY"; }
	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	// Settings & UI
	void DataLoaded() override;
	void RestoreDefaultSettings() override;
	void LoadSettings(json& o_json) override;
	void SaveSettings(json& o_json) override;

	void DrawSettings() override;
	void SettingsGeneral();
	void SettingsCelestials();
	void SettingsAtmosphere();
	void SettingsClouds();
	void SettingsDebug();

	// Resources
	void SetupResources() override;
	void ClearShaderCache() override;
	void CompileShaders();
	bool ShadersOK();

	// Draw
	void Reset() override;
	void EarlyPrepass() override;
	void ReflectionsPrepass() override;
	void Prepass() override;
	void GenerateLuts();
	void AccumShadow();
	inline void PostPostLoad() override { Hooks::Install(); }

	////////////////////////////////////////////////// Feature Specific Data
	constexpr static uint16_t kTrLutW = 256;
	constexpr static uint16_t kTrLutH = 64;
	constexpr static uint16_t kMsLutW = 32;
	constexpr static uint16_t kMsLutH = 32;
	constexpr static uint16_t kSvLutW = 200;
	constexpr static uint16_t kSvLutH = 150;
	constexpr static uint16_t kApLutW = 32;
	constexpr static uint16_t kApLutH = 32;
	constexpr static uint16_t kApLutD = 32;

	struct WorldspaceInfo
	{
		float zBottom = -14500.f;
	};

	struct Settings
	{
		bool enabled = true;
		bool overrideWhitelist = false;
		bool overrideDirLight = false;
		int tonemapper = 2;
		float vanillaMix = 0;
		float trMix = 0;
		float apLumMix = 1;
		float apTrMix = 1;

		float2 cloudShadowRemapRange = float2{ 0, 1.f };

		float3 sunlightColor = float3{ 1.0f, 0.97f, 0.95f } * 1e3f;
		float3 masserColor = float3{ 1.0f, 0.6f, 0.6f } * 5e-3f;
		float3 secundaColor = float3{ 0.8f, 1.0f, 1.0f } * 5e-3f;

		float adaptationStart = DirectX::XMConvertToRadians(-2);
		float adaptationEnd = DirectX::XMConvertToRadians(-15);
		float dayExposure = 1e-2f;
		float nightExposure = 1e2f;

		std::map<std::string, WorldspaceInfo> worldspaceWhitelist = {
			{ "Tamriel", { -14500.f } },
			{ "WindhelmWorld", { -14500.f } },
			{ "RiftenWorld", { -14500.f } },
			{ "MarkarthWorld", { -14500.f } },
			{ "WhiterunWorld", { -14500.f } },
			{ "SolitudeWorld", { -14500.f } },
			{ "WhiterunDragonsreachWorld", { -14500.f } },
			{ "DLC01FalmerValley", { 3000.f } },
			{ "DLC2SolstheimWorld", { 256.f } }
		};
		float3 groundAlbedo = { .2f, .2f, .2f };

		float rayleighFalloff = 1 / 8.69645f;                    // in km^-1
		float3 rayleighScatter = { 6.6049f, 12.345f, 29.413f };  // in megameter^-1
		float aerosolFalloff = 1 / 1.2f;
		float aerosolPhaseG = 0.8f;
		float3 aerosolScatter = { 39.96f, 39.96f, 39.96f };
		float3 aerosolAbsorption = { 4.44f, 4.44f, 4.44f };
		float ozoneAltitude = 22.3499f + 35.66071f * .5f;  // in km
		float ozoneThickness = 35.66071f;
		float3 ozoneAbsorption = { 2.2911f, 1.5404f, 0 };

		float cloudRelightMix = 1.f;
		float cloudOriginalMix = 0.5f;
		float silverLiningMix = 1.f;
		float silverLiningSpread = 0.f;
	} settings;

	struct CbData
	{
		// DYNAMIC
		float2 texDim;
		float2 rcpTexDim;  //
		float2 frameDim;
		float2 rcpFrameDim;  //

		float zCameraPlanet;
		float3 sunDir;  //
		float3 sunlightColor;
		float trMix;  //
		float3 masserDir;
		float apLumMix;  //
		float3 masserColor;
		float apTrMix;  //
		float3 secundaDir;
		float _pad3;  //
		float3 secundaColor;

		// GENERAL
		uint enabled;  //
		int tonemapper;
		float vanillaMix;

		// WORLD
		float zBottom;
		float rPlanet;  //
		float rAtmosphere;
		float3 groundAlbedo;  //

		// ATMOSPHERE
		float2 cloudShadowRemapRange;

		float aerosolFalloff;
		float aerosolPhaseG;  //
		float3 aerosolScatter;
		float _pad5;  //
		float3 aerosolAbsorption;

		float rayleighFalloff;
		float3 rayleighScatter;  //

		float ozoneAltitude;  //
		float ozoneThickness;
		float3 ozoneAbsorption;  //

		// CLOUDS (VANILLA)
		float cloudRelightMix;
		float cloudOriginalMix;
		float silverLiningMix;
		float silverLiningSpread;  //
	} cbData;
	static_assert(sizeof(CbData) % 16 == 0);

	eastl::unique_ptr<Texture2D> texTrLut = nullptr;  // transmittance
	eastl::unique_ptr<Texture2D> texMsLut = nullptr;  // multiscattering
	eastl::unique_ptr<Texture2D> texSvLut = nullptr;  // sky view
	eastl::unique_ptr<Texture3D> texApLut = nullptr;  // aerial perspective
	eastl::unique_ptr<Texture2D> texApShadow = nullptr;

	winrt::com_ptr<ID3D11SamplerState> sampTr = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampSv = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampNoise = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> csTrLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csMsLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csSvLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csApLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csShadowAccum = nullptr;

	ID3D11SamplerState* originalPSSamplers[2] = { nullptr, nullptr };

	void ModifySky();
	void RestoreSamplers();
	struct Hooks
	{
		struct BSSkyShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSSkyShader_RestoreGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);
			stl::write_vfunc<0x7, BSSkyShader_RestoreGeometry>(RE::VTABLE_BSSkyShader[0]);
		}
	};
};
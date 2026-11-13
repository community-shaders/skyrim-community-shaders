#pragma once

#include "Utils/TexturePreview.h"

#include "PhysicalSky/Aerosol.h"
#include "PhysicalSky/CloudNoiseGenerator.h"
#include "PhysicalSky/Lightning.h"
#include "PhysicalSky/LocalNdf.h"
#include "PhysicalSky/Ndf.h"
#include "PhysicalSky/Storm.h"
#include "PhysicalSky/VoxelCloud.h"

struct PhysicalSky final : public Feature
{
	////////////////////////////////////////////////// Boilerplate

	// Metadata
	inline std::string GetName() override { return "Physical Sky"; }
	std::string GetDisplayName() override { return T("feature.physical_sky.name", "Physical Sky"); }
	inline std::string GetShortName() override { return "PhysicalSky"; }
	inline std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.physical_sky.description", "Physically-based sky model for realistic sky gradients and other astronomical effects."),
			{
				T("feature.physical_sky.key_feature_1", "Physically-based atmosphere and aerial perspective."),
				T("feature.physical_sky.key_feature_2", "Procedural sun disk and celestial lighting controls."),
				T("feature.physical_sky.key_feature_3", "Worldspace whitelist and interior override support."),
				T("feature.physical_sky.key_feature_4", "Cloud relighting and silver lining controls."),
			}
		};
	}

	// Functionality
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
	void SettingsVolumetricClouds();
	void SettingsWeatherEffects();
	void DrawCloudStatus();
	void DrawCloudLayerProfile();
	void DrawWeatherMapSection();
	/// Show an optional inline map, or the contents of the Editor's live map window.
	void DrawCloudWeatherPreview(bool middle, bool standalone = false);
	/// Draw global noise and texture inputs shared by the cloud layers.
	void DrawCloudResourcesSection();
	void DrawLowCloudSection();
	void DrawCloudMotionSection();
	void DrawCloudStormSection();
	void DrawCloudLightingSection();
	void DrawCirrusSection();
	void DrawCloudQualitySection();
	void DrawVanillaCloudSection();
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
		bool enableAllExteriorCells = false;
		bool forceEnableAllInteriorCells = false;
		bool overrideDirLight = true;
		bool lightSkyStatics = true;
		float skyStaticsBrightness = 1.0f;
		bool halfResApShadow = false;
		float vanillaMix = 0;
		float trMix = 1;
		float apLumMix = 1;
		float apTrMix = 1;

		float2 cloudShadowRemapRange = float2{ 0, 1.f };

		// Linear RGB illuminance before extinction; working-gamut Y is in lux.
		float3 sunlightColor = float3{ 1.0f, 0.97f, 0.95f } * 10.f;
		float3 masserColor = float3{ 1.0f, 0.6f, 0.6f } * 0.1f;
		float3 secundaColor = float3{ 0.8f, 1.0f, 1.0f } * 0.05f;

		bool proceduralSun = true;
		float sunDiskRad = DirectX::XMConvertToRadians(0.53f);

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
		float fallbackZBottom = 0.f;
		float3 groundAlbedo = { .2f, .2f, .2f };

		float planetRadius = 6.36e3f;      // in km
		float atmosphereRadius = 6.42e3f;  // in km

		float rayleighFalloff = 1 / 8.69645f;                         // in km^-1
		float3 rayleighScatter = { 6.6049f, 12.345f, 29.413f };       // in megameter^-1 (sRGB band-averaged)
		float3 rayleighScatterAP1 = { 6.9344f, 12.0203f, 28.9100f };  // in megameter^-1 (AP1 band-averaged via CIE 1931 spectral integration)
		Aerosol::Type aerosolType = Aerosol::Type::Custom;
		float aerosolLoading = 1.f;
		float aerosolHumidity = 50.f;
		float aerosolFalloff = 1 / 1.2f;
		float aerosolPhaseG = 0.8f;
		float3 aerosolScatter = { 39.96f, 39.96f, 39.96f };
		float3 aerosolAbsorption = { 4.44f, 4.44f, 4.44f };
		float ozoneAltitude = 22.3499f + 35.66071f * .5f;  // in km
		float ozoneThickness = 35.66071f;
		float3 ozoneAbsorption = { 2.2911f, 1.5404f, 0 };        // sRGB band-averaged
		float3 ozoneAbsorptionAP1 = { 2.2499f, 1.6602f, 0.0f };  // AP1 band-averaged via CIE 1931 spectral integration

		// VANILLA CLOUDS
		bool enableVanillaClouds = true;
		float cloudRelightMix = 1.f;
		float cloudOriginalMix = 0.5f;
		float silverLiningMix = 1.f;
		float silverLiningSpread = 0.f;

		// VOLUMETRIC CLOUDS
		bool enableVolumetricClouds = false;
		float lightningIntensity = 0.f;  // zero restores vanilla weather lightning
		struct LightningSettings
		{
			float rate = 3.f;                       // requested discharges per minute
			float2 distanceRange = { 0.35f, 7.f };  // horizontal kilometres
			float radius = 600.f;                   // equivalent luminous cloud radius, metres
			float cloudLuminance = 1000.f;          // peak cd/m^2
			float channelLuminance = 1.0e7f;        // peak cd/m^2
			float channelRadius = 0.12f;            // metres
			int strokeCount = 3;
			float strokeDuration = 20.f;  // milliseconds, full optical pulse duration
			float strokeInterval = 90.f;  // milliseconds between stroke starts
			float strokeDecay = 0.65f;
			float groundChance = 35.f;            // percent
			float cloudBaseChance = 35.f;         // percent; the remainder is embedded cloud lightning
			float3 color = { 0.86f, 0.9f, 1.f };  // linear sRGB chromaticity
		};
		LightningSettings lightning;
		float rayMarchRange = 32.f;
		float shadowVolumeRange = 8.f;  // km
		float marchStepScale = 0.5f;
		int cloudBlurRadius = 1;
		NdfSettings cloudMap = {};
		CloudLayer cloudLayer = {};
		CloudNoiseSettings cloudNoise = {};
		SceneBlend voxelClouds;

		Aerosol::Optics GetAerosolOptics() const;
		void ApplyAerosolOptics();
		/// Validates the lightning controls after loading or scene interpolation.
		void SanitizeLightning();
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
		float sunDiskCos;  //
		float3 secundaColor;

		// GENERAL
		uint enabled;  //
		float pad;
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
		uint halfResApShadow;  //
		float3 aerosolAbsorption;

		float rayleighFalloff;
		float3 rayleighScatter;  //

		float ozoneAltitude;  //
		float ozoneThickness;
		float3 ozoneAbsorption;  //

		// CLOUDS (VANILLA)
		uint enableVanillaClouds;
		float cloudRelightMix;
		float cloudOriginalMix;
		float silverLiningMix;  //
		float silverLiningSpread;

		// VOLUMETRIC CLOUDS (toggle + shadow-volume parameters for GetDirlightTransmittance)
		uint enableVolumetricClouds;
		float shadowVolumeRange;
		float lowestCloudAltitude;  //
		float highestCloudAltitude;
		float3 volCloudScatter;  //
		uint volCloudUseSun;
		float3 volCloudAbsorption;  //
		float volCloudLowBottom;
		float volCloudLowThickness;

		// SETTINGS
		uint lightSkyStatics;
		float skyStaticsBrightness;  //
		float4 lightningPositionRadius;
		float4 lightningEndPosition;
		float4 lightningColorIntensity;
		float4 lightningControl;         // generation, mask history valid, segment count, ownership
		float4 lightningPhotometry;      // cloud and channel cd/m^2, channel radius in m, cloud-base mode
		float4 lightningPlacement;       // pending query: distance bounds in game units, seed, base mode
		float4 lightningChannelEmitter;  // centroid and equivalent projected-area radius, game units
		float4 lightningPreparationPositionRadius;
		float4 lightningPreparationControl;  // base mode, history valid, active mask lane, preparing
	} cbData;
	STATIC_ASSERT_ALIGNAS_16(CbData);

	eastl::unique_ptr<Texture2D> texTrLut = nullptr;  // transmittance
	eastl::unique_ptr<Texture2D> texMsLut = nullptr;  // multiscattering
	eastl::unique_ptr<Texture2D> texSvLut = nullptr;  // sky view
	eastl::unique_ptr<Texture2D> texSkyTail = nullptr;
	eastl::unique_ptr<Texture3D> texApLut = nullptr;
	eastl::unique_ptr<Texture2D> texApShadow = nullptr;

	// Volumetric cloud resources
	constexpr static uint16_t kShadowVolW = 256;
	constexpr static uint16_t kShadowVolH = 256;
	constexpr static uint16_t kShadowVolD = 64;
	constexpr static uint16_t kShadowMeanHeights = 64;
	constexpr static uint16_t kVolCubeSize = 64;
	constexpr static uint32_t kCloudBoundaryCells = 256;
	eastl::unique_ptr<Texture2D> texCloudBoundary = nullptr;
	eastl::unique_ptr<Texture2D> texCloudHeightBounds = nullptr;
	eastl::unique_ptr<Texture2D> texCloudHeightBoundsTiles = nullptr;
	eastl::unique_ptr<Texture2D> texCloudWeatherBounds = nullptr;
	eastl::unique_ptr<Texture2D> texStormTiles = nullptr;
	winrt::com_ptr<ID3D11Buffer> cloudBoundaryIndices = nullptr;
	winrt::com_ptr<ID3D11BlendState> cloudBoundaryBlend = nullptr;
	winrt::com_ptr<ID3D11RasterizerState> cloudBoundaryRasterizer = nullptr;
	winrt::com_ptr<ID3D11DepthStencilState> cloudBoundaryDepth = nullptr;
	winrt::com_ptr<ID3D11VertexShader> vsCloudBoundary = nullptr;
	winrt::com_ptr<ID3D11PixelShader> psCloudBoundary = nullptr;
	struct alignas(16) CloudBoundaryCB
	{
		float4 gridOriginSpacing;
		float4 fieldFrequencyWind;
		float4 shearAltitude;
		float4 evolution;
		float4 frameDimensions;
		float cloudCurvatureRadius;
		float bottomZ;
		uint gridCellCount;
		uint cloudFrameIndex;
		float4 localRect;
		float4 localAltitude;
	};
	static_assert(sizeof(CloudBoundaryCB) == 128);
	eastl::unique_ptr<ConstantBuffer> cloudBoundaryCB = nullptr;

	eastl::unique_ptr<Texture2D> texVolTr = nullptr;           // full-resolution volumetric transmittance result
	eastl::unique_ptr<Texture2D> texVolLum = nullptr;          // full-resolution volumetric luminance result
	eastl::unique_ptr<Texture2D> texVolAux = nullptr;          // full-resolution cloud depth/metadata
	eastl::unique_ptr<Texture2D> texVolLowTr = nullptr;        // quarter-resolution trace transmittance
	eastl::unique_ptr<Texture2D> texVolLowLum = nullptr;       // quarter-resolution trace luminance
	eastl::unique_ptr<Texture2D> texVolLowAux = nullptr;       // quarter-resolution trace depth/metadata
	eastl::unique_ptr<Texture2D> texVolFilteredTr = nullptr;   // blurred transmittance for compositing
	eastl::unique_ptr<Texture2D> texVolFilteredLum = nullptr;  // blurred luminance for compositing
	eastl::unique_ptr<Texture2D> texVolFilteredAux = nullptr;
	eastl::unique_ptr<Texture2D> texVolHistoryTr = nullptr;
	eastl::unique_ptr<Texture2D> texVolHistoryLum = nullptr;
	eastl::unique_ptr<Texture2D> texVolHistoryAux = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeTr = nullptr;   // low-resolution cubemap transmittance result
	eastl::unique_ptr<Texture2D> texVolCubeLum = nullptr;  // low-resolution cubemap luminance result
	eastl::unique_ptr<Texture2D> texVolCubeAux = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeHistoryTr = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeHistoryLum = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeHistoryAux = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeTraceTr = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeTraceLum = nullptr;
	eastl::unique_ptr<Texture2D> texVolCubeTraceAux = nullptr;
	SkyLightning lightning;
	eastl::unique_ptr<Texture2D> texLightningTrace, texLightningMask, texLightningHistory;
	eastl::unique_ptr<Texture2D> texLightningCubeTrace, texLightningCubeMask, texLightningCubeHistory;
	eastl::unique_ptr<Texture2D> texLightningChannels, texLightningCubeLum;
	winrt::com_ptr<ID3D11ComputeShader> csLightningQuery, csLightningComposite, csLightningCubeComposite;
	eastl::unique_ptr<Texture3D> texShadowVolume = nullptr;  // cloud shadow volume 3D
	eastl::unique_ptr<Texture3D> texShadowVolumeRaw = nullptr;
	eastl::unique_ptr<Texture2D> texCloudShadowMean = nullptr;

	// ImGui::Image cannot sample a cubemap or a volume texture, so those are shown
	// through a 2D view built from the resource itself.
	ankerl::unordered_dense::map<ID3D11Resource*, winrt::com_ptr<ID3D11ShaderResourceView>> debugCubeFaceSrvs;
	int32_t debugCubeFace = 0;
	eastl::unique_ptr<Texture2D> debugApSlice = nullptr;
	TexturePreview texturePreview;
	std::array<bool, 2> showCloudWeatherPreview{};
	std::array<int, 2> cloudWeatherPreviewChannel{};
	void DrawDebugCloudTexture(ID3D11ShaderResourceView* srv, const char* id, const char* label, float scale, bool scalar = false);
	ID3D11ShaderResourceView* GetDebugCubeFaceSrv(Texture2D* a_texture);
	ID3D11ShaderResourceView* GetDebugVolumeSliceSrv(ID3D11ShaderResourceView* a_srv, eastl::unique_ptr<Texture2D>& a_target);
	void DrawDebugCube(Texture2D* a_texture, const char* a_label, float a_scale);
	void DrawDebugVolume(ID3D11ShaderResourceView* a_srv, const char* a_label, eastl::unique_ptr<Texture2D>& a_target, float a_scale);

	winrt::com_ptr<ID3D11ShaderResourceView> baseShapeNoiseSrv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> cloudProfileLutSrv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> cloudAdjustmentLutSrv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> importedShapeNoiseSrv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> importedAdjustmentLutSrv = nullptr;
	CloudNoiseGenerator cloudNoiseGenerator;
	bool cloudProfileGenerated = true;
	bool cloudAdjustmentGenerated = false;
	TextureManager ndfTexManager{ "Cloud Map" };
	bool SetLocalCloudState(const LocalNdfState& state);
	NdfManager ndfManager;
	LocalNdfManager localNdfManager;
	CirrusMapManager cirrusMapManager;
	MidCloudMapManager midCloudMapManager;

	static constexpr uint32_t kCloudMsCalibrationSize = 49;

	// Volumetric cloud constants (compute-only)
	struct VolumetricCloudCB
	{
		float rayMarchRange;
		float shadowVolumeRange;
		float marchStepScale;
		uint cloudFrameIndex;

		float2 rcpFrameDim;
		float2 activeFrameDim;
		float2 lowFrameDim;
		float2 previousFrameDim;

		float3 dirlightDir;
		float bottomZ;
		float3 previousCamera;
		float planetRadius;

		float lowestCloudAltitude;
		float highestCloudAltitude;
		float lowCloudBaseAltitude;
		float lowCloudTopAltitude;

		float lowCloudTraceBottomAltitude;
		float lowCloudTraceTopAltitude;
		float cloudCurvatureRadius;
		float cloudEvolutionDelta;

		float shadowVolumeBottom;
		float shadowVolumeTop;
		uint historyValid;
		uint cirrusEnabled;

		float2 lowNdfFrequency;
		float2 noiseWindOffset;
		float2 cloudWindDelta;
		float2 cloudShapeShear;
		float2 previousShapeShear;
		float2 cirrusWindOffset;

		float2 cirrusWindDelta;
		float cirrusAltitude;
		float cirrusUvInvSize;

		float cirrusDensityScale;
		float cirrusLightThickness;
		float phaseForwardG;
		float phaseBackwardG;

		float lowDensityScale;
		float coverageBottomPower;
		float coverageHeightRange;
		float bottomDensityPower;

		float bottomDensityWidth;
		float topExpansion;
		float scatterDensityThreshold;
		float scatterDensityPower;

		float cloudLightingScale;
		float sunExtinction;
		float viewExtinction;
		float ambientStrength;

		float msAlbedo;
		float msStrength;
		float msBoundaryWidth;
		float msTransportExtinction;

		float4 cloudEvolution;
		float4x4 previousViewProj;

		float4 ndfBoundaryRect;
		float4 localNdfRect;
		float4 localNdfAltitude;
		float4 localNdfAuxiliary;
		float4 lowCloudPhase;
		uint32_t stormCount;
		uint32_t voxelCount;
		float2 voxelAltitude;
		uint32_t skipShapeNoise;
		uint32_t useVoxelNoise;
		float2 noisePadding;  // Previous curvature radius, weather-history refresh weight.
		float4 middleLayer;
		float4 middleShape;
		std::array<float4, kCloudMsCalibrationSize> msCalibration;
		float4 middleCells;
		float4 middleBands;
		float4 middleBandShape;
	};
	static_assert(sizeof(VolumetricCloudCB) == 1328);
	static_assert(offsetof(VolumetricCloudCB, dirlightDir) == 48);
	static_assert(offsetof(VolumetricCloudCB, previousCamera) == 64);
	static_assert(offsetof(VolumetricCloudCB, lowNdfFrequency) == 128);
	static_assert(offsetof(VolumetricCloudCB, cirrusWindDelta) == 176);
	static_assert(offsetof(VolumetricCloudCB, cloudLightingScale) == 240);
	static_assert(offsetof(VolumetricCloudCB, cloudEvolution) == 272);
	static_assert(offsetof(VolumetricCloudCB, previousViewProj) == 288);
	static_assert(offsetof(VolumetricCloudCB, localNdfRect) == 368);
	static_assert(offsetof(VolumetricCloudCB, lowCloudPhase) == 416);
	static_assert(offsetof(VolumetricCloudCB, stormCount) == 432);
	static_assert(offsetof(VolumetricCloudCB, middleLayer) == 464);
	static_assert(offsetof(VolumetricCloudCB, msCalibration) == 496);
	static_assert(offsetof(VolumetricCloudCB, middleCells) == 1280);
	static_assert(offsetof(VolumetricCloudCB, middleBands) == 1296);
	static_assert(offsetof(VolumetricCloudCB, middleBandShape) == 1312);
	eastl::unique_ptr<ConstantBuffer> volCloudCb = nullptr;
	eastl::unique_ptr<ConstantBuffer> cloudDispatchCb;
	eastl::unique_ptr<Texture2D> texVolStormMotion, texVolCubeStormMotion;
	eastl::unique_ptr<Texture2D> texVoxelTraceTr, texVoxelTraceLum, texVoxelTraceAux, texVoxelTraceMotion, texVoxelTraceLightning;

	eastl::unique_ptr<Texture2D> texVolCloudAmbientSH = nullptr;
	eastl::unique_ptr<Texture2D> texCloudAmbientPartial;
	eastl::unique_ptr<Texture2D> texCloudAmbientLighting = nullptr;
	static constexpr uint32_t kCloudAmbientVolumeWidth = 32;
	static constexpr uint32_t kCloudAmbientVolumeDepth = 16;
	static constexpr uint32_t kCloudAmbientCascadeCount = 4;
	eastl::unique_ptr<Texture3D> texCloudAmbientDensity;
	std::array<std::array<eastl::unique_ptr<Texture3D>, 2>, kCloudAmbientCascadeCount> texCloudAmbientSource;
	std::array<eastl::unique_ptr<Texture3D>, kCloudAmbientCascadeCount> texCloudAmbientView;
	eastl::unique_ptr<Texture3D> texCloudAmbientSweep;
	std::array<eastl::unique_ptr<Texture2D>, 2> texCloudAmbientLayout;
	eastl::unique_ptr<Texture2D> texCloudAmbientBoundary;
	std::array<uint32_t, kCloudAmbientCascadeCount> cloudAmbientIterations = {};
	bool cloudAmbientValid = false;
	std::string cloudAmbientWorldspace;
	uint32_t cloudAmbientUpdateIndex = 0;
	struct CloudAmbientVolumeCB
	{
		float4 phase;
		uint32_t cascade;
		uint32_t width;
		uint32_t reset;
		uint32_t iterations;
	};
	static_assert(sizeof(CloudAmbientVolumeCB) == 32);
	eastl::unique_ptr<ConstantBuffer> cloudAmbientVolumeCB = nullptr;
	uint32_t volFrameIndex = 0;
	bool volMainHistoryValid = false;
	bool cloudHeightBoundsDirty = true;
	bool cloudWeatherChanged = false;
	bool volHistoryUnifiedMedium = false;
	float2 volHistoryFrameDim = {};
	float volHistoryCurvatureRadius = 0.f;
	struct CloudWindState
	{
		std::array<double, 2> lowOffset = {};
		std::array<double, 2> highOffset = {};
		double phase = 0.0;
		float2 shear = {};
		float disturbance = 0.f;
		double stormTime = 0.0;
	};
	CloudWindState volWind;
	CloudWindState volHistoryWind;
	float4x4 volHistoryViewProj = {};
	float3 volHistoryCamera = {};
	std::string volCloudSettingsKey;
	std::string volCloudHistoryKey;

	// Static low-cloud cross-section shown on the Clouds page.
	static constexpr uint32_t kCloudCrossSectionW = 512;
	static constexpr uint32_t kCloudCrossSectionH = 160;
	eastl::unique_ptr<Texture2D> texCloudCrossSection = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudCrossSection = nullptr;
	eastl::unique_ptr<ConstantBuffer> cloudCrossSectionCB = nullptr;
	bool cloudCrossSectionDirty = true;
	bool cloudCrossSectionReady = false;
	float2 cloudCrossSectionCamera = {};
	struct alignas(16) CloudCrossSectionCB
	{
		float4 field;       // frequency X/Y, base altitude (m), top altitude (m)
		float4 cameraSpan;  // camera X/Y (m), NDF span X/Y (m)
		float4 shape;       // bottom spread, spread height, softness, softness height
		float4 shape2;      // top expansion, density scale, unused, unused
	};
	static_assert(sizeof(CloudCrossSectionCB) == 64);

	winrt::com_ptr<ID3D11ComputeShader> csVolMainView = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csUpperView = nullptr, csUpperSunDepth = nullptr;
	eastl::unique_ptr<Texture2D> texUpperSunDepth;
	static constexpr uint32_t kMiddleBoundsSize = 32, kMiddleBoundsLevels = 6;
	eastl::unique_ptr<Texture2D> texMiddleBounds;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kMiddleBoundsLevels> middleBoundsSrvs;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMiddleBoundsLevels> middleBoundsUavs;
	winrt::com_ptr<ID3D11ComputeShader> csMiddleBounds, csMiddleBoundsReduce;
	winrt::com_ptr<ID3D11ComputeShader> csUpperTiles, csUpperViewTiles;
	eastl::unique_ptr<Texture2D> texUpperTiles;
	bool middleBoundsDirty = true;
	winrt::com_ptr<ID3D11ComputeShader> csVolMainWithoutStorm = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolMainWeather = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csStormTiles = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolMainVolcanic, csVolCubemapVolcanic, csVolShadowVolcanic, csCloudAmbientDensityVolcanic;
	winrt::com_ptr<ID3D11ComputeShader> csVolMainSingleVolcanic;
	winrt::com_ptr<ID3D11ComputeShader> csVoxelView, csVoxelViewSingle, csVolCubemapVoxel, csVolShadowVoxel, csCloudAmbientDensityVoxel, csVoxelLighting;
	winrt::com_ptr<ID3D11ComputeShader> csVolFilter = nullptr, csVolFilterWide = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolReproject = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolCubeReproject = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolShadowVolume = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolShadowWeather, csVolShadowColumns, csCloudShadowMeanWeather;
	winrt::com_ptr<ID3D11ComputeShader> csVolShadowResample = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudShadowMean, csCloudShadowMeanVolcanic, csCloudShadowMeanVoxel;
	winrt::com_ptr<ID3D11ComputeShader> csVolCubemap = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVolAmbientSH = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientReduce;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientLighting = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientDensity = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientBoundary = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientSweep = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientSweepFar = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientProject = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientLayout = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudAmbientView = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudHeightBounds = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudHeightBoundsReduce = nullptr;

	winrt::com_ptr<ID3D11SamplerState> sampTileable = nullptr;

	// Volumetric cloud methods
	enum class VolumetricCloudPass
	{
		kShadowVolume,
		kMainViewAndCubemap
	};
	void SetupVolumetricResources();
	void CompileVolumetricShaders();
	void RenderCloudBoundary(const CloudBoundaryCB& a_data, NdfTextureSet a_textures);
	void LoadCloudTextures();
	void UpdateCloudWind();
	/// Resolves the worldspace-scoped storm and rejects invalid authored parameters.
	void UpdateCloudStorm(const std::string& worldspace);
	/// Identifies geometry owned by the configured reference while its cloud replacement is ready.
	bool IsReplacedSkyGeometry(const RE::BSGeometry* geometry) const;
	CloudStormManager cloudStormManager;
	VoxelCloudManager voxelCloudManager;
	void RenderVolumetricClouds(VolumetricCloudPass a_pass);
	/// Rebuilds the low-cloud slab preview used by DrawCloudLayerProfile().
	void RenderCloudCrossSection();
	void GenerateSkyTail();

	winrt::com_ptr<ID3D11SamplerState> sampTr = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampSv = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampNoise = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> csTrLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csMsLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csSvLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csSkyTailGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csApLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csShadowAccum = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csShadowAccumHalfRes = nullptr;

	ID3D11SamplerState* originalPSSamplers[2] = { nullptr, nullptr };
	winrt::com_ptr<ID3D11Buffer> originalVSSkyFeatureBuffer = nullptr;
	winrt::com_ptr<ID3D11SamplerState> originalPSGrassSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> originalPSWaterSampler = nullptr;
	uint32_t originalPSWaterSamplerModifiedBits = 0;

	void ModifySky();
	void RestoreSamplers();
	void ModifyGrass();
	void RestoreGrassSampler();
	void ModifyWater();
	void RestoreWaterSampler();
	struct Hooks
	{
		struct BSCullingProcess_AppendVirtual
		{
			static void thunk(RE::BSCullingProcess* process, RE::BSGeometry& geometry, std::int32_t alphaGroupIndex);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShaderProperty_GetRenderPasses
		{
			static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSEffectShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderMode, RE::BSShaderAccumulator* accumulator);
			static inline REL::Relocation<decltype(thunk)> func;
		};

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

		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSGrassShader_RestoreGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSWaterShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSWaterShader_RestoreGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			SkyLightning::InstallHooks();
			stl::write_vfunc<0x18, BSCullingProcess_AppendVirtual>(RE::VTABLE_BSCullingProcess[0]);
			stl::write_vfunc<0x2A, BSEffectShaderProperty_GetRenderPasses>(RE::VTABLE_BSEffectShaderProperty[0]);
			stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);
			stl::write_vfunc<0x7, BSSkyShader_RestoreGeometry>(RE::VTABLE_BSSkyShader[0]);
			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
			stl::write_vfunc<0x7, BSGrassShader_RestoreGeometry>(RE::VTABLE_BSGrassShader[0]);
			stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
			stl::write_vfunc<0x7, BSWaterShader_RestoreGeometry>(RE::VTABLE_BSWaterShader[0]);
		}
	};
};

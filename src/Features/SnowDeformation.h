#pragma once

#include "Buffer.h"

#include <d3d11_1.h>
#include <deque>
#include <intrin.h>
#include <list>

/** @brief Util::CompileShader for a snow shader's main(); null defines are skipped, and a_blob receives the bytecode. */
ID3D11DeviceChild* SD_CompileShader(const wchar_t* a_path, const char* a_target, std::initializer_list<const char*> a_defines = {}, winrt::com_ptr<ID3DBlob>* a_blob = nullptr);
uint32_t SD_PositionBytes(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc);
/** @brief A one-mip 2D texture with an SRV, UAV and RTV for each of those bind flags in a_bind. */
Texture2D* SD_MakeTexture2D(UINT a_width, UINT a_height, DXGI_FORMAT a_format, const char* a_name, UINT a_bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

struct SnowDeformation : Feature
{
public:
	virtual inline std::string GetName() override { return "Snow Deformation"; }
	virtual std::string GetDisplayName() override { return T("feature.snow_deformation.name", "Snow Deformation"); }
	virtual inline std::string GetShortName() override { return "SnowDeformation"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SNOW_DEFORMATION"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief The lighting shader samples the deformation map on landscape draws. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::Lighting;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.snow_deformation.description", "Maintains a persistent deformation map around the player so snow can be visibly compressed by actors moving through it, leaving lasting trails."),
			{ T("feature.snow_deformation.key_feature_1", "Persistent world-space deformation map following the player"),
				T("feature.snow_deformation.key_feature_2", "Trails carved by the player, NPCs and creatures"),
				T("feature.snow_deformation.key_feature_3", "Snowfall-driven snow refill"),
				T("feature.snow_deformation.key_feature_4", "Compute-shader based, low performance impact") } };
	};

	static constexpr const char* kAttribution =
		"Snow Deformation for Community Shaders - (c) 2026 josefplyr1 - GPL-3.0-or-later - github.com/community-shaders/skyrim-community-shaders/pull/2659";

	static constexpr uint kTextureDim = 2048;
	static constexpr uint kMaxStamps = 256;
	static constexpr uint kContactDim = 2048;
	static constexpr float kContactHalfExtent = 3072.0f;
	static constexpr float kContactNone = 1.0e30f;
	static constexpr uint kContactMaxBones = 80;
	static constexpr uint kContactMaxActors = 16;
	static constexpr uint kContactMaxCorpses = 8;
	static constexpr float kStampModeCarve = 0.0f;
	static constexpr float kStampModeBody = 40.0f;
	static constexpr float kStampImpactScale = 0.45f;
	/** @brief A stamp reaches no texel past this many radii: the carve ends at 1, the impact ring at 1.3. Mirrors STAMP_GATE in DeformationUpdateCS.hlsl. */
	static constexpr float kStampGate = 1.3f;
	static constexpr uint kSpellStampReserve = 48;

	static constexpr float kUnitsPerMeter = 70.0f;
	static constexpr float kMaxRangeSkinsM = 200.0f;

	static constexpr uint kShellGridDim = 640;
	static constexpr float kShellGridSpacing = 8.0f;

	static constexpr int kShellWarpBands = 5;
	static constexpr float kShellWarpBandVerts[kShellWarpBands] = { 192.0f, 8.0f, 8.0f, 8.0f, 104.0f };
	static constexpr float kShellWarpBandMul[kShellWarpBands] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };

	static constexpr float kShellOriginSnap = kShellGridSpacing * 32.0f;

	/** @brief World half-span of the warped shell grid (center to edge) at a given inner spacing. Linear in spacing: the band shape is unchanged. */
	static float ShellWarpedHalfSpan(float a_spacing = kShellGridSpacing)
	{
		float span = 0.0f;
		float verts = 0.0f;
		for (int band = 0; band < kShellWarpBands; ++band) {
			span += kShellWarpBandVerts[band] * kShellWarpBandMul[band];
			verts += kShellWarpBandVerts[band];
		}
		span += std::max(kShellGridDim * 0.5f - verts, 0.0f) * kShellWarpBandMul[kShellWarpBands - 1];
		return span * a_spacing;
	}

	static constexpr int kShellWindowDim = 1024;
	static constexpr float kShellVertexSpacing = 128.0f;
	static constexpr int kShellTexelsPerCell = 32;
	static constexpr int kShellWindowCells = kShellWindowDim / kShellTexelsPerCell;
	static constexpr float kShellMissingHeight = -100000.0f;

	static constexpr uint kSnowClassCount = 12;
	struct SnowClassDef
	{
		const char* label;
		const char* match;
	};
	static constexpr SnowClassDef kSnowClasses[kSnowClassCount] = {
		{ "Grass Snow", "grasssnow" },
		{ "Trodden Path", "snowpath" },
		{ "Snowy Rocks", "snowrocks" },
		{ "Snow 01", "snow01" },
		{ "Snow 02", "snow02" },
		{ "Roads", "road" },
		{ "Dirt", "dirt" },
		{ "Grass & Fields", "grass" },
		{ "Rocks & Cliffs", "rock" },
		{ "Coast & Beach", "coast" },
		{ "Mud & Rivers", "mud" },
		{ "Other", "" },
	};

	struct TextureDefaultDef
	{
		const char* match;
		float depth;
	};
	static constexpr TextureDefaultDef kTextureDefaults[] = {
		{ "frozenmarshice", 20.0f },
	};

	static constexpr uint kShellVertexLayers = 4;
	static constexpr uint16_t kNoLandTexture = 0xFFFF;
	static constexpr size_t kMaxLandTextures = 512;

	struct ShellCellData
	{
		std::array<float, 33 * 33> height;
		std::array<std::array<uint16_t, kShellVertexLayers>, 33 * 33> layerTexture;
		std::array<std::array<uint8_t, kShellVertexLayers>, 33 * 33> layerWeight;
		std::array<uint8_t, 33 * 33> vertexAO;
		std::vector<float> fine;
		std::array<uint8_t, 4> fineLevel{};
		uint32_t worldspaceID = 0;
	};

	struct LandTextureEntry
	{
		std::string path;
		std::string label;
		int classIndex = (int)kSnowClassCount - 1;
		float depth = -5.0f;
		bool overridden = false;
		bool shipped = false;
		float shippedDepth = 0.0f;
	};

	struct StampBones
	{
		RE::NiPointer<RE::NiAVObject> root;
		struct Foot
		{
			RE::NiPointer<RE::NiAVObject> node;
			RE::NiPointer<RE::NiAVObject> toe;
		};
		std::vector<Foot> feet;
		struct Limb
		{
			RE::NiPointer<RE::NiAVObject> a;
			RE::NiPointer<RE::NiAVObject> b;
			float radius;
			float restLength = -1.0f;
		};
		std::vector<Limb> limbs;
		float bodyAlpha = -1.0f;
		RE::BSFixedString bodyAlphaSource;
		float anyAlpha = -1.0f;
		RE::BSFixedString anyAlphaSource;
		uint16_t alphaRecheck = 0;
		uint16_t alphaSettle = 0;
		uint16_t attachRecheck = 0;
		uint16_t collectDelay = 0;
		float dryTravel = 0.0f;
		float prevPosX = 0.0f;
		float prevPosY = 0.0f;
		bool hasPrevPos = false;
		bool dryRecollected = false;
		bool collisionFallback = false;
		uint16_t contactStarved = 0;
		bool contactStarveLogged = false;
	};

	struct Settings
	{
		bool EnableSnowDeformation = true;
		float RefillRateMultiplier = 1.0f;
		bool EnableSnowAccumulation = true;
		std::map<std::string, float> TextureDepths;
		std::string SnowTexturePath = "Textures\\PBR\\Landscape\\snow01.dds";
		float ParallaxShadowStrength = 0.25f;
		float ParallaxDepth = 0.5f;
		float SnowResistance = 33.0f;
		bool ShellHorizonMarch = true;
		bool SnowGlints = true;
		bool SnowScreenSpaceShadows = true;
		bool SnowCastsShadows = true;
		uint32_t DeformMapResolution = 4096;
		float RangeTrenchesM = 200.0f;
		float RangeActorTrenchesM = 200.0f;
	};

	static constexpr float kAccumulationPeak = 1.5f;
	static constexpr float kAccumulationHours = 1.0f;
	static constexpr float kAccumulationMeltHours = 1.0f;
	static constexpr float kAccumulationFadeDays = 3.0f;
	static constexpr float kFloatingActorBand = 20.0f;
	/** @brief Snow depth of each texture family, in world units; a texture's own depth (TextureDepths, shipped defaults) wins. */
	static constexpr std::array<float, kSnowClassCount> kSnowClassDepths = { 14.0f, 18.0f, 30.0f, 30.0f, 30.0f, -8.0f, -8.0f, -8.0f, -8.0f, -8.0f, -8.0f, -8.0f };
	static constexpr float kTrampleZoneScale = 0.75f;
	/** @brief Share of the surrounding snow a workspace bowl melts away. */
	static constexpr float kTrampleZoneMelt = 0.5f;
	static constexpr float kUndulationStrength = 8.0f;
	static constexpr float kImpactReach = 0.75f;
	static constexpr float kSpeedSpoilSize = 0.25f;
	static constexpr float kObjectRasterReachM = 100.0f;

	/** @brief GPU-side settings, appended to the shared FeatureData cbuffer (b6). Layout must match SnowDeformationSettings in SharedData.hlsli. */
	struct alignas(16) SettingsGPU
	{
		float2 WindowOrigin;
		float InvWorldSize;
		float padParallaxFollow;

		uint DebugTerrainOverlay;
		float LODReplaceStart;
		float LODReplaceFadeInv;
		float padLODSnowSensitivity;

		float SnowIsLinear;
		float SnowRoughnessScale;
		float LODReplaceEnable;
		float SnowHasNormal;

		float padSnowTexturedEnable;
		float ProjSnowEnable;
		float padLODObjectEnable;
		float padProjUnauthoredThreshold;

		DirectX::XMINT2 DeformMapOrigin;
		float padLandWetPrint;
		float padPrintFullDepth;

		float2 WaterWindowOrigin;
		float WaterWindowTexel;
		float WaterWindowDim;

		float padLandIce;
		float padProjSnowNormalEnable;
		float SnowSpecularLevel;
		float LandRecolorLumLo;

		DirectX::XMINT4 padPrintRect;

		float padParallaxFlatten;
		float padIceClock[3];
	};
	STATIC_ASSERT_ALIGNAS_16(SettingsGPU);
	static_assert(sizeof(SettingsGPU) == 144, "SettingsGPU must match the fxc-reflected SnowDeformationSettings in SharedData.hlsli");

	/**
	 * @brief Returns this frame's GPU settings for the shared FeatureData buffer.
	 *
	 * Also advances the deformation window when a_inWorld is true. The origin
	 * must be computed here (during State::UpdateSharedData, before Prepass) so
	 * the constant buffer and the scrolled texture agree within a frame.
	 */
	SettingsGPU GetCommonBufferData(bool a_inWorld);

	/** @brief Per-dispatch constants for the deformation update. Layout must match PerFrame in DeformationUpdateCS.hlsl. */
	struct alignas(16) PerFrame
	{
		float2 WindowOrigin;
		DirectX::XMINT2 MapOrigin;

		float TexelSize;
		uint StampCount;
		float StampFalloffStart;
		float RefillAmount;

		float2 WindBias;
		float GameDeltaTime;
		float BodyPrint;

		DirectX::XMINT4 RingRects[2];
		uint RingRectCount;
		uint RingTotalTexels;
		uint PassClock;
		float AccumulationScale;

		float4 Stamps[kMaxStamps];
		float4 StampEnds[kMaxStamps];

		float2 ContactCenter;
		float ContactHalfExtent;
		float ContactDim;

		float2 TerrainWindowOrigin;
		float TerrainTexelSize;
		float TerrainDim;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(offsetof(PerFrame, ContactCenter) == 8288, "PerFrame tail must match the fxc-reflected layout of DeformationUpdateCS");
	static_assert(offsetof(PerFrame, TerrainWindowOrigin) == 8304 && sizeof(PerFrame) == 8320, "PerFrame tail must match the fxc-reflected layout of DeformationUpdateCS");

	static constexpr float kTrenchWallSharpness = 66.0f;

	Settings settings;

	static constexpr float kBaseRefillTime = 700.0f;
	static constexpr float kReferenceSnowDensity = 2.0f;
	static constexpr float kMaxSnowfallIntensity = 4.0f;

	/** @brief Snowfall intensity from the weather records' precipitation density, faded across weather transitions. 1.0 = reference-density snowfall; 0 for ash and in a No Sky worldspace. */
	float ComputeSnowfallIntensity() const;
	/** @brief The same transition-faded precipitation density for any weather class (snow, rain). */
	float ComputePrecipitationIntensity(RE::TESWeather::WeatherDataFlag a_flag) const;
	struct SnowWeatherRange
	{
		bool scanned = false;
		uint32_t weathers = 0;
		float heaviest = 1.0f;
		float split = 0.5f;
		std::string heaviestName;
		std::string splitName;
	};
	/** @brief Scans the loaded weathers the first time it is asked, after the data has loaded. */
	const SnowWeatherRange& SnowWeathers();
	SnowWeatherRange snowWeatherRange;
	/** @brief The player's worldspace when it is flagged No Sky (Blackreach), else null. Outdoor weather state holds there as it does indoors. */
	static RE::TESWorldSpace* NoSkyWorldspace();
	/** @brief The first kNotSnowKeywords entry (the MATO and LOD batch vetoes' list) found in a name, case-insensitive, or null. */
	static const char* NotSnowKeywordIn(const char* a_name);
	float snowfallIntensity = 0.0f;

	struct GameClock
	{
		float lastHours = -1.0f;
		float timescale = 20.0f;
		float elapsedHours = 0.0f;
		bool reversed = false;
		/** @brief Takes this frame's reading; a set a_unarm re-arms instead of measuring. @return The calendar's hours passed, negative without a calendar. */
		float Read(std::atomic<bool>& a_unarm);
	};
	GameClock gameClock;
	std::atomic<bool> gameClockUnarm{ false };
	std::atomic<float> gameClockHours{ -1.0f };
	/** @brief Takes this frame's reading. Call once, before anything that consumes it. */
	void TickGameClock();
	GameClock weatherClock;
	std::atomic<bool> weatherClockUnarm{ false };

	ConstantBuffer* perFrame = nullptr;
	Texture2D* deformationTextures[2] = { nullptr, nullptr };

	Texture2D* bermFieldTexture = nullptr;

	/** @brief SRV of the deformation map (the single canonical texture), for shader sampling. Physical (toroidal) layout - readers translate through DeformMapOrigin. */
	ID3D11ShaderResourceView* GetDeformationSRV() const { return deformationTextures[0] ? deformationTextures[0]->srv.get() : nullptr; }
	/** @brief SRV of the baked berm field; null before SetupResources. */
	ID3D11ShaderResourceView* GetBermFieldSRV() const { return bermFieldTexture ? bermFieldTexture->srv.get() : nullptr; }

	Texture2D* passStateTexture = nullptr;
	Texture2D* bodyReachTexture = nullptr;
	double passClockSeconds = 256.0;
	static constexpr UINT kUntouchedTilesSlot = 57;
	static constexpr UINT kBodyReachSlot = 56;
	static constexpr UINT kTerrainWetSlot = 58;

	static constexpr float kRefillStep = 1.0f / 1024.0f;
	float refillBank = 0.0f;

	static constexpr uint32_t kUndulationFieldDim = 2048;
	static constexpr float kUndulationFieldTexel = 16.0f;
	static constexpr float kUndulationFieldHalfExtent = kUndulationFieldDim * kUndulationFieldTexel * 0.5f;
	static constexpr float kUndulationFieldSnap = 512.0f;

	Texture2D* undulationFieldTexture = nullptr;
	static constexpr uint32_t kNoiseHashDim = 256;
	static constexpr uint32_t kNoiseHashSlot = 109;
	Texture2D* noiseHashTexture = nullptr;
	bool noiseHashBaked = false;
	/** @brief Bakes the hash table on the first call after it was created; the bound SRV, or null until baked. */
	ID3D11ShaderResourceView* NoiseHashSRV();
	static constexpr uint32_t kTrenchDetailSlot = 112;
	winrt::com_ptr<ID3D11ShaderResourceView> trenchDetailNormalSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> trenchDetailHeightSRV;
	bool trenchDetailAttempted = false;
	void EnsureTrenchDetailTextures();
	struct alignas(16) UndulationFieldCB
	{
		float2 FieldOriginWorld;
		float FieldTexel;
		float padFieldScale;
	};
	STATIC_ASSERT_ALIGNAS_16(UndulationFieldCB);
	ConstantBuffer* undulationFieldCB = nullptr;
	float2 undulationFieldCenter = { 0.0f, 0.0f };
	bool undulationFieldValid = false;
	/** @brief Rebakes when the field recenters; no-ops when current. Called once per frame from Prepass. */
	void UpdateUndulationField();
	ID3D11ShaderResourceView* GetUndulationFieldSRV() const { return undulationFieldTexture ? undulationFieldTexture->srv.get() : nullptr; }

	/** @brief Creates the ping-pong deformation textures and the per-frame constant buffer. */
	virtual void SetupResources() override;

	/** @brief (Re)creates the ping-pong deformation textures at deformMapDim, releasing any previous pair. */
	void CreateDeformationTextures();

	/**
	 * @brief Per-frame update: gathers actor stamp positions, scrolls the window
	 * to follow the camera, and dispatches the deformation update compute shader.
	 */
	virtual void Prepass() override;
	/** @brief Prepass's map, simulation and per-frame work; Prepass then runs the early ground prepass (S1). */
	void PrepassSimulation();
	/** @brief Puts back the blended depth SyncSssDepth changed; runs after Screen-Space Shadows' PostPrepass march (feature order). */
	virtual void PostPrepass() override;

	enum SnowCS : uint32_t
	{
		kExclusionFieldCS,
		kDeformationRingCS,
		kDeformationEvolveCS,
		kDeformationStampCS,
		kDeformationStampAllCS,
		kDeformationScanEvolveCS,
		kDeformationScanSnapshotCS,
		kDeformationSnapshotCS,
		kDeformationScanBermCS,
		kUntouchedTilesCS,
		kBermFieldTiledCS,
		kUndulationFieldCS,
		kNoiseHashCS,
		kShellTerrainCutCS,
		kDepthSyncCS,
		kSssSyncCS,
		kSssRestoreCS,
		kTerrainFineCS,
		kWindowFillCS,
		kSnowCSCount
	};
	/** @brief Returns one of the trail map's compute shaders, compiling it on first use; null if it failed to compile. */
	ID3D11ComputeShader* GetSnowCS(SnowCS a_which);
	winrt::com_ptr<ID3D11ComputeShader> snowCS[kSnowCSCount];
	bool deformationCSFailed = false;

	static constexpr uint kStampTileCap = 16384;
	winrt::com_ptr<ID3D11Buffer> stampTileBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> stampTileSRV;
	std::vector<uint32_t> stampTileScratch;
	std::vector<uint32_t> stampTileBits;
	/** @brief Fills stampTileScratch with the deduped physical tiles the stamp inputs touch (wrap-aware). Returns the count, or UINT32_MAX on overflow past kStampTileCap. */
	uint32_t BuildStampTileList(const PerFrame& a_data);

	winrt::com_ptr<ID3D11Texture2D> occupancyTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> occupancyUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> occupancySRV;
	winrt::com_ptr<ID3D11Buffer> evolveTileBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> evolveTileUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> evolveTileSRV;
	winrt::com_ptr<ID3D11Buffer> evolveArgsBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> evolveArgsUAV;
	winrt::com_ptr<ID3D11Buffer> snapshotTileBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> snapshotTileUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> snapshotTileSRV;
	winrt::com_ptr<ID3D11Buffer> snapshotArgsBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> snapshotArgsUAV;
	bool tileGridsNeedInit = true;

	winrt::com_ptr<ID3D11Texture2D> bermDirtyTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> bermDirtyUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> bermDirtySRV;
	winrt::com_ptr<ID3D11Buffer> bermTileBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> bermTileUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> bermTileSRV;
	winrt::com_ptr<ID3D11Buffer> bermArgsBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> bermArgsUAV;
	winrt::com_ptr<ID3D11Texture2D> bermOccupancyTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> bermOccupancyUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> bermOccupancySRV;
	winrt::com_ptr<ID3D11Texture2D> untouchedTilesTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> untouchedTilesUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> untouchedTilesSRV;

	struct ArrivalRect
	{
		int x0, y0, w, h;
	};
	/** @brief The rects whose world assignment changes under this frame's scroll (or the full map on a clear). ONE definition shared by RingCS's dispatch and BuildTrenchInject, so the ring pass and the inject upload can never disagree about which texels are arriving. Returns the rect count (0-2). */
	int ComputeArrivalRects(DirectX::XMINT2 a_scroll, bool a_clearing, ArrivalRect a_rects[2]) const;

	/** @brief Copies a LOGICAL-space box out of the toroidal map into a staging texture laid out logically: up to four physical segments split at the wrap seam, each landing at its logical offset in the destination. Every CPU read of the map routes through this so no site does its own torus arithmetic. a_physOrigin is the MapOrigin the source content was written under. */
	void CopyLogicalBox(ID3D11Texture2D* a_dst, ID3D11Texture2D* a_src, int a_x0, int a_y0, int a_w, int a_h, DirectX::XMINT2 a_physOrigin);
	virtual void ClearShaderCache() override;

	/** @brief Draws the ImGui settings UI. Implemented in SnowDeformation/Menu.cpp. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Installs both landscape hooks; the TESObjectLAND detour attaches after TruePBR's so it sees the final quad materials. Implemented in SnowDeformation/TerrainData.cpp. */
	virtual void PostPostLoad() override;

	Texture2D* shellTerrainTexture = nullptr;
	Texture2D* shellTerrainWet = nullptr;
	Texture2D* shellTerrainBare = nullptr;
	std::vector<uint8_t> shellBareScratch;
	static constexpr UINT kTerrainBareSlot = 26;
	static constexpr UINT kTBMaskSlot = 110;
	bool shellWindowLandBlend = false;
	float shellWindowLandBlendStart = 1.0f;
	/** @brief Rebuilds shellTerrainWet when its inputs changed. Call after the water cover pass, before any landscape-shell pass. */
	void UpdateTerrainWet();
	/** @brief SampleTerrain's t21: the cut window while it is current, else the raw window. */
	ID3D11ShaderResourceView* TerrainWetSRV() const;
	uint64_t shellTerrainWetKey[3] = {};
	bool shellTerrainWetValid = false;
	/** @brief The SRV DrawShell and the caster bind at t27 (WaterWindow). */
	ID3D11ShaderResourceView* ShellWaterSRV() const;
	static constexpr int kShellFineCells = 9;
	static constexpr float kShellFineTexel = 32.0f;
	static constexpr int kShellFineDim = kShellFineCells * 128;
	static constexpr size_t kShellFineCacheCells = 128;
	std::deque<uint64_t> shellFineOrder;
	/** @brief Fills a_data.fine from the land's quad meshes; false (and fine empty) when any quad had no readable vertex copy or its lattice did not line up with heights[]. */
	bool ReadLandMeshHeights(RE::TESObjectLAND* a_land, ShellCellData& a_data);
	/** @brief Each frame: loaded cells whose bake ran before the engine subdivided their land mesh (289 vertices at hook time, 4225 later) get their fine data once the meshes have grown; a hard refusal is remembered so the cell is not re-tried. Sets shellFineDirty. */
	void RefreshLandMeshHeights();
	/** @brief The quad's drawn geometry: geom[q] when it holds the subdivided mesh, else the largest BSTriShape under mesh[q]. */
	static RE::BSTriShape* LandQuadGeometry(RE::TESObjectLAND::LoadedLandData* a_loaded, uint32_t a_quad);
	uint32_t fineRefreshFrame = 0;
	std::atomic<bool> shellFineDirty{ false };
	std::unordered_map<uint64_t, uint32_t> shellFineRefused;
	/** @brief 0 none, 1 = 17x17, 2 = 33x33, 4 = 65x65 vertices per quad. */
	static uint32_t LandQuadLevel(RE::BSTriShape* a_geometry);
	Texture2D* shellTerrainFine = nullptr;
	float shellFineOriginX = 0.0f;
	float shellFineOriginY = 0.0f;
	bool shellFineValid = false;
	uint32_t shellFineMeshCells = 0;
	std::atomic<bool> fineMeshWarned{ false };
	struct alignas(16) TerrainFineCB
	{
		float2 FineOriginWorld;
		float2 WindowOriginWorld;
		uint32_t FineDim;
		uint32_t WindowDim;
		float TexelSize;
		float FineTexel;
	};
	ConstantBuffer* terrainFineCB = nullptr;
	void BuildTerrainFineWindow();
	static constexpr float kShellTessTargetPx = 16.0f;
	/** @brief kShellTessTargetPx in this frame's render pixels (TessScreen.x): an upscaler's render scale applied. */
	float ShellTessTargetRenderPx() const;

	/** @brief Per-draw constants for the shell pass. Layout must match ShellCB in SnowShell.hlsl. */
	struct alignas(16) ShellCB
	{
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraView;

		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;

		float2 GridOrigin;
		float GridSpacing;
		float TerrainTexelSize;

		float2 GridToTerrainOffset;
		float2 GridToDeformOffset;

		float WarpedHalfSpan;
		uint TerrainDim;
		float DeformInvWorldSize;
		float ShellCullBare;

		uint HasSnowTexture;
		float SnowTextureIsLinear;
		float HasSnowNormal;
		float HasSnowRmaos;

		float SnowRoughnessScale;
		float SnowSpecularLevel;
		float2 SnowUVOffset;

		float4 SnowGlintParams;

		float EnableGlints;
		float HasSnowHeight;
		float TrenchDetailRelief;
		float TrenchDetailShadow;

		float2 ObjectHeightCenter;
		float ObjectHeightHalfExtent;
		float BermBakeActive;

		float CrispShadows;
		float ScreenSpaceShadowsActive;
		float PointLightsActive;
		float SkylightingActive;

		float2 SunCascadeSlices;
		float2 LandContest;

		float3 SnowParallax;
		float SeamRampInv;

		float4 SeamBounds;

		float4 ExclusionFieldWindow;

		float4 UndulationFieldWindow;

		DirectX::XMINT2 DeformMapOrigin;
		DirectX::XMINT2 ShellFlags;

		float4 FineWindow;
		float4 TessScreen;
		float4 FieldDims;

		float AccumulationDepthScale;
		float BodyPrint;
		float pad0[2];
	};
	STATIC_ASSERT_ALIGNAS_16(ShellCB);
	static_assert(offsetof(ShellCB, AccumulationDepthScale) == 576, "ShellCB tail must match the fxc-reflected layout of SnowShell.hlsl");
	static_assert(sizeof(ShellCB) == 592, "ShellCB tail must match the fxc-reflected layout of SnowShell.hlsl");

	struct PointShadowLightData
	{
		DirectX::XMFLOAT4X4 LightTransform;
		uint32_t SliceIndex;
		uint32_t LightType;
		float ShadowRadius;
		float pad;
	};
	static constexpr uint32_t kPointShadowMaxLights = 4;

	/**
	 * @brief Draws the ground snow into the deferred G-buffer.
	 *
	 * Called from Deferred::DeferredPasses before the composite, while the
	 * frame's G-buffer contents and main depth are complete. The ground is the
	 * landscape replay's prepass (here when S1 did not draw it), the resolve
	 * and one full-screen deferred pass. Binds its own targets/state and
	 * restores the previous pipeline state afterwards.
	 * Implemented in SnowDeformation/Shell.cpp.
	 */
	void DrawShell();

	Texture2D* shellDeferredAttr[3] = {};
	winrt::com_ptr<ID3D11DepthStencilState> shellDeferredDepthState;
	bool EnsureDeferredResources();
	winrt::com_ptr<ID3D11PixelShader> shellPSDeferred;
	ID3D11PixelShader* GetShellPSDeferred();

	/** @brief Shades this frame's drift meshes as ground snow, with the deferred pass's own shading on their surface. Before that pass, which keeps the pixels the ground claimed. Implemented in SnowDeformation/Statics.cpp. */
	void DrawDriftCoats(ID3D11DeviceContext* a_context, const ShellCB& a_cb);
	winrt::com_ptr<ID3D11VertexShader> driftCoatVS;
	winrt::com_ptr<ID3D11PixelShader> driftCoatPS;
	winrt::com_ptr<ID3D11DepthStencilState> driftCoatDepthState;
	ID3D11VertexShader* GetDriftCoatVS();
	ID3D11PixelShader* GetDriftCoatPS();

	Texture2D* marchHeight[3] = {};
	Texture2D* marchSunEnvelope = nullptr;
	enum MarchCS : uint32_t
	{
		kGroundTilesCS,
		kGroundRowsCS,
		kFarRowsCS,
		kSunTilesCS,
		kSunRowsCS,
		kMarchCSCount
	};
	ID3D11ComputeShader* marchHeightCS[kMarchCSCount] = {};
	ID3D11ComputeShader* GetMarchHeightCS(MarchCS a_which);
	struct alignas(16) MarchCacheCB
	{
		uint32_t RowStart;
		uint32_t RowCount;
		uint32_t pad[2];
		float4 Sun;
	};
	STATIC_ASSERT_ALIGNAS_16(MarchCacheCB);
	ConstantBuffer* marchCacheCB = nullptr;
	bool marchCacheValid = false;
	bool marchFarValid = false;
	uint64_t marchCacheKey = 0;
	uint32_t marchCacheTerrainVersion = 0;
	float marchCacheFineOrigin[2] = {};
	uint32_t marchCacheRowCursor = 0;
	uint32_t marchFarRowCursor = 0;
	uint32_t marchCacheFastRows = 0;
	uint32_t marchFarFastRows = 0;
	uint64_t bermBakeSeq = 0;
	uint64_t marchCacheBermSeq = 0;
	float marchSunDir[3] = { 0.0f, 0.0f, 1.0f };
	bool marchSunValid = false;
	uint32_t marchSunRowCursor = 0;
	uint32_t marchSunFastRows = 0;
	uint32_t marchIdleFrames = 0;
	static constexpr uint32_t kMarchIdleFrames = 30;
	/** @brief Brings the caches up to date and binds them at PS t106-t108, the sun envelope at t100 (DrawShell, after the shell CB upload). False = no caches this frame: the marches must be switched off, as they read nothing else. */
	bool UpdateMarchCaches(const ShellCB& a_cb);
	void UnbindMarchCaches();
	void ReleaseMarchCache();
	winrt::com_ptr<ID3D11PixelShader> shellPSPrepassAttr[2];
	ID3D11PixelShader* GetShellPSPrepassAttr(bool a_keepHidden);

	enum class EarlyPrepass : uint8_t
	{
		Off,
		Drawing,
		NotDrawing,
		NeedsTerrainBlending,
		Unavailable
	};
	EarlyPrepass shellEarlyState = EarlyPrepass::Off;
	uint32_t shellEarlyFrame = UINT32_MAX;
	std::unique_ptr<ShellCB> shellEarlyCB;
	D3D11_VIEWPORT shellEarlyViewport{};
	D3D11_VIEWPORT shellDrawViewport{};
	uint32_t shellDrawViewportFrame = UINT32_MAX;
	bool shellDrawGroundGated = false;
	uint32_t shellDrawTerrainVersion = 0;
	float shellDrawDynRes[2] = {};
	Texture2D* shellEarlyPending = nullptr;
	Texture2D* shellEarlyMarks = nullptr;
	bool EnsureEarlyPrepassResources();
	void DrawShellEarlyPrepass();
	winrt::com_ptr<ID3D11PixelShader> shellPSPrepassEarly;
	ID3D11PixelShader* GetShellPSPrepassEarly();

	winrt::com_ptr<ID3D11PixelShader> shellPSEarlyResolve;
	ID3D11PixelShader* GetShellPSEarlyResolve();

	uint32_t shellSssSyncFrame = UINT32_MAX;
	D3D11_BOX shellSssRect{};
	/** @brief Early-prepass frames: S1's raster depth into Terrain Blending's 32-bit blended depth, the replaced values into shellEarlyMarks. */
	void SyncSssDepth();
	/** @brief Writes shellEarlyMarks back over the pixels SyncSssDepth changed. */
	void RestoreSssDepth();
	/** @brief a_cs over shellSssRect with t1 the raster depth, u0 the blended depth, u1 the marks; CS t1, u0-u1, b2 and the shader go back as found. */
	void DispatchSssDepth(ID3D11ComputeShader* a_cs, const char* a_pass);

	ID3D11ShaderResourceView* shellGroundMarksSRV = nullptr;
	winrt::com_ptr<ID3D11PixelShader> shellFillPSClaim;
	ID3D11PixelShader* GetShellFillPSClaim();
	static void SetShellSeamBounds(ShellCB& a_cb, int a_minX, int a_minY, int a_maxX, int a_maxY);

	ID3D11VertexShader* GetShellFillVS();
	ID3D11PixelShader* GetShellFillPS();
	winrt::com_ptr<ID3D11VertexShader> shellFillVS;
	winrt::com_ptr<ID3D11PixelShader> shellFillPS;
	bool EnsurePrepassResources(ID3D11ShaderResourceView* a_mainDepthSRV);
	Texture2D* shellRasterDepth = nullptr;
	winrt::com_ptr<ID3D11Texture2D> shellTestDepth;
	winrt::com_ptr<ID3D11DepthStencilView> shellTestDepthDSV;
	winrt::com_ptr<ID3D11ShaderResourceView> shellTestDepthSRV;
	/** @brief Origin and extent (pixels) of the rendered rectangle for DepthSyncCS: the viewport under dynamic resolution. */
	struct alignas(16) DepthSyncRectCB
	{
		uint32_t Origin[2];
		uint32_t Extent[2];
	};
	ConstantBuffer* depthSyncRectCB = nullptr;
	/** @brief depthSyncRectCB, created on first use. */
	ConstantBuffer* DepthSyncRectBuffer();
	/** @brief The rectangle a screen pass covers: the viewport plus a_guard pixels, clamped to the target; the whole target with no viewport. */
	D3D11_BOX ScreenRectBox(const D3D11_VIEWPORT* a_viewport, uint32_t a_width, uint32_t a_height, uint32_t a_guard) const;
	winrt::com_ptr<ID3D11DepthStencilState> shellFillDepthState;

	ConstantBuffer* shellCB = nullptr;

	/** @brief Loads a single 2D DDS through the GAME's resource system, so archives resolve and a modlist override still wins. */
	static bool LoadGameDDS(const std::string& a_dataRelativePath, winrt::com_ptr<ID3D11ShaderResourceView>& a_srv);
	winrt::com_ptr<ID3D11RasterizerState> shellRasterState;
	winrt::com_ptr<ID3D11DepthStencilState> shellDepthState;

	winrt::com_ptr<ID3D11ShaderResourceView> lightingCloudShadowSRV;

	winrt::com_ptr<ID3D11ShaderResourceView> shellSnowDiffuseSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> shellSnowNormalSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> shellSnowRmaosSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> shellSnowHeightSRV;
	bool shellSnowTextureIsPBR = false;
	bool shellSnowTextureAttempted = false;

	float snowGlintLogDensity = 6.0f;
	float snowGlintMicroRoughness = 0.3f;
	float snowGlintDensityRandomization = 5.0f;
	float snowGlintScreenSpaceScale = 1.0f;
	float snowRoughnessScale = 0.7f;
	float snowSpecularLevel = 0.02f;
	float snowDisplacementScale = 1.0f;
	std::string snowPBRSetName;
	winrt::com_ptr<ID3D11SamplerState> shellSnowSampler;

	/** @brief Lazy-loads the shell snow texture set (and its authored PBR parameters) from the user-configured path. Implemented in SnowDeformation/Shell.cpp. */
	void EnsureShellSnowTextures();
	/** @brief PS define list for the shell shaders: PSHADER, optional extra, SNOW_EXP_HEIGHT_FOG and SNOW_IBL when those features are loaded, and the shell feature defines. Implemented in SnowDeformation/Shell.cpp. */
	static std::vector<std::pair<const char*, const char*>> ShellPSDefines(const char* a_extra = nullptr);
	/** @brief Adds SNOW_TERRAIN_VARIATION and SNOW_SKYLIGHTING when those optional features are loaded; every shader that includes SnowShell.hlsl takes them. Implemented in SnowDeformation/Shell.cpp. */
	static void AddShellFeatureDefines(std::vector<std::pair<const char*, const char*>>& a_defines);
	/** @brief Define list for a non-PS shader that includes SnowShell.hlsl: an optional extra plus the shell feature defines. Implemented in SnowDeformation/Shell.cpp. */
	static std::vector<std::pair<const char*, const char*>> ShellDefines(const char* a_extra = nullptr);
	/** @brief Re-reads the matched TruePBR texture set's values (per frame: follows ReloadTextureSetData and live menu edits). Implemented in SnowDeformation/Shell.cpp. */
	void RefreshSnowPBRParams();

	/** @brief Bakes one cell's heights and per-vertex snow coverage from LoadedLandData. Called from the TESObjectLAND hook. Implemented in SnowDeformation/TerrainData.cpp. */
	void BakeShellCell(RE::TESObjectLAND* land);

	/** @brief Recenters and re-uploads the terrain data window when the camera crosses cells or new cells were baked. Called from Prepass. Implemented in SnowDeformation/TerrainData.cpp. */
	void UpdateShellTerrainWindow();

	/** @brief Tracks the camera's worldspace and invalidates the world-anchored caches on a change. Called from Prepass. Implemented in SnowDeformation/TerrainData.cpp. */
	void UpdateActiveWorldspace();

	/** @brief Registers the texture set a land texture is drawn with (the season's swap when one is active) and returns its registry index, or kNoLandTexture for an absent one. Implemented in SnowDeformation/TerrainData.cpp. */
	uint16_t RegisterLandTexture(RE::TESLandTexture* a_landTexture);

	/** @brief Re-resolves every non-overridden texture against the current class depths. Call after the class sliders or the whole settings block move. Implemented in SnowDeformation/TerrainData.cpp. */
	void RefreshLandTextureDepths();

	/** @brief Pins one texture to its own depth, or clears the pin when the depth is empty. Holds the registry lock: the bake thread reads the same map. Implemented in SnowDeformation/TerrainData.cpp. */
	void SetLandTextureOverride(const std::string& a_path, std::optional<float> a_depth);

	/** @brief Snapshot of the per-index depths, taken once per window rebuild so the texel loop needs no lock. Implemented in SnowDeformation/TerrainData.cpp. */
	std::vector<float> LandTextureDepthSnapshot();

	/** @brief Re-resolves one entry: user value, then shipped per-texture default, then the family depth. Caller holds landTextureMutex exclusively. */
	void ResolveLandTextureDepthLocked(LandTextureEntry& a_entry);

	/** @brief Re-resolves every entry against the class defaults and the override map. Caller holds landTextureMutex exclusively. */
	void ResolveLandTextureDepthsLocked();

	/** @brief Per-dispatch constants for the window far fill. Layout must match WindowFillCB in TerrainWindowFillCS.hlsl. */
	struct alignas(16) WindowFillCB
	{
		float2 WindowOriginWorld;
		float TexelSize;
		uint WindowDim;

		float2 HeightMapScale;
		float2 HeightMapOffset;

		float2 HeightRange;
		float padSnowDepthUnits;
		float padFill;

		float2 LODTileBase;
		float LODTileSpan;
		float padLODSnowSensitivity;

		float4 LODTileValid;
	};
	STATIC_ASSERT_ALIGNAS_16(WindowFillCB);
	ConstantBuffer* windowFillCB = nullptr;

	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11ShaderResourceView>> lodTileCache;
	std::unordered_set<uint64_t> lodTileMisses;
	/** @brief Fetches (or loads) the level-32 LOD diffuse tile with the given SW cell coords for the current fill worldspace. Implemented in SnowDeformation/TerrainData.cpp. */
	ID3D11ShaderResourceView* GetLODTile(const std::string& a_worldspace, int a_cellX, int a_cellY);

	/** @brief Fills sentinel window texels from the Terrain Shadows xLODGen heightmap (height + snow-line coverage, provenance in .w). Runs after each window upload. Implemented in SnowDeformation/TerrainData.cpp. */
	void FillShellWindowFromHeightmap();
	std::string lastFillWorldspace;

	/** @brief Nominal (untrampled) snow depth in world units at a world XY, resolved from the baked cell data and the class depth sliders. Returns a_missing where no cell is baked (interiors, unvisited land). Implemented in SnowDeformation/TerrainData.cpp. */
	float GetNominalSnowDepthAt(float a_x, float a_y, float a_missing);

	uint32_t shellStatCellsInWindow = 0;
	uint32_t shellStatSnowTexels = 0;
	float shellStatMinHeight = 0.0f;
	float shellStatMaxHeight = 0.0f;

	struct CapturedSnowStatic
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiTransform world;
		bool makesLayout;
	};

	std::vector<CapturedSnowStatic> capturedStatics;
	struct DriftCoatDraw
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiTransform world;
	};
	std::vector<DriftCoatDraw> driftCoats;
	struct CapturedWater
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiTransform world;
		float planeConstant;
	};
	std::vector<CapturedWater> capturedWater;
	/** @brief Fills capturedWater from TESWaterSystem::waterObjects (every live body's shape, culled or not); not from the water shader's draws, which also carry the LOD quadtree, skirts, the displacement mesh and the waterline-crossing effect. */
	void GatherWaterObjects();
	ankerl::unordered_dense::set<void*> capturedStaticsSet;

	/** @brief Records the lighting draws the object height raster takes. Called from the BSLightingShader::SetupGeometry hook. Implemented in SnowDeformation/Statics.cpp. */
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);

	/** @brief Publishes the per-pass SnowProjectedIsSnow permutation bit, classified from the pass technique (currentRawTechnique), and re-binds the snow set for classified draws. Must run BEFORE the game's SetupGeometry, which consumes the descriptor. Implemented in SnowDeformation/Statics.cpp. */
	void SetProjectedSnowBit(RE::BSLightingShader* a_shader, RE::BSRenderPass* a_pass);

	/** @brief Installs the SetupGeometry capture hook. Called from PostPostLoad; implemented in SnowDeformation/Statics.cpp. */
	void InstallStaticsCaptureHook();
	/** @brief PlayerCamera::Update vfunc: after the game has placed the camera, ClampCameraAboveSnow. Implemented in TerrainData.cpp. */
	void InstallCameraHook();
	/** @brief Snow surface (ground + class depth, accumulation-scaled) at a world XY from the CPU terrain window; false where the window has no ground or no snow. */
	bool SampleShellSurface(float a_x, float a_y, float& a_surface) const;
	/** @brief Third person, mount, bleedout (ragdoll, death), dragon and PCTransition, plus any non-free camera while the player is down: if the camera sits under the shell, pull it toward the player along its own line until it clears the snow, the way a terrain hit would; while knocked down, ragdolled or dead, lift it straight up instead. */
	void ClampCameraAboveSnow();
	winrt::com_ptr<ID3D11Texture2D> landMasksCopyTex;
	winrt::com_ptr<ID3D11ShaderResourceView> landMasksCopySRV;

	/** @brief Copies a_box (all of it when null) of the resource behind a_srcSRV into an owned SRV-only texture sized to a_extent's far corner (a_box's when null, the source's when both are), recreating it when that size or the format changes. The SRV doubles as the validity signal (nulled by callers on invalid frames), so it is rebuilt even when the texture itself is still current. Implemented in SnowDeformation/Shell.cpp. */
	static void CopySRVResource(ID3D11ShaderResourceView* a_srcSRV, const char* a_name,
		winrt::com_ptr<ID3D11Texture2D>& a_tex, winrt::com_ptr<ID3D11ShaderResourceView>& a_srv, const D3D11_BOX* a_box = nullptr, const D3D11_BOX* a_extent = nullptr);

	winrt::com_ptr<ID3D11ShaderResourceView> shadowAtlasLiveSRV;
	/** @brief The atlas the crisp cascade shadows sample this frame (in place), or null (the VSM path). */
	ID3D11ShaderResourceView* ShadowAtlasSRV() const { return shadowAtlasLiveSRV.get(); }
	winrt::com_ptr<ID3D11SamplerState> shadowCmpSampler;
	winrt::com_ptr<ID3D11SamplerState> shellLinearSampler;

	/** @brief Called from State::Draw while the game renders the shadow MASK (Utility shader, RenderShadowmask); the only point where PS t4 genuinely holds the sun cascade atlas (at any other time it holds whatever texture the last draw bound). Keeps the atlas for the crisp shell shadows to read in place. Same trigger VolumetricShadows and Skylighting use. Implemented in SnowDeformation/Shadows.cpp. */
	void CaptureShadowAtlas();

	std::unique_ptr<ShellCB> lastShellCBData;

	uint32_t sunCascadeSlice[2] = { 0, 1 };

	/** @brief The sun cascades' real atlas slices into sunCascadeSlice every frame (the snow's shadow reads), whether or not anything casts. */
	void RecordSunCascadeSlices(uint32_t a_atlasSlices);

	/** @brief Re-derives the camera-anchored placement fields of a ShellCB (the fields' frame) from THIS frame's camera. Shared by DrawShell, S1 and the shadow passes, so no pass reads the fields through a frame-old placement (a stale one slides a crisp shadow during camera motion). Implemented in SnowDeformation/Shell.cpp. */
	void RefreshShellGridPlacement(ShellCB& a_cb, const float4* a_camAdjust = nullptr);

	/** @brief Queries adapter VRAM usage/budget in MB via IDXGIAdapter3 (zeros when unavailable). Usage above budget = driver demotion to system RAM = the large persistent FPS-loss mode that survives disabling features. */
	void QueryAdapterVRAM(uint64_t& a_usageMB, uint64_t& a_budgetMB, uint64_t* a_nonLocalMB = nullptr);
	/** @brief VRAM-Aware Trench Map, from Prepass: drops a trench map above 2048 to 2048 for the session when the adapter nears its VRAM budget. */
	void TickVRAMLog();

	Buffer* pointShadowLights = nullptr;

	winrt::com_ptr<ID3D11Texture2D> pointShadowLiveTex;
	winrt::com_ptr<ID3D11ShaderResourceView> pointShadowLiveSRV;
	/** @brief The array the local-light shadows sample this frame (t39, in place), or null. */
	ID3D11ShaderResourceView* PointShadowAtlasSRV() const { return pointShadowLiveSRV.get(); }

	PointShadowLightData pendingPointShadows[kPointShadowMaxLights] = {};

	/** @brief Called from State::Draw at a LOCAL light's shadow-mask pass (Utility RenderShadowmaskSpot/Pb/Dpb): the only moment the light's descriptor is live and PS t4 holds the local map array. Snapshots every live local descriptor. Implemented in SnowDeformation/Shadows.cpp. */
	void CapturePointShadowMask();

	/** @brief Uploads the pending point-shadow table (t38). The table stays empty until mask-time snapshots exist. Implemented in SnowDeformation/Shadows.cpp. */
	void UpdatePointShadowLights();

	/** @brief Per-object constants for the height, water and contact captures. Layout must match StaticCBFields.hlsli. */
	struct alignas(16) StaticsCB
	{
		float4 WorldRow0;
		float4 WorldRow1;
		float4 WorldRow2;
		float2 HeightWindowCenter;
		float HeightHalfExtent;
		float pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(StaticsCB);
	static_assert(sizeof(StaticsCB) == 64, "StaticsCB must match the fxc-reflected StaticCB of SnowHeightCapture and SnowContactCapture");

	template <class F>
	static void TimedTraverse(RE::NiAVObject* a_root, F&& a_visitor)
	{
		RE::BSVisit::TraverseScenegraphCollision(a_root, std::ref(a_visitor));
	}

	static constexpr uint kHeightMapDim = 2048;
	/** @brief The object height map's half-extent in world units. Every window fill, texel derivation and shader mirror must take it from here: the texel size is derived from it. */
	static constexpr float ObjectRasterHalfExtent() { return kObjectRasterReachM * kUnitsPerMeter; }
	bool cityWorldspace = false;
	/** @brief Landscape shell and road class are off this frame: the walled cities keep the game's own ground. */
	bool GroundShellsSuspended() const { return cityWorldspace; }
	/** @brief 1 - Snow Resistance: the share of a body's reach into the layer that carves. */
	float BodyPrintScale() const { return 1.0f - std::clamp(settings.SnowResistance, 0.0f, 100.0f) * 0.01f; }
	/** @brief Interior cell: no landscape exists, so no ground shell and no trench can; object snow is unaffected. */
	bool InInteriorCell() const
	{
		auto* tes = globals::game::tes;
		return tes && tes->interiorCell;
	}
	float objectRasterHalfExtentBuilt = 0.0f;

	static constexpr float kHeightMapEmptyTop = -100000.0f;
	static constexpr float kHeightMapEmptyBottom = 100000.0f;

	Texture2D* heightTopRaw[2] = { nullptr, nullptr };
	Texture2D* heightBottomRaw[2] = { nullptr, nullptr };
	Texture2D* heightRaster = nullptr;
	Texture2D* heightBottomFiltered = nullptr;
	Texture2D* heightTileEmpty = nullptr;
	static constexpr int kQualityPresetCount = 3;
	/** @brief Quality presets, Low (0) to High (2); High is the shipped defaults. Sets every preset lever; the rest of the settings keep their values. */
	void ApplyQualityPreset(int a_preset);
	/** @brief The preset every lever currently matches, or -1 (custom). */
	int MatchingQualityPreset() const;
	uint heightCurrent = 0;
	bool heightMapValid = false;
	bool heightMapsEmpty = false;
	float2 heightWindowCenter = { 0, 0 };
	float mainViewportMinDepth = 0.0f;
	float mainViewportMaxDepth = 1.0f;
	uint32_t mainViewportFrame = UINT32_MAX;

	winrt::com_ptr<ID3D11BlendState> heightMaxBlendState;
	winrt::com_ptr<ID3D11BlendState> heightRasterBlendState;
	Texture2D* waterHeightTexture = nullptr;
	Texture2D* waterFineTexture = nullptr;
	Texture2D* waterVisibleTexture = nullptr;
	Texture2D* waterNearTexture = nullptr;
	bool waterCoverDirty = true;
	uint32_t waterCoverTerrainVersion = 0;
	uint32_t waterWindowVersion = 0;
	void CoverWaterWindow();
	float2 waterFineCenter = { 0, 0 };
	float waterFineHalf = 0.0f;
	bool waterFineValid = false;
	static constexpr int kWaterFineDim = 1024;
	int waterWindowCellX = INT_MIN;
	int waterWindowCellY = INT_MIN;
	struct WaterBakeKey
	{
		const void* vb;
		const void* ib;
		uint64_t descKey;
		uint32_t indexCount;
		RE::NiTransform world;
		float planeConstant;
	};
	std::vector<WaterBakeKey> waterBakeKeys;
	std::vector<WaterBakeKey> waterBakeScratch;
	bool waterBakeValid = false;
	winrt::com_ptr<ID3D11VertexShader> waterCaptureVS;
	winrt::com_ptr<ID3D11PixelShader> waterCapturePS;
	winrt::com_ptr<ID3DBlob> waterCaptureVSBlob;
	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> waterILCache;
	bool waterCaptureShadersFailed = false;
	bool waterFirstLogged = false;
	void RenderWaterCapture();
	winrt::com_ptr<ID3D11VertexShader> heightVS;
	winrt::com_ptr<ID3D11PixelShader> heightPS;
	winrt::com_ptr<ID3D11ComputeShader> heightScrollRasterCS;
	std::vector<uint8_t> heightRasterKey[3];
	winrt::com_ptr<ID3D11ComputeShader> heightCombineCS;
	winrt::com_ptr<ID3D11ComputeShader> heightWaterCoverCS;

	/** @brief Per-update constants for the height-window processing and the height capture PS. Layout must match HeightProcessFields.hlsli. */
	struct alignas(16) HeightProcessCB
	{
		DirectX::XMINT2 ScrollDelta;
		uint ClearAll;
		uint pad0;

		float2 HeightWindowCenter;
		float HeightHalfExtent;
		float pad1;

		float2 TerrainWindowOrigin;
		float TerrainTexelSize;
		uint TerrainDim;

		/** @brief ScrollCS's ghost merge: the camera clip transform, to ask whether the surface that wrote a stale texel was still submitted this frame. */
		Matrix GhostViewProj;
		float4 GhostCameraPosAdjust;
	};
	STATIC_ASSERT_ALIGNAS_16(HeightProcessCB);
	static_assert(sizeof(HeightProcessCB) == 128, "HeightProcessCB must match the fxc-reflected layout of HeightProcessFields.hlsli");

	ConstantBuffer* heightProcessCB = nullptr;

	static constexpr uint kMaxExclusions = 256;
	static constexpr float kTorchClearRadius = 40.0f;
	static constexpr float kElevatedStampCutoff = 70.0f;

	static constexpr float kHeatClearRadiusMin = 40.0f;
	static constexpr float kHeatClearRadiusMax = 90.0f;
	static constexpr float kFireClearRadius = 300.0f;
	static constexpr float kRaisedFlameClearRadius = 80.0f;
	static constexpr float kUnknownHeatClearRadius = 140.0f;

	struct HeatSourceSpec
	{
		const char* substring;
		float radius;
	};
	static constexpr HeatSourceSpec kHeatSpecs[] = {
		{ "giantcampfire", 450.0f },
		{ "bonfire", 450.0f },
		{ "campfire", 300.0f },
		{ "firepit", 300.0f },
		{ "hearth", 300.0f },
		{ "smolder", 160.0f },
		{ "brazier", 150.0f },
		{ "cookingspit", 140.0f },
		{ "cookingpot", 140.0f },
		{ "sconce", 80.0f },
		{ "torch", 80.0f },
	};

	struct TrampleSpec
	{
		const char* substring;
		float radius;
		float forwardBias;
		bool ownsFlames = false;
		float meltStrength = 0.0f;
		float aspect = 1.0f;
		bool smoothEdge = false;
	};
	static constexpr TrampleSpec kTrampleSpecs[] = {
		{ "smelter", 300.0f, 0.0f, true },
		{ "forge", 260.0f, 0.0f, true },
		{ "sawmill", 220.0f, 0.0f },
		{ "millsaw", 220.0f, 0.0f },
		{ "stables", 200.0f, 0.0f },
		{ "chopping", 150.0f, 50.0f },
		{ "enchanting", 110.0f, 60.0f },
		{ "alchemy", 110.0f, 60.0f },
		{ "workbench", 140.0f, 0.0f },
		{ "sharpeningwheel", 130.0f, 60.0f },
		{ "tanningrack", 120.0f, 60.0f },
		{ "anvil", 120.0f, 50.0f },
		{ "marketstall", 110.0f, 0.0f },
		{ "well01", 110.0f, 0.0f },
		{ "shrine", 90.0f, 0.0f },
		{ "bedroll", 160.0f, 0.0f, false, 1.0f, 1.75f, true },
		{ "haymound", 120.0f, 0.0f, false, 1.0f, 1.0f, true },
		{ "haybale", 100.0f, 0.0f, false, 1.0f, 1.0f, true },
	};

	static constexpr const char* kSealedContainerSubstrings[] = {
		"sarcophagus",
	};

	static constexpr float kGroundFireBand = 40.0f;

	static constexpr uint kExclusionFieldDim = 1024;
	static constexpr float kExclusionFieldHalfExtent = 16384.0f;

	Texture2D* exclusionFieldTexture = nullptr;
	float2 exclusionFieldCenter = { 0.0f, 0.0f };
	bool exclusionFieldValid = false;

	/** @brief Layout must match ExclusionFieldCB in ExclusionFieldCS.hlsl. */
	struct alignas(16) ExclusionFieldCB
	{
		float2 FieldCenter;
		float padHalfExtent;
		float FieldTexelSize;

		float2 TerrainWindowOrigin;
		float TerrainTexelSize;
		uint TerrainDim;
	};
	STATIC_ASSERT_ALIGNAS_16(ExclusionFieldCB);
	ConstantBuffer* exclusionFieldCB = nullptr;

	/** @brief SRV of the wide exclusion field; null before SetupResources. */
	ID3D11ShaderResourceView* GetExclusionFieldSRV() const { return exclusionFieldTexture ? exclusionFieldTexture->srv.get() : nullptr; }

	/** @brief Layout must match ExclusionCB in SnowExclusions.hlsli. */
	struct alignas(16) ExclusionsCB
	{
		float4 PosRadius[kMaxExclusions];
		float4 DirExtType[kMaxExclusions];
		uint ExclusionCount;
		float pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(ExclusionsCB);
	static_assert(offsetof(ExclusionsCB, ExclusionCount) == sizeof(float4) * kMaxExclusions * 2, "exclusionListBuffer mirrors the two leading arrays");
	ConstantBuffer* doorsCB = nullptr;
	StructuredBuffer* exclusionListBuffer = nullptr;
	uint64_t exclusionListHash = 0;
	bool doorsCBUploaded = false;
	int exclusionBakeCellX = INT_MIN;
	int exclusionBakeCellY = INT_MIN;
	uint32_t exclusionBakeTerrainVersion = 0;
	std::unique_ptr<ExclusionsCB> exclusionBakeList;
	/** @brief Bakes the wide exclusion field from the gathered exclusion list, or skips when nothing it reads has changed. Implemented in SnowDeformation/Statics.cpp. */
	void RenderExclusionField(const ExclusionsCB& a_list);
	uint32_t doorRefreshCounter = 0;
	std::vector<std::pair<float4, float4>> staticExclusions;
	std::vector<float4> heatSources;
	std::vector<float4> exclusionPendingHeat;
	std::vector<std::pair<float4, float4>> exclusionPending;
	std::vector<size_t> exclusionPendingFlames;
	std::vector<float4> exclusionPendingGuards;
	uint32_t exclusionGatherSlot = 0;
	uint32_t exclusionGatherSlots = 0;
	bool exclusionGatherInterior = false;
	RE::NiPoint3 exclusionGatherOrigin;
	void StartExclusionGather();
	void StepExclusionGather();
	RE::BSContainer::ForEachResult ExclusionVisit(RE::TESObjectREFR* a_ref);
	void FinishExclusionGather();
	RE::BGSListForm* survivalHeatSources = nullptr;
	bool survivalHeatSourcesResolved = false;

	/** @brief Creates the height-window textures. Implemented in SnowDeformation/Statics.cpp. */
	void CreateHeightFieldResources();
	/** @brief Scrolls the accumulated height maps to the new window position and rasterizes this frame's captured statics into them (MAX/MIN). Called from DrawShell before the screen-space passes. Implemented in SnowDeformation/Statics.cpp. */
	void RenderObjectHeightMap();
	/** @brief Bare square, nothing captured, no volume snow: no reader of the object fields runs, so the block is skipped and its rows read 0; the next run rebuilds from a clear. */
	void SkipObjectHeightMap();
	/** @brief Interior with nothing reading the object fields and no ghost in the maps: the window and the exclusion walk advance as a run's would; no GPU work. */
	void IdleObjectHeightMap();
	/** @brief The maps hold sentinels only, are valid, and were built at the current reach. */
	bool HeightMapIdleReady() const;
	float4 heightGhostBox = {};
	bool heightGhostsLive = true;
	bool cascadeSnowSoonLast = true;
	Matrix cascadeGateVP{};
	float4 cascadeGateCam{};
	uint32_t cascadeGateFrame = UINT32_MAX;
	bool cascadeGateMeets = true;
	uint32_t cascadeCountFrame = UINT32_MAX;
	static constexpr int kGatherJumpTexels = 32;

	winrt::com_ptr<ID3DBlob> heightVSBlob;
	bool staticsShadersFailed = false;
	ConstantBuffer* staticsCB = nullptr;
	winrt::com_ptr<ID3D11DeviceContext1> staticsContext1;
	winrt::com_ptr<ID3D11Buffer> staticsRecordCB[2];
	bool staticsRecordChecked = false;
	static constexpr uint32_t kStaticsRecordStride = 256;
	static constexpr uint32_t kStaticsRecordStart = 8192;
	static constexpr uint32_t kStaticsRecordMax = 65536;
	uint32_t staticsRecordCapacity = 0;
	uint32_t staticsRecordRefused = 0;
	bool EnsureStaticsRecordCB(uint32_t a_count);
	/** @brief a_needed (optional, one byte per record): records left out are not copied; it must cover every record a draw binds. */
	bool UploadStaticsRecords(const StaticsCB* a_records, uint32_t a_count, const uint8_t* a_needed = nullptr);
	void BindStaticsRecord(uint32_t a_index, bool a_pixelStage, bool a_tessStages, uint32_t& a_parity);
	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> staticsILCache;
	/** @brief Input layout for a vertex descriptor, created on first sight against the height capture VS (POSITION+NORMAL; any VS reading a subset binds to it). Null is cached for descriptors that cannot be laid out. */
	ID3D11InputLayout* StaticsInputLayoutFor(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc);

	Texture2D* contactHeight = nullptr;
	winrt::com_ptr<ID3D11BlendState> contactMinBlendBodies;
	winrt::com_ptr<ID3D11BlendState> contactMinBlendProps;
	winrt::com_ptr<ID3D11RasterizerState> contactRasterState;
	winrt::com_ptr<ID3D11VertexShader> contactVS;
	winrt::com_ptr<ID3D11PixelShader> contactPS;
	bool contactShadersFailed = false;
	struct ContactProp
	{
		RE::ObjectRefHandle ref;
		float minX, minY, maxX, maxY;
		bool corpse = false;
		float groundZ = 0.0f;
		float layer = 0.0f;
	};
	std::vector<ContactProp> contactProps;
	std::unordered_set<const void*> contactSkinLogged;
	float2 contactCenter = { 0, 0 };
	uint contactDrawsLast = 0;
	/** @brief Bone palette for one skin partition, plus the capture window. Absolute world rows, so the VS needs no object transform and no pivot. */
	struct alignas(16) ContactSkinCB
	{
		float4 BoneRows[240];
		float2 SkinWindowCenter;
		float SkinHalfExtent;
		float SkinBoneCount;
	};
	STATIC_ASSERT_ALIGNAS_16(ContactSkinCB);
	ConstantBuffer* contactSkinCB = nullptr;
	winrt::com_ptr<ID3D11VertexShader> contactSkinVS;
	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> contactSkinILCache;
	winrt::com_ptr<ID3DBlob> contactSkinVSBlob;
	/** @brief a_dynamicPositionBytes 0 = positions in the game's vertex buffer; 16 or 8 = a second stream of float4/half4 positions copied from a BSDynamicTriShape's dynamicData (RaceMenu BodyMorph, OBody, facegen). */
	ID3D11InputLayout* ContactSkinInputLayoutFor(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc, uint32_t a_dynamicPositionBytes);
	winrt::com_ptr<ID3D11Buffer> contactDynamicVB;
	uint32_t contactDynamicVBBytes = 0;
	std::unordered_set<const void*> contactDynamicLogged;
	std::vector<ContactProp> contactActors;
	uint contactCorpseCount = 0;
	std::vector<uint32_t> contactPriority;
	std::vector<RE::NiTransform> contactPaletteScratch;
	std::vector<uint8_t> contactPaletteBuilt;
	uint contactSkinDrawsLast = 0;
	uint contactCarriedLast = 0;
	struct ContactSkinAudit
	{
		const void* partition = nullptr;
		uint8_t indexing = 0;
	};
	std::unordered_map<const void*, ContactSkinAudit> contactSkinIndexing;
	struct ContactSweep
	{
		RE::NiTransform world;
		uint32_t frame = 0;
		const void* buffer = nullptr;
	};
	std::unordered_map<const void*, ContactSweep> contactSweepStates;
	uint32_t contactSweepFrame = 0;
	static constexpr float kContactSweepStep = 6.0f;
	static constexpr uint32_t kContactMaxSweep = 8;
	std::unordered_set<const void*> contactSkinMissingLogged;
	std::unordered_set<const void*> contactSkinHiddenLogged;
	std::unordered_set<uint32_t> incorporealLogged;
	std::unordered_set<uint32_t> seeThroughKeptLogged;
	static constexpr float kContactSkipMargin = 16.0f;
	static constexpr uint16_t kContactStarveFrames = 60;
	uint contactStarveLines = 0;
	bool EnsureContactResources();
	/** @brief The contact capture; a_coatOnly (S5 round 8, while the deformation passes sleep) walks only the actors the body coat's snowfall is due. */
	void DrawContactCapture(ID3D11DeviceContext* a_context, bool a_coatOnly = false);

	/** @brief Compiles the object height raster's shaders on first use, keeping the VS bytecode for the input layouts. Implemented in SnowDeformation/Statics.cpp. */
	bool EnsureHeightRasterShaders();
	/** @brief Creates the input layouts of this frame's layout-making captures, which the object height raster finds from the next frame on. Implemented in SnowDeformation/Statics.cpp. */
	void CreateCaptureLayouts();

	/** @brief Caches a "tile is snow material" bitmask per landscape quad material, for the terrain shader's per-tile snow detection. */
	void TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	/** @brief Publishes the cached snow mask for the material about to be drawn via ExtraFeatureDescriptor bits 11-16. */
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);

	/** @brief Landscape snow (LANDSCAPE-SNOW-PLAN): hooks Lighting RestoreGeometry and Utility Setup/RestoreGeometry; Lighting's SetupGeometry side rides the statics capture hook. Implemented in LandscapeSnow.cpp. */
	void InstallLandscapeSnow();
	/** @brief After the game's SetupGeometry: captures a depth-prepass landscape draw for the ground snow's replay, and moves a sun-cascade one onto LandscapeSnow.hlsl, tessellated near the camera and through the raising VS beyond. */
	void LandscapeSnowSetup(RE::BSRenderPass* a_pass, bool a_lighting);
	/** @brief Before the game's RestoreGeometry: puts back everything LandscapeSnowSetup changed. */
	void LandscapeSnowRestore();
	/** @brief After a Lighting draw went out (its RestoreGeometry): the first deferred-pass draw of the frame records the main pass's viewport depth range (mainViewportMaxDepth). */
	void NoteMainViewport();
	/** @brief Rebuilds the landscape's ShellCB copy and SRV set from the live fields; false while one is missing or the footprint holds no snow. */
	bool LandscapeSnowRefresh();
	/** @brief The shell surface's grid and field windows for this frame, around a_cb's camera: DrawShellEarlyPrepass's recipe. */
	void PlaceShellFields(ShellCB& a_cb);
	static constexpr uint32_t kShellSurfaceSRVs = 59;
	void LandscapeSurfaceSRVs(ID3D11ShaderResourceView** a_srvs);
	/** @brief The ground snow can draw this frame: the replay's shaders are ready and this frame's depth prepass has been captured. Counts the frames that wanted it and drew no ground. */
	bool LandscapeShadeReady();
	/** @brief A blended class depth under the land-blend rule: positive depth scaled by smoothstep(kLandBlendDepthStart, 1, snow weight); unchanged when the feature is off. Shared by the window bake and GetNominalSnowDepthAt. */
	float LandBlendDepth(float a_depth, float a_snowWeight) const;
	/** @brief The resolve's depth bonus (log2 odds per unit of remaining snow) and carve penalty (log2 odds at a full carve). */
	float2 LandBlendContest() const;
	/** @brief The ground snow's prepass geometry: draws this frame's captured depth-prepass landscape through LandscapeSnowShell.hlsl. Binds VS/HS/DS, the HS/DS fields on a_cb (a_shellCB at b0) and the IA; the caller's PS, targets and depth state stay. IA buffers and the b11 slots go back as found. Returns the draws issued. */
	uint32_t DrawLandscapeSnowPrepass(ID3D11DeviceContext* a_context, const ShellCB& a_cb, ID3D11Buffer* a_shellCB);
	static constexpr float kLandBlendDepthStart = 0.50f;
	static constexpr float kLandRecolorLumLo = 0.48f;
	/** @brief The recolor runs this frame: the ground snow draws here and the snow set is loaded. */
	bool LandRecolorActive() const { return settings.EnableSnowDeformation && !GroundShellsSuspended() && shellSnowDiffuseSRV != nullptr; }

protected:
	void GatherStamps(PerFrame& perFrameData);

	std::unordered_map<uintptr_t, uint8_t> snowMasks;
	std::shared_mutex snowMaskMutex;

	std::unordered_map<uint64_t, ShellCellData> shellCells;
	std::unordered_map<uint64_t, uint32_t> shellFillerCells;
	std::unordered_set<uint64_t> shellSnowyCells;
	mutable std::shared_mutex shellSnowyCellMutex;
	enum SnowGateVerdict : uint32_t
	{
		kSnowGateSnowy = 0,
		kSnowGateUnknown = 1,
		kSnowGateBare = 2,
	};
	bool WindowHasSnow(float a_halfExtentUnits, bool a_unknownIsSnowy = false, uint32_t* a_verdictOut = nullptr) const;
	bool LoadedCellSquare(int& a_minX, int& a_minY, int& a_maxX, int& a_maxY) const;
	uint32_t CellRangeSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY) const;
	uint32_t CellWalkSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY, bool a_unknownIsSnowy) const;
	uint32_t ViewSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY, const ShellCB& a_cb);
	static constexpr float kViewGateMarginZ = 256.0f;
	std::unordered_map<uint64_t, std::pair<float, float>> viewGateCellZ;
	uint32_t viewGateCellZVersion = UINT32_MAX;
	void CullUnseenCaptures(const ShellCB& a_cb);
	float squareFloorZ = 0.0f;
	bool squareFloorValid = false;
	uint32_t squareFloorVersion = UINT32_MAX;
	int squareFloorKey[4] = { INT_MIN, INT_MIN, INT_MIN, INT_MIN };
	uint32_t captureViewCulled = 0;
	bool GroundNearHasSnow(float a_x, float a_y);
	std::unordered_map<uint64_t, uint8_t> groundSnowCellCache;
	uint32_t groundSnowCellCacheVersion = 0;
	uint32_t groundSnowFrame = UINT32_MAX;
	bool groundSnowFrameInterior = false;
	bool groundSnowFrameHasSquare = false;
	int groundSnowFrameSquare[4] = {};
	int groundSnowCacheSquare[4] = { INT_MIN, INT_MIN, INT_MIN, INT_MIN };
	uint8_t groundSnowSquareVerdict = kSnowGateUnknown;
	bool groundSnowSquareValid = false;
	bool DeformationWindowHasSnow() const { return WindowHasSnow(deformWorldSize * 0.5f, true, &deformSnowVerdict); }
	bool DeformationWindowHasReaders() const;
	bool ShellFootprintHasSnow() const { return WindowHasSnow(ShellWarpedHalfSpan()); }
	bool CascadeSnowSoon();
	using FootprintPlanes = std::array<std::array<double, 4>, 4>;
	static constexpr size_t kMaxSunFootprints = 4;
	bool SunCascadeFootprints(FootprintPlanes* a_out, size_t& a_count) const;
	bool SnowCellsMeetFootprints(const FootprintPlanes* a_planes, size_t a_count, float a_rise, bool a_soon);
	bool LsCascadeMeetsSnow();
	float LsRiseReach() const;
	mutable uint32_t deformSnowVerdict = kSnowGateUnknown;
	mutable uint32_t shellSnowVerdict = kSnowGateUnknown;
	uint32_t shellGateBareFrames = 0;
	static constexpr uint32_t kShellGateBareFrames = 30;
	bool deformSuspended = false;
	uint32_t deformGateBareFrames = 0;
	mutable std::shared_mutex shellCellMutex;
	std::atomic<bool> shellDataDirty{ true };
	uint32_t shellTerrainVersion = 0;
	std::atomic<uint32_t> snowDepthVersion{ 1 };
	std::vector<LandTextureEntry> landTextures;
	std::string textureFilter;
	std::unordered_map<uint32_t, uint16_t> landTextureByTextureSet;
	std::shared_mutex landTextureMutex;

	std::atomic<uint32_t> activeWorldspace{ 0 };
	uint32_t shellWindowWorldspace = 0;
	int shellWindowCellX = INT_MIN;
	int shellWindowCellY = INT_MIN;
	std::vector<float> shellUploadScratch;

	float2 windowOrigin = { 0, 0 };
	DirectX::XMINT2 pendingScrollDelta = { 0, 0 };
	DirectX::XMINT2 mapOrigin = { 0, 0 };
	bool clearRequested = true;

	float deformWorldSize = 14000.0f;
	bool trenchRangeDirty = false;
	uint deformMapDim = kTextureDim;
	bool deformMapDimDirty = false;
	uint deformMapVramCap = 0;
	bool rangeInitApplied = false;

	bool deformIdleSkipped = false;
	winrt::com_ptr<ID3D11Buffer> deformActivityBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> deformActivityUAV;
	static constexpr uint kDeformActivitySlots = 3;
	winrt::com_ptr<ID3D11Buffer> deformActivityStaging[kDeformActivitySlots];
	uint64_t deformActivitySlotSeq[kDeformActivitySlots] = {};
	bool deformActivityPending[kDeformActivitySlots] = {};
	uint64_t deformDispatchSeq = 0;
	uint64_t deformLastNonIdleSeq = 0;
	uint64_t deformFlagSeq = 0;
	bool deformFlagActive = true;
	bool deformActivitySlotEvolveRan[kDeformActivitySlots] = {};
	bool evolveFlagActive = true;
	uint64_t evolveVerdictSeq = 0;
	uint64_t evolveLastArmSeq = 0;
	std::vector<float4> lastStampSet;
	std::vector<float4> lastStampEnds;
	std::vector<uint8_t> stampMatchUsed;
	std::vector<uint32_t> stampMatchIndex;
	void PollDeformActivity(ID3D11DeviceContext* a_context);

	std::atomic<float> accumWeatherIntensity{ 0.0f };

	std::atomic<float> snowAccumulation{ 0.0f };
	void TickAccumulation();
	float GetAccumulationDepthScale() const;

	uint32_t stampGatherFrame = UINT32_MAX;

public:
	/** @brief The engine's own decal geometries, by the names it gives them: blood and impact marks, skinned or not. Implemented in SnowDeformation/Statics.cpp. */
	static bool IsEngineDecalName(const char* a_name);

protected:
public:
	virtual void DataLoaded() override;
	~SnowDeformation();
	/** @brief Worker body: every non-debug shader getter once, in visibility order. */
	void RunShaderPrime();
	std::atomic<int> snowPrimeState = 0;
	std::atomic<int> snowPrimePhase = 0;
	/** @brief True while the prime still owns this group's shader members - the render path must skip. */
	bool SnowShadersPending(int a_group) const
	{
		return snowPrimeState.load(std::memory_order_acquire) == 1 &&
		       snowPrimePhase.load(std::memory_order_acquire) < a_group;
	}
	std::thread snowPrimeThread;

protected:
	static constexpr UINT kHorizonSnowAlbedoSlot = 33;
	static constexpr UINT kHorizonSnowNormalSlot = 23;
	static constexpr UINT kWaterWindowSlot = 75;
	static constexpr UINT kHorizonSnowRmaosSlot = 68;

public:
	/** @brief Applies pending range-setting changes (trench window resize + map clear). Called at Prepass start; the first call applies loaded settings. */
	void ApplyRangeSettings();

protected:
	std::unordered_map<uint64_t, float2> stampPrevPositions;

	std::unordered_map<uint32_t, StampBones> stampBoneCache;

	bool ActorIsFloating(RE::Actor* a_actor, RE::NiAVObject* a_root, const StampBones* a_bones, bool a_useFeet, float a_groundZ, float* a_gapOut = nullptr, bool* a_byFeetOut = nullptr) const;

	bool ActorIsIncorporeal(RE::Actor* a_actor, RE::NiAVObject* a_root, StampBones* a_bones, float* a_alphaOut = nullptr) const;
	static bool IsProxyOrOverlayGeometry(const RE::BSGeometry* a_geometry, const RE::BSShaderProperty* a_property);

	struct PropAnchor
	{
		RE::NiPoint3 pos;
		uint32_t cycle = 0;
		bool mover = false;
	};
	std::unordered_map<uint32_t, PropAnchor> propPrevPositions;
	static constexpr uint32_t kPropScanInterval = 6;
	uint32_t propScanCycle = 0;
	std::vector<RE::TESObjectCELL*> propScanCells;
	uint32_t propScanFrame = 0;
	std::vector<RE::ObjectRefHandle> propScanMovers[kPropScanInterval];
	std::vector<RE::ObjectRefHandle> propScanHazards[kPropScanInterval];
	size_t PropScanHazardCount() const
	{
		size_t n = 0;
		for (const auto& slice : propScanHazards)
			n += slice.size();
		return n;
	}

	struct CorpseRest
	{
		uint16_t stillFrames = 0;
		bool settled = false;
		RE::NiPoint3 prevCenter;
		bool hasPrevCenter = false;
		RE::NiPoint3 restCenter;
		float prevZ = 0.0f;
		bool hasPrevZ = false;
	};
	std::unordered_map<uint32_t, CorpseRest> corpseRestStates;

	static constexpr float kImpactSpeedFloor = 400.0f;
	static constexpr float kImpactSpeedSmoothing = 0.1f;
	static constexpr float kFootTeleportSpeed = 4000.0f;
	static constexpr float kImpactKnockedDown = 0.6f;
	static constexpr float kRushEnergy = 0.5f;
	static constexpr float kRushRearm = 0.4f;
	struct ImpactTrack
	{
		RE::NiPoint3 prev;
		double time = 0.0;
		float speed = 0.0f;
		float fall = 0.0f;
		float speedEnergy = 0.0f;
		float landingEnergy = 0.0f;
		float knockEnergy = 0.0f;
		float landingSpeed = 0.0f;
		bool seen = false;
		bool wasInAir = false;
		/** @brief The largest of the three: what packs snow onto a body's coat, and shakes it off. */
		float Jolt() const { return std::max({ speedEnergy, landingEnergy, knockEnergy }); }
	};
	std::unordered_map<uint32_t, ImpactTrack> impactTracks;
	struct SpoilFoot
	{
		float2 last{};
		uint32_t frame = 0;
		bool planted = false;
	};
	std::unordered_map<uint64_t, SpoilFoot> spoilFeet;
};

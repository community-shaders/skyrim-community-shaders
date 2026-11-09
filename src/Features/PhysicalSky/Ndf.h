#pragma once

#include "Buffer.h"
#include "Utils/SceneBlend.h"
#include "Utils/Serialize.h"

struct TextureManager
{
	std::string name;
	uint64_t revision = 0;
	ankerl::unordered_dense::map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> texList;
	ankerl::unordered_dense::map<std::string, HRESULT> loadResults;

	bool LoadTexture(std::filesystem::path path);
	void EnsureLoaded(const std::string& path);
	void Reload();
	static std::string Key(const std::filesystem::path& path);

	inline ID3D11ShaderResourceView* Query(const std::string& path) const
	{
		const auto key = Key(std::filesystem::u8path(path));
		if (texList.contains(key))
			return texList.at(key).get();
		return nullptr;
	}

	inline std::vector<std::string> ListPaths()
	{
		std::vector<std::string> retval;
		std::ranges::transform(texList, std::back_inserter(retval), [](auto& pair) { return pair.first; });
		std::ranges::sort(retval);
		return retval;
	}

	std::string uiPath;
	void DrawUI();
};

namespace nlohmann
{
	void to_json(json&, const TextureManager&);
	void from_json(const json&, TextureManager&);
}

struct TexNdfSettings
{
	std::string heightPath;
	std::string modelingPath;
};

struct NdfNoiseParameters
{
	uint32_t type = 1;
	uint32_t seed = 1337;
	uint32_t frequency = 16;
	uint32_t octaves = 4;
	float persistence = 0.5f;
	uint32_t lacunarity = 2;
	float contrast = 1.f;
	float bias = 0.f;
	uint32_t repetitions = 1;
	float responseExponent = 1.f;
	float2 padding = {};
};
static_assert(sizeof(NdfNoiseParameters) == 48);

struct NdfNoiseInput
{
	NdfNoiseParameters parameters;
	std::string texturePath;
};

struct NdfNoiseLayer
{
	uint32_t noise = 2;
	float frequency = 1.f;
	float exponent = 1.f;
	float padding0 = 0.f;
	float2 offset = {};
	float2 padding1 = {};
	float4 range = { -1.f, 1.f, 0.f, 1.f };
};
static_assert(sizeof(NdfNoiseLayer) == 48);

struct NdfGenerationParameters
{
	NdfNoiseLayer primary = { .noise = 3, .exponent = 1.4f, .range = { -0.4f, 0.65f, 0.f, 0.65f } };
	NdfNoiseLayer secondary = { .noise = 1, .range = { -0.2f, 0.8f, 0.f, 0.4f } };
	NdfNoiseLayer coverageGain = { .range = { -1.f, 1.f, 1.f, 1.f } };
	NdfNoiseLayer modeling = { .exponent = 1.6f, .range = { -0.5f, 0.6f, 0.f, 0.5f } };
	NdfNoiseLayer modelingGain = { .range = { -1.f, 1.f, 1.f, 1.f } };
	NdfNoiseLayer heightVariation = { .range = { 0.f, 1.f, 0.f, 0.02f } };
	float4 heightAuxiliaryRange = { 0.f, 1.f, 0.f, 0.f };
	float4 bottomTypeRange = { -0.4f, 0.5f, 0.f, 0.25f };
	float2 baseHeight = { 0.f, 0.95f };
	float bottomTypeExponent = 1.f;
	uint32_t heightFromCoverage = 1;
	uint32_t localBlendMode = 0;
	float localModelingWeight = 1.f;
	float localHeightWeight = 1.f;
	float localWindScale = 1.f;
	float2 windOffset = {};
	uint32_t hasLocalMask = 0;
	uint32_t imported = 0;
	uint32_t preview = 0;
	float3 padding = {};
};
static_assert(sizeof(NdfGenerationParameters) == 384);

struct ProceduralNdfSettings
{
	NdfGenerationParameters parameters;
	std::array<NdfNoiseInput, 4> noise = { { { { .type = 0, .frequency = 4, .octaves = 4, .persistence = 0.5f, .lacunarity = 2, .contrast = 1.f, .bias = 0.f } },
		{ { .type = 1, .frequency = 4, .octaves = 6, .persistence = 0.6f, .contrast = 1.6f, .bias = -0.02f, .repetitions = 2, .responseExponent = 1.8f } },
		{ { .type = 1, .frequency = 32 } },
		{ { .type = 2, .frequency = 12, .contrast = 1.1f, .bias = -0.05f, .responseExponent = 2.f } } } };
	TexNdfSettings local;
	std::string localMaskPath;
};

enum class NdfType : uint32_t
{
	Texture = 0,
	Procedural = 1
};

enum class LocalNdfBlendMode : uint32_t
{
	Interpolate = 0,
	Maximum = 1
};

struct LocalNdfState
{
	std::string asset;
	std::string worldspace;
	bool enabled = true;
	float2 center = {};
	float2 size = { 16384.f, 16384.f };
	float rotation = 0.f;
	float altitude = 256.f;
	float heightSpan = 1792.f;
	float strength = 1.f;
	float modelingWeight = 1.f;
	float heightWeight = 1.f;
	float feather = 200.f;
	bool modelingAlpha = true;
	LocalNdfBlendMode modelingBlend = LocalNdfBlendMode::Interpolate;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LocalNdfState,
	asset, worldspace, enabled, center, size, rotation, altitude, heightSpan,
	strength, modelingWeight, heightWeight, feather, modelingAlpha, modelingBlend)

struct NdfSettings
{
	NdfType type = NdfType::Procedural;
	TexNdfSettings texture;
	ProceduralNdfSettings procedural;
	SceneBlend localCloud;
	float localTexelSize = 32.f;
};

struct NdfTextureSet
{
	ID3D11ShaderResourceView* height = nullptr;
	ID3D11ShaderResourceView* modeling = nullptr;
	explicit operator bool() const { return height && modeling; }
};

struct NdfManager
{
	constexpr static uint16_t kNdfDim = 512;
	eastl::unique_ptr<Texture2D> texHeight = nullptr;
	eastl::unique_ptr<Texture2D> texModeling = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> generatorProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> noiseProgram = nullptr;

	void SetupResources();
	void CompileShaders();
	static const char* GetSettingsTypeName(const NdfSettings& settings);
	static const char* GetSettingsHint(const NdfSettings& settings);

	/// Global map source: procedural versus texture pair, plus the texture pair slots.
	static void DrawNdfSource(NdfSettings& settings, TextureManager& textures);

	/// Scene-authorable weather shape: coverage, types, heights and remap intervals.
	static void DrawNdfShape(NdfGenerationParameters& parameters);

	/// The repeating global overlay and the scene-authorable local influence weights.
	static void DrawNdfOverlay(NdfSettings& settings, TextureManager& textures);

	/// Global noise slots that feed the weather shape.
	static void DrawNdfNoise(NdfSettings& settings, TextureManager& textures);

	bool UpdateNdf(const NdfSettings& settings, TextureManager& textures);
	NdfTextureSet GetNdf(const NdfSettings& settings, TextureManager& textures);

	const std::array<ID3D11ShaderResourceView*, 4>& GetNoiseInputs() const { return noiseInputs; }
	uint64_t GetRevision() const { return generatedRevision; }
	/// Tracks noise input changes independently of weather-field advection.
	uint64_t GetNoiseRevision() const { return noiseRevision; }
	/// Tracks every shared weather-noise slot, excluding low-cloud advection.
	uint64_t GetWeatherNoiseRevision() const { return weatherNoiseRevision; }
	float2 GetWeatherOffset() const { return generatedData.windOffset; }
	float4 GetHeightAuxiliaryRange() const { return generatedData.heightAuxiliaryRange; }
	/// Channel picker plus a preview of the generated weather map. Debug and the Clouds page share it.
	void DrawPreview(float a_size = 384.0f);

	static bool IsTextureNdf(ID3D11ShaderResourceView* srv, uint32_t channels);
	static bool IsLinearFormat(DXGI_FORMAT format, uint32_t channels);
	static bool IsTexturePair(NdfTextureSet maps);

private:
	static NdfTextureSet QueryTextures(const TexNdfSettings& settings, TextureManager& textures);
	eastl::unique_ptr<ConstantBuffer> generatorCb;
	eastl::unique_ptr<ConstantBuffer> noiseCb;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	std::array<eastl::unique_ptr<Texture2D>, 4> noiseTextures;
	std::array<NdfNoiseParameters, 4> generatedNoise = {};
	std::array<bool, 4> noiseValid = {};
	NdfGenerationParameters generatedData = {};
	std::array<ID3D11ShaderResourceView*, 7> generatedSources = {};
	std::array<std::string, 9> sourcePaths = {};
	uint64_t generatedRevision = 0;
	uint64_t noiseRevision = 0;
	uint64_t weatherNoiseRevision = 0;
	uint64_t textureRevision = 0;
	std::array<ID3D11ShaderResourceView*, 4> noiseInputs = {};
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> noiseInputResources;
	eastl::unique_ptr<Texture2D> texPreview;
	int preview = 0;
	bool generatedValid = false;
};

struct LowCloudSettings
{
	float ndfAltitudeOffset = 256.f;
	float ndfAltitudeScale = 1792.f;
	float curvatureRadius = 74.946f;
	float2 ndfScale = { 16.f, 16.f };
	float bottomSpreadScale = (1.f - 0.39f) / 0.75f;
	float bottomSpreadHeight = 0.30f;
	float bottomSoftness = 1.f;
	float bottomSoftnessHeight = 0.68f;
	float topExpansionScale = 1.f;
	float densityScale = 0.75f;

	float2 GetNdfAltitudeRangeKm() const;
	float GetCurvatureRadiusKm() const;
	float GetShadowBottomKm(float bottomKm, float rangeKm, uint32_t resolution) const;
};

struct CirrusNoiseLayer
{
	uint32_t noise = 2;
	float frequency = 1.f;
	float2 offset = {};
	float4 range = { -1.f, 1.f, 0.f, 1.f };
};
static_assert(sizeof(CirrusNoiseLayer) == 32);

struct CirrusWeatherState
{
	std::array<CirrusNoiseLayer, 2> layers = { { { .noise = 1, .range = { -0.7f, 0.7f, 0.f, 0.6f } }, {} } };
	std::string localMap;
	float localWeight = 0.f;
	float localWindScale = 1.f;
	uint32_t localBlendMode = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CirrusNoiseLayer,
	noise, frequency, offset, range)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CirrusWeatherState,
	layers, localMap, localWeight, localWindScale, localBlendMode)

struct MidCloudWeatherState
{
	// Retain the old coverage/type keys. Missing new fields are neutral so
	// existing scene endpoints and RGB maps keep their authored appearance.
	std::array<CirrusNoiseLayer, 2> layers = { { { .noise = 1, .range = { -0.7f, 0.7f, 0.f, 0.6f } },
		{ .range = { -1.f, 1.f, 0.65f, 0.65f } } } };
	CirrusNoiseLayer secondaryCoverage{ .noise = 3, .range = { -1.f, 1.f, 0.f, 0.f } };
	CirrusNoiseLayer coverageGain{ .range = { -1.f, 1.f, 1.f, 1.f } };
	CirrusNoiseLayer thicknessVariation{ .noise = 0, .range = { -1.f, 1.f, 0.f, 0.f } };
	std::string localMap;
	float localWeight = 0.f;
	float localWindScale = 1.f;
	uint32_t localBlendMode = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MidCloudWeatherState,
	layers, secondaryCoverage, coverageGain, thicknessVariation, localMap, localWeight, localWindScale, localBlendMode)

struct CirrusSettings
{
	CirrusSettings();
	bool enabled = true;
	float altitude = 8000.f;
	float uvSize = 1.f;
	float densityScale = 1.f;
	SceneBlend weather;

	float GetAltitudeKm() const;
	float GetUvSize() const;
};

struct CloudLightingSettings
{
	float brightness = 1.f;
	float sunExtinction = 1.f;
	float viewExtinction = 1.f;
	float dropletDiameter = 20.f;
	float phaseForwardG = 0.85f;
	float phaseBackwardG = -0.3f;
	float msStrength = 1.f;
	float msAlbedo = 0.999f;
	float msBoundaryWidth = 100.f;
	float ambientStrength = 1.f;
	float scatterDensityThreshold = 0.001f;
	float scatterDensityPower = 1.f;
	float cirrusLightThickness = 200.f;
};

struct CloudWindSettings
{
	float2 lowVelocity = { 5.f, 0.f };
	float2 highVelocity = { 10.f, 0.f };
	float development = 0.25f;
	float disturbance = 0.2f;

	static CloudWindSettings Interpolate(const CloudWindSettings& from, const CloudWindSettings& to, float weight);
};

struct MidCloudBands
{
	float direction = 0.f;
	float wavelength = 2000.f;
	float warp = 0.25f;
	float coverage = 0.f;
	float thickness = 0.f;
	float displacement = 0.f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MidCloudBands, direction, wavelength, warp, coverage, thickness, displacement)

struct MidCloudSettings
{
	MidCloudSettings();
	bool enabled = false;
	float altitude = 3500.f;
	float thickness = 800.f;
	float densityScale = 0.02f;
	float uvSize = 1.f;
	float edgeSoftness = 0.1f;
	float cellFrequency = 2.f;
	float cellStrength = 0.9f;
	float stratusCellStrength = 0.1f;
	float warpStrength = 0.1f;
	float detailScale = 256.f;
	uint32_t detailMode = 1;
	float heightPower = 1.f;
	MidCloudBands bands;
	SceneBlend weather;
	/// Conservative curved-layer support, in metres above the atmosphere base.
	float2 GetAltitudeRange() const;
};

struct MidCloudMapManager
{
	/// Allocate the weather-map accumulation pair and generation constants.
	void SetupResources();
	/// Recompile generation and invalidate the cached output.
	void CompileShaders();
	/// Regenerate changed scene/noise inputs; returns true when the map changes.
	bool Update(const MidCloudSettings& settings, TextureManager& textures, const NdfManager& ndf);
	/// Draw middle-layer and scene weather controls.
	static void DrawSettings(MidCloudSettings& settings, TextureManager& textures);
	/// Return the generated map, or null when generation is unavailable.
	ID3D11ShaderResourceView* Weather() const { return valid ? output.get() : nullptr; }

private:
	struct Parameters
	{
		std::array<CirrusNoiseLayer, 5> weather;
		float localWeight;
		uint32_t localBlendMode;
		float weight;
		uint32_t hasPrevious;
		float4 shape;
	};
	static_assert(sizeof(Parameters) == 192);
	std::array<eastl::unique_ptr<Texture2D>, 2> maps;
	eastl::unique_ptr<ConstantBuffer> parameters;
	winrt::com_ptr<ID3D11ComputeShader> shader;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	winrt::com_ptr<ID3D11ShaderResourceView> output;
	std::string key;
	uint64_t textureRevision = 0, noiseRevision = 0;
	bool valid = false;
};

struct CloudLayer
{
	LowCloudSettings low;
	MidCloudSettings middle;
	CirrusSettings cirrus;
	CloudLightingSettings lighting;
	CloudWindSettings wind;
};

struct CirrusTextureSet
{
	ID3D11ShaderResourceView* weather = nullptr;
	ID3D11ShaderResourceView* patterns = nullptr;
	explicit operator bool() const { return weather && patterns; }
};

struct CirrusMapManager
{
	void SetupResources();
	void CompileShaders();
	bool ShadersReady(const CirrusSettings& settings) const;
	static void DrawSettings(CirrusSettings& settings, CloudLightingSettings& lighting, TextureManager& textures);
	bool Update(const CirrusSettings& settings, TextureManager& textures, const NdfManager& ndf);
	CirrusTextureSet GetTextures() const;

private:
	static constexpr uint32_t kDimension = 512;
	struct GenerationParameters
	{
		std::array<CirrusNoiseLayer, 2> weather;
		float2 windOffset = {};
		float localWeight = 0.f;
		float localWindScale = 1.f;
		uint32_t localBlendMode = 0;
		float weight = 1.f;
		uint32_t hasPrevious = 0;
		float padding = 0.f;
	};
	static_assert(sizeof(GenerationParameters) == 96);
	std::array<eastl::unique_ptr<Texture2D>, 2> texWeather;
	eastl::unique_ptr<ConstantBuffer> generationCb;
	winrt::com_ptr<ID3D11ComputeShader> weatherProgram;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	std::string generatedKey;
	float2 generatedWind = {};
	uint64_t generatedRevision = 0;
	uint64_t noiseRevision = 0;
	bool generatedValid = false;
	CirrusTextureSet outputs;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> outputResources;
};

#pragma once

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyD3D11.hpp>

#include <Buffer.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <FeatureBuffer.h>

class State
{
public:
	static State* GetSingleton()
	{
		static State singleton;
		return &singleton;
	}

	bool enabledClasses[RE::BSShader::Type::Total - 1];
	bool enablePShaders = true;
	bool enableVShaders = true;
	bool enableCShaders = true;

	bool updateShader = true;
	bool settingCustomShader = false;
	RE::BSShader* currentShader = nullptr;
	std::string adapterDescription = "";

	uint32_t currentVertexDescriptor = 0;
	uint32_t currentPixelDescriptor = 0;
	spdlog::level::level_enum logLevel = spdlog::level::info;
	std::string shaderDefinesString = "";
	std::vector<std::pair<std::string, std::string>> shaderDefines{};  // data structure to parse string into; needed to avoid dangling pointers
	const std::string folderPath = "Data\\SKSE\\Plugins\\CommunityShaders";
	const std::string testConfigPath = "Data\\SKSE\\Plugins\\CommunityShaders\\SettingsTest.json";
	const std::string userConfigPath = "Data\\SKSE\\Plugins\\CommunityShaders\\SettingsUser.json";
	const std::string defaultConfigPath = "Data\\SKSE\\Plugins\\CommunityShaders\\SettingsDefault.json";

	bool upscalerLoaded = false;

	float timer = 0;

	enum ConfigMode
	{
		DEFAULT,
		USER,
		TEST
	};

	void Draw();
	void Reset();
	void Setup();

	void Load(ConfigMode a_configMode = ConfigMode::USER, bool a_allowReload = true);
	void Save(ConfigMode a_configMode = ConfigMode::USER);
	void PostPostLoad();

	bool ValidateCache(CSimpleIniA& a_ini);
	void WriteDiskCacheInfo(CSimpleIniA& a_ini);

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	void SetDefines(std::string defines);
	std::vector<std::pair<std::string, std::string>>* GetDefines();

	/*
     * Whether a_type is currently enabled in Community Shaders
     *
     * @param a_type The type of shader to check
     * @return Whether the shader has been enabled.
     */
	bool ShaderEnabled(const RE::BSShader::Type a_type);

	/*
     * Whether a_shader is currently enabled in Community Shaders
     *
     * @param a_shader The shader to check
     * @return Whether the shader has been enabled.
     */
	bool IsShaderEnabled(const RE::BSShader& a_shader);

	/*
     * Whether developer mode is enabled allowing advanced options.
	 * Use at your own risk! No support provided.
     *
	 * <p>
	 * Developer mode is active when the log level is trace or debug.
	 * </p>
	 * 
     * @return Whether in developer mode.
     */
	bool IsDeveloperMode();

	void ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_targetIndex, RE::BSGraphics::RenderTargetProperties* a_properties);

	void SetupResources();
	void ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred = false);

	void BeginPerfEvent(std::string_view title);
	void EndPerfEvent();
	void SetPerfMarker(std::string_view title);

	void SetAdapterDescription(const std::wstring& description);

	bool frameAnnotations = false;

	uint lastVertexDescriptor = 0;
	uint lastPixelDescriptor = 0;
	uint modifiedVertexDescriptor = 0;
	uint modifiedPixelDescriptor = 0;
	uint lastModifiedVertexDescriptor = 0;
	uint lastModifiedPixelDescriptor = 0;
	uint currentExtraDescriptor = 0;
	uint lastExtraDescriptor = 0;
	bool forceUpdatePermutationBuffer = true;

	enum class ExtraShaderDescriptors : uint32_t
	{
		InWorld = 1 << 0,
		IsReflections = 1 << 1,
		IsBeastRace = 1 << 2,
		EffectShadows = 1 << 3,
		IsDecal = 1 << 4
	};

	void UpdateSharedData(bool a_inWorld);

	struct alignas(16) PermutationCB
	{
		uint VertexShaderDescriptor;
		uint PixelShaderDescriptor;
		uint ExtraShaderDescriptor;
		uint pad0[1];
	};

	ConstantBuffer* permutationCB = nullptr;

	struct alignas(16) SharedDataCB
	{
		float4 WaterData[25];
		DirectX::XMFLOAT3X4 DirectionalAmbient;
		float4 DirLightDirection;
		float4 DirLightColor;
		float4 CameraData;
		float4 BufferDim;
		float Timer;
		uint FrameCount;
		uint FrameCountAlwaysActive;
		uint InInterior;
		uint InMapMenu;
		uint HideSky;
		float2 pad0;
	};

	ConstantBuffer* sharedDataCB = nullptr;
	ConstantBuffer* featureDataCB = nullptr;

	Util::FrameChecker frameChecker;
	uint frameCount = 0;

	// Skyrim constants
	float2 screenSize = {};
	D3D_FEATURE_LEVEL featureLevel;

	TracyD3D11Ctx tracyCtx = nullptr;  // Tracy context

	void ClearDisabledFeatures();
	bool SetFeatureDisabled(const std::string& featureName, bool isDisabled);
	bool IsFeatureDisabled(const std::string& featureName);
	std::unordered_map<std::string, bool>& GetDisabledFeatures();

	bool useFrameAnnotations = false;

	// Features that are more special then others
	std::unordered_map<std::string, bool> specialFeatures = {
		{ "Frame Generation", false },
		{ "Upscaling", false },
		{ "TruePBR", false },
	};
	std::unordered_map<std::string, bool> disabledFeatures;

	inline ~State()
	{
#ifdef TRACY_ENABLE
		if (tracyCtx)
			TracyD3D11Destroy(tracyCtx);
#endif
	}

private:
	std::shared_ptr<REX::W32::ID3DUserDefinedAnnotation> pPerf;
	bool initialized = false;
};

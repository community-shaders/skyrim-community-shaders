#pragma once

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyC.h>
#include <Tracy/TracyD3D11.hpp>

#include <Buffer.h>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#include <FeatureBuffer.h>

#include <Hooks.h>
#include <mutex>

class State
{
public:
	State()
	{
		std::lock_guard<std::mutex> lock(statsMutex);
		for (auto& v : smoothDrawCalls) v = 0.0;
		for (auto& v : drawCalls) v = 0;
		for (auto& v : frameTimePerType) v = 0.0f;
		for (auto& v : smoothFrameTimePerType) v = 0.0f;
		for (auto& v : enabledClasses) v = true;

		// Initialize QueryPerformanceCounter frequency
		frameTimingFrequency.QuadPart = 0;
		frameStartTime.QuadPart = 0;
	}
	/** @brief Acquires the stats mutex and returns a scoped lock guard. */
	std::lock_guard<std::mutex> Lock() { return std::lock_guard<std::mutex>(statsMutex); }

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

	float timer = 0;
	double smoothDrawCalls[RE::BSShader::Type::Total + 1];
	int drawCalls[RE::BSShader::Type::Total + 1];

	// Frame time tracking per shader type (in milliseconds)
	float frameTimePerType[RE::BSShader::Type::Total + 1];      ///< Per-type frame time in milliseconds.
	float smoothFrameTimePerType[RE::BSShader::Type::Total + 1]; ///< EMA-smoothed per-type frame time in milliseconds.

	// Timing state for per-type frame time tracking using QueryPerformanceCounter
	LARGE_INTEGER frameTimingFrequency;
	LARGE_INTEGER frameStartTime;
	bool frameTimingActive = false;

	enum ConfigMode
	{
		DEFAULT,
		USER,
		TEST,
		THEME
	};

	/** @brief Per-draw-call hook: updates feature state, constant buffers, and overlay. */
	void Draw();
	/** @brief Accumulates per-shader-type draw call counts and frame timing for the performance overlay. */
	void Debug();
	/** @brief Per-frame reset: advances timer, caches menu state, resets descriptors and frame counters. */
	void Reset();
	/** @brief One-time post-D3D setup: creates resources, probes GPU caps, initializes features. */
	void Setup();

	/**
	 * @brief Loads settings from disk (default, then user, then overrides).
	 * @param a_configMode Which config file to load.
	 * @param a_allowReload If true, retries once after a parse error.
	 */
	void Load(ConfigMode a_configMode = ConfigMode::USER, bool a_allowReload = true);
	/**
	 * @brief Persists current settings to the config file for the given mode.
	 * @param a_configMode Which config file to write.
	 */
	void Save(ConfigMode a_configMode = ConfigMode::USER);

	/**
	 * @brief Serializes all settings to a JSON object (in-memory, no disk I/O).
	 * @param o_json Output JSON object to populate.
	 */
	void SaveToJson(nlohmann::json& o_json);
	/**
	 * @brief Restores settings from a JSON object (in-memory, no disk I/O).
	 * @param i_json Input JSON object to read from.
	 */
	void LoadFromJson(nlohmann::json& i_json);

	/** @brief Loads the active theme preset from the menu settings. */
	void LoadTheme();
	/** @brief No-op kept for backward compatibility; theme is now saved with user settings. */
	void SaveTheme();

	/**
	 * @brief Validates the disk shader cache against all loaded features.
	 * @param a_ini The cache INI to validate against.
	 * @return True if all feature cache entries are still valid.
	 */
	bool ValidateCache(CSimpleIniA& a_ini);
	/**
	 * @brief Writes each feature's cache metadata into the disk cache INI.
	 * @param a_ini The cache INI to write into.
	 */
	void WriteDiskCacheInfo(CSimpleIniA& a_ini);

	/**
	 * @brief Sets the global log level and flushes on that level.
	 * @param a_level The spdlog severity level to apply.
	 */
	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	/**
	 * @brief Parses a semicolon-delimited "NAME=VALUE" string into shader defines.
	 * @param defines Semicolon-separated define string (e.g. "FOO=1;BAR=2").
	 */
	void SetDefines(std::string defines);
	std::vector<std::pair<std::string, std::string>>* GetDefines();

	/**
	 * @brief Checks whether the given shader type is enabled.
	 * @param a_type The type of shader to check.
	 * @return True if the shader type is enabled.
	 */
	bool ShaderEnabled(const RE::BSShader::Type a_type);

	/**
	 * @brief Checks whether the given shader is enabled.
	 * @param a_shader The shader to check.
	 * @return True if the shader is enabled.
	 */
	bool IsShaderEnabled(const RE::BSShader& a_shader);

	/**
	 * @brief Checks whether developer mode is active (log level is trace or debug).
	 *
	 * Developer mode enables advanced options. Use at your own risk.
	 * @return True if in developer mode.
	 */
	bool IsDeveloperMode();

	/**
	 * @brief Adds UAV access support to a render target.
	 * @param a_targetIndex The render target to modify.
	 * @param a_properties The target's properties (modified in place).
	 */
	void ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_targetIndex, RE::BSGraphics::RenderTargetProperties& a_properties);

	/** @brief Allocates constant buffers, Tracy context, and profiler resources. */
	void SetupResources();

	/**
	 * @brief Logs per-format support for D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD.
	 *
	 * We perform typed UAV loads on a number of non-guaranteed formats; on GPUs
	 * that lack TypedUAVLoadAdditionalFormats those reads return undefined data.
	 * Called once at startup; emits one info line per supported format and one
	 * warn line per unsupported format with the feature that needs it.
	 */
	void CheckTypedUAVLoadSupport();
	/**
	 * @brief Strips and rewrites shader descriptor bits for Community Shaders' pipeline.
	 * @param a_shader The shader being compiled.
	 * @param a_vertexDescriptor Vertex descriptor flags (modified in place).
	 * @param a_pixelDescriptor Pixel descriptor flags (modified in place).
	 * @param a_forceDeferred If true, forces the Deferred flag regardless of current pass.
	 */
	void ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred = false);

	/** @brief Opens a named GPU performance event (D3D annotation + Tracy zone). */
	void BeginPerfEvent(std::string_view title);
	/** @brief Closes the most recent GPU performance event. */
	void EndPerfEvent();
	/** @brief Inserts a single-point GPU performance marker. */
	void SetPerfMarker(std::string_view title);

	/** @brief Converts and stores the GPU adapter description from wide string. */
	void SetAdapterDescription(const std::wstring& description);

	bool frameAnnotations = false;

	// Pass D3DCOMPILE_PARTIAL_PRECISION to fxc. With explicit min16float types this is
	// mostly belt-and-braces in SM5, but it lets the compiler downgrade unmarked float
	// ops to FP16 where it can prove safety. On by default; toggle off when reversing
	// shaders or chasing a precision bug.
	// Atomic: written from the UI thread, read from compilation pool workers.
	std::atomic_bool enablePartialPrecision{ false };

	// Pass D3DCOMPILE_AVOID_FLOW_CONTROL to fxc. Forces the compiler to flatten branches
	// into predicated ops instead of using dynamic flow control. Can win on uniform-branch
	// or short-body branches; can lose on long divergent branches that vanilla flow
	// control would skip. Transient (session-only); not saved to config because the
	// right setting depends on the current scene/work, not the user.
	// Atomic: written from the UI thread, read from compilation pool workers.
	std::atomic_bool enableAvoidFlowControl{ false };

	uint lastVertexDescriptor = 0;
	uint lastPixelDescriptor = 0;
	uint modifiedVertexDescriptor = 0;
	uint modifiedPixelDescriptor = 0;
	uint lastModifiedVertexDescriptor = 0;
	uint lastModifiedPixelDescriptor = 0;
	uint lastExtraDescriptor = 0;
	uint lastExtraFeatureDescriptor = 0;

	/**
	 * Bitflags describing extra shader-specific properties.
	 */
	
	/**
	 * Bitflags describing extra feature-specific properties related to terrain displacement and material models.
	 */
	
	/**
	 * Checks whether the main menu or loading menu is cached as open.
	 * @returns true if either the main menu or loading menu is open, false otherwise.
	 */
	
	/**
	 * Checks whether the main menu or loading menu is open, querying the UI if provided.
	 * @param ui Pointer to the UI manager; if non-null, performs live menu checks as a fallback.
	 * @returns true if the main menu or loading menu is open, false otherwise.
	 */
	
	/**
	 * Updates the shared constant buffer data based on world state and rendering pass.
	 * @param a_inWorld Whether the camera is in world space.
	 * @param a_prepass Whether this is a prepass rendering phase.
	 */
	
	/**
	 * Updates sky shader permutation based on the current render pass.
	 * @param a_pass The render pass to inspect.
	 */
	
	/**
	 * Checks whether directional shadows are available for the current scene.
	 * @returns true if directional shadows are present, false otherwise.
	 */
	enum class ExtraShaderDescriptors : uint32_t
	{
		InWorld = 1 << 0,
		IsReflections = 1 << 1,
		IsBeastRace = 1 << 2,
		GrassSphereNormal = 1 << 3,
		IsSun = 1 << 4,
		SuppressExternalEmittance = 1 << 5
	};

	enum class ExtraFeatureDescriptors : uint32_t
	{
		THLand0HasDisplacement = 1 << 0,
		THLand1HasDisplacement = 1 << 1,
		THLand2HasDisplacement = 1 << 2,
		THLand3HasDisplacement = 1 << 3,
		THLand4HasDisplacement = 1 << 4,
		THLand5HasDisplacement = 1 << 5,
		ETMaterialModel = 0b111 << 6,
		THLandHasDisplacement = 1 << 9
	};

	bool inWorld = false;
	bool activeReflections = false;

	// Cached menu open states, updated once per frame in Reset().
	// Avoids repeated IsMenuOpen calls (each constructs a BSFixedString).
	bool isMainMenuOpen = false;
	bool isLoadingMenuOpen = false;
	bool isMapMenuOpen = false;
	/** @brief Returns true if the cached main-menu or loading-menu state is open. */
	bool IsMainOrLoadingMenuOpen() const { return isMainMenuOpen || isLoadingMenuOpen; }
	/** @brief Returns true if main/loading menu is open, with a live fallback query via the UI pointer. */
	bool IsMainOrLoadingMenuOpen(RE::UI* ui) const
	{
		return IsMainOrLoadingMenuOpen() ||
		       (ui && (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)));
	}

	void UpdateSharedData(bool a_inWorld, bool a_prepass);
	void UpdateSkyShaderPermutation(RE::BSRenderPass* a_pass);
	bool HasDirectionalShadows() const;

	struct PermutationCB
	{
		uint VertexShaderDescriptor;
		uint PixelShaderDescriptor;
		uint ExtraShaderDescriptor;
		uint ExtraFeatureDescriptor;

		float EffectRadius;
		float3 pad0;

		bool operator==(const PermutationCB& other) const
		{
			return PixelShaderDescriptor == other.PixelShaderDescriptor &&
			       ExtraShaderDescriptor == other.ExtraShaderDescriptor &&
			       ExtraFeatureDescriptor == other.ExtraFeatureDescriptor && EffectRadius == other.EffectRadius;
		}
	};
	STATIC_ASSERT_ALIGNAS_16(PermutationCB);

	ConstantBuffer* permutationCB = nullptr;

	struct alignas(16) SharedDataCB
	{
		float4 WaterData[25];
		DirectX::XMFLOAT3X4 DirectionalAmbient;
		float4 DirLightDirection;
		float4 DirLightColor;
		float4 SunDirection;
		float4 SunColor;
		float4 MasserDirection;
		float4 MasserColor;
		float4 SecundaDirection;
		float4 SecundaColor;
		float4 CameraData;
		float4 BufferDim;
		float Timer;
		uint FrameCount;
		uint FrameCountAlwaysActive;
		uint InInterior;
		uint HasDirectionalShadows;
		uint InMapMenu;
		uint HideSky;
		float MipBias;
		float WaterSystemHeight;  // TES::GetWaterHeight in camera-relative Z; -NI_INFINITY when no water body found
		float3 pad0;
		float4 AmbientSHR;
		float4 AmbientSHG;
		float4 AmbientSHB;
		float4 HDRData;  // xyz + menu scene encoding in w — see HDRDisplay::GetSharedDataHDR
	};
	STATIC_ASSERT_ALIGNAS_16(SharedDataCB);

	ConstantBuffer* sharedDataCB = nullptr;
	ConstantBuffer* featureDataCB = nullptr;

	PermutationCB permutationData{};
	PermutationCB permutationDataPrevious{};

	Util::FrameChecker frameChecker;
	uint frameCount = 0;
	// Thread-safe mirror of frameCount maintained by the render thread.
	// Off-thread readers (MCP listener, future telemetry) must read this
	// instead of touching frameCount directly to avoid a data race.
	std::atomic<uint32_t> frameCountAtomic{ 0 };

	// Skyrim constants
	D3D_FEATURE_LEVEL featureLevel;

	TracyD3D11Ctx tracyCtx = nullptr;  // Tracy context

	// Moon and Stars mod detection
	inline static bool moonAndStarsLoaded = false;

	void ClearDisabledFeatures();
	bool SetFeatureDisabled(const std::string& featureName, bool isDisabled);
	bool IsFeatureDisabled(const std::string& featureName);
	std::unordered_map<std::string, bool>& GetDisabledFeatures();

	bool useFrameAnnotations = false;

	// --- Utility Methods ---
	/**
	 * @brief Gets the total smoothed draw calls from the global state
	 * @return Total number of draw calls as float
	 */
	float GetTotalSmoothedDrawCalls() const;

	/**
	 * @brief Base helper that iterates through valid shader types (excluding None and Total)
	 * @param callback Function to call for each valid shader type with parameters: (type, typeIndex, classIndex)
	 */
	template <typename Callback>
	static void ForEachValidShaderType(Callback callback)
	{
		for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
			if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
				continue;
			int typeIndex = magic_enum::enum_integer(type);
			int classIndex = typeIndex - 1;
			callback(type, typeIndex, classIndex);
		}
	}

	/**
	 * @brief Iterates through valid shader types with performance metrics
	 * @param callback Function to call for each shader type with parameters: (type, typeIndex, drawCalls, frameTime, percent, costPerCall)
	 */
	template <typename Callback>
	static void ForEachShaderTypeWithMetrics(Callback callback)
	{
		ForEachValidShaderType([&](auto type, int typeIndex, [[maybe_unused]] int classIndex) {
			float drawCalls = static_cast<float>(GetSingleton()->smoothDrawCalls[typeIndex]);
			float frameTime = static_cast<float>(GetSingleton()->smoothFrameTimePerType[typeIndex]);
			float percent = (frameTime > 0.0f && GetSingleton()->smoothFrameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] > 0.0f) ?
			                    (frameTime / GetSingleton()->smoothFrameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] * 100.0f) :
			                    0.0f;
			float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
			callback(type, typeIndex, drawCalls, frameTime, percent, costPerCall);
		});
	}

	/**
	 * @brief Iterates through valid shader types with class indices for UI operations
	 * @param callback Function to call for each shader type with parameters: (type, classIndex)
	 */
	template <typename Callback>
	static void ForEachShaderTypeWithIndex(Callback callback)
	{
		ForEachValidShaderType([&](auto type, [[maybe_unused]] int typeIndex, int classIndex) {
			callback(type, classIndex);
		});
	}

	std::unordered_map<std::string, bool> disabledFeatures;
	std::mutex m_mutex;

	inline ~State()
	{
#ifdef TRACY_ENABLE
		if (tracyCtx)
			TracyD3D11Destroy(tracyCtx);
#endif
	}

private:
	std::shared_ptr<REX::W32::ID3DUserDefinedAnnotation> pPerf;
	std::mutex statsMutex;
};

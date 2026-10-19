#pragma once

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyC.h>
#include <Tracy/TracyD3D11.hpp>

#include <Buffer.h>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>

using json = nlohmann::json;

#include "SharedData.h"
#include <FeatureBuffer.h>

#include <Hooks.h>
#include <mutex>
#include <unordered_map>

struct Feature;

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
	RE::BSGraphics::VertexShader* customVertexShader = nullptr;
	RE::BSGraphics::PixelShader* customPixelShader = nullptr;
	RE::BSShader* currentShader = nullptr;
	std::string adapterDescription = "";

	uint32_t currentVertexDescriptor = 0;
	uint32_t currentPixelDescriptor = 0;
	spdlog::level::level_enum logLevel = spdlog::level::info;
	bool enableDeveloperMode = false;  ///< Explicit developer mode toggle; also enabled when log level is debug/trace.
	// Guarded by shaderDefinesMutex; read it through GetDefinesString().
	std::string shaderDefinesString = "";
	std::shared_ptr<const std::vector<std::pair<std::string, std::string>>> shaderDefines = std::make_shared<const std::vector<std::pair<std::string, std::string>>>();  // data structure to parse string into; needed to avoid dangling pointers
	std::mutex shaderDefinesMutex;

	float timer = 0;
	double smoothDrawCalls[RE::BSShader::Type::Total + 1];
	int drawCalls[RE::BSShader::Type::Total + 1];

	// Frame time tracking per shader type (in milliseconds)
	float frameTimePerType[RE::BSShader::Type::Total + 1];        ///< Per-type frame time in milliseconds.
	float smoothFrameTimePerType[RE::BSShader::Type::Total + 1];  ///< EMA-smoothed per-type frame time in milliseconds.

	// Timing state for per-type frame time tracking using QueryPerformanceCounter
	LARGE_INTEGER frameTimingFrequency;
	LARGE_INTEGER frameStartTime;
	bool frameTimingActive = false;

	/** @brief Per-draw-call hook: updates feature state, constant buffers, and overlay. */
	void Draw();
	/** @brief Accumulates per-shader-type draw call counts and frame timing for the performance overlay. */
	void Debug();
	/** @brief Per-frame reset: advances timer, caches menu state, resets descriptors and frame counters. */
	void Reset();
	/** @brief One-time post-D3D setup: creates resources, probes GPU caps, initializes features. */
	void Setup();

	/**
	 * @brief Which feature owns the HDR tonemap pass this frame.
	 *
	 * Effects11 and Post Processing both replace the vanilla ISHDR tonemap and cannot
	 * coexist: Effects11 skips the whole pass, while Post Processing relies on it running
	 * so ISHDR can take its passthrough branch. Exactly one owner is resolved per frame.
	 */
	enum class TonemapOwner : uint
	{
		kVanilla = 0,         ///< Vanilla ISHDR tonemap runs unmodified.
		kPostProcessing = 1,  ///< Post Processing pipeline drives tonemapping.
		kEffects11 = 2        ///< Effects11 (ENB-compatible) replaces the pass entirely.
	};

	/**
	 * @brief Resolves the tonemap owner for the current frame (cached once per frame).
	 *
	 * Effects11 wins when both features want the pass, because it applies a whole-preset
	 * look that degrades badly when partially applied. The choice is surfaced in both
	 * feature UIs so the loser is not silently disabled.
	 *
	 * @return The feature owning tonemapping this frame.
	 */
	TonemapOwner GetTonemapOwner();

	/**
	 * @brief Dispatches the HDR tonemap pass to the resolved owner.
	 * @param a_input Render target holding the scene color to tonemap.
	 * @param a_output Render target receiving the tonemapped result.
	 * @return True if the vanilla pass was fully replaced and must not be invoked.
	 */
	bool HandlePostProcessing(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output);

	/**
	 * @brief Loads settings: the code's defaults, then enabled Baseline packs, then the user's edits
	 *        from SettingsUser.json.
	 * @param a_allowReload If true, retries once after a parse error.
	 */
	void Load(bool a_allowReload = true);
	/** @brief Writes the user's edits and the core settings to SettingsUser.json. */
	void Save();
	/**
	 * @brief Writes every feature's code defaults to FeatureDefaults.json, for reference.
	 * @return The written file, or an empty path if the write failed.
	 */
	std::filesystem::path DumpFeatureDefaults() const;

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

	/**
	 * @brief Recombines defaults and the enabled Baseline packs under every loaded feature, keeping the
	 *        user's edits on top. Call after the set of Baseline packs, or their files, changes.
	 */
	void ReapplySettingsLayers();

	/**
	 * @brief Clears the user's edits to one feature, so it shows what defaults and Baseline packs give it.
	 * @param feature A loaded feature.
	 */
	void RestoreFeatureLayers(Feature* feature);

	/** @brief Loaded features whose user edits change settings a Baseline pack also sets. */
	std::vector<Feature*> FindEditsUnderPack(const std::string& packId);

	/** @brief Drops the user's edits to every setting a Baseline pack sets, so the pack shows as authored. */
	void ClearEditsUnderPack(const std::string& packId);

	/**
	 * @brief Whether the user's edits to a feature override settings an enabled Baseline pack sets.
	 * @details Cached per feature; InvalidateEditsUnderBaselines marks the answer stale after an edit.
	 */
	bool HasEditsUnderBaselines(Feature* feature);

	/** @brief Forgets the cached HasEditsUnderBaselines answer for one feature, or every feature when null. */
	void InvalidateEditsUnderBaselines(Feature* feature = nullptr);

	/** @brief Drops the user's edits to every setting an enabled Baseline pack sets on one feature. */
	void ClearFeatureEditsUnderBaselines(Feature* feature);

	/** @brief Notes that feature settings were edited, so the main menu's Save shows them as unsaved. */
	void MarkSettingsChanged() { settingsChanged = true; }

	/**
	 * @brief Marks the settings changed if a feature's settings now differ from what was last saved or
	 *        loaded. For actions ImGui does not flag as edits, such as a button that resets or applies a preset.
	 */
	void CheckFeatureSettingsChanged(Feature* feature);

	/** @brief Re-checks the sections that are not feature settings (Menu, General, ...) against the last save or
	 *  load. Cheap enough to call on any frame the main menu edited something. */
	void CheckCoreSettingsChanged();

	/**
	 * @brief A feature's settings in the layers beneath the user's edits: defaults with the enabled Baseline packs applied.
	 * @param featureName The feature's GetName().
	 * @return Null when the feature has no settings layers.
	 */
	const nlohmann::json* GetFeatureBase(const std::string& featureName) const
	{
		const auto found = featureBases.find(featureName);
		return found != featureBases.end() ? &found->second : nullptr;
	}

	/** @brief True when the user config has changes not yet saved. */
	bool HasUnsavedChanges() const { return settingsChanged || coreSettingsChanged; }

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
	std::shared_ptr<const std::vector<std::pair<std::string, std::string>>> GetDefines();
	/** @brief Returns a copy of the normalized define string; safe to call from any thread. */
	std::string GetDefinesString();

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
	 * @brief Checks whether developer mode is active.
	 *
	 * Active when Enable Developer Mode is on, or when log level is debug/trace.
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

	/**
	 * @brief Pixel descriptor bits a shader type reads at runtime although they also take part in the lookup.
	 * @param a_shader Shader whose descriptor is being uploaded.
	 */
	[[nodiscard]] static uint32_t GetRuntimePixelBits(const RE::BSShader& a_shader);

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

	// Keep compiled shaders in Data/ShaderCache/ContentStore, keyed by their preprocessed source and
	// compile inputs, so a cache wipe or plugin update restores unchanged shaders instead of recompiling.
	// Atomic: written from the UI thread, read from compilation pool workers.
	std::atomic_bool enableContentStore{ false };
	/// Size limit of the persistent shader store in MB; least recently used shaders beyond it are evicted.
	std::atomic<uint32_t> contentStoreMaxMB{ 4096 };
	static constexpr uint32_t kContentStoreMinMB = 512;
	static constexpr uint32_t kContentStoreMaxMB = 32768;

	uint lastVertexDescriptor = 0;
	uint lastPixelDescriptor = 0;
	uint modifiedVertexDescriptor = 0;
	uint modifiedPixelDescriptor = 0;
	uint lastModifiedVertexDescriptor = 0;
	uint lastModifiedPixelDescriptor = 0;
	uint lastExtraDescriptor = 0;
	uint lastExtraFeatureDescriptor = 0;

	/**
	 * Updates Lighting shader permutation state from the current render pass.
	 * @param a_pass The Lighting render pass whose blend state should be inspected.
	 */
	void UpdateLightingShaderPermutation(RE::BSRenderPass* a_pass);

	/** @brief Bitflags describing extra shader-specific properties. */
	enum class ExtraShaderDescriptors : uint32_t
	{
		InWorld = 1 << 0,
		IsReflections = 1 << 1,
		IsBeastRace = 1 << 2,
		GrassSphereNormal = 1 << 3,
		IsSun = 1 << 4,
		SuppressExternalEmittance = 1 << 5,
		AdditiveLighting = 1 << 6,
		IsEye = 1 << 7,
		SourceAlphaBlend = 1 << 8,
		NoSkyScattering = 1 << 9,  ///< Cloud layer drawn without Effects11 cloud scattering
		IsAurora = 1 << 10,
		IsMoon = 1 << 11,
	};

	/** @brief Bitflags describing extra feature-specific properties related to terrain displacement and material models. */
	enum class ExtraFeatureDescriptors : uint32_t
	{
		THLand0HasDisplacement = 1 << 0,
		THLand1HasDisplacement = 1 << 1,
		THLand2HasDisplacement = 1 << 2,
		THLand3HasDisplacement = 1 << 3,
		THLand4HasDisplacement = 1 << 4,
		THLand5HasDisplacement = 1 << 5,
		ETMaterialModel = 0b111 << 6,
		THLandHasDisplacement = 1 << 9,
		TVMeshVariation = 1 << 10
	};

	bool inWorld = false;
	bool activeReflections = false;

	// Cached menu open states, updated once per frame in Reset().
	// Avoids repeated IsMenuOpen calls (each constructs a BSFixedString).
	bool isMainMenuOpen = false;
	bool isLoadingMenuOpen = false;
	bool isMapMenuOpen = false;
	bool isStatsMenuOpen = false;
	bool flatWorldMapLoaded = false;  ///< FlatMapMarkersSSE is loaded, which flat world map mods (e.g. FWMF) rely on
	/** @brief The map menu is open while a flat world map mod is installed. */
	bool IsFlatWorldMapOpen() const { return isMapMenuOpen && flatWorldMapLoaded; }
	/**
	 * @brief Checks whether the main menu or loading menu is cached as open.
	 * @returns true if either the main menu or loading menu is open, false otherwise.
	 */
	bool IsMainOrLoadingMenuOpen() const { return isMainMenuOpen || isLoadingMenuOpen; }
	/**
	 * @brief Checks whether the main menu or loading menu is open, querying the UI if provided.
	 * @param ui Pointer to the UI manager; if non-null, performs live menu checks as a fallback.
	 * @returns true if the main menu or loading menu is open, false otherwise.
	 */
	bool IsMainOrLoadingMenuOpen(RE::UI* ui) const
	{
		return IsMainOrLoadingMenuOpen() ||
		       (ui && (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)));
	}
	/** @brief Full-screen menus drawing their own art, which must not be graded by post-process effects. */
	bool IsFullScreenMenuOpen() const { return IsMainOrLoadingMenuOpen() || isMapMenuOpen || isStatsMenuOpen; }
	/** @brief The main menu, a loading screen or the flat world map is open. */
	bool IsMainLoadingOrFlatMapOpen() const { return IsMainOrLoadingMenuOpen() || IsFlatWorldMapOpen(); }
	/** @brief Gameplay is paused or suspended behind a menu. Cached menus are kept explicit in case a mod clears kPausesGame. */
	bool IsPausedOrMenuOpen(RE::UI* ui) const
	{
		return (ui && ui->GameIsPaused()) || IsMainOrLoadingMenuOpen(ui) || isMapMenuOpen;
	}

	/**
	 * @brief Updates the shared constant buffer data based on world state and rendering pass.
	 * @param a_inWorld Whether the camera is in world space.
	 * @param a_prepass Whether this is a prepass rendering phase.
	 */
	void UpdateSharedData(bool a_inWorld, bool a_prepass);
	/**
	 * @brief Updates sky shader permutation based on the current render pass.
	 * @param a_pass The render pass to inspect.
	 */
	void UpdateSkyShaderPermutation(RE::BSRenderPass* a_pass);
	/**
	 * @brief Flags effect shader passes that draw the aurora (sky objects under the sky's aurora root).
	 * @param a_pass The render pass to inspect.
	 */
	void UpdateEffectShaderPermutation(RE::BSRenderPass* a_pass);
	/**
	 * @brief Checks whether directional shadows are available for the current scene.
	 * @returns true if directional shadows are present, false otherwise.
	 */
	bool HasDirectionalShadows() const;

	struct PermutationCB
	{
		uint VertexShaderDescriptor;
		uint PixelShaderDescriptor;
		uint ExtraShaderDescriptor;
		uint ExtraFeatureDescriptor;

		float EffectRadius;
		uint BaseTextureIsWorking;
		uint RenderToUI;
		uint pad;

		bool operator==(const PermutationCB& other) const
		{
			return VertexShaderDescriptor == other.VertexShaderDescriptor &&
			       PixelShaderDescriptor == other.PixelShaderDescriptor &&
			       ExtraShaderDescriptor == other.ExtraShaderDescriptor &&
			       ExtraFeatureDescriptor == other.ExtraFeatureDescriptor &&
			       EffectRadius == other.EffectRadius && BaseTextureIsWorking == other.BaseTextureIsWorking && RenderToUI == other.RenderToUI;
		}
	};
	STATIC_ASSERT_ALIGNAS_16(PermutationCB);

	std::unique_ptr<ConstantBuffer> permutationCB;

	using SharedDataCB = ::SharedDataCB;

	std::unique_ptr<ConstantBuffer> sharedDataCB;
	std::unique_ptr<ConstantBuffer> featureDataCB;

	PermutationCB permutationData{};
	PermutationCB permutationDataPrevious{};

	Util::FrameChecker frameChecker;
	void RequestHistoryReset() { historyResetFrame = frameCount + 1u; }
	bool ShouldResetHistory() const { return frameCount == historyResetFrame; }

	uint frameCount = 0;
	// Thread-safe mirror of frameCount maintained by the render thread.
	// Off-thread readers (MCP listener, future telemetry) must read this
	// instead of touching frameCount directly to avoid a data race.
	std::atomic<uint32_t> frameCountAtomic{ 0 };

	// Skyrim constants
	D3D_FEATURE_LEVEL featureLevel;

	TracyD3D11Ctx tracyCtx = nullptr;  // Tracy context

	void ClearDisabledFeatures();
	bool SetFeatureDisabled(const std::string& featureName, bool isDisabled);
	bool IsFeatureDisabled(const std::string& featureName);
	std::unordered_map<std::string, bool>& GetDisabledFeatures();
	void SetFeatureFavorite(const std::string& featureName, bool isFavorite);
	bool IsFeatureFavorite(const std::string& featureName);

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
	std::unordered_map<std::string, bool> favoriteFeatures;
	std::mutex m_mutex;

	inline ~State()
	{
#ifdef TRACY_ENABLE
		if (tracyCtx)
			TracyD3D11Destroy(tracyCtx);
#endif
	}

private:
	/// The code's defaults, captured on the first load; the bottom settings layer.
	nlohmann::json defaultSettings;
	/// Per feature (keyed by GetName()): defaults with the enabled Baseline packs applied. The user's
	/// edits are what a feature's settings differ from this by.
	std::unordered_map<std::string, nlohmann::json> featureBases;
	/// Per feature: the cached HasEditsUnderBaselines answer.
	std::unordered_map<std::string, bool> editsUnderBaselines;
	/// Per feature: the edits SettingsUser.json holds, kept for features that are not loaded so a save
	/// carries them over untouched.
	std::unordered_map<std::string, nlohmann::json> userFeatureEdits;

	/** @brief Defaults with the enabled Baseline packs applied, for one feature. */
	nlohmann::json BuildFeatureBase(Feature* feature);
	/** @brief The part of a feature's settings that differs from its base: the user's edits. */
	nlohmann::json DiffFromFeatureBase(const std::string& featureName, const nlohmann::json& current) const;
	/** @brief Replaces each feature's full settings in a config with only the user's edits to it. */
	void KeepOnlyUserEdits(nlohmann::json& settings);

	/** @brief Writes the sections that are not feature settings; the caller holds m_mutex. */
	void SaveCoreToJson(nlohmann::json& settings);
	/** @brief Hash of what SaveCoreToJson writes, to tell whether those sections changed. */
	size_t HashCoreSettings();
	/** @brief Clears the unsaved state after the user config was written or read. */
	void MarkSettingsSaved();
	/** @brief Drops a feature's edits to the settings packData sets, so those show the pack's values.
	 *  The caller holds a SceneLayerGuard. */
	void DropEditsUnder(Feature* feature, const nlohmann::json& packData);

	bool settingsChanged = false;
	bool coreSettingsChanged = false;
	size_t savedCoreHash = 0;

	std::optional<TonemapOwner> tonemapOwner;
	uint historyResetFrame = UINT_MAX;
	std::shared_ptr<REX::W32::ID3DUserDefinedAnnotation> pPerf;
	std::mutex statsMutex;
};

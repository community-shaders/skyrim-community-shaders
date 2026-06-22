#pragma once

#include "Buffer.h"

/** @brief Event handler that resets cubemap capture state when the loading menu closes. */
class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	/**
	 * @brief Handles menu open/close events; resets cubemap capture on loading screen exit.
	 * @param a_event The menu open/close event data.
	 * @param a_eventSource The event source dispatcher.
	 * @return Event processing control (always kContinue).
	 */
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource);
	/** @brief Registers the singleton event handler with the UI event source. */
	static bool Register();
};

struct DynamicCubemaps : Feature
{
public:
	const std::string defaultDynamicCubeMapSavePath = "Data\\textures\\DynamicCubemaps";

	// Specular irradiance

	ID3D11SamplerState* computeSampler = nullptr;

	struct alignas(16) SpecularMapFilterSettingsCB
	{
		float roughness;
		float pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(SpecularMapFilterSettingsCB);

	ID3D11ComputeShader* specularIrradianceCS = nullptr;
	ConstantBuffer* spmapCB = nullptr;
	Texture2D* envTexture = nullptr;
	Texture2D* envReflectionsTexture = nullptr;
	ID3D11UnorderedAccessView* uavArray[7];
	ID3D11UnorderedAccessView* uavReflectionsArray[7];

	// Reflection capture

	struct alignas(16) UpdateCubemapCB
	{
		float3 CameraPreviousPosAdjust;
		uint pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(UpdateCubemapCB);

	ID3D11ComputeShader* updateCubemapCS = nullptr;
	ID3D11ComputeShader* updateCubemapReflectionsCS = nullptr;
	ID3D11ComputeShader* updateCubemapFakeReflectionsCS = nullptr;

	ConstantBuffer* updateCubemapCB = nullptr;

	ID3D11ComputeShader* inferCubemapCS = nullptr;
	ID3D11ComputeShader* inferCubemapReflectionsCS = nullptr;
	ID3D11ComputeShader* inferCubemapFakeReflectionsCS = nullptr;

	Texture2D* envCaptureTexture = nullptr;
	Texture2D* envCaptureRawTexture = nullptr;
	Texture2D* envCapturePositionTexture = nullptr;

	Texture2D* envCaptureReflectionsTexture = nullptr;
	Texture2D* envCaptureRawReflectionsTexture = nullptr;
	Texture2D* envCapturePositionReflectionsTexture = nullptr;

	Texture2D* envInferredTexture = nullptr;

	ID3D11ShaderResourceView* defaultCubemap = nullptr;

	bool activeReflections = false;
	bool fakeReflections = false;

	bool resetCapture[2] = { true, true };
	bool recompileFlag = false;
	float previousHoursPassed = 0.0f;

	enum class NextTask
	{
		kCapture,
		kInferrence,
		kIrradiance,
		kBC6HCompress,
		kCapture2,
		kInferrence2,
		kIrradiance2,
		kBC6HCompress2
	};

	NextTask nextTask = NextTask::kCapture;

	// BC6H compression
	struct alignas(16) BC6HEncodeCB
	{
		uint TextureSizeInBlocksX;
		uint TextureSizeInBlocksY;
		uint MipLevel;
		uint pad;
	};
	STATIC_ASSERT_ALIGNAS_16(BC6HEncodeCB);

	ID3D11ComputeShader* bc6hEncodeCS = nullptr;
	ConstantBuffer* bc6hEncodeCB = nullptr;

	ID3D11ShaderResourceView* envTextureArraySRV = nullptr;
	ID3D11ShaderResourceView* envReflectionsTextureArraySRV = nullptr;

	Texture2D* envTextureBC6H = nullptr;
	Texture2D* envReflectionsTextureBC6H = nullptr;
	Texture2D* bc6hScratchTexture = nullptr;

	uint32_t bc6hMipLevels = 0;

	ID3D11UnorderedAccessView* bc6hScratchUAVs[8] = {};

	// Editor window

	struct Settings
	{
		uint EnabledCreator = false;
		uint EnabledSSR = true;
		uint pad0[2];
		float4 CubemapColor{ 1.0f, 1.0f, 1.0f, 0.0f };
	};

	Settings settings;
	/**
	 * @brief Advances the cubemap pipeline by one task each frame.
	 *
	 * Cycles through capture, inference, irradiance, and BC6H compression
	 * stages for both primary and reflection cubemaps.
	 */
	void UpdateCubemap();

	/** @brief Binds the BC6H-compressed cubemap textures as pixel shader resources after deferred rendering. */
	void PostDeferred();

	virtual inline std::string GetName() override { return "Dynamic Cubemaps"; }
	virtual std::string GetDisplayName() override { return T("feature.dynamic_cubemaps.name", "Dynamic Cubemaps"); }
	virtual inline std::string GetShortName() override { return "DynamicCubemaps"; }
	virtual inline std::string_view GetShaderDefineName() override { return "DYNAMIC_CUBEMAPS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.dynamic_cubemaps.description", "Provides real-time environment mapping and reflections by generating dynamic cube maps that capture the surrounding environment, enabling realistic reflections on surfaces."),
			{ T("feature.dynamic_cubemaps.key_feature_1", "Real-time environment capture for realistic reflections"),
				T("feature.dynamic_cubemaps.key_feature_2", "Dynamic cube map generation based on camera position"),
				T("feature.dynamic_cubemaps.key_feature_3", "Enhanced water reflections with environmental details"),
				T("feature.dynamic_cubemaps.key_feature_4", "Optimized cubemap inference and irradiance calculation") } };
	};

	/** @brief Returns additional shader define options based on current settings (e.g., ENABLESSR). */
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	/** @brief Creates all GPU resources: textures, UAVs, constant buffers, samplers, and BC6H scratch buffers. */
	virtual void SetupResources() override;
	/** @brief Resets reflection state each frame based on sky visibility and interior status. */
	virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	/** @brief Loads dynamic cubemap settings from JSON and flags shaders for recompilation. */
	virtual void LoadSettings(json&) override;
	/** @brief Resets all settings to their default values and flags shaders for recompilation. */
	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the ImGui settings UI for screen-space reflections and cubemap creator. */
	virtual void DrawSettings() override;
	/** @brief Registers the menu open/close event handler after game data is loaded. */
	virtual void DataLoaded() override;
	/** @brief Called after all plugins have loaded. */
	virtual void PostPostLoad() override;

	/** @brief Releases all cached compute shaders so they can be recompiled on next use. */
	virtual void ClearShaderCache() override;
	/** @brief Returns the cubemap update compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderUpdate();
	/** @brief Returns the reflections cubemap update compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderUpdateReflections();
	/** @brief Returns the fake reflections cubemap update compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderUpdateFakeReflections();

	/** @brief Returns the cubemap inference compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderInferrence();
	/** @brief Returns the reflections cubemap inference compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderInferrenceReflections();
	/** @brief Returns the fake reflections cubemap inference compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderInferrenceFakeReflections();

	/** @brief Returns the specular irradiance compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderSpecularIrradiance();

	/**
	 * @brief Captures the current scene into the environment cubemap using depth and color data.
	 * @param a_reflections If true, captures into the reflections cubemap; otherwise the primary cubemap.
	 */
	void UpdateCubemapCapture(bool a_reflections);

	/**
	 * @brief Infers a complete cubemap from captured data using the default cubemap as a fallback.
	 * @param a_reflections If true, processes the reflections cubemap; otherwise the primary cubemap.
	 */
	void Inferrence(bool a_reflections);

	/**
	 * @brief Computes pre-filtered specular irradiance mip levels for the cubemap.
	 * @param a_reflections If true, processes the reflections cubemap; otherwise the primary cubemap.
	 */
	void Irradiance(bool a_reflections);

	/**
	 * @brief Compresses the irradiance cubemap to BC6H format via a compute shader encoder.
	 * @param a_reflections If true, compresses the reflections cubemap; otherwise the primary cubemap.
	 */
	void CompressToBC6H(bool a_reflections);

	/** @brief Returns the BC6H block encoder compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetComputeShaderBC6HEncode();

	virtual bool IsCore() const override { return true; };
};

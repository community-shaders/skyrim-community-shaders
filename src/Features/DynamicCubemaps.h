#pragma once

#include "Buffer.h"

/**
 * Handles menu open and close events.
 */

/**
 * Processes a menu open/close event notification.
 * @param a_event The menu open/close event.
 * @param a_eventSource The event source.
 * @return The event notification control value.
 */

/**
 * Registers this handler to receive menu events.
 * @return `true` if registration succeeds, `false` otherwise.
 */
class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource);
	static bool Register();
};

/**
	 * Feature that generates dynamic cube maps for environment mapping and reflections.
	 * 
	 * Manages GPU resources and compute passes to capture and process environmental
	 * data into cube maps for real-time reflections and specular irradiance calculations.
	 */
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
		// IrradianceB (levels 2-7) costs ~45us total from 6 small dispatches.
		// Per-dispatch overhead is ~7.4us each, with the compute work shrinking rapidly.
		// Splitting off the last level (level 7, ~7us) and combining it with BC6H(~32us)
		// gives three near-equal frames per chain:
		//   kCaptureInferAndIrradianceA:  C+I+IA          ~39us
		//   kIrradianceBA:                levels 2-6       ~37us
		//   kIrradianceBBAndBC6H:         level 7 + BC6H   ~7+32 = 39us
		kCaptureInferAndIrradianceA,   // Capture + Inferrence + IrradianceA (chain 1): ~39us
		kIrradianceBA,                 // mip levels 2-6 (5 dispatches, chain 1): ~37us
		kIrradianceBBAndBC6H,          // mip level 7 + BC6H compress (chain 1): ~39us
		kCaptureInferAndIrradianceA2,  // chain 2 equivalents (reflections only):
		kIrradianceBA2,
		kIrradianceBBAndBC6H2,
	};

	NextTask nextTask = NextTask::kCaptureInferAndIrradianceA;

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
	void UpdateCubemap();

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

	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;

	/**
 * Indicates whether the feature applies shader defines to the given shader type.
 * @returns Always `true`.
 */
bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	/**
	 * Initialize Direct3D resources required for dynamic cubemap generation.
	 */
	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;

	virtual void ClearShaderCache() override;
	ID3D11ComputeShader* GetComputeShaderUpdate();
	ID3D11ComputeShader* GetComputeShaderUpdateReflections();
	ID3D11ComputeShader* GetComputeShaderUpdateFakeReflections();

	ID3D11ComputeShader* GetComputeShaderInferrence();
	ID3D11ComputeShader* GetComputeShaderInferrenceReflections();
	ID3D11ComputeShader* GetComputeShaderInferrenceFakeReflections();

	ID3D11ComputeShader* GetComputeShaderSpecularIrradiance();

	void UpdateCubemapCapture(bool a_reflections);

	void Inferrence(bool a_reflections);

	void Irradiance(bool a_reflections, uint32_t a_startLevel, uint32_t a_endLevel, bool a_doSetup);

	void CompressToBC6H(bool a_reflections);

	ID3D11ComputeShader* GetComputeShaderBC6HEncode();

	virtual bool IsCore() const override { return true; };
};

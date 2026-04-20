#pragma once

#include "Buffer.h"

class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource);
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
	bool enabledAtBoot = false;
	void UpdateCubemap();

	void PostDeferred();

	virtual inline std::string GetName() override { return "Dynamic Cubemaps"; }
	virtual inline std::string GetShortName() override { return "DynamicCubemaps"; }
	virtual inline std::string_view GetShaderDefineName() override { return "DYNAMIC_CUBEMAPS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides real-time environment mapping and reflections by generating dynamic cube maps that capture the surrounding environment, enabling realistic reflections on surfaces.",
			{ "Real-time environment capture for realistic reflections",
				"Dynamic cube map generation based on camera position",
				"Enhanced water reflections with environmental details",
				"Support for both standard and VR rendering modes",
				"Optimized cubemap inference and irradiance calculation" }
		};
	}
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;

	std::map<std::string, Util::GameSetting> iniVRCubeMapSettings{
		{ "bAutoWaterSilhouetteReflections:Water", { "Auto Water Silhouette Reflections", "Automatically reflects silhouettes on water surfaces.", 0, true, false, true } },
		{ "bForceHighDetailReflections:Water", { "Force High Detail Reflections", "Forces the use of high-detail reflections on water surfaces.", 0, true, false, true } }
	};

	std::map<std::string, Util::GameSetting> hiddenVRCubeMapSettings{
		{ "bReflectExplosions:Water", { "Reflect Explosions", "Enables reflection of explosions on water surfaces.", 0x1eaa000, true, false, true } },
		{ "bReflectLODLand:Water", { "Reflect LOD Land", "Enables reflection of low-detail (LOD) terrain on water surfaces.", 0x1eaa060, true, false, true } },
		{ "bReflectLODObjects:Water", { "Reflect LOD Objects", "Enables reflection of low-detail (LOD) objects on water surfaces.", 0x1eaa078, true, false, true } },
		{ "bReflectLODTrees:Water", { "Reflect LOD Trees", "Enables reflection of low-detail (LOD) trees on water surfaces.", 0x1eaa090, true, false, true } },
		{ "bReflectSky:Water", { "Reflect Sky", "Enables reflection of the sky on water surfaces.", 0x1eaa0a8, true, false, true } },
		{ "bUseWaterRefractions:Water", { "Use Water Refractions", "Enables refractions for water surfaces, affecting how light bends through water.", 0x1eaa0c0, true, false, true } }
	};

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

	void Irradiance(bool a_reflections);

	void CompressToBC6H(bool a_reflections);

	ID3D11ComputeShader* GetComputeShaderBC6HEncode();

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};

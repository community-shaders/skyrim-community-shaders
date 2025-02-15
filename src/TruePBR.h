#pragma once

struct GlintParameters
{
	bool enabled = false;
	float screenSpaceScale = 1.5f;
	float logMicrofacetDensity = 40.f;
	float microfacetRoughness = .015f;
	float densityRandomization = 2.f;
};

struct TruePBR
{
public:
	static TruePBR* GetSingleton()
	{
		static TruePBR singleton;
		return &singleton;
	}

	inline std::string GetShortName() { return "TruePBR"; }

	void DrawSettings();
	void SetupResources();
	void PrePass();
	void PostPostLoad();
	void DataLoaded();

	void SetShaderResouces(ID3D11DeviceContext* a_context);
	void GenerateShaderPermutations(RE::BSShader* shader);

	void SetupGlintsTexture();
	eastl::unique_ptr<Texture2D> glintsNoiseTexture = nullptr;

	std::unordered_map<uint32_t, std::string> editorIDs;

	struct PBRTextureSetData
	{
		float roughnessScale = 1.f;
		float displacementScale = 1.f;
		float specularLevel = 0.04f;

		RE::NiColor subsurfaceColor;
		float subsurfaceOpacity = 0.f;

		RE::NiColor coatColor = { 1.f, 1.f, 1.f };
		float coatStrength = 1.f;
		float coatRoughness = 1.f;
		float coatSpecularLevel = 0.04f;
		float innerLayerDisplacementOffset = 0.f;

		RE::NiColor fuzzColor;
		float fuzzWeight = 0.f;

		GlintParameters glintParameters;
	};

	void SetupFrame();

	void SetupTextureSetData();
	void ReloadTextureSetData();
	PBRTextureSetData* GetPBRTextureSetData(const RE::TESForm* textureSet);
	bool IsPBRTextureSet(const RE::TESForm* textureSet);

	void SetupDefaultPBRLandTextureSet();

	std::unordered_map<std::string, PBRTextureSetData> pbrTextureSets;
	RE::BGSTextureSet* defaultPbrLandTextureSet = nullptr;
	bool defaultLandTextureSetReplaced = false;
	std::string selectedPbrTextureSetName;
	PBRTextureSetData* selectedPbrTextureSet = nullptr;

	struct PBRMaterialObjectData
	{
		std::array<float, 3> baseColorScale = { 1.f, 1.f, 1.f };
		float roughness = 1.f;
		float specularLevel = 1.f;

		GlintParameters glintParameters;
	};

	void SetupMaterialObjectData();
	PBRMaterialObjectData* GetPBRMaterialObjectData(const RE::TESForm* materialObject);
	bool IsPBRMaterialObject(const RE::TESForm* materialObject);

	std::unordered_map<std::string, PBRMaterialObjectData> pbrMaterialObjects;
	std::string selectedPbrMaterialObjectName;
	PBRMaterialObjectData* selectedPbrMaterialObject = nullptr;

	RE::BGSTextureSet* currentTextureSet = nullptr;
};

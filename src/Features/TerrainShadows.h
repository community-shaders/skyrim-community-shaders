#pragma once

#include <filesystem>

struct TerrainShadows : public Feature
{
	static TerrainShadows* GetSingleton()
	{
		static TerrainShadows singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() override { return "Terrain Shadows"; }
	virtual inline std::string GetShortName() override { return "TerrainShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_SHADOWS"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool EnableTerrainShadow = true;
	} settings;

	bool needPrecompute = false;
	uint shadowUpdateIdx = 0;

	struct HeightMapMetadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
		float2 zRange;
	};
	std::unordered_map<std::string, HeightMapMetadata> heightmaps;
	HeightMapMetadata* cachedHeightmap;

	struct ShadowUpdateCB
	{
		float2 LightPxDir;   // direction on which light descends, from one pixel to next via dda
		float2 LightDeltaZ;  // per LightUVDir, upper penumbra and lower, should be negative
		uint StartPxCoord;
		float2 PxSize;
		uint pad0[1];
		float2 PosRange;
		float2 ZRange;
	} shadowUpdateCBData;
	static_assert(sizeof(ShadowUpdateCB) % 16 == 0);
	std::unique_ptr<ConstantBuffer> shadowUpdateCB = nullptr;

	struct alignas(16) PerFrame
	{
		uint EnableTerrainShadow;
		float3 Scale;
		float2 ZRange;
		float2 Offset;
	};

	PerFrame GetCommonBufferData();

	winrt::com_ptr<ID3D11ComputeShader> shadowUpdateProgram = nullptr;

	std::unique_ptr<Texture2D> texHeightMap = nullptr;
	std::unique_ptr<Texture2D> texShadowHeight = nullptr;

	bool IsHeightMapReady();

	virtual void SetupResources() override;
	void ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style);
	void CompileComputeShaders();

	virtual void DrawSettings() override;

	virtual void EarlyPrepass() override;
	void LoadHeightmap();
	void Precompute();
	void UpdateShadow();

	virtual void ReflectionsPrepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; }
	virtual void ClearShaderCache() override;
	virtual bool SupportsVR() override { return true; };
};
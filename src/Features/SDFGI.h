#pragma once

#include "Buffer.h"

struct SDFGI : Feature
{
public:
	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "SDFGI"; }
	virtual inline std::string GetShortName() override { return "SDFGI"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			"Signed Distance Field Global Illumination provides world-space "
			"volumetric indirect lighting using multi-cascade SDF volumes and "
			"light probes with spherical harmonics.",
			std::vector<std::string>{
				"View-independent global illumination",
				"Multi-cascade SDF for large-scale coverage",
				"Light probe integration with temporal stability",
				"Configurable quality and cascade settings" });
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();
	bool ShadersOK();

	void DrawSDFGI();

	ID3D11ShaderResourceView* GetOutputSRV();

	//////////////////////////////////////////////////////////////////////////////////

	static constexpr uint CASCADE_SIZE = 128;
	static constexpr uint PROBE_DIVISOR = 16;
	static constexpr uint PROBE_AXIS_SIZE = CASCADE_SIZE / PROBE_DIVISOR + 1;
	static constexpr uint MAX_CASCADES = 8;
	static constexpr uint LIGHTPROBE_OCT_SIZE = 6;
	static constexpr uint SH_SIZE = 16;
	static constexpr uint MAX_LIGHTS = 128;

	bool recompileFlag = false;
	uint frameCount = 0;
	uint currentLightCount = 0;

	struct Settings
	{
		bool Enabled = true;
		uint NumCascades = 4;
		float MinCellSize = 4.0f;
		float Energy = 1.0f;
		float NormalBias = 1.1f;
		float BounceFeedback = 0.5f;
		uint RayCount = 16;
		uint FramesToConverge = 16;
		uint FramesToUpdateLight = 4;
		bool UseOcclusion = true;
		float YScale = 1.5f;
		bool ShowDebug = false;
	} settings;

	struct alignas(16) SDFGIParams
	{
		float GridSize[3];
		uint MaxCascades;

		uint Cascade;
		uint LightCount;
		uint ProcessOffset;
		uint ProcessIncrement;

		int ProbeAxisSize;
		float BounceFeedback;
		float YMult;
		uint UseOcclusion;

		int Scroll[3];
		int StepSize;

		int ProbeOffset[3];
		uint HalfSize;

		uint OcclusionIndex;
		uint HistoryIndex;
		uint HistorySize;
		uint RayCount;

		float RayBias;
		int ImageSize[2];
		uint SkyFlags;

		int WorldOffset[3];
		float SkyEnergy;

		float SkyColor[3];
		uint StoreAmbientTexture;
	};
	STATIC_ASSERT_ALIGNAS_16(SDFGIParams);

	struct alignas(16) CascadeData
	{
		float Offset[3];
		float ToCell;
		int ProbeWorldOffset[3];
		uint Pad;
		float Pad2[4];
	};
	STATIC_ASSERT_ALIGNAS_16(CascadeData);

	struct alignas(16) SDFGISampleCB
	{
		float GridSize[3];
		uint MaxCascades;

		uint UseOcclusion;
		int ProbeAxisSize;
		float ProbeToUVW;
		float NormalBias;

		float LightprobeTexPixelSize[3];
		float Energy;

		float LightprobeUVOffset[3];
		float YMult;

		CascadeData Cascades[MAX_CASCADES];
	};
	STATIC_ASSERT_ALIGNAS_16(SDFGISampleCB);

	struct SDFGILight
	{
		float color[3];
		float energy;
		float direction[3];
		uint hasShadow;
		float position[3];
		float attenuation;
		uint type;
		float cosSpotAngle;
		float invSpotAttenuation;
		float radius;
	};

	struct alignas(16) VoxelizeCBData
	{
		DirectX::XMFLOAT4X4 ViewProj;
		DirectX::XMFLOAT4X4 World;
		float CascadeOffset[3];
		float CascadeToCell;
		float GridSizeF[3];
		uint pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(VoxelizeCBData);

	struct ProcessVoxel
	{
		uint position;
		uint albedo;
		uint light;
		uint lightAniso;
	};

	struct Cascade
	{
		float cellSize = 0;
		int position[3] = {};
		int dirtyRegions[3] = {};
		bool allDirty = true;

		eastl::unique_ptr<Texture3D> sdfTex;
		eastl::unique_ptr<Texture3D> lightTex;
		eastl::unique_ptr<Texture3D> lightAniso0Tex;
		eastl::unique_ptr<Texture3D> lightAniso1Tex;

		eastl::unique_ptr<Buffer> solidCellBuffer;
		eastl::unique_ptr<Buffer> dispatchBuffer;

		winrt::com_ptr<ID3D11ShaderResourceView> solidCellSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> solidCellUAV;
		winrt::com_ptr<ID3D11ShaderResourceView> dispatchSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> dispatchUAV;

		eastl::unique_ptr<Texture2D> probeHistoryTex;
		eastl::unique_ptr<Texture2D> probeAverageTex;
	};

	std::vector<Cascade> cascades;

	// Shared voxelization textures
	eastl::unique_ptr<Texture3D> renderAlbedo;
	eastl::unique_ptr<Texture3D> renderEmission;
	eastl::unique_ptr<Texture3D> renderGeomFacing;
	eastl::unique_ptr<Texture3D> renderSdf[2];
	eastl::unique_ptr<Texture3D> renderSdfHalf[2];

	// Shared probe / occlusion data
	eastl::unique_ptr<Texture2D> lightprobeData;
	eastl::unique_ptr<Texture3D> occlusionTex;

	// Light buffer for direct light pass
	eastl::unique_ptr<StructuredBuffer> lightBuffer;

	// Screen-space output
	eastl::unique_ptr<Texture2D> giOutput;

	// Constant buffers
	eastl::unique_ptr<ConstantBuffer> paramsCB;
	eastl::unique_ptr<ConstantBuffer> cascadesCB;
	eastl::unique_ptr<ConstantBuffer> sampleCB;

	// Preprocess compute shaders (one per mode)
	enum PreprocessMode
	{
		PP_JFA_INIT_HALF = 0,
		PP_JFA_PASS,
		PP_JFA_OPTIMIZED,
		PP_JFA_UPSCALE,
		PP_OCCLUSION,
		PP_STORE,
		PP_SCROLL,
		PP_SCROLL_OCCLUSION,
		PP_COUNT
	};
	winrt::com_ptr<ID3D11ComputeShader> preprocessCS[PP_COUNT];

	winrt::com_ptr<ID3D11ComputeShader> directLightStaticCS;
	winrt::com_ptr<ID3D11ComputeShader> directLightDynamicCS;

	winrt::com_ptr<ID3D11ComputeShader> integrateProcessCS;
	winrt::com_ptr<ID3D11ComputeShader> integrateStoreCS;
	winrt::com_ptr<ID3D11ComputeShader> integrateScrollCS;
	winrt::com_ptr<ID3D11ComputeShader> integrateScrollStoreCS;

	// Voxelization rasterization pipeline (3-axis orthographic)
	winrt::com_ptr<ID3D11VertexShader> voxelizeVS;
	winrt::com_ptr<ID3D11PixelShader> voxelizePS;
	winrt::com_ptr<ID3D11InputLayout> voxelizeInputLayout;
	winrt::com_ptr<ID3D11RasterizerState> voxelizeRasterState;
	winrt::com_ptr<ID3D11DepthStencilState> voxelizeDepthState;
	winrt::com_ptr<ID3D11BlendState> voxelizeBlendState;
	winrt::com_ptr<ID3D11Buffer> voxelizeVB;
	winrt::com_ptr<ID3D11Buffer> voxelizeIB;
	uint voxelizeIndexCount = 0;
	eastl::unique_ptr<ConstantBuffer> voxelizeCB;
	winrt::com_ptr<ID3D11Texture2D> voxelizeDummyTex;
	winrt::com_ptr<ID3D11RenderTargetView> voxelizeDummyRTV;

	winrt::com_ptr<ID3D11ComputeShader> sampleCS;
	winrt::com_ptr<ID3D11ComputeShader> debugCS;

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler;

private:
	void CreateCascadeResources(uint cascadeIndex);
	void CreateSharedResources();
	void UpdateCascades();
	void UpdateParams(uint cascade, int stepSize = 0);
	void DispatchPreprocess(uint cascade);
	void DispatchDirectLight(uint cascade);
	void DispatchIntegrate(uint cascade);
	void DispatchSample();

	void CompileVoxelizeShaders();
	void CreateVoxelizeResources();
	void StubVoxelizeRegion(uint cascade);
	void StubGatherLights(std::vector<SDFGILight>& lights);

	float3 lastCameraPos = { 0, 0, 0 };
};

#pragma once

#include "Buffer.h"

struct SSRT : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "SSRT"; }
	virtual inline std::string GetShortName() override { return "SSRT"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			std::string(
				"Screen-space probe GI based on AMD Capsaicin GI-1.1. "
				"Uses screen-space probes with SH9 irradiance, Hi-Z tracing, "
				"temporal reprojection, and spatio-temporal denoising."),
			std::vector<std::string>{
				"Screen-space probe indirect lighting",
				"SH9-based irradiance evaluation",
				"Hi-Z screen-space ray tracing",
				"Temporal probe caching and denoising" });
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();
	bool ShadersOK();

	void DrawSSRT();
	void UpdateCB();

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;
	uint pingPong = 0;

	// Probe grid dimensions (computed in SetupResources from screen size)
	uint probeSize = 8;
	uint probeCountX = 0;
	uint probeCountY = 0;
	uint maxProbeCount = 0;
	uint spawnDimsX = 0;
	uint spawnDimsY = 0;
	uint maxSpawnCount = 0;
	uint maxRayCount = 0;
	uint probeMaskMipCount = 0;

	struct Settings
	{
		bool Enabled = true;
		bool EnableVanillaSSAO = false;
		// Quality
		float GIIntensity = 1.f;
		float AOIntensity = 1.f;
		uint ProbeSpawnTileSize = 16;
		uint MaxHiZSteps = 64;
		float HiZThickness = 0.05f;
		float HiZMaxDistance = 500.f;
	} settings;

	struct alignas(16) SSRTCB
	{
		// Reprojection: PrevViewProjUnjittered * ViewProjInverse
		DirectX::XMFLOAT4X4 Reprojection;
		// Inverse of previous frame's ViewProj (for world reconstruction from prev depth)
		DirectX::XMFLOAT4X4 PrevViewProjInverse;

		float3 Eye;
		uint FrameIndex;

		float2 BufferDimensions;
		float2 RcpBufferDimensions;

		float2 NearFar;
		float CellSize;
		uint ProbeSize;

		uint ProbeCountX;
		uint ProbeCountY;
		uint ProbeMaskMipCount;
		uint ProbeSpawnTileSize;

		int BlurDirectionX;
		int BlurDirectionY;
		uint MaxHiZSteps;
		float HiZThickness;

		float HiZMaxDistance;
		float GIIntensity;
		float AOIntensity;
		uint MaxSpawnCount;

		uint MaxRayCount;
		uint DepthPyramidMipCount;
		float pad1;
		float pad2;
	};
	STATIC_ASSERT_ALIGNAS_16(SSRTCB);
	eastl::unique_ptr<ConstantBuffer> ssrtCB;
	SSRTCB cachedCB{};  // cached for per-pass blur direction updates

	bool debugProbes = false;

	//////////////////////////////////////////////////////////////////////////////////
	// Textures
	//////////////////////////////////////////////////////////////////////////////////

	// Final output (RGBA16F: .rgb = indirect lighting, .a = AO)
	eastl::unique_ptr<Texture2D> texGIOcclusion = nullptr;

	// Probe radiance buffer (R16G16B16A16_FLOAT, probeBufferWidth x probeBufferHeight)
	eastl::unique_ptr<Texture2D> texProbeBuffer[2] = {};

	// Probe mask (R32_UINT, probeCount.x x probeCount.y, with mip chain)
	eastl::unique_ptr<Texture2D> texProbeMask[2] = {};
	// Per-mip UAVs for probe mask (mip 0 is texProbeMask[i]->uav)
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> probeMaskMipUAVs[2];

	// Depth pyramid for Hi-Z tracing (R32_FLOAT, screen dims, mip chain, MAX downsample)
	eastl::unique_ptr<Texture2D> texDepthPyramid = nullptr;
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> depthPyramidMipSRVs;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> depthPyramidMipUAVs;
	uint depthPyramidMipCount = 0;

	// Radiance pyramid (scene color with mip chain for hit-point sampling)
	eastl::unique_ptr<Texture2D> texRadiancePyramid = nullptr;

	// GI denoiser textures
	eastl::unique_ptr<Texture2D> texGIDenoiserColor[2] = {};
	eastl::unique_ptr<Texture2D> texGIDenoiserBlurMask = nullptr;
	eastl::unique_ptr<Texture2D> texGIDenoiserColorDelta[2] = {};

	// Previous frame depth + normals for temporal reprojection
	eastl::unique_ptr<Texture2D> texPrevDepth = nullptr;
	eastl::unique_ptr<Texture2D> texPrevNormals = nullptr;

	//////////////////////////////////////////////////////////////////////////////////
	// Structured Buffers
	//////////////////////////////////////////////////////////////////////////////////

	// Probe SH coefficients: uint2[9 * maxProbeCount], ping-pong
	eastl::unique_ptr<Buffer> bufProbeSH[2] = {};

	// Probe spawn: uint[maxSpawnCount], ping-pong
	eastl::unique_ptr<Buffer> bufProbeSpawn[2] = {};

	// Prefix sum output: uint[maxSpawnCount]
	eastl::unique_ptr<Buffer> bufProbeSpawnScan = nullptr;

	// Compacted probe index: uint[maxSpawnCount]
	eastl::unique_ptr<Buffer> bufProbeSpawnIndex = nullptr;

	// Per-ray sample direction: uint2[maxRayCount]
	eastl::unique_ptr<Buffer> bufProbeSpawnSample = nullptr;

	// Per-ray traced radiance: uint2[maxRayCount]
	eastl::unique_ptr<Buffer> bufProbeSpawnRadiance = nullptr;

	// Empty tile list and count
	eastl::unique_ptr<Buffer> bufEmptyTile = nullptr;
	eastl::unique_ptr<Buffer> bufEmptyTileCount = nullptr;

	// Override tile list and count
	eastl::unique_ptr<Buffer> bufOverrideTile = nullptr;
	eastl::unique_ptr<Buffer> bufOverrideTileCount = nullptr;

	// Indirect dispatch args (12 bytes: groupX, groupY, groupZ)
	eastl::unique_ptr<Buffer> bufDispatchIndirectArgs = nullptr;

	// Prefix sum block totals: uint[ceil(maxSpawnCount/128)]
	eastl::unique_ptr<Buffer> bufPrefixSumBlockTotals = nullptr;

	//////////////////////////////////////////////////////////////////////////////////
	// Output accessor
	//////////////////////////////////////////////////////////////////////////////////

	inline ID3D11ShaderResourceView* GetOutputTexture()
	{
		return (loaded && settings.Enabled) ?
		           texGIOcclusion->srv.get() :
		           (ID3D11ShaderResourceView*)nullptr;
	}

	//////////////////////////////////////////////////////////////////////////////////
	// Samplers
	//////////////////////////////////////////////////////////////////////////////////

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	//////////////////////////////////////////////////////////////////////////////////
	// Compute Shaders
	//////////////////////////////////////////////////////////////////////////////////

	// Depth pyramid
	winrt::com_ptr<ID3D11ComputeShader> pyramidCopyCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> buildDepthPyramidCS = nullptr;

	// Probe lifecycle
	winrt::com_ptr<ID3D11ComputeShader> clearCountersCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> clearProbeMaskCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> reprojectScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> filterProbeMaskCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spawnScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compactScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> patchScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> sampleScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> populateScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blendScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> filterScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> projectScreenProbesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> interpolateScreenProbesCS = nullptr;

	// Prefix sum
	winrt::com_ptr<ID3D11ComputeShader> prefixSumLocalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefixSumBlockCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefixSumFinalizeCS = nullptr;

	// Utility
	winrt::com_ptr<ID3D11ComputeShader> prepareDispatchIndirectCS = nullptr;

	// GI denoiser
	winrt::com_ptr<ID3D11ComputeShader> reprojectGICS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> filterGICS = nullptr;
};

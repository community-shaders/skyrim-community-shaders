#pragma once
#include "Feature.h"

struct OrderIndependentTransparency final : Feature
{
	virtual inline std::string GetName() override { return "Order Independent Transparency"; }
	virtual std::string GetDisplayName() override { return T("feature.order_independent_transparency.name", "Order Independent Transparency"); }
	virtual inline std::string GetShortName() override { return "OrderIndependentTransparency"; }
	virtual inline std::string_view GetShaderDefineName() override { return "OIT"; }
	virtual std::string_view GetCategory() const override { return "Materials"; }

	// Get the shader define for material shader compilation, needs to invalidate shader cache when setting changes
	std::span<const D3D_SHADER_MACRO> GetShaderDefines() const;
	bool UpdateShaderDefines();

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.order_independent_transparency.description",
				"Order Independent Transparency (OIT) correctly blends overlapping transparent surfaces, such as water, hair, glass, foliage, and smoke."),
			{ T("feature.order_independent_transparency.key_feature_1", "Corrects water reflections and refraction") }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	// Implementation method
	// The difficulty of implementing more method here is the shader descriptor flags / defines
	// We will take way too much flags for that
	enum Method : int
	{
		OIT_DISABLED, // Disable OIT
		OIT_VISUALIZE,// Visualize the number of layers
		OIT_AT,       // AT, Adpative transparency, A buffer with per-pixel linked list
		OIT_BLENDED,  // Weighted blended OIT
		OIT_RVO,      // MLAB, K-Buffer with rasterizer order view
	};

	struct Settings
	{
		static constexpr float InfDistanceThreshold = 200'000.f;
		Method	Method = OIT_BLENDED;  // Method to use for OIT
		uint	BufferSize = 8; // Multiplier of the back buffer size for holding Fragment List Node
		uint	MaxLayers = 8;  // Maximum layers in OIT resolution, layers exceeding this limit may introduce rendering artifacts
		float	AlphaThreshold = 0.f; // Alpha cutoff below which pixels are discarded
		float	DepthThreshold = 1.f; // Camera space depth threshold to prevent z-fighting on 32 bits float precision limit
		float	DistanceThreshold = InfDistanceThreshold; // Distance to camera before enabling OIT, to exclude large & complex distant volumetric fogs
		bool	CaptureMultiplicativeLayer = true; // Whether to support multiplicative blend mode
		bool	OverrideRenderTargets = false; // Force override render target in alpha pass, only use you having issue
		bool	WriteDepth = true; // Allow the OIT composition to write depth for closest mesh with 'Write Depth' flag
		float	WriteDepthThreshold = 0.f; // Don't write depth if the layer's alpha is below this threshold
		float	WBOITAdditiveAlphaScale = 0.01f;
		float	WBOITMinProjectedDistance = 0.2f;
		float	WBOITMinPreAlphaWeight = 0.01f;
		float	WBOITWeightMin = 0.1f;
		float	WBOITWeightMax = 1.f;
	};

	struct alignas(16) FeatureCB
	{
		uint	MaxListNodes = 0;
		uint	Flags = 0;
		float	AlphaThreshold = 0.f;
		float	DepthThreshold = 1.f;
		float	WBOITAdditiveAlphaScale = 0.01f;
		float	WBOITMinProjectedDistance = 0.2f;
		float	WBOITMinPreAlphaWeight = 0.01f;
		float	WBOITWeightMin = 0.1f;
		float	WBOITWeightMax = 1.f;
		float	Pad0 = 0.f;
		float	Pad1 = 0.f;
		float	Pad2 = 0.f;
	};
	static_assert(sizeof(FeatureCB) == 48);

	FeatureCB featureCB;

	Settings settings;

	D3D_SHADER_MACRO shaderDefines[2] = { { nullptr, nullptr }, { nullptr, nullptr } };
	char shaderDefineBuffer[4]; // For holding string of MaxLayers

	float passTime; // API Time spend in the transparency (material) pass
	float compositeTime; // API Time spend in the composite pass

	const auto& GetCommonBufferData() const { return featureCB; }

	virtual void PostPostLoad() override;

	virtual void DataLoaded() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	void OnSettingLoaded();

	uint GetNodeCount() const;
	void CompileShaders();
	bool SetupPixelBuffers();
	bool SetupPixelBuffers(uint numElem);
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	virtual bool SupportsVR() { return true; };
	virtual bool IsCore() const override { return false; };

	void PreSetStateDirty();
	void PreDrawHack();
	void SetupGeometry(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags);
	void RestoreGeometry(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags);
	void BeginAlphaGroup();
	void EndAlphaGroup();
	void BeginWater();
	void EndWater();
	[[nodiscard]] bool ShouldCapture() const { return inAlphaPass /*&& closeEnough*/; }

	struct FragmentListNode
	{
		uint next;
		float packedDepthAndFlags;
		std::array<uint, 2> packedColorRGBA;
	};

	static_assert(sizeof(FragmentListNode) == 16);

	// GPU Resources

	// OIT_AT
	std::optional<Texture2D>					headerBuffer; // RUINT32, Fragement list header buffer or clear mask
	std::optional<Buffer>						nodesBuffer;  // Fragement list nodes buffer
	// OIT_BLENDED
	std::optional<Texture2D>					accumalationBuffer;      // RGBA16F, transparent layers in front of water, blend to main color buffer
	std::optional<Texture2D>					accumalationWaterBuffer;  // RGBA16F, all transparent layers, blend to alpha only buffer for SSR next frame
	std::optional<Texture2D>					revealageBuffer;         // RGBA16F, x/y = revealage, w = min write-depth
	// OIT_RVO
	std::optional<Buffer>						colorBuffer; // RWStructuredBuffer<uint2[OIT_NODE_COUNTS]>
	std::optional<Buffer>						depthBuffer; // RWStructuredBuffer<float4[OIT_NODE_COUNTS]>

	winrt::com_ptr<ID3D11PixelShader>			psVisualize;   // For adaptive transparency
	winrt::com_ptr<ID3D11PixelShader>			psAT; // For adaptive transparency
	winrt::com_ptr<ID3D11PixelShader>			psBlend;   // For adaptive transparency
	winrt::com_ptr<ID3D11PixelShader>			psROV;   // For adaptive transparency

	winrt::com_ptr<ID3D11DepthStencilState>		depthStencilState; // depth testing but not writing
	winrt::com_ptr<ID3D11BlendState>			wboitBlendState; // weighted blended OIT accumulation
	winrt::com_ptr<ID3D11BlendState>			resolveBlendState; // depth testing but not writing
	winrt::com_ptr<ID3D11DepthStencilState>		resolveDepthStencilState;  // depth testing but not writing

	// Temporarily for setting render target in alpha pass, not reference counted
	std::array<ID3D11RenderTargetView*, 6>		rtvs;
	std::array<ID3D11UnorderedAccessView*, 3>	uavs;
	ID3D11DepthStencilView*						dsv;

	int											calls = 0;
	RE::NiPoint3								cameraPos;
	RE::NiTransform								cameraWorldInverse;
	bool										closeEnough = false;
	bool										drawWriteDepth = false;

	// States
	bool inAlphaPass = false;  // Whether we are in alpha pass of the render

	void DisableForResourceFailure();
};

#pragma once

#include "Feature.h"

#include <d3d11.h>
#include <unordered_map>
#include <winrt/base.h>

/**
 * @brief VR Stereo Rendering Optimizations feature.
 *
 * Uses hardware stencil culling to skip Eye 1 pixel shading for pixels that can be
 * reprojected from Eye 0 via lateral stereo reprojection, then runs a compute shader
 * to fill those pixels. This avoids redundant pixel shading in overlapping stereo regions.
 *
 * Pipeline:
 *   1. DispatchStencil()       - CS classifies per-pixel reprojection viability into a mode texture,
 *                                then a fullscreen VS/PS pass writes that classification into the stencil buffer.
 *   2. (Game renders Eye 1)    - Hardware stencil test skips shading for marked pixels.
 *   3. DispatchReprojection()  - CS reprojects Eye 0 color into the skipped Eye 1 pixels.
 */
struct VRStereoOptimizations : public Feature
{
	//=============================================================================
	// ENUMS
	//=============================================================================

	/// Operating mode for stereo reprojection
	enum class StereoMode : uint32_t
	{
		Off = 0,      ///< Feature disabled
		Enable = 1    ///< Stereo reprojection enabled
	};

	/// Per-pixel classification written by StencilCS
	enum PixelMode : uint8_t
	{
		MODE_DISOCCLUDED = 0,  ///< Fully shaded, no reprojection, no blend
		MODE_EDGE = 1,         ///< Fully shaded + bilateral blend with other eye
		MODE_MAIN = 2,         ///< Eye 0: no reproject (Perf) / bilateral (Quality). Eye 1: overwrite (Perf) / bilateral (Quality)
		MODE_EDGE_NEIGHBOUR = 3,  ///< Outer band: background pixels near edge, blended in post-process
	};

	//=============================================================================
	// FEATURE BASE CLASS OVERRIDES
	//=============================================================================

	virtual inline std::string GetName() override { return "VR Stereo Optimizations"; }
	virtual inline std::string GetShortName() override { return "VRStereoOptimizations"; }
	virtual inline std::string_view GetShaderDefineName() override { return "VR_STEREO_OPT"; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Utility; }
	virtual inline bool SupportsVR() override { return true; }

	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void ClearShaderCache() override;

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Stereo rendering optimizations for VR that skip redundant pixel shading via stencil culling and lateral reprojection.",
			{ "Hardware stencil culling of Eye 1 pixels reprojectable from Eye 0",
				"Compute shader lateral reprojection to fill culled pixels",
				"Performance, Quality, and Foveated modes",
				"Debug visualization overlays" }
		};
	}

	//=============================================================================
	// SETTINGS
	//=============================================================================

	struct Settings
	{
		StereoMode stereoMode = StereoMode::Enable;
		float disocclusionDepthThreshold = 0.01f;
		float edgeDepthThreshold = 0.05f;
		int edgeWidth = 3;  ///< Half-width of edge band in pixels (total band = 2 * edgeWidth)
		float minEdgeDistance = 5000.0f;  ///< Minimum linearized depth for edge AA (game units)
		float fullBlendDistance = 0.0f;  ///< Linearized depth below which both eyes are fully shaded + blended (game units)
		float pomDepthScale = 22.5f;  ///< Scale factor for POM depth correction in stereo reprojection
		bool debugFullBlendDepth = false;  ///< Show full blend depth zone as cyan overlay
		float qualityJitterOffset = 0.125f;
		float foveatedRegionRadius = 0.3f;
		float foveatedRegionCenterX = 0.5f;
		float foveatedRegionCenterY = 0.5f;
		bool useEyeTracking = false;

		int reprojectionMode = 5;  // 0=Blend, 4=Overwrite, 5=Overwrite Eye1 Only

		// Debug controls
		bool debugVisualization = false;
		bool debugSkipMerge = false;
		bool debugForceAllStencil = false;
		bool debugForceAllReprojectCS = false;
		bool debugDepthMap = false;
		bool debugPOMDepth = false;  ///< Show POM depth data (Reflectance.w) as heatmap overlay

		// MIP LOD Bias (negative = sharper textures)
		// 0 = Off, 1 = All textures (global), 2 = Distant trees only (depth-gated TREE_ANIM)
		int mipBiasMode = 0;
		float mipLodBias = -2.0f;
		float mipBiasNearDist = 2000.0f;   ///< Game units: no bias closer than this
		float mipBiasFarDist = 6000.0f;    ///< Game units: full bias beyond this

		// CAS (Contrast Adaptive Sharpening) - post-TAA
		float casStrength = 0.0f;  ///< 0.0 = disabled, 0.0-1.0 = subtle to strong (hidden for now)
		float alphaTestThreshold = 0.001f;  ///< Alpha floor for TREE_ANIM zombie texel removal
	} settings;

	//=============================================================================
	// GPU CONSTANT BUFFER (must match HLSL cbuffer layout exactly)
	//=============================================================================

	struct alignas(16) VRStereoOptParams
	{
		float FrameDim[2];              // Full stereo buffer dimensions
		float RcpFrameDim[2];           // 1.0 / FrameDim

		uint32_t StereoModeValue;       // Cast of StereoMode enum (0-3)
		float DisocclusionThreshold;
		float EdgeDepthThreshold;
		uint32_t EdgeWidth;

		float QualityJitter[2];         // Sub-pixel jitter offset (Quality mode)
		float FoveatedRadius;
		float pad2;

		float FoveatedCenter[2];        // Foveal region center UV
		float MinEdgeDistance;
		float FullBlendDistance;   // Linearized depth for full blend zone
	};
	static_assert(sizeof(VRStereoOptParams) % 16 == 0, "VRStereoOptParams must be 16-byte aligned for HLSL cbuffer.");

	//=============================================================================
	// PUBLIC API
	//=============================================================================

	/**
	 * @brief Classify Eye 1 pixels and write stencil marks.
	 *
	 * Dispatches the stencil classification CS, then performs a fullscreen triangle pass
	 * to write the classification into the hardware stencil buffer.
	 * Called from Deferred::StartDeferred() after OverrideBlendStates().
	 */
	void DispatchStencil();

	/**
	 * @brief Reproject Eye 0 color into stencil-culled Eye 1 pixels.
	 *
	 * Copies the main render target, then dispatches a CS to fill skipped pixels
	 * using lateral reprojection from Eye 0.
	 * Called from Deferred::DeferredPasses() after DeferredCompositeCS.
	 */
	void DispatchReprojection();

	/**
	 * @brief Creates or retrieves a modified DSS with stencil NOT_EQUAL test.
	 *
	 * Clones the given DSS with read-only stencil (WriteMask=0x00, Func=NOT_EQUAL, ref=1)
	 * so that pixels marked by our stencil write pass are skipped during normal rendering.
	 * Cached per unique input DSS pointer.
	 *
	 * @param originalDSS The original depth-stencil state to modify.
	 * @return Modified DSS with stencil test, or original if creation fails.
	 */
	ID3D11DepthStencilState* GetOrCreateModifiedDSS(ID3D11DepthStencilState* originalDSS);

	/// Whether the stencil pass is currently active this frame
	bool IsStencilActive() const { return stencilActive; }

	/// Deactivate stencil culling (called from Deferred after geometry rendering completes)
	void DeactivateStencil();

	/// Apply CAS sharpening to the main render target (called after TAA)
	void ApplyCAS(RE::RENDER_TARGET a_target);

	/// Get mode texture SRV for external consumers (e.g., DeferredCompositeCS Eye 1 skip)
	ID3D11ShaderResourceView* GetModeTextureSRV() const { return texPerPixelMode ? texPerPixelMode->srv.get() : nullptr; }

private:
	//=============================================================================
	// INTERNAL METHODS
	//=============================================================================

	/// Fullscreen triangle pass: reads mode texture, writes stencil ref=1 for MODE_MAIN pixels
	void ExecuteStencilWritePass();

	/// Late stencil write callback (placeholder for future multi-pass strategies)
	void PerformLateStencilWrite();

	/// Compiles all shaders used by this feature
	void CompileShaders();

	/// Updates the constant buffer with current settings and frame dimensions
	void UpdateConstantBuffer();

	//=============================================================================
	// GPU RESOURCES
	//=============================================================================

	eastl::unique_ptr<ConstantBuffer> paramsCB;
	eastl::unique_ptr<Texture2D> texPerPixelMode;       ///< R8_UINT classification texture (full SBS resolution)
	eastl::unique_ptr<Texture2D> reprojectionCopyTex;    ///< Copy of main RT for reprojection read

	winrt::com_ptr<ID3D11DepthStencilState> stencilWriteDSS;
	winrt::com_ptr<ID3D11RasterizerState> stencilWriteRS;
	winrt::com_ptr<ID3D11DepthStencilView> stencilWriteReadOnlyDSV;  ///< Read-only-depth DSV for stencil write pass (allows simultaneous depth SRV)

	winrt::com_ptr<ID3D11ComputeShader> stencilCS;
	winrt::com_ptr<ID3D11ComputeShader> stencilDebugDepthMapCS;
	winrt::com_ptr<ID3D11VertexShader> stencilWriteVS;
	winrt::com_ptr<ID3D11PixelShader> stencilWritePS;
	winrt::com_ptr<ID3D11ComputeShader> reprojectionCS;

	// CAS sharpening resources
	winrt::com_ptr<ID3D11ComputeShader> casCS;
	eastl::unique_ptr<Texture2D> casTex;  ///< UAV-capable texture for CAS output
	winrt::com_ptr<ID3D11Buffer> casParamsBuf;       ///< Structured buffer for CAS sharpness param
	winrt::com_ptr<ID3D11ShaderResourceView> casParamsSRV;  ///< SRV for CAS sharpness param

	/// Cache of original DSS -> modified DSS with stencil NOT_EQUAL enforcement
	std::unordered_map<ID3D11DepthStencilState*, winrt::com_ptr<ID3D11DepthStencilState>> dssCache;

	bool stencilActive = false;
	uint32_t stencilSwapCount = 0;
};

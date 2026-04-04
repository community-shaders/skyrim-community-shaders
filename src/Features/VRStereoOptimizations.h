#pragma once

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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
 *   1. DispatchStencil()             - CS classifies per-pixel reprojection viability into a mode texture,
 *                                      then a fullscreen VS/PS pass writes that classification into the stencil buffer.
 *   2. (Game renders Eye 1)          - Hardware stencil test skips shading for marked pixels.
 *   3. VR::DrawStereoBlend()         - Stereo overwrite CS reprojects Eye 0 color into skipped Eye 1 pixels.
 */
struct VRStereoOptimizations
{
	bool loaded = false;

	//=============================================================================
	// ENUMS
	//=============================================================================

	/// Operating mode for stereo reprojection
	enum class StereoMode : uint32_t
	{
		Off = 0,    ///< Feature disabled
		Enable = 1  ///< Stereo reprojection enabled
	};

	/// Per-pixel classification written by StencilCS
	enum PixelMode : uint8_t
	{
		MODE_DISOCCLUDED = 0,     ///< Fully shaded, no reprojection, no blend
		MODE_EDGE = 1,            ///< Fully shaded + bilateral blend with other eye
		MODE_MAIN = 2,            ///< Eye 0: no reproject (Perf) / bilateral (Quality). Eye 1: overwrite (Perf) / bilateral (Quality)
		MODE_EDGE_NEIGHBOUR = 3,  ///< Outer band: background pixels near edge, blended in post-process
		MODE_FULL_BLEND = 4,      ///< Near-camera pixels: fully shaded in both eyes + bilateral blended
	};

	//=============================================================================
	// PUBLIC METHODS
	//=============================================================================

	void SetupResources();
	void Reset();
	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();
	void ClearShaderCache();

	//=============================================================================
	// SETTINGS
	//=============================================================================

	struct Settings
	{
		StereoMode stereoMode = StereoMode::Enable;
		float disocclusionDepthThreshold = 0.01f;
		float edgeDepthThreshold = 0.05f;
		float minEdgeDistance = 5000.0f;     ///< Minimum linearized depth for edge AA (game units)
		float fullBlendDistance = 0.0f;      ///< Linearized depth below which both eyes are fully shaded + blended (game units)
		float pomDepthScale = 22.5f;         ///< Scale factor for POM depth correction in stereo reprojection
		float forwardOcclusionScale = 0.1f;  ///< Eye 0 depth multiplier for directional disocclusion; 0 = disabled
		bool debugFullBlendDepth = false;    ///< Show full blend depth zone as cyan overlay
		float qualityJitterOffset = 0.125f;
		float foveatedRegionRadius = 0.3f;
		float foveatedRegionCenterX = 0.5f;
		float foveatedRegionCenterY = 0.5f;
		bool useEyeTracking = false;

		// Debug controls
		bool debugVisualization = false;
		bool debugSkipMerge = false;
		bool debugForceAllStencil = false;
		bool debugForceAllReprojectCS = false;
		bool debugDepthMap = false;
		bool debugPOMDepth = false;  ///< Show POM depth data (Reflectance.w) as heatmap overlay

	} settings;

	//=============================================================================
	// GPU CONSTANT BUFFER (must match HLSL cbuffer layout exactly)
	//=============================================================================

	struct alignas(16) VRStereoOptParams
	{
		float FrameDim[2];     // Full stereo buffer dimensions
		float RcpFrameDim[2];  // 1.0 / FrameDim

		uint32_t StereoModeValue;  // Cast of StereoMode enum (0-3)
		float DisocclusionThreshold;
		float EdgeDepthThreshold;
		uint32_t EdgeWidth;

		float QualityJitter[2];  // Sub-pixel jitter offset (Quality mode)
		float FoveatedRadius;
		float ForwardOcclusionScale;  ///< Eye 0 depth multiplier for directional disocclusion (0 = disabled)

		float FoveatedCenter[2];  // Foveal region center UV
		float MinEdgeDistance;
		float FullBlendDistance;  // Linearized depth for full blend zone
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
	void NoteStencilSwap() { ++stencilSwapCount; }

	/// Deactivate stencil culling (called from Deferred after geometry rendering completes)
	void DeactivateStencil();

	/// Get mode texture SRV for external consumers (e.g., DeferredCompositeCS Eye 1 skip)
	ID3D11ShaderResourceView* GetModeTextureSRV() const { return texPerPixelMode ? texPerPixelMode->srv.get() : nullptr; }

private:
	//=============================================================================
	// INTERNAL METHODS
	//=============================================================================

	/// Fullscreen triangle pass: reads mode texture, writes stencil ref=1 for MODE_MAIN pixels
	void ExecuteStencilWritePass();

	/// Compiles all shaders used by this feature
	void CompileShaders();

	/// Updates the constant buffer with current settings and frame dimensions
	void UpdateConstantBuffer();

	//=============================================================================
	// GPU RESOURCES
	//=============================================================================

	eastl::unique_ptr<ConstantBuffer> paramsCB;
	eastl::unique_ptr<Texture2D> texPerPixelMode;  ///< R8_UINT classification texture (full SBS resolution)

	winrt::com_ptr<ID3D11DepthStencilState> stencilWriteDSS;
	winrt::com_ptr<ID3D11RasterizerState> stencilWriteRS;

	winrt::com_ptr<ID3D11ComputeShader> stencilCS;
	winrt::com_ptr<ID3D11ComputeShader> stencilDebugDepthMapCS;
	winrt::com_ptr<ID3D11VertexShader> stencilWriteVS;
	winrt::com_ptr<ID3D11PixelShader> stencilWritePS;

	/// Cache of original DSS -> modified DSS with stencil NOT_EQUAL enforcement
	std::unordered_map<ID3D11DepthStencilState*, winrt::com_ptr<ID3D11DepthStencilState>> dssCache;

	bool stencilActive = false;
	uint32_t stencilSwapCount = 0;
};

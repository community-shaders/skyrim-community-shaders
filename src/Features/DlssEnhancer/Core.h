#pragma once

// ============================================================================
// DlssEnhancer::Core — GPU resource pool & mode-dispatch entry point
// ============================================================================
//
// Owns all per-mode intermediate textures (Default / Faster / Extreme),
// compute-shader objects (stretch, temporal smooth, subrect blend), and the
// public entry points consumed by Upscaling.cpp.
//
// ============================================================================

#include "Buffer.h"
#include "Params.h"
#include <d3d11_4.h>
#include <winrt/base.h>

class Streamline;

namespace DlssEnhancer
{
	class Core
	{
	public:
		// Stage1: dispatches across Default / Faster / Extreme modes.
		static bool ExecuteVRDlssCore(Streamline& streamline,
			ID3D11Resource* upscalingTexture,
			ID3D11Resource* depthTexture,
			ID3D11Resource* reactiveMask,
			ID3D11Resource* transparencyMask,
			ID3D11Resource* motionVectors);

		// Shared VR per-eye preprocessing/finalization for non-DLSS callers (e.g. FSR).
		static bool PrepareVRPerEyeInputs(
			ID3D11Resource* colorSrc,
			ID3D11Resource* depthSrc,
			ID3D11Resource* mvecSrc,
			ID3D11Resource* reactiveSrc,
			ID3D11Resource* transparencySrc,
			uint32_t eyeWidthIn,
			uint32_t eyeHeightIn,
			uint32_t eyeWidthOut,
			uint32_t eyeHeightOut);

		static bool FinalizeVRPerEyeOutputs(
			ID3D11Resource* colorDst,
			uint32_t eyeWidthOut,
			uint32_t eyeHeightOut);

		// Release all GPU resources owned by Core.
		static void ClearResources();
		static void ClearShaderCache();

		// ── Own VR resources (independent from Upscaling) ──

		// Per-eye intermediate buffers (Default full-eye mode)
		static inline eastl::unique_ptr<Texture2D> vrIntermediateColorIn[2];
		static inline eastl::unique_ptr<Texture2D> vrIntermediateColorOut[2];
		static inline eastl::unique_ptr<Texture2D> vrIntermediateDepth[2];
		static inline eastl::unique_ptr<Texture2D> vrIntermediateMotionVectors[2];
		static inline eastl::unique_ptr<Texture2D> vrIntermediateReactiveMask[2];
		static inline eastl::unique_ptr<Texture2D> vrIntermediateTransparencyMask[2];

		// Subrect-sized textures (Default/Faster subrect mode)
		static inline eastl::unique_ptr<Texture2D> vrSubrectColorIn[2];
		static inline eastl::unique_ptr<Texture2D> vrSubrectColorOut[2];
		static inline eastl::unique_ptr<Texture2D> vrSubrectDepth[2];
		static inline eastl::unique_ptr<Texture2D> vrSubrectMotionVectors[2];
		static inline eastl::unique_ptr<Texture2D> vrSubrectReactiveMask[2];
		static inline eastl::unique_ptr<Texture2D> vrSubrectTransparencyMask[2];
		static inline uint32_t vrSubrectInW = 0, vrSubrectInH = 0, vrSubrectOutW = 0, vrSubrectOutH = 0;

		// Extreme mode: combined strip texture
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripColorIn;
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripColorOut;
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripDepth;
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripMotionVectors;
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripReactiveMask;
		static inline eastl::unique_ptr<Texture2D> vrExtremeStripTransparencyMask;
		static inline uint32_t vrExtremeStripW = 0, vrExtremeStripH = 0;
		static inline uint32_t vrExtremeStripOutW = 0, vrExtremeStripOutH = 0;

		// Faster mode per-eye output textures (subOutW × subOutH)
		static inline eastl::unique_ptr<Texture2D> vrFasterColorOut[2];
		static inline uint32_t vrFasterOutW = 0, vrFasterOutH = 0;

		// DRS region copy (render-resolution SBS)
		static inline eastl::unique_ptr<Texture2D> vrRenderSBS;
		static inline uint32_t vrRenderSBSW = 0, vrRenderSBSH = 0;

		// DRS stretch compute shader resources
		static inline winrt::com_ptr<ID3D11ComputeShader> vrSubrectStretchCS;
		static inline winrt::com_ptr<ID3D11Buffer> vrSubrectStretchCB;
		static inline winrt::com_ptr<ID3D11SamplerState> vrSubrectStretchSampler;

		// Periphery temporal smooth (ping-pong history at render-res SBS)
		static inline eastl::unique_ptr<Texture2D> vrTemporalHistory[2];   // SRV+UAV
		static inline winrt::com_ptr<ID3D11ShaderResourceView> vrMvecSRV;  // cached SRV on game's mvec resource
		static inline ID3D11Resource* vrMvecSRVOwner = nullptr;            // track which resource the SRV was created from
		static inline uint32_t vrTemporalHistoryW = 0, vrTemporalHistoryH = 0;
		static inline uint32_t vrTemporalFrameIdx = 0;
		static inline bool vrTemporalHistoryValid = false;

		// Temporal smooth compute shader resources
		static inline winrt::com_ptr<ID3D11ComputeShader> vrTemporalSmoothCS;
		static inline winrt::com_ptr<ID3D11Buffer> vrTemporalSmoothCB;
		static inline winrt::com_ptr<ID3D11SamplerState> vrTemporalSmoothSampler;

		// Subrect blend compute shader resources (feather / dither copy-back)
		static inline winrt::com_ptr<ID3D11ComputeShader> vrSubrectBlendCS;
		static inline winrt::com_ptr<ID3D11Buffer> vrSubrectBlendCB;
		static inline winrt::com_ptr<ID3D11ShaderResourceView> vrBlendSrcSRV;
		static inline ID3D11Resource* vrBlendSrcSRVOwner = nullptr;

		// Subrect UV hash for resource recreation detection
		static inline uint64_t activeSubrectUVHash = 0;

	private:
		static bool ExecuteDefaultMode(Streamline& streamline, const VRDlssParams& p);
		static bool ExecuteFasterMode(Streamline& streamline, const VRDlssParams& p);
		static bool ExecuteExtremeMode(Streamline& streamline, const VRDlssParams& p);
	};
}

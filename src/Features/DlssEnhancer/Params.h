#pragma once

#include "../DlssEnhancerFeature.h"
#include <d3d11.h>

namespace DlssEnhancer
{
	/// Unified parameter block consumed by Mode functions.
	/// Abstracts DLSSperf-awareness: when DLSSperf hook is active the display
	/// dimensions, output texture, and jitter are corrected transparently.
	struct VRDlssParams
	{
		// ── Dimensions ──
		uint32_t renderW;       // SBS render width  (after DRS)
		uint32_t renderH;       // SBS render height (after DRS)
		uint32_t eyeWidthIn;    // per-eye input  (render) width
		uint32_t eyeHeightIn;   // per-eye input  (render) height
		uint32_t eyeWidthOut;   // per-eye output (display) width
		uint32_t eyeHeightOut;  // per-eye output (display) height

		// ── Textures ──
		ID3D11Resource* colorSrc;                // input color  (always kMAIN)
		ID3D11Resource* colorDst;                // output color (kMAIN or testTexture)
		ID3D11UnorderedAccessView* colorDstUAV;  // UAV for stretch output target
		ID3D11Resource* depthTexture;
		ID3D11Resource* reactiveMask;
		ID3D11Resource* transparencyMask;
		ID3D11Resource* motionVectors;

		// ── Mode & subrect ──
		DlssEnhancerFeature::DlssMode mode;
		Subrect::UVRegion leftUV;
		Subrect::UVRegion rightUV;
		bool isFullEye;

		// ── Jitter (pixel-space, render resolution) ──
		float jitterX;
		float jitterY;

		/// Build a complete parameter block from current global state.
		/// @param upscalingTexture  kMAIN resource from Upscaling.
		static VRDlssParams Resolve(
			ID3D11Resource* upscalingTexture,
			ID3D11Resource* depthTexture,
			ID3D11Resource* reactiveMask,
			ID3D11Resource* transparencyMask,
			ID3D11Resource* motionVectors);
	};
}

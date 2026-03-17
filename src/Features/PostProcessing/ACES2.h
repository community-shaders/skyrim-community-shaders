#pragma once

// ACES 2.0 Output Transform
// Faithfully ported from the official aces-aswf/aces-core CTL reference (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)
//
// Key CTL source files:
//   Lib.Academy.OutputTransform.ctl
//   Lib.Academy.Tonescale.ctl

#include <array>
#include <cmath>

namespace ACES2
{
	// Table dimensions matching official CTL: tableSize=360, totalTableSize=362 (with wrapping entries)
	static constexpr int TABLE_SIZE = 360;
	static constexpr int TOTAL_TABLE_SIZE = TABLE_SIZE + 2;  // +2 for hue wrapping
	static constexpr int BASE_INDEX = 1;                     // first real entry in padded table

	// ========================================================================
	// GPU-side structures (must match HLSL declarations exactly)
	// ========================================================================

	// JMh color appearance model parameters for GPU
	// Contains precomputed matrices and constants for a single set of primaries
	struct alignas(16) JMhParamsGPU
	{
		float mtxRGBtoCAM16c[12];    // 3x4 row-major (combined RGB->XYZ->CAM16 with chromatic adaptation)
		float mtxCAM16cToRGB[12];    // 3x4 row-major inverse
		float mtxConeRespToAab[12];  // 3x4 row-major (cone response -> opponent Aab, prescaled)
		float mtxAabToConeResp[12];  // 3x4 row-major inverse

		float F_L_n;   // F_L / ref_luminance (luminance adaptation normalized)
		float cz;      // model_gamma = surround[1] * (1.48 + sqrt(Y_b / ref_luminance))
		float inv_cz;  // 1 / cz
		float A_w_J;   // _post_adaptation_cone_response_compression_fwd(F_L) — achromatic adapted white

		float inv_A_w_J;  // 1 / A_w_J
		float pad0, pad1, pad2;
	};

	// Tone scale parameters (Michaelis-Menten based curve)
	struct alignas(16) TSParamsGPU
	{
		float n;    // peakLuminance
		float n_r;  // 100.0 (reference peak luminance)
		float g;    // 1.15 (contrast exponent)
		float t_1;  // 0.04 (shadow toe)

		float c_t;  // derived crosstalk
		float s_2;  // derived shoulder
		float u_2;  // derived mid-shadow
		float m_2;  // derived max output normalized

		float forward_limit;  // input clamp limit
		float inverse_limit;  // inverse clamp limit
		float log_peak;       // log10(peakLuminance / 100)
		float pad;
	};

	// Complete GPU constant buffer
	// All data the shader needs for the full ACES 2.0 output transform
	struct alignas(16) ACES2CB
	{
		// Tonescale parameters
		TSParamsGPU ts;

		// JMh params for input primaries (AP0) — used for RGB<->JMh and J<->Y
		JMhParamsGPU inputParams;

		// JMh params for limiting primaries (sRGB for SDR, Rec.709 for SDR / P3-D65 for HDR)
		JMhParamsGPU limitParams;

		// Shared compression parameters
		float peakLuminance;
		float limitJMax;      // Y_to_J(peakLuminance, input_params)
		float modelGamma;     // surround[1] * (1.48 + sqrt(Y_b / ref_luminance))
		float modelGammaInv;  // 1 / modelGamma

		// Chroma compression parameters (peak-luminance dependent)
		float chromaSat;            // shadow expansion: max(0.2, 1.3 - 1.3*0.69*logPeak)
		float chromaSatThr;         // expansion threshold: 0.5 / peakLuminance
		float chromaCompr;          // highlight compression: 2.4 + 2.4*3.3*logPeak
		float chromaCompressScale;  // pow(0.03379*peak, 0.30596) - 0.45135

		// Gamut compression parameters
		float midJ;               // Y_to_J(c_t * ref_luminance, input_params)
		float focusDist;          // focus_distance + focus_distance * focus_distance_scaling * logPeak
		float lowerHullGamma;     // 1.14 + 0.07 * logPeak
		float lowerHullGammaInv;  // 1 / lowerHullGamma

		// Hue linearity search range (for fast binary search in non-uniform hue table)
		int hueSearchLo;
		int hueSearchHi;
		float pad0, pad1;

		// Limiting gamut -> display encoding gamut matrix (P3-D65 -> Rec.2020 for HDR, identity for SDR)
		float limitToDisplayMtx[12];  // 3x4 row-major

		// AP0 -> AP1 and AP1 -> AP0 matrices for clamping
		float AP0toAP1[12];  // 3x4 row-major
		float AP1toAP0[12];  // 3x4 row-major

		// Lookup tables (TOTAL_TABLE_SIZE = 362 entries each)
		float tableHues[TOTAL_TABLE_SIZE];            // non-uniform hue sample positions
		float tableCuspsJ[TOTAL_TABLE_SIZE];          // cusp J at each hue
		float tableCuspsM[TOTAL_TABLE_SIZE];          // cusp M at each hue
		float tableUpperHullGamma[TOTAL_TABLE_SIZE];  // per-hue upper hull gamma (stored as 1/gamma)
		float tableReachM[TOTAL_TABLE_SIZE];          // reach gamut max M at limitJMax
	};

	// Initialize all ACES 2.0 parameters for SDR (100 nits, sRGB output)
	ACES2CB ComputeParams(float peakLuminance = 100.0f);
}

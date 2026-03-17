// ACES 2.0 Output Transform — HLSL Implementation
// Faithfully ported from the official aces-aswf/aces-core CTL reference (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)
//
// Key CTL source files:
//   Lib.Academy.OutputTransform.ctl
//   Lib.Academy.Tonescale.ctl
//
// This shader reads precomputed parameters from a constant buffer
// and performs the full ACES 2.0 forward output transform:
//   ACEScg(AP1) → AP0 → clamp → JMh(CAM16) → tonescale + chroma compress → gamut compress → display RGB

#ifndef ACES2_HLSLI
#define ACES2_HLSLI

#include "Common/ColorSpaces.hlsli"

#define ACES2_TABLE_SIZE 360
#define ACES2_TOTAL_TABLE_SIZE 362
#define ACES2_BASE_INDEX 1

// ============================================================
// Constant buffer — must match ACES2::ACES2CB layout on CPU
// ============================================================
cbuffer ACES2Params : register(b2)
{
	// --- TSParamsGPU (48 bytes) ---
	float aces2_ts_n;
	float aces2_ts_n_r;
	float aces2_ts_g;
	float aces2_ts_t_1;

	float aces2_ts_c_t;
	float aces2_ts_s_2;
	float aces2_ts_u_2;
	float aces2_ts_m_2;

	float aces2_ts_forward_limit;
	float aces2_ts_inverse_limit;
	float aces2_ts_log_peak;
	float aces2_ts_pad;

	// --- JMhParamsGPU: inputParams (80 bytes) ---
	float4 aces2_inp_mtxRGBtoCAM16c[3];
	float4 aces2_inp_mtxCAM16cToRGB[3];
	float4 aces2_inp_mtxConeRespToAab[3];
	float4 aces2_inp_mtxAabToConeResp[3];
	float aces2_inp_F_L_n;
	float aces2_inp_cz;
	float aces2_inp_inv_cz;
	float aces2_inp_A_w_J;
	float aces2_inp_inv_A_w_J;
	float aces2_inp_pad0, aces2_inp_pad1, aces2_inp_pad2;

	// --- JMhParamsGPU: limitParams (80 bytes) ---
	float4 aces2_lim_mtxRGBtoCAM16c[3];
	float4 aces2_lim_mtxCAM16cToRGB[3];
	float4 aces2_lim_mtxConeRespToAab[3];
	float4 aces2_lim_mtxAabToConeResp[3];
	float aces2_lim_F_L_n;
	float aces2_lim_cz;
	float aces2_lim_inv_cz;
	float aces2_lim_A_w_J;
	float aces2_lim_inv_A_w_J;
	float aces2_lim_pad0, aces2_lim_pad1, aces2_lim_pad2;

	// --- Shared compression parameters ---
	float aces2_peakLuminance;
	float aces2_limitJMax;
	float aces2_modelGamma;
	float aces2_modelGammaInv;

	// --- Chroma compression ---
	float aces2_chromaSat;
	float aces2_chromaSatThr;
	float aces2_chromaCompr;
	float aces2_chromaCompressScale;

	// --- Gamut compression ---
	float aces2_midJ;
	float aces2_focusDist;
	float aces2_lowerHullGamma;
	float aces2_lowerHullGammaInv;

	// --- Hue search range ---
	int aces2_hueSearchLo;
	int aces2_hueSearchHi;
	float aces2_pad0, aces2_pad1;

	// --- Matrices ---
	float4 aces2_limitToDisplayMtx[3];
	float4 aces2_AP0toAP1[3];
	float4 aces2_AP1toAP0[3];
};

// Lookup tables via StructuredBuffer (TOTAL_TABLE_SIZE = 362 entries each)
StructuredBuffer<float> ACES2TableHues : register(t2);
StructuredBuffer<float> ACES2TableCuspsJ : register(t3);
StructuredBuffer<float> ACES2TableCuspsM : register(t4);
StructuredBuffer<float> ACES2TableUpperHullGamma : register(t5);
StructuredBuffer<float> ACES2TableReachM : register(t6);

// ============================================================
// CAM constants (matching official CTL)
// ============================================================
static const float ACES2_J_SCALE = 100.0;
static const float ACES2_CAM_NL_OFFSET = 27.13;  // 0.2713 * 100
static const float ACES2_REF_LUMINANCE = 100.0;

// Gamut compression constants
static const float ACES2_SMOOTH_CUSPS = 0.12;
static const float ACES2_SMOOTH_M = 0.27;
static const float ACES2_CUSP_MID_BLEND = 1.3;
static const float ACES2_FOCUS_GAIN_BLEND = 0.3;
static const float ACES2_FOCUS_ADJUST_GAIN = 0.55;
static const float ACES2_COMPRESSION_THRESHOLD = 0.75;

// ============================================================
// Helper: load 3x3 matrix from float4[3] (row-major)
// ============================================================
float3x3 ACES2_loadMat3(float4 rows[3])
{
	return float3x3(rows[0].xyz, rows[1].xyz, rows[2].xyz);
}

// ============================================================
// Post-adaptation cone response compression (official CTL)
// ============================================================
float ACES2_postAdapt_fwd_inner(float Rc)
{
	float F_L_Y = pow(Rc, 0.42);
	return F_L_Y / (ACES2_CAM_NL_OFFSET + F_L_Y);
}

float ACES2_postAdapt_inv_inner(float Ra)
{
	float Ra_lim = min(Ra, 0.99);
	float F_L_Y = ACES2_CAM_NL_OFFSET * Ra_lim / (1.0 - Ra_lim);
	return pow(F_L_Y, 1.0 / 0.42);
}

float ACES2_postAdapt_fwd(float v)
{
	return sign(v) * ACES2_postAdapt_fwd_inner(abs(v));
}

float ACES2_postAdapt_inv(float v)
{
	return sign(v) * ACES2_postAdapt_inv_inner(abs(v));
}

// ============================================================
// CAM model: J <-> Y (optimized achromatic path)
// Uses input params (AP0 primaries)
// ============================================================
float ACES2_J_to_Y(float J)
{
	float abs_J = abs(J);
	float A = pow(abs_J / ACES2_J_SCALE, aces2_inp_inv_cz);
	float Ra = aces2_inp_A_w_J * A;
	float Y = ACES2_postAdapt_inv_inner(Ra) / aces2_inp_F_L_n;
	return Y;
}

float ACES2_Y_to_J(float Y)
{
	float abs_Y = abs(Y);
	float Ra = ACES2_postAdapt_fwd_inner(abs_Y * aces2_inp_F_L_n);
	float J = ACES2_J_SCALE * pow(Ra * aces2_inp_inv_A_w_J, aces2_inp_cz);
	return sign(Y) * J;
}

// ============================================================
// CAM model: RGB -> JMh using precomputed JMhParams matrices
// The matrices encode: RGB -> CAM16 cone space (with chromatic adaptation)
// then cone response -> opponent Aab (with normalization)
// ============================================================
struct ACES2_JMhSet
{
	float4 mtxRGBtoCAM16c[3];
	float4 mtxCAM16cToRGB[3];
	float4 mtxConeRespToAab[3];
	float4 mtxAabToConeResp[3];
	float cz;
	float inv_cz;
};

float3 ACES2_RGB_to_JMh_params(float3 RGB, ACES2_JMhSet p)
{
	float3x3 mtxCam = ACES2_loadMat3(p.mtxRGBtoCAM16c);
	float3 rgb_m = mul(RGB, mtxCam);  // row-vector * matrix (CTL uses mult_f3_f33 = v * M)

	float3 rgb_a = float3(
		ACES2_postAdapt_fwd(rgb_m.x),
		ACES2_postAdapt_fwd(rgb_m.y),
		ACES2_postAdapt_fwd(rgb_m.z));

	float3x3 mtxAab = ACES2_loadMat3(p.mtxConeRespToAab);
	float3 Aab = mul(rgb_a, mtxAab);

	if (Aab.x <= 0.0)
		return float3(0, 0, 0);

	float J = ACES2_J_SCALE * pow(Aab.x, p.cz);
	float M = sqrt(Aab.y * Aab.y + Aab.z * Aab.z);
	float h_rad = atan2(Aab.z, Aab.y);
	float h = h_rad * 180.0 / 3.14159265;
	if (h < 0.0)
		h += 360.0;

	return float3(J, M, h);
}

float3 ACES2_JMh_to_RGB_params(float3 JMh, ACES2_JMhSet p)
{
	float h_rad = JMh.z * 3.14159265 / 180.0;
	float A = pow(JMh.x / ACES2_J_SCALE, p.inv_cz);
	float a = JMh.y * cos(h_rad);
	float b = JMh.y * sin(h_rad);
	float3 Aab = float3(A, a, b);

	float3x3 mtxInvAab = ACES2_loadMat3(p.mtxAabToConeResp);
	float3 rgb_a = mul(Aab, mtxInvAab);

	float3 rgb_m = float3(
		ACES2_postAdapt_inv(rgb_a.x),
		ACES2_postAdapt_inv(rgb_a.y),
		ACES2_postAdapt_inv(rgb_a.z));

	float3x3 mtxInvCam = ACES2_loadMat3(p.mtxCAM16cToRGB);
	return mul(rgb_m, mtxInvCam);
}

// Build input/limit param sets from cbuffer
ACES2_JMhSet ACES2_getInputParams()
{
	ACES2_JMhSet p;
	p.mtxRGBtoCAM16c[0] = aces2_inp_mtxRGBtoCAM16c[0];
	p.mtxRGBtoCAM16c[1] = aces2_inp_mtxRGBtoCAM16c[1];
	p.mtxRGBtoCAM16c[2] = aces2_inp_mtxRGBtoCAM16c[2];
	p.mtxCAM16cToRGB[0] = aces2_inp_mtxCAM16cToRGB[0];
	p.mtxCAM16cToRGB[1] = aces2_inp_mtxCAM16cToRGB[1];
	p.mtxCAM16cToRGB[2] = aces2_inp_mtxCAM16cToRGB[2];
	p.mtxConeRespToAab[0] = aces2_inp_mtxConeRespToAab[0];
	p.mtxConeRespToAab[1] = aces2_inp_mtxConeRespToAab[1];
	p.mtxConeRespToAab[2] = aces2_inp_mtxConeRespToAab[2];
	p.mtxAabToConeResp[0] = aces2_inp_mtxAabToConeResp[0];
	p.mtxAabToConeResp[1] = aces2_inp_mtxAabToConeResp[1];
	p.mtxAabToConeResp[2] = aces2_inp_mtxAabToConeResp[2];
	p.cz = aces2_inp_cz;
	p.inv_cz = aces2_inp_inv_cz;
	return p;
}

ACES2_JMhSet ACES2_getLimitParams()
{
	ACES2_JMhSet p;
	p.mtxRGBtoCAM16c[0] = aces2_lim_mtxRGBtoCAM16c[0];
	p.mtxRGBtoCAM16c[1] = aces2_lim_mtxRGBtoCAM16c[1];
	p.mtxRGBtoCAM16c[2] = aces2_lim_mtxRGBtoCAM16c[2];
	p.mtxCAM16cToRGB[0] = aces2_lim_mtxCAM16cToRGB[0];
	p.mtxCAM16cToRGB[1] = aces2_lim_mtxCAM16cToRGB[1];
	p.mtxCAM16cToRGB[2] = aces2_lim_mtxCAM16cToRGB[2];
	p.mtxConeRespToAab[0] = aces2_lim_mtxConeRespToAab[0];
	p.mtxConeRespToAab[1] = aces2_lim_mtxConeRespToAab[1];
	p.mtxConeRespToAab[2] = aces2_lim_mtxConeRespToAab[2];
	p.mtxAabToConeResp[0] = aces2_lim_mtxAabToConeResp[0];
	p.mtxAabToConeResp[1] = aces2_lim_mtxAabToConeResp[1];
	p.mtxAabToConeResp[2] = aces2_lim_mtxAabToConeResp[2];
	p.cz = aces2_lim_cz;
	p.inv_cz = aces2_lim_inv_cz;
	return p;
}

// ============================================================
// Tonescale (Michaelis-Menten, official ACES 2.0)
// ============================================================
float ACES2_tonescale_fwd(float x)
{
	float f = aces2_ts_m_2 * pow(max(0, x) / (x + aces2_ts_s_2), aces2_ts_g);
	float h = max(0, f * f / (f + aces2_ts_t_1));
	return h * aces2_ts_n_r;
}

// ============================================================
// Table lookups (non-uniform hue table with binary search)
// ============================================================

// Reach M uses uniform hue table (simple index)
float ACES2_reachM_from_table(float h)
{
	float wrapped = fmod(h, 360.0);
	if (wrapped < 0.0)
		wrapped += 360.0;
	int base = (int)(wrapped / 360.0 * ACES2_TABLE_SIZE);
	float t = wrapped - (float)base;  // fractional part within 1-degree bin
	int i_lo = base + ACES2_BASE_INDEX;
	int i_hi = i_lo + 1;
	return lerp(ACES2TableReachM[i_lo], ACES2TableReachM[i_hi], t);
}

// Non-uniform hue table binary search for interval
int ACES2_lookup_hue_interval(float h)
{
	// Binary search in the non-uniform hue table for the interval containing h
	uint i = ACES2_BASE_INDEX + (uint)(fmod(h < 0 ? h + 360 : h, 360.0) / 360.0 * ACES2_TOTAL_TABLE_SIZE);
	uint i_lo = (uint)max((int)ACES2_BASE_INDEX, (int)i + aces2_hueSearchLo);
	uint i_hi = (uint)min((int)(ACES2_BASE_INDEX + ACES2_TABLE_SIZE), (int)i + aces2_hueSearchHi);

	[loop] while (i_lo + 1 < i_hi)
	{
		uint mid = (i_lo + i_hi) / 2;
		if (h > ACES2TableHues[mid])
			i_lo = mid;
		else
			i_hi = mid;
	}
	return max(1, (int)i_hi);
}

// Cusp lookup via non-uniform hue table (binary search on hue)
float2 ACES2_cuspFromTable(float h)
{
	// Binary search for the hue interval in the cusp table
	// The cusp table entries are ordered by the non-uniform hues in ACES2TableHues
	int low_i = 0;
	int high_i = ACES2_BASE_INDEX + ACES2_TABLE_SIZE;
	int i = (int)(fmod(h < 0 ? h + 360 : h, 360.0) / 360.0 * ACES2_TABLE_SIZE) + ACES2_BASE_INDEX;

	[loop] while (low_i + 1 < high_i)
	{
		// Use the hue stored in the table (via TableHues) to compare
		// But our cusp table doesn't have its own hue column - we use TableHues for ordering
		float table_h = ACES2TableHues[i];
		if (h > table_h)
			low_i = i;
		else
			high_i = i;
		i = (low_i + high_i) / 2;
	}

	float lo_J = ACES2TableCuspsJ[high_i - 1];
	float lo_M = ACES2TableCuspsM[high_i - 1];
	float lo_h = ACES2TableHues[high_i - 1];
	float hi_J = ACES2TableCuspsJ[high_i];
	float hi_M = ACES2TableCuspsM[high_i];
	float hi_h = ACES2TableHues[high_i];

	float t = (h - lo_h) / max(hi_h - lo_h, 1e-10);
	return float2(lerp(lo_J, hi_J, t), lerp(lo_M, hi_M, t));
}

// Upper hull gamma from non-uniform table
float ACES2_upperHullGamma(float h)
{
	int i_hi = ACES2_lookup_hue_interval(h);
	float lo_h = ACES2TableHues[i_hi - 1];
	float hi_h = ACES2TableHues[i_hi];
	float t = (h - lo_h) / max(hi_h - lo_h, 1e-10);
	return lerp(ACES2TableUpperHullGamma[i_hi - 1], ACES2TableUpperHullGamma[i_hi], t);
}

// ============================================================
// Chroma compress norm (hue-dependent Fourier series)
// ============================================================
float ACES2_chromaCompressNorm(float h)
{
	float hr = h * 3.14159265 / 180.0;
	float a = cos(hr);
	float b = sin(hr);
	float cos_hr2 = a * a - b * b;
	float sin_hr2 = 2.0 * a * b;
	float cos_hr3 = 4.0 * a * a * a - 3.0 * a;
	float sin_hr3 = 3.0 * b - 4.0 * b * b * b;

	float M = 11.34072 * a +
	          16.46899 * cos_hr2 +
	          7.88380 * cos_hr3 +
	          14.66441 * b +
	          -6.37224 * sin_hr2 +
	          9.19364 * sin_hr3 +
	          77.12896;

	return M * aces2_chromaCompressScale;
}

// ============================================================
// Toe function (official CTL)
// ============================================================
float ACES2_toe(float x, float limit, float k1_in, float k2_in, bool invert)
{
	if (x > limit)
		return x;

	float k2 = max(k2_in, 0.001);
	float k1 = sqrt(k1_in * k1_in + k2 * k2);
	float k3 = (limit + k1) / (limit + k2);

	if (invert) {
		return (x * x + k1 * x) / (k3 * (x + k2));
	} else {
		float minus_b = k3 * x - k1;
		float minus_c = k2 * k3 * x;
		return 0.5 * (minus_b + sqrt(minus_b * minus_b + 4.0 * minus_c));
	}
}

// ============================================================
// Chroma compression (official CTL: chroma_compress_fwd)
// ============================================================
float3 ACES2_chromaCompress_fwd(float3 JMh, float tonemapped_J)
{
	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	float M_compr = M;

	if (M != 0.0) {
		float nJ = tonemapped_J / aces2_limitJMax;
		float snJ = max(0.0, 1.0 - nJ);
		float Mnorm = ACES2_chromaCompressNorm(h);
		float limit = pow(nJ, aces2_modelGammaInv) * ACES2_reachM_from_table(h) / Mnorm;

		float toe_limit = limit - 0.001;
		float toe_snJ_sat = snJ * aces2_chromaSat;
		float toe_sqrt_nJ_sat_thr = sqrt(nJ * nJ + aces2_chromaSatThr);
		float toe_nJ_compr = nJ * aces2_chromaCompr;

		// Rescale M with tonescaled J (using model gamma to keep chromaticities constant)
		M_compr = M * pow(max(1e-10, tonemapped_J / max(J, 1e-10)), aces2_modelGammaInv);

		// Normalize M
		M_compr = M_compr / Mnorm;

		// Expand (toe inverse): expand shadows/midtones
		M_compr = limit - ACES2_toe(limit - M_compr, toe_limit, toe_snJ_sat, toe_sqrt_nJ_sat_thr, false);

		// Compress (toe forward): create highlight desaturation rolloff
		M_compr = ACES2_toe(M_compr, limit, toe_nJ_compr, snJ, false);

		// Denormalize
		M_compr = M_compr * Mnorm;
	}

	return float3(tonemapped_J, M_compr, h);
}

// ============================================================
// Gamut compression (official CTL: gamut_compress_fwd)
// ============================================================

float ACES2_computeFocusJ(float cusp_J)
{
	return lerp(cusp_J, aces2_midJ, min(1.0, ACES2_CUSP_MID_BLEND - (cusp_J / aces2_limitJMax)));
}

float ACES2_getFocusGain(float J, float analytical_threshold)
{
	float gain = aces2_limitJMax * aces2_focusDist;
	if (J > analytical_threshold) {
		float adj = log10(max(1e-4, (aces2_limitJMax - analytical_threshold) / max(0.0001, aces2_limitJMax - J)));
		adj = adj * adj + 1.0;
		gain *= adj;
	}
	return gain;
}

float ACES2_solveJIntersect(float J, float M, float focusJ, float maxJ, float slope_gain)
{
	float M_scaled = M / slope_gain;
	float a = M_scaled / focusJ;

	if (J < focusJ) {
		float b = 1.0 - M_scaled;
		float c = -J;
		float det = b * b - 4.0 * a * c;
		return -2.0 * c / (b + sqrt(max(0, det)));
	} else {
		float b = -(1.0 + M_scaled + maxJ * a);
		float c = maxJ * M_scaled + J;
		float det = b * b - 4.0 * a * c;
		return -2.0 * c / (b - sqrt(max(0, det)));
	}
}

float ACES2_compressionVectorSlope(float intersect_J, float focus_J, float slope_gain)
{
	float dir = (intersect_J < focus_J) ? intersect_J : (aces2_limitJMax - intersect_J);
	return dir * (intersect_J - focus_J) / (focus_J * slope_gain);
}

float ACES2_sminScaled(float a, float b, float scale_ref)
{
	float s = ACES2_SMOOTH_CUSPS * scale_ref;
	float h = max(s - abs(a - b), 0.0) / max(s, 1e-10);
	return min(a, b) - h * h * h * s / 6.0;
}

float ACES2_estimateLineBoundaryM(float J_intersect, float slope, float inv_gamma, float J_max, float M_max, float J_ref)
{
	float nJ = J_intersect / max(J_ref, 1e-10);
	float shifted = J_ref * pow(max(1e-10, nJ), inv_gamma);
	return shifted * M_max / max(J_max - slope * M_max, 1e-10);
}

float ACES2_findGamutBoundaryM(
	float2 JMcusp, float J_max,
	float gamma_top_inv, float gamma_bottom_inv,
	float J_src, float slope, float J_cusp_int)
{
	float M_lower = ACES2_estimateLineBoundaryM(J_src, slope, gamma_bottom_inv, JMcusp.x, JMcusp.y, J_cusp_int);
	float f_J_cusp = J_max - J_cusp_int;
	float f_J_src = J_max - J_src;
	float f_cusp_J = J_max - JMcusp.x;
	float M_upper = ACES2_estimateLineBoundaryM(f_J_src, -slope, gamma_top_inv, f_cusp_J, JMcusp.y, f_J_cusp);
	return ACES2_sminScaled(M_lower, M_upper, JMcusp.y);
}

float ACES2_reinhardRemap(float scale, float nd, bool invert)
{
	if (invert) {
		if (nd >= 1.0)
			return scale;
		return scale * -(nd / (nd - 1.0));
	}
	return scale * nd / (1.0 + nd);
}

float ACES2_remapM(float M, float gamut_boundary_M, float reach_boundary_M)
{
	float ratio = gamut_boundary_M / max(reach_boundary_M, 1e-10);
	float proportion = max(ratio, ACES2_COMPRESSION_THRESHOLD);
	float threshold = proportion * gamut_boundary_M;

	if (M <= threshold || proportion >= 1.0)
		return M;

	float m_offset = M - threshold;
	float gamut_offset = gamut_boundary_M - threshold;
	float reach_offset = reach_boundary_M - threshold;

	float scale = reach_offset / max((reach_offset / max(gamut_offset, 1e-10)) - 1.0, 1e-10);
	float nd = m_offset / max(scale, 1e-10);

	return threshold + ACES2_reinhardRemap(scale, nd, false);
}

float3 ACES2_gamutCompress_fwd(float3 JMh)
{
	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	if (J <= 0.0)
		return float3(0, 0, h);

	if (M < 0.0 || J > aces2_limitJMax)
		return float3(J, 0, h);

	// Get hue-dependent parameters
	float2 JMcusp = ACES2_cuspFromTable(h);
	float gamma_top_inv = ACES2_upperHullGamma(h);
	float gamma_bottom_inv = aces2_lowerHullGammaInv;
	float focus_J = ACES2_computeFocusJ(JMcusp.x);
	float analytical_threshold = lerp(JMcusp.x, aces2_limitJMax, ACES2_FOCUS_GAIN_BLEND);

	// Compute compression vector
	float slope_gain = ACES2_getFocusGain(J, analytical_threshold);
	float J_src = ACES2_solveJIntersect(J, M, focus_J, aces2_limitJMax, slope_gain);
	float gamut_slope = ACES2_compressionVectorSlope(J_src, focus_J, slope_gain);

	float J_cusp_sg = ACES2_getFocusGain(JMcusp.x, analytical_threshold);
	float J_cusp_int = ACES2_solveJIntersect(JMcusp.x, JMcusp.y, focus_J, aces2_limitJMax, J_cusp_sg);

	float gamut_boundary_M = ACES2_findGamutBoundaryM(
		JMcusp, aces2_limitJMax,
		gamma_top_inv, gamma_bottom_inv,
		J_src, gamut_slope, J_cusp_int);

	if (gamut_boundary_M <= 0.0)
		return float3(J, 0, h);

	float reach_max_M = ACES2_reachM_from_table(h);

	float reach_boundary_M = ACES2_estimateLineBoundaryM(
		J_src, gamut_slope, aces2_modelGammaInv,
		aces2_limitJMax, reach_max_M, aces2_limitJMax);

	float remapped_M = ACES2_remapM(M, gamut_boundary_M, reach_boundary_M);

	return float3(J_src + remapped_M * gamut_slope, remapped_M, h);
}

// ============================================================
// Top-level ACES 2.0 Output Transform
// Input: linear ACEScg (AP1) — already in tonemapper native space
// Output: linear display-referred RGB (sRGB for SDR, Rec.2020 for HDR)
// ============================================================
float3 ACES2OutputTransform(float3 acescg)
{
	// Apply exposure from tonemapParams
	acescg *= tonemapParams[0].x;

	// AP1 -> AP0
	float3x3 AP1toAP0 = ACES2_loadMat3(aces2_AP1toAP0);
	float3 aces = mul(AP1toAP0, acescg);

	// Clamp to AP1 range (official: clamp_AP0_to_AP1 then back)
	float3x3 AP0toAP1 = ACES2_loadMat3(aces2_AP0toAP1);
	float3 ap1 = mul(AP0toAP1, aces);
	ap1 = clamp(ap1, 0.0, aces2_ts_forward_limit);
	aces = mul(AP1toAP0, ap1);

	// Convert AP0 -> JMh using input params (AP0 primaries)
	ACES2_JMhSet inp = ACES2_getInputParams();
	float3 JMh = ACES2_RGB_to_JMh_params(aces, inp);

	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	// ---- Tonemap + Chroma Compress ----
	// J -> Y (scene luminance) -> tonescale -> Y (display luminance) -> J
	float linearY = ACES2_J_to_Y(J) / ACES2_REF_LUMINANCE;
	float tonemapped_Y = ACES2_tonescale_fwd(linearY);
	float J_ts = ACES2_Y_to_J(tonemapped_Y);

	// Chroma compression (rescales M, expands shadows, compresses highlights)
	float3 JMh_tc = ACES2_chromaCompress_fwd(JMh, J_ts);

	// ---- Gamut Compress ----
	float3 JMh_gc = ACES2_gamutCompress_fwd(JMh_tc);

	// ---- Convert back to RGB in limiting gamut ----
	ACES2_JMhSet lim = ACES2_getLimitParams();
	float3 displayRGB = ACES2_JMh_to_RGB_params(JMh_gc, lim);

	// Convert from limiting gamut to display encoding gamut
	float3x3 limitToDisplay = ACES2_loadMat3(aces2_limitToDisplayMtx);
	displayRGB = mul(limitToDisplay, displayRGB);

<<<<<<< HEAD
	// Clamp to [0,1] displayable range
	displayRGB = saturate(displayRGB);
	== == == =
				 // Output scaling:
		// The tonescale outputs nits (Y * n_r). After JMh round-trip through limit params,
		// displayRGB is in [0, peakLuminance/ref_luminance] range.
		// For SDR: values are in [0, 1] — just clamp
		// For HDR: values represent linear light where 1.0 = ref_luminance(100 nits)
		//          no additional scaling needed, HDR display pipeline handles nit mapping
		if (enableHDR)
	{
		displayRGB = max(0, displayRGB);
	}
	else
	{
		displayRGB = saturate(displayRGB);
	}
>>>>>>> e7c5dbaff (try fix aces 2)

	return displayRGB;
}

#endif  // ACES2_HLSLI

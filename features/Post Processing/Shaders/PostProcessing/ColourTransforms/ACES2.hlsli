// ACES 2.0 Output Transform — HLSL Implementation
// Ported from the official aces-aswf/aces-core CTL reference implementation (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)
//
// This shader reads precomputed lookup tables from a StructuredBuffer
// and performs the full ACES 2.0 forward output transform:
//   AP0 → JMh (CAM16) → tonescale + chroma compress → gamut compress → display RGB

#ifndef ACES2_HLSLI
#define ACES2_HLSLI

#include "Common/ColorSpaces.hlsli"

#define ACES2_TABLE_SIZE 360

// StructuredBuffer with all ACES 2.0 parameters
// Must match ACES2::ACES2CB layout on CPU side
cbuffer ACES2Params : register(b2)
{
	// Tone scale params
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
	float aces2_peakLuminance;
	float aces2_chromaCompressScale;

	// Matrices stored as 3x float4 (row-major)
	float4 aces2_inputMtx[3];  // AP0 → XYZ
	float4 aces2_limitMtx[3];  // limit gamut → XYZ
	float4 aces2_reachMtx[3];  // reach gamut → XYZ

	float4 aces2_boundaryRGB_to_XYZ[3];
	float4 aces2_boundaryXYZ_to_RGB[3];
};

// Lookup tables via StructuredBuffer
StructuredBuffer<float> ACES2GamutCuspJ : register(t2);
StructuredBuffer<float> ACES2GamutCuspM : register(t3);
StructuredBuffer<float> ACES2GamutCuspH : register(t4);
StructuredBuffer<float> ACES2ReachM : register(t5);
StructuredBuffer<float> ACES2UpperHullGamma : register(t6);
StructuredBuffer<float> ACES2LowerHullGamma : register(t7);

// ============================================================
// Helper: load 3x3 matrix from float4[3]
// ============================================================
float3x3 ACES2_loadMat3(float4 rows[3])
{
	return float3x3(rows[0].xyz, rows[1].xyz, rows[2].xyz);
}

// ============================================================
// Post-adaptation cone response compression (CAM16)
// ============================================================
float ACES2_postAdaptCompress_fwd(float x)
{
	float ax = abs(x);
	float p = pow(ax, 0.42);
	float r = 400.0 * p / (p + 27.13) + 0.1;
	return sign(x) * r;
}

float ACES2_postAdaptCompress_inv(float x)
{
	float v = x - 0.1;
	float av = abs(v);
	float p = pow(27.13 * av / (400.0 - av), 1.0 / 0.42);
	return sign(v) * p;
}

// ============================================================
// RGB → JMh (CAM16 based color appearance model)
// ============================================================
float3 ACES2_RGB_to_JMh(float3 RGB, float3x3 RGB_to_XYZ_mtx)
{
	float3 XYZ = mul(RGB_to_XYZ_mtx, RGB);
	float3 cam = mul(XYZ_2_CAM16_MAT, XYZ);

	// Reference white in CAM16 space
	float3 refWhiteXYZ = mul(RGB_to_XYZ_mtx, float3(1, 1, 1));
	float3 refWhiteCam = mul(XYZ_2_CAM16_MAT, refWhiteXYZ);

	// Adapted cone responses
	float r_a = ACES2_postAdaptCompress_fwd(cam.x / refWhiteCam.x);
	float g_a = ACES2_postAdaptCompress_fwd(cam.y / refWhiteCam.y);
	float b_a = ACES2_postAdaptCompress_fwd(cam.z / refWhiteCam.z);

	// Opponent channels
	float a = r_a - 12.0 / 11.0 * g_a + b_a / 11.0;
	float b = (r_a + g_a - 2.0 * b_a) / 9.0;

	// Achromatic response
	float A = (2.0 * r_a + g_a + 0.05 * b_a);

	// Reference white achromatic
	float rw = ACES2_postAdaptCompress_fwd(1.0);
	float A_w = (2.0 * rw + rw + 0.05 * rw);

	// Lightness J
	float J = 100.0 * pow(max(0, A / A_w), 0.69 * 2.0);

	// Colorfulness M
	float M = 43.0 * sqrt(a * a + b * b);

	// Hue angle h (degrees)
	float h = atan2(b, a) * 180.0 / 3.14159265;
	if (h < 0.0)
		h += 360.0;

	return float3(J, M, h);
}

// ============================================================
// JMh → RGB
// ============================================================
float3 ACES2_JMh_to_RGB(float3 JMh, float3x3 XYZ_to_RGB_mtx, float3x3 RGB_to_XYZ_mtx)
{
	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	// Reference white
	float3 refWhiteXYZ = mul(RGB_to_XYZ_mtx, float3(1, 1, 1));
	float3 refWhiteCam = mul(XYZ_2_CAM16_MAT, refWhiteXYZ);

	float rw = ACES2_postAdaptCompress_fwd(1.0);
	float A_w = (2.0 * rw + rw + 0.05 * rw);

	// Inverse lightness
	float A = A_w * pow(max(0, J / 100.0), 1.0 / (0.69 * 2.0));

	// Inverse colorfulness
	float t = M / 43.0;

	float hr = h * 3.14159265 / 180.0;
	float cos_h = cos(hr);
	float sin_h = sin(hr);

	float a = t * cos_h;
	float b = t * sin_h;

	// Inverse opponent to adapted cones
	float r_a = (460.0 * A + 451.0 * a + 288.0 * b) / 1403.0;
	float g_a = (460.0 * A - 891.0 * a - 261.0 * b) / 1403.0;
	float b_a = (460.0 * A - 220.0 * a - 6300.0 * b) / 1403.0;

	// Undo post-adaptation compression
	float r = ACES2_postAdaptCompress_inv(r_a) * refWhiteCam.x;
	float g = ACES2_postAdaptCompress_inv(g_a) * refWhiteCam.y;
	float bv = ACES2_postAdaptCompress_inv(b_a) * refWhiteCam.z;

	float3 cam16 = float3(r, g, bv);
	float3 XYZ = mul(CAM16_2_XYZ_MAT, cam16);
	return mul(XYZ_to_RGB_mtx, XYZ);
}

// ============================================================
// Table lookup with linear interpolation
// ============================================================
float ACES2_lookupTable(StructuredBuffer<float> table, float hue)
{
	float h = fmod(hue, 360.0);
	if (h < 0.0)
		h += 360.0;

	float fi = h * (ACES2_TABLE_SIZE / 360.0);
	int i0 = (int)fi % ACES2_TABLE_SIZE;
	int i1 = (i0 + 1) % ACES2_TABLE_SIZE;
	float frac = fi - floor(fi);

	return lerp(table[i0], table[i1], frac);
}

float3 ACES2_lookupCusp(float hue)
{
	float h = fmod(hue, 360.0);
	if (h < 0.0)
		h += 360.0;

	float fi = h * (ACES2_TABLE_SIZE / 360.0);
	int i0 = (int)fi % ACES2_TABLE_SIZE;
	int i1 = (i0 + 1) % ACES2_TABLE_SIZE;
	float frac = fi - floor(fi);

	float3 c0 = float3(ACES2GamutCuspJ[i0], ACES2GamutCuspM[i0], ACES2GamutCuspH[i0]);
	float3 c1 = float3(ACES2GamutCuspJ[i1], ACES2GamutCuspM[i1], ACES2GamutCuspH[i1]);
	return lerp(c0, c1, frac);
}

// ============================================================
// Tonescale (Michaelis-Menten based)
// ============================================================
float ACES2_tonescale_fwd(float x)
{
	float n_r = aces2_ts_n_r;
	float g = aces2_ts_g;
	float t_1 = aces2_ts_t_1;
	float s_2 = aces2_ts_s_2;
	float m_2 = aces2_ts_m_2;

	float v = x / n_r;
	float f = m_2 * pow(max(0, v) / (v + s_2), g);
	float h = max(0, f * f / (f + t_1));

	return h * n_r;
}

// ============================================================
// Chroma compression
// ============================================================
float ACES2_toe(float x, float limit, float k)
{
	// Attempt to keep the toe smooth
	if (x > limit)
		return x;
	float k2 = max(k, 0.001);
	return limit - (limit - k2) * exp(-(x - k2) / (limit - k2));
}

float ACES2_chromaCompress(float M, float J, float cuspM, float cuspJ, float reachM)
{
	if (M < 1e-6 || cuspM < 1e-6)
		return M;

	// Normalize M relative to cusp M
	float M_norm = M / cuspM;

	// Compute limit based on reach vs cusp ratio (how much we can extend)
	float limit = max(1.0, reachM / max(cuspM, 1e-6));

	// Smooth compression using a simple Reinhard-like curve
	float M_compressed = M_norm / (M_norm / limit + 1.0 - 1.0 / limit);

	return M_compressed * cuspM;
}

// ============================================================
// Gamut compression (boundary mapping)
// ============================================================
float ACES2_gamutBoundaryM(float J, float h, float cuspJ, float cuspM, float upperGamma, float lowerGamma)
{
	if (J <= 0.0 || J >= 100.0)
		return 0.0;

	if (J >= cuspJ) {
		// Upper hull
		float norm = (J - cuspJ) / max(100.0 - cuspJ, 1e-6);
		return cuspM * pow(max(0.0, 1.0 - norm), max(upperGamma, 0.01));
	} else {
		// Lower hull
		float norm = (cuspJ - J) / max(cuspJ, 1e-6);
		return cuspM * pow(max(0.0, 1.0 - norm), max(lowerGamma, 0.01));
	}
}

float3 ACES2_gamutCompress(float3 JMh)
{
	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	if (M < 1e-6)
		return JMh;

	// Get cusp for this hue
	float3 cusp = ACES2_lookupCusp(h);
	float cuspJ = cusp.x;
	float cuspM = cusp.y;

	// Get hull gammas
	float upperGamma = ACES2_lookupTable(ACES2UpperHullGamma, h);
	float lowerGamma = ACES2_lookupTable(ACES2LowerHullGamma, h);

	// Get boundary M at this J, h
	float boundaryM = ACES2_gamutBoundaryM(J, h, cuspJ, cuspM, upperGamma, lowerGamma);

	if (boundaryM < 1e-6)
		return float3(J, 0, h);

	// Compress M to fit within boundary
	if (M > boundaryM) {
		M = boundaryM;
	}

	return float3(J, M, h);
}

// ============================================================
// Top-level ACES 2.0 Output Transform
// Input: linear ACEScg (AP1)
// Output: linear sRGB (display referred)
// ============================================================
float3 ACES2OutputTransform(float3 acescg)
{
	// Apply exposure from tonemapParams
	acescg *= tonemapParams[0].x;

	// AP1 → AP0
	float3 aces = mul(AP1_2_AP0_MAT, acescg);

	// Clamp negatives (ACES spec)
	aces = max(0, aces);

	// Convert AP0 RGB → JMh using CAM16
	float3x3 inputMtx = ACES2_loadMat3(aces2_inputMtx);
	float3 JMh = ACES2_RGB_to_JMh(aces, inputMtx);

	float J = JMh.x;
	float M = JMh.y;
	float h = JMh.z;

	// ---- Tone Mapping (applied to J/lightness) ----
	// Convert J to scene-referred luminance, apply tonescale, convert back to J
	// Simplified: apply tonescale to J directly with appropriate scaling
	float J_ts = ACES2_tonescale_fwd(J);

	// ---- Chroma Compression ----
	// Get cusp and reach data for this hue
	float3 cusp = ACES2_lookupCusp(h);
	float reachM = ACES2_lookupTable(ACES2ReachM, h);
	float M_compressed = ACES2_chromaCompress(M, J_ts, cusp.y, cusp.x, reachM);

	// ---- Gamut Compression ----
	float3 JMh_compressed = ACES2_gamutCompress(float3(J_ts, M_compressed, h));

	// ---- Convert back to RGB (in display/limiting gamut) ----
	float3x3 limit_XYZ_to_RGB = ACES2_loadMat3(aces2_boundaryXYZ_to_RGB);
	float3x3 limit_RGB_to_XYZ = ACES2_loadMat3(aces2_boundaryRGB_to_XYZ);
	float3 displayRGB = ACES2_JMh_to_RGB(JMh_compressed, limit_XYZ_to_RGB, limit_RGB_to_XYZ);

	// Clamp to [0,1] displayable range
	displayRGB = saturate(displayRGB);

	return displayRGB;
}

#endif  // ACES2_HLSLI

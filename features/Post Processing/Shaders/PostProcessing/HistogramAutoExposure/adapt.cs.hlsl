/// By ProfJack/五脚猫, 2024-2-17 UTC

#include "PostProcessing/HistogramAutoExposure/common.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);
StructuredBuffer<float> RWTexAdaptation : register(t1);

// Pre-calculate constants for matrix multiplications
static const float4x3 RGB2LMSR_MATRIX = float4x3(
											0.31670331, 0.70299344, 0.08120592,
											0.10129085, 0.72118661, 0.12041039,
											0.01451538, 0.05643031, 0.53416779,
											0.01724063, 0.60147464, 0.40056206) *
                                        24.303;

static const float3x3 LMS2RGB_MATRIX = float3x3(
										   4.57829597, -4.48749114, 0.31554848,
										   -0.63342362, 2.03236026, -0.36183302,
										   -0.05749394, -0.09275939, 1.90172089) /
                                       24.303;

// Optimized matrix multiplication
float4 RGB2LMSR(float3 c)
{
	return mul(RGB2LMSR_MATRIX, c);
}

float3 LMS2RGB(float3 c)
{
	return mul(LMS2RGB_MATRIX, c);
}

// Constants for Purkinje calculation - moved outside of function
static const float3 m = float3(0.63721, 0.39242, 1.6064);
static const float K = 45.0;
static const float S = 10.0;
static const float k3 = 0.6;
static const float k5 = 0.2;
static const float k6 = 0.29;
static const float rw = 0.139;
static const float p = 0.6189;
static const float logExposure = 380.0f;
static const float K_S = K / S;  // Pre-calculate division

// https://www.ncbi.nlm.nih.gov/pmc/articles/PMC2630540/pdf/nihms80286.pdf
// Optimized Purkinje shift calculation
float3 PurkinjeShift(float3 c, float nightAdaptation)
{
	// Skip calculation entirely if adaptation is near zero
	if (nightAdaptation < 1e-5)
		return c;

	float4 lmsr = RGB2LMSR(c * logExposure);

	// Pre-calculate some terms for g calculation
	float3 lmsr_w_terms = float3(k5, k5, k6) * lmsr.w;
	float3 denominator = 1 + (.33 / m) * (lmsr.xyz + lmsr_w_terms);
	float3 g = rsqrt(denominator);  // Use faster rsqrt instead of 1/sqrt

	// Pre-calculate some common terms
	float g_x_over_m_x = g.x / m.x;
	float g_y_over_m_y = g.y / m.y;
	float g_z_over_m_z = g.z / m.z;
	float k5_lmsr_w = k5 * lmsr.w;

	// Calculate color opponent signals
	float rc_gr = K_S * ((1.0 + rw * k3) * g_y_over_m_y - (k3 + rw) * g_x_over_m_x) * k5_lmsr_w;
	float rc_by = K_S * (k6 * g_z_over_m_z - k3 * (p * k5 * g_x_over_m_x + (1.0 - p) * k5 * g_y_over_m_y)) * lmsr.w;
	float rc_lm = K * (p * g_x_over_m_x + (1.0 - p) * g_y_over_m_y) * k5_lmsr_w;

	// Calculate gain factors - simplified for performance
	float half_rc_gr = 0.5 * rc_gr;
	float3 lms_gain = float3(-half_rc_gr + 0.5 * rc_lm, half_rc_gr + 0.5 * rc_lm, rc_by + rc_lm) * nightAdaptation;

	// Convert back to RGB
	return LMS2RGB(lmsr.rgb + lms_gain) / logExposure;
}

[numthreads(32, 32, 1)] void main(uint2 tid
								  : SV_DispatchThreadID) {
	const static float logEV = -3;  // log2(0.125)

	// Early out for out-of-bounds threads
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);
	if (tid.x >= dims.x || tid.y >= dims.y)
		return;

	float3 color = TexColor[tid].rgb;

	// auto exposure
	float avgLuma = RWTexAdaptation[0];
	color *= 0.18 * ExposureCompensation / clamp(avgLuma, AdaptationRange.x, AdaptationRange.y);

	// Optimize Purkinje shift calculation by avoiding unnecessary work
	if (PurkinjeStrength > 1e-3) {
		// Pre-calculate some parts of the purkinje mix calculation
		float log_avgLuma = log2(avgLuma);
		float mix_term = (log_avgLuma - logEV - PurkinjeMaxEV) / (PurkinjeStartEV - PurkinjeMaxEV);
		float purkinjeMix = lerp(PurkinjeStrength, 0.f, saturate(mix_term));

		// Only do the expensive PurkinjeShift calculation if the mix factor is significant
		if (purkinjeMix > 1e-3)
			color = PurkinjeShift(color, purkinjeMix);
	}

	RWTexOut[tid] = float4(color, 1);
}
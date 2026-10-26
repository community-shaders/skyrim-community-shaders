/// Bloom/Flare/Glare/Exposure Composite pass
/// Combines bloom, lens flare, physical glare, and auto exposure results with the main color texture.
/// Formula: SceneColor * GlobalExposure * LocalExposure + Bloom * GlobalExposure
/// Scene and Bloom paths are kept separate so LocalExposure is applied on scene only.
/// Purkinje effect is applied after compositing on the final perceived image.
/// Uses #ifdef HAS_BLOOM / HAS_LENS_FLARE / HAS_GLARE / HAS_EXPOSURE / HAS_LOCAL_EXPOSURE to control behavior.

#include "PostProcessing/fullscreen.hlsli"

#include "Common/Color.hlsli"

Texture2D<float4> TexColor : register(t0);

#ifdef HAS_BLOOM
Texture2D<float4> TexBloom : register(t1);
#endif

#ifdef HAS_LENS_FLARE
Texture2D<float4> TexFlare : register(t2);
#endif

#ifdef HAS_GLARE
Texture2D<float4> TexGlare : register(t3);
#endif

#ifdef HAS_EXPOSURE
StructuredBuffer<float> TexAdaptation : register(t4);
#endif

#ifdef HAS_LOCAL_EXPOSURE
Texture2D<float> TexLocalExposureBase : register(t5);

cbuffer LocalExposureCB : register(b2)
{
	float ManualExposure : packoffset(c0.x);
	float Strength : packoffset(c0.y);
	float HighlightContrast : packoffset(c0.z);
	float ShadowContrast : packoffset(c0.w);
	float DetailStrength : packoffset(c1.x);
	float MiddleGreyBias : packoffset(c1.w);
	float HighlightThreshold : packoffset(c2.x);
	float ShadowThreshold : packoffset(c2.y);
	float HighlightThresholdStrength : packoffset(c2.z);
	float ShadowThresholdStrength : packoffset(c2.w);
	float LogLuminanceMin : packoffset(c4.x);
	float LogLuminanceMax : packoffset(c4.y);
};

float RemapBaseContrast(float centeredBase)
{
	bool isHighlight = centeredBase >= 0.0;
	float magnitude = abs(centeredBase);
	float contrast = isHighlight ? HighlightContrast : ShadowContrast;
	float threshold = isHighlight ? HighlightThreshold : ShadowThreshold;
	float thresholdStrength = isHighlight ? HighlightThresholdStrength : ShadowThresholdStrength;

	// Preserve tones near middle grey, then release smoothly into the selected
	// base contrast. At full threshold strength the protected shoulder persists.
	float transitionWidth = rcp(max(1.0 - thresholdStrength, 1e-3));
	float release = smoothstep(threshold, threshold + transitionWidth, magnitude);
	float protectedOffset = min(magnitude, threshold) * (1.0 - release);
	float remappedMagnitude = magnitude * contrast + protectedOffset * (1.0 - contrast);

	if (isHighlight && magnitude > threshold)
		remappedMagnitude = max(remappedMagnitude, threshold);

	return isHighlight ? remappedMagnitude : -remappedMagnitude;
}

float ComputeLocalExposure(float3 sceneColor, float baseLogLuminance, float globalExposure, float middleGreyCompensation)
{
	float sceneLuminance = Color::RGBToLuminance(max(sceneColor, 0.0));
	float sceneLogLuminance = clamp(log2(max(sceneLuminance, exp2(LogLuminanceMin))), LogLuminanceMin, LogLuminanceMax);
	float logGlobalExposure = log2(max(globalExposure, 1e-5));
	float exposedLogLuminance = sceneLogLuminance + logGlobalExposure;
	float exposedBase = baseLogLuminance + logGlobalExposure;
	float detail = exposedLogLuminance - exposedBase;
	float middleGrey = log2(0.18 * middleGreyCompensation) + MiddleGreyBias;
	float remappedBase = middleGrey + RemapBaseContrast(exposedBase - middleGrey);
	float targetLogLuminance = remappedBase + detail * DetailStrength;
	float logAdjustment = (targetLogLuminance - exposedLogLuminance) * Strength;
	return exp2(clamp(logAdjustment, -8.0, 8.0));
}
#endif

#ifdef HAS_EXPOSURE
cbuffer AutoExposureCB : register(b1)
{
	float2 AdaptArea;
	float2 AdaptationRange;
	float AdaptLerp;
	float ExposureCompensation;
	float PurkinjeStartEV;
	float PurkinjeMaxEV;
	float PurkinjeStrength;

	float pad[3];
};

// ==================== Purkinje Effect ====================
// https://www.ncbi.nlm.nih.gov/pmc/articles/PMC2630540/pdf/nihms80286.pdf
// Simulates the blue shift of human vision under low light conditions.
// Applied AFTER compositing, on the final perceived (exposed) image.

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

float4 RGB2LMSR(float3 c)
{
	return mul(RGB2LMSR_MATRIX, c);
}

float3 LMS2RGB(float3 c)
{
	return mul(LMS2RGB_MATRIX, c);
}

static const float3 m = float3(0.63721, 0.39242, 1.6064);
static const float K = 45.0;
static const float S = 10.0;
static const float k3 = 0.6;
static const float k5 = 0.2;
static const float k6 = 0.29;
static const float rw = 0.139;
static const float p = 0.6189;
static const float logExposure = 380.0f;
static const float K_S = K / S;

float3 PurkinjeShift(float3 c, float nightAdaptation)
{
	if (nightAdaptation < 1e-5)
		return c;

	float4 lmsr = RGB2LMSR(c * logExposure);

	float3 lmsr_w_terms = float3(k5, k5, k6) * lmsr.w;
	float3 denominator = 1 + (.33 / m) * (lmsr.xyz + lmsr_w_terms);
	float3 g = rsqrt(denominator);

	float g_x_over_m_x = g.x / m.x;
	float g_y_over_m_y = g.y / m.y;
	float g_z_over_m_z = g.z / m.z;
	float k5_lmsr_w = k5 * lmsr.w;

	float rc_gr = K_S * ((1.0 + rw * k3) * g_y_over_m_y - (k3 + rw) * g_x_over_m_x) * k5_lmsr_w;
	float rc_by = K_S * (k6 * g_z_over_m_z - k3 * (p * k5 * g_x_over_m_x + (1.0 - p) * k5 * g_y_over_m_y)) * lmsr.w;
	float rc_lm = K * (p * g_x_over_m_x + (1.0 - p) * g_y_over_m_y) * k5_lmsr_w;

	float half_rc_gr = 0.5 * rc_gr;
	float3 lms_gain = float3(-half_rc_gr + 0.5 * rc_lm, half_rc_gr + 0.5 * rc_lm, rc_by + rc_lm) * nightAdaptation;

	return LMS2RGB(lmsr.rgb + lms_gain) / logExposure;
}
#endif  // HAS_EXPOSURE

float4 PSComposite(FullscreenTriangleVSOutput input) : SV_Target
{
	uint2 tid = uint2(input.Position.xy);

	float3 sceneColor = TexColor[tid].rgb;

	// Accumulate bloom/flare/glare contributions (separate from scene)
	float3 bloomContrib = 0;

#ifdef HAS_BLOOM
	bloomContrib += TexBloom[tid].rgb;
#endif

#ifdef HAS_LENS_FLARE
	bloomContrib += TexFlare[tid].rgb;
#endif

#ifdef HAS_GLARE
	bloomContrib += TexGlare[tid].rgb;
#endif

#ifdef HAS_EXPOSURE
	// Compute global exposure value
	float avgLuma = TexAdaptation[0];
	float globalExposure = 0.18 * ExposureCompensation / clamp(avgLuma, AdaptationRange.x, AdaptationRange.y);

	// Formula: SceneColor * GlobalExposure * LocalExposure + Bloom * GlobalExposure
	// LocalExposure multiplier derived from the base luminance pass (1.0 if not enabled)
#	ifdef HAS_LOCAL_EXPOSURE
	float baseLogLuminance = TexLocalExposureBase[tid];
	float localExposure = ComputeLocalExposure(sceneColor, baseLogLuminance, globalExposure, ExposureCompensation);
#	else
	float localExposure = 1.0;
#	endif

	float3 result = sceneColor * globalExposure * localExposure + bloomContrib * globalExposure;

	// Purkinje effect: applied on the final perceived (exposed + composited) image
	if (PurkinjeStrength > 1e-3) {
		float avgEV100 = log2(avgLuma) + 3.0;
		float mix_term = (avgEV100 - PurkinjeMaxEV) / (PurkinjeStartEV - PurkinjeMaxEV);
		float purkinjeMix = lerp(PurkinjeStrength, 0.0, saturate(mix_term));

		if (purkinjeMix > 1e-3)
			result = PurkinjeShift(result, purkinjeMix);
	}
#else
	// No global exposure
#	ifdef HAS_LOCAL_EXPOSURE
	float baseLogLuminance = TexLocalExposureBase[tid];
	float localExposure = ComputeLocalExposure(sceneColor, baseLogLuminance, ManualExposure, 1.0);
	float3 result = (sceneColor * localExposure + bloomContrib) * ManualExposure;
#	else
	// No exposure at all: simple additive composite
	float3 result = sceneColor + bloomContrib;
#	endif
#endif

	return float4(result, 1);
}

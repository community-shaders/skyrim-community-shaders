
#include "Common/Color.hlsli"
#include "Common/PumboDICE.hlsli"
#include "Common/Uncharted2Tonemapper.hlsli"

Texture2D<float4> Framebuffer : register(t0);

RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 HDRData;
};

static const float PQ_constant_N = (2610.0 / 4096.0 / 4.0);
static const float PQ_constant_M = (2523.0 / 4096.0 * 128.0);

// PQ (Perceptual Quantiser; ST.2084) encode/decode used for HDR TV and grading
float3 LinearToPQ(float3 linearCol, const float maxPqValue)
{
	linearCol /= maxPqValue;

	float3 colToPow = pow(linearCol, PQ_constant_N);
	float3 numerator = PQ_constant_C1 + PQ_constant_C2 * colToPow;
	float3 denominator = 1.0 + PQ_constant_C3 * colToPow;
	float3 pq = pow(numerator / denominator, PQ_constant_M);

	return pq;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float3 framebuffer = Framebuffer[dispatchID.xy];

	// Tonemap game render before UI blending
#if 0
    // Gamma space tonemapping results are more pleasing
	// Investigate linear tonemapping with lowered exposure	
    framebuffer.rgb = tonemap::frostbite::BT709(framebuffer.rgb, Color::LinearToGammaSafe(HDRData.y / HDRData.z), 0.5);
#else
	float exposureBias = 1.0;
	float hue_correction_strength = 0.5;  // recommended range: below 1
	float shoulderStart = 0.15;           // recommended range: 0.25 - 0.5

	float3 untonemapped = Color::GammaToLinearSafe(framebuffer.rgb) * exposureBias;
	framebuffer.rgb = applyPumboDICE(untonemapped.rgb, HDRData.z, HDRData.y, shoulderStart);
	framebuffer.rgb = Color::Correct::Hue(framebuffer.rgb, applyUncharted2Tonemap(untonemapped), hue_correction_strength);
	framebuffer.rgb = Color::LinearToGammaSafe(framebuffer.rgb);

#endif
	// Apply the Reinhard tonemapper on any background color in excess, to avoid it burning it through the UI.
	float3 excessBackgroundColor = framebuffer - min(1.0, framebuffer);
	float3 tonemappedBackgroundColor = excessBackgroundColor / (1.0 + excessBackgroundColor);
	framebuffer = min(1.0, framebuffer) + lerp(tonemappedBackgroundColor, excessBackgroundColor, 1.0);

	framebuffer = Color::GammaToLinearSafe(framebuffer);

	// Scale combined UI and game render by game brightness
	framebuffer *= HDRData.z;

	// Convert to BT.2020 and Encode in PQ
	framebuffer = Color::BT709ToBT2020(framebuffer);
	framebuffer = LinearToPQ(framebuffer, 10000.f);

	HDROutput[dispatchID.xy] = float4(framebuffer, 1.0);
};

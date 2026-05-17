// Camera effects for Community Shaders
// Film grain based on https://www.shadertoy.com/view/3sGSWV (MIT License)

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "PostProcessing/common.hlsli"

cbuffer CameraCB : register(b1)
{
	// Fisheye
	float FEFoV;
	float FECrop;

	// Chromatic aberration
	float CAStrength;

	// Noise
	float NoiseStrength;
	int NoiseType;
	float2 ScreenSize;

	bool UseFE;
}

Texture2D<float4> InputTexture : register(t0);

SamplerState ColorSampler : register(s0);

RWTexture2D<float4> OutputTexture : register(u0);

#define BUFFER_ASPECT_RATIO ScreenSize.x / ScreenSize.y
#define ASPECT_RATIO float2(BUFFER_ASPECT_RATIO, 1.0)

// Film grain helper functions
// From Dave Hoskins: https://www.shadertoy.com/view/4djSRW
float GrainHash(float3 p3)
{
	p3 = frac(p3 * 0.1031);
	p3 += dot(p3, p3.yzx + 19.19);
	return frac((p3.x + p3.y) * p3.z);
}

// From iq: https://www.shadertoy.com/view/4sfGzS
float GrainNoise(float3 x)
{
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(lerp(GrainHash(i + float3(0, 0, 0)),
						 GrainHash(i + float3(1, 0, 0)), f.x),
					lerp(GrainHash(i + float3(0, 1, 0)),
						GrainHash(i + float3(1, 1, 0)), f.x),
					f.y),
		lerp(lerp(GrainHash(i + float3(0, 0, 1)),
				 GrainHash(i + float3(1, 0, 1)), f.x),
			lerp(GrainHash(i + float3(0, 1, 1)),
				GrainHash(i + float3(1, 1, 1)), f.x),
			f.y),
		f.z);
}

// Slightly high-passed continuous value-noise
float GrainSource(float3 x, float strength, float pitch)
{
	float center = GrainNoise(x);
	float v1 = center - GrainNoise(float3(1, 0, 0) / pitch + x) + 0.5;
	float v2 = center - GrainNoise(float3(0, 1, 0) / pitch + x) + 0.5;
	float v3 = center - GrainNoise(float3(-1, 0, 0) / pitch + x) + 0.5;
	float v4 = center - GrainNoise(float3(0, -1, 0) / pitch + x) + 0.5;

	float total = (v1 + v2 + v3 + v4) * 0.25;
	return lerp(1.0, 0.5 + total, strength);
}

float2 FishEye(float2 texcoord, float FEFoV, float FECrop)
{
	float2 radiant_vector = texcoord - 0.5;
	float diagonal_length = length(ASPECT_RATIO);

	float fov_factor = Math::PI * float(FEFoV) / 360.0;

	float fit_fov = sin(atan(tan(fov_factor) * diagonal_length));
	float crop_value = lerp(1.0 + (diagonal_length - 1.0) * cos(fov_factor), diagonal_length, FECrop * pow(abs(sin(fov_factor)), 6.0));

	// Circularize radiant vector and apply cropping
	float2 cn_radiant_vector = 2.0 * radiant_vector * ASPECT_RATIO / crop_value * fit_fov;

	if (length(cn_radiant_vector) < 1.0) {
		float z = sqrt(1.0 - cn_radiant_vector.x * cn_radiant_vector.x - cn_radiant_vector.y * cn_radiant_vector.y);
		float theta = acos(z) / fov_factor;

		float2 d = normalize(cn_radiant_vector);
		texcoord = (theta * d) / (2.0 * ASPECT_RATIO) + 0.5;
	}

	return texcoord;
}

[numthreads(8, 8, 1)] void CS_Camera(uint3 DTid : SV_DispatchThreadID) {
	static const float2 TEXEL_SIZE = float2(1.0f / ScreenSize.x, 1.0f / ScreenSize.y);
	float2 texcoord = (DTid.xy + 0.5f) * TEXEL_SIZE;
	float2 texcoord_clean = texcoord.xy;

	// Fisheye
	if (UseFE) {
		texcoord.xy = FishEye(texcoord_clean, FEFoV, FECrop);
	}

	float3 color = InputTexture.SampleLevel(ColorSampler, texcoord, 0).rgb;

	// Chromatic aberration
	[branch] if (CAStrength != 0.0)
	{
		color = SampleCA(InputTexture, ColorSampler, texcoord, CAStrength, 0).rgb;
	}

	// Film grain
	[branch] if (NoiseStrength != 0.0)
	{
		float2 pixelCoord = DTid.xy;
		float t = float(SharedData::FrameCount);

		static const float GRAIN_RATE = 1.0;
		static const float GRAIN_PITCH = 1.0;
		static const float GRAIN_LIFT_RATIO = 0.5;

		float3 grain;
		if (NoiseType == 1) {
			// Color grain
			float rg = GrainSource(float3(pixelCoord, floor(GRAIN_RATE * t)), NoiseStrength, GRAIN_PITCH);
			float gg = GrainSource(float3(pixelCoord, floor(GRAIN_RATE * (t + 9.0))), NoiseStrength, GRAIN_PITCH);
			float bg = GrainSource(float3(pixelCoord, floor(GRAIN_RATE * (t - 9.0))), NoiseStrength, GRAIN_PITCH);

			static const float COLOR_LEVEL = 1.0;
			float3 color_grain = float3(rg, gg, bg);
			grain = lerp(dot(color_grain, float3(0.2126, 0.7152, 0.0722)).xxx, color_grain, COLOR_LEVEL);
		} else {
			// Monochrome film grain
			static const float NEUTRAL_GRAIN_FACTOR = 1.4142135;  // sqrt(2)
			grain = GrainSource(float3(pixelCoord, floor(GRAIN_RATE * t)), NoiseStrength / NEUTRAL_GRAIN_FACTOR, GRAIN_PITCH).xxx;
		}

		color = max(lerp(color * grain, color + (grain - 1.0), GRAIN_LIFT_RATIO), 0.0);
	}

	OutputTexture[DTid.xy] = float4(color, 1.0f);
}

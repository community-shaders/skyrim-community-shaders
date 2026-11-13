#include "PostProcessing/Camera/camera.hlsli"

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

static const float2 DiskKernel[16] = {
	float2(0.176777, 0.000000),
	float2(-0.225772, 0.206826),
	float2(0.034558, -0.393771),
	float2(0.284571, 0.371173),
	float2(-0.522223, -0.092374),
	float2(0.494695, -0.314685),
	float2(-0.165466, 0.615525),
	float2(-0.315561, -0.607594),
	float2(0.684642, 0.250030),
	float2(-0.712256, 0.294009),
	float2(0.343354, -0.733729),
	float2(0.253730, 0.808932),
	float2(-0.764746, -0.443186),
	float2(0.897134, -0.197232),
	float2(-0.547507, 0.778772),
	float2(-0.126487, -0.976090)
};

float3 GrainHash(float3 p)
{
	p = frac(p * float3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return frac((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

float4 main(FullscreenTriangleVSOutput input) : SV_Target
{
	float2 uv = input.Position.xy * CameraScreen.zw;
	float radius = saturate(CameraNormalizedRadius(uv));
	float3 sharp = InputTexture.SampleLevel(ColorSampler, uv, 0).rgb;
	float3 color = sharp;

	float softnessEdge = pow(radius, max(CameraSoftness.z, 0.001));
	float blurAmount = saturate(CameraSoftness.y * softnessEdge);
	[branch] if (CameraSoftness.x > 0.0 && blurAmount > 0.0)
	{
		float rotation = frac(52.9829189 * frac(dot(floor(input.Position.xy), float2(0.06711056, 0.00583715)))) * 6.28318530718;
		float rotationSin, rotationCos;
		sincos(rotation, rotationSin, rotationCos);

		float pixelRadius = CameraSoftness.x * (CameraScreen.y / 720.0) * softnessEdge;
		float3 blurred = 0.0;
		[unroll] for (uint i = 0; i < 16; ++i)
		{
			float2 kernelUV = DiskKernel[i];
			float2 offset = float2(
				kernelUV.x * rotationCos - kernelUV.y * rotationSin,
				kernelUV.x * rotationSin + kernelUV.y * rotationCos);
			blurred += InputTexture.SampleLevel(ColorSampler, uv + offset * pixelRadius * CameraScreen.zw, 0).rgb;
		}
		color = lerp(sharp, blurred * (1.0 / 16.0), blurAmount);
	}

	[branch] if (CameraGrain.z > 0.0)
	{
		float2 grainPixel = floor(input.Position.xy / max(CameraGrain.x, 0.5));
		float frame = fmod((float)SharedData::FrameCount, 4096.0);
		float3 noise = GrainHash(float3(grainPixel, frame));
		float3 grain = lerp(noise.xxx, noise, saturate(CameraGrain.y));

		float luminance = max(Color::RGBToLuminance(color), 0.0);
		float compressedLuminance = luminance / (1.0 + luminance);
		float response = lerp(0.25, 1.0, 4.0 * compressedLuminance * (1.0 - compressedLuminance));
		float3 amplitude = grain * (CameraGrain.z * response);
		color = color * exp2(amplitude) + grain * (CameraGrain.z * response * 0.005);
	}

	return float4(max(color, 0.0), 1.0);
}

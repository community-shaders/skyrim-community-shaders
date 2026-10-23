#include "Common/Color.hlsli"
#include "Common/ColorManagement.hlsli"
#include "Common/SharedData.hlsli"
#include "DynamicCubemaps/CubemapCommon.hlsli"

TextureCube<float4> EnvCaptureTexture : register(t0);
TextureCube<float4> ReflectionsTexture : register(t1);
TextureCube<float4> DefaultCubemap : register(t2);

RWTexture2DArray<float4> EnvInferredTexture : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)] void main(uint3 ThreadID : SV_DispatchThreadID) {
	uint width, height, depth;
	EnvInferredTexture.GetDimensions(width, height, depth);
	float3 uv = GetCubemapSamplingVector(ThreadID, float2(width, height));
	float4 color = EnvCaptureTexture.SampleLevel(LinearSampler, uv, 0);

	float mipLevel = 0.0;

#if !defined(REFLECTIONS)
	float k = 1.5;
	float brightness = k;
#endif

	while (color.w < 1.0 && mipLevel <= 9) {
		mipLevel++;

		float4 tempColor = 0.0;
		if (mipLevel < 9) {
			tempColor = EnvCaptureTexture.SampleLevel(LinearSampler, uv, mipLevel);
		} else {
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(-1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, -1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, -1.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, 1.0), 10);
		}

#if !defined(REFLECTIONS)
		tempColor *= brightness;
		brightness *= k;
#endif

		if ((color.w + tempColor.w) > 1.0) {
			mipLevel -= color.w;
			float alphaDiff = 1.0 - color.w;
			tempColor.xyzw *= alphaDiff / tempColor.w;
			color.xyzw += tempColor;
			break;
		} else {
			color.xyzw += tempColor;
		}
	}

#if defined(REFLECTIONS)
	color.rgb = lerp(color.rgb, ColorManagement::SceneToLinear(ReflectionsTexture.SampleLevel(LinearSampler, uv, 0.0).rgb), saturate(mipLevel / 8.0));
#else
	if (color.a <= 0.0001)
		color.rgb = ColorManagement::SceneToLinear(Color::Ambient(max(0.0, SharedData::GetAmbient(uv))));
	color.rgb = lerp(color.rgb, color.rgb * DefaultCubemap.SampleLevel(LinearSampler, uv, 0.0).xyz, saturate(mipLevel / 8.0));
#endif

	color.rgb = ColorManagement::LinearToScene(color.rgb);
	EnvInferredTexture[ThreadID] = max(0, color);
}

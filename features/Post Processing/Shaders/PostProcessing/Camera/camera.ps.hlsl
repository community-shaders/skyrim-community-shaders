#include "PostProcessing/Camera/camera.hlsli"

float4 main(FullscreenTriangleVSOutput input) : SV_Target
{
	float2 uv = input.Position.xy * CameraScreen.zw;
	float2 aspectScale = CameraAspectScale();
	float2 lensPosition = (uv - 0.5) * aspectScale;
	float cornerRadiusSquared = CameraLens.z * CameraLens.z;
	float radiusSquared = dot(lensPosition, lensPosition) / cornerRadiusSquared;

	float fovScale = tan(radians(clamp(CameraLens.w, 20.0, 160.0)) * 0.5);
	float distortionStrength = clamp(CameraLens.x * fovScale, -0.5, 0.75);
	float distortion = 1.0 / (1.0 - distortionStrength * radiusSquared);
	float edgeCrop = max(1.0, 1.0 / (1.0 - distortionStrength));
	float2 sampleUV = lensPosition * (distortion / edgeCrop) / aspectScale + 0.5;

	float fringe = pow(saturate(CameraNormalizedRadius(uv)), max(CameraAberration.w, 0.001));
	float2 radialDirection = (uv - 0.5) * 2.0;

	[branch] if (any(abs(CameraAberration.xyz) > 0.0))
	{
		float2 redOffset = radialDirection * (CameraAberration.x * fringe * 0.0015);
		float2 greenOffset = radialDirection * (CameraAberration.y * fringe * 0.0015);
		float2 blueOffset = radialDirection * (CameraAberration.z * fringe * 0.0015);
		float3 color;
		color.r = InputTexture.SampleLevel(ColorSampler, sampleUV + redOffset, 0).r;
		color.g = InputTexture.SampleLevel(ColorSampler, sampleUV + greenOffset, 0).g;
		color.b = InputTexture.SampleLevel(ColorSampler, sampleUV + blueOffset, 0).b;
		return float4(color, 1.0);
	}

	return float4(InputTexture.SampleLevel(ColorSampler, sampleUV, 0).rgb, 1.0);
}

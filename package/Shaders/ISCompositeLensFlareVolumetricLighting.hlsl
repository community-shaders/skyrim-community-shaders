#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState VLSourceSampler : register(s0);
SamplerState LFSourceSampler : register(s1);

Texture2D<float4> VLSourceTex : register(t0);
Texture2D<float4> LFSourceTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 VolumetricLightingColor : packoffset(c0);
};

#	define LinearSampler VLSourceSampler
#	include "Common/ShadowSampling.hlsli"

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.0.xxx;

#	if defined(VOLUMETRIC_LIGHTING)
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float volumetricLightingPower = VLSourceTex.Sample(VLSourceSampler, screenPosition).x;
	float3 volumetricLightingColor = VolumetricLightingColor.xyz;
	if (SharedData::enbSettings.Enable) {
		volumetricLightingColor = lerp(volumetricLightingColor, dot(volumetricLightingColor, 1.0 / 3.0), SharedData::enbSettings.VolumetricRaysDesaturation);
		volumetricLightingColor *= SharedData::enbSettings.VolumetricRaysColorFilter;
	}
	color += volumetricLightingColor * Color::VolumetricLighting(volumetricLightingPower.xxx).x;
#	endif

#	if defined(LENS_FLARE)
	float3 lensFlareColor = LFSourceTex.Sample(LFSourceSampler, input.TexCoord).xyz;
	if (SharedData::linearLightingSettings.enableLinearLighting) {
		color += Color::SkyrimGammaToLinear(lensFlareColor);
	} else {
		color += lensFlareColor;
	}
#	endif

	if (SharedData::enbSettings.Enable && SharedData::enbSettings.EnableVolumetricRays) {
		uint eyeIndex = 0;

		float2 uv = input.TexCoord.xy;

		float depth = SharedData::GetDepth(uv);
		float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
		float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
		positionMS.xyz /= positionMS.w;

		float3 viewDirection = normalize(positionMS.xyz);

		float volumetricShadow = ShadowSampling::Get3DFilteredShadowVolumetric(positionMS.xyz, viewDirection, input.Position.xy, eyeIndex, SharedData::enbSettings.VolumetricRaysExtinction);

		float3 ibl = ImageBasedLighting::GetSkyIBL(float3(0, 0, -1));
		ibl = lerp(dot(ibl, 1.0 / 3.0), ibl, 2.0);

		float phase = dot(viewDirection, SharedData::SunColor.xyz) * 0.5 + 0.5;

		color.xyz += volumetricShadow * (SharedData::SunColor.xyz * phase + ibl * SharedData::enbSettings.VolumetricRaysSkyColorAmount) * SharedData::enbSettings.VolumetricRaysIntensity;
	}

	psout.Color = color;

	return psout;
}
#endif

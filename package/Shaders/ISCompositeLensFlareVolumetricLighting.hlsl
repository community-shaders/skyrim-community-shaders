#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color : SV_Target0;
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

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.0.xxx;

#	if defined(VOLUMETRIC_LIGHTING)
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float3 volumetricLightingColor = VolumetricLightingColor.xyz;
	float volumetricLightingPower = VLSourceTex.Sample(VLSourceSampler, screenPosition).x;

	if (SharedData::enbSettings.Enable){
		volumetricLightingColor = lerp(volumetricLightingColor, dot(volumetricLightingColor, 1.0 / 3.0), SharedData::enbSettings.VolumetricRaysDesaturation);
		volumetricLightingColor *= SharedData::enbSettings.VolumetricRaysColorFilter;
		volumetricLightingColor *= SharedData::enbSettings.VolumetricRaysIntensity;

		volumetricLightingPower *= SharedData::enbSettings.VolumetricRaysRangeFactor;
	}

	color = volumetricLightingColor * volumetricLightingPower;
#	endif

#	if defined(LENS_FLARE)
	float3 lensFlareColor = LFSourceTex.Sample(LFSourceSampler, input.TexCoord).xyz;
	color += lensFlareColor;
#	endif

	psout.Color = color;

	return psout;
}
#endif

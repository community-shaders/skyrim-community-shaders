// Spatial GI denoiser: bilateral blur with depth/normal weights (separable 2-pass)
// On the Y pass (g_BlurDirection.y > 0), writes final output to texGIOcclusion

#include "SSRT/GI1Common.hlsli"
#include "SSRT/GIDenoiser.hlsli"

Texture2D<float4> g_InputColor : register(t0);
Texture2D<float> g_DepthTexture : register(t1);
Texture2D<float4> g_NormalsTexture : register(t2);
Texture2D<float> g_BlurMaskTexture : register(t3);

RWTexture2D<float4> g_OutputColor : register(u0);  // X pass: temp, Y pass: final output (texGIOcclusion)

[numthreads(8, 8, 1)] void main(uint2 did
								 : SV_DispatchThreadID)
{
	if (any(did >= uint2(g_BufferDimensions)))
		return;

	float4 lighting = g_InputColor.Load(int3(did, 0));

	// Read blur radius from blur mask
	float blur_mask_value = g_BlurMaskTexture.Load(int3(did, 0)).x;
	int   blur_radius     = int(max(blur_mask_value * kGIDenoiser_MaxBlurMask, 0.0f));

	if (blur_radius > 0)
	{
		float  weight = 1.0f;
		float3 color  = lighting.xyz / max(lighting.w, 1.0f);

		float  center_depth  = toLinearDepth(g_DepthTexture.Load(int3(did, 0)).x, g_NearFar);
		float3 center_normal = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(did, 0)).xy);

		for (int r = -blur_radius; r <= blur_radius; ++r)
		{
			if (r == 0)
				continue;

			int2   tap = clamp(int2(did) + r * g_BlurDirection, 0, int2(g_BufferDimensions) - 1);
			float4 c   = g_InputColor.Load(int3(tap, 0));

			if (c.w > 0.0f)
			{
				float  d = toLinearDepth(g_DepthTexture.Load(int3(tap, 0)).x, g_NearFar);
				float3 n = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(tap, 0)).xy);

				float depth_diff    = 1.0f - (center_depth / d);
				float depth_factor  = exp2(-(lighting.w > 0.0f ? 2e2f : 2e1f) * abs(depth_diff));
				float normal_factor = max(dot(n, center_normal), 0.0f);
				normal_factor *= normal_factor;
				normal_factor *= normal_factor;

				float w = depth_factor * (lighting.w > 0.0f ? normal_factor : 1.0f);

				color  += w * (c.xyz / max(c.w, 1.0f));
				weight += w;
			}
		}

		lighting.xyz = (color / weight) * max(lighting.w, 1.0f);
	}

	// On Y pass, write final AO+GI output
	if (g_BlurDirectionY > 0)
	{
		float3 final_color = lighting.xyz / max(lighting.w, 1.0f);

		// Derive AO from irradiance luminance
		float ao = saturate(exp(-g_AOIntensity * luminance(final_color)));

		g_OutputColor[did] = float4(final_color * g_GIIntensity, ao);
	}
	else
	{
		g_OutputColor[did] = lighting;
	}
}

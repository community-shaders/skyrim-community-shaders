// Temporal GI denoiser: reproject previous frame's accumulated color
// Uses motion vectors, depth/normal consistency, and adaptive history length

#include "SSRT/GI1Common.hlsli"
#include "SSRT/GIDenoiser.hlsli"

Texture2D<float> g_DepthTexture : register(t0);
Texture2D<float4> g_NormalsTexture : register(t1);
Texture2D<float2> g_VelocityTexture : register(t2);
Texture2D<float4> g_PrevDenoisedColor : register(t3);
Texture2D<float> g_PrevColorDelta : register(t4);
Texture2D<float> g_PrevDepthTexture : register(t5);
Texture2D<float4> g_PrevNormalsTexture : register(t6);

RWTexture2D<float4> g_GIDenoiserColor : register(u0);      // accumulated color (read/write)
RWTexture2D<float> g_GIDenoiserBlurMask : register(u1);     // blur mask
RWTexture2D<float> g_GIDenoiserColorDelta : register(u2);   // color delta

[numthreads(8, 8, 1)] void main(uint2 did
								 : SV_DispatchThreadID)
{
	if (any(did >= uint2(g_BufferDimensions)))
		return;

	float4 color       = g_GIDenoiserColor[did];
	float4 lighting    = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float3 raw_normal  = g_NormalsTexture.Load(int3(did, 0)).xyz;

	float  alpha_blend  = 1.0f;
	float  color_delta  = 0.0f;
	float2 uv           = (did + 0.5f) / g_BufferDimensions;
	bool   is_sky_pixel = (dot(raw_normal, raw_normal) == 0.0f);

	if (!is_sky_pixel)
	{
		float2 velocity    = g_VelocityTexture.SampleLevel(g_PointClampSampler, uv, 0.0f).xy;
		float2 previous_uv = (uv - velocity);

		float  depth  = g_DepthTexture.Load(int3(did, 0)).x;
		float3 normal   = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(did, 0)).xy);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normal, false));

		if (all(previous_uv > 0.0f) && all(previous_uv < 1.0f))
		{
			float3 world     = reconstructWorldPosition(uv, depth);
			float  cell_size = distance(g_Eye, world) * g_CellSize;

			cell_size *= lerp(1.0f, 5.0f, pow(1.0f - max(dot(normalize(g_Eye - world), normalWS), 0.0f), 6.0f));

			float  weight     = 0.0f;
			float2 texel_size = 1.0f / g_BufferDimensions;

			for (float y = -1.0f; y <= 1.0f; ++y)
			{
				for (float x = -1.0f; x <= 1.0f; ++x)
				{
					float2 st              = previous_uv + float2(x, y) * texel_size;
					float4 c               = g_PrevDenoisedColor.SampleLevel(g_PointClampSampler, st, 0.0f);
					float3 previous_normal = g_PrevNormalsTexture.SampleLevel(g_PointClampSampler, st, 0.0f).xyz;

					if (c.w < 1.0f || dot(previous_normal, previous_normal) == 0.0f)
						continue;

					previous_normal = GBuffer::DecodeNormal(previous_normal.xy);

					float  previous_depth = g_PrevDepthTexture.SampleLevel(g_PointClampSampler, st, 0.0f).x;
					float3 previous_world = reconstructPrevWorldPosition(st, previous_depth);

					if (distance(world, previous_world) < cell_size && dot(normal, previous_normal) > 0.95f)
					{
						float subpixel_dist = distance(floor(st * g_BufferDimensions) + 0.5f, previous_uv * g_BufferDimensions);
						float w             = saturate(1.0f - subpixel_dist * 0.707107f);

						color_delta += w * g_PrevColorDelta.SampleLevel(g_PointClampSampler, st, 0.0f).x;
						lighting    += w * c;
						weight      += w;
					}
				}
			}

			if (weight > 0.0f)
			{
				float wd = sign(color_delta);
				color_delta  = GIDenoiser_RemoveNaNs(abs(color_delta) / weight);
				color_delta *= wd;

				lighting = GIDenoiser_RemoveNaNs(lighting / weight);
			}

			if (color.w > 0.0f)
			{
				float lumaA = luminance(color.xyz);
				float lumaB = luminance(lighting.xyz / max(lighting.w, 1.0f));

				color_delta = lerp(color_delta, lumaA - lumaB, 1.0f / 8.0f);
				alpha_blend = saturate(1.0f - abs(color_delta) / max(lumaB, 1e-4f));
			}
		}
	}

	float blur_mask = (!is_sky_pixel ? max(kGIDenoiser_MaxBlurMask - lighting.w, 0.0f) : -1.0f);

	if (color.w > 0.0f || lighting.w < 1.0f)
	{
		lighting += float4(color.xyz, color.w > 0.0f ? 1.0f : -1.0f);
	}

	float2 vignette_uv      = uv * (1.0f - uv.yx);
	float  vignette         = pow(15.0f * vignette_uv.x * vignette_uv.y, 0.25f);
	float  max_sample_count = max(lerp(4.0f, 8.0f * min(abs(lighting.w), kGIDenoiser_MaxBlurMask), alpha_blend) * vignette, 1.0f);

	if (lighting.w > max_sample_count)
	{
		lighting *= (max_sample_count / lighting.w);
	}

	g_GIDenoiserBlurMask[did]    = blur_mask / kGIDenoiser_MaxBlurMask;
	g_GIDenoiserColor[did]       = lighting;
	g_GIDenoiserColorDelta[did]  = color_delta;
}

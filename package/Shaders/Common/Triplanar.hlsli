#ifndef TRIPLANAR_HLSLI
#define TRIPLANAR_HLSLI

namespace Triplanar
{
	/// Compute triplanar blend weights from world-space normal.
	float3 GetWeights(float3 normal)
	{
		float3 a = abs(normal);
		float3 w = float3(
			(a.x >= a.y && a.x >= a.z) ? 1.0 : 0.0,
			(a.y > a.x && a.y >= a.z) ? 1.0 : 0.0,
			(a.z > a.x && a.z > a.y) ? 1.0 : 0.0);
		return w;
	}

	/// Sample texture using triplanar projection from world position.
	/// Projects from 3 orthogonal planes (YZ, XZ, XY) and blends by weight.
	float4 Sample(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale)
	{
		return tex.Sample(samp, worldPos.zy * scale) * weights.x +
		       tex.Sample(samp, worldPos.xz * scale) * weights.y +
		       tex.Sample(samp, worldPos.xy * scale) * weights.z;
	}

	/// Sample texture using triplanar projection with mip bias.
	float4 SampleBias(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float bias)
	{
		return tex.SampleBias(samp, worldPos.zy * scale, bias) * weights.x +
		       tex.SampleBias(samp, worldPos.xz * scale, bias) * weights.y +
		       tex.SampleBias(samp, worldPos.xy * scale, bias) * weights.z;
	}
}

#endif  // TRIPLANAR_HLSLI

#ifndef TRIPLANAR_HLSLI
#define TRIPLANAR_HLSLI

namespace Triplanar
{
	static const float DEFAULT_SHARPNESS = 6.0;
	static const float DYNAMIC_SHARPNESS = 8.0;
	static const float DYNAMIC_THRESHOLD = 0.4;  // fraction of max weight below which an axis is suppressed

	/// Compute triplanar blend weights from world-space normal.
	/// Higher sharpness produces sharper transitions between projection planes.
	float3 GetWeights(float3 normal, float sharpness)
	{
		float3 w = pow(abs(normal), sharpness);
		return w / (dot(w, 1.0) + EPSILON_DIVISION);
	}

	float3 GetWeights(float3 normal)
	{
		return GetWeights(normal, DEFAULT_SHARPNESS);
	}

	/// Triplanar weights with threshold suppression to eliminate stretching on rigid surfaces.
	/// Zeroes axes below `threshold` fraction of the dominant axis, then re-normalizes.
	float3 GetWeightsDynamic(float3 normal, float sharpness, float threshold)
	{
		float3 w = GetWeights(normal, sharpness);
		w = max(w - max(w.x, max(w.y, w.z)) * threshold, 0.0);
		return w / (dot(w, 1.0) + EPSILON_DIVISION);
	}

	float3 GetWeightsDynamic(float3 normal)
	{
		return GetWeightsDynamic(normal, DYNAMIC_SHARPNESS, DYNAMIC_THRESHOLD);
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

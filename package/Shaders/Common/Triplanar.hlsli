#ifndef TRIPLANAR_HLSLI
#define TRIPLANAR_HLSLI

namespace Triplanar
{

	/// Compute triplanar blend weights from world-space normal.
	float3 GetWeights(float3 normal)
	{
		float3 w = abs(normal);
		w = pow(w, 4.0); // Sharper transitions for better visual quality
		return w / dot(w, 1.0);
	}

	/// Sample texture using triplanar projection from world position.
	/// Projects from 3 orthogonal planes (YZ, XZ, XY) and blends by weight.
	/// Optimized for Z-up.
	float4 Sample(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale)
	{
		return tex.Sample(samp, worldPos.yz * scale) * weights.x +
		       tex.Sample(samp, worldPos.xz * scale) * weights.y +
		       tex.Sample(samp, worldPos.xy * scale) * weights.z;
	}

	/// Sample texture using triplanar projection with mip bias.
	float4 SampleBias(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float bias)
	{
		return tex.SampleBias(samp, worldPos.yz * scale, bias) * weights.x +
		       tex.SampleBias(samp, worldPos.xz * scale, bias) * weights.y +
		       tex.SampleBias(samp, worldPos.xy * scale, bias) * weights.z;
	}

	/// Sample normal map using triplanar projection from world position.
	/// Correctly reorients normals for each axis (Z-up) and blends by weight.
	float3 SampleNormal(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float3 worldNormal)
	{
		// Sample normal maps for each plane
		float3 nX = tex.Sample(samp, worldPos.yz * scale).xyz * 2.0 - 1.0;
		float3 nY = tex.Sample(samp, worldPos.xz * scale).xyz * 2.0 - 1.0;
		float3 nZ = tex.Sample(samp, worldPos.xy * scale).xyz * 2.0 - 1.0;

		// Correctly swizzle and sign the normals for world space (Z-up)
		// n.z is the depth of the normal map
		float3 wnX = float3(nX.z * (worldNormal.x >= 0 ? 1 : -1), nX.x, nX.y);
		float3 wnY = float3(nY.x, nY.z * (worldNormal.y >= 0 ? 1 : -1), nY.y);
		float3 wnZ = float3(nZ.x, nZ.y, nZ.z * (worldNormal.z >= 0 ? 1 : -1));

		return normalize(wnX * weights.x + wnY * weights.y + wnZ * weights.z);
	}

	/// Sample texture using triplanar projection stochastically from world position.
	float4 SampleStochastic(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float noise)
	{
		float3 dPdx = ddx(worldPos * scale);
		float3 dPdy = ddy(worldPos * scale);

		if (noise < weights.x)
			return tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		else if (noise < weights.x + weights.y)
			return tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		else
			return tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
	}

	/// Sample texture using triplanar projection stochastically with mip bias.
	float4 SampleStochasticBias(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float bias, float noise)
	{
		// Use SampleGrad to avoid boundary artifacts while respecting stochastic selection.
		float3 dPdx = ddx(worldPos * scale);
		float3 dPdy = ddy(worldPos * scale);

		if (noise < weights.x)
			return tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		else if (noise < weights.x + weights.y)
			return tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		else
			return tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
	}

	/// Sample normal map using triplanar projection stochastically.
	float3 SampleNormalStochastic(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float3 worldNormal, float noise)
	{
		float3 dPdx = ddx(worldPos * scale);
		float3 dPdy = ddy(worldPos * scale);

		float3 n;
		if (noise < weights.x) {
			n = tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz).xyz * 2.0 - 1.0;
			n = float3(n.z * (worldNormal.x >= 0 ? 1 : -1), n.x, n.y);
		} else if (noise < weights.x + weights.y) {
			n = tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz).xyz * 2.0 - 1.0;
			n = float3(n.x, n.z * (worldNormal.y >= 0 ? 1 : -1), n.y);
		} else {
			n = tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy).xyz * 2.0 - 1.0;
			n = float3(n.x, n.y, n.z * (worldNormal.z >= 0 ? 1 : -1));
		}

		return normalize(n);
	}
}

#endif  // TRIPLANAR_HLSLI

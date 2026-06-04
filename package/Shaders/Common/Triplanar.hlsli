#ifndef TRIPLANAR_HLSLI
#define TRIPLANAR_HLSLI

namespace Triplanar
{
	static const float BLEND_SHARPNESS = 6.0;  // Power for weight computation; higher = sharper axis transitions
	static const float STRETCH_CUTOFF = 0.4;   // ~cos(66°) — per-axis alignment below this produces visible stretching

	/// Compute triplanar blend weights using face normal mask and smooth vertex normal blend.
	float3 GetWeights(float3 vertexNormal, float3 faceNormal, float sharpness = BLEND_SHARPNESS)
	{
		float3 mask = step(STRETCH_CUTOFF, abs(faceNormal));
		float3 w = pow(abs(vertexNormal), sharpness) * mask;
		return w / (dot(w, 1.0) + EPSILON_DIVISION);
	}

	/// Weighted triplanar sample blending all 3 planes — stable for alpha/fade values.
	float4 Sample(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale)
	{
		return tex.Sample(samp, worldPos.yz * scale) * weights.x +
		       tex.Sample(samp, worldPos.xz * scale) * weights.y +
		       tex.Sample(samp, worldPos.xy * scale) * weights.z;
	}

	/// Compute gradients for stochastic triplanar sampling, pre-computed before branching.
	void ComputeGradients(float3 worldPos, float scale, out float3 dPdx, out float3 dPdy)
	{
		dPdx = ddx(worldPos * scale);
		dPdy = ddy(worldPos * scale);
	}

	/// Stochastic triplanar: select one projection plane via noise, reducing 3 texture reads to 1.
	float4 SampleStochastic(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float noise)
	{
		float3 dPdx = 0.0;
		float3 dPdy = 0.0;
		ComputeGradients(worldPos, scale, dPdx, dPdy);

		float4 result = 0;
		if (noise < weights.x)
			result = tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		else if (noise < weights.x + weights.y)
			result = tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		else
			result = tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
		return result;
	}

	/// Stochastic triplanar with mip bias via gradient scaling.
	float4 SampleStochasticBias(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float bias, float noise)
	{
		float3 dPdx = 0.0;
		float3 dPdy = 0.0;
		ComputeGradients(worldPos, scale, dPdx, dPdy);
		float biasScale = exp2(bias);
		dPdx *= biasScale;
		dPdy *= biasScale;

		float4 result = 0;
		if (noise < weights.x)
			result = tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		else if (noise < weights.x + weights.y)
			result = tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		else
			result = tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
		return result;
	}
}

#endif  // TRIPLANAR_HLSLI

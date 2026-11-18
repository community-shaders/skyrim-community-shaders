#define COMPUTESHADER

struct NoiseLayer
{
	uint noise;
	float frequency;
	float2 offset;
	float4 range;
};
cbuffer Parameters : register(b1)
{
	NoiseLayer weather[5];  // primary, type, secondary, coverage gain, thickness variation
	float localWeight;
	uint localBlendMode;
	float weight;
	uint hasPrevious;
	float4 shape;  // coverage-to-height exponent, displacement / nominal thickness
};
Texture2D<float> Noise0 : register(t0);
Texture2D<float> Noise1 : register(t1);
Texture2D<float> Noise2 : register(t2);
Texture2D<float> Noise3 : register(t3);
Texture2D<float4> LocalWeather : register(t4);
Texture2D<float4> PreviousWeather : register(t5);
SamplerState NoiseSampler : register(s0);
RWTexture2D<float4> OutputWeather : register(u0);

float SampleNoise(Texture2D<float> source, float2 uv, float frequency, uint2 size)
{
	uint2 dimensions;
	source.GetDimensions(dimensions.x, dimensions.y);
	const float2 footprint = float2(dimensions) * abs(frequency) / size;
	return source.SampleLevel(NoiseSampler, uv, log2(max(max(footprint.x, footprint.y), 1.0)));
}

float WeatherValue(NoiseLayer layer, float2 uv, uint2 size)
{
	// Neutral extension fields and constant cloud types need no texture query.
	if (layer.range.z == layer.range.w)
		return layer.range.z;
	float n = 0.0;
	const float2 p = (uv + layer.offset) * layer.frequency;
	switch (layer.noise) {
	case 0u:
		n = SampleNoise(Noise0, p, layer.frequency, size);
		break;
	case 1u:
		n = SampleNoise(Noise1, p, layer.frequency, size);
		break;
	case 2u:
		n = SampleNoise(Noise2, p, layer.frequency, size);
		break;
	case 3u:
		n = SampleNoise(Noise3, p, layer.frequency, size);
		break;
	}
	return lerp(layer.range.z, layer.range.w, saturate((n * 2.0 - 1.0 - layer.range.x) / (layer.range.y - layer.range.x)));
}

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	uint2 size;
	OutputWeather.GetDimensions(size.x, size.y);
	if (any(tid >= size))
		return;
	const float2 uv = (float2(tid) + 0.5) / size;
	float2 controls = float2(WeatherValue(weather[0], uv, size), saturate(WeatherValue(weather[1], uv, size)));
	controls.x = WeatherValue(weather[3], uv, size) * max(controls.x, WeatherValue(weather[2], uv, size));
	if (localWeight > 0.0) {
		uint2 dimensions;
		LocalWeather.GetDimensions(dimensions.x, dimensions.y);
		const float mip = log2(max(max(float(dimensions.x) / size.x, float(dimensions.y) / size.y), 1.0));
		const float3 local = saturate(LocalWeather.SampleLevel(NoiseSampler, uv, mip).rgb);
		controls = localBlendMode == 0u ? lerp(controls, local.rg, local.b * localWeight) : max(controls, local.rg * localWeight);
	}
	const float coverHeight = saturate(pow(saturate(controls.x), shape.x) + WeatherValue(weather[4], uv, size));
	float bottom = 0.2 * (1.0 - coverHeight);
	float top = 0.2 + 0.8 * coverHeight;
	// A collapsed layer has no material and must not pull the geometry of a
	// nonempty scene endpoint toward its zero-thickness bounds during blending.
	float density = coverHeight > 0.0 ? smoothstep(0.15, 0.65, controls.x) : 0.0;
	// Only encode the nominal layer inside its conservative displaced support.
	// The band phase is evaluated in world space, independently of this tile.
	bottom = (bottom + shape.y) / (1.0 + 2.0 * shape.y);
	top = (top + shape.y) / (1.0 + 2.0 * shape.y);
	// Density-weighted geometry keeps fading empty weather from dragging the
	// occupied layer to height zero. Decode after spatial/scene interpolation.
	float4 result = float4(1.0, bottom, top, controls.y) * (density * weight);
	if (hasPrevious != 0u)
		result += PreviousWeather.Load(int3(tid, 0));
	OutputWeather[tid] = result;
}

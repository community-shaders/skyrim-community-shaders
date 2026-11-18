#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

struct NoiseLayer
{
	uint noise;
	float frequency;
	float2 offset;
	float4 range;
};

cbuffer CB : register(b1)
{
	NoiseLayer weather[2];
	float2 windOffset;
	float localWeight;
	float localWindScale;
	uint localBlendMode;
	float weight;
	uint hasPrevious;
	float padding;
};
Texture2D<float> Noise0 : register(t0);
Texture2D<float> Noise1 : register(t1);
Texture2D<float> Noise2 : register(t2);
Texture2D<float> Noise3 : register(t3);
Texture2D<float4> LocalWeather : register(t4);
Texture2D<float2> PreviousWeather : register(t5);
SamplerState NoiseSampler : register(s0);
RWTexture2D<float2> OutputWeather : register(u0);

float SampleNoise(Texture2D<float> source, NoiseLayer layer, float2 uv, uint2 size)
{
	uint2 sourceSize;
	source.GetDimensions(sourceSize.x, sourceSize.y);
	const float2 footprint = float2(sourceSize) * layer.frequency / size;
	const float mip = log2(max(max(footprint.x, footprint.y), 1.0));
	return source.SampleLevel(NoiseSampler, (uv + layer.offset - windOffset * 0.0001) * layer.frequency, mip);
}

float WeatherValue(NoiseLayer layer, float2 uv, uint2 size)
{
	float value = 0.0;
	switch (layer.noise) {
	case 0u:
		value = SampleNoise(Noise0, layer, uv, size);
		break;
	case 1u:
		value = SampleNoise(Noise1, layer, uv, size);
		break;
	case 2u:
		value = SampleNoise(Noise2, layer, uv, size);
		break;
	case 3u:
		value = SampleNoise(Noise3, layer, uv, size);
		break;
	}
	value = value * 2.0 - 1.0;
	return saturate((value - layer.range.x) / (layer.range.y - layer.range.x)) * (layer.range.w - layer.range.z) + layer.range.z;
}

[numthreads(8, 8, 1)] void generateWeather(uint2 tid : SV_DispatchThreadID) {
	uint2 size;
	OutputWeather.GetDimensions(size.x, size.y);
	if (any(tid >= size))
		return;
	const float2 uv = (float2(tid) + 0.5) / size;
	float2 value = float2(WeatherValue(weather[0], uv, size), WeatherValue(weather[1], uv, size));
	[branch] if (localWeight > 0.0)
	{
		uint2 sourceSize;
		LocalWeather.GetDimensions(sourceSize.x, sourceSize.y);
		const float mip = log2(max(max(float(sourceSize.x) / size.x, float(sourceSize.y) / size.y), 1.0));
		const float3 local = LocalWeather.SampleLevel(NoiseSampler, uv - windOffset * (0.0001 * localWindScale), mip).rgb;
		value = localBlendMode == 0u ? lerp(value, local.rg, local.b * localWeight) : max(value, local.rg * localWeight);
	}
	value *= weight;
	[branch] if (hasPrevious != 0u)
		value += PreviousWeather.Load(int3(tid, 0));
	OutputWeather[tid] = value;
}

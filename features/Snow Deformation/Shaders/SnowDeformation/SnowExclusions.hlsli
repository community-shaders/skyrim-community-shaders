#ifndef SNOW_EXCLUSIONS_HLSLI
#define SNOW_EXCLUSIONS_HLSLI

#define MAX_EXCLUSIONS 256

cbuffer ExclusionCB : register(b1)
{
	float4 ExclusionPosRadius[MAX_EXCLUSIONS];
	float4 ExclusionDirExtType[MAX_EXCLUSIONS];
	uint ExclusionCount;
	float3 exclusionPad;
}

float ExclusionNoise(float2 worldXY)
{
	float2 c = worldXY / 24.0;
	float2 i = floor(c);
	float2 f = frac(c);
	f = f * f * (3.0 - 2.0 * f);
	float4 h;
	h.x = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453);
	h.y = frac(sin(dot(i + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
	h.z = frac(sin(dot(i + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
	h.w = frac(sin(dot(i + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
	return lerp(lerp(h.x, h.y, f.x), lerp(h.z, h.w, f.x), f.y);
}

struct ExclusionResult
{
	float Suppress;
	float Melt;
	float Flatten;
};

ExclusionResult EvaluateExclusionAt(uint index, float2 worldXY, float terrain)
{
	ExclusionResult result;
	result.Suppress = 0.0;
	result.Melt = 0.0;
	result.Flatten = 0.0;

	float3 center = ExclusionPosRadius[index].xyz;
	float radius = ExclusionPosRadius[index].w;
	float4 dirExtType = ExclusionDirExtType[index];
	float zBand = dirExtType.w > 2.5 ? 120.0 : 300.0;
	[branch] if (abs(center.z - terrain) < zBand)
	{
		float2 d = worldXY - center.xy;
		float influence = 0.0;
		[branch] if (dirExtType.w > 2.5)
		{
			float u = dot(d, dirExtType.xy);
			float v = dot(d, float2(-dirExtType.y, dirExtType.x));
			float e = max(abs(u) / max(dirExtType.z, 1.0), abs(v) / max(radius, 1.0));
			influence = 1.0 - smoothstep(0.85, 1.0, e);
			result.Suppress = influence;
		}
		else if (dirExtType.w < 0.5)
		{
			float u = dot(d, dirExtType.xy);
			float v = dot(d, float2(-dirExtType.y, dirExtType.x));
			float a = radius + dirExtType.z;
			float b = radius * 0.85;
			float e = sqrt((u * u) / (a * a) + (v * v) / (b * b));
			e /= 0.8 + 0.4 * ExclusionNoise(worldXY);
			influence = 1.0 - smoothstep(0.45, 1.0, e);
			result.Suppress = influence;
		}
		else
		{
			float2 dEff = d;
			float axisLen = length(dirExtType.xy);
			[branch] if (axisLen > 0.001)
			{
				float2 axisDir = dirExtType.xy / axisLen;
				dEff -= axisDir * dot(d, axisDir) * (axisLen / (1.0 + axisLen));
			}
			float noisy = 1.0;
			[branch] if (dirExtType.w < 1.5)
			{
				noisy = 0.7 + 0.35 * ExclusionNoise(worldXY) + 0.35 * ExclusionNoise(worldXY * 0.3);
			}
			float noisyRadius = radius * noisy;
			float dist = length(dEff);
			influence = (1.0 - smoothstep(noisyRadius * 0.35, noisyRadius, dist)) * dirExtType.z;
			result.Melt = influence;
		}
		result.Flatten = influence;
	}

	return result;
}

float ExclusionReach(float4 posRadius, float4 dirExtType)
{
	return 1.4 * max(posRadius.w, 1.0) * (1.0 + length(dirExtType.xy)) + 1.2 * max(abs(dirExtType.z), 1.0);
}

#endif

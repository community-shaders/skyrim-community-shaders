#define CSHADER

#include "ProceduralGrass/PGrassCommon.hlsli"

// Resample the Low tier worldspace grass id map into the density field used for canopy darkening.
Texture2D<uint> GrassPresenceTexture : register(t0);  // Worldspace grass id per sample, zero is bare
RWTexture2D<uint> GrassDensityTexture : register(u0);

static const float PRESENCE_FILTER_RADIUS = 4.0f;

float LoadPresenceCoverage(int2 sample, int dimension)
{
	if (any(sample < 0) || any(sample >= dimension))
		return 0.0f;

	return GrassPresenceTexture[sample] != 0 ? 1.0f : 0.0f;
}

float SamplePresenceCoverage(float2 coordinate, int dimension)
{
	int2 lowerSample = int2(floor(coordinate));
	float2 fraction = coordinate - float2(lowerSample);
	float lowerLeft = LoadPresenceCoverage(lowerSample, dimension);
	float lowerRight = LoadPresenceCoverage(lowerSample + int2(1, 0), dimension);
	float upperLeft = LoadPresenceCoverage(lowerSample + int2(0, 1), dimension);
	float upperRight = LoadPresenceCoverage(lowerSample + int2(1, 1), dimension);

	return lerp(lerp(lowerLeft, lowerRight, fraction.x), lerp(upperLeft, upperRight, fraction.x), fraction.y);
}

float FilterPresenceCoverage(float2 coordinate, int dimension, float centreCoverage)
{
	const float center = centreCoverage * 4.0f;
	const float cardinal =
		SamplePresenceCoverage(coordinate + float2(PRESENCE_FILTER_RADIUS, 0.0f), dimension) +
		SamplePresenceCoverage(coordinate - float2(PRESENCE_FILTER_RADIUS, 0.0f), dimension) +
		SamplePresenceCoverage(coordinate + float2(0.0f, PRESENCE_FILTER_RADIUS), dimension) +
		SamplePresenceCoverage(coordinate - float2(0.0f, PRESENCE_FILTER_RADIUS), dimension);
	const float diagonal =
		SamplePresenceCoverage(coordinate + float2(PRESENCE_FILTER_RADIUS, PRESENCE_FILTER_RADIUS), dimension) +
		SamplePresenceCoverage(coordinate + float2(PRESENCE_FILTER_RADIUS, -PRESENCE_FILTER_RADIUS), dimension) +
		SamplePresenceCoverage(coordinate + float2(-PRESENCE_FILTER_RADIUS, PRESENCE_FILTER_RADIUS), dimension) +
		SamplePresenceCoverage(coordinate - float2(PRESENCE_FILTER_RADIUS, PRESENCE_FILTER_RADIUS), dimension);

	return (center + cardinal * 2.0f + diagonal) * (1.0f / 16.0f);
}

float QuinticSmoothstep(float value)
{
	float value2 = value * value;
	return value2 * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}

[numthreads(8, 8, 1)] void main(uint3 threadID : SV_DispatchThreadID) {
	uint densityDimension = (uint)grassAOParams.x;
	if (any(threadID.xy >= densityDimension))
		return;

	float2 densityTexelUV = (float2(threadID.xy) + 0.5f) / densityDimension;
	float2 densityWorldPosition = occlusionParams.xy + (densityTexelUV - 0.5f) * (occlusionHalfExtent * 2.0f);

	int presenceDimension = (int)grassPresenceParams.w;
	float2 presenceCoordinate = (densityWorldPosition - grassPresenceParams.xy) * grassPresenceParams.z;

	// Only use the centre sample when all of its neighbouring map samples exist. Missing neighbours count as bare ground, so grass density fades out at map boundaries.
	bool hasCentreCoverage = all(presenceCoordinate >= 0.0f) && all(presenceCoordinate < float(presenceDimension - 1));
	float centreCoverage = hasCentreCoverage ? SamplePresenceCoverage(presenceCoordinate, presenceDimension) : 0.0f;
	float filteredCoverage = FilterPresenceCoverage(presenceCoordinate, presenceDimension, centreCoverage);

	// Inset the filtered boundary so the fade finishes inside the grass-covered area.
	float edgeFade = QuinticSmoothstep(saturate((filteredCoverage - 0.75f) * 4.0f));
	float densityCoverage = centreCoverage * edgeFade;

	GrassDensityTexture[threadID.xy] = (uint)(densityCoverage * grassAOParams.z + 0.5f);
}

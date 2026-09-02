// Physical Glare — animated tear-film phase applied to a cached aperture

Texture2D<float2> TexApertureBase : register(t0);
RWTexture2D<float2> RWTexAperture : register(u0);

cbuffer GlareCB : register(b1)
{
	float Threshold;
	float Intensity;
	float ScatterStrength;
	uint ApertureMode;
	int ApertureBlades;
	float ApertureRotation;
	float AdaptSpeed;
	float DeltaTime;
	uint FFTResolution;
	float PaddingRatio;
	float ScreenWidth;
	float ScreenHeight;
	uint ChannelIndex;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;
	float PSFSharpness;
	float PSFNoiseFloor;
	uint EnableEyelashes;
	float EyelashCurvature;
	uint EyelashCount;
	float EyelashLength;
	uint ParticleCount;
	float ParticleSize;
	uint GratingCount;
	float GratingStrength;
	float TearFilmStrength;
	float TearFilmSpeed;
	uint TearFilmComplexity;
	float TearFilmTime;
	uint SutureBranches;
	float SutureStrength;
};

static const float PI = 3.14159265358979323846;

float HashToFloat(uint seed)
{
	seed = (seed ^ 61u) ^ (seed >> 16u);
	seed *= 9u;
	seed ^= seed >> 4u;
	seed *= 0x27d4eb2du;
	seed ^= seed >> 15u;
	return float(seed) / 4294967295.0;
}

[numthreads(8, 8, 1)] void CS_TearFilm(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float2 center = float2(FFTResolution, FFTResolution) * 0.5;
	float aspect = ScreenWidth / max(ScreenHeight, 1.0);
	float radius = float(FFTResolution) * ApertureSize;
	float2 pos = float2(tid) + 0.5 - center;
	pos.y *= aspect;

	float r = length(pos);
	float theta = atan2(pos.y, pos.x);
	float edgeFactor = smoothstep(radius * 0.55, radius * 0.95, r);
	float phaseOffset = 0.0;
	uint complexity = min(TearFilmComplexity, 16u);
	for (uint h = 0; h < complexity; h++) {
		float amp = HashToFloat(h * 3u + 7000u) * 0.6 + 0.4;
		float angularFreq = float(h + 2);
		float timeSpeed = (HashToFloat(h * 3u + 7001u) * 1.6 + 0.2) * TearFilmSpeed;
		float timePhase = HashToFloat(h * 3u + 7002u) * 2.0 * PI;
		phaseOffset += amp * sin(angularFreq * theta + timeSpeed * TearFilmTime + timePhase);
	}
	phaseOffset *= TearFilmStrength * edgeFactor * 2.5;

	float s, c;
	sincos(phaseOffset, s, c);
	float2 value = TexApertureBase[tid];
	RWTexAperture[tid] = float2(value.x * c - value.y * s, value.x * s + value.y * c);
}

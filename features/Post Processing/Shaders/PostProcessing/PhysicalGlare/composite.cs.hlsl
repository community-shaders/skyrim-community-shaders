// Physical Glare — Scene composite with energy conservation
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Reconstructs per-channel glare from IFFT results, upsamples from FFT
// resolution to screen resolution via Catmull-Rom bicubic interpolation [4],
// and composites onto the original scene.
//
// Energy conservation: the bright threshold contribution is subtracted from
// the scene and replaced by the convolved (spread) version.  At Intensity=1.0
// total luminous energy is redistributed, not added.
//
// Only the center region [P, 1−P) of the IFFT output is read, corresponding
// to the non-padded scene area (P = PaddingRatio) [1, section 2.5].
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.
//   [4] Jimenez (2012), Filmic SMAA, SIGGRAPH — bicubic via HW bilinear.

Texture2D<float4> TexScene : register(t0);     // Original scene (full resolution)
Texture2D<float4> TexIFFT_RGB : register(t1);  // Packed RGB real components (FFT resolution)

RWTexture2D<float4> RWTexOutput : register(u0);  // Final composited output (full resolution)

SamplerState LinearSampler : register(s0);

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

	// (remaining fields omitted — only needed by aperture/PSF shaders)
	// Row 5-10: eye/lens mode params (not needed here)
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
	float SutureWidth;
	uint StarburstCount;
	float StarburstStrength;
	float StarburstIrregularity;
	uint DustCount;
	float DustSize;
	uint BladeRoughnessFreq;
	float BladeRoughnessAmp;
	uint ScratchCount;
	float ScratchOpacity;
	float ScratchLength;
	float ScratchWidth;
	float SphericalAberration;
	uint UseAP1;
	float KernelScale;
	float _pad11;
};

groupshared float3 GlareTile[256];

float4 CatmullRomWeights(float fraction)
{
	float w0 = fraction * (-0.5 + fraction * (1.0 - 0.5 * fraction));
	float w1 = 1.0 + fraction * fraction * (-2.5 + 1.5 * fraction);
	float w2 = fraction * (0.5 + fraction * (2.0 - 1.5 * fraction));
	float w3 = fraction * fraction * (-0.5 + 0.5 * fraction);
	return float4(w0, w1, w2, w3);
}

float3 LoadGlare(int2 position)
{
	return TexIFFT_RGB.Load(int3(clamp(position, 0, int(FFTResolution) - 1), 0)).rgb;
}

[numthreads(8, 8, 1)] void CS_Composite(
	uint2 tid : SV_DispatchThreadID,
	uint2 groupID : SV_GroupID,
	uint groupIndex : SV_GroupIndex) {
	float2 screenSize = float2(ScreenWidth, ScreenHeight);
	float sceneScale = 1.0 - 2.0 * PaddingRatio;
	uint2 firstPixel = groupID * 8;
	float2 firstUV = (float2(firstPixel) + 0.5) / screenSize;
	float2 lastUV = (min(float2(firstPixel + 7), screenSize - 1.0) + 0.5) / screenSize;
	int2 tileStart = int2(floor((firstUV * sceneScale + PaddingRatio) * float(FFTResolution) - 0.5)) - 1;
	int2 tileEnd = int2(floor((lastUV * sceneScale + PaddingRatio) * float(FFTResolution) - 0.5)) + 2;
	uint2 tileSize = uint2(tileEnd - tileStart + 1);
	bool cached = all(tileSize <= 16);

	[branch] if (cached)
	{
		for (uint index = groupIndex; index < tileSize.x * tileSize.y; index += 64) {
			int2 position = tileStart + int2(index % tileSize.x, index / tileSize.x);
			GlareTile[index] = LoadGlare(position);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (tid.x >= (uint)ScreenWidth || tid.y >= (uint)ScreenHeight)
		return;

	float3 scene = TexScene[tid].rgb;

	// Map screen UV [0,1] → IFFT UV [P, 1-P], reading only the centre
	// region that contained the original scene within the zero-padded
	// texture [1, section 2.5].
	float2 uv = (float2(tid) + 0.5) / screenSize;
	float2 samplePos = (uv * sceneScale + PaddingRatio) * float(FFTResolution) - 0.5;
	int2 base = int2(floor(samplePos));
	float2 fraction = samplePos - float2(base);
	float4 weightsX = CatmullRomWeights(fraction.x);
	float4 weightsY = CatmullRomWeights(fraction.y);
	int2 offset = base - 1 - tileStart;
	float3 glare = 0;

	[branch] if (cached)
	{
		[unroll] for (uint row = 0; row < 4; ++row)
		{
			float3 filteredRow = 0;
			[unroll] for (uint column = 0; column < 4; ++column)
				filteredRow += GlareTile[(offset.y + row) * tileSize.x + offset.x + column] * weightsX[column];
			glare += filteredRow * weightsY[row];
		}
	}
	else
	{
		[unroll] for (uint row = 0; row < 4; ++row)
		{
			float3 filteredRow = 0;
			[unroll] for (uint column = 0; column < 4; ++column)
				filteredRow += LoadGlare(base - 1 + int2(column, row)) * weightsX[column];
			glare += filteredRow * weightsY[row];
		}
	}
	glare = max(0, glare);

	// Sanitize extreme values
	glare = min(glare, 65000.0);

	// Energy-conserving glare contribution:
	// Subtract the thresholded bright component and output the convolved
	// (spread) result.  At Intensity = 1 total energy is redistributed,
	// not gained.  Values != 1 allow artistic attenuation or exaggeration.
	// This output is combined with the scene in the Composite pass.
	float3 bright = max(0, scene - Threshold);
	float3 output = (glare - bright) * Intensity;

	// Clamp to prevent negative values at Intensity > 1.0
	output = max(output, 0);

	RWTexOutput[tid] = float4(output, 1.0);
}

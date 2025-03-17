#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

namespace Glints
{
	Texture2D<float4> Glint2023NoiseMap : register(t20);

	//=======================================================================================
	// TOOLS
	//=======================================================================================
	float erfinv(float x)
	{
		float w, p;
		w = -log((1.0 - x) * (1.0 + x));
		if (w < 5.000000) {
			w = w - 2.500000;
			p = 2.81022636e-08;
			p = 3.43273939e-07 + p * w;
			p = -3.5233877e-06 + p * w;
			p = -4.39150654e-06 + p * w;
			p = 0.00021858087 + p * w;
			p = -0.00125372503 + p * w;
			p = -0.00417768164 + p * w;
			p = 0.246640727 + p * w;
			p = 1.50140941 + p * w;
		} else {
			w = sqrt(w) - 3.000000;
			p = -0.000200214257;
			p = 0.000100950558 + p * w;
			p = 0.00134934322 + p * w;
			p = -0.00367342844 + p * w;
			p = 0.00573950773 + p * w;
			p = -0.0076224613 + p * w;
			p = 0.00943887047 + p * w;
			p = 1.00167406 + p * w;
			p = 2.83297682 + p * w;
		}
		return p * x;
	}

	float sampleNormalDistribution(float u, float mu, float sigma)
	{
		//return mu + sigma * (sqrt(-2.0 * log(u.x))* cos(2.0 * pi * u.y));
		float x = sigma * 1.414213f * erfinv(2.0 * u - 1.0) + mu;
		return x;
	}

	float3 sampleNormalDistribution(float3 u, float mu, float sigma)
	{
		//return mu + sigma * (sqrt(-2.0 * log(u.x))* cos(2.0 * pi * u.y));
		float x0 = sigma * 1.414213f * erfinv(2.0 * u.x - 1.0) + mu;
		float x1 = sigma * 1.414213f * erfinv(2.0 * u.y - 1.0) + mu;
		float x2 = sigma * 1.414213f * erfinv(2.0 * u.z - 1.0) + mu;
		return float3(x0, x1, x2);
	}

	float4 sampleNormalDistribution(float4 u, float mu, float sigma)
	{
		//return mu + sigma * (sqrt(-2.0 * log(u.x))* cos(2.0 * pi * u.y));
		float x0 = sigma * 1.414213f * erfinv(2.0 * u.x - 1.0) + mu;
		float x1 = sigma * 1.414213f * erfinv(2.0 * u.y - 1.0) + mu;
		float x2 = sigma * 1.414213f * erfinv(2.0 * u.z - 1.0) + mu;
		float x3 = sigma * 1.414213f * erfinv(2.0 * u.w - 1.0) + mu;
		return float4(x0, x1, x2, x3);
	}

	float HashWithoutSine13(float3 p3)
	{
		p3 = frac(p3 * .1031);
		p3 += dot(p3, p3.yzx + 33.33);
		return frac((p3.x + p3.y) * p3.z);
	}

	float2x2 Inverse(float2x2 A)
	{
		return float2x2(A[1][1], -A[0][1], -A[1][0], A[0][0]) / determinant(A);
	}

	void GetGradientEllipse(float2 duvdx, float2 duvdy, out float2 ellipseMajor, out float2 ellipseMinor)
	{
		float2x2 J = float2x2(duvdx, duvdy);
		J = Inverse(J);
		J = mul(J, transpose(J));

		float a = J[0][0];
		float b = J[0][1];
		float c = J[1][0];
		float d = J[1][1];

		float T = a + d;
		float D = a * d - b * c;
		float SQ = sqrt(abs(T * T / 3.99999 - D));
		float L1 = T / 2.0 - SQ;
		float L2 = T / 2.0 + SQ;

		float2 A0 = float2(L1 - d, c);
		float2 A1 = float2(L2 - d, c);
		float r0 = rsqrt(L1);
		float r1 = rsqrt(L2);
		ellipseMajor = normalize(A0) * r0;
		ellipseMinor = normalize(A1) * r1;
	}

	float2 RotateUV(float2 uv, float rotation, float2 mid)
	{
		float2 rel_uv = uv - mid;
		float2 sincos_rot;
		sincos(rotation, sincos_rot.y, sincos_rot.x);
		return float2(
			sincos_rot.x * rel_uv.x + sincos_rot.y * rel_uv.y + mid.x,
			sincos_rot.x * rel_uv.y - sincos_rot.y * rel_uv.x + mid.y);
	}

	float BilinearLerp(float4 values, float2 valuesLerp)
	{
		// Values XY = float4(00, 01, 10, 11)
		float resultX = lerp(values.x, values.z, valuesLerp.x);
		float resultY = lerp(values.y, values.w, valuesLerp.x);
		float result = lerp(resultX, resultY, valuesLerp.y);
		return result;
	}

	float4 BilinearLerpParallel4(float4 values00, float4 values01, float4 values10, float4 values11, float4 valuesLerpX, float4 valuesLerpY)
	{
		// Values XY = float4(00, 01, 10, 11)
		float4 resultX = lerp(values00, values10, valuesLerpX);
		float4 resultY = lerp(values01, values11, valuesLerpX);
		float4 result = lerp(resultX, resultY, valuesLerpY);
		return result;
	}

	float Remap(float s, float a1, float a2, float b1, float b2)
	{
		return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
	}

	float Remap01To(float s, float b1, float b2)
	{
		return b1 + s * (b2 - b1);
	}

	float RemapTo01(float s, float a1, float a2)
	{
		return (s - a1) / (a2 - a1);
	}

	float4 RemapTo01(float4 s, float4 a1, float4 a2)
	{
		return (s - a1) / (a2 - a1);
	}

	float3 GetBarycentricWeights(float2 p, float2 a, float2 b, float2 c)
	{
		/*float2 v0 = b - a;
		float2 v1 = c - a;
		float2 v2 = p - a;
		float d00 = dot(v0, v0);
		float d01 = dot(v0, v1);
		float d11 = dot(v1, v1);
		float d20 = dot(v2, v0);
		float d21 = dot(v2, v1);
		float denom = d00 * d11 - d01 * d01;
		float v = (d11 * d20 - d01 * d21) / denom;
		float w = (d00 * d21 - d01 * d20) / denom;
		float u = 1.0 - v - w;
		return float3(u, v, w);*/

		float2 v0 = b - a;
		float2 v1 = c - a;
		float2 v2 = p - a;
		float den = v0.x * v1.y - v1.x * v0.y;
		float rcpDen = rcp(den);
		float v = (v2.x * v1.y - v1.x * v2.y) * rcpDen;
		float w = (v0.x * v2.y - v2.x * v0.y) * rcpDen;
		float u = 1.0f - v - w;
		return float3(u, v, w);
	}

	float4 GetBarycentricWeightsTetrahedron(float3 p, float3 v1, float3 v2, float3 v3, float3 v4)
	{
		float3 c11 = v1 - v4, c21 = v2 - v4, c31 = v3 - v4, c41 = v4 - p;

		float2 m1 = c31.yz / c31.x;
		float2 c12 = c11.yz - c11.x * m1, c22 = c21.yz - c21.x * m1, c32 = c41.yz - c41.x * m1;

		float4 uvwk = 0.0.rrrr;
		float m2 = c22.y / c22.x;
		uvwk.x = (c32.x * m2 - c32.y) / (c12.y - c12.x * m2);
		uvwk.y = -(c32.x + c12.x * uvwk.x) / c22.x;
		uvwk.z = -(c41.x + c21.x * uvwk.y + c11.x * uvwk.x) / c31.x;
		uvwk.w = 1.0 - uvwk.z - uvwk.y - uvwk.x;

		return uvwk;
	}

	void UnpackFloat(float input, out float a, out float b)
	{
		uint uintInput = asuint(input);
		a = f16tof32(uintInput >> 16);
		b = f16tof32(uintInput);
	}

	void UnpackFloatParallel4(float4 input, out float4 a, out float4 b)
	{
		uint4 uintInput = asuint(input);
		a = f16tof32(uintInput >> 16);
		b = f16tof32(uintInput);
	}

	//=======================================================================================
	// GLINTS TEST NOVEMBER 2022
	//=======================================================================================

	struct GlintCachedVars
	{
		float2 uv;
		uint gridSeed;
		float footprintArea;
	};

	void CustomRand4Texture(float microfacetRoughness, float2 slope, float2 slopeRandOffset, out float4 outUniform, out float4 outGaussian, out float2 slopeLerp)
	{
		uint2 size = 128;
		float2 slope2 = abs(slope) / microfacetRoughness;
		slope2 = slope2 + (slopeRandOffset * size);
		slopeLerp = frac(slope2);
		uint2 slopeCoord = uint2(floor(slope2)) % size;

		float4 packedRead = Glint2023NoiseMap[slopeCoord];
		UnpackFloatParallel4(packedRead, outUniform, outGaussian);
	}

	float GenerateAngularBinomialValueForSurfaceCell(float4 randB, float4 randG, float2 slopeLerp, float footprintOneHitProba, float binomialSmoothWidth, float footprintMean, float footprintSTD, float microfacetCount)
	{
		float4 gating;
		if (binomialSmoothWidth > 0.0000001)
			gating = saturate(RemapTo01(randB, footprintOneHitProba + binomialSmoothWidth, footprintOneHitProba - binomialSmoothWidth));
		else
			gating = randB < footprintOneHitProba;

		float4 gauss = randG * footprintSTD + footprintMean;
		gauss = clamp(floor(gauss), 0, microfacetCount);
		float4 results = gating * (1.0 + gauss);
		float result = BilinearLerp(results, slopeLerp);
		return result;
	}

	float SampleGlintGridSimplex(float noise, float logDensity, float roughness, float densityRandomization, GlintCachedVars vars, float2 slope, float targetNDF)
	{
		// Get surface space glint simplex grid cell
		const float2x2 gridToSkewedGrid = float2x2(1.0, -0.57735027, 0.0, 1.15470054);
		float2 skewedCoord = mul(gridToSkewedGrid, vars.uv);
		int2 baseId = int2(floor(skewedCoord));
		float3 temp = float3(frac(skewedCoord), 0.0);
		temp.z = 1.0 - temp.x - temp.y;
		float s = step(0.0, -temp.z);
		float s2 = 2.0 * s - 1.0;
		int2 glint0 = baseId + int2(s, s);
		int2 glint1 = baseId + int2(s, 1.0 - s);
		int2 glint2 = baseId + int2(1.0 - s, s);
		float3 barycentrics = float3(-temp.z * s2, s - temp.y * s2, s - temp.x * s2);

		// Generate per surface cell random number to pick sample
		int selectedSample = 0;

		{
			float2 accumWeights = barycentrics;
			accumWeights.y += accumWeights.x;

			if (noise < accumWeights.x)
				selectedSample = 0;
			else if (noise < accumWeights.y)
				selectedSample = 1;
			else
				selectedSample = 2;
		}

		int2 selectedGlint = (selectedSample == 0) ? glint0 : (selectedSample == 1) ? glint1 :
		                                                                              glint2;
		float3 randSelected = Random::pcg3d(uint3(selectedGlint + 2147483648, vars.gridSeed)) / 4294967296.0;

		// Get per surface cell per slope cell random numbers
		float4 randSlopesB, randSlopesG;
		float2 slopeLerp;
		CustomRand4Texture(roughness, slope, randSelected.yz, randSlopesB, randSlopesG, slopeLerp);

		// Compute microfacet count with randomization
		float logDensityRand = clamp(sampleNormalDistribution(float(randSelected.x), logDensity.r, densityRandomization), 0.0, 50.0);
		float microfacetCount = max(1e-8, vars.footprintArea.r * exp(logDensityRand));

		// Compute binomial properties
		float hitProba = roughness * targetNDF;
		float footprintOneHitProba = (1.0 - pow(abs(1.0 - hitProba), microfacetCount));
		float footprintMean = (microfacetCount - 1.0) * hitProba;
		float footprintSTD = sqrt((microfacetCount - 1.0) * hitProba * (1.0 - hitProba));
		float binomialSmoothWidth = 0.1 * clamp(footprintOneHitProba * 10, 0.0, 1.0) * clamp((1.0 - footprintOneHitProba) * 10, 0.0, 1.0);

		// Generate numbers of reflecting microfacets
		float result = GenerateAngularBinomialValueForSurfaceCell(randSlopesB, randSlopesG, slopeLerp, footprintOneHitProba, binomialSmoothWidth, footprintMean, footprintSTD, microfacetCount);
		return result / microfacetCount;
	}

	void GetAnisoCorrectingGridTetrahedron(bool centerSpecialCase, inout float thetaBinLerp, float ratioLerp, float lodLerp, out float3 p0, out float3 p1, out float3 p2, out float3 p3)
	{
		[branch] if (centerSpecialCase == true)  // SPECIAL CASE (no anisotropy, center of blending pattern, different triangulation)
		{
			float3 a = float3(0, 1, 0);
			float3 b = float3(0, 0, 0);
			float3 c = float3(1, 1, 0);
			float3 d = float3(0, 1, 1);
			float3 e = float3(0, 0, 1);
			float3 f = float3(1, 1, 1);
			[branch] if (lodLerp > 1.0 - ratioLerp)  // Upper pyramid
			{
				[branch] if (RemapTo01(lodLerp, 1.0 - ratioLerp, 1.0) > thetaBinLerp)  // Left-up tetrahedron (a, e, d, f)
				{
					p0 = a;
					p1 = e;
					p2 = d;
					p3 = f;
				}
				else  // Right-down tetrahedron (f, e, c, a)
				{
					p0 = f;
					p1 = e;
					p2 = c;
					p3 = a;
				}
			}
			else  // Lower tetrahedron (b, a, c, e)
			{
				p0 = b;
				p1 = a;
				p2 = c;
				p3 = e;
			}
		}
		else  // NORMAL CASE
		{
			float3 a = float3(0, 1, 0);
			float3 b = float3(0, 0, 0);
			float3 c = float3(0.5, 1, 0);
			float3 d = float3(1, 0, 0);
			float3 e = float3(1, 1, 0);
			float3 f = float3(0, 1, 1);
			float3 g = float3(0, 0, 1);
			float3 h = float3(0.5, 1, 1);
			float3 i = float3(1, 0, 1);
			float3 j = float3(1, 1, 1);
			[branch] if (thetaBinLerp < 0.5 && thetaBinLerp * 2.0 < ratioLerp)  // Prism A
			{
				[branch] if (lodLerp > 1.0 - ratioLerp)  // Upper pyramid
				{
					[branch] if (RemapTo01(lodLerp, 1.0 - ratioLerp, 1.0) > RemapTo01(thetaBinLerp * 2.0, 0.0, ratioLerp))  // Left-up tetrahedron (a, f, h, g)
					{
						p0 = a;
						p1 = f;
						p2 = h;
						p3 = g;
					}
					else  // Right-down tetrahedron (c, a, h, g)
					{
						p0 = c;
						p1 = a;
						p2 = h;
						p3 = g;
					}
				}
				else  // Lower tetrahedron (b, a, c, g)
				{
					p0 = b;
					p1 = a;
					p2 = c;
					p3 = g;
				}
			}
			else if (1.0 - ((thetaBinLerp - 0.5) * 2.0) > ratioLerp)  // Prism B
			{
				[branch] if (lodLerp < 1.0 - ratioLerp)  // Lower pyramid
				{
					[branch] if (RemapTo01(lodLerp, 0.0, 1.0 - ratioLerp) > RemapTo01(thetaBinLerp, 0.5 - (1.0 - ratioLerp) * 0.5, 0.5 + (1.0 - ratioLerp) * 0.5))  // Left-up tetrahedron (b, g, i, c)
					{
						p0 = b;
						p1 = g;
						p2 = i;
						p3 = c;
					}
					else  // Right-down tetrahedron (d, b, c, i)
					{
						p0 = d;
						p1 = b;
						p2 = c;
						p3 = i;
					}
				}
				else  // Upper tetrahedron (c, g, h, i)
				{
					p0 = c;
					p1 = g;
					p2 = h;
					p3 = i;
				}
			}
			else  // Prism C
			{
				[branch] if (lodLerp > 1.0 - ratioLerp)  // Upper pyramid
				{
					[branch] if (RemapTo01(lodLerp, 1.0 - ratioLerp, 1.0) > RemapTo01((thetaBinLerp - 0.5) * 2.0, 1.0 - ratioLerp, 1.0))  // Left-up tetrahedron (c, j, h, i)
					{
						p0 = c;
						p1 = j;
						p2 = h;
						p3 = i;
					}
					else  // Right-down tetrahedron (e, i, c, j)
					{
						p0 = e;
						p1 = i;
						p2 = c;
						p3 = j;
					}
				}
				else  // Lower tetrahedron (d, e, c, i)
				{
					p0 = d;
					p1 = e;
					p2 = c;
					p3 = i;
				}
			}
		}

		return;
	}

	void PrecomputeGlints(float rnd, float2 uv, float2 duvdx, float2 duvdy, float screenSpaceScale, out GlintCachedVars vars)
	{
		// ACCURATE PIXEL FOOTPRINT ELLIPSE
		float2 ellipseMajor, ellipseMinor;
		GetGradientEllipse(duvdx, duvdy, ellipseMajor, ellipseMinor);
		float ellipseRatio = length(ellipseMajor) / (length(ellipseMinor) + 1e-8);

		// SHARED GLINT NDF VALUES
		float halfScreenSpaceScaler = screenSpaceScale * 0.5;
		float footprintArea = length(ellipseMajor) * halfScreenSpaceScaler * length(ellipseMinor) * halfScreenSpaceScaler * 4.0;

		// MANUAL LOD COMPENSATION
		float lod = log2(length(ellipseMinor) * halfScreenSpaceScaler);
		float lod0 = (int)lod;  //lod >= 0.0 ? (int)(lod) : (int)(lod - 1.0);
		float lod1 = lod0 + 1;
		float divLod0 = pow(2.0, lod0);
		float divLod1 = pow(2.0, lod1);
		float lodLerp = frac(lod);
		float footprintAreaLOD0 = exp2(2.0 * lod0);
		float footprintAreaLOD1 = exp2(2.0 * lod1);

		// MANUAL ANISOTROPY RATIO COMPENSATION
		float ratio0 = max(pow(2.0, (int)log2(ellipseRatio)), 1.0);
		float ratio1 = ratio0 * 2.0;
		float ratioLerp = saturate(Remap(ellipseRatio, ratio0, ratio1, 0.0, 1.0));

		// MANUAL ANISOTROPY ROTATION COMPENSATION
		float2 v1 = float2(0.0, 1.0);
		float2 v2 = normalize(ellipseMajor);
		float theta = atan2(v1.x * v2.y - v1.y * v2.x, v1.x * v2.x + v1.y * v2.y);
		float thetaGrid = Math::HALF_PI / max(ratio0, 2.0);
		float thetaBin = (int)(theta / thetaGrid) * thetaGrid;
		thetaBin = thetaBin + (thetaGrid / 2.0);
		float thetaBin0 = theta < thetaBin ? thetaBin - thetaGrid / 2.0 : thetaBin;
		float thetaBinH = thetaBin0 + thetaGrid / 4.0;
		float thetaBin1 = thetaBin0 + thetaGrid / 2.0;
		float thetaBinLerp = Remap(theta, thetaBin0, thetaBin1, 0.0, 1.0);
		thetaBin0 = thetaBin0 <= 0.0 ? Math::PI + thetaBin0 : thetaBin0;

		// TETRAHEDRONIZATION OF ROTATION + RATIO + LOD GRID
		bool centerSpecialCase = (ratio0.x == 1.0);
		float2 divLods = float2(divLod0, divLod1);
		float2 footprintAreas = float2(footprintAreaLOD0, footprintAreaLOD1);
		float2 ratios = float2(ratio0, ratio1);
		float4 thetaBins = float4(thetaBin0, thetaBinH, thetaBin1, 0.0);  // added 0.0 for center singularity case
		float3 tetras[4];
		GetAnisoCorrectingGridTetrahedron(centerSpecialCase, thetaBinLerp, ratioLerp, lodLerp, tetras[0], tetras[1], tetras[2], tetras[3]);
		if (centerSpecialCase == true)  // Account for center singularity in barycentric computation
			thetaBinLerp = Remap01To(thetaBinLerp, 0.0, ratioLerp);
		float4 tetraBarycentricWeights = GetBarycentricWeightsTetrahedron(float3(thetaBinLerp, ratioLerp, lodLerp), tetras[0], tetras[1], tetras[2], tetras[3]);  // Compute barycentric coordinates within chosen tetrahedron

		float3 accumWeights = normalize(tetraBarycentricWeights);
		accumWeights.y += accumWeights.x;
		accumWeights.z += accumWeights.y;

		int selectedTetra = 3;
		if (rnd < accumWeights.x)
			selectedTetra = 0;
		else if (rnd < accumWeights.y)
			selectedTetra = 1;
		else if (rnd < accumWeights.z)
			selectedTetra = 2;

		// PREPARE NEEDED ROTATIONS
		float3 tetra = tetras[selectedTetra];
		tetra.x *= 2;
		if (centerSpecialCase)
			tetra.x = (tetra.y == 0) ? 3 : tetra.x;

		vars.uv = RotateUV(uv, thetaBins[tetra.x], 0.0.rr) / divLods[tetra.z] / float2(1.0, ratios[tetra.y]);
		vars.gridSeed = HashWithoutSine13(float3(log2(divLods[tetra.z]), fmod(thetaBins[tetra.x], Math::TAU), ratios[tetra.y])) * 4294967296.0;
		vars.footprintArea = ratios[tetra.y] * footprintAreas[tetra.z];
	}

	float4 SampleGlints2023NDF(float noise, float logDensity, float roughness, float densityRandomization, GlintCachedVars vars, float3 H, float targetNDF, float maxNDF)
	{
		float2 slope = H.xy;  // Orthographic slope projected grid
		float rescaledTargetNDF = targetNDF / maxNDF;
		float sampleContribution = SampleGlintGridSimplex(noise, logDensity, roughness, densityRandomization, vars, slope, rescaledTargetNDF);
		return min(sampleContribution * (1.0 / roughness), 60) * maxNDF;  // somewhat brute force way of prevent glazing angle extremities}
	}
}

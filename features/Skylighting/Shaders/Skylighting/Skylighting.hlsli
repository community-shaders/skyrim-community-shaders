#ifndef __SKYLIGHTING_DEPENDENCY_HLSL__
#define __SKYLIGHTING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace Skylighting
{
#if defined(SKYLIGHTING_PROBE_REGISTER)
	Texture3D<sh2> SkylightingProbeArray : register(SKYLIGHTING_PROBE_REGISTER);
#elif defined(PSHADER)
	Texture3D<sh2> SkylightingProbeArray : register(t50);
#endif

	const static uint3 ARRAY_DIM = uint3(256, 256, 128);
	const static float3 ARRAY_SIZE = 10000.f * float3(1, 1, 0.5);
	const static float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	float GetFadeOutFactor(float3 positionMS)
	{
		float3 uvw = saturate(positionMS / ARRAY_SIZE + .5);
		float3 dists = min(uvw, 1 - uvw);
		float edgeDist = min(dists.x, min(dists.y, dists.z));
		return saturate(edgeDist * 20);
	}

	float MixDiffuse(float visibility)
	{
		return lerp(SharedData::skylightingSettings.MinDiffuseVisibility, 1.0, visibility);
	}

	float MixSpecular(float visibility)
	{
		return lerp(SharedData::skylightingSettings.MinSpecularVisibility, 1.0, saturate(visibility));
	}

#if defined(PSHADER)
	void ApplySkylighting(inout float3 diffuseColor, inout float3 directionalAmbientColor, float3 albedo, float skylightingDiffuse)
	{
		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;

		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);

		float3 linAmbient = Color::IrradianceToLinear(directionalAmbientColor);
		float3 multiBounceSkylighting = MultiBounceAO(albedo, skylightingDiffuse);
		directionalAmbientColor = Color::IrradianceToGamma(linAmbient * multiBounceSkylighting);

		diffuseColor += directionalAmbientColor;
	}
#endif

#if defined(PSHADER) || defined(SKYLIGHTING_PROBE_REGISTER)
	sh2 Sample(float2 screenPosition, float3 positionMS, float3 normalWS)
	{
		const static sh2 unitSH = float4(sqrt(4 * Math::PI), 0, 0, 0);
		sh2 scaledUnitSH = unitSH / 1e-10;

		if (SharedData::InInterior)
			return scaledUnitSH;

		positionMS.xyz += normalWS * CELL_SIZE * 0.5;  // Receiver normal bias

		if (SharedData::FrameCount) {  // Check TAA
			uint3 rand = Random::pcg3d(uint3(uint2(screenPosition.xy) & 127u, SharedData::FrameCount & 63u));
			float3 offset = float3(rand) * (1.0f / 4294967296.0f) * 2.0 - 1.0;
			positionMS.xyz += offset * CELL_SIZE * 0.5;
		}

		float3 positionMSAdjusted = positionMS - SharedData::skylightingSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1))
			return scaledUnitSH;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float wsum = 0;
		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 2; j++)
				for (int k = 0; k < 2; k++) {
					int3 offset = int3(i, j, k);
					int3 cellID = cell000 + offset;

					if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
						continue;

					float3 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
					cellCentreMS = cellCentreMS * CELL_SIZE;

					// https://handmade.network/p/75/monter/blog/p/7288-engine_work__global_illumination_with_irradiance_probes
					// basic tangent checks
					float tangentWeight = dot(normalize(cellCentreMS - positionMSAdjusted), normalWS) * 0.5 + 0.5;

					float3 trilinearWeights = 1 - abs(offset - trilinearPos);
					float w = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z * tangentWeight;

					uint3 cellTexID = (cellID + SharedData::skylightingSettings.ArrayOrigin.xyz) % ARRAY_DIM;
					sh2 probe = SphericalHarmonics::Scale(SkylightingProbeArray[cellTexID], w);

					sum = SphericalHarmonics::Add(sum, probe);
					wsum += w;
				}

		return SphericalHarmonics::Scale(sum, rcp(wsum + EPSILON_WEIGHT_SUM));
	}

	// Compute skylighting diffuse for a receiver biased to face upward (grass/foliage).
	// The result is pre-divided by vertexAO so that a subsequent multiply by vertexAO
	// yields min(skylightingDiffuse, vertexAO). Pass vertexAO = 1 to skip this compensation.
	float GetVertexSkylightingDiffuse(float2 screenPosition, float3 positionMS, float3 normalWS, float vertexAO)
	{
		if (SharedData::InInterior)
			return 1.0;

		float fadeOutFactor = GetFadeOutFactor(positionMS);

		float3 biasedNormal = normalWS;
		biasedNormal.z = max(0.0, biasedNormal);
		biasedNormal = normalize(biasedNormal);

		sh2 skylightingSH = Sample(screenPosition, positionMS, normalWS);
		float skylightingDiffuse = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(biasedNormal)) / Math::PI;
		skylightingDiffuse = saturate(skylightingDiffuse);
		skylightingDiffuse = lerp(1.0, skylightingDiffuse, fadeOutFactor);
		skylightingDiffuse = MixDiffuse(skylightingDiffuse);

		return saturate(skylightingDiffuse / max(vertexAO, 1e-5));
	}

	sh2 SampleNoBias(float3 positionMS)
	{
		const static sh2 unitSH = float4(sqrt(4 * Math::PI), 0, 0, 0);
		sh2 scaledUnitSH = unitSH / 1e-10;

		if (SharedData::InInterior)
			return scaledUnitSH;

		float3 positionMSAdjusted = positionMS - SharedData::skylightingSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1))
			return scaledUnitSH;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float wsum = 0;
		[unroll] for (int i = 0; i < 2; i++)
			[unroll] for (int j = 0; j < 2; j++)
				[unroll] for (int k = 0; k < 2; k++)
		{
			int3 offset = int3(i, j, k);
			int3 cellID = cell000 + offset;

			if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
				continue;

			float3 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
			cellCentreMS = cellCentreMS * CELL_SIZE;

			float3 trilinearWeights = 1 - abs(offset - trilinearPos);
			float w = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z;

			uint3 cellTexID = (cellID + SharedData::skylightingSettings.ArrayOrigin.xyz) % ARRAY_DIM;
			sh2 probe = SphericalHarmonics::Scale(SkylightingProbeArray[cellTexID], w);

			sum = SphericalHarmonics::Add(sum, probe);
			wsum += w;
		}

		return SphericalHarmonics::Scale(sum, rcp(wsum + EPSILON_WEIGHT_SUM));
	}
#endif
}

#endif

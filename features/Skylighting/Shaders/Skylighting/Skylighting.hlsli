#ifndef __SKYLIGHTING_DEPENDENCY_HLSL__
#define __SKYLIGHTING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace Skylighting
{
#if defined(PSHADER)
	Texture3D<sh2> SkylightingProbeArray : register(t50);
	Texture2DArray<float3> stbn_vec3_2Dx1D_128x128x64 : register(t51);
	Texture3D<uint> ShadowVisibilityBitArray : register(t52);
	Texture3D<float> ShadowVisibilityProbeArray : register(t53);
#endif

	const static uint3 ARRAY_DIM = uint3(256, 256, 128);
	const static float3 ARRAY_SIZE = 4096.f * 2.5f * float3(1, 1, 0.5);
	const static float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	float getFadeOutFactor(float3 positionMS)
	{
		float3 uvw = saturate(positionMS / ARRAY_SIZE + .5);
		float3 dists = min(uvw, 1 - uvw);
		float edgeDist = min(dists.x, min(dists.y, dists.z));
		return saturate(edgeDist * 20);
	}

	float mixDiffuse(SharedData::SkylightingSettings params, float visibility)
	{
		return lerp(params.MinDiffuseVisibility, 1.0, visibility);
	}

	float mixSpecular(SharedData::SkylightingSettings params, float visibility)
	{
		return lerp(params.MinSpecularVisibility, 1.0, saturate(visibility));
	}

#if defined(PSHADER)
	void applySkylighting(inout float3 diffuseColor, inout float3 directionalAmbientColor, float3 albedo, float skylightingDiffuse)
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

		directionalAmbientColor = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColor) * Color::MultiBounceAO(Color::IrradianceToLinear(albedo / Color::PBRLightingScale), skylightingDiffuse));

		diffuseColor += directionalAmbientColor;
	}
#endif

	sh2 sample(SharedData::SkylightingSettings params, Texture3D<sh2> probeArray, Texture3D<float> shadowVisArray, Texture2DArray<float3> blueNoise, float2 screenPosition, float3 positionMS, float3 normalWS, out float shadowVisibility)
	{
		const static sh2 unitSH = float4(sqrt(4 * Math::PI), 0, 0, 0);
		sh2 scaledUnitSH = unitSH / 1e-10;

		if (SharedData::InInterior) {
			shadowVisibility = 1.0;
			return scaledUnitSH;
		}

		positionMS.xyz += normalWS * CELL_SIZE;  // Receiver normal bias

		if (SharedData::FrameCount) {  // Check TAA
			float3 offset = blueNoise[int3(screenPosition.xy % 128, SharedData::FrameCount % 64)] * 2.0 - 1.0;
			positionMS.xyz += offset * CELL_SIZE * 0.5;
		}

		float3 positionMSAdjusted = positionMS - params.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1)) {
			shadowVisibility = 1.0;
			return scaledUnitSH;
		}

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float shadowSum = 0;
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

					uint3 cellTexID = (cellID + params.ArrayOrigin.xyz) % ARRAY_DIM;
					sh2 probe = SphericalHarmonics::Scale(probeArray[cellTexID], w);
					
					shadowSum += shadowVisArray[cellTexID] * w;

					sum = SphericalHarmonics::Add(sum, probe);
					wsum += w;
				}

		float rcpWsum = rcp(wsum + 1e-10);
		shadowVisibility = shadowSum * rcpWsum;
		return SphericalHarmonics::Scale(sum, rcpWsum);
	}

	sh2 sampleFast(SharedData::SkylightingSettings params, Texture3D<sh2> probeArray, Texture3D<float> shadowVisArray, Texture3D<uint> shadowBitArray, float3 positionMS, out float shadowVisibility)
	{
		const static sh2 unitSH = float4(sqrt(4 * Math::PI), 0, 0, 0);
		sh2 scaledUnitSH = unitSH / 1e-10;

		if (SharedData::InInterior) {
			shadowVisibility = 1.0;
			return scaledUnitSH;
		}

		float3 positionMSAdjusted = positionMS - params.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1)) {
			shadowVisibility = 1.0;
			return scaledUnitSH;
		}

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float shadowSum = 0;
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

			uint3 cellTexID = (cellID + params.ArrayOrigin.xyz) % ARRAY_DIM;
			sh2 probe = SphericalHarmonics::Scale(probeArray[cellTexID], w);

			static const float3 noise3D[32] = {
				float3(0.247, -0.583, 0.891),
				float3(-0.672, 0.315, -0.428),
				float3(0.934, 0.762, -0.153),
				float3(-0.391, -0.847, 0.526),
				float3(0.618, 0.094, 0.739),
				float3(-0.825, -0.271, -0.683),
				float3(0.152, 0.968, 0.347),
				float3(0.503, -0.714, -0.592),
				float3(-0.436, 0.629, 0.814),
				float3(0.887, -0.198, 0.461),
				float3(-0.759, 0.852, -0.305),
				float3(0.321, -0.476, -0.921),
				float3(-0.094, 0.543, -0.768),
				float3(0.776, 0.418, 0.632),
				float3(-0.538, -0.695, 0.279),
				float3(0.649, -0.921, 0.186),
				float3(-0.913, 0.127, 0.574),
				float3(0.285, 0.806, -0.447),
				float3(0.471, -0.352, 0.698),
				float3(-0.627, -0.194, -0.856),
				float3(0.834, 0.591, -0.712),
				float3(-0.173, -0.968, -0.421),
				float3(0.562, 0.239, -0.785),
				float3(-0.745, 0.487, 0.316),
				float3(0.108, -0.631, 0.894),
				float3(0.926, -0.845, -0.267),
				float3(-0.384, 0.712, -0.539),
				float3(0.697, 0.163, 0.825),
				float3(-0.851, -0.429, 0.641),
				float3(0.214, 0.934, 0.372),
				float3(0.578, -0.762, -0.614),
				float3(-0.469, 0.381, 0.947)
			};

			uint shadowBits = shadowBitArray[cellTexID];

			float tempShadowSum = 0;
			float tempShadowWeight = 0;

			for(uint i = 0; i < 32; i++){				
				float3 bitCellCentreMS = cellCentreMS + noise3D[i] * Skylighting::CELL_SIZE;
				float weight = rcp(distance(positionMSAdjusted, bitCellCentreMS) + 1e-10);

				tempShadowSum += float((shadowBits >> i) & 1u) * weight;
				tempShadowWeight += weight;
			}

			tempShadowSum *= rcp(tempShadowWeight + 1e-10);

			shadowSum += tempShadowSum * w;

			sum = SphericalHarmonics::Add(sum, probe);
			wsum += w;
		}

		shadowVisibility = shadowSum * rcp(wsum + 1e-10);

		return SphericalHarmonics::Scale(sum, rcp(wsum + 1e-10));
	}

}

#endif

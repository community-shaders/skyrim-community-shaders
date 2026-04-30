#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

#include "SDFGI/common.hlsli"

// ---- Resources ----

Texture2D<float> depthTexture : register(t0);
Texture2D<float4> normalRoughnessTexture : register(t1);
Texture2DArray<uint> lightprobeData : register(t2);     // R32_UINT, octahedral encoded probes
Texture3D<uint> occlusionTex : register(t3);             // R16_UINT

RWTexture2D<float4> giOutput : register(u0);            // R11G11B10_FLOAT

// ---- Sample-pass constant buffer (b2, separate from common b0/b1) ----

cbuffer SDFGISampleCB : register(b2)
{
	float3 SampleGridSize;
	uint SampleMaxCascades;
	uint UseOcclusion;
	int ProbeAxisSize;
	float ProbeToUVW;
	float NormalBias;
	float3 LightprobeTexPixelSize;
	float Energy;
	float3 LightprobeUVOffset;
	float YMult;
	CascadeData SampleCascades[8];
};

// ---- Helpers ----

// Convert world position to cascade-local probe coordinate space.
// Returns continuous float3 so the caller can compute trilinear corners.
float3 WorldToProbePos(float3 worldPos, uint cascadeIdx)
{
	return (worldPos - SampleCascades[cascadeIdx].Offset) * SampleCascades[cascadeIdx].ToCell;
}

// Map an octahedral UV + probe grid coordinate to lightprobeData texel coordinates.
// probeGridPos : integer 3D position in the probe grid (x, y, z)
// octUV        : [0,1]^2 octahedral direction UV
// Returns int3(u, v, slice) for Texture2DArray.Load().
int3 ProbeTexCoord(int3 probeGridPos, float2 octUV, uint cascadeIdx)
{
	// Each probe occupies (LIGHTPROBE_OCT_SIZE + 2) texels with a 1-texel border.
	int2 probeTexel = probeGridPos.xz * (LIGHTPROBE_OCT_SIZE + 2) + int2(octUV * LIGHTPROBE_OCT_SIZE) + 1;

	// Y axis maps to array slice: probeGridPos.y + cascade layer offset.
	int slice = probeGridPos.y + (int)(cascadeIdx * (uint)ProbeAxisSize);

	return int3(probeTexel, slice);
}

// Compute a smooth cascade blend factor that fades towards the edges of the cascade volume.
// Returns 1.0 when fully inside, 0.0 when at/beyond the boundary.
float CascadeBlendFactor(float3 probePos, float3 gridExtent)
{
	// Distance from edge in probe cells (negative = outside).
	float3 fromEdge = min(probePos, gridExtent - probePos);
	float minEdge = min(min(fromEdge.x, fromEdge.y), fromEdge.z);

	// Blend over a 1-cell margin at the boundary.
	return saturate(minEdge);
}

// ---- Main kernel ----

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// Bounds check
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	// Screen UV
	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	// Depth
	float depth = depthTexture[dispatchID.xy];
	if (depth >= 1.0) {
		giOutput[dispatchID.xy] = float4(0, 0, 0, 0);
		return;
	}

	// Reconstruct camera-relative world position
	float4 positionCS = float4(2.0 * float2(uv.x, -uv.y + 1.0) - 1.0, depth, 1.0);
	float4 positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionWS.xyz /= positionWS.w;

	// Decode view-space normal from GBuffer (octahedral in RG channels)
	float3 normalVS = GBuffer::DecodeNormal(normalRoughnessTexture[dispatchID.xy].xy);
	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

	// Bias the sample position along the normal to reduce self-shadowing / bleeding
	float3 biasedPos = positionWS.xyz + normalWS * NormalBias;

	// Probe grid extent (number of probes per axis)
	float3 gridExtent = float3(ProbeAxisSize, ProbeAxisSize, ProbeAxisSize);

	float3 accumulatedLight = float3(0, 0, 0);
	float cascadeBlend = 0.0;

	for (uint c = 0; c < SampleMaxCascades; c++) {
		// Convert world position to cascade-local probe coordinates
		float3 probePos = WorldToProbePos(biasedPos, c);

		// Check if inside cascade bounds (with 0.5 cell margin for trilinear)
		float3 clampedPos = clamp(probePos, 0.5, gridExtent - 0.5);
		if (any(abs(probePos - clampedPos) > 0.001))
			continue;

		// Trilinear base corner and fractional offsets
		float3 baseF = probePos - 0.5;
		int3 baseI = int3(floor(baseF));
		float3 frac3 = baseF - float3(baseI);

		float3 diffuseLight = float3(0, 0, 0);
		float totalWeight = 0.0;

		// 8 corners of the trilinear cell
		[unroll]
		for (uint corner = 0; corner < 8; corner++) {
			int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
			int3 probeIdx = baseI + offset;

			// Clamp to valid probe range
			probeIdx = clamp(probeIdx, int3(0, 0, 0), int3(gridExtent) - 1);

			// Trilinear weight
			float3 triWeights = lerp(1.0 - frac3, frac3, float3(offset));
			float weight = triWeights.x * triWeights.y * triWeights.z;

			// Direction from probe to pixel for back-face weighting
			float3 probeWorldPos = SampleCascades[c].Offset + (float3(probeIdx) + 0.5) / SampleCascades[c].ToCell;
			float3 probeToPixelDir = normalize(positionWS.xyz - probeWorldPos);

			// Back-face weight: reduce contribution from probes behind the surface
			weight *= max(0.005, dot(normalWS, probeToPixelDir));

			// Occlusion weighting
			if (UseOcclusion) {
				uint3 occDim;
				occlusionTex.GetDimensions(occDim.x, occDim.y, occDim.z);
				int3 occCoord = int3((float3(probeIdx) + 0.5) / gridExtent * float3(occDim));
				occCoord.z = (occCoord.z + (int)c * (int)occDim.z / (int)SampleMaxCascades);
				occCoord = clamp(occCoord, int3(0,0,0), int3(occDim) - 1);
				uint occValue = occlusionTex.Load(int4(occCoord, 0));
				float occFactor = (float)occValue / 65535.0;
				weight *= occFactor;
			}

			// Convert normal to octahedral UV for probe lookup
			float2 octUV = OctEncode(normalWS);

			// Compute probe texel coordinates and load
			int3 texCoord = ProbeTexCoord(probeIdx, octUV, c);
			uint encodedRadiance = lightprobeData.Load(int4(texCoord.xy, texCoord.z, 0));
			float3 probeLight = DecodeRGBE9995(encodedRadiance);

			diffuseLight += probeLight * weight;
			totalWeight += weight;
		}

		// Normalize by total weight
		if (totalWeight > 1e-6)
			diffuseLight /= totalWeight;

		// Cascade blend factor (smooth falloff at edges)
		float blend = CascadeBlendFactor(probePos, gridExtent);

		if (cascadeBlend == 0.0) {
			// First contributing cascade
			accumulatedLight = diffuseLight;
		} else {
			// Blend with previous cascade result
			accumulatedLight = lerp(accumulatedLight, diffuseLight, 1.0 - cascadeBlend);
		}

		cascadeBlend = blend;

		// If fully inside this cascade, no need to check coarser ones
		if (blend >= 1.0)
			break;
	}

	accumulatedLight *= Energy;

	giOutput[dispatchID.xy] = float4(accumulatedLight, 1.0);
}

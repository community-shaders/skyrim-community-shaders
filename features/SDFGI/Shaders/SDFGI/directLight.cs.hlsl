// SDFGI Direct Light Compute Shader
// Port of Godot's sdfgi_direct_light.glsl to DX11 HLSL
// Computes direct lighting for each solid voxel in the SDF grid

#include "SDFGI/common.hlsli"

// SDF cascade distance fields (R8_UNORM, one per cascade)
Texture3D<float> sdfCascade0 : register(t0);
Texture3D<float> sdfCascade1 : register(t1);
Texture3D<float> sdfCascade2 : register(t2);
Texture3D<float> sdfCascade3 : register(t3);
Texture3D<float> sdfCascade4 : register(t4);
Texture3D<float> sdfCascade5 : register(t5);
Texture3D<float> sdfCascade6 : register(t6);
Texture3D<float> sdfCascade7 : register(t7);

// Voxel data
StructuredBuffer<ProcessVoxel> solidCellBuffer : register(t8);
Buffer<uint> dispatchBuffer : register(t9);

// Lightprobe data for bounce feedback (RGBE9995 encoded probes)
Texture2DArray<uint> lightprobeData : register(t10);

// Occlusion texture
Texture3D<uint> occlusionTex : register(t11);

// Light output (RGBE encoded)
RWTexture3D<uint> lightTex : register(u0);

// Anisotropic light storage
// lightAniso0Tex stores 4 directions: +X (r), -X (g), +Y (b), -Y (a)
RWTexture3D<float4> lightAniso0Tex : register(u1);
// lightAniso1Tex stores 2 directions: +Z (r), -Z (g)
RWTexture3D<float2> lightAniso1Tex : register(u2);

// Structured buffer for lights
StructuredBuffer<SDFGILight> lightBuffer : register(t12);

// 6 cardinal directions for anisotropic storage
static const float3 anisoDir[ANISOTROPY_SIZE] = {
	float3(1, 0, 0),
	float3(-1, 0, 0),
	float3(0, 1, 0),
	float3(0, -1, 0),
	float3(0, 0, 1),
	float3(0, 0, -1)
};

// 26-neighbor offsets for light spreading
static const int3 neighborOffsets[26] = {
	// face neighbors (6)
	int3(-1, 0, 0), int3(1, 0, 0),
	int3(0, -1, 0), int3(0, 1, 0),
	int3(0, 0, -1), int3(0, 0, 1),
	// edge neighbors (12)
	int3(-1, -1, 0), int3(-1, 1, 0), int3(1, -1, 0), int3(1, 1, 0),
	int3(-1, 0, -1), int3(-1, 0, 1), int3(1, 0, -1), int3(1, 0, 1),
	int3(0, -1, -1), int3(0, -1, 1), int3(0, 1, -1), int3(0, 1, 1),
	// corner neighbors (8)
	int3(-1, -1, -1), int3(-1, -1, 1), int3(-1, 1, -1), int3(-1, 1, 1),
	int3(1, -1, -1), int3(1, -1, 1), int3(1, 1, -1), int3(1, 1, 1)
};

// Sample the SDF cascade texture by index
float SampleSDFCascade(uint cascadeIdx, float3 uvw)
{
	[forcecase] switch (cascadeIdx) {
	case 0:
		return sdfCascade0.SampleLevel(linearSampler, uvw, 0);
	case 1:
		return sdfCascade1.SampleLevel(linearSampler, uvw, 0);
	case 2:
		return sdfCascade2.SampleLevel(linearSampler, uvw, 0);
	case 3:
		return sdfCascade3.SampleLevel(linearSampler, uvw, 0);
	case 4:
		return sdfCascade4.SampleLevel(linearSampler, uvw, 0);
	case 5:
		return sdfCascade5.SampleLevel(linearSampler, uvw, 0);
	case 6:
		return sdfCascade6.SampleLevel(linearSampler, uvw, 0);
	case 7:
		return sdfCascade7.SampleLevel(linearSampler, uvw, 0);
	default:
		return 1.0;
	}
}

// Trace shadow ray through SDF cascades
// Returns shadow factor: 1.0 = fully lit, 0.0 = fully shadowed
float TraceShadowRay(float3 rayPos, float3 rayDir, uint startCascade, float maxDist)
{
	float shadow = 1.0;

	for (uint c = startCascade; c < MaxCascades; c++) {
		float cellSize = 1.0 / Cascades[c].ToCell;
		float3 cascadePos = WorldToCascadePos(rayPos, c);

		// Check if we're within this cascade's bounds
		float3 boundsMin = float3(0.5, 0.5, 0.5);
		float3 boundsMax = GridSize - float3(0.5, 0.5, 0.5);

		if (any(cascadePos < boundsMin) || any(cascadePos > boundsMax))
			continue;

		// Step size scaled for this cascade
		float3 uvwScale = 1.0 / GridSize;

		float traveled = 0.0;
		float maxSteps = 512.0;

		for (float step = 0; step < maxSteps; step++) {
			float3 currentCascadePos = WorldToCascadePos(rayPos, c);

			// Bounds check
			if (any(currentCascadePos < boundsMin) || any(currentCascadePos > boundsMax))
				break;

			float3 uvw = currentCascadePos * uvwScale;
			float sdfDist = SampleSDFCascade(c, uvw);

			// SDF is stored as R8_UNORM [0,1], remap to distance in cells
			float advance = sdfDist * 255.0 - 1.0;

			if (advance < 0.05) {
				shadow = 0.0;
				break;
			}

			float stepDist = advance * cellSize;
			rayPos += rayDir * stepDist;
			traveled += stepDist;

			if (maxDist > 0.0 && traveled > maxDist)
				break;
		}

		if (shadow < 0.5)
			break;
	}

	return shadow;
}

// Sample lightprobe for bounce feedback
float3 SampleLightprobe(float3 worldPos, uint cascadeIdx)
{
	float3 cascadePos = WorldToCascadePos(worldPos, cascadeIdx);
	float3 probePos = cascadePos / float(PROBE_DIVISOR);

	// Clamp to valid probe range
	int3 probeCoord = clamp(int3(probePos), int3(0, 0, 0), int3(ProbeAxisSize - 1, ProbeAxisSize - 1, ProbeAxisSize - 1));

	// Trilinear interpolation weights
	float3 frac3 = frac(probePos);

	float3 result = float3(0, 0, 0);
	float totalWeight = 0.0;

	// 8 surrounding probes for trilinear interpolation
	for (uint i = 0; i < 8; i++) {
		int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
		int3 coord = clamp(probeCoord + offset, int3(0, 0, 0), int3(ProbeAxisSize - 1, ProbeAxisSize - 1, ProbeAxisSize - 1));

		float3 w3 = lerp(1.0 - frac3, frac3, float3(offset));
		float weight = w3.x * w3.y * w3.z;

		// Lightprobe data is laid out as a 2D atlas with Z slices in the array dimension
		int2 texCoord = int2(coord.x, coord.y);
		uint encoded = lightprobeData.Load(int4(texCoord, coord.z, 0));
		float3 probeLight = DecodeRGBE9995(encoded);

		result += probeLight * weight;
		totalWeight += weight;
	}

	if (totalWeight > 0.0)
		result /= totalWeight;

	return result;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint index = DTid.x;

	// Get total cell count from dispatch buffer
	uint totalCells = dispatchBuffer[3];

	if (index >= totalCells)
		return;

#ifdef MODE_DYNAMIC
	// Progressive update: only process a subset of voxels each frame
	if ((index % ProcessIncrement) != ProcessOffset)
		return;
#endif

	// Read voxel data
	ProcessVoxel voxel = solidCellBuffer[index];
	uint3 gridPos = UnpackVoxelPosition(voxel.position);
	float3 albedo = UnpackVoxelAlbedo(voxel.albedo);

	// Extract 6-bit facing mask from albedo field (bits 15-20)
	uint facingBits = (voxel.albedo >> 15) & 0x3F;

	// Determine which aniso directions are valid for this voxel
	bool validAniso[ANISOTROPY_SIZE];
	[unroll]
	for (uint a = 0; a < ANISOTROPY_SIZE; a++) {
		validAniso[a] = (facingBits >> a) & 1;
	}

	// Get voxel world position
	float3 voxelWorldPos = GetVoxelWorldPos(gridPos, Cascade);
	float cellSize = 1.0 / Cascades[Cascade].ToCell;

	// Initialize light accumulation per aniso direction
	float3 lightAccum[ANISOTROPY_SIZE];
	[unroll]
	for (uint d = 0; d < ANISOTROPY_SIZE; d++) {
		lightAccum[d] = float3(0, 0, 0);
	}

#ifdef MODE_DYNAMIC
	// Bounce feedback: sample existing lightprobes for indirect contribution
	if (BounceFeedback > 0.0) {
		float3 indirectLight = SampleLightprobe(voxelWorldPos, Cascade);
		float3 bounceContrib = indirectLight * albedo * BounceFeedback;

		[unroll]
		for (uint bd = 0; bd < ANISOTROPY_SIZE; bd++) {
			if (validAniso[bd]) {
				lightAccum[bd] += bounceContrib;
			}
		}
	}
#endif

	// Process each light
	for (uint lightIdx = 0; lightIdx < LightCount; lightIdx++) {
		SDFGILight light = lightBuffer[lightIdx];

		float3 lightDir;
		float attenuation = 1.0;
		float maxShadowDist = -1.0;  // negative = unlimited

		if (light.type == LIGHT_TYPE_DIRECTIONAL) {
			// Directional light
			lightDir = -light.direction;
			attenuation = 1.0;
		} else {
			// Omni or spot light
			float3 toLight = light.position - voxelWorldPos;
			float dist = length(toLight);
			lightDir = toLight / max(dist, 0.0001);

			if (light.radius > 0.0) {
				float invRadius = 1.0 / light.radius;
				attenuation = GetOmniAttenuation(dist, invRadius, light.attenuation);
			} else {
				attenuation = 0.0;
			}

			maxShadowDist = dist;

			// Spot light cone falloff
			if (light.type == LIGHT_TYPE_SPOT) {
				float cosAngle = dot(-lightDir, light.direction);

				if (cosAngle < light.cosSpotAngle) {
					attenuation = 0.0;
				} else {
					float innerAngle = light.cosSpotAngle + (1.0 - light.cosSpotAngle) * 0.7;
					float spotRim = max(0.0001, innerAngle - light.cosSpotAngle);
					attenuation *= saturate((cosAngle - light.cosSpotAngle) / spotRim);
					attenuation *= pow(cosAngle, light.invSpotAttenuation);
				}
			}
		}

		// Skip lights with negligible contribution
		if (attenuation < 0.001)
			continue;

		// Shadow ray-march through SDF
		float shadow = 1.0;
		if (light.hasShadow) {
			// Offset ray origin slightly to avoid self-shadowing
			float3 shadowOrigin = voxelWorldPos + lightDir * cellSize * 1.5;
			shadow = TraceShadowRay(shadowOrigin, lightDir, Cascade, maxShadowDist);
		}

		if (shadow < 0.001)
			continue;

		// Compute lit contribution
		float3 lightContrib = light.color * light.energy * attenuation * shadow;

		// Distribute to aniso directions by dot product weight
		[unroll]
		for (uint ad = 0; ad < ANISOTROPY_SIZE; ad++) {
			if (validAniso[ad]) {
				float weight = max(0.0, dot(anisoDir[ad], lightDir));
				lightAccum[ad] += lightContrib * albedo * weight;
			}
		}
	}

	// Compute total light as average of valid directions
	float3 totalLight = float3(0, 0, 0);
	uint validCount = 0;
	[unroll]
	for (uint td = 0; td < ANISOTROPY_SIZE; td++) {
		if (validAniso[td]) {
			totalLight += lightAccum[td];
			validCount++;
		}
	}
	if (validCount > 0)
		totalLight /= (float)validCount;

	// Encode total light as RGBE8985 for the main light texture
	uint encodedLight = EncodeRGBE8985(totalLight);

	// Compute anisotropy weights
	// Each direction stores the ratio of that direction's light to the total
	float maxLight = max(max(totalLight.r, totalLight.g), totalLight.b);
	float4 aniso0 = float4(0, 0, 0, 0);
	float2 aniso1 = float2(0, 0);

	if (maxLight > 0.0) {
		float totalMag = 0.0;
		float anisoMag[ANISOTROPY_SIZE];

		[unroll]
		for (uint am = 0; am < ANISOTROPY_SIZE; am++) {
			anisoMag[am] = validAniso[am] ? (lightAccum[am].r + lightAccum[am].g + lightAccum[am].b) : 0.0;
			totalMag += anisoMag[am];
		}

		if (totalMag > 0.0) {
			float invTotal = 1.0 / totalMag;
			aniso0 = float4(
				anisoMag[0] * invTotal,
				anisoMag[1] * invTotal,
				anisoMag[2] * invTotal,
				anisoMag[3] * invTotal);
			aniso1 = float2(
				anisoMag[4] * invTotal,
				anisoMag[5] * invTotal);
		} else {
			// Uniform distribution for valid directions
			float invValid = (validCount > 0) ? (1.0 / (float)validCount) : 0.0;
			[unroll]
			for (uint uv = 0; uv < 4; uv++) {
				aniso0[uv] = validAniso[uv] ? invValid : 0.0;
			}
			aniso1.x = validAniso[4] ? invValid : 0.0;
			aniso1.y = validAniso[5] ? invValid : 0.0;
		}
	}

	// Write to output textures at the voxel's grid position
	uint3 writePos = gridPos;
	lightTex[writePos] = encodedLight;
	lightAniso0Tex[writePos] = aniso0;
	lightAniso1Tex[writePos] = aniso1;

	// Fill 26-connected neighbors that share this voxel's light
	// This spreads light to empty neighbor cells for smoother interpolation
	[loop]
	for (uint n = 0; n < 26; n++) {
		if (GetNeighborBit(voxel, n)) {
			int3 neighborPos = int3(gridPos) + neighborOffsets[n];

			// Bounds check
			if (all(neighborPos >= int3(0, 0, 0)) && all(neighborPos < int3(GridSize))) {
				uint3 nPos = uint3(neighborPos);
				lightTex[nPos] = encodedLight;
				lightAniso0Tex[nPos] = aniso0;
				lightAniso1Tex[nPos] = aniso1;
			}
		}
	}
}

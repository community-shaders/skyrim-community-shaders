/// SDFGI probe integration compute shader.
///
/// Port of Godot's sdfgi_integrate.glsl to DX11 HLSL.
/// Integrates lighting at probe positions using SDF ray-marching
/// and spherical harmonics.
///
/// Modes (selected via preprocessor defines):
///   MODE_PROCESS      - Ray-march through SDF cascades, accumulate SH
///   MODE_STORE        - Convert SH probe data to octahedral radiance map
///   MODE_SCROLL       - Scroll probe history when cascade moves
///   MODE_SCROLL_STORE - Store scrolled history data back

#include "SDFGI/common.hlsli"

// ---------------------------------------------------------------------------
// SDF cascade textures (one per cascade, up to MAX_CASCADES)
// ---------------------------------------------------------------------------

Texture3D<float> sdfCascade0 : register(t0);
Texture3D<float> sdfCascade1 : register(t1);
Texture3D<float> sdfCascade2 : register(t2);
Texture3D<float> sdfCascade3 : register(t3);
Texture3D<float> sdfCascade4 : register(t4);
Texture3D<float> sdfCascade5 : register(t5);
Texture3D<float> sdfCascade6 : register(t6);
Texture3D<float> sdfCascade7 : register(t7);

// ---------------------------------------------------------------------------
// Light cascade textures (RGBE9995 encoded, one per cascade)
// ---------------------------------------------------------------------------

Texture3D<uint> lightCascade0 : register(t8);
Texture3D<uint> lightCascade1 : register(t9);
Texture3D<uint> lightCascade2 : register(t10);
Texture3D<uint> lightCascade3 : register(t11);
Texture3D<uint> lightCascade4 : register(t12);
Texture3D<uint> lightCascade5 : register(t13);
Texture3D<uint> lightCascade6 : register(t14);
Texture3D<uint> lightCascade7 : register(t15);

// ---------------------------------------------------------------------------
// UAVs
// ---------------------------------------------------------------------------

// Probe SH history (R16G16B16A16_SINT, 2D array with HistorySize slices)
// Layout: Width = ProbeAxisSize * ProbeAxisSize, Height = ProbeAxisSize * SH_SIZE
RWTexture2DArray<int4> probeHistoryTex : register(u0);

// Probe SH running average (R32G32B32A32_SINT, single 2D)
// Same spatial layout as history
RWTexture2D<int4> probeAverageTex : register(u1);

#if defined(MODE_STORE) || defined(MODE_SCROLL_STORE)
// Lightprobe octahedral radiance map (R32_UINT, 2D array, RGBE9995 encoded)
RWTexture2DArray<uint> lightprobeData : register(u2);
#endif

// ---------------------------------------------------------------------------
// Helpers to sample SDF/Light by cascade index
// ---------------------------------------------------------------------------

float SampleSDF(uint cascadeIdx, float3 uvw)
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
#if MAX_CASCADES > 4
	case 4:
		return sdfCascade4.SampleLevel(linearSampler, uvw, 0);
	case 5:
		return sdfCascade5.SampleLevel(linearSampler, uvw, 0);
	case 6:
		return sdfCascade6.SampleLevel(linearSampler, uvw, 0);
	case 7:
		return sdfCascade7.SampleLevel(linearSampler, uvw, 0);
#endif
	default:
		return 1.0;
	}
}

uint SampleLight(uint cascadeIdx, int3 texel)
{
	[forcecase] switch (cascadeIdx) {
	case 0:
		return lightCascade0.Load(int4(texel, 0));
	case 1:
		return lightCascade1.Load(int4(texel, 0));
	case 2:
		return lightCascade2.Load(int4(texel, 0));
	case 3:
		return lightCascade3.Load(int4(texel, 0));
#if MAX_CASCADES > 4
	case 4:
		return lightCascade4.Load(int4(texel, 0));
	case 5:
		return lightCascade5.Load(int4(texel, 0));
	case 6:
		return lightCascade6.Load(int4(texel, 0));
	case 7:
		return lightCascade7.Load(int4(texel, 0));
#endif
	default:
		return 0;
	}
}

// Sample SDF gradient (central differences) to approximate surface normal
float3 SampleSDFGradient(uint cascadeIdx, float3 uvw, float texelSize)
{
	float3 grad;
	grad.x = SampleSDF(cascadeIdx, uvw + float3(texelSize, 0, 0)) - SampleSDF(cascadeIdx, uvw - float3(texelSize, 0, 0));
	grad.y = SampleSDF(cascadeIdx, uvw + float3(0, texelSize, 0)) - SampleSDF(cascadeIdx, uvw - float3(0, texelSize, 0));
	grad.z = SampleSDF(cascadeIdx, uvw + float3(0, 0, texelSize)) - SampleSDF(cascadeIdx, uvw - float3(0, 0, texelSize));
	float len = length(grad);
	return (len > 1e-6) ? (grad / len) : float3(0, 1, 0);
}

// ---------------------------------------------------------------------------
// Probe texture coordinate helpers
// ---------------------------------------------------------------------------

// Probe history / average texture layout:
//   X coordinate: probeX + probeY * ProbeAxisSize
//   Y coordinate: probeZ * SH_SIZE + shIndex
//   Z coordinate (history only): HistoryIndex slice

int2 GetProbeSHTexCoord(uint3 probeCoord, uint shIdx)
{
	return int2(
		probeCoord.x + probeCoord.y * ProbeAxisSize,
		probeCoord.z * SH_SIZE + shIdx);
}

// Lightprobe octahedral texture layout:
//   Each probe occupies (LIGHTPROBE_OCT_SIZE + 2) x (LIGHTPROBE_OCT_SIZE + 2) texels
//   with a 1-texel border for filtering.
//   probeCoord.x + probeCoord.y * ProbeAxisSize maps the XY probe grid,
//   probeCoord.z selects the array slice.
int3 GetLightprobeTexCoord(uint3 probeCoord, uint2 octTexel, uint cascadeSlice)
{
	uint probeTexSize = LIGHTPROBE_OCT_SIZE + 2;
	int2 baseXY = int2(
		(probeCoord.x + probeCoord.y * ProbeAxisSize) * probeTexSize + 1,
		probeCoord.z * probeTexSize + 1);
	return int3(baseXY + int2(octTexel), cascadeSlice);
}

// =========================================================================
#ifdef MODE_PROCESS
// =========================================================================

// Dispatch: (ProbeAxisSize^3 + 63) / 64 groups, 1, 1
// Thread group: 8x8x1 = 64 threads, each handles one probe.
// Each thread independently processes its own probe (no groupshared needed).

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID,
	uint3 localID : SV_GroupThreadID,
	uint localIdx : SV_GroupIndex)
{
	uint probeIdx = groupID.x * 64 + localIdx;

	uint totalProbes = (uint)(ProbeAxisSize * ProbeAxisSize * ProbeAxisSize);
	if (probeIdx >= totalProbes)
		return;

	// Convert flat index to 3D probe coordinate
	uint3 probeCoord;
	probeCoord.x = probeIdx % (uint)ProbeAxisSize;
	probeCoord.y = (probeIdx / (uint)ProbeAxisSize) % (uint)ProbeAxisSize;
	probeCoord.z = probeIdx / (uint)(ProbeAxisSize * ProbeAxisSize);

	// Compute probe world position from cascade data
	float cellSize = 1.0 / Cascades[Cascade].ToCell;

	float3 probeWorldPos = Cascades[Cascade].Offset +
		(float3(probeCoord) * PROBE_DIVISOR + float3(PROBE_DIVISOR, PROBE_DIVISOR, PROBE_DIVISOR) * 0.5) / Cascades[Cascade].ToCell;

	// Apply Y scaling
	probeWorldPos.y *= YMult;

	// SH accumulation (per-thread, not shared)
	float shAccumR[SH_SIZE];
	float shAccumG[SH_SIZE];
	float shAccumB[SH_SIZE];

	[unroll]
	for (uint s = 0; s < SH_SIZE; ++s) {
		shAccumR[s] = 0.0;
		shAccumG[s] = 0.0;
		shAccumB[s] = 0.0;
	}

	// Frame-based rotation offset for temporal dithering (golden angle)
	float rotAngle = (float)(HistoryIndex) * 2.399963;

	// Ray march loop
	for (uint rayIdx = 0; rayIdx < RayCount; ++rayIdx) {
		// Generate ray direction using Vogel hemisphere sampling
		float3 rayDir = VogelHemisphereDir(rayIdx, RayCount, rotAngle);

		// Apply random hemisphere flip to cover full sphere
		// Use a hash of rayIdx + historyIndex to decide flip
		uint flipSeed = rayIdx * 13 + HistoryIndex * 7;
		if (flipSeed & 1)
			rayDir.x = -rayDir.x;
		if (flipSeed & 2)
			rayDir.y = -rayDir.y;
		if ((flipSeed >> 2) & 1)
			rayDir.z = -rayDir.z;

		// Normalize after potential flips (already normalized but ensure)
		rayDir = normalize(rayDir);

		// Start position offset by bias
		float3 rayPos = probeWorldPos + rayDir * RayBias * cellSize;

		float3 hitLight = float3(0, 0, 0);
		bool hit = false;

		// March through SDF cascades from finest to coarsest
		float totalDist = 0.0;
		float maxDist = cellSize * CASCADE_SIZE * 0.5;

		for (uint step = 0; step < 256; ++step) {
			if (totalDist > maxDist)
				break;

			// Try to find this position in a cascade
			bool foundCascade = false;

			for (uint ci = Cascade; ci < MaxCascades; ++ci) {
				float ciCellSize = 1.0 / Cascades[ci].ToCell;

				// Convert ray position to cascade grid coordinates
				float3 gridPos = (rayPos - Cascades[ci].Offset) * Cascades[ci].ToCell;

				// Check bounds (with margin)
				float margin = 2.0;
				if (any(gridPos < margin) || any(gridPos > GridSize - margin))
					continue;

				// Sample SDF distance
				float3 uvw = gridPos / GridSize;
				float sdfDist = SampleSDF(ci, uvw);

				// SDF is stored as R8_UNORM [0,1] mapping to [0, 255] cells distance
				float worldDist = (sdfDist * 255.0 - 1.0) * ciCellSize;

				if (worldDist < ciCellSize * 0.5) {
					// Hit: sample light at this position
					int3 lightTexel = int3(gridPos);
					lightTexel = clamp(lightTexel, int3(0, 0, 0), int3(GridSize) - int3(1, 1, 1));

					uint encodedLight = SampleLight(ci, lightTexel);
					hitLight = DecodeRGBE9995(encodedLight);

					// Weight by dot product with surface normal from SDF gradient
					float texelSize = 1.0 / GridSize.x;
					float3 surfNormal = SampleSDFGradient(ci, uvw, texelSize);
					float ndotl = max(dot(surfNormal, -rayDir), 0.0);
					hitLight *= ndotl;

					// Add bounce feedback
					hitLight *= (1.0 + BounceFeedback);

					hit = true;
					break;
				}

				// Advance ray
				float advance = max(worldDist, ciCellSize * 0.5);
				rayPos += rayDir * advance;
				totalDist += advance;
				foundCascade = true;
				break;
			}

			if (hit)
				break;

			if (!foundCascade) {
				// Outside all cascades - treat as sky
				break;
			}
		}

		// Sky contribution on miss
		if (!hit) {
			if (SkyFlags > 0) {
				hitLight = SkyColor * SkyEnergy;
			}
		}

		// Accumulate into SH
		[unroll]
		for (uint sh = 0; sh < SH_SIZE; ++sh) {
			float basis = SHBasis(sh, rayDir);
			shAccumR[sh] += hitLight.r * basis;
			shAccumG[sh] += hitLight.g * basis;
			shAccumB[sh] += hitLight.b * basis;
		}
	}

	// Normalize SH by 4*PI / RayCount
	float normFactor = 4.0 * PI / (float)RayCount;

	// Fixed-point scale for storage
	float fixedPointScale = (float)(1 << HISTORY_BITS);

	[unroll]
	for (uint sh2 = 0; sh2 < SH_SIZE; ++sh2) {
		int2 texCoord = GetProbeSHTexCoord(probeCoord, sh2);

		// Convert to fixed-point integers
		int4 newValue = int4(
			(int)(shAccumR[sh2] * normFactor * fixedPointScale),
			(int)(shAccumG[sh2] * normFactor * fixedPointScale),
			(int)(shAccumB[sh2] * normFactor * fixedPointScale),
			0);

		// Read old history value at this slot
		int4 oldValue = probeHistoryTex[int3(texCoord, HistoryIndex)];

		// Write new history
		probeHistoryTex[int3(texCoord, HistoryIndex)] = newValue;

		// Update running average: average += (new - old)
		int4 currentAvg = probeAverageTex[texCoord];
		probeAverageTex[texCoord] = currentAvg + (newValue - oldValue);
	}
}

#endif  // MODE_PROCESS

// =========================================================================
#ifdef MODE_STORE
// =========================================================================

// Converts SH probe data to octahedral radiance map.
// Dispatch covers all probes and all octahedral texels.
// Thread layout: each thread handles one texel in the octahedral map.

[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID)
{
	// Compute which probe and which octahedral texel this thread handles
	uint probeLinearX = globalID.x / LIGHTPROBE_OCT_SIZE;
	uint octX = globalID.x % LIGHTPROBE_OCT_SIZE;

	uint probeZ = globalID.y / LIGHTPROBE_OCT_SIZE;
	uint octY = globalID.y % LIGHTPROBE_OCT_SIZE;

	uint totalProbesXY = (uint)(ProbeAxisSize * ProbeAxisSize);
	if (probeLinearX >= totalProbesXY)
		return;
	if (probeZ >= (uint)ProbeAxisSize)
		return;

	uint3 probeCoord;
	probeCoord.x = probeLinearX % (uint)ProbeAxisSize;
	probeCoord.y = probeLinearX / (uint)ProbeAxisSize;
	probeCoord.z = probeZ;

	// Compute direction from octahedral UV
	float2 octUV = (float2(octX, octY) + 0.5) / (float)LIGHTPROBE_OCT_SIZE;
	float3 dir = OctDecode(octUV);

	// Read SH average and evaluate irradiance in this direction
	float3 irradiance = float3(0, 0, 0);
	float invScale = 1.0 / (float)(1 << HISTORY_BITS);
	float invHistory = 1.0 / (float)HistorySize;

	[unroll]
	for (uint sh = 0; sh < SH_SIZE; ++sh) {
		int2 texCoord = GetProbeSHTexCoord(probeCoord, sh);
		int4 avgValue = probeAverageTex[texCoord];

		// Convert from fixed-point accumulator to float
		// The average is the sum of all history slots, so divide by HistorySize
		float3 shCoeff = float3(avgValue.rgb) * invScale * invHistory;

		float basis = SHBasis(sh, dir);
		irradiance += shCoeff * basis;
	}

	// Clamp to non-negative
	irradiance = max(irradiance, 0.0);

	// Encode as RGBE9995 and store
	uint encoded = EncodeRGBE9995(irradiance);

	// Store in lightprobe data texture
	// Two cascade layers per cascade: diffuse and specular-like
	uint cascadeSlice = Cascade * 2;

	// Write the interior texel
	int3 coord = GetLightprobeTexCoord(probeCoord, uint2(octX, octY), cascadeSlice);
	lightprobeData[coord] = encoded;

	// Write border texels for bilinear filtering
	uint probeTexSize = LIGHTPROBE_OCT_SIZE + 2;
	int2 probeBaseXY = int2(
		(probeCoord.x + probeCoord.y * ProbeAxisSize) * probeTexSize,
		probeCoord.z * probeTexSize);

	// Bottom border (y == 0)
	if (octY == 0) {
		// Mirror: the border texel at y=0 comes from the opposite octahedral edge
		float2 mirrorUV = float2(1.0 - octUV.x, -octUV.y + 1.0 / (float)LIGHTPROBE_OCT_SIZE);
		float3 mirrorDir = OctDecode(mirrorUV);
		float3 mirrorIrr = float3(0, 0, 0);

		[unroll]
		for (uint ms = 0; ms < SH_SIZE; ++ms) {
			int2 tc = GetProbeSHTexCoord(probeCoord, ms);
			int4 av = probeAverageTex[tc];
			float3 sc = float3(av.rgb) * invScale * invHistory;
			mirrorIrr += sc * SHBasis(ms, mirrorDir);
		}
		mirrorIrr = max(mirrorIrr, 0.0);
		lightprobeData[int3(probeBaseXY + int2(octX + 1, 0), cascadeSlice)] = EncodeRGBE9995(mirrorIrr);
	}

	// Top border (y == LIGHTPROBE_OCT_SIZE - 1)
	if (octY == LIGHTPROBE_OCT_SIZE - 1) {
		float2 mirrorUV = float2(1.0 - octUV.x, 1.0 + (1.0 - octUV.y));
		float3 mirrorDir = OctDecode(saturate(mirrorUV));
		float3 mirrorIrr = float3(0, 0, 0);

		[unroll]
		for (uint ms = 0; ms < SH_SIZE; ++ms) {
			int2 tc = GetProbeSHTexCoord(probeCoord, ms);
			int4 av = probeAverageTex[tc];
			float3 sc = float3(av.rgb) * invScale * invHistory;
			mirrorIrr += sc * SHBasis(ms, mirrorDir);
		}
		mirrorIrr = max(mirrorIrr, 0.0);
		lightprobeData[int3(probeBaseXY + int2(octX + 1, probeTexSize - 1), cascadeSlice)] = EncodeRGBE9995(mirrorIrr);
	}

	// Left border (x == 0)
	if (octX == 0) {
		float2 mirrorUV = float2(-octUV.x + 1.0 / (float)LIGHTPROBE_OCT_SIZE, 1.0 - octUV.y);
		float3 mirrorDir = OctDecode(mirrorUV);
		float3 mirrorIrr = float3(0, 0, 0);

		[unroll]
		for (uint ms = 0; ms < SH_SIZE; ++ms) {
			int2 tc = GetProbeSHTexCoord(probeCoord, ms);
			int4 av = probeAverageTex[tc];
			float3 sc = float3(av.rgb) * invScale * invHistory;
			mirrorIrr += sc * SHBasis(ms, mirrorDir);
		}
		mirrorIrr = max(mirrorIrr, 0.0);
		lightprobeData[int3(probeBaseXY + int2(0, octY + 1), cascadeSlice)] = EncodeRGBE9995(mirrorIrr);
	}

	// Right border (x == LIGHTPROBE_OCT_SIZE - 1)
	if (octX == LIGHTPROBE_OCT_SIZE - 1) {
		float2 mirrorUV = float2(1.0 + (1.0 - octUV.x), 1.0 - octUV.y);
		float3 mirrorDir = OctDecode(saturate(mirrorUV));
		float3 mirrorIrr = float3(0, 0, 0);

		[unroll]
		for (uint ms = 0; ms < SH_SIZE; ++ms) {
			int2 tc = GetProbeSHTexCoord(probeCoord, ms);
			int4 av = probeAverageTex[tc];
			float3 sc = float3(av.rgb) * invScale * invHistory;
			mirrorIrr += sc * SHBasis(ms, mirrorDir);
		}
		mirrorIrr = max(mirrorIrr, 0.0);
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, octY + 1), cascadeSlice)] = EncodeRGBE9995(mirrorIrr);
	}

	// Corner texels
	if (octX == 0 && octY == 0) {
		// Bottom-left corner: average of two edge neighbors
		lightprobeData[int3(probeBaseXY + int2(0, 0), cascadeSlice)] = encoded;
	}
	if (octX == LIGHTPROBE_OCT_SIZE - 1 && octY == 0) {
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, 0), cascadeSlice)] = encoded;
	}
	if (octX == 0 && octY == LIGHTPROBE_OCT_SIZE - 1) {
		lightprobeData[int3(probeBaseXY + int2(0, probeTexSize - 1), cascadeSlice)] = encoded;
	}
	if (octX == LIGHTPROBE_OCT_SIZE - 1 && octY == LIGHTPROBE_OCT_SIZE - 1) {
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, probeTexSize - 1), cascadeSlice)] = encoded;
	}
}

#endif  // MODE_STORE

// =========================================================================
#ifdef MODE_SCROLL
// =========================================================================

// Scrolls probe history data when a cascade moves.
// Shifts probe data by Scroll amount.
// Out-of-bounds probes get zeroed.
//
// Dispatch: globalID.x covers probes (up to ProbeAxisSize^3),
//           globalID.y covers SH bands (up to SH_SIZE).

[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID)
{
	// Thread mapping: globalID.x indexes into flat probe array,
	//                 globalID.y indexes SH band
	uint probeFlat = globalID.x;
	uint shIdx = globalID.y;

	uint totalProbes = (uint)(ProbeAxisSize * ProbeAxisSize * ProbeAxisSize);
	if (probeFlat >= totalProbes)
		return;
	if (shIdx >= SH_SIZE)
		return;

	uint3 probeCoord;
	probeCoord.x = probeFlat % (uint)ProbeAxisSize;
	probeCoord.y = (probeFlat / (uint)ProbeAxisSize) % (uint)ProbeAxisSize;
	probeCoord.z = probeFlat / (uint)(ProbeAxisSize * ProbeAxisSize);

	// Compute source probe coordinate (where this probe's data came from before scroll)
	int3 srcCoord = int3(probeCoord) + Scroll;

	int2 dstTexCoord = GetProbeSHTexCoord(probeCoord, shIdx);

	// Check if source is in bounds
	bool inBounds = all(srcCoord >= int3(0, 0, 0)) && all(srcCoord < int3(ProbeAxisSize, ProbeAxisSize, ProbeAxisSize));

	if (inBounds) {
		int2 srcTexCoord = GetProbeSHTexCoord(uint3(srcCoord), shIdx);

		// Copy all history slots for this SH band
		for (uint h = 0; h < HistorySize; ++h) {
			probeHistoryTex[int3(dstTexCoord, h)] = probeHistoryTex[int3(srcTexCoord, h)];
		}

		// Copy average (only do this once per probe, not per SH band)
		if (shIdx == 0) {
			for (uint s = 0; s < SH_SIZE; ++s) {
				int2 dstSH = GetProbeSHTexCoord(probeCoord, s);
				int2 srcSH = GetProbeSHTexCoord(uint3(srcCoord), s);
				probeAverageTex[dstSH] = probeAverageTex[srcSH];
			}
		}
	} else {
		// Zero out - this probe has no valid history after scroll
		for (uint h = 0; h < HistorySize; ++h) {
			probeHistoryTex[int3(dstTexCoord, h)] = int4(0, 0, 0, 0);
		}

		// Zero average (only once per probe)
		if (shIdx == 0) {
			for (uint s = 0; s < SH_SIZE; ++s) {
				int2 dstSH = GetProbeSHTexCoord(probeCoord, s);
				probeAverageTex[dstSH] = int4(0, 0, 0, 0);
			}
		}
	}
}

#endif  // MODE_SCROLL

// =========================================================================
#ifdef MODE_SCROLL_STORE
// =========================================================================

// Stores scrolled history data back into the lightprobe octahedral texture.
// This is essentially the same as MODE_STORE but run after MODE_SCROLL
// to update the octahedral map for probes that were shifted.

[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID)
{
	// Same mapping as MODE_STORE
	uint probeLinearX = globalID.x / LIGHTPROBE_OCT_SIZE;
	uint octX = globalID.x % LIGHTPROBE_OCT_SIZE;

	uint probeZ = globalID.y / LIGHTPROBE_OCT_SIZE;
	uint octY = globalID.y % LIGHTPROBE_OCT_SIZE;

	uint totalProbesXY = (uint)(ProbeAxisSize * ProbeAxisSize);
	if (probeLinearX >= totalProbesXY)
		return;
	if (probeZ >= (uint)ProbeAxisSize)
		return;

	uint3 probeCoord;
	probeCoord.x = probeLinearX % (uint)ProbeAxisSize;
	probeCoord.y = probeLinearX / (uint)ProbeAxisSize;
	probeCoord.z = probeZ;

	// Only update probes that were affected by the scroll
	// Check if this probe was out-of-bounds (would have been zeroed or copied)
	int3 srcCoord = int3(probeCoord) + Scroll;
	bool wasScrolled = any(Scroll != int3(0, 0, 0));
	if (!wasScrolled)
		return;

	// Compute direction from octahedral UV
	float2 octUV = (float2(octX, octY) + 0.5) / (float)LIGHTPROBE_OCT_SIZE;
	float3 dir = OctDecode(octUV);

	// Read SH average and evaluate irradiance
	float3 irradiance = float3(0, 0, 0);
	float invScale = 1.0 / (float)(1 << HISTORY_BITS);
	float invHistory = 1.0 / (float)HistorySize;

	[unroll]
	for (uint sh = 0; sh < SH_SIZE; ++sh) {
		int2 texCoord = GetProbeSHTexCoord(probeCoord, sh);
		int4 avgValue = probeAverageTex[texCoord];
		float3 shCoeff = float3(avgValue.rgb) * invScale * invHistory;
		float basis = SHBasis(sh, dir);
		irradiance += shCoeff * basis;
	}

	irradiance = max(irradiance, 0.0);
	uint encoded = EncodeRGBE9995(irradiance);

	uint cascadeSlice = Cascade * 2;
	int3 coord = GetLightprobeTexCoord(probeCoord, uint2(octX, octY), cascadeSlice);
	lightprobeData[coord] = encoded;

	// Write border texels (simplified: only update interior + immediate edges)
	uint probeTexSize = LIGHTPROBE_OCT_SIZE + 2;
	int2 probeBaseXY = int2(
		(probeCoord.x + probeCoord.y * ProbeAxisSize) * probeTexSize,
		probeCoord.z * probeTexSize);

	if (octY == 0) {
		lightprobeData[int3(probeBaseXY + int2(octX + 1, 0), cascadeSlice)] = encoded;
	}
	if (octY == LIGHTPROBE_OCT_SIZE - 1) {
		lightprobeData[int3(probeBaseXY + int2(octX + 1, probeTexSize - 1), cascadeSlice)] = encoded;
	}
	if (octX == 0) {
		lightprobeData[int3(probeBaseXY + int2(0, octY + 1), cascadeSlice)] = encoded;
	}
	if (octX == LIGHTPROBE_OCT_SIZE - 1) {
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, octY + 1), cascadeSlice)] = encoded;
	}

	// Corners
	if (octX == 0 && octY == 0)
		lightprobeData[int3(probeBaseXY, cascadeSlice)] = encoded;
	if (octX == LIGHTPROBE_OCT_SIZE - 1 && octY == 0)
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, 0), cascadeSlice)] = encoded;
	if (octX == 0 && octY == LIGHTPROBE_OCT_SIZE - 1)
		lightprobeData[int3(probeBaseXY + int2(0, probeTexSize - 1), cascadeSlice)] = encoded;
	if (octX == LIGHTPROBE_OCT_SIZE - 1 && octY == LIGHTPROBE_OCT_SIZE - 1)
		lightprobeData[int3(probeBaseXY + int2(probeTexSize - 1, probeTexSize - 1), cascadeSlice)] = encoded;
}

#endif  // MODE_SCROLL_STORE

#ifndef __LLF_COMMON_DEPENDENCY_HLSL__
#define __LLF_COMMON_DEPENDENCY_HLSL__

#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)
#define MAX_CLUSTER_LIGHTS 256

namespace LightFlags
{
	static const uint PortalStrict = (1 << 0);
	static const uint Shadow = (1 << 1);
	static const uint Simple = (1 << 2);

	static const uint Initialised = (1 << 8);
	static const uint Disabled = (1 << 9);
	static const uint InverseSquare = (1 << 10);
	static const uint Linear = (1 << 11);
}

struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

struct LightGrid
{
	uint offset;
	uint lightCount;
	uint pad0[2];
};

struct Light
{
	float3 color;
	float fade;
	float radius;
	float invRadius;
	float fadeZone;
	float sizeBias;
	float4 positionWS[2];
	uint4 roomFlags;
	uint lightFlags;
	uint shadowMapIndex;
	float2 pad0;
};

// ---------------------------------------------------------------------------
// LLFDEBUG visualization helpers — only compiled when the debug macro is set
// (pixel shaders only; compute shaders that include this file don't define it)
// ---------------------------------------------------------------------------
#if defined(LLFDEBUG)

// Accumulated per-pixel debug counters filled during the light loop.
// Declare with: LLFDebugInfo di = LLFDebugInfoInit();
// Update with:  LLFDebugAccumulate(di, light, shadowComponent, shadowCoverage);
struct LLFDebugInfo
{
	uint PLShadowCount;      // shadow-flagged lights seen (valid + overflow)
	float MinPLShadow;       // darkest shadow value (1.0 = none seen yet)
	uint UnshadowedPLCount;  // point/spot lights without shadow maps
	uint OverflowCount;      // shadow lights whose slot index exceeded ShadowMapSlots
	uint FirstShadowIndex;   // shadowMapIndex of first valid shadow light
	bool HasFirstShadow;
	uint SpotCount;  // ShadowLightParam.x == 0
	uint HemiCount;  // ShadowLightParam.x == 1
	uint OmniCount;  // ShadowLightParam.x == 2
};

LLFDebugInfo LLFDebugInfoInit()
{
	LLFDebugInfo di;
	di.PLShadowCount = 0;
	di.MinPLShadow = 1.0;
	di.UnshadowedPLCount = 0;
	di.OverflowCount = 0;
	di.FirstShadowIndex = 0;
	di.HasFirstShadow = false;
	di.SpotCount = 0;
	di.HemiCount = 0;
	di.OmniCount = 0;
	return di;
}

// Call once per clustered/strict light after sampling its shadow.
// shadowCoverage should be the hasCoverage output from GetShadowLightShadow.
// shadowType should be (uint)LightLimitFix::Shadows[light.shadowMapIndex].ShadowLightParam.x
// when light.shadowMapIndex < ShadowMapSlots, or any value otherwise (it won't be read).
void LLFDebugAccumulate(inout LLFDebugInfo di, Light light, float shadowComponent, bool shadowCoverage,
	uint shadowType)
{
	if (light.lightFlags & LightFlags::Shadow) {
		di.PLShadowCount++;
		if (shadowCoverage)
			di.MinPLShadow = min(di.MinPLShadow, shadowComponent);
		if (light.shadowMapIndex >= SharedData::lightLimitFixSettings.ShadowMapSlots) {
			di.OverflowCount++;
		} else {
			if (!di.HasFirstShadow) {
				di.FirstShadowIndex = light.shadowMapIndex;
				di.HasFirstShadow = true;
			}
			if (shadowType == 0)
				di.SpotCount++;
			else if (shadowType == 1)
				di.HemiCount++;
			else
				di.OmniCount++;
		}
	} else {
		di.UnshadowedPLCount++;
	}
}

// Returns the debug visualization color for this pixel.
// Callers supply the small set of per-shader-variant values:
//   mode0Color  — output for mode 0 (e.g. TurboColormap(strictLightsOverflow))
//   mode1Color  — output for mode 1 (e.g. TurboColormap(strictLightCount/15))
//   mode2Color  — output for mode 2 (e.g. TurboColormap(clusteredCount/MAX))
//   mode3Color  — output for mode 3 (e.g. float3(dirSoftShadow, dirDetailedShadow, 0))
//   lumaColor   — accumulated lighting color used as luma source for mode 8
float3 LLFDebugGetVizColor(LLFDebugInfo di,
	float3 mode0Color, float3 mode1Color, float3 mode2Color, float3 mode3Color,
	float3 lumaColor)
{
	uint mode = SharedData::lightLimitFixSettings.LightsVisualisationMode;

	if (mode == 0)
		return mode0Color;
	else if (mode == 1)
		return mode1Color;
	else if (mode == 2)
		return mode2Color;
	else if (mode == 3)
		return mode3Color;
	else if (mode == 4)
		return Color::TurboColormap((float)di.PLShadowCount / 8.0);
	else if (mode == 5)
		return float3(di.MinPLShadow, di.MinPLShadow, di.MinPLShadow);
	else if (mode == 6)
		return Color::TurboColormap((float)di.UnshadowedPLCount / 8.0);
	else if (mode == 7) {
		if (di.OverflowCount > 0)
			return float3(1.0, 0.0, 0.0);
		uint validCount = di.PLShadowCount - di.OverflowCount;
		uint slots = SharedData::lightLimitFixSettings.ShadowMapSlots;
		float t;
		if (validCount == 0)
			t = 0.0;
		else if (validCount <= 4)
			t = float(validCount - 1) / 3.0 * 0.3;
		else {
			uint extSlots = max(slots, 6u) - 5u;
			t = 0.3 + saturate(float(validCount - 5) / float(extSlots)) * 0.5;
		}
		return Color::TurboColormap(t);
	} else if (mode == 8) {
		float luma = dot(lumaColor, float3(0.2126, 0.7152, 0.0722));
		if (di.OverflowCount > 0)
			return float3(1.0, 0.0, 0.0);
		else if (!di.HasFirstShadow)
			return luma.xxx;
		float hue = frac(float(di.FirstShadowIndex) * 0.618033988);
		float3 rgb = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
		return rgb * luma;
	} else {
		// Mode 9 — light type visualization
		if (di.OverflowCount > 0)
			return float3(1.0, 0.0, 0.0);
		float scale = 1.0 / 4.0;
		float3 typeColor = float3(
			saturate(float(di.SpotCount) * scale),
			saturate(float(di.HemiCount) * scale),
			saturate(float(di.OmniCount) * scale));
		bool hasShadowLights = (di.SpotCount + di.HemiCount + di.OmniCount) > 0;
		if (!hasShadowLights)
			typeColor = saturate(float(di.UnshadowedPLCount) * scale) * 0.35;
		return typeColor;
	}
}

#endif  // defined(LLFDEBUG)

#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
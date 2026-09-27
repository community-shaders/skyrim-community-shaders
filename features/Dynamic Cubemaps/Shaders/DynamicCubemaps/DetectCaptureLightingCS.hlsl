#include "DynamicCubemaps/CaptureCommon.hlsli"

groupshared uint3 RegionCounts[4];
groupshared uint VisibleSamples;

[numthreads(16, 16, 1)] void main(uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
	if (groupIndex < 4)
		RegionCounts[groupIndex] = 0;
	if (groupIndex == 0)
		VisibleSamples = 0;
	GroupMemoryBarrierWithGroupSync();

	CaptureLightingState state = LightingState[0];
	uint captureBit = 1u << CaptureIndex;
	bool reset = ResetCapture != 0 || (state.PendingResetMask & captureBit) != 0 || state.Initialized == 0;
	if (!reset) {
		uint width, height, faces;
		DynamicCubemap.GetDimensions(width, height, faces);
		uint2 xy = uint2((groupThreadID.xy + 0.5) * float2(width, height) / 16.0);
		for (uint face = 0; face < 6; face++) {
			uint3 texel = uint3(xy, face);
			float3 position, color;
			float2 uv;
			if (!SampleCapture(texel, position, color, uv))
				continue;
			InterlockedAdd(VisibleSamples, 1);

			float4 history = DynamicCubemapRaw[texel];
			float4 historyPosition = DynamicCubemapPosition[texel];
			if (history.a < 0.9 || CaptureHistoryExpired(historyPosition.w) ||
				!CaptureGeometryMatches(AdjustCapturePosition(historyPosition.xyz), position))
				continue;

			float currentLuminance = Color::RGBToLuminance(color);
			float previousLuminance = Color::RGBToLuminance(history.rgb / history.a);
			if (!isfinite(previousLuminance) || max(currentLuminance, previousLuminance) < 0.001)
				continue;

			float deltaEV = log2(max(currentLuminance, 0.0001) / max(previousLuminance, 0.0001));
			uint region = (uv.x >= 0.5 ? 1u : 0u) + (uv.y >= 0.5 ? 2u : 0u);
			InterlockedAdd(RegionCounts[region].x, 1);
			if (deltaEV >= 1.0)
				InterlockedAdd(RegionCounts[region].y, 1);
			if (deltaEV <= -1.0)
				InterlockedAdd(RegionCounts[region].z, 1);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 0) {
		uint matched = 0;
		uint brighter = 0;
		uint darker = 0;
		uint brighterRegions = 0;
		uint darkerRegions = 0;
		for (uint region = 0; region < 4; region++) {
			uint3 counts = RegionCounts[region];
			matched += counts.x;
			brighter += counts.y;
			darker += counts.z;
			if (counts.x >= 8) {
				brighterRegions += counts.y * 4 >= counts.x * 3 ? 1u : 0u;
				darkerRegions += counts.z * 4 >= counts.x * 3 ? 1u : 0u;
			}
		}

		float3 ambient = Color::IrradianceToLinear(Color::Ambient(max(0.0, SharedData::GetAmbient(0.0))));
		float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0;
		float3 directional = Color::IrradianceToLinear(Color::DirectionalLight(max(0.0, SharedData::DirLightColor.rgb) / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult);
		float lightingLuminance = max(Color::RGBToLuminance(ambient + directional), 0.0001);
		bool lightingChanged = state.Initialized != 0 && abs(log2(lightingLuminance / max(state.ReferenceLuminance, 0.0001))) >= 1.0;
		bool captureChanged = matched >= 48 && matched * 2 >= VisibleSamples &&
		                      ((brighterRegions >= 2 && brighter * 4 >= matched * 3) || (darkerRegions >= 2 && darker * 4 >= matched * 3));
		if (!reset && (lightingChanged || captureChanged)) {
			state.PendingResetMask |= 3u;
			reset = true;
		}
		if (reset)
			state.ReferenceLuminance = lightingLuminance;
		state.PendingResetMask &= ~captureBit;
		state.Reset = reset ? 1u : 0u;
		state.Initialized = 1;
		LightingState[0] = state;
	}
}

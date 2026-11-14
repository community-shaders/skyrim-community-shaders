#ifndef PHYSICAL_SKY_LIGHTNING_HLSLI
#define PHYSICAL_SKY_LIGHTNING_HLSLI

#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

namespace Lightning
{
	bool HasSource()
	{
		return SharedData::physSkyData.lightningControl.w > 0.0 && SharedData::physSkyData.lightningPositionRadius.w > 0.0;
	}
	uint ActiveLane()
	{
		return (uint)SharedData::physSkyData.lightningPreparationControl.z;
	}
	bool HasAnySource()
	{
		return HasSource() || SharedData::physSkyData.lightningPreparationControl.w > 0.0;
	}
	float ActiveMask(float2 masks)
	{
		return ActiveLane() == 0u ? masks.x : masks.y;
	}
	float2 ResolveMasks(float2 history, float2 current)
	{
		const bool firstActive = ActiveLane() == 0u;
		const float2 previous = firstActive ? history : history.yx;
		const float2 currentMask = firstActive ? current : current.yx;
		const float active = !HasSource() ? 0.0 : (SharedData::physSkyData.lightningControl.y > 0.0 ? previous.x : currentMask.x);
		const float pending = SharedData::physSkyData.lightningPreparationControl.w <= 0.0 ? 0.0 :
		                                                                                     (SharedData::physSkyData.lightningPreparationControl.y > 0.0 ? previous.y : currentMask.y);
		return clamp(firstActive ? float2(active, pending) : float2(pending, active), 0.0, 65504.0);
	}

	float3 Color()
	{
		return SharedData::physSkyData.lightningColorIntensity.rgb *
		       (SharedData::physSkyData.lightningPhotometry.x * SharedData::physSkyData.lightningColorIntensity.w);
	}

	// The two spatial bases are independent of the animated flash intensity.
	// The local profile supplies cloud height, rather than a fixed world altitude.
	float SourceMask(float4 source, bool baseMode, float3 worldPosition, float height, float potential)
	{
		if (source.w <= 0.0)
			return 0.0;
		const float distance = length(worldPosition.xy - source.xy) / source.w;
		const float bottom = 100.0 * pow(1.0 - saturate(height * 10.0), 3.0) * pow(1.0 - saturate(distance * 0.25), 16.0);
		const float z = (worldPosition.z - source.z) * GAME_UNIT_TO_M;
		const float vertical = saturate((240.0 - z) * 0.025) * saturate((130.0 + z) * (1.0 / 175.0));
		const float elevated = 4000.0 * pow(vertical, 4.0) * pow(1.0 - saturate(distance), 6.0);
		const float cloud = saturate(potential * 9.0);
		// Normalize each authored shape to a peak of one: the UI value is cd/m^2,
		// not a multiplier on the empirical 100/4000 emission constants.
		const float baseMask = bottom * (1.0 - (1.0 - bottom * 0.015625) * cloud) / 156.25;
		const float embeddedMask = 0.3 * elevated * (1.0 - (1.0 - elevated * 0.009375) * cloud) / 45000.0;
		return saturate(baseMode ? baseMask : embeddedMask);
	}
	float2 CloudMask(float3 worldPosition, float height, float potential)
	{
		// Explicit components avoid dynamically indexed local writes in FXC's rolled cloud march.
		const float active = SourceMask(SharedData::physSkyData.lightningPositionRadius,
			SharedData::physSkyData.lightningPhotometry.w > 0.0, worldPosition, height, potential);
		const float pending = SourceMask(SharedData::physSkyData.lightningPreparationPositionRadius,
			SharedData::physSkyData.lightningPreparationControl.x > 0.0, worldPosition, height, potential);
		return ActiveLane() == 0u ? float2(active, pending) : float2(pending, active);
	}

	float4 Emitter(uint index)
	{
		return index == 0u ? SharedData::physSkyData.lightningPositionRadius : SharedData::physSkyData.lightningChannelEmitter;
	}

	float3 IncidentFrom(uint index, float3 worldPosition)
	{
		const float4 source = Emitter(index);
		const float3 offset = source.xyz - worldPosition;
		const float radius2 = source.w * source.w;
		// Equivalent finite emitter: projected solid angle tends to A/d^2 in the
		// far field and pi at the emitter. cd/m^2 times projected sr gives lux.
		const float luminance = index == 0u ? SharedData::physSkyData.lightningPhotometry.x : SharedData::physSkyData.lightningPhotometry.y;
		return HasSource() ? SharedData::physSkyData.lightningColorIntensity.rgb *
		                         (luminance * SharedData::physSkyData.lightningColorIntensity.w * 3.14159265 * radius2 / max(dot(offset, offset) + radius2, 1e-6)) :
		                     0.0;
	}

	float3 Incident(float3 worldPosition)
	{
		return IncidentFrom(0u, worldPosition) + IncidentFrom(1u, worldPosition);
	}
}
#endif

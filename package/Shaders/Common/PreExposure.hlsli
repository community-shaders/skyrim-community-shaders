#ifndef COMMON_PRE_EXPOSURE_HLSLI
#define COMMON_PRE_EXPOSURE_HLSLI

#include "Common/Permutation.hlsli"
#include "Common/SharedData.hlsli"

// Radiance storage only. Materials, light settings, transmittance and metering
// remain in their original units. CPU publishes identity outside linear lighting.
namespace PreExposure
{
#if defined(ENABLE_LL)
	static const float Scale = SharedData::PreExposure;
	static const float InverseScale = SharedData::RcpPreExposure;
	static const float HistoryScale = SharedData::PreExposureHistoryRatio;
#else
	static const float Scale = 1.0;
	static const float InverseScale = 1.0;
	static const float HistoryScale = 1.0;
#endif
	float Encode(float value) { return value * Scale; }
	float3 Encode(float3 value)
	{
		float3 encoded = value * Scale;
#if defined(ENABLE_LL)
		// These RGB writes target scene FP16. During exposure adaptation, keep
		// out-of-range highlights finite so they cannot poison FFT/history.
		// Limit only the stored value; scalar physical thresholds stay unbounded.
		encoded = clamp(encoded, -65504.0, 65504.0);
#endif
		return encoded;
	}
	float Decode(float value) { return value * InverseScale; }
	float3 Decode(float3 value) { return value * InverseScale; }
	float3 History(float3 value) { return value * HistoryScale; }

	// Reflection captures and menu models are lighting/UI data, not scene color.
	float3 EncodeScene(float3 value)
	{
		return !Permutation::RenderToUI && !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) ? Encode(value) : value;
	}
}

#endif

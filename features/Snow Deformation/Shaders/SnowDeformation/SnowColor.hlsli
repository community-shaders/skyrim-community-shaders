#ifndef __SNOW_COLOR_HLSLI__
#define __SNOW_COLOR_HLSLI__

#include "Common/SharedData.hlsli"
#include "Common/ColorManagement.hlsli"

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace SnowColor
{
	float3 SetAlbedo(float3 texel, bool isLinear)
	{
		return isLinear ? Color::LinearToPBRMaterial(ColorManagement::DiffuseToWorking(texel, true)) : ColorManagement::DiffuseToWorking(texel);
	}
}

namespace SnowIce
{
	static const uint kHistoryBins = 128;
	static const float kClockWrap = 1024.0;

	float3 AtCut(float a_date)
	{
		const float newest = floor(SharedData::snowDeformationSettings.IceClock);
		float age = newest - a_date;
		age -= age > kClockWrap - float(kHistoryBins) ? kClockWrap : 0.0;
		const float t = newest - clamp(age, 0.0, float(kHistoryBins - 1));
		const float t0 = floor(t);
		const uint b0 = uint(t0) & (kHistoryBins - 1);
		const uint b1 = (b0 + 1) & (kHistoryBins - 1);
		return lerp(SharedData::snowDeformationSettings.IceHistory[b0].xyz, SharedData::snowDeformationSettings.IceHistory[b1].xyz, t - t0);
	}
}
#endif

#endif

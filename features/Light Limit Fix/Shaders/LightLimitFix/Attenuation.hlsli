#ifndef __LLF_ATTENUATION_DEPENDENCY_HLSL__
#define __LLF_ATTENUATION_DEPENDENCY_HLSL__

#include "Common/Game.hlsli"

namespace LightLimitFix
{

#include "LightLimitFix/Common.hlsli"

	static const float SCALE = 0.8f;
	static const float METRES_TO_UNITS_SQ = METRES_TO_UNITS * METRES_TO_UNITS;
	static const float SCALED_UNITS_SQ = SCALE * METRES_TO_UNITS_SQ;

	float GetAttenuation(float distance, Light light)
	{
		float isEnabled = 1.0f - float((light.lightFlags & LightFlags::Disabled) != 0);
		float isInvSq = float((light.lightFlags & LightFlags::InverseSquare) != 0);

		float invSq = SCALED_UNITS_SQ * rcp(distance * distance + light.sizeBias);
		float t = saturate((light.radius - distance) * light.fadeZone);
		float fastSmoothstep = t * t * (3.0f - 2.0f * t);
		invSq *= fastSmoothstep;

		float intensityFactor = saturate(distance * light.invRadius);
		float reg = 1.0f - intensityFactor * intensityFactor;

		return lerp(reg, invSq, isInvSq) * isEnabled;
	}
}

#endif  //__LLF_ATTENUATION_DEPENDENCY_HLSL__

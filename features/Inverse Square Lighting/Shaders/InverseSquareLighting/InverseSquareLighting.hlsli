#include "Common/SharedData.hlsli"

namespace InverseSquareLighting
{
	static const float SCALE = 0.8f;
	static const float METRES_TO_UNITS = 70.f;
	static const float METRES_TO_UNITS_SQ = METRES_TO_UNITS * METRES_TO_UNITS;
	static const float FADE_ZONE = 0.175f;

	float GetAttenuation(float distance, LightLimitFix::Light light)
	{
		float isEnabled = 1.0f - float((light.lightFlags & LightLimitFix::LightFlags::Disabled) != 0);
		float isInvSq   = float((light.lightFlags & LightLimitFix::LightFlags::InverseSquare) != 0);

		float radiusFadeStart = light.radius - light.radius * FADE_ZONE;
		float invSq = (SCALE * METRES_TO_UNITS_SQ) / (distance * distance + SCALE * METRES_TO_UNITS_SQ);
		invSq *= smoothstep(light.radius, radiusFadeStart, distance);
		
		float intensityFactor = saturate(distance / light.radius);
		float reg = 1.0f - intensityFactor * intensityFactor;
		
		return lerp(reg, invSq, isInvSq) * isEnabled;
	}
}
#include "Common/SharedData.hlsli"

namespace InverseSquareLighting
{
	static const float SCALE = 0.8f;
	static const float METRES_TO_UNITS = 70.f;
	static const float METRES_TO_UNITS_SQ = METRES_TO_UNITS * METRES_TO_UNITS;
	static const float SCALED_UNITS_SQ = SCALE * METRES_TO_UNITS_SQ;
	static const float FADE_ZONE_BASE = 4.5f * SCALE * METRES_TO_UNITS;

	float GetAttenuation(float distance, LightLimitFix::Light light)
	{
		float isEnabled = 1.0f - float((light.lightFlags & LightLimitFix::LightFlags::Disabled) != 0);
		float isInvSq = float((light.lightFlags & LightLimitFix::LightFlags::InverseSquare) != 0);
		float invRadius = rcp(light.radius);

		float invSq = SCALED_UNITS_SQ * rcp(distance * distance + SCALED_UNITS_SQ);
		float fadeZone = saturate(FADE_ZONE_BASE * invRadius);
		float t = saturate((light.radius - distance) * rcp(light.radius * fadeZone));
		float fastSmoothstep = t * t * (3.0f - 2.0f * t);
		invSq *= fastSmoothstep;

		float intensityFactor = saturate(distance * invRadius);
		float reg = 1.0f - intensityFactor * intensityFactor;

		return lerp(reg, invSq, isInvSq) * isEnabled;
	}
}
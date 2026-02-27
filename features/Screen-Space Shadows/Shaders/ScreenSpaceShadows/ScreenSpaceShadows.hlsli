#include "Common/Math.hlsli"

namespace ScreenSpaceShadows
{
	Texture2D<unorm half> ScreenSpaceShadowsTexture : register(t45);

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise, uint eyeIndex)
	{
#if defined(VR)
		// Depth-weighted rotated blur to reduce per-eye noise before stereo sync
		noise *= Math::TAU;

		half2x2 rotationMatrix = half2x2(cos(noise), sin(noise), -sin(noise), cos(noise));

		float weight = 0;
		float shadow = 0;

		int3 centerCoord = SharedData::ConvertUVToSampleCoord(uv, eyeIndex);
		float viewZ = SharedData::GetScreenDepth(SharedData::DepthTexture.Load(centerCoord).x);

		static const float2 BlurOffsets[4] = {
			float2(0.381664f, 0.89172f),
			float2(0.491409f, 0.216926f),
			float2(0.937803f, 0.734825f),
			float2(0.00921659f, 0.0562151f),
		};

		for (uint i = 0; i < 4; i++) {
			float2 offset = mul(BlurOffsets[i], rotationMatrix) * 0.0025;

			float2 sampleUV = saturate(uv + offset);
			int3 sampleCoord = SharedData::ConvertUVToSampleCoord(sampleUV, eyeIndex);

			float linearDepth = SharedData::GetScreenDepth(SharedData::DepthTexture.Load(sampleCoord).x);

			float attenuation = 1.0 - saturate(100.0 * abs(linearDepth - viewZ) / viewZ);
			if (attenuation > 0.0) {
				shadow += ScreenSpaceShadowsTexture.Load(sampleCoord).x * attenuation;
				weight += attenuation;
			}
		}

		if (weight > 0.0)
			shadow /= weight;
		else
			shadow = ScreenSpaceShadowsTexture.Load(centerCoord).x;

		return shadow;
#else
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition.xy + 0.5f), 0)).x;
#endif
	}
}
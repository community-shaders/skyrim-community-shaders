// Screen Space Shadows consumption helper.
// Non-VR: depth-weighted 4-sample Poisson blur for spatial denoising.
// VR: direct Load — the Poisson blur's per-pixel noise rotation is
// screen-position-dependent, causing shadows to shift on camera movement.
// Without TAA to average out the rotation noise, the instability hits
// the final output directly. Matches the stable v1.2 VR implementation.

#include "Common/Math.hlsli"

namespace ScreenSpaceShadows
{
	Texture2D<unorm half> ScreenSpaceShadowsTexture : register(t45);

	float4 GetBlurWeights(float4 depths, float centerDepth)
	{
		centerDepth += 1.0;
		float depthSharpness = saturate((1024.0 * 1024.0) / (centerDepth * centerDepth));
		float4 depthDifference = (depths - centerDepth) * depthSharpness;
		return exp2(-depthDifference * depthDifference);
	}

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise, uint eyeIndex)
	{
#if defined(VR)
		// VR: direct sample, no spatial blur. The Poisson blur's per-pixel noise
		// rotation is screen-position-dependent — camera movement changes the
		// rotation angle for the same world surface, causing shadows to visually
		// shift. Without TAA to average out the rotation noise, the per-frame
		// instability hits the final output directly. Direct Load avoids this.
		// Matches the stable v1.2 VR implementation.
		return ScreenSpaceShadowsTexture.Load(int3(screenPosition.xy, 0));
#else
		// Flat: depth-weighted 4-sample Poisson blur for spatial denoising.
		// Rotated per-pixel by screen-space noise to break structured patterns.
		// TAA averages out the rotation noise across frames.
		noise *= Math::TAU;

		float2x2 rotationMatrix = float2x2(cos(noise), sin(noise), -sin(noise), cos(noise));

		float4 shadowSamples = 0;
		float4 depthSamples = 0;

#	if defined(DEFERRED) && !defined(DO_ALPHA_TEST)
		depthSamples[0] = screenPosition.z;
#	else
		depthSamples[0] = SharedData::DepthTexture.Load(int3(screenPosition.xy, 0)).x;
#	endif

		shadowSamples[0] = ScreenSpaceShadowsTexture.Load(int3(screenPosition.xy, 0));

		static const float2 BlurOffsets[3] = {
			float2(-0.6720635096678028f, 0.6601738628451107f),
			float2(0.6110340335380645f, 0.5269905984201742f),
			float2(0.20239029763403027f, -0.7841160574831084f),
		};

		[unroll] for (uint i = 1; i < 4; i++)
		{
			float2 offset = mul(BlurOffsets[i - 1], rotationMatrix) * 0.0025;

			float2 sampleUV = uv + offset;
			sampleUV = saturate(sampleUV);

			int3 sampleCoord = SharedData::ConvertUVToSampleCoord(sampleUV, eyeIndex);

			depthSamples[i] = SharedData::DepthTexture.Load(sampleCoord).x;
			shadowSamples[i] = ScreenSpaceShadowsTexture.Load(sampleCoord);
		}

		depthSamples = SharedData::GetScreenDepths(depthSamples);

		float4 blurWeights = GetBlurWeights(depthSamples, depthSamples[0]);
		float shadow = dot(shadowSamples, blurWeights);

		float blurWeightsTotal = dot(blurWeights, 1.0);
		[flatten] if (blurWeightsTotal > 0.0)
			shadow = shadow / blurWeightsTotal;

		return shadow;
#endif
	}
}

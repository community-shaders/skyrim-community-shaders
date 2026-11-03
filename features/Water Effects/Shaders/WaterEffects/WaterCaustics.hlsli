Texture2D<float4> WaterCaustics : register(t65);

namespace WaterEffects
{
	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed) + (uv * tiling));
	}

	float SampleCaustics(float2 uv, float2 uvDdx, float2 uvDdy)
	{
		return WaterCaustics.SampleGrad(SampColorSampler, uv, uvDdx, uvDdy).x;
	}

	// Approximate wavelength-dependent refraction by offsetting red/blue around green.
	float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset, float2 uvDdx, float2 uvDdy, float2 dispersionDdx, float2 dispersionDdy)
	{
		float center = SampleCaustics(uv, uvDdx, uvDdy);
		float3 dispersed = float3(
			SampleCaustics(uv - dispersionOffset * 0.75, uvDdx - dispersionDdx * 0.75, uvDdy - dispersionDdy * 0.75),
			center,
			SampleCaustics(uv + dispersionOffset, uvDdx + dispersionDdx, uvDdy + dispersionDdy));
		return lerp(center.xxx, dispersed, 0.5);
	}

	float3 ComputeCaustics(float4 waterData, float3 worldPosition)
	{
		float3 result = 1.0.xxx;
		float causticsDistToWater = waterData.w - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);

		// Gradients come from the unwrapped UV and are taken outside the branch: PanCausticsUV's frac() would put a
		// mip seam at every wrap, and screen derivatives are undefined in divergent control flow.
		float2 causticsUV = (worldPosition.xy + FrameBuffer::CameraPosAdjust.xy) * 0.005;
		float dispersionAmount = 0.025 * shoreFactorCaustics * saturate(causticsDistToWater / 256.0);
		float2 dispersionOffset = float2(0.6, 0.8) * dispersionAmount;
		float2 causticsUVDdx = ddx(causticsUV);
		float2 causticsUVDdy = ddy(causticsUV);
		float2 dispersionDdx = float2(0.6, 0.8) * ddx(dispersionAmount);
		float2 dispersionDdy = float2(0.6, 0.8) * ddy(dispersionAmount);

		if (shoreFactorCaustics > 0.0) {
			float causticsFade = 1.0 - saturate(causticsDistToWater / 1024.0);
			causticsFade *= causticsFade;

			float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0);
			float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5);

			const float3 causticsHigh =
				(causticsFade > 0.0) ? (min(SampleCausticsDispersion(causticsUV1, dispersionOffset, causticsUVDdx, causticsUVDdy, dispersionDdx, dispersionDdy), SampleCausticsDispersion(causticsUV2, dispersionOffset, causticsUVDdx * -0.5, causticsUVDdy * -0.5, dispersionDdx, dispersionDdy)) * 4.0) : 1.0.xxx;

			causticsUV *= 0.5;
			dispersionOffset *= 0.5;
			causticsUVDdx *= 0.5;
			causticsUVDdy *= 0.5;
			dispersionDdx *= 0.5;
			dispersionDdy *= 0.5;

			causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0);
			causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5);

			const float3 causticsLow =
				(causticsFade < 1.0) ? (min(SampleCausticsDispersion(causticsUV1, dispersionOffset, causticsUVDdx, causticsUVDdy, dispersionDdx, dispersionDdy), SampleCausticsDispersion(causticsUV2, dispersionOffset, causticsUVDdx * -0.5, causticsUVDdy * -0.5, dispersionDdx, dispersionDdy)) * 4.0) : 1.0.xxx;

			const float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
			result = lerp(1.0.xxx, caustics, shoreFactorCaustics);
		}

		return result;
	}
}

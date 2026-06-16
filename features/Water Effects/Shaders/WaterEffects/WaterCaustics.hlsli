Texture2D<float4> WaterCaustics : register(t65);

namespace WaterEffects
{
	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed) + (uv * tiling));
	}

	float SampleCaustics(float2 uv)
	{
		return WaterCaustics.Sample(SampColorSampler, uv).x;
	}

	// Approximate wavelength-dependent refraction by offsetting red/blue around green.
	float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset)
	{
		float center = SampleCaustics(uv);
		float3 dispersed = float3(
			SampleCaustics(uv - dispersionOffset * 0.75),
			center,
			SampleCaustics(uv + dispersionOffset));
		return lerp(center.xxx, dispersed, 0.5);
	}

	float3 ComputeCaustics(float4 waterData, float3 worldPosition)
	{
		float causticsDistToWater = waterData.w - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);

		if (shoreFactorCaustics > 0.0) {
			float causticsFade = 1.0 - saturate(causticsDistToWater / 1024.0);
			causticsFade *= causticsFade;

			float2 causticsUV = (worldPosition.xy + FrameBuffer::CameraPosAdjust.xy) * 0.005;
			float2 dispersionOffset = float2(0.6, 0.8) * (0.025 * shoreFactorCaustics * saturate(causticsDistToWater / 256.0));

			float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0);
			float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5);

			float3 causticsHigh = 1.0.xxx;
			if (causticsFade > 0.0)
				causticsHigh = min(SampleCausticsDispersion(causticsUV1, dispersionOffset), SampleCausticsDispersion(causticsUV2, dispersionOffset)) * 4.0;

			causticsUV *= 0.5;
			dispersionOffset *= 0.5;

			causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0);
			causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5);

			float3 causticsLow = 1.0.xxx;
			if (causticsFade < 1.0)
				causticsLow = min(SampleCausticsDispersion(causticsUV1, dispersionOffset), SampleCausticsDispersion(causticsUV2, dispersionOffset)) * 4.0;

			float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
			return lerp(1.0.xxx, caustics, shoreFactorCaustics);
		}

		return 1.0.xxx;
	}
}

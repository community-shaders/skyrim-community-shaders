Texture2D<float4> WaterCaustics : register(t65);

namespace WaterEffects
{
	/** @brief Panned caustics UV, unwrapped; wrap it with WrapCausticsUV before sampling. */
	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return (float2(1, 0) * SharedData::Timer * speed) + (uv * tiling);
	}

	/**
	 * @brief frac() with the integer part taken from the quad's top-left pixel.
	 * The UV stays continuous inside each 2x2 quad, so implicit derivatives see no wrap seam.
	 */
	float4 WrapCausticsUV(float4 uv, float2 quadPixel)
	{
		return uv - Math::QuadReadTopLeft(floor(uv), quadPixel);
	}

	float SampleCaustics(float2 uv)
	{
		return WaterCaustics.SampleBias(SampColorSampler, uv, SharedData::MipBias).x;
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

	/** @brief Caustics multiplier for the directional light. @param pixelPos SV_Position.xy. */
	float3 ComputeCaustics(float4 waterData, float3 worldPosition, float2 pixelPos)
	{
		float3 result = 1.0.xxx;
		float causticsDistToWater = waterData.w - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);

		// Wrapped before the branch, since QuadReadTopLeft needs the whole quad active.
		float2 quadPixel = Math::QuadPixel(pixelPos);
		float2 causticsUV = (worldPosition.xy + FrameBuffer::CameraPosAdjust.xy) * 0.005;
		float4 causticsHighUV = WrapCausticsUV(float4(PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0), PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5)), quadPixel);
		causticsUV *= 0.5;
		float4 causticsLowUV = WrapCausticsUV(float4(PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0), PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5)), quadPixel);

		// Quads that diverge here sit at the shoreline, where shoreFactorCaustics is near 0 and hides the samples.
		if (shoreFactorCaustics > 0.0) {
			float causticsFade = 1.0 - saturate(causticsDistToWater / 1024.0);
			causticsFade *= causticsFade;

			float2 dispersionOffset = float2(0.6, 0.8) * (0.025 * shoreFactorCaustics * saturate(causticsDistToWater / 256.0));

			const float3 causticsHigh =
				(causticsFade > 0.0) ? (min(SampleCausticsDispersion(causticsHighUV.xy, dispersionOffset), SampleCausticsDispersion(causticsHighUV.zw, dispersionOffset)) * 4.0) : 1.0.xxx;

			dispersionOffset *= 0.5;

			const float3 causticsLow =
				(causticsFade < 1.0) ? (min(SampleCausticsDispersion(causticsLowUV.xy, dispersionOffset), SampleCausticsDispersion(causticsLowUV.zw, dispersionOffset)) * 4.0) : 1.0.xxx;

			const float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
			result = lerp(1.0.xxx, caustics, shoreFactorCaustics);
		}

		return result;
	}
}

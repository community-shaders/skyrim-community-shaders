Texture2DArray<float> SnowShadowAtlas : register(t22);
SamplerComparisonState SnowShadowCmpSampler : register(s2);

struct PointShadowLight
{
	column_major float4x4 LightTransform;
	uint SliceIndex;
	uint LightType;
	float ShadowRadius;
	float padPSL;
};
StructuredBuffer<PointShadowLight> PointShadowLights : register(t38);
Texture2DArray<float> SnowPointShadowAtlas : register(t39);

namespace SnowShadow
{
	float SampleCascadeCmp(float3 posLS, uint atlasSlice)
	{
		return SnowShadowAtlas.SampleCmpLevelZero(SnowShadowCmpSampler, float3(posLS.xy, atlasSlice), posLS.z);
	}

	float SampleCascadePCF(float3 posLS, uint atlasSlice, float2 texel, float a_spread)
	{
		float shadow = SampleCascadeCmp(posLS, atlasSlice);
		const float2 kTaps[4] = { { 1.4, 0.4 }, { -0.4, 1.4 }, { -1.4, -0.4 }, { 0.4, -1.4 } };
		[unroll] for (uint tapI = 0; tapI < 4; tapI++)
			shadow += SampleCascadeCmp(float3(posLS.xy + kTaps[tapI] * texel * a_spread, posLS.z), atlasSlice);
		return shadow * 0.2;
	}

	float GetCascadeShadow(float3 positionRel, float3 normalWS, float a_spread, uint2 a_atlasSlices)
	{
		DirectionalShadowLightData sd = DirectionalShadowLights[0];

		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionRel));

		float result = 1.0;
		[branch] if (shadowMapDepth < sd.EndSplitDistances.y)
		{
			float3 positionWS = positionRel + FrameBuffer::CameraPosAdjust.xyz + normalWS * 3.0;

			float atlasW, atlasH, atlasSlices;
			SnowShadowAtlas.GetDimensions(atlasW, atlasH, atlasSlices);
			float2 texel = 1.0 / float2(atlasW, atlasH);

			float cascadeSelect = saturate((shadowMapDepth - sd.StartSplitDistances.y) / (sd.EndSplitDistances.x - sd.StartSplitDistances.y));
			uint primaryCascade = uint(cascadeSelect);

			float3 posLS = mul(sd.ShadowProj[primaryCascade], float4(positionWS, 1)).xyz;
			posLS.xy = saturate(posLS.xy);
			posLS.z -= 0.0008 * (primaryCascade + 1.0);
			float shadow = SampleCascadePCF(posLS, primaryCascade == 0 ? a_atlasSlices.x : a_atlasSlices.y, texel, a_spread);

			const float blendW = primaryCascade == 0 ? smoothstep(0, 1, cascadeSelect) : 0.0;
			[branch] if (blendW > 0.0)
			{
				posLS = mul(sd.ShadowProj[1], float4(positionWS, 1)).xyz;
				posLS.xy = saturate(posLS.xy);
				posLS.z -= 0.0016;
				float shadowBlend = SampleCascadePCF(posLS, a_atlasSlices.y, texel, a_spread);
				shadow = lerp(shadow, shadowBlend, blendW);
			}

			float fade = saturate(shadowMapDepth / sd.EndSplitDistances.y);
			result = lerp(1.0, shadow, 1.0 - pow(fade * fade, 8));
		}
		return result;
	}

	static const float kSssNearFloorStart = 2500.0;
	static const float kSssNearFloorEnd = 4000.0;

	float GetSssHandoff(float viewDist)
	{
		float fade = saturate(viewDist / max(DirectionalShadowLights[0].EndSplitDistances.y, 1.0));
		fade = pow(fade * fade, 8);
		return fade * smoothstep(kSssNearFloorStart, kSssNearFloorEnd, viewDist);
	}

	static const float kSpotShadowBias = 0.0015;
	static const float kRadialShadowBias = 0.003;

	float SamplePointCmp(float2 uv, uint slice, float cmp)
	{
		return SnowPointShadowAtlas.SampleCmpLevelZero(SnowShadowCmpSampler, float3(uv, slice), cmp);
	}

	float GetPointLightShadow(float3 positionWS, uint shadowLightIndex, float lightRadius)
	{
		PointShadowLight sl = PointShadowLights[shadowLightIndex];
		lightRadius = sl.ShadowRadius > 1.0 ? sl.ShadowRadius : lightRadius;
		float vis = 1.0;
		[branch] if (sl.LightType == 1)
		{
			float4 positionLS = mul(sl.LightTransform, float4(positionWS, 1.0));
			[branch] if (positionLS.w > 1e-4)
			{
				positionLS.xyz /= positionLS.w;
				float2 uv = positionLS.xy * 0.5 + 0.5;
				vis = SamplePointCmp(uv, sl.SliceIndex, positionLS.z - kSpotShadowBias);
				vis -= pow(2.0 * length(0.5 * positionLS.xy), 8.0) * vis;
			}
		}
		else if (sl.LightType == 2)
		{
			float4 unadjusted = mul(sl.LightTransform, float4(positionWS, 1.0));
			[branch] if (unadjusted.z * 0.5 + 0.5 >= 0.0)
			{
				float3 positionLS = unadjusted.xyz / unadjusted.w;
				float3 lightDirection = normalize(normalize(positionLS) + float3(0, 0, 1));
				float2 uv = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
				float cmp = saturate(length(positionLS) / lightRadius) - kRadialShadowBias;
				vis = SamplePointCmp(uv, sl.SliceIndex, cmp);
			}
		}
		else if (sl.LightType == 3)
		{
			float3 positionLS = mul(sl.LightTransform, float4(positionWS, 1.0)).xyz;
			bool lowerHalf = positionLS.z * 0.5 + 0.5 < 0.0;
			float3 lightDirection = normalize(normalize(positionLS) + (lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1)));
			float2 uv = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			uv.y = lowerHalf ? 1.0 - 0.5 * uv.y : 0.5 * uv.y;
			float cmp = saturate(length(positionLS) / lightRadius) - kRadialShadowBias;
			vis = SamplePointCmp(uv, sl.SliceIndex, cmp);
		}
		return saturate(vis);
	}
}

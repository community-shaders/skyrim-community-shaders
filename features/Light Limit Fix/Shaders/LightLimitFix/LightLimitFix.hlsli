
namespace LightLimitFix
{

#include "LightLimitFix/Common.hlsli"

	static const float DirectionalBias = 0.5f * (0.00025f) / 3.0f;

	// Shadow Radius for PCF
	static const float PCFRadius2D = 0.002;

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
		Light StrictLights[15];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3

	bool GetClusterIndex(in float2 uv, in float z, inout uint clusterIndex)
	{
		const uint3 clusterSize = SharedData::lightLimitFixSettings.ClusterSize.xyz;

		if (!FrameBuffer::FrameParams.y)  // Fix first person lights
			uv = 0.5;

		z = max(z, SharedData::CameraData.y);

		uint clusterZ = log(z / SharedData::CameraData.y) * clusterSize.z / log(SharedData::CameraData.x / SharedData::CameraData.y);
		uint3 cluster = uint3(uint2(uv * clusterSize.xy), clusterZ);

		// Bounds validation to prevent out-of-range cluster indices
		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

	bool IsLightIgnored(Light light)
	{
		bool lightIgnored = false;
		if ((light.lightFlags & LightFlags::PortalStrict) && RoomIndex >= 0) {
			lightIgnored = true;
			int roomIndex = RoomIndex;
			[unroll] for (int flagsIndex = 0; flagsIndex < 4; ++flagsIndex)
			{
				if (roomIndex < 32) {
					if (((light.roomFlags[flagsIndex] >> roomIndex) & 1) == 1) {
						lightIgnored = false;
					}
					break;
				}
				roomIndex -= 32;
			}
		}
		return lightIgnored;
	}

	struct ShadowLightData
	{
		column_major float4x4 ShadowProj;
		column_major float4x4 InvShadowProj;
		float4 ShadowLightParam;
	};

	StructuredBuffer<ShadowLightData> ShadowsLights : register(t100);
	Texture2DArray<float> ShadowMaps : register(t101);

	float GetDirectionalShadow(float3 worldPosition, float3 worldPositionWS, float2x2 rotationMatrix, uint eyeIndex)
	{
		DirectionalShadowLightData shadowLightData = DirectionalShadowLights[0];

		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(worldPosition, eyeIndex));

		if (shadowMapDepth > shadowLightData.EndSplitDistances.y)
			return 1.0;

		float fadeFactor = 1.0 - pow(saturate(dot(worldPosition.xyz, worldPosition.xyz) / shadowLightData.EndSplitDistances.y), 8);

		// Compute cascade blend factor
		float cascadeSelect = smoothstep(shadowLightData.StartSplitDistances.y, shadowLightData.EndSplitDistances.x, shadowMapDepth);

		// Determine which cascade(s) to sample
		uint primaryCascade = cascadeSelect;
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float3 positionLS = mul(shadowLightData.ShadowProj[primaryCascade], float4(worldPositionWS, 1)).xyz;
		positionLS.z -= DirectionalBias;

		// Sample primary cascade
		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
			float2 sampleUV = positionLS.xy + sampleOffset * PCFRadius2D;
			shadow += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), primaryCascade)) > positionLS.z), 0.25);
		}

		shadow /= 8.0;

		// Blend with secondary cascade if needed
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(shadowLightData.ShadowProj[secondaryCascade], float4(worldPositionWS, 1)).xyz;
			positionLS.z -= DirectionalBias;

			float shadowBlend = 0.0;

			[unroll] for (int i = 0; i < 8; i++)
			{
				float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
				float2 sampleUV = positionLS.xy + sampleOffset * PCFRadius2D;
				shadowBlend += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), secondaryCascade)) > positionLS.z), 0.25);
			}

			shadowBlend /= 8.0;

			shadow = lerp(shadow, shadowBlend, cascadeSelect);
		}

		return lerp(shadow, 1.0, fadeFactor);
	}

	float GetDirectionalShadow(float3 worldPosition, float3 worldPositionWS, float2x2 rotationMatrix)
	{
		return GetDirectionalShadow(worldPosition, worldPositionWS, rotationMatrix, 0);
	}

	float SampleShadowGather(uint shadowIndex, float2 uv, float receiverDepth)
	{
		float4 samples = ShadowMaps.GatherRed(LinearSampler, float3(uv, shadowIndex));
		return dot(float4(samples > receiverDepth), 0.25);
	}

	float GetSpotlightShadow(ShadowLightData shadowLightData, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		positionLS.xyz /= positionLS.w;

		positionLS.xy = positionLS.xy * 0.5 + 0.5;

		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
			float2 sampleUV = positionLS.xy + sampleOffset * PCFRadius2D;
			shadow += SampleShadowGather(shadowIndex, sampleUV, positionLS.z);
		}

		return shadow / 8.0;
	}

	float SampleParaboloidShadow(uint shadowIndex, float2 sampleUV, float depth, float2x2 rotationMatrix)
	{
		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 offset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * PCFRadius2D;
			float2 uv = sampleUV + offset;

			// Clamp to the correct paraboloid half
			uv.y = (sampleUV.y >= 0.5) ? max(uv.y, 0.5) : min(uv.y, 0.5);

			shadow += SampleShadowGather(shadowIndex, uv, depth);
		}

		return shadow / 8.0;
	}

	float GetOmnidirectionalShadow(ShadowLightData shadowLightData, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		bool lowerHalf = positionLS.z < 0;

		// Hemisphere-only early out
		if (!lowerHalf && positionLS.z <= 0)
			return 1.0;

		positionLS.xyz /= positionLS.w;

		float3 posOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalize(positionLS.xyz) + posOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		float depth = saturate(length(positionLS.xyz) / shadowLightData.ShadowLightParam.y);
		depth -= shadowLightData.ShadowLightParam.z;

		return SampleParaboloidShadow(shadowIndex, sampleUV, depth, rotationMatrix);
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPositionWS, float2x2 rotationMatrix, out bool hasCoverage)
	{
		hasCoverage = true;  // default: paraboloid lights always sample

		ShadowLightData shadowLightData = ShadowsLights[shadowIndex];

		[flatten] if (shadowLightData.ShadowLightParam.y == 0) return 1.0;
		[flatten] if (shadowLightData.ShadowLightParam.y < 0) return 0.0;

		float4 positionLS = mul(shadowLightData.ShadowProj, float4(worldPositionWS, 1));

		[branch] if (shadowLightData.ShadowLightParam.x == 0)
		{
			float shadowBaseVisibility = GetSpotlightShadow(shadowLightData, shadowIndex, positionLS, rotationMatrix);
			positionLS.xyz /= positionLS.w;

			float spotFalloff = saturate(1.0 - dot(positionLS.xy, positionLS.xy));

			return shadowBaseVisibility * spotFalloff;
		}

		return GetOmnidirectionalShadow(shadowLightData, shadowIndex, positionLS, rotationMatrix);
	}
}

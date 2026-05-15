
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

	StructuredBuffer<ShadowLightData> Shadows : register(t100);
	Texture2DArray<float> ShadowMaps : register(t101);
	Texture2DArray<float> DirectionalShadowCascades : register(t99);

	// engineMaskShadow: the engine's pre-rendered shadow-mask sample at this
	// pixel (TexShadowMaskSampler.Load(int3(Position.xy, 0)).x). The engine
	// renders all four cascades into that mask using its native sampling.
	// LLF's DirectionalShadowLightData cbuffer only carries the first two
	// cascade matrices (ShadowProj[2] / EndSplitDistances.xy), so this
	// function only has cascade 0/1 detail available. Past EndSplitDistances.y
	// we fall through to the engine mask -- previously the function returned
	// 1.0 (fully lit) there, which produced a visible shadow termination
	// plane anchored to camera-relative depth that swept across world
	// geometry on HMD rotation in VR.
	float GetDirectionalShadow(float3 worldPosition, float3 worldPositionWS, float2x2 rotationMatrix, uint eyeIndex, float engineMaskShadow)
	{
		DirectionalShadowLightData shadowLightData = DirectionalShadowLights[0];

		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(worldPosition, eyeIndex));

		// Past cascade 1 -- defer to the engine's 4-cascade mask.
		if (shadowMapDepth > shadowLightData.EndSplitDistances.y)
			return engineMaskShadow;

		// Blend from LLF PCF deep in cascade 1 toward the engine mask as we
		// approach cascade 1's far edge, avoiding a hard discontinuity at the
		// boundary where LLF stops and engine sampling takes over.
		//
		// Previous formula used `dot(worldPosition, worldPosition) /
		// EndSplitDistances.y` -- dimensionally wrong (length^2 / length)
		// AND inverted (close pixels got engineMaskShadow, far got LLF).
		// Because `worldPosition` is camera-relative in Skyrim's vertex
		// output, that produced a visible ~sqrt(EndSplitDistances.y)-radius
		// ring around the camera that moved with the player -- a clear
		// HMD-tracked artifact in VR. Switching to linear `shadowMapDepth`
		// and reversing the blend direction makes the handoff a smooth
		// world-anchored transition at the cascade boundary.
		float fadeFactor = smoothstep(shadowLightData.EndSplitDistances.y * 0.8,
			shadowLightData.EndSplitDistances.y,
			shadowMapDepth);

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

		// Within cascade 1's far edge, blend from LLF's PCF result toward
		// the engine mask -- avoids a hard discontinuity at the cascade
		// boundary where LLF stops and engine sampling takes over.
		return lerp(shadow, engineMaskShadow, fadeFactor);
	}

	float GetDirectionalShadow(float3 worldPosition, float3 worldPositionWS, float2x2 rotationMatrix, uint eyeIndex)
	{
		// Convenience overload: lit fallback when the caller doesn't have
		// the engine mask available. Preserves the previous behaviour for
		// any caller that pre-dates the engineMaskShadow parameter.
		return GetDirectionalShadow(worldPosition, worldPositionWS, rotationMatrix, eyeIndex, 1.0);
	}

	float GetDirectionalShadow(float3 worldPosition, float3 worldPositionWS, float2x2 rotationMatrix)
	{
		return GetDirectionalShadow(worldPosition, worldPositionWS, rotationMatrix, 0, 1.0);
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
		positionLS.z -= shadowLightData.ShadowLightParam.z;

		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
			float2 sampleUV = positionLS.xy + sampleOffset * PCFRadius2D;
			shadow += SampleShadowGather(shadowIndex, sampleUV, positionLS.z);
		}

		return shadow / 8.0;
	}

	// PCF sample around a paraboloid UV.
	//   isDualParaboloid = true  : the slice contains two stacked paraboloids
	//                              (omni: upper in y∈[0,0.5], lower in y∈[0.5,1]).
	//                              Clamp PCF samples to the originating half so we
	//                              don't bleed across the seam.
	//   isDualParaboloid = false : the slice contains a single paraboloid filling
	//                              the whole y∈[0,1] (hemi). No clamping needed —
	//                              the entire slice is valid shadow data.
	float SampleParaboloidShadow(uint shadowIndex, float2 sampleUV, float depth, float2x2 rotationMatrix, bool isDualParaboloid)
	{
		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 offset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * PCFRadius2D;
			float2 uv = sampleUV + offset;

			if (isDualParaboloid) {
				// Clamp PCF samples to the originating paraboloid half.
				uv.y = (sampleUV.y >= 0.5) ? max(uv.y, 0.5) : min(uv.y, 0.5);
			}

			shadow += SampleShadowGather(shadowIndex, uv, depth);
		}

		return shadow / 8.0;
	}

	float GetOmnidirectionalShadow(ShadowLightData shadowLightData, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		// ShadowLightParam.x:
		//   0 = spot/frustum (handled in GetShadowLightShadow before reaching here)
		//   1 = hemisphere   — engine renders ONE paraboloid filling the slice
		//   2 = omnidirectional (dual paraboloid) — TWO paraboloids stacked in slice
		//
		// Verified against kSHADOWMAPS slice contents in RenderDoc: hemi slices show
		// a single continuous depth gradient across y=0.5 with no seam, while omni
		// slices show two distinct paraboloid renderings stacked. Treating hemi
		// like omni applies a Y-axis compression / mirror that visibly distorts
		// (the "inverted or rotated 90°" symptom).
		const bool isOmni = (shadowLightData.ShadowLightParam.x == 2);

		bool lowerHalf = positionLS.z < 0;

		// Hemi only renders the +Z paraboloid; behind the light has no shadow data.
		// Returning 1.0 (fully lit) lets the light's own attenuation handle falloff
		// for points the engine never wrote shadow data for.
		if (!isOmni && lowerHalf)
			return 1.0;

		positionLS.xyz /= positionLS.w;

		float3 posOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalize(positionLS.xyz) + posOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;

		// Y compression only applies to omni's dual layout. Hemi fills the whole
		// slice so its sampleUV.y stays in [0, 1] directly.
		if (isOmni)
			sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		float depth = saturate(length(positionLS.xyz) / shadowLightData.ShadowLightParam.y);
		depth -= shadowLightData.ShadowLightParam.z;

		return SampleParaboloidShadow(shadowIndex, sampleUV, depth, rotationMatrix, isOmni);
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPositionWS, float2x2 rotationMatrix, out bool hasCoverage)
	{
		hasCoverage = true;  // default: paraboloid lights always sample

		ShadowLightData shadowLightData = Shadows[shadowIndex];

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

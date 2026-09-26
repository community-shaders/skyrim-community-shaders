#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

namespace LightLimitFix
{

#include "LightLimitFix/Common.hlsli"

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint FirstPerson;
		float4 WorldEyePosition;
		Light StrictLights[15];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3

	struct LocalShadowData
	{
		column_major float4x4 ShadowProj;
		float4 Params;   // x: 0 spot / 1 hemisphere / 2 omni, y: radius (<= 0 = no data), z: depth bias, w: fade-in
		float4 Params2;  // x: spot cone falloff exponent, y: skinned receiver offset toward the light
		float4 Origin;
	};

	StructuredBuffer<LocalShadowData> LocalShadows : register(t102);
	Texture2DArray<float> LocalShadowMaps : register(t103);

	static const uint LOCAL_SHADOW_TYPE_SPOT = 0;
	static const uint LOCAL_SHADOW_TYPE_HEMISPHERE = 1;
	static const uint LOCAL_SHADOW_TYPE_OMNI = 2;

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
		if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
			return !(ShadowBitMask & (1 << light.shadowLightIndex));
		}

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

	float2x2 GetShadowRotationMatrix(float noise)
	{
		float angle = noise * Math::TAU;
		float sinAngle, cosAngle;
		sincos(angle, sinAngle, cosAngle);
		return float2x2(cosAngle, sinAngle, -sinAngle, cosAngle);
	}

	float SampleLocalShadowTap(SamplerState samp, uint slice, float2 uv, float receiverDepth)
	{
		const float texel = SharedData::lightLimitFixSettings.LocalShadowTexelSize;
		float2 texelPosition = uv / texel - 0.5;
		float2 weight = frac(texelPosition);
		float4 lit = float4(LocalShadowMaps.GatherRed(samp, float3((floor(texelPosition) + 1.0) * texel, slice)) >= receiverDepth);
		return lerp(lerp(lit.w, lit.z, weight.x), lerp(lit.x, lit.y, weight.x), weight.y);
	}

	float SampleLocalShadowPCF(SamplerState samp, uint slice, float2 uv, float receiverDepth, float2x2 rotationMatrix, float2 clampMin, float2 clampMax)
	{
		const uint sampleCount = SharedData::lightLimitFixSettings.LocalShadowSamples;
		float shadow = 0.0;
		[branch] if (sampleCount <= 1)
		{
			shadow = SampleLocalShadowTap(samp, slice, clamp(uv, clampMin, clampMax), receiverDepth);
		}
		else
		{
			const float radiusUV = SharedData::lightLimitFixSettings.LocalShadowFilterRadius;
			const uint stride = sampleCount >= 8 ? 1 : 2;
			float sum = 0.0;
			[loop] for (uint i = 0; i < 8; i += stride)
			{
				float2 offset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * radiusUV;
				sum += SampleLocalShadowTap(samp, slice, clamp(uv + offset, clampMin, clampMax), receiverDepth);
			}
			shadow = sum * (stride == 1 ? 0.125 : 0.25);
		}
		return shadow;
	}

	// position is relative to eyePosition; each cached projection is relative to its own Origin.
	float GetLocalShadow(SamplerState samp, uint slice, float3 position, float3 eyePosition, float3 towardLight, bool skinnedReceiver, float2x2 rotationMatrix)
	{
		LocalShadowData data = LocalShadows[slice];
		float rawShadow = 1.0;
		float fade = 0.0;

		[branch] if (data.Params.y > 0.0)
		{
			fade = data.Params.w;
			const float texel = SharedData::lightLimitFixSettings.LocalShadowTexelSize;
			const uint shadowType = (uint)data.Params.x;
			float3 shadowPosition = position + (eyePosition - data.Origin.xyz) + towardLight * (skinnedReceiver ? data.Params2.y : 0.0);
			float4 positionLS = mul(data.ShadowProj, float4(shadowPosition, 1.0));

			[branch] if (shadowType == LOCAL_SHADOW_TYPE_SPOT)
			{
				// The vanilla mask only draws the frustum volume: behind the light and outside the cone is dark.
				rawShadow = 0.0;
				[branch] if (positionLS.w > 1e-4)
				{
					positionLS.xyz /= positionLS.w;
					[branch] if (all(abs(positionLS.xy) < 1.0) && positionLS.z >= 0.0)
					{
						float2 uv = positionLS.xy * 0.5 + 0.5;
						float receiverDepth = positionLS.z - data.Params.z;
						float spotFalloff = saturate(1.0 - pow(length(positionLS.xy), data.Params2.x));
						rawShadow = SampleLocalShadowPCF(samp, slice, uv, receiverDepth, rotationMatrix, texel, 1.0 - texel) * spotFalloff;
					}
				}
			}
			else
			{
				const bool omni = shadowType == LOCAL_SHADOW_TYPE_OMNI;
				const bool lowerHalf = positionLS.z < -1.0;
				[branch] if (omni || !lowerHalf)
				{
					float3 lightDirection = normalize(normalize(positionLS.xyz) + float3(0.0, 0.0, lowerHalf ? -1.0 : 1.0));
					// Must keep the sign: the lower paraboloid half has a negative axis term, and the
					// flip it produces is what maps that hemisphere. Clamping to a positive epsilon
					// collapses the whole lower half onto one edge texel. Matches vanilla Utility.hlsl.
					float axialTerm = lightDirection.z;
					axialTerm = abs(axialTerm) < 1e-4 ? (axialTerm < 0.0 ? -1e-4 : 1e-4) : axialTerm;
					float2 uv = lightDirection.xy / axialTerm * 0.5 + 0.5;
					float2 clampMin = texel;
					float2 clampMax = 1.0 - texel;
					if (omni) {
						uv.y = lowerHalf ? 1.0 - 0.5 * uv.y : 0.5 * uv.y;
						clampMin.y = lowerHalf ? 0.5 + texel : texel;
						clampMax.y = lowerHalf ? 1.0 - texel : 0.5 - texel;
					}

					float receiverDepth = saturate(length(positionLS.xyz) / data.Params.y) - data.Params.z;
					rawShadow = SampleLocalShadowPCF(samp, slice, uv, receiverDepth, rotationMatrix, clampMin, clampMax);
				}
			}
		}

		return lerp(1.0, rawShadow, fade);
	}
}

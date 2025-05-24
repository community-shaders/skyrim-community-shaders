#include "LightLimitFix/Common.hlsli"

cbuffer PerFrame : register(b0)
{
	row_major float4x4 InvProjMatrix[2];
	float LightsNear;
	float LightsFar;
}

float3 GetPositionVS(float2 texcoord, float depth, int eyeIndex = 0)
{
	float4 clipSpaceLocation;
	clipSpaceLocation.xy = texcoord * 2.0f - 1.0f;  // convert from [0,1] to [-1,1]
	clipSpaceLocation.y *= -1;
	clipSpaceLocation.z = depth;
	clipSpaceLocation.w = 1.0f;
	float4 homogenousLocation = mul(clipSpaceLocation, InvProjMatrix[eyeIndex]);
	return homogenousLocation.xyz / homogenousLocation.w;
}

//reference
//https://github.com/Angelo1211/HybridRenderingEngine/

RWStructuredBuffer<ClusterAABB> clusters : register(u0);

float3 IntersectionZPlane(float3 B, float z_dist)
{
	//Because this is a Z based normal this is fixed
	float3 normal = float3(0.0, 0.0, -1.0);
	float3 d = B;
	//Computing the intersection length for the line and the plane
	float t = z_dist / d.z;  //dot(normal, d);

	//Computing the actual xyz position of the point along the line
	float3 result = t * d;

	return result;
}

[numthreads(1, 1, 1)] void main(uint3 groupId
								: SV_GroupID,
								uint3 dispatchThreadId
								: SV_DispatchThreadID,
								uint3 groupThreadId
								: SV_GroupThreadID,
								uint groupIndex
								: SV_GroupIndex) {
	uint clusterIndex = groupId.x +
	                    groupId.y * CLUSTER_BUILDING_DISPATCH_SIZE_X +
	                    groupId.z * (CLUSTER_BUILDING_DISPATCH_SIZE_X * CLUSTER_BUILDING_DISPATCH_SIZE_Y);

	float2 clusterSize = rcp(float2(CLUSTER_BUILDING_DISPATCH_SIZE_X, CLUSTER_BUILDING_DISPATCH_SIZE_Y));

	float2 texcoordMax = (groupId.xy + 1) * clusterSize;
	float2 texcoordMin = groupId.xy * clusterSize;
#if !defined(VR)
	float3 maxPointVS = GetPositionVS(texcoordMax, 1.0f);
	float3 minPointVS = GetPositionVS(texcoordMin, 1.0f);
#else
	float3 maxPointVS = max(GetPositionVS(texcoordMax, 1.0f, 0), GetPositionVS(texcoordMax, 1.0f, 1));
	float3 minPointVS = min(GetPositionVS(texcoordMin, 1.0f, 0), GetPositionVS(texcoordMin, 1.0f, 1));
#endif  // !VR

	float clusterNear = LightsNear * pow(LightsFar / LightsNear, groupId.z / float(CLUSTER_BUILDING_DISPATCH_SIZE_Z));
	float clusterFar = LightsNear * pow(LightsFar / LightsNear, (groupId.z + 1) / float(CLUSTER_BUILDING_DISPATCH_SIZE_Z));

	float3 minPointNear = IntersectionZPlane(minPointVS, clusterNear);
	float3 minPointFar = IntersectionZPlane(minPointVS, clusterFar);
	float3 maxPointNear = IntersectionZPlane(maxPointVS, clusterNear);
	float3 maxPointFar = IntersectionZPlane(maxPointVS, clusterFar);

	float3 minPointAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
	float3 maxPointAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

	clusters[clusterIndex].minPoint = float4(minPointAABB, 0.0);
	clusters[clusterIndex].maxPoint = float4(maxPointAABB, 0.0);
}
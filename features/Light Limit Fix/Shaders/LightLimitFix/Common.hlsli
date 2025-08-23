#ifndef __LLF_COMMON_DEPENDENCY_HLSL__
#define __LLF_COMMON_DEPENDENCY_HLSL__

#include "Common/BufferAlignment.hlsli"

#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)
#define MAX_CLUSTER_LIGHTS 256

namespace LightFlags
{
	static const uint PortalStrict = (1 << 0);
	static const uint Shadow = (1 << 1);
	static const uint Simple = (1 << 2);

	static const uint Initialised = (1 << 8);
	static const uint Disabled = (1 << 9);
	static const uint InverseSquare = (1 << 10);
}

struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

struct LightGrid
{
	uint offset;
	uint lightCount;
	uint pad0[2];  // Manual padding for 16-byte alignment
};
// Validate that LightGrid is properly aligned
VALIDATE_BUFFER_DETAILED(LightGrid);

struct Light
{
	float3 color;
	float fade;
	float radius;
	float invRadius;
	float fadeZone;
	float sizeBias;
	float4 positionWS[2];
	uint4 roomFlags;
	uint lightFlags;
	uint shadowLightIndex;
	uint pad0;  // Manual padding for 16-byte alignment
	uint pad1;  // Manual padding for 16-byte alignment
};
// Validate that Light structure is properly aligned
VALIDATE_BUFFER_DETAILED(Light);

#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
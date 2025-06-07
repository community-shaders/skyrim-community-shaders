#include "Common.hlsli"

cbuffer PerFrame : register(b0)
{
	uint LightCount;
}

//references
//https://github.com/pezcode/Cluster

StructuredBuffer<ClusterAABB> clusters : register(t0);
StructuredBuffer<StructuredLight> lights : register(t1);

RWStructuredBuffer<uint> lightIndexCounter : register(u0);  //1
RWStructuredBuffer<uint> lightIndexList : register(u1);     //MAX_CLUSTER_LIGHTS * 16^3
RWStructuredBuffer<LightGrid> lightGrid : register(u2);     //16^3

groupshared StructuredLight sharedLights[GROUP_SIZE];

bool LightIntersectsCluster(StructuredLight light, ClusterAABB cluster, int eyeIndex = 0)
{
	float3 closest = max(cluster.minPoint.xyz, min(light.positionVS[eyeIndex].xyz, cluster.maxPoint.xyz));

	float3 dist = closest - light.positionVS[eyeIndex].xyz;
	return dot(dist, dist) <= (light.radius * light.radius);
}

[numthreads(16, 8, 8)] void main(uint3 groupId
								 : SV_GroupID,
								 uint3 dispatchThreadId
								 : SV_DispatchThreadID,
								 uint3 groupThreadId
								 : SV_GroupThreadID,
								 uint groupIndex
								 : SV_GroupIndex) {
	if (all(dispatchThreadId == 0)) {
		lightIndexCounter[0] = 0;
	}

	uint visibleLightCount = 0;
	uint visibleLightIndices[MAX_CLUSTER_LIGHTS];

	uint clusterIndex = groupIndex + GROUP_SIZE * groupId.z;

	ClusterAABB cluster = clusters[clusterIndex];

	uint lightOffset = 0;
	uint lightCount = LightCount;

	while (lightOffset < lightCount) {
		uint batchSize = min(GROUP_SIZE, lightCount - lightOffset);

		if (groupIndex < batchSize) {
			uint lightIndex = lightOffset + groupIndex;
			StructuredLight light = lights[lightIndex];
			sharedLights[groupIndex] = light;
		}

		GroupMemoryBarrierWithGroupSync();

		for (uint i = 0; i < batchSize; i++) {
			StructuredLight light = lights[i];

			if (visibleLightCount < MAX_CLUSTER_LIGHTS && (LightIntersectsCluster(light, cluster)
#ifdef VR
															  || LightIntersectsCluster(light, cluster, 1)
#endif  // VR
																  )) {
				visibleLightIndices[visibleLightCount] = lightOffset + i;
				visibleLightCount++;
			}
		}

		lightOffset += batchSize;
	}

	GroupMemoryBarrierWithGroupSync();

	uint offset = 0;
	InterlockedAdd(lightIndexCounter[0], visibleLightCount, offset);

	for (uint i = 0; i < visibleLightCount; i++) {
		lightIndexList[offset + i] = visibleLightIndices[i];
	}

	lightGrid[clusterIndex].offset = offset;
	lightGrid[clusterIndex].lightCount = visibleLightCount;
}

//https://www.3dgep.com/forward-plus/#Grid_Frustums_Compute_Shader
/*
function CullLights( L, C, G, I )
    Input: A set L of n lights.
    Input: A counter C of the current index into the global light index list.
    Input: A 2D grid G of index offset and count in the global light index list.
    Input: A list I of global light index list.
    Output: A 2D grid G with the current tiles offset and light count.
    Output: A list I with the current tiles overlapping light indices appended to it.
 
1.  let t be the index of the current tile  ; t is the 2D index of the tile.
2.  let i be a local light index list       ; i is a local light index list.
3.  let f <- Frustum(t)                     ; f is the frustum for the current tile.
 
4.  for l in L                      ; Iterate the lights in the light list.
5.      if Cull( l, f )             ; Cull the light against the tile frustum.
6.          AppendLight( l, i )     ; Append the light to the local light index list.
                     
7.  c <- AtomicInc( C, i.count )    ; Atomically increment the current index of the 
                                    ; global light index list by the number of lights
                                    ; overlapping the current tile and store the
                                    ; original index in c.
             
8.  G(t) <- ( c, i.count )          ; Store the offset and light count in the light grid.
         
9.  I(c) <- i                       ; Store the local light index list into the global 
                                    ; light index list.
*/
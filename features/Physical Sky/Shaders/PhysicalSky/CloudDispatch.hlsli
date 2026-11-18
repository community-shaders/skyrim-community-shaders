#ifndef PHYSICAL_SKY_CLOUD_DISPATCH_HLSLI
#define PHYSICAL_SKY_CLOUD_DISPATCH_HLSLI

// Tiles cover every checkerboard phase and include the finite direct-light
// probes. Clear tiles can use the ordinary cloud shader without losing shadows.
Texture2D<uint> TexStormTiles : register(t88);
RWTexture2D<uint> RWStormTiles : register(u7);

[numthreads(8, 8, 1)] void classifyStormTiles(uint2 tile : SV_DispatchThreadID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(tile >= (uint2(info.lowFrameDim) + 7u) / 8u))
		return;
	float3 rays[4] = {
		CloudScreenRay(tile * 32u),
		CloudScreenRay((tile + uint2(1, 0)) * 32u),
		CloudScreenRay((tile + 1u) * 32u),
		CloudScreenRay((tile + uint2(0, 1)) * 32u)
	};
	float3 planes[5];
	planes[4] = normalize(rays[0] + rays[1] + rays[2] + rays[3]);
	[unroll] for (uint side = 0u; side < 4u; ++side)
	{
		const float3 normal = normalize(cross(rays[side], rays[(side + 1u) & 3u]));
		planes[side] = dot(normal, planes[4]) >= 0.0 ? normal : -normal;
	}
	const float3 eye = (FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ)) * GAME_UNIT_TO_M;
	// The 240 m probe range can overshoot by half a stratum under jitter;
	// the lowest probe count is four, hence the conservative 270 m extent.
	const float3 shadowReach = -info.dirlightDir * 270.0;
	uint occupied = 0u;
	[loop] for (uint index = 0u; index < CloudStormCount() && occupied == 0u; ++index)
	{
		const CloudStormData storm = CloudStorms[index];
		if (IsVolcanic(storm)) {
			[loop] for (uint segment = 0u; segment < 7u; ++segment)
			{
				const float4 a = storm.parcels[segment].sphere;
				const float4 b = storm.parcels[segment + 1u].sphere;
				bool intersects = true;
				[unroll] for (uint plane = 0u; plane < 5u; ++plane)
				{
					// The continuous carrier lies in the convex hull of these
					// endpoint spheres. The halo encloses displaced Alligator lobes.
					const float support = max(dot(planes[plane], a.xyz - eye) + a.w,
											  dot(planes[plane], b.xyz - eye) + b.w) +
					                      200.0;
					intersects = intersects && support + max(dot(planes[plane], shadowReach), 0.0) >= 0.0;
				}
				if (intersects) {
					occupied = 1u;
					break;
				}
			}
		} else {
			float3 lower, upper;
			SupercellInfluenceBounds(storm, lower, upper);
			const float3 center = (lower + upper) * 0.5 - eye;
			const float3 radius = (upper - lower) * 0.5;
			bool intersects = true;
			[unroll] for (uint plane = 0u; plane < 5u; ++plane)
				intersects = intersects && dot(planes[plane], center) + dot(abs(planes[plane]), radius) + max(dot(planes[plane], shadowReach), 0.0) >= 0.0;
			occupied = intersects ? 1u : 0u;
		}
	}
	RWStormTiles[tile] = occupied;
}

#endif

#ifndef PHYSICAL_SKY_CLOUD_VOXEL_VIEW_HLSLI
#define PHYSICAL_SKY_CLOUD_VOXEL_VIEW_HLSLI

#ifdef CLOUD_VOXEL_VIEW
cbuffer VoxelViewDispatch : register(b3) { uint4 VoxelScreenRect; };

// Both integrals represent the same full-resolution checkerboard pixel.
// Composite before the shared temporal reconstruction, without spatial upsampling.
void StoreVoxelCloudSample(uint2 tid, float4 voxelColor, float voxelDepth)
{
	const float voxelTransmission = 1.0 - saturate(voxelColor.a);
	const float ndfTransmission = TexVolLowTr[tid];
	const float4 ndfColor = TexVolLowLum[tid];
	float4 metadata = TexVolLowAux[tid];
	float voxelFront = 1.0;
	[branch] if (voxelColor.a > 0.0 && ndfTransmission < 1.0)
	{
		// These are integral centroids, not surfaces. Blend the two layer orders
		// near equal depths; do not switch the entire cloud on a depth comparison.
		// The transition covers a march step and the stored half-float depth error.
		const float meanDepth = max(metadata.x, voxelDepth);
		const float transition = max(EncodeCloudDepth(CloudViewStep(DecodeCloudDepth(meanDepth), 0.0)), meanDepth * 0.002);
		voxelFront = smoothstep(-transition, transition, metadata.x - voxelDepth);
	}
	const float voxelVisibility = lerp(ndfTransmission, 1.0, voxelFront);
	const float ndfVisibility = lerp(1.0, voxelTransmission, voxelFront);
	const float voxelWeight = voxelColor.a * voxelVisibility;
	const float ndfWeight = (1.0 - ndfTransmission) * ndfVisibility;
	precise float depthWeight = voxelWeight + ndfWeight;
	metadata.x = depthWeight > 0.0 ?
	                 (voxelDepth * voxelWeight + metadata.x * ndfWeight) / depthWeight :
	                 metadata.x;
	metadata.zw *= ndfVisibility;
	const float transmission = ndfTransmission * voxelTransmission;
	RWTexTr[tid] = transmission;
	RWTexLum[tid] = float4(min(voxelVisibility * voxelColor.rgb + ndfVisibility * ndfColor.rgb, 65504.0), 1.0 - transmission);
	RWTexAux[tid] = metadata;
	// The local-motion channel also owns static NVDF: zero displacement, with
	// its opacity preventing NDF wind, shear and curvature motion from moving it.
	float4 localMotion = TexVolLowStormMotion[tid] * ndfVisibility;
	localMotion.w += voxelWeight;
	RWStormMotion[tid] = localMotion;
	RWLightningMask[tid] = TexLightningTrace[tid] * ndfVisibility;
}

[numthreads(8, 8, 1)] void renderVoxelView(uint2 tid : SV_DispatchThreadID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(tid >= uint2(info.lowFrameDim)))
		return;
	if (any(tid < VoxelScreenRect.xy) || any(tid >= VoxelScreenRect.zw)) {
		StoreVoxelCloudSample(tid, 0.0, 0.0);
		return;
	}
	const uint2 pixel = tid * 4u + CloudPhaseOffset();
	const float3 direction = CloudScreenRay(pixel);
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
	// Depth belongs to this phase's ray, not a min/max neighbour in the tile.
	const float deviceDepth = CloudSceneDeviceDepth(pixel, 1u);
	const float sceneDistance = deviceDepth >= 1.0 - 1e-6 ? info.rayMarchRange :
	                                                        SharedData::GetScreenDepth(deviceDepth) / max(abs(mul(FrameBuffer::CameraView, float4(direction, 0)).z), 1e-6);
	const float rangeEnd = sceneDistance;
	float cursor = 0.0;
	float2 interval = CloudVoxelInterval(eye, direction, cursor, true);
	[branch] if (min(interval.y, rangeEnd) <= interval.x)
	{
		StoreVoxelCloudSample(tid, 0.0, 0.0);
		return;
	}
	const float phase = CloudLowPhase(dot(direction, info.dirlightDir));
#	ifdef CLOUD_VOXEL_SINGLE
	// A single placement's reference width/material are constant along the ray.
	// Fetch the existing calibration once; height, density and attenuation are
	// still evaluated at every occupied step, with the same lighting response.
	CloudMsProfile msProfile = (CloudMsProfile)0;
	[branch] if (info.msStrength > 0.0 && info.msAlbedo > 0.0)
	{
		const VoxelCloudData cloud = VoxelClouds[0];
		NDFInfo material = (NDFInfo)0;
		material.altitude = VoxelAltitude(cloud);
		material.voxelBlend = 1.0;
		material.voxelDensityScale = cloud.parameters.y * cloud.centerWeight.w;
		msProfile = PrepareCloudMsProfile(material);
	}
#	endif
	float3 radiance = 0.0;
	float transmission = 1.0, depthSum = 0.0, weightSum = 0.0;
	// Selecting the next box also skips gaps between independent placements.
	CLOUD_VOXEL_INSTANCE_LOOP for (uint region = 0u; region < CloudVoxelCount() && transmission > .1; ++region)
	{
		if (region != 0u)
			interval = CloudVoxelInterval(eye, direction, cursor, true);
		const float end = min(interval.y, rangeEnd);
		if (end <= interval.x)
			break;
		float distance = interval.x;
		[loop] while (distance < end && transmission > .1)
		{
			uint2 occupied;
			const float step = CloudVoxelStep(eye + direction * distance, distance, 0.0, occupied);
			if (any(occupied != 0u)) {
				const float metres = distance * GAME_UNIT_TO_M;
				const float3 position = eye + direction * (distance + step * 0.5);
				CloudDensityContext context = (CloudDensityContext)0;
				const float density = AddVoxelDensity(position, 0.0, metres, 0.0, occupied, 0.0, context);
				if (density > 1e-10) {
					context.ndf.voxelBlend = 1.0;
					const float depthWeight = CloudDepthWeight(transmission, density, distance);
					depthSum += depthWeight * distance;
					weightSum += depthWeight;
					const float opticalDepth = step * GAME_UNIT_TO_M * density;
					const float stepTransmission = exp(-opticalDepth);
					const float opacity = CloudOpacity(opticalDepth);
#	ifdef CLOUD_VOXEL_SINGLE
					const float lightDepth = TraceVoxelLighting(position, metres) * info.sunExtinction;
					const float multiple = CloudMultipleScattering(density, context.ndf, msProfile);
					float3 source = ShadeCloudLighting(position, context.ndf, density, opticalDepth, opacity, phase, lightDepth, multiple);
#	else
					float3 source = CloudLighting(position, direction, context.ndf, density, opticalDepth, opacity, metres, 0.0, phase);
#	endif
					source += opacity * Lightning::ActiveMask(Lightning::CloudMask(position + float3(0, 0, info.bottomZ), context.ndf.height_fraction, context.ndf.dimension_profile)) * Lightning::Color();
					if (info.ambientStrength > 0.0) {
						const float2 material = CloudMaterialAmbient(context.ndf);
						source += opacity * material.y * info.ambientStrength * info.cloudLightingScale * CloudAmbientTransport::ViewSource(position);
					}
					// Native NVDF integrates aerial perspective at each occupied step.
					if (!ViewAtmosphere::Enabled()) {
						const float4 air = SampleCloudAerialPerspective(direction, distance, 0.0);
						source = source * air.a + air.rgb * opacity;
					}
					radiance += transmission * source;
					transmission *= stepTransmission;
				}
			}
			distance += step;
		}
		cursor = interval.y;
	}
	const float opacity = 1.0 - saturate((transmission - .1) / .9);
	StoreVoxelCloudSample(tid, float4(min(radiance, 65504.0), opacity),
		weightSum > 0.0 ? EncodeCloudDepth(depthSum / weightSum) : 0.0);
}
#endif
#endif

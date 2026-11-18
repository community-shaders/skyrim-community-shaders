#ifndef PHYSICAL_SKY_SHADOW_VOLUME_HLSLI
#define PHYSICAL_SKY_SHADOW_VOLUME_HLSLI

#include "PhysicalSky/CloudGeometry.hlsli"

namespace CloudShadowVolume
{
	float2 GridCenter(float2 camera, float range, uint2 dimensions)
	{
		const float2 cellSize = range / float2(dimensions);
		return floor(camera / cellSize) * cellSize;
	}

	// Row 0: bottom/top altitude, curvature radius, valid flag. Remaining rows
	// store mean transmittance at evenly spaced heights, including both endpoints.
	Texture2D<float4> MeanProfile : register(t69);

	float FarTransmittance(float3 pos, float3 lightDir, float4 shadowSample, uint2 dimensions, out float localWeight)
	{
		localWeight = 1.0;
		const float4 profile = MeanProfile.Load(int3(0, 0, 0));
		if (profile.w == 0.0)
			return 1.0;
		const float3 origin = float3(pos.xy - FrameBuffer::CameraPosAdjust.xy, pos.z);
		const float2 shell = IntersectCloudBelow(origin, lightDir, profile.y, profile.z);
		localWeight = 0.0;
		if (shell.y <= max(shell.x, 0.0))
			return 1.0;
		if (all(shadowSample.xyz >= 0.0) && all(shadowSample.xyz <= 1.0)) {
			// A locally stored prefix is only complete if the light reaches the
			// cloud top before leaving the box. Fade incomplete side exits to the
			// mean as well, otherwise low-angle rays remain bright inside the box.
			const float range = SharedData::physSkyData.shadowVolumeRange;
			const float2 center = GridCenter(FrameBuffer::CameraPosAdjust.xy, range, max(dimensions, 1u));
			const float2 below = IntersectCloudBelow(origin, lightDir, profile.x, profile.z);
			const float exitDistance = shell.y > max(max(shell.x, 0.0), below.y) ? shell.y : min(shell.y, max(below.x, 0.0));
			const float3 exitPosition = pos + exitDistance * lightDir;
			const float2 exitUv = (exitPosition.xy - center) / range + 0.5;
			const float2 edge = min(min(shadowSample.xy, 1.0 - shadowSample.xy), min(exitUv, 1.0 - exitUv));
			localWeight = exitPosition.z >= profile.x && exitPosition.z <= profile.y + 1.0 ? smoothstep(0.0, 0.1, min(edge.x, edge.y)) : 0.0;
		}
		if (localWeight * shadowSample.w >= 1.0)
			return 1.0;  // Complete local ray: the mean has zero blending weight.
		uint width, height;
		MeanProfile.GetDimensions(width, height);
		const float altitude = CloudProfileAltitude(pos, FrameBuffer::CameraPosAdjust.xy, profile.z);
		const float index = saturate((altitude - profile.x) / max(profile.y - profile.x, 1.0)) * (height - 2u);
		const uint lo = (uint)index + 1u;
		const float mean = lerp(MeanProfile.Load(int3(0, lo, 0)).x,
			MeanProfile.Load(int3(0, min(lo + 1u, height - 1u), 0)).x, frac(index));
		return saturate(mean);
	}

	float EdgeWeight(float3 uvw, uint2 dimensions)
	{
		const float2 edge = min(uvw.xy, 1.0 - uvw.xy) * float2(dimensions);
		return smoothstep(0.0, 2.0, min(edge.x, edge.y));
	}

	// The volume stores transmittance toward the light. Outside
	// receivers must sample the ray's entry into the box, never its exit.
	float4 GetSample(float3 pos, float3 lightDir, float3 boundsMin, float3 boundsMax, uint2 dimensions)
	{
		float tNear = 0.0;
		float tFar = 3.402823466e+38;
		[unroll] for (uint axis = 0; axis < 3; ++axis)
		{
			if (abs(lightDir[axis]) < 1e-8) {
				if (pos[axis] < boundsMin[axis] || pos[axis] > boundsMax[axis])
					return -1.0;
			} else {
				const float t0 = (boundsMin[axis] - pos[axis]) / lightDir[axis];
				const float t1 = (boundsMax[axis] - pos[axis]) / lightDir[axis];
				tNear = max(tNear, min(t0, t1));
				tFar = min(tFar, max(t0, t1));
			}
		}
		if (tFar <= tNear)
			return -1.0;
		const float3 entry = saturate((pos + tNear * lightDir - boundsMin) / (boundsMax - boundsMin));
		const float3 middle = saturate((pos + (0.5 * tNear + 0.5 * tFar) * lightDir - boundsMin) / (boundsMax - boundsMin));
		// Fade grazing paths at the projected footprint, including side-face entries.
		return float4(entry, max(EdgeWeight(entry, dimensions), EdgeWeight(middle, dimensions)));
	}

	// Only use this overload with a known linear-clamp sampler. Compute passes
	// own their samplers, unlike the material/depth/shadow-mask pixel shaders.
	float SampleTransmittance(Texture3D<float> volume, SamplerState linearClamp, float4 shadowSample, float outside = 1.0)
	{
		const float3 uvw = shadowSample.xyz;
		if (any(uvw < 0.0) || any(uvw > 1.0))
			return outside;
		return lerp(outside, volume.SampleLevel(linearClamp, uvw, 0), shadowSample.w);
	}

	float SampleTransmittance(Texture3D<float> volume, float4 shadowSample, float outside = 1.0)
	{
		const float3 uvw = shadowSample.xyz;
		if (any(uvw < 0.0) || any(uvw > 1.0))
			return outside;
		uint3 dims;
		volume.GetDimensions(dims.x, dims.y, dims.z);
		if (any(dims == 0))
			return outside;

		// Callers include material, depth and shadow-mask paths with different
		// sampler states. Explicit trilinear filtering keeps all of them clamped
		// and continuous, including receivers projected exactly onto a box face.
		const float3 coord = uvw * dims - 0.5;
		const int3 base = int3(floor(coord));
		const int3 lo = clamp(base, 0, int3(dims) - 1);
		const int3 hi = clamp(base + 1, 0, int3(dims) - 1);
		const float3 w = frac(coord);
		const float z0 = lerp(
			lerp(volume.Load(int4(lo.x, lo.y, lo.z, 0)), volume.Load(int4(hi.x, lo.y, lo.z, 0)), w.x),
			lerp(volume.Load(int4(lo.x, hi.y, lo.z, 0)), volume.Load(int4(hi.x, hi.y, lo.z, 0)), w.x), w.y);
		const float z1 = lerp(
			lerp(volume.Load(int4(lo.x, lo.y, hi.z, 0)), volume.Load(int4(hi.x, lo.y, hi.z, 0)), w.x),
			lerp(volume.Load(int4(lo.x, hi.y, hi.z, 0)), volume.Load(int4(hi.x, hi.y, hi.z, 0)), w.x), w.y);
		return lerp(outside, lerp(z0, z1, w.z), shadowSample.w);
	}
}
#endif

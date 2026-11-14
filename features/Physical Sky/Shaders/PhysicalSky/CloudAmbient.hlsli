float2 CloudMaterialAmbient(NDFInfo ndf);

#ifndef PHYSICAL_SKY_CLOUD_AMBIENT_HLSLI
#define PHYSICAL_SKY_CLOUD_AMBIENT_HLSLI

Texture2D<float4> TexCloudAmbientLighting : register(t40);
Texture3D<float2> TexCloudAmbientDensity : register(t41);
Texture3D<float4> TexCloudAmbientSource0 : register(t42);
Texture3D<float4> TexCloudAmbientSource1 : register(t43);
Texture3D<float4> TexCloudAmbientSource2 : register(t44);
Texture3D<float4> TexCloudAmbientSweep : register(t45);
Texture2D<float4> TexCloudAmbientPreviousLayout : register(t46);
Texture2D<float4> TexCloudAmbientBoundary : register(t47);
Texture2D<float4> TexCloudAmbientLayout : register(t48);
Texture3D<float4> TexCloudAmbientSource3 : register(t49);
Texture3D<float4> TexCloudAmbientView0 : register(t50);
Texture3D<float4> TexCloudAmbientView1 : register(t51);
Texture3D<float4> TexCloudAmbientView2 : register(t52);
Texture3D<float4> TexCloudAmbientView3 : register(t53);
RWTexture3D<float4> RWCloudAmbientView0 : register(u0);
RWTexture3D<float4> RWCloudAmbientView1 : register(u1);
RWTexture3D<float4> RWCloudAmbientView2 : register(u2);
RWTexture3D<float4> RWCloudAmbientView3 : register(u3);
RWTexture2D<float4> RWCloudAmbientLighting : register(u0);
RWTexture3D<float2> RWCloudAmbientDensity : register(u0);
RWTexture3D<float4> RWCloudAmbientSweep : register(u0);
RWTexture2D<float4> RWCloudAmbientBoundary : register(u0);
RWTexture3D<float4> RWCloudAmbientSource : register(u1);
RWTexture2D<float4> RWCloudAmbientLayout : register(u1);

cbuffer CloudAmbientVolumeConstants : register(b2)
{
	float4 CloudAmbientPhase;
	uint CloudAmbientCascade;
	uint CloudAmbientWidth;
	uint CloudAmbientReset;
	uint CloudAmbientIterations;
};

namespace CloudAmbientTransport
{
	struct Grid
	{
		float3 origin;
		float3 spacing;
		uint3 size;
	};

	Grid CurrentGrid(uint cascade)
	{
		const VolumetricCloudData info = VolumetricCloudBuffer[0];
		const float2 heights = TexCloudHeightBounds[uint2(0, 0)];
		float2 altitude = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, heights) * GAME_UNIT_TO_M;
		if (info.localNdfAltitude.y > 0.0) {
			altitude.x = min(altitude.x, info.localNdfAltitude.x);
			altitude.y = max(altitude.y, info.localNdfAltitude.x + info.localNdfAltitude.y);
		}
		// Independent instances are not part of the weather Height reduction.
		// Their full height must participate in the transport grid as well.
		[loop] for (uint index = 0u; index < CloudStormCount(); ++index)
		{
			const float2 bounds = CloudStorms[index].altitudeMotion.xy;
			altitude.x = min(altitude.x, bounds.x);
			altitude.y = max(altitude.y, bounds.x + bounds.y);
		}
		if (info.voxelCount > 0u) {
			altitude.x = min(altitude.x, info.voxelAltitude.x);
			altitude.y = max(altitude.y, info.voxelAltitude.y);
		}
		Grid grid;
		grid.size = uint3(CloudAmbientWidth, CloudAmbientWidth, CloudAmbientWidth / 2u);
		const float extent = 4000.0 * exp2(2.0 * float(cascade));
		float verticalExtent = max(altitude.y - altitude.x + 64.0, 96.0);
		// Leave complete boundary cells after snapping the grid origin. Otherwise
		// the tall eruption can touch the grid's last slice and expose its edge.
		if (CloudStormCount() > 0u || info.voxelCount > 0u)
			verticalExtent = max(verticalExtent, (altitude.y - altitude.x) / (1.0 - 4.0 / grid.size.z));
		grid.spacing = float3(extent / grid.size.xy, verticalExtent / grid.size.z);
		const float3 center = float3(FrameBuffer::CameraPosAdjust.xy * GAME_UNIT_TO_M, 0.5 * (altitude.x + altitude.y));
		grid.origin = floor(center / grid.spacing) * grid.spacing - 0.5 * float3(grid.size) * grid.spacing;
		return grid;
	}

	Grid StoredGrid(uint cascade, bool previous)
	{
		const float4 origin = previous ? TexCloudAmbientPreviousLayout[uint2(0, cascade)] : TexCloudAmbientLayout[uint2(0, cascade)];
		const float4 size = previous ? TexCloudAmbientPreviousLayout[uint2(1, cascade)] : TexCloudAmbientLayout[uint2(1, cascade)];
		Grid grid;
		grid.origin = origin.xyz;
		grid.spacing = float3(origin.ww, size.x);
		grid.size = uint3(size.yy, size.z);
		return grid;
	}

	float3 Coordinate(float3 pos, uint cascade, bool previous)
	{
		const VolumetricCloudData info = VolumetricCloudBuffer[0];
		const float4 windCamera = previous ? TexCloudAmbientPreviousLayout[uint2(2, cascade)] : TexCloudAmbientLayout[uint2(2, cascade)];
		const float radius = previous ? TexCloudAmbientPreviousLayout[uint2(3, cascade)].x : TexCloudAmbientLayout[uint2(3, cascade)].x;
		const float2 xy = pos.xy - info.noiseWindOffset + windCamera.xy;
		return float3(xy, pos.z + CloudCurvatureOffset(xy - windCamera.zw, radius)) * GAME_UNIT_TO_M;
	}

	float3 SampleMoment(uint cascade, Grid grid, float3 uvw, uint moment, bool previous)
	{
		const float4 previousPhase = previous ? TexCloudAmbientPreviousLayout[uint2(4, cascade)] : TexCloudAmbientLayout[uint2(4, cascade)];
		const float rescale = moment == 0u ? 1.0 : (moment < 4u ? CloudAmbientPhase.z / max(previousPhase.z, 1e-6) : CloudAmbientPhase.w / max(previousPhase.w, 1e-6));
		uvw = clamp(uvw, 0.5 / float3(grid.size), 1.0 - 0.5 / float3(grid.size));
		uvw.z = (uvw.z + moment) / 9.0;
		if (cascade == 0u)
			return rescale * TexCloudAmbientSource0.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
		if (cascade == 1u)
			return rescale * TexCloudAmbientSource1.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
		if (cascade == 2u)
			return rescale * TexCloudAmbientSource2.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
		return rescale * TexCloudAmbientSource3.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
	}

	float MomentBasis(uint index, float3 d)
	{
		if (index == 0u)
			return 1.0;
		if (index < 4u)
			return 1.732050808 * d[index - 1u] * CloudAmbientPhase.z;
		if (index == 4u)
			return 3.872983346 * d.x * d.y * CloudAmbientPhase.w;
		if (index == 5u)
			return 3.872983346 * d.y * d.z * CloudAmbientPhase.w;
		if (index == 6u)
			return 3.872983346 * d.z * d.x * CloudAmbientPhase.w;
		if (index == 7u)
			return 1.118033989 * (3.0 * d.z * d.z - 1.0) * CloudAmbientPhase.w;
		return 1.936491673 * (d.x * d.x - d.y * d.y) * CloudAmbientPhase.w;
	}

	float3 BoundarySource(float3 ray, float altitude);
	float2 AltitudeRange();

	// Solid-angle hemisphere contributions cached with the atmosphere lighting.
	void HemisphereSources(float altitude, out float3 sky, out float3 lower)
	{
		const float2 range = AltitudeRange();
		const float row = 4.0 * sqrt(saturate((altitude - range.x) / max(range.y - range.x, 1.0)));
		sky = TexCloudAmbientLighting.SampleLevel(TransmittanceSampler, float2((5.5 + row) / 512.0, 0.5 / 7.0), 0).rgb;
		lower = TexCloudAmbientLighting.SampleLevel(TransmittanceSampler, float2((10.5 + row) / 512.0, 0.5 / 7.0), 0).rgb;
	}

	float3 MiddleSource(float3 pos, float2 opticalDepth)
	{
		float3 sky, lower;
		HemisphereSources(0.5 * (VolumetricCloudBuffer[0].middleLayer.x + VolumetricCloudBuffer[0].middleLayer.y), sky, lower);
		const float2 response = MiddleAmbientResponse(opticalDepth);
		return sky * response.x + lower * response.y;
	}

	// The same source used to shade the middle cloud supplies the low cloud's
	// boundary radiance. A thick deck replaces the sky, including its colour.
	struct MiddleEnvironment
	{
		float2 depth;
		float3 bottom;
		float3 top;
	};

	MiddleEnvironment MiddleEnvironmentAt(float3 pos)
	{
		MiddleEnvironment env = (MiddleEnvironment)0;
		const VolumetricCloudData info = VolumetricCloudBuffer[0];
		[branch] if (info.middleLayer.z <= 0.0) return env;
		const float4 column = MiddleCloudColumn(pos.xy);
		if (column.z <= 0.0)
			return env;
		env.depth = MiddleColumnDepths(column, pos);
		env.bottom = MiddleBoundaryRadiance(float3(pos.xy, column.x - CloudCurvatureOffset(pos.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius)), float3(0, 0, 1), float2(env.depth.x + env.depth.y, 0));
		env.top = MiddleBoundaryRadiance(float3(pos.xy, column.y - CloudCurvatureOffset(pos.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius)), float3(0, 0, -1), float2(0, env.depth.x + env.depth.y));
		return env;
	}

	float3 MiddleBoundary(MiddleEnvironment env, float3 direction, float3 incoming)
	{
		const bool upward = direction.z >= 0.0;
		const float depth = upward ? env.depth.x : env.depth.y;
		const float transmission = exp(-depth / max(abs(direction.z), 0.2));
		return lerp(upward ? env.bottom : env.top, incoming, transmission);
	}

	float3 Source(float3 pos, float3 ray, bool previous)
	{
		float3 source = 0.0;
		float remaining = 1.0;
		[loop] for (uint cascade = 0u; cascade < 4u && remaining > 0.001; ++cascade)
		{
			const float valid = previous ? TexCloudAmbientPreviousLayout[uint2(1, cascade)].w : TexCloudAmbientLayout[uint2(1, cascade)].w;
			if (valid == 0.0)
				continue;
			const Grid grid = StoredGrid(cascade, previous);
			const float3 uvw = (Coordinate(pos, cascade, previous) - grid.origin) / (grid.spacing * float3(grid.size));
			const float3 edge = min(uvw, 1.0 - uvw) * float3(grid.size);
			const float blend = smoothstep(0.5, 3.5, min(edge.x, edge.y)) * smoothstep(0.0, 1.0, edge.z);
			if (blend <= 0.0)
				continue;
			float3 value = 0.0;
			[unroll] for (uint moment = 0u; moment < 9u; ++moment)
				value += MomentBasis(moment, ray) * SampleMoment(cascade, grid, uvw, moment, previous);
			source += remaining * blend * max(value, 0.0);
			remaining *= 1.0 - blend;
		}
		if (remaining > 0.001)
			source += remaining * BoundarySource(ray, pos.z);
		return source;
	}

	float3 ViewSource(float3 pos)
	{
		float3 source = 0.0;
		float remaining = 1.0;
		[loop] for (uint cascade = 0u; cascade < 4u && remaining > 0.001; ++cascade)
		{
			if (TexCloudAmbientLayout[uint2(1, cascade)].w == 0.0)
				continue;
			const Grid grid = StoredGrid(cascade, false);
			const float3 uvw = (Coordinate(pos, cascade, false) - grid.origin) / (grid.spacing * float3(grid.size));
			const float3 edge = min(uvw, 1.0 - uvw) * float3(grid.size);
			const float blend = smoothstep(0.5, 3.5, min(edge.x, edge.y)) * smoothstep(0.0, 1.0, edge.z);
			if (blend <= 0.0)
				continue;
			float3 value;
			if (cascade == 0u)
				value = TexCloudAmbientView0.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
			else if (cascade == 1u)
				value = TexCloudAmbientView1.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
			else if (cascade == 2u)
				value = TexCloudAmbientView2.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
			else
				value = TexCloudAmbientView3.SampleLevel(TransmittanceSampler, uvw, 0).rgb;
			source += remaining * blend * value;
			remaining *= 1.0 - blend;
		}
		if (remaining > 0.001) {
			const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, VolumetricCloudBuffer[0].bottomZ);
			const float3 delta = pos - eye;
			source += remaining * BoundarySource(delta * rsqrt(max(dot(delta, delta), 1e-8)), pos.z);
		}
		return source;
	}

	float3 BaseDirection(uint index, out float weight)
	{
		static const float3 directions[26] = {
			float3(-1, -1, -1),
			float3(0, -1, -1),
			float3(1, -1, -1),
			float3(-1, 0, -1),
			float3(0, 0, -1),
			float3(1, 0, -1),
			float3(-1, 1, -1),
			float3(0, 1, -1),
			float3(1, 1, -1),
			float3(-1, -1, 0),
			float3(0, -1, 0),
			float3(1, -1, 0),
			float3(-1, 0, 0),
			float3(1, 0, 0),
			float3(-1, 1, 0),
			float3(0, 1, 0),
			float3(1, 1, 0),
			float3(-1, -1, 1),
			float3(0, -1, 1),
			float3(1, -1, 1),
			float3(-1, 0, 1),
			float3(0, 0, 1),
			float3(1, 0, 1),
			float3(-1, 1, 1),
			float3(0, 1, 1),
			float3(1, 1, 1)
		};
		const float3 d = directions[index];
		const float n = dot(d, d);
		weight = n < 1.5 ? 1.0 / 21.0 : (n < 2.5 ? 4.0 / 105.0 : 9.0 / 280.0);
		return d * rsqrt(n);
	}

	float3 RotateDirection(float3 d, bool inverse)
	{
		const float3 axis = normalize(TexCloudAmbientLighting[uint2(0, 6)].xyz);
		// Rodrigues rotation keeps all angular weights and moments intact.
		const float3 crossAxis = float3(axis.z, 0.0, -axis.x);
		if (axis.y < -0.9999)
			return float3(-d.x, -d.y, d.z);
		return d + (inverse ? -1.0 : 1.0) * cross(crossAxis, d) +
		       cross(crossAxis, cross(crossAxis, d)) / max(1.0 + axis.y, 1e-4);
	}

	float3 OrientedDirection(uint index, out float weight)
	{
		const float3 d = BaseDirection(index, weight);
		return RotateDirection(d, false);
	}

	uint DirectionIndex(float3 node)
	{
		const uint index = uint(dot(node + 1.0, float3(1, 3, 9)));
		return index - (index > 13u ? 1u : 0u);
	}

	void BoundaryWeights(float3 direction, out uint3 indices, out float3 weights)
	{
		const float3 d = RotateDirection(direction, true);
		const float3 magnitude = abs(d);
		const uint major = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0u : (magnitude.y >= magnitude.z ? 1u : 2u);
		const uint a = (major + 1u) % 3u;
		const uint b = (major + 2u) % 3u;
		const float2 uv = saturate(abs(float2(d[a], d[b]) / d[major]));
		const float3 center = float3(sign(d[major]), 0.0, 0.0);
		const float3 edge = float3(center.x, uv.x >= uv.y ? sign(d[a]) : 0.0, uv.x >= uv.y ? 0.0 : sign(d[b]));
		const float3 corner = float3(center.x, sign(d[a]), sign(d[b]));
		indices = uint3(
			DirectionIndex(major == 0u ? center : (major == 1u ? center.zxy : center.yzx)),
			DirectionIndex(major == 0u ? edge : (major == 1u ? edge.zxy : edge.yzx)),
			DirectionIndex(major == 0u ? corner : (major == 1u ? corner.zxy : corner.yzx)));
		// Cube-face triangles form a continuous partition across transport directions.
		weights = float3(1.0 - max(uv.x, uv.y), abs(uv.x - uv.y), min(uv.x, uv.y));
	}

	uint MajorAxis(float3 d, Grid grid)
	{
		const float3 rate = abs(d) / grid.spacing;
		return rate.x >= rate.y && rate.x >= rate.z ? 0u : (rate.y >= rate.z ? 1u : 2u);
	}

	float3 Boundary(uint direction, float altitude);
	float3 Direction(uint index, out float weight)
	{
		static const float nodes[4] = { 0.0694318442, 0.3300094782, 0.6699905218, 0.9305681558 };
		static const float weights[4] = { 0.1739274226, 0.3260725774, 0.3260725774, 0.1739274226 };
		const uint ring = index / 64u;
		const float z = nodes[ring % 4u] * (ring < 4u ? -1.0 : 1.0);
		const float angle = (float(index % 64u) + 0.5) * (2.0 * Math::PI / 64.0);
		weight = weights[ring % 4u] / 128.0;
		return float3(sqrt(1.0 - z * z) * float2(cos(angle), sin(angle)), z);
	}

	float2 AltitudeRange()
	{
		const VolumetricCloudData info = VolumetricCloudBuffer[0];
		const float bottom = max(info.shadowVolumeBottom, GAME_UNITS_PER_METER);
		return float2(bottom, max(info.highestCloudAltitude, bottom + GAME_UNITS_PER_METER));
	}

	float3 Boundary(uint direction, float altitude)
	{
		const float2 range = AltitudeRange();
		const float row = 4.0 * sqrt(saturate((altitude - range.x) / max(range.y - range.x, 1.0)));
		return TexCloudAmbientBoundary.SampleLevel(TransmittanceSampler, float2((direction + 0.5) / 26.0, (1.5 + row) / 15.0), 0).rgb;
	}

	float3 BoundarySource(float3 ray, float altitude)
	{
		const float2 range = AltitudeRange();
		const float row = 4.0 * sqrt(saturate((altitude - range.x) / max(range.y - range.x, 1.0)));
		float3 source = 0.0;
		[unroll] for (uint moment = 0u; moment < 9u; ++moment)
			source += MomentBasis(moment, ray) * TexCloudAmbientBoundary.SampleLevel(TransmittanceSampler,
																			float2((row + 0.5) / 26.0, (6.5 + moment) / 15.0), 0)
			                                         .rgb;
		return max(source, 0.0);
	}

}

[numthreads(4, 4, 4)] void resolveCloudAmbientView(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
	const uint cascade = group.x < 256u ? 0u : 1u + (group.x - 256u) / 32u;
	const uint block = cascade == 0u ? group.x : (group.x - 256u) % 32u;
	const uint width = cascade == 0u ? 32u : 16u;
	const uint row = width / 4u;
	const uint3 index = uint3(block % row, (block / row) % row, block / (row * row)) * 4u + thread;
	if (cascade >= 4u || any(index >= uint3(width, width, width / 2u)))
		return;
	float3 value = 0.0;
	if (TexCloudAmbientLayout[uint2(1, cascade)].w != 0.0) {
		const VolumetricCloudData info = VolumetricCloudBuffer[0];
		const CloudAmbientTransport::Grid grid = CloudAmbientTransport::StoredGrid(cascade, false);
		const float4 windCamera = TexCloudAmbientLayout[uint2(2, cascade)];
		const float radius = TexCloudAmbientLayout[uint2(3, cascade)].x;
		float3 pos = (grid.origin + (float3(index) + 0.5) * grid.spacing) * GAME_UNITS_PER_METER;
		pos.z -= CloudCurvatureOffset(pos.xy - windCamera.zw, radius);
		pos.xy += info.noiseWindOffset - windCamera.xy;
		const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
		const float3 displacement = pos - eye;
		const float3 ray = displacement * rsqrt(max(dot(displacement, displacement), 1e-8));
		const float2 previousPhase = TexCloudAmbientLayout[uint2(4, cascade)].zw;
		const float2 phaseScale = CloudAmbientPhase.zw / max(previousPhase, 1e-6);
		[unroll] for (uint moment = 0u; moment < 9u; ++moment)
		{
			const int4 address = int4(index.xy, index.z + moment * grid.size.z, 0);
			float3 coefficient;
			if (cascade == 0u)
				coefficient = TexCloudAmbientSource0.Load(address).rgb;
			else if (cascade == 1u)
				coefficient = TexCloudAmbientSource1.Load(address).rgb;
			else if (cascade == 2u)
				coefficient = TexCloudAmbientSource2.Load(address).rgb;
			else
				coefficient = TexCloudAmbientSource3.Load(address).rgb;
			const float rescale = moment == 0u ? 1.0 : (moment < 4u ? phaseScale.x : phaseScale.y);
			value += CloudAmbientTransport::MomentBasis(moment, ray) * (rescale * coefficient);
		}
	}
	const float4 source = float4(max(value, 0.0), 0.0);
	if (cascade == 0u)
		RWCloudAmbientView0[index] = source;
	else if (cascade == 1u)
		RWCloudAmbientView1[index] = source;
	else if (cascade == 2u)
		RWCloudAmbientView2[index] = source;
	else
		RWCloudAmbientView3[index] = source;
}

// Parallel composition of affine radiance intervals along a sheared light ray.
#ifndef CLOUD_AMBIENT_SWEEP_THREADS
#	define CLOUD_AMBIENT_SWEEP_THREADS 32
#endif
groupshared float4 gAmbientIntervals[CLOUD_AMBIENT_SWEEP_THREADS];
[numthreads(CLOUD_AMBIENT_SWEEP_THREADS, 1, 1)] void sweepCloudAmbient(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const CloudAmbientTransport::Grid grid = CloudAmbientTransport::CurrentGrid(CloudAmbientCascade);
	const float3 d = TexCloudAmbientBoundary[uint2(group.z, 0)].xyz;
	const uint major = CloudAmbientTransport::MajorAxis(d, grid);
	const uint a = (major + 1u) % 3u;
	const uint b = (major + 2u) % 3u;
	const float distance = grid.spacing[major] / max(abs(d[major]), 1e-6);
	const float3 offset = d / grid.spacing * (grid.spacing[major] / d[major]);
	const float2 base = float2(group.xy) - 0.5 * grid.size.x;
	const float2 reach = 0.5 * (float(grid.size[major]) - 1.0) * abs(float2(offset[a], offset[b]));
	if (any(base + reach < -0.5) || any(base - reach > float2(grid.size[a], grid.size[b]) - 0.5)) {
		RWCloudAmbientSweep[uint3(group.xy, group.z * 32u + lane)] = float4(0, 0, 0, 1);
		return;
	}
	const float majorCell = d[major] > 0.0 ? float(grid.size[major]) - 1.0 - lane : float(lane);
	const float shear = majorCell - 0.5 * (float(grid.size[major]) - 1.0);
	const float3 sweepCell = float3(majorCell,
		float(group.x) - 0.5 * grid.size.x + shear * offset[a],
		float(group.y) - 0.5 * grid.size.x + shear * offset[b]);
	const float3 cell = major == 0u ? sweepCell : (major == 1u ? sweepCell.zxy : sweepCell.yzx);
	const bool inside = lane < grid.size[major] && all(cell >= -0.5) && all(cell <= float3(grid.size) - 0.5);
	float3 source = 0.0;
	float halfTransmission = 1.0;
	if (inside) {
		const float3 uvw = (clamp(cell, 0.0, float3(grid.size) - 1.0) + 0.5) / float3(32, 32, 16);
		const float2 medium = TexCloudAmbientDensity.SampleLevel(TransmittanceSampler, uvw, 0);
		const float sigma = medium.x;
		halfTransmission = exp(-0.5 * sigma * distance);
		if (sigma > 0.0) {
			const float3 coordinate = grid.origin + (cell + 0.5) * grid.spacing;
			float3 pos = coordinate * GAME_UNITS_PER_METER;
			pos.z -= CloudCurvatureOffset(pos.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
			source = (medium.y / max(sigma, 1e-8)) * (CloudAmbientReset != 0u ? CloudAmbientTransport::BoundarySource(d, pos.z) : CloudAmbientTransport::Source(pos, d, true));
		}
	}
	const float transmission = halfTransmission * halfTransmission;
	gAmbientIntervals[lane] = float4((1.0 - transmission) * source, transmission);
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 1u; stride < CLOUD_AMBIENT_SWEEP_THREADS; stride <<= 1u)
	{
		const float4 own = gAmbientIntervals[lane];
		float4 upstream = float4(0, 0, 0, 1);
		if (lane >= stride)
			upstream = gAmbientIntervals[lane - stride];
		GroupMemoryBarrierWithGroupSync();
		gAmbientIntervals[lane] = float4(own.rgb + own.a * upstream.rgb, own.a * upstream.a);
		GroupMemoryBarrierWithGroupSync();
	}
	float4 upstream = float4(0, 0, 0, 1);
	if (lane > 0u)
		upstream = gAmbientIntervals[lane - 1u];
	RWCloudAmbientSweep[uint3(group.xy, group.z * 32u + lane)] = float4(halfTransmission * upstream.rgb + (1.0 - halfTransmission) * source,
		halfTransmission * upstream.a);
}

	[numthreads(4, 4, 4)] void projectCloudAmbient(uint3 index : SV_DispatchThreadID)
{
	const CloudAmbientTransport::Grid grid = CloudAmbientTransport::CurrentGrid(CloudAmbientCascade);
	if (any(index >= grid.size))
		return;
	const float altitude = (grid.origin.z + (index.z + 0.5) * grid.spacing.z) * GAME_UNITS_PER_METER;
	const float2 xy = (grid.origin.xy + (float2(index.xy) + 0.5) * grid.spacing.xy) * GAME_UNITS_PER_METER;
	const float3 position = float3(xy, altitude - CloudCurvatureOffset(xy - FrameBuffer::CameraPosAdjust.xy, VolumetricCloudBuffer[0].cloudCurvatureRadius));
	const CloudAmbientTransport::MiddleEnvironment middle = CloudAmbientTransport::MiddleEnvironmentAt(position);
	float3 q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0, q4 = 0.0;
	float3 q5 = 0.0, q6 = 0.0, q7 = 0.0, q8 = 0.0;
	[loop] for (uint directionIndex = 0u; directionIndex < 26u; ++directionIndex)
	{
		const float4 direction = TexCloudAmbientBoundary[uint2(directionIndex, 0)];
		const uint major = CloudAmbientTransport::MajorAxis(direction.xyz, grid);
		const uint a = (major + 1u) % 3u;
		const uint b = (major + 2u) % 3u;
		const float3 offset = direction.xyz / grid.spacing * (grid.spacing[major] / direction[major]);
		const float shear = float(index[major]) - 0.5 * (float(grid.size[major]) - 1.0);
		const float3 address = float3(float(index[a]) - shear * offset[a] + 0.5 * grid.size.x,
			float(index[b]) - shear * offset[b] + 0.5 * grid.size.x,
			(direction[major] > 0.0 ? float(grid.size[major] - 1u - index[major]) : float(index[major])) + directionIndex * 32u);
		const float4 interval = TexCloudAmbientSweep.SampleLevel(TransmittanceSampler, (address + 0.5) / float3(64, 64, 832), 0);
		const float3 boundary = CloudAmbientTransport::MiddleBoundary(middle, direction.xyz, CloudAmbientTransport::Boundary(directionIndex, altitude));
		const float3 incoming = direction.w * max(interval.rgb + interval.a * boundary, 0.0);
		q0 += CloudAmbientTransport::MomentBasis(0u, direction.xyz) * incoming;
		q1 += CloudAmbientTransport::MomentBasis(1u, direction.xyz) * incoming;
		q2 += CloudAmbientTransport::MomentBasis(2u, direction.xyz) * incoming;
		q3 += CloudAmbientTransport::MomentBasis(3u, direction.xyz) * incoming;
		q4 += CloudAmbientTransport::MomentBasis(4u, direction.xyz) * incoming;
		q5 += CloudAmbientTransport::MomentBasis(5u, direction.xyz) * incoming;
		q6 += CloudAmbientTransport::MomentBasis(6u, direction.xyz) * incoming;
		q7 += CloudAmbientTransport::MomentBasis(7u, direction.xyz) * incoming;
		q8 += CloudAmbientTransport::MomentBasis(8u, direction.xyz) * incoming;
	}
	RWCloudAmbientSource[uint3(index.xy, index.z + 0u * grid.size.z)] = float4(q0, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 1u * grid.size.z)] = float4(q1, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 2u * grid.size.z)] = float4(q2, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 3u * grid.size.z)] = float4(q3, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 4u * grid.size.z)] = float4(q4, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 5u * grid.size.z)] = float4(q5, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 6u * grid.size.z)] = float4(q6, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 7u * grid.size.z)] = float4(q7, 0.0);
	RWCloudAmbientSource[uint3(index.xy, index.z + 8u * grid.size.z)] = float4(q8, 0.0);
}

groupshared float3 gAmbientBoundary[32];
groupshared float3 gAmbientDirections[32];
groupshared float3 gAmbientColors[512];
groupshared float3 gAmbientReduction[512];
groupshared uint3 gAmbientBins[512];
groupshared float3 gAmbientWeights[512];
groupshared float gAmbientBinWeight[512];
[numthreads(512, 1, 1)] void buildCloudAmbientBoundary(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	float weight = 0.0;
	float3 selected = 0.0;
	if (lane < 26u) {
		selected = CloudAmbientTransport::OrientedDirection(lane, weight);
		gAmbientDirections[lane] = selected;
	}
	GroupMemoryBarrierWithGroupSync();
	float sampleWeight;
	const float3 sampleDirection = CloudAmbientTransport::Direction(lane, sampleWeight);
	const float3 radiance = TexCloudAmbientLighting[uint2(lane, group.y + 1u)].rgb;
	uint3 bins;
	float3 weights;
	CloudAmbientTransport::BoundaryWeights(sampleDirection, bins, weights);
	gAmbientBins[lane] = bins;
	gAmbientWeights[lane] = sampleWeight * weights;
	gAmbientColors[lane] = radiance;
	gAmbientReduction[lane] = sampleWeight * radiance;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 256u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride)
			gAmbientReduction[lane] += gAmbientReduction[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u)
		gAmbientDirections[30] = gAmbientReduction[0];
	GroupMemoryBarrierWithGroupSync();
	const uint bin = lane / 16u;
	const uint binLane = lane % 16u;
	float3 boundary = 0.0;
	float solidAngle = 0.0;
	if (bin < 26u) {
		[loop] for (uint sampleIndex = binLane; sampleIndex < 512u; sampleIndex += 16u)
		{
			const float binWeight = dot(float3(gAmbientBins[sampleIndex] == bin), gAmbientWeights[sampleIndex]);
			boundary += binWeight * gAmbientColors[sampleIndex];
			solidAngle += binWeight;
		}
	}
	gAmbientReduction[lane] = boundary;
	gAmbientBinWeight[lane] = solidAngle;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint binStride = 8u; binStride > 0u; binStride >>= 1u)
	{
		if (binLane < binStride) {
			gAmbientReduction[lane] += gAmbientReduction[lane + binStride];
			gAmbientBinWeight[lane] += gAmbientBinWeight[lane + binStride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane < 26u) {
		boundary = gAmbientReduction[lane * 16u] / max(gAmbientBinWeight[lane * 16u], 1e-8);
		gAmbientBoundary[lane] = weight * boundary;
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane == 0u) {
		float3 average = 0.0;
		[unroll] for (uint averageDirection = 0u; averageDirection < 26u; ++averageDirection)
			average += gAmbientBoundary[averageDirection];
		gAmbientDirections[31] = gAmbientDirections[30] / max(average, 1e-8);
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane < 26u) {
		boundary *= gAmbientDirections[31];
		gAmbientBoundary[lane] = weight * boundary;
		RWCloudAmbientBoundary[uint2(lane, group.y + 1u)] = float4(boundary, 0.0);
		// Cache the external fallback separately from the spatial transport
		// boundary. MainView only reads its existing SH; no middle-weather,
		// ray intersections or hemisphere reconstruction in that hot path.
		const float2 range = CloudAmbientTransport::AltitudeRange();
		const float fraction = group.y * 0.25;
		const float altitude = lerp(range.x, range.y, fraction * fraction);
		const float3 position = float3(FrameBuffer::CameraPosAdjust.xy, altitude);
		const CloudAmbientTransport::MiddleEnvironment middle = CloudAmbientTransport::MiddleEnvironmentAt(position);
		gAmbientBoundary[lane] = weight * CloudAmbientTransport::MiddleBoundary(middle, selected, boundary);
		if (group.y == 0u)
			RWCloudAmbientBoundary[uint2(lane, 0)] = float4(selected, weight);
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane < 9u) {
		float3 coefficient = 0.0;
		[unroll] for (uint momentDirection = 0u; momentDirection < 26u; ++momentDirection)
			coefficient += CloudAmbientTransport::MomentBasis(lane, gAmbientDirections[momentDirection]) * gAmbientBoundary[momentDirection];
		RWCloudAmbientBoundary[uint2(group.y, 6u + lane)] = float4(coefficient, 0.0);
	}
}

	[numthreads(1, 1, 1)] void updateCloudAmbientLayout()
{
	const CloudAmbientTransport::Grid grid = CloudAmbientTransport::CurrentGrid(CloudAmbientCascade);
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	RWCloudAmbientLayout[uint2(0, CloudAmbientCascade)] = float4(grid.origin, grid.spacing.x);
	RWCloudAmbientLayout[uint2(1, CloudAmbientCascade)] = float4(grid.spacing.z, grid.size.x, grid.size.z, 1.0);
	RWCloudAmbientLayout[uint2(2, CloudAmbientCascade)] = float4(info.noiseWindOffset, FrameBuffer::CameraPosAdjust.xy);
	RWCloudAmbientLayout[uint2(3, CloudAmbientCascade)] = float4(info.cloudCurvatureRadius, CloudAmbientIterations, 0, 0);
	RWCloudAmbientLayout[uint2(4, CloudAmbientCascade)] = CloudAmbientPhase;
}
// Cell averages preserve thin layers while the primary march retains their full density.
[numthreads(4, 4, 4)] void buildCloudAmbientDensity(uint3 index : SV_DispatchThreadID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const CloudAmbientTransport::Grid grid = CloudAmbientTransport::CurrentGrid(CloudAmbientCascade);
	if (any(index >= grid.size))
		return;
	const float3 center = grid.origin + (float3(index) + 0.5) * grid.spacing;
	static const float3 quadrature[4] = {
		float3(-1, -1, -1), float3(-1, 1, 1), float3(1, -1, 1), float3(1, 1, -1)
	};
	float2 medium = 0.0;
	[unroll(4)] for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
	{
		const float3 offset = quadrature[sampleIndex] * 0.288675135;
		const float2 xy = center.xy + offset.xy * grid.spacing.xy;
		const float2 anchored = xy * GAME_UNITS_PER_METER;
		const float2 field = CloudFieldPosition(xy - info.noiseWindOffset * GAME_UNIT_TO_M, info.cloudEvolution.xy);
		float2 heights = SampleCloudHeights(field * GAME_UNITS_PER_METER, anchored).xy * GAME_UNIT_TO_M;
		// A storm can lie below the weather layer; include its support before quadrature.
		[loop] for (uint stormIndex = 0u; stormIndex < CloudStormCount(); ++stormIndex)
		{
			const CloudStormData storm = CloudStorms[stormIndex];
			const float2 lo = IsVolcanic(storm) ? storm.plumeLower.xy : storm.centerRadiusStrength.xy + min(storm.driftDensity.xy, 0.0) - storm.animation.z;
			const float2 hi = IsVolcanic(storm) ? storm.plumeUpper.xy : storm.centerRadiusStrength.xy + max(storm.driftDensity.xy, 0.0) + storm.animation.z;
			if (all(xy >= lo) && all(xy <= hi)) {
				heights.x = min(heights.x, storm.altitudeMotion.x);
				heights.y = max(heights.y, storm.altitudeMotion.x + storm.altitudeMotion.y);
			}
		}
		const float bottom = max(center.z - 0.5 * grid.spacing.z, heights.x);
		const float top = min(center.z + 0.5 * grid.spacing.z, heights.y);
		if (top > bottom) {
			const float profileAltitude = lerp(bottom, top, 0.5 + offset.z) * GAME_UNITS_PER_METER;
			const float3 pos = float3(anchored, profileAltitude - CloudCurvatureOffset(anchored - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius));
			CloudDensityContext context;
			// Middle cloud has an analytic ambient response and an external boundary
			// filter. Exclude it here even where the altitude bands overlap.
			float extinction = sampleCloudDensity(pos, GetCloudLayer(info), 0.0, 700.0, 0.0, context);
			const float2 phase = CloudMaterialAmbient(context.ndf);
			const float sigma = extinction * ((top - bottom) / grid.spacing.z) * phase.x;
			medium += sigma * float2(1.0, phase.y);
		}
	}
	RWCloudAmbientDensity[index] = 0.25 * medium * info.viewExtinction;
}

groupshared float3 gAmbientHemispheres[512];
[numthreads(512, 1, 1)] void buildCloudAmbientLighting(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	float weight;
	const float3 d = CloudAmbientTransport::Direction(lane, weight);
	const float2 range = CloudAmbientTransport::AltitudeRange();
	const float fraction = group.y * 0.25;
	const float altitude = lerp(range.x, range.y, fraction * fraction);
	const float3 atmosphere = d.z > 0.0 ? CloudAmbientSky(d, altitude) :
	                                      CloudAmbientGround(d, TexCloudAmbientSH[int2(5, 0)].rgb, altitude);
	const float3 radiance = CirrusBoundaryRadiance(float3(FrameBuffer::CameraPosAdjust.xy, altitude), d, atmosphere);
	RWCloudAmbientLighting[uint2(lane, group.y + 1u)] = float4(radiance, 0.0);
	gAmbientReduction[lane] = weight * radiance;
	gAmbientHemispheres[lane] = weight * radiance;
	const float luminance = d.z > 0.0 ? dot(radiance, float3(0.2126, 0.7152, 0.0722)) : 0.0;
	gAmbientColors[lane].x = luminance / 64.0;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 256u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride)
			gAmbientReduction[lane] += gAmbientReduction[lane + stride];
		if (stride < 256u && lane % 256u < stride)
			gAmbientHemispheres[lane] += gAmbientHemispheres[lane + stride];
		if (group.y == 4u && stride < 64u && (lane % 64u) < stride)
			gAmbientColors[lane].x += gAmbientColors[lane + stride].x;
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		RWCloudAmbientLighting[uint2(group.y, 0)] = float4(gAmbientReduction[0], 0.0);
		RWCloudAmbientLighting[uint2(5u + group.y, 0)] = float4(gAmbientHemispheres[256], 0.0);
		RWCloudAmbientLighting[uint2(10u + group.y, 0)] = float4(gAmbientHemispheres[0], 0.0);
	}
	if (group.y < 4u)
		return;
	GroupMemoryBarrierWithGroupSync();
	const float ringMean = gAmbientColors[(lane / 64u) * 64u].x;
	gAmbientReduction[lane] = d * (weight * max(luminance - ringMean, 0.0));
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint axisStride = 256u; axisStride > 0u; axisStride >>= 1u)
	{
		if (lane < axisStride)
			gAmbientReduction[lane] += gAmbientReduction[lane + axisStride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		const float3 axis = gAmbientReduction[0];
		RWCloudAmbientLighting[uint2(0, 6)] = float4(length(axis) > 1e-8 ? normalize(axis) : float3(0, 1, 0), 0);
	}
}

#endif

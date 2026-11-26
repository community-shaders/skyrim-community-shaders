#ifndef PHYSICAL_SKY_CLOUD_VOXEL_HLSLI
#define PHYSICAL_SKY_CLOUD_VOXEL_HLSLI

// A single placement uses fixed-volume traversal. The generic path
// retains scene-transition overlap and multiple independently placed assets.
#ifdef CLOUD_VOXEL_SINGLE
#	define CLOUD_VOXEL_INSTANCE_LOOP [unroll]
#else
#	define CLOUD_VOXEL_INSTANCE_LOOP [loop]
#endif

uint CloudVoxelCount()
{
#ifdef CLOUD_VOXEL_SINGLE
	return 1u;
#elif defined(CLOUD_VOXELS)
	return VolumetricCloudBuffer[0].voxelCount;
#else
	return 0u;
#endif
}

#ifdef CLOUD_VOXELS
struct VoxelCloudData
{
	float4 centerWeight;
	float4 axisX, axisY, axisZ;
	float4 parameters;
	float4 noise;
};

uint VoxelAssetSlot(VoxelCloudData cloud)
{
#	ifdef CLOUD_VOXEL_SINGLE
	// VoxelCloudManager assigns the first active asset to slot zero.
	return 0u;
#	else
	return (uint)cloud.parameters.w;
#	endif
}

Texture2DArray<float3> TexVoxelModeling[8] : register(t60);
Texture2DArray<float3> TexVoxelDistance[8] : register(t68);
StructuredBuffer<VoxelCloudData> VoxelClouds : register(t77);

// D3D11 supports BC volumes as 2D slice arrays. Interpolate Z explicitly to
// retain compressed fields rather than expanding them 8x in GPU memory.
float3 SampleVoxelField(Texture2DArray<float3> field, float3 uv, float mip)
{
	uint width, height, slices;
	field.GetDimensions(width, height, slices);
	const float z = clamp(uv.z * slices - .5, 0.0, float(slices - 1u));
	const float lower = floor(z);
	return lerp(field.SampleLevel(TransmittanceSampler, float3(uv.xy, lower), 0.0),
		field.SampleLevel(TransmittanceSampler, float3(uv.xy, min(lower + 1.0, float(slices - 1u))), 0.0), z - lower);
}

// SM5 resource indices must be literal. Placements sharing an asset reuse its slot.
#	define VOXEL_SAMPLE_CASES(texture, function, uv, mip) \
	case 0:                                                \
		result = function(texture[0], uv, mip);            \
		break;                                             \
	case 1:                                                \
		result = function(texture[1], uv, mip);            \
		break;                                             \
	case 2:                                                \
		result = function(texture[2], uv, mip);            \
		break;                                             \
	case 3:                                                \
		result = function(texture[3], uv, mip);            \
		break;                                             \
	case 4:                                                \
		result = function(texture[4], uv, mip);            \
		break;                                             \
	case 5:                                                \
		result = function(texture[5], uv, mip);            \
		break;                                             \
	case 6:                                                \
		result = function(texture[6], uv, mip);            \
		break;                                             \
	case 7:                                                \
		result = function(texture[7], uv, mip);            \
		break;

float3 VoxelModeling(uint slot, float3 uv, float mip)
{
	float3 result = 0.0;
	switch (slot) {
		VOXEL_SAMPLE_CASES(TexVoxelModeling, SampleVoxelField, uv, mip)
	}
	return result;
}

float3 VoxelDistanceChannels(uint slot, float3 uv)
{
	float3 result = 0.0;
	switch (slot) {
		VOXEL_SAMPLE_CASES(TexVoxelDistance, SampleVoxelField, uv, 0.0)
	}
	return result;
}

#	undef VOXEL_SAMPLE_CASES

float3 VoxelCoordinates(float3 metres, VoxelCloudData cloud)
{
	const float3 offset = metres - cloud.centerWeight.xyz;
	return float3(dot(offset, cloud.axisX.xyz) * cloud.axisX.w,
			   dot(offset, cloud.axisY.xyz) * cloud.axisY.w, dot(offset, cloud.axisZ.xyz) * cloud.axisZ.w) +
	       .5;
}

float VoxelSignedDistance(float3 uv, VoxelCloudData cloud)
{
	const float3 packed = VoxelDistanceChannels(VoxelAssetSlot(cloud), uv);
	// The NVDF RGB encoding is linear, so filtered packed samples remain valid.
	return ((packed.y * .035294145 + packed.x + packed.z * .0006920431) * 4352.0 - 256.0) * cloud.parameters.x;
}

float2 VoxelAltitude(VoxelCloudData cloud)
{
	const float halfHeight = .5 * (abs(cloud.axisX.z) / cloud.axisX.w + abs(cloud.axisY.z) / cloud.axisY.w + abs(cloud.axisZ.z) / cloud.axisZ.w);
	return (cloud.centerWeight.z + float2(-halfHeight, halfHeight)) * GAME_UNITS_PER_METER;
}

float2 VoxelBoxInterval(float3 origin, float3 direction, VoxelCloudData cloud, float start)
{
	const float3 uv = VoxelCoordinates(origin * GAME_UNIT_TO_M, cloud);
	const float3 ray = float3(dot(direction, cloud.axisX.xyz) * cloud.axisX.w,
		dot(direction, cloud.axisY.xyz) * cloud.axisY.w, dot(direction, cloud.axisZ.xyz) * cloud.axisZ.w);
	const bool3 parallel = abs(ray) < 1e-12;
	if (any(parallel && (uv < 0.0 || uv > 1.0)))
		return 0.0;
	const float3 inverse = rcp(parallel ? 1e-12 : ray);
	const float3 first = -uv * inverse, last = (1.0 - uv) * inverse;
	const float3 lower = parallel ? -1e30 : min(first, last);
	const float3 upper = parallel ? 1e30 : max(first, last);
	const float2 interval = float2(max(start * GAME_UNIT_TO_M, max(lower.x, max(lower.y, lower.z))), min(upper.x, min(upper.y, upper.z)));
	return interval.y > interval.x ? interval * GAME_UNITS_PER_METER : 0.0;
}

float VoxelHeightReveal(float height, float strength)
{
	if (strength >= 1.0)
		return 1.0;
	if (strength <= 0.0)
		return 0.0;
	// Apply the height reveal along the instance's local Z axis.
	const float threshold = saturate(1.0 - strength) * 1.2;
	const float lower = max(0.0, threshold - .2);
	return saturate((height - lower) / max(min(1.0, threshold) - lower, 1e-6));
}

float EvaluateVoxelDensity(float3 metres, float3 field, VoxelCloudData cloud, float distance, float mipBias, float minimumMip, bool enableNearDetail)
{
	if (field.x <= 0.0 || field.z <= 0.0)
		return 0.0;
#	ifdef CLOUD_VOXEL_COARSE
	return field.x * field.z;
#	else
	[branch] if (VolumetricCloudBuffer[0].skipShapeNoise != 0u) return field.x * field.z;
	const float mip = max(log2(abs(distance * cloud.noise.y) + 1.0) + mipBias, minimumMip);
	const float3 coordinate = float3(metres.xy - VolumetricCloudBuffer[0].noiseWindOffset * GAME_UNIT_TO_M, metres.z) * (cloud.noise.x * .01);
	const float4 noise = NoiseVoxel.SampleLevel(TileableSampler, coordinate, mip);
	const float baseNoise = lerp(noise.y, noise.x, field.x);
	const float smallNoise = noise.w * .2 + .1;
	float threshold = baseNoise + (smallNoise - baseNoise + (noise.z * .3 - smallNoise) * pow(max(field.x, 0.0), .0625)) * field.y;
	const bool nearDetail = distance < 150.0 && enableNearDetail;
	const float blend = saturate((distance - 50.0) * .01);
	if (nearDetail) {
		const float foldedW = abs(abs(noise.w * 2.0 - 1.0) * 2.0 - 1.0);
		const float foldedY = 1.0 - pow(abs(abs(noise.y * 2.0 - 1.0) * 2.0 - 1.0), 4.0);
		const float nearNoise = saturate(lerp(foldedY, foldedW * foldedW, field.y));
		threshold = nearNoise + (blend * .100000024 + .9) * (threshold - nearNoise);
	}
	const float verticalDensity = pow(saturate(field.z), 4.0);
	const float baseDensity = verticalDensity * saturate((field.x - threshold) / max(1.0 - threshold, 1e-6));
	if (baseDensity <= 0.0)
		return 0.0;
	const float logDensity = (max(1e-10, verticalDensity) * .3 + .3) * log2(baseDensity);
	const float density = nearDetail ? exp2((blend * .5 + .5) * logDensity) * (blend * .334 + .666) : exp2(logDensity);
	return (saturate((distance - 10.0) * .009090909) * .75 + .25) * density;
#	endif
}

float AddVoxelDensity(float3 pos, float mipBias, float viewDistance, float minimumMip,
	uint2 occupied, float ndfDensity, inout CloudDensityContext context)
{
	float voxelDensity = 0.0, referenceDensity = 0.0;
	NDFInfo voxelContext = (NDFInfo)0;
	float dominant = 0.0;
	CLOUD_VOXEL_INSTANCE_LOOP for (uint i = 0u; i < CloudVoxelCount(); ++i)
	{
		if (((i < 32u ? occupied.x : occupied.y) & (1u << (i & 31u))) == 0u)
			continue;
		const VoxelCloudData cloud = VoxelClouds[i];
		const float3 uv = VoxelCoordinates(pos * GAME_UNIT_TO_M, cloud);
		if (any(uv < 0.0) || any(uv > 1.0))
			continue;
		const float3 field = VoxelModeling(VoxelAssetSlot(cloud), uv, 0.0) * VoxelHeightReveal(uv.z, cloud.parameters.z);
		const float density = EvaluateVoxelDensity(pos * GAME_UNIT_TO_M, field, cloud, viewDistance, mipBias, minimumMip, mipBias == 0.0) * cloud.parameters.y * cloud.centerWeight.w;
		voxelDensity += density;
		referenceDensity += density * cloud.parameters.y * cloud.centerWeight.w;
		if (density > dominant) {
			dominant = density;
			voxelContext.in_layer = true;
			voxelContext.coverage = field.x;
			voxelContext.dimension_profile = field.x * field.z;
			voxelContext.height_fraction = uv.z;
			voxelContext.altitude = VoxelAltitude(cloud);
		}
	}
	const float total = ndfDensity + voxelDensity;
	if (voxelDensity > 0.0) {
		const float ndfWeight = ndfDensity / total;
		const float lightDensity = context.light_density + voxelDensity;
		if (ndfDensity <= 0.0)
			context.ndf = voxelContext;
		context.ndf.voxelBlend = voxelDensity / total;
		context.ndf.voxelDensityScale = referenceDensity / voxelDensity;
		context.ndf.stormBlend *= ndfWeight;
		context.storm_displacement *= ndfWeight;
		context.light_density = lightDensity;
	}
	return total;
}

float sampleCloudDensity(float3 pos, CloudLayer layer, float mipBias, float viewDistance, float minimumMip, out CloudDensityContext context)
{
	const float ndfDensity = sampleNdfCloudDensity(pos, layer, mipBias, viewDistance, minimumMip, context);
	return AddVoxelDensity(pos, mipBias, viewDistance, minimumMip, uint2(0xffffffffu, 0xffffffffu), ndfDensity, context);
}

// Resolve three near-detail probes and store the longer coarse integral.
// The response to this optical depth remains the shared NDF lighting model.
Texture2DArray<float> TexVoxelLighting : register(t85);
RWTexture2DArray<float> RWVoxelLighting : register(u0);
cbuffer VoxelLightingUpdate : register(b4)
{
	uint VoxelLightingFirstSlice;
	uint VoxelLightingSliceCount;
	uint2 VoxelLightingPadding;
};
static const uint VOXEL_LIGHTING_DEPTH = 32u;

float VoxelCoarseDensity(float3 uv, VoxelCloudData cloud)
{
	if (any(uv < 0.0) || any(uv > 1.0))
		return 0.0;
	const float3 field = VoxelModeling(VoxelAssetSlot(cloud), uv, 0.0) * VoxelHeightReveal(uv.z, cloud.parameters.z);
	return field.x * field.z;
}

[numthreads(4, 4, 4)] void buildVoxelLighting(uint3 pixel : SV_DispatchThreadID) {
	pixel.z += VoxelLightingFirstSlice;
	if (pixel.z >= VoxelLightingFirstSlice + VoxelLightingSliceCount)
		return;
	uint width, height, slices;
	RWVoxelLighting.GetDimensions(width, height, slices);
	if (any(pixel >= uint3(width, height, slices)))
		return;
	const uint index = pixel.z / VOXEL_LIGHTING_DEPTH;
	const VoxelCloudData cloud = VoxelClouds[index];
	float3 uv = (float3(pixel.xy, pixel.z % VOXEL_LIGHTING_DEPTH) + .5) / float3(width, height, VOXEL_LIGHTING_DEPTH);
	const float3 light = VolumetricCloudBuffer[0].dirlightDir;
	const float3 ray = float3(dot(light, cloud.axisX.xyz) * cloud.axisX.w,
		dot(light, cloud.axisY.xyz) * cloud.axisY.w, dot(light, cloud.axisZ.xyz) * cloud.axisZ.w);
	// VoxelCloudLightingShader: 256 coarse probes beyond the 16 m fine range.
	// This is the native occlusion estimator, not a 16 m midpoint quadrature:
	// multiplying every coarse sample by that step overwhelmed fine shadows.
	uv += ray * 16.0;
	float opticalDepth = 0.0;
	[loop] for (uint stepIndex = 0; stepIndex < 256u && all(uv >= 0.0) && all(uv <= 1.0); ++stepIndex)
	{
		const float sdf = VoxelSignedDistance(uv, cloud);
		const float step = max(1.0, sdf);
		const float encodedDistance = (sdf / cloud.parameters.x + 256.0) / 4352.0;
		const float density = VoxelCoarseDensity(uv, cloud);
		opticalDepth += density * (saturate(encodedDistance * 136.0 - 7.0) * 7.0 + 1.0);
		uv += ray * step;
	}
	RWVoxelLighting[pixel] = min(opticalDepth, 65504.0);
}

float VoxelLightingIntegral(uint index, float3 uv)
{
	const float z = clamp(uv.z * VOXEL_LIGHTING_DEPTH - .5, 0.0, float(VOXEL_LIGHTING_DEPTH - 1u));
	const float first = floor(z);
	const float slice = index * VOXEL_LIGHTING_DEPTH;
	return lerp(TexVoxelLighting.SampleLevel(TransmittanceSampler, float3(uv.xy, slice + first), 0),
		TexVoxelLighting.SampleLevel(TransmittanceSampler, float3(uv.xy, slice + min(first + 1.0, float(VOXEL_LIGHTING_DEPTH - 1u))), 0), z - first);
}

float TraceVoxelLighting(float3 pos, float viewDistance)
{
	const float3 metres = pos * GAME_UNIT_TO_M;
	// Native spatial hash is shared by all three fine probes at this point.
	uint hash = asuint(metres.x);
	hash = (((hash >> 16u) ^ (hash & 0x00ffffffu)) * 10368889u) + asuint(metres.y);
	hash = (((hash >> 16u) ^ (hash & 0x00ffffffu)) * 10368889u) + asuint(metres.z);
	hash = ((hash >> 16u) ^ (hash & 0x00ffffffu)) * 10368889u;
	hash = ((hash >> 16u) ^ (hash & 0x00ffffffu)) * 10368889u;
	const float jitter = asfloat(((hash >> 16u) ^ (hash & 0x007fffffu)) | 0x3f800000u) - 1.5;
	const float3 direction = VolumetricCloudBuffer[0].dirlightDir;
	float depth = 0.0;
	CLOUD_VOXEL_INSTANCE_LOOP for (uint i = 0u; i < CloudVoxelCount(); ++i)
	{
		const VoxelCloudData cloud = VoxelClouds[i];
		const float3 uv = VoxelCoordinates(metres, cloud);
		if (any(uv < 0.0) || any(uv > 1.0))
			continue;
		float fine = 0.0;
		[unroll] for (uint probe = 0u; probe < 3u; ++probe)
		{
			const float3 samplePos = metres + direction * ((probe + 1.0 + jitter) * (16.0 / 3.0));
			const float3 sampleUv = VoxelCoordinates(samplePos, cloud);
			if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
				continue;
			const float3 field = VoxelModeling(VoxelAssetSlot(cloud), sampleUv, 0.0) * VoxelHeightReveal(sampleUv.z, cloud.parameters.z);
			fine += EvaluateVoxelDensity(samplePos, field, cloud, viewDistance, 0.0, 0.0, false) * (16.0 / 3.0);
		}
		depth += (VoxelLightingIntegral(i, uv) + fine) * cloud.parameters.y * cloud.centerWeight.w;
	}
	return depth;
}

float CloudVoxelStep(float3 pos, float distance, float angularFootprint, out uint2 occupied)
{
	occupied = 0u;
	const float metres = distance * GAME_UNIT_TO_M;
	const float baseStep = (1.0 + 2.0 * saturate((metres - 800.0) * .0003125)) *
	                       VolumetricCloudBuffer[0].marchStepScale * max(1.0, sqrt(metres) * .08) * (angularFootprint > 0.0 ? 8.0 : 1.0);
	float step = 1e30;
	CLOUD_VOXEL_INSTANCE_LOOP for (uint i = 0u; i < CloudVoxelCount(); ++i)
	{
		const VoxelCloudData cloud = VoxelClouds[i];
		const float3 uv = VoxelCoordinates(pos * GAME_UNIT_TO_M, cloud);
		if (all(uv >= 0.0) && all(uv <= 1.0)) {
			const float sdf = VoxelSignedDistance(uv, cloud);
			step = min(step, max(baseStep, sdf));
			// Native VoxelCloudComputeShader tests SDF before jittering the sample.
			// Empty-space steps must not evaluate modeling, noise or lighting.
			if (sdf < 0.0) {
				if (i < 32u)
					occupied.x |= 1u << i;
				else
					occupied.y |= 1u << (i - 32u);
			}
		} else {
			const float3 outside = max(abs(uv - .5) - .5, 0.0) / float3(cloud.axisX.w, cloud.axisY.w, cloud.axisZ.w);
			step = min(step, max(length(outside), .01));
		}
	}
	return step * GAME_UNITS_PER_METER;
}
#endif

float2 CloudVoxelInterval(float3 eye, float3 direction, float start, bool nearestOnly)
{
	float2 result = 0.0;
#ifdef CLOUD_VOXELS
	CLOUD_VOXEL_INSTANCE_LOOP for (uint i = 0u; i < CloudVoxelCount(); ++i)
	{
		const float2 interval = VoxelBoxInterval(eye, direction, VoxelClouds[i], start);
		if (interval.y <= interval.x)
			continue;
		if (result.y <= result.x || (nearestOnly && interval.x < result.x))
			result = interval;
		else if (!nearestOnly || interval.x == result.x)
			result = float2(min(result.x, interval.x), max(result.y, interval.y));
	}
#endif
	return result;
}
#endif

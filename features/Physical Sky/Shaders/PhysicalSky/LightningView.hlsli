#ifndef PHYSICAL_SKY_LIGHTNING_VIEW_HLSLI
#define PHYSICAL_SKY_LIGHTNING_VIEW_HLSLI

struct LightningSegment
{
	float4 start;
	float4 end;
};
StructuredBuffer<LightningSegment> LightningSegments : register(t94);
RWStructuredBuffer<float4> LightningCandidates : register(u4);
Texture2D<float2> LightningResolvedMask : register(t95);
TextureCube<float2> LightningResolvedCubeMask : register(t95);
RWTexture2D<float4> RWLightningChannels : register(u4);
RWTexture2D<float4> RWLightningColor : register(u5);
RWTexture2DArray<float4> RWLightningCubeColor : register(u5);

float LightningHash(float value)
{
	return frac(sin(value * 12.9898 + 78.233) * 43758.5453);
}

[numthreads(32, 1, 1)] void findLightningSource(uint3 id : SV_DispatchThreadID) {
	const uint index = id.x;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float seed = SharedData::physSkyData.lightningPlacement.z * 71.0 + index * 13.0;
	const float angle = LightningHash(seed) * 6.2831853;
	const float distanceSample = LightningHash(seed + 1.0);
	float2 xy = FrameBuffer::CameraPosAdjust.xy + float2(cos(angle), sin(angle)) *
	                                                  lerp(SharedData::physSkyData.lightningPlacement.x, SharedData::physSkyData.lightningPlacement.y, distanceSample * distanceSample);
	float bottom = info.lowCloudTraceBottomAltitude, top = info.lowCloudTraceTopAltitude;
	// Match the weather renderer's moving field and local height overrides.
	// The global trace slab is not the cloud column's actual base and top.
	const float2 fieldMeters = CloudFieldPosition((xy - info.noiseWindOffset) * GAME_UNIT_TO_M, info.cloudEvolution.xy);
	const float2 heights = SampleCloudHeights(fieldMeters * GAME_UNITS_PER_METER, xy).xy;
	if (heights.y > heights.x) {
		const float curvature = CloudCurvatureOffset(xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
		bottom = heights.x - curvature;
		top = heights.y - curvature;
	}
	[loop] for (uint storm = 0u; storm < CloudStormCount(); ++storm)
	{
		if (IsVolcanic(CloudStorms[storm]))
			continue;
		const CloudStormData instance = CloudStorms[storm];
		const float2 stormXY = (instance.centerRadiusStrength.xy + float2(cos(angle), sin(angle)) * instance.centerRadiusStrength.z * sqrt(LightningHash(seed + 2.0)) * 0.75) * GAME_UNITS_PER_METER;
		const float stormDistance = length(stormXY - FrameBuffer::CameraPosAdjust.xy);
		if (stormDistance < SharedData::physSkyData.lightningPlacement.x || stormDistance > SharedData::physSkyData.lightningPlacement.y)
			continue;
		xy = stormXY;
		bottom = instance.altitudeMotion.x * GAME_UNITS_PER_METER;
		top = bottom + instance.altitudeMotion.y * GAME_UNITS_PER_METER;
		break;
	}
	float4 candidate = 0.0;
	const float horizontalDistance = length(xy - FrameBuffer::CameraPosAdjust.xy);
	if (horizontalDistance < SharedData::physSkyData.lightningPlacement.x || horizontalDistance > SharedData::physSkyData.lightningPlacement.y) {
		LightningCandidates[index] = candidate;
		return;
	}
	// Prefer the base for ground strikes, but keep searching the whole column:
	// erosion and local storm height maps can leave its lowest samples empty.
	const bool baseDischarge = SharedData::physSkyData.lightningPlacement.w > 0.0;
	const uint heightSamples = baseDischarge ? 16u : 5u;
	// Density sampling contains nested storm/voxel loops; keep this search dynamic.
	[loop] for (uint h = 0u; h < heightSamples; ++h)
	{
		const float height = baseDischarge ? 0.03 + h * 0.06 : 0.12 + h * 0.15;
		const float3 position = float3(xy, lerp(bottom, top, height));
		CloudDensityContext context;
		const float density = sampleCloudDensity(position, GetCloudLayer(info), 2.0, 3000.0, 0.0, context);
		if (density > 0.03 && context.ndf.dimension_profile > 0.1) {
			candidate = float4(position, (top - bottom) * GAME_UNIT_TO_M);
			break;
		}
	}
	LightningCandidates[index] = candidate;
}

// Density only: a channel is attenuated by the cloud in FRONT of its depth.
// The final scene/cloud transmittance includes clouds behind the channel.
float LightningChannelTransmission(float3 direction, float distance)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
	float opticalDepth = 0.0;
	const float step = distance / 48.0;
	[loop] for (uint i = 0u; i < 48u && opticalDepth < 10.0; ++i)
	{
		CloudDensityContext context;
		const float cursor = (i + 0.5) * step;
		const float density = sampleCloudDensity(eye + direction * cursor, GetCloudLayer(info), 2.0, cursor * GAME_UNIT_TO_M, 0.0, context);
		opticalDepth += density * info.viewExtinction * step * GAME_UNIT_TO_M;
	}
	return exp(-opticalDepth);
}

float4 LightningChannel(float3 direction, float sceneDistance, float footprint, float intensity)
{
	if (!Lightning::HasSource() || intensity <= 0.0)
		return 0.0;
	float energy = 0.0, depth = 0.0, strongest = 0.0;
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz;
	// Reject rays outside the entire branched discharge before reading segments.
	const float3 source = SharedData::physSkyData.lightningPositionRadius.xyz;
	const float3 end = SharedData::physSkyData.lightningEndPosition.xyz;
	const float3 center = (source + end) * 0.5 - eye;
	const float bound = length(end - source) * 0.5 + SharedData::physSkyData.lightningPositionRadius.w * 2.0;
	const float closest = clamp(dot(center, direction), 0.0, sceneDistance);
	if (dot(center - direction * closest, center - direction * closest) > bound * bound)
		return 0.0;
	const uint count = min((uint)SharedData::physSkyData.lightningControl.z, 128u);
	[loop] for (uint i = 0u; i < count; ++i)
	{
		const LightningSegment segment = LightningSegments[i];
		const float3 start = segment.start.xyz - eye;
		const float3 v = segment.end.xyz - segment.start.xyz;
		const float dv = dot(direction, v);
		const float denominator = dot(v, v) - dv * dv;
		const float t = saturate((dv * dot(direction, start) - dot(v, start)) / max(denominator, 1e-6));
		const float3 closestPoint = start + v * t;
		const float distance = dot(closestPoint, direction);
		if (distance <= 0.0 || distance > sceneDistance)
			continue;
		const float width = max(segment.start.w, footprint * distance * 0.65);
		const float separation2 = dot(closestPoint - distance * direction, closestPoint - distance * direction) / (width * width);
		if (separation2 > 100.0)
			continue;
		const float coverage = min(segment.start.w / width, 1.0);
		const float contribution = ((exp2(-4.0 * separation2) + 0.035 * exp2(-0.16 * separation2)) / 1.035) * coverage * segment.end.w;
		energy = max(energy, contribution);
		if (contribution > strongest) {
			strongest = contribution;
			depth = distance;
		}
	}
	if (energy <= 1e-7)
		return 0.0;
	const float transmission = LightningChannelTransmission(direction, depth);
	return float4(SharedData::physSkyData.lightningColorIntensity.rgb *
					  (intensity * SharedData::physSkyData.lightningPhotometry.y * energy * transmission),
		EncodeCloudDepth(depth));
}

[numthreads(8, 8, 1)] void compositeLightning(uint2 pixel : SV_DispatchThreadID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(pixel >= uint2(info.activeFrameDim)))
		return;
	float4 cloud = RWLightningColor[pixel];
	cloud.rgb += Lightning::ActiveMask(LightningResolvedMask[pixel]) * Lightning::Color();
	RWLightningColor[pixel] = float4(min(cloud.rgb, 65504.0), cloud.a);
	const float3 ray = CloudScreenRay(pixel);
	const float deviceDepth = TexDepth[pixel];
	const float sceneDistance = deviceDepth >= 1.0 - 1e-6 ? info.rayMarchRange :
	                                                        SharedData::GetScreenDepth(deviceDepth) / max(abs(mul(FrameBuffer::CameraView, float4(ray, 0)).z), 1e-6);
	const float footprint = 2.0 / max(abs(FrameBuffer::CameraProj[1][1]) * info.activeFrameDim.y, 1.0);
	float4 channel = LightningChannel(ray, sceneDistance, footprint, SharedData::physSkyData.lightningColorIntensity.w);
	if (!ViewAtmosphere::Enabled() && channel.a > 0.0)
		channel.rgb *= SampleCloudAerialPerspective(ray, DecodeCloudDepth(channel.a), 0.0).a;
	RWLightningChannels[pixel] = float4(min(channel.rgb, 65504.0), channel.a);
}

	[numthreads(8, 8, 1)] void compositeLightningCube(uint3 pixel : SV_DispatchThreadID)
{
	uint width, height, faces;
	RWLightningCubeColor.GetDimensions(width, height, faces);
	if (any(pixel >= uint3(width, height, faces)))
		return;
	const float3 direction = CloudCubeDirection(pixel.xy, pixel.z, width);
	float4 cloud = float4(Lightning::ActiveMask(LightningResolvedCubeMask.SampleLevel(TransmittanceSampler, direction, 0)) *
							  SharedData::physSkyData.lightningColorIntensity.rgb * SharedData::physSkyData.lightningPhotometry.x,
		0);
	const float4 channel = LightningChannel(direction, VolumetricCloudBuffer[0].rayMarchRange, 2.0 / width, 1.0);
	// Reflection composition applies foreground air together with the cloud source.
	cloud.rgb += channel.rgb;
	RWLightningCubeColor[pixel] = float4(min(cloud.rgb, 65504.0), cloud.a);
}
#endif

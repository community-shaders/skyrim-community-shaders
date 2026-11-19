#ifndef PHYSICAL_SKY_CLOUD_TEMPORAL_HLSLI
#define PHYSICAL_SKY_CLOUD_TEMPORAL_HLSLI

#include "PhysicalSky/CloudPhase.hlsli"

uint2 CloudPhaseOffset()
{
	return CloudPhaseOffset(VolumetricCloudBuffer[0].cloudFrameIndex);
}

float2 CloudScreenUv(uint2 pixel)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	return FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition((pixel + 0.5) * info.rcpFrameDim);
}

float3 CloudScreenRay(uint2 pixel)
{
	const float2 uv = CloudScreenUv(pixel);
	const float4 position = mul(FrameBuffer::CameraViewProjInverse, float4(uv * float2(2, -2) + float2(-1, 1), 1, 1));
	return normalize(position.xyz / position.w);
}

#include "PhysicalSky/CloudDispatch.hlsli"

float CloudSceneDeviceDepth(uint2 pixel, uint footprint)
{
	const uint2 origin = (pixel / footprint) * footprint;
	const uint2 maximum = uint2(VolumetricCloudBuffer[0].activeFrameDim) - 1u;
	float depth = 0.0;
	[unroll] for (uint y = 0u; y < footprint; ++y)
		[unroll] for (uint x = 0u; x < footprint; ++x)
			depth = max(depth, TexDepth[min(origin + uint2(x, y), maximum)]);
	return depth;
}

float CloudHistoryBlend(float2 motion)
{
	return saturate((length(motion) - 0.0001) * 2500.0) * 0.5 + 0.5;
}

float3 CloudSampleDisplacement(float3 ray, float4 metadata, float opacity, float4 stormMotion, out float uncertainty, out float projectionDepth)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	projectionDepth = metadata.x;
	const float cirrusFraction = saturate(metadata.z / max(opacity, 1e-6));
	if (cirrusFraction == 1.0) {
		uncertainty = 0.0;
		return float3(info.cirrusWindDelta, 0);
	}
	const float stormFraction = saturate(stormMotion.w / max(opacity, 1e-6));
	const float height = saturate(metadata.w / max(opacity - metadata.z - stormMotion.w, 1e-6));
	float lowDepth = DecodeCloudDepth(metadata.x);
	float2 projectionWeights = float2(1.0 - cirrusFraction, cirrusFraction);
	if (cirrusFraction > 0.0 && cirrusFraction < 0.99) {
		const float3 planetEye = float3(0, 0, FrameBuffer::CameraPosAdjust.z - info.bottomZ + info.planetRadius);
		const float2 shell = IntersectSpherePair(planetEye, ray, info.planetRadius + info.cirrusAltitude);
		const float cirrusDepth = shell.x >= 0.0 ? shell.x : shell.y;
		if (cirrusDepth > 0.0) {
			lowDepth = max((lowDepth - cirrusFraction * cirrusDepth) / (1.0 - cirrusFraction), 0.0);
			if (lowDepth > 0.0) {
				// Angular motion is proportional to inverse distance.
				projectionWeights /= float2(lowDepth, cirrusDepth);
				const float inverseDepth = projectionWeights.x + projectionWeights.y;
				projectionDepth = EncodeCloudDepth(rcp(inverseDepth));
				projectionWeights /= inverseDepth;
			}
		}
	}
	const float2 position = (FrameBuffer::CameraPosAdjust.xy + ray.xy * lowDepth - info.noiseWindOffset) * GAME_UNIT_TO_M;
	const float2 shearChange = (info.cloudShapeShear - info.previousShapeShear) * smoothstep(0.0, 1.0, height);
	const float2 fieldDelta = CloudPreviousFieldOffset(position, -shearChange * GAME_UNIT_TO_M, info.cloudEvolution);
	float3 lowDisplacement = float3(info.cloudWindDelta - fieldDelta * GAME_UNITS_PER_METER,
		info.cloudEvolutionDelta * (4.0 * height * (1.0 - height)));
	const float localWeight = LocalNdfMotionWeight(FrameBuffer::CameraPosAdjust.xy + ray.xy * lowDepth, height, info);
	const float localMotionUncertainty = length(lowDisplacement) * localWeight;
	lowDisplacement *= 1.0 - localWeight;
	// Ownership comes from nonzero extinction integrated on this ray. A mean
	// depth inside the storm's box says nothing about which clouds were visible.
	const float stormWeight = saturate(stormFraction / max(1.0 - cirrusFraction, 1e-6));
	const float3 stormDisplacement = stormMotion.xyz / max(stormMotion.w, 1e-6);
	const float stormUncertainty = stormWeight * (1.0 - stormWeight) * length(stormDisplacement - lowDisplacement);
	lowDisplacement = lerp(lowDisplacement, stormDisplacement, stormWeight);
	const float2 currentRelativeXY = ray.xy * lowDepth;
	const float2 previousRelativeXY = currentRelativeXY + FrameBuffer::CameraPosAdjust.xy - info.previousCamera.xy - lowDisplacement.xy;
	const float previousRadius = info.noisePadding.x > 0.0 ? info.noisePadding.x : info.cloudCurvatureRadius;
	const float curvatureDisplacement = (1.0 - stormWeight) *
	                                    (dot(previousRelativeXY - currentRelativeXY, previousRelativeXY + currentRelativeXY) / info.cloudCurvatureRadius +
											dot(previousRelativeXY, previousRelativeXY) / previousRadius * ((info.cloudCurvatureRadius - previousRadius) / info.cloudCurvatureRadius));
	lowDisplacement.z += curvatureDisplacement;
	const float3 highDisplacement = float3(info.cirrusWindDelta, 0);
	const float3 disagreement = lowDisplacement - highDisplacement;
	const float transverseDisagreement = length(disagreement - ray * dot(disagreement, ray));
	uncertainty = (1.0 - cirrusFraction) * ((1.0 - stormWeight) * (abs(info.cloudEvolutionDelta) + length(shearChange) * 60.0 + localMotionUncertainty) + abs(curvatureDisplacement) + stormUncertainty) +
	              cirrusFraction * (1.0 - cirrusFraction) * transverseDisagreement;
	return lowDisplacement * projectionWeights.x + highDisplacement * projectionWeights.y;
}

float CloudEvolutionBlend(float uncertainty, float depth, float pixelsPerRadian)
{
	const float cycleError = uncertainty * pixelsPerRadian * 16.0 / max(DecodeCloudDepth(depth), 1.0);
	return saturate(cycleError - 0.5) * 0.5;
}

float BlendCloudTransmittance(float previous, float current, float weight)
{
	precise float value = previous + (current - previous) * weight;
	return clamp(value, min(previous, current), max(previous, current));
}

struct CloudTemporalSample
{
	// Half-float opacity cannot retain vanishing transmission.
	float transmittance;
	float4 color;
	float4 metadata;
	float2 lightning;
};

CloudTemporalSample ReconstructCloudSamples(float transmittances[4], float4 colors[4], float4 metadata[4], float2 masks[4], float2 fraction, bool reweightEmpty)
{
	float4 weights = float4((1.0 - fraction.x) * (1.0 - fraction.y), fraction.x * (1.0 - fraction.y),
		(1.0 - fraction.x) * fraction.y, fraction.x * fraction.y);
	if (reweightEmpty) {
		[unroll] for (uint i = 0u; i < 4u; ++i)
			weights[i] = transmittances[i] == 1.0 ? 0.01 : weights[i];
		weights /= dot(weights, float4(1, 1, 1, 1));
	}
	CloudTemporalSample result = (CloudTemporalSample)0;
	float weightSum = 0.0;
	float2 transmittanceRange = float2(1, 0);
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		transmittanceRange = float2(min(transmittanceRange.x, transmittances[i]), max(transmittanceRange.y, transmittances[i]));
		result.transmittance += transmittances[i] * weights[i];
		weightSum += weights[i];
		result.color += colors[i] * weights[i];
		result.metadata += metadata[i] * weights[i];
		result.lightning += masks[i] * weights[i];
	}
	result.transmittance = clamp(result.transmittance / weightSum, transmittanceRange.x, transmittanceRange.y);
	return result;
}

CloudTemporalSample ReconstructCloud(Texture2D<float> transmittance, Texture2D<float4> color, Texture2D<float4> depth, Texture2D<float2> mask,
	float2 position, int2 maximum, bool reweightEmpty)
{
	if (!reweightEmpty) {
		uint2 dimensions;
		transmittance.GetDimensions(dimensions.x, dimensions.y);
		const float2 uv = (clamp(position, 0.0, float2(maximum)) + 0.5) / float2(dimensions);
		CloudTemporalSample result;
		result.transmittance = transmittance.SampleLevel(TransmittanceSampler, uv, 0);
		result.color = color.SampleLevel(TransmittanceSampler, uv, 0);
		result.metadata = depth.SampleLevel(TransmittanceSampler, uv, 0);
		result.lightning = mask.SampleLevel(TransmittanceSampler, uv, 0);
		return result;
	}
	const int2 base = int2(floor(position));
	float transmittances[4];
	float4 colors[4];
	float4 metadata[4];
	float2 masks[4];
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		const int2 pixel = clamp(base + int2(i & 1u, i >> 1u), 0, maximum);
		transmittances[i] = transmittance[pixel];
		colors[i] = color[pixel];
		metadata[i] = depth[pixel];
		masks[i] = mask[pixel];
	}
	return ReconstructCloudSamples(transmittances, colors, metadata, masks, saturate(position - floor(position)), reweightEmpty);
}

bool CloudFootprintHasCloud(Texture2D<float4> color, float2 position, int2 maximum)
{
	const int2 base = int2(floor(position));
	float opacity = 0.0;
	[unroll] for (uint i = 0u; i < 4u; ++i)
		opacity = max(opacity, color[clamp(base + int2(i & 1u, i >> 1u), 0, maximum)].a);
	return opacity > 0.0;
}

bool ReconstructCloudSurface(float2 position, float sceneDepth, out CloudTemporalSample result,
	out float2 depthRange, out float4 sampleMinimum, out float4 sampleMaximum, out float motionDepth, out float4 stormMotion)
{
	const int2 base = int2(floor(position));
	const float2 fraction = saturate(position - floor(position));
	const int2 maximum = int2(VolumetricCloudBuffer[0].lowFrameDim) - 1;
	result = (CloudTemporalSample)0;
	CloudTemporalSample nearest = (CloudTemporalSample)0;
	stormMotion = 0.0;
	float4 nearestStormMotion = 0.0;
	float nearestDistance = 4.0;
	float weightSum = 0.0;
	float motionDepthSum = 0.0;
	float motionWeightSum = 0.0;
	depthRange = float2(asfloat(0x7f7fffffu), 0);
	sampleMinimum = float4(asfloat(0x7f7fffffu).xxx, 1);
	sampleMaximum = 0.0;
	bool found = false;
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		const int2 pixel = clamp(base + int2(i & 1u, i >> 1u), 0, maximum);
		const float4 metadata = TexVolLowAux[pixel];
		if (abs(metadata.y - sceneDepth) > 0.064)
			continue;
		const float4 color = TexVolLowLum[pixel];
		const float transmittance = TexVolLowTr[pixel];
		depthRange = float2(min(depthRange.x, metadata.x), max(depthRange.y, metadata.x));
		sampleMinimum = min(sampleMinimum, float4(color.rgb, transmittance));
		sampleMaximum = max(sampleMaximum, float4(color.rgb, transmittance));
		const float weight = transmittance == 1.0 ? 0.01 :
		                                            ((i & 1u) != 0u ? fraction.x : 1.0 - fraction.x) * (i >= 2u ? fraction.y : 1.0 - fraction.y);
		result.transmittance += transmittance * weight;
		result.color += color * weight;
		result.metadata += metadata * weight;
		result.lightning += TexLightningTrace[pixel] * weight;
		stormMotion += TexVolLowStormMotion[pixel] * weight;
		weightSum += weight;
		const float motionWeight = color.a * weight;
		motionDepthSum += metadata.x * motionWeight;
		motionWeightSum += motionWeight;
		const float2 offset = float2(pixel) - position;
		const float distance = dot(offset, offset);
		if (!found || distance < nearestDistance) {
			nearest.transmittance = transmittance;
			nearest.color = color;
			nearest.metadata = metadata;
			nearest.lightning = TexLightningTrace[pixel];
			nearestStormMotion = TexVolLowStormMotion[pixel];
			nearestDistance = distance;
		}
		found = true;
	}
	if (weightSum > 0.0) {
		result.transmittance = clamp(result.transmittance / weightSum, sampleMinimum.w, sampleMaximum.w);
		result.color /= weightSum;
		result.metadata /= weightSum;
		result.lightning /= weightSum;
		stormMotion /= weightSum;
	} else {
		result = nearest;
		stormMotion = nearestStormMotion;
	}
	// Empty rays carry boundary distances rather than cloud motion.
	motionDepth = motionWeightSum > 0.0 ? motionDepthSum / motionWeightSum : result.metadata.x;
	return found;
}

float3 RebaseCloudHistoryDepth(inout CloudTemporalSample history, float3 direction, float3 previousPosition, float3 cameraDelta)
{
	// History stores a distance from its own camera.
	const float3 position = previousPosition * (DecodeCloudDepth(history.metadata.x) / max(length(previousPosition), 1e-6)) - cameraDelta;
	history.metadata.x = EncodeCloudDepth(max(dot(position, direction), 0.0));
	return position;
}

void ResolveCloudHistoryVisibility(inout CloudTemporalSample history, CloudTemporalSample current,
	float2 depthRange, float4 sampleMinimum, float4 sampleMaximum, float3 historyPosition, float2 currentUv, float minimumConfidence)
{
	const float4 clip = mul(FrameBuffer::CameraViewProj, float4(historyPosition, 1));
	float confidence = 1.0;
	if (clip.w > 0.0) {
		const float2 historyUv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
		const float cycleDisplacement = length((historyUv - currentUv) * VolumetricCloudBuffer[0].activeFrameDim) * 16.0;
		confidence = max(smoothstep(0.0, 0.5, cycleDisplacement), minimumConfidence);
	}
	const float depthTolerance = EncodeCloudDepth(CloudViewStep(DecodeCloudDepth(depthRange.y), 0.0));
	const bool depthValid = history.metadata.x >= depthRange.x - depthTolerance && history.metadata.x <= depthRange.y + depthTolerance;
	const float4 currentValue = float4(current.color.rgb, current.transmittance);
	const float4 difference = float4(history.color.rgb, history.transmittance) - currentValue;
	const float4 extent = max(lerp(currentValue - sampleMinimum, sampleMaximum - currentValue, step(0.0, difference)), 0.0);
	const float4 ratio = abs(difference) > 1e-6 ? extent / max(abs(difference), 1e-6) : 1.0;
	const float clippedWeight = depthValid ? saturate(min(min(ratio.x, ratio.y), min(ratio.z, ratio.w))) : 0.0;
	const float historyWeight = lerp(1.0, clippedWeight, confidence);
	// Clip along the current-to-history segment: premultiplied radiance and
	// transmission retain the same mixture. Equal opacity does not make an old
	// foreground cloud valid behind its newly exposed background.
	history.transmittance = BlendCloudTransmittance(current.transmittance, history.transmittance, historyWeight);
	history.color = lerp(current.color, history.color, historyWeight);
	history.metadata = lerp(current.metadata, history.metadata, historyWeight);
	history.lightning = lerp(current.lightning, history.lightning, historyWeight);
}

float4 LoadCloudBoundary(uint2 pixel, float3 ray)
{
	const float4 rect = VolumetricCloudBuffer[0].ndfBoundaryRect;
	const float2 camera = FrameBuffer::CameraPosAdjust.xy;
	const float empty = asfloat(0x7f7fffffu);
	const float exitX = abs(ray.x) > 1e-8 ? ((ray.x > 0.0 ? rect.z : rect.x) - camera.x) / ray.x : empty;
	const float exitY = abs(ray.y) > 1e-8 ? ((ray.y > 0.0 ? rect.w : rect.y) - camera.y) / ray.y : empty;
	const float3 depths = TexCloudBoundary.Load(int3(pixel, 0)).xyz;
	const float viewCosine = max(abs(mul(FrameBuffer::CameraView, float4(ray, 0)).z), 1e-6);
	return float4(depths.x != empty ? DecodeCloudDepth(depths.x) / viewCosine : 0.0,
		DecodeCloudDepth(-depths.y) / viewCosine,
		depths.z != empty ? DecodeCloudDepth(depths.z) / viewCosine : 0.0,
		max(min(exitX, exitY), 0.0));
}

#ifdef CLOUD_MAIN_DISPATCH
cbuffer CloudDispatch : register(b3) { uint4 StormScreenRect; };
#endif

#if defined(CLOUD_MAIN_DISPATCH) && defined(SINGLE_CLOUD_STORM)
#	define CLOUD_MAIN_THREADS 16, 8, 1
#elif defined(CLOUD_MAIN_DISPATCH) && defined(VOLCANIC_STORMS_ONLY)
#	define CLOUD_MAIN_THREADS 16, 4, 1
#elif defined(CLOUD_MAIN_DISPATCH) && defined(NO_CLOUD_STORMS)
#	define CLOUD_MAIN_THREADS 8, 4, 1
#else
#	define CLOUD_MAIN_THREADS 8, 8, 1
#endif
[numthreads(CLOUD_MAIN_THREADS)] void main(uint2 tid : SV_DispatchThreadID) {
#ifdef CLOUD_MAIN_DISPATCH
#	if defined(NO_CLOUD_STORMS) && !defined(CLOUD_VOXELS)
	[branch] if (all(tid >= StormScreenRect.xy) && all(tid < StormScreenRect.zw))
	{
		if (TexStormTiles[tid / 8u] != 0u)
			return;
	}
#	else
	tid += StormScreenRect.xy;
	if (any(tid >= StormScreenRect.zw) || TexStormTiles[tid / 8u] == 0u)
		return;
#	endif
#endif
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(tid >= uint2(info.lowFrameDim)))
		return;
	const uint2 pixel = tid * 4u + CloudPhaseOffset();
	const float3 ray = CloudScreenRay(pixel);
	// This ray belongs to the checkerboard phase pixel, not the farthest
	// surface in its 4x4 tile. A sky neighbour must not expose clouds behind terrain.
	const float deviceDepth = CloudSceneDeviceDepth(pixel, 1u);
	const float linearDepth = SharedData::GetScreenDepth(deviceDepth);
	const float sceneDistance = deviceDepth >= 1.0 - 1e-6 ? 0.0 :
	                                                        linearDepth / max(abs(mul(FrameBuffer::CameraView, float4(ray, 0)).z), 1e-6);
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
	const float2 jitter = float2(CloudTemporalMarchHash(pixel, info.cloudFrameIndex), CloudSpatialMarchHash(pixel));
	const VolumetricCloudResult result = RenderVolumetricCloudRay(ray, eye, sceneDistance, jitter, 0.0, LoadCloudBoundary(tid, ray), 0.0);
	RWTexTr[tid] = result.transmittance.x;
	RWTexLum[tid] = float4(min(result.lum, 65504.0), 1.0 - result.transmittance.x);
	RWStormMotion[tid] = result.stormMotion;
	RWTexAux[tid] = float4(EncodeCloudDepth(result.cloud_depth), EncodeCloudDepth(linearDepth), result.motionMetadata * (1.0 - result.transmittance.x));
	RWLightningMask[tid] = min(result.lightning, 65504.0);
}

void StoreUpperCloudPixel(VolumetricCloudResult upper, uint2 tid)
{
	float transmission = TexVolLowTr[tid];
	float4 color = TexVolLowLum[tid], metadata = TexVolLowAux[tid], motion = TexVolLowStormMotion[tid];
	float2 lightning = TexLightningTrace[tid];
	CompositeUpperCloud(upper, transmission, color, metadata, motion, lightning);
	RWTexTr[tid] = transmission;
	RWTexLum[tid] = color;
	RWTexAux[tid] = metadata;
	RWStormMotion[tid] = motion;
	RWLightningMask[tid] = lightning;
}

void RenderUpperViewPixel(uint2 tid)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(tid >= uint2(info.lowFrameDim)))
		return;
	const uint2 pixel = tid * 4u + CloudPhaseOffset();
	const float3 direction = CloudScreenRay(pixel);
	const float deviceDepth = CloudSceneDeviceDepth(pixel, 1u);
	const float sceneDistance = deviceDepth >= 1.0 - 1e-6 ? 0.0 : SharedData::GetScreenDepth(deviceDepth) / max(abs(mul(FrameBuffer::CameraView, float4(direction, 0)).z), 1e-6);
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
	// Integrate middle segments deterministically; the checkerboard already
	// moves the view ray. Filter noise by the screen footprint, not march length.
	const float2 pixelFootprint = 2.0 / max(abs(float2(FrameBuffer::CameraProjUnjittered[0][0], FrameBuffer::CameraProjUnjittered[1][1])) * info.activeFrameDim, 1.0);
	const VolumetricCloudResult upper = RenderUpperCloudRay(direction, eye, sceneDistance, CloudSpatialMarchHash(pixel), max(pixelFootprint.x, pixelFootprint.y));
	StoreUpperCloudPixel(upper, tid);
}

[numthreads(8, 8, 1)] void renderUpperView(uint2 tid : SV_DispatchThreadID) {
	RenderUpperViewPixel(tid);
}

#include "PhysicalSky/UpperCloudTiles.hlsli"

	[numthreads(8, 8, 1)] void reproject(uint2 pixel : SV_DispatchThreadID)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (any(pixel >= uint2(info.activeFrameDim)))
		return;
	const float2 uv = CloudScreenUv(pixel);
	const float sceneDepth = EncodeCloudDepth(SharedData::GetScreenDepth(CloudSceneDeviceDepth(pixel, 1u)));
	const float2 currentPosition = (float2(pixel) - CloudPhaseOffset()) * 0.25;
	CloudTemporalSample current;
	float2 currentDepthRange;
	float4 currentMinimum, currentMaximum;
	float motionDepth;
	float4 stormMotion;
	const bool currentValid = ReconstructCloudSurface(currentPosition, sceneDepth, current,
		currentDepthRange, currentMinimum, currentMaximum, motionDepth, stormMotion);
	float4 motionMetadata;
	float motionOpacity;
	if (currentValid) {
		motionMetadata = current.metadata;
		motionMetadata.x = motionDepth;
		motionOpacity = current.color.a;
	} else {
		uint2 traceDimensions;
		TexVolLowAux.GetDimensions(traceDimensions.x, traceDimensions.y);
		const float2 tracePixel = clamp((float2(pixel) - CloudPhaseOffset()) * 0.25 + 0.5, 0.5, info.lowFrameDim - 0.5);
		motionMetadata = TexVolLowAux.SampleLevel(TransmittanceSampler, tracePixel / traceDimensions, 0);
		motionOpacity = TexVolLowLum.SampleLevel(TransmittanceSampler, tracePixel / traceDimensions, 0).a;
		stormMotion = TexVolLowStormMotion.SampleLevel(TransmittanceSampler, tracePixel / traceDimensions, 0);
	}
	const float3 ray = CloudScreenRay(pixel);
	float uncertainty, depth;
	const float3 displacement = CloudSampleDisplacement(ray, motionMetadata, motionOpacity, stormMotion, uncertainty, depth);
	const float2 projectionScale = abs(float2(FrameBuffer::CameraProjUnjittered[0][0], FrameBuffer::CameraProjUnjittered[1][1])) * info.activeFrameDim * 0.5;
	const float viewCosine = abs(mul(FrameBuffer::CameraView, float4(ray, 0)).z);
	const float evolutionBlend = max(info.noisePadding.y, CloudEvolutionBlend(uncertainty, depth, max(projectionScale.x, projectionScale.y) / max(viewCosine * viewCosine, 1e-4)));
	const float3 cameraDelta = FrameBuffer::CameraPosAdjust.xyz - info.previousCamera - displacement;
	const float3 previousPosition = ray * DecodeCloudDepth(depth) + cameraDelta;
	const float4 clip = mul(info.previousViewProj, float4(previousPosition, 1));
	const float2 previousUv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
	CloudTemporalSample result;
	bool useHistory = info.historyValid != 0u && all(info.previousFrameDim > 0.0) && clip.w > 0.0 && all(previousUv == saturate(previousUv));
	if (useHistory) {
		result = ReconstructCloud(TexVolHistoryTr, TexVolHistoryLum, TexVolHistoryAux, TexLightningHistory,
			previousUv * info.previousFrameDim - 0.5, int2(info.previousFrameDim) - 1, false);
		const float3 historyPosition = RebaseCloudHistoryDepth(result, ray, previousPosition, cameraDelta);
		const bool traced = all((pixel & 3u) == CloudPhaseOffset());
		// The traced phase supplies its own current surface sample. The
		// background-depth rejection only governs the other checkerboard phases.
		useHistory = traced || abs(sceneDepth - result.metadata.y) <= 0.064;
		// Sparse phase neighbours are not a visibility change in a static medium.
		// Keep their changing bounds from overwriting the other 15 history phases.
		float localRotationConfidence = 0.0;
		[branch] if (info.voxelCount > 0u && stormMotion.w > 0.0)
		{
			// A static local volume has no wind displacement. Pure camera rotation
			// also has no depth-reprojection residual, but moves its sharp silhouette
			// across history texels. Include that motion without including TAA jitter.
			const float4 currentDirection = mul(FrameBuffer::CameraViewProjUnjittered, float4(ray, 0));
			const float4 previousDirection = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(ray, 0));
			if (currentDirection.w > 0.0 && previousDirection.w > 0.0) {
				const float2 rotationPixels = (previousDirection.xy / previousDirection.w - currentDirection.xy / currentDirection.w) * (0.5 * info.activeFrameDim);
				localRotationConfidence = smoothstep(0.0, 0.5, length(rotationPixels)) * saturate(stormMotion.w / max(motionOpacity, 1e-6));
			}
		}
		if (useHistory && currentValid && (any(cameraDelta != 0.0) || evolutionBlend > 0.0 || localRotationConfidence > 0.0))
			ResolveCloudHistoryVisibility(result, current, currentDepthRange, currentMinimum, currentMaximum, historyPosition, uv, max(evolutionBlend * 2.0, localRotationConfidence));
		if (useHistory && traced) {
			const float weight = max(CloudHistoryBlend(previousUv - uv), evolutionBlend * 2.0);
			result.transmittance = BlendCloudTransmittance(result.transmittance, TexVolLowTr[pixel / 4u], weight);
			result.color = lerp(result.color, TexVolLowLum[pixel / 4u], weight);
			result.metadata = lerp(result.metadata, TexVolLowAux[pixel / 4u], weight);
			result.lightning = lerp(result.lightning, TexLightningTrace[pixel / 4u], weight);
		}
		if (useHistory && currentValid && evolutionBlend > 0.0 && !traced) {
			result.transmittance = BlendCloudTransmittance(result.transmittance, current.transmittance, evolutionBlend);
			result.color = lerp(result.color, current.color, evolutionBlend);
			result.metadata = lerp(result.metadata, current.metadata, evolutionBlend);
			result.lightning = lerp(result.lightning, current.lightning, evolutionBlend);
		}
		if (useHistory && result.color.a > 0.0 && !currentValid)
			useHistory = CloudFootprintHasCloud(TexVolLowLum, currentPosition, int2(info.lowFrameDim) - 1);
	}
	if (!useHistory) {
		if (currentValid)
			result = current;
		else
			// Rejected history still reconstructs the current cloud.
			// A different background depth does not imply an empty foreground volume.
			result = ReconstructCloud(TexVolLowTr, TexVolLowLum, TexVolLowAux, TexLightningTrace, currentPosition, int2(info.lowFrameDim) - 1, true);
	}
	if (useHistory || currentValid)
		result.metadata.y = sceneDepth;
	RWTexTr[pixel] = saturate(result.transmittance);
	RWTexLum[pixel] = float4(clamp(result.color.rgb, 0.0, 65504.0), saturate(result.color.a));
	RWTexAux[pixel] = result.metadata;
	// A new source invalidates only its mask, not the cloud's color history.
	RWLightningMask[pixel] = Lightning::ResolveMasks(result.lightning, currentValid ? current.lightning : float2(0, 0));
}

float3 CloudCubeDirection(float2 pixel, uint face, uint size)
{
	const float2 uv = (pixel + 0.5) / size * float2(2, -2) + float2(-1, 1);
	float3 direction;
	switch (face) {
	case 0:
		direction = float3(1, uv.y, -uv.x);
		break;
	case 1:
		direction = float3(-1, uv.y, uv.x);
		break;
	case 2:
		direction = float3(uv.x, 1, -uv.y);
		break;
	case 3:
		direction = float3(uv.x, -1, uv.y);
		break;
	case 4:
		direction = float3(uv.x, uv.y, 1);
		break;
	default:
		direction = float3(-uv.x, uv.y, -1);
		break;
	}
	return normalize(direction);
}

CloudTemporalSample ReconstructCloudCube(TextureCube<float> transmittance, TextureCube<float4> color, TextureCube<float4> depth, TextureCube<float2> mask,
	float2 position, uint face, uint size, bool reweightEmpty)
{
	if (!reweightEmpty) {
		const float3 direction = CloudCubeDirection(clamp(position, 0.0, float(size - 1u)), face, size);
		CloudTemporalSample result;
		result.transmittance = transmittance.SampleLevel(TransmittanceSampler, direction, 0);
		result.color = color.SampleLevel(TransmittanceSampler, direction, 0);
		result.metadata = depth.SampleLevel(TransmittanceSampler, direction, 0);
		result.lightning = mask.SampleLevel(TransmittanceSampler, direction, 0);
		return result;
	}
	const float2 base = floor(position);
	float transmittances[4];
	float4 colors[4];
	float4 metadata[4];
	float2 masks[4];
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		const float2 pixel = clamp(base + float2(i & 1u, i >> 1u), 0.0, float(size - 1u));
		const float3 direction = CloudCubeDirection(pixel, face, size);
		transmittances[i] = transmittance.SampleLevel(TransmittanceSampler, direction, 0);
		colors[i] = color.SampleLevel(TransmittanceSampler, direction, 0);
		metadata[i] = depth.SampleLevel(TransmittanceSampler, direction, 0);
		masks[i] = mask.SampleLevel(TransmittanceSampler, direction, 0);
	}
	return ReconstructCloudSamples(transmittances, colors, metadata, masks, saturate(position - base), reweightEmpty);
}

bool CloudFootprintHasCloud(TextureCube<float4> color, float2 position, uint face, uint size)
{
	const float2 base = floor(position);
	float opacity = 0.0;
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		const float2 pixel = clamp(base + float2(i & 1u, i >> 1u), 0.0, float(size - 1u));
		opacity = max(opacity, color.SampleLevel(TransmittanceSampler, CloudCubeDirection(pixel, face, size), 0).a);
	}
	return opacity > 0.0;
}

float2 CloudCubePosition(float3 direction, uint face)
{
	float2 position;
	float depth;
	switch (face) {
	case 0:
		position = float2(-direction.z, direction.y);
		depth = direction.x;
		break;
	case 1:
		position = float2(direction.z, direction.y);
		depth = -direction.x;
		break;
	case 2:
		position = float2(direction.x, -direction.z);
		depth = direction.y;
		break;
	case 3:
		position = float2(direction.x, direction.z);
		depth = -direction.y;
		break;
	case 4:
		position = direction.xy;
		depth = direction.z;
		break;
	default:
		position = float2(-direction.x, direction.y);
		depth = -direction.z;
		break;
	}
	position *= (depth >= 0.0 ? 1.0 : -1.0) / max(abs(depth), 1e-6);
	return position * float2(0.5, -0.5) + 0.5;
}

#include "PhysicalSky/CloudReflection.hlsli"

[numthreads(8, 8, 1)] void reprojectCubemap(uint3 tid : SV_DispatchThreadID) {
	uint3 dims;
	RWTexCubeTr.GetDimensions(dims.x, dims.y, dims.z);
	if (any(tid >= dims))
		return;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 direction = CloudCubeDirection(tid.xy, tid.z, dims.x);
	const float2 tracePixel = clamp((float2(tid.xy) - CloudPhaseOffset()) * 0.25, 0.0, float(dims.x / 4u - 1u));
	const float3 depthDirection = CloudCubeDirection(tracePixel, tid.z, dims.x / 4u);
	const float4 motionMetadata = TexCubeTraceAux.SampleLevel(TransmittanceSampler, depthDirection, 0);
	float uncertainty, depth;
	const float3 displacement = CloudSampleDisplacement(direction, motionMetadata, TexCubeTraceLum.SampleLevel(TransmittanceSampler, depthDirection, 0).a, TexCubeStormMotion.SampleLevel(TransmittanceSampler, depthDirection, 0), uncertainty, depth);
	const float cubeCosine = max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
	const float evolutionBlend = max(info.noisePadding.y, CloudEvolutionBlend(uncertainty, depth, 0.5 * dims.x / (cubeCosine * cubeCosine)));
	const float3 cameraDelta = FrameBuffer::CameraPosAdjust.xyz - info.previousCamera - displacement;
	const float3 previousPosition = direction * DecodeCloudDepth(depth) + cameraDelta;
	const float3 previousDirection = normalize(previousPosition);
	const float3 absoluteDirection = abs(previousDirection);
	const uint face = absoluteDirection.x >= max(absoluteDirection.y, absoluteDirection.z) ? (previousDirection.x > 0 ? 0u : 1u) :
	                                                                                         (absoluteDirection.y >= absoluteDirection.z ? (previousDirection.y > 0 ? 2u : 3u) : (previousDirection.z > 0 ? 4u : 5u));
	const float2 historyUv = CloudCubePosition(previousDirection, face);
	CloudTemporalSample result;
	bool useHistory = info.historyValid != 0u && all(isfinite(previousDirection));
	if (useHistory) {
		result = ReconstructCloudCube(TexCubeHistoryTr, TexCubeHistoryLum, TexCubeHistoryAux, TexLightningCubeHistory,
			historyUv * dims.x - 0.5, face, dims.x, false);
		RebaseCloudHistoryDepth(result, direction, previousPosition, cameraDelta);
		const bool traced = all((tid.xy & 3u) == CloudPhaseOffset());
		if (traced) {
			const float3 traceDirection = CloudCubeDirection(tid.xy / 4u, tid.z, dims.x / 4u);
			const float4 color = TexCubeTraceLum.SampleLevel(TransmittanceSampler, traceDirection, 0);
			const float2 motion = CloudCubePosition(previousDirection, tid.z) - (float2(tid.xy) + 0.5) / dims.x;
			const float weight = max(CloudHistoryBlend(motion), evolutionBlend * 2.0);
			result.transmittance = BlendCloudTransmittance(result.transmittance, TexCubeTraceTr.SampleLevel(TransmittanceSampler, traceDirection, 0), weight);
			result.color = lerp(result.color, color, weight);
			result.metadata = lerp(result.metadata, TexCubeTraceAux.SampleLevel(TransmittanceSampler, traceDirection, 0), weight);
			result.lightning = lerp(result.lightning, TexLightningCubeTrace.SampleLevel(TransmittanceSampler, traceDirection, 0), weight);
		}
		if (!traced && evolutionBlend > 0.0) {
			const CloudTemporalSample current = ReconstructCloudCube(TexCubeTraceTr, TexCubeTraceLum, TexCubeTraceAux, TexLightningCubeTrace,
				(float2(tid.xy) - CloudPhaseOffset()) * 0.25, tid.z, dims.x / 4u, true);
			result.transmittance = BlendCloudTransmittance(result.transmittance, current.transmittance, evolutionBlend);
			result.color = lerp(result.color, current.color, evolutionBlend);
			result.metadata = lerp(result.metadata, current.metadata, evolutionBlend);
			result.lightning = lerp(result.lightning, current.lightning, evolutionBlend);
		}
		if (result.color.a > 0.0)
			useHistory = CloudFootprintHasCloud(TexCubeTraceLum,
				(float2(tid.xy) - CloudPhaseOffset()) * 0.25, tid.z, dims.x / 4u);
	}
	if (!useHistory) {
		result = ReconstructCloudCube(TexCubeTraceTr, TexCubeTraceLum, TexCubeTraceAux, TexLightningCubeTrace,
			(float2(tid.xy) - CloudPhaseOffset()) * 0.25, tid.z, dims.x / 4u, true);
	}
	RWTexCubeTr[tid] = saturate(result.transmittance);
	RWTexCubeLum[tid] = float4(clamp(result.color.rgb, 0.0, 65504.0), saturate(result.color.a));
	RWTexCubeAux[tid] = result.metadata;
	RWLightningCubeMask[tid] = Lightning::ResolveMasks(result.lightning, TexLightningCubeTrace.SampleLevel(TransmittanceSampler, depthDirection, 0));
}

#endif

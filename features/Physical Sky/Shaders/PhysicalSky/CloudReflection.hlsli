#ifndef PHYSICAL_SKY_CLOUD_REFLECTION_HLSLI
#define PHYSICAL_SKY_CLOUD_REFLECTION_HLSLI

#ifdef CLOUD_REFLECTION
static const uint CloudReflectionSegments = 64u;
groupshared float4 ReflectionDirect[CloudReflectionSegments];
groupshared float4 ReflectionAmbient[CloudReflectionSegments];
groupshared float4 ReflectionWeights[CloudReflectionSegments];
groupshared float4 ReflectionMotion[CloudReflectionSegments];
groupshared float2 ReflectionLightning[CloudReflectionSegments];
groupshared uint ReflectionUpperVisible;

// Both layers use the same ordered reduction and shared-memory storage.
// Direct and forward transport retain their own transmission coefficients.
void ReduceCloudReflection(VolumetricCloudResult value, uint lane)
{
	ReflectionDirect[lane] = float4(value.lum, value.rawTransmission.x);
	ReflectionAmbient[lane] = float4(value.transportLum, value.rawTransmission.y);
	ReflectionWeights[lane] = value.integralWeights;
	ReflectionMotion[lane] = value.stormMotion;
	ReflectionLightning[lane] = value.lightning;
	GroupMemoryBarrierWithGroupSync();

	[unroll] for (uint stride = 1u; stride < CloudReflectionSegments; stride *= 2u)
	{
		if ((lane & (stride * 2u - 1u)) == 0u) {
			const float4 front = ReflectionDirect[lane], back = ReflectionDirect[lane + stride];
			const float4 frontAmbient = ReflectionAmbient[lane], backAmbient = ReflectionAmbient[lane + stride];
			ReflectionDirect[lane] = float4(front.rgb + front.a * back.rgb, front.a * back.a);
			ReflectionAmbient[lane] = float4(frontAmbient.rgb + frontAmbient.a * backAmbient.rgb, frontAmbient.a * backAmbient.a);
			ReflectionWeights[lane] += front.a * ReflectionWeights[lane + stride];
			ReflectionMotion[lane] += front.a * ReflectionMotion[lane + stride];
			ReflectionLightning[lane] += front.a * ReflectionLightning[lane + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
}

VolumetricCloudResult ResolveCloudReflection()
{
	VolumetricCloudResult result = (VolumetricCloudResult)0;
	const float4 weights = ReflectionWeights[0];
	const float rawTransmission = CloudBackgroundTransmittance(ReflectionDirect[0].a, ReflectionAmbient[0].a);
	const float transmission = saturate((rawTransmission - 0.1) / 0.9);
	const float opacity = 1.0 - transmission;
	result.transmittance = transmission;
	result.lum = (ReflectionDirect[0].rgb + ReflectionAmbient[0].rgb) / (1.0 - min(rawTransmission, 0.1));
	result.lightning = ReflectionLightning[0] / (1.0 - min(rawTransmission, 0.1));
	result.cloud_depth = weights.y > 0.0 ? weights.x / weights.y : VolumetricCloudBuffer[0].rayMarchRange;
	result.motionMetadata = weights.y > 0.0 ? saturate(weights.zw / weights.y) : 0.0;
	result.stormMotion = weights.y > 0.0 ? ReflectionMotion[0] * (opacity / weights.y) : 0.0;
	return result;
}

// Preserve the upper march's early exit across parallel strata. In particular,
// a cirrus sheet behind an opaque middle deck must not resume scattering.
void TrimUpperCloudReflection(inout VolumetricCloudResult value, uint lane)
{
	float2 prefix = value.rawTransmission;
	ReflectionDirect[lane] = float4(prefix, 0.0, 0.0);
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 1u; stride < CloudReflectionSegments; stride *= 2u)
	{
		const float2 preceding = lane >= stride ? ReflectionDirect[lane - stride].xy : 1.0;
		GroupMemoryBarrierWithGroupSync();
		prefix *= preceding;
		ReflectionDirect[lane] = float4(prefix, 0.0, 0.0);
		GroupMemoryBarrierWithGroupSync();
	}
	const float2 incident = lane > 0u ? ReflectionDirect[lane - 1u].xy : 1.0;
	if (max(incident.x, incident.y) <= 0.1) {
		value = (VolumetricCloudResult)0;
		value.rawTransmission = 1.0;
	}
	GroupMemoryBarrierWithGroupSync();
}

// One group integrates one reflection ray, including the middle layer.
[numthreads(64, 1, 1)] void renderCubemap(uint3 pixel : SV_GroupID, uint lane : SV_GroupIndex) {
	uint3 dimensions;
	RWTexCubeTr.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
	if (any(pixel >= dimensions))
		return;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const uint2 fullPixel = pixel.xy * 4u + CloudPhaseOffset();
	const float3 direction = CloudCubeDirection(fullPixel, pixel.z, dimensions.x * 4u);
	const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
	const uint2 seed = fullPixel + uint2(pixel.z * dimensions.x * 4u, 0);
	const float2 jitter = float2(CloudTemporalMarchHash(seed, info.cloudFrameIndex), CloudSpatialMarchHash(seed));
	const float cosine = max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
	const float footprint = 2.0 * cosine / (dimensions.x * 4u);
	const float2 stratum = float2(lane, lane + 1u) / float(CloudReflectionSegments);
	const VolumetricCloudResult value = RenderVolumetricCloudRay(direction, eye, 0.0, jitter, 0.0, 0.0, footprint, stratum);
	ReduceCloudReflection(value, lane);
	bool hasUpper = info.middleLayer.z > 0.0 || info.cirrusEnabled != 0u;
	VolumetricCloudResult lower = (VolumetricCloudResult)0;
	if (lane == 0u) {
		lower = ResolveCloudReflection();
		if (hasUpper) {
			const float first = UpperCloudFirstDistance(eye, direction, 0.0);
			ReflectionUpperVisible = first < 1e30 && (lower.transmittance.x > 0.0 || !UpperCloudOccluded(first, EncodeCloudDepth(lower.cloud_depth)));
		}
	}
	VolumetricCloudResult upper = (VolumetricCloudResult)0;
	if (hasUpper) {
		// Lane zero must finish reading the low layer before reusing its storage.
		GroupMemoryBarrierWithGroupSync();
		hasUpper = ReflectionUpperVisible != 0u;
		if (hasUpper) {
			VolumetricCloudResult segment = RenderUpperCloudRay(direction, eye, 0.0, jitter.y, footprint, stratum);
			TrimUpperCloudReflection(segment, lane);
			ReduceCloudReflection(segment, lane);
			if (lane == 0u)
				upper = ResolveCloudReflection();
		}
	}
	if (lane != 0u)
		return;

	// Keep the cloud source independent of foreground air. Reflection composition
	// applies either the shared reflection medium or the legacy AP at this depth.
	float combinedTransmission = lower.transmittance.x;
	const float opacity = 1.0 - combinedTransmission;
	float4 color = float4(lower.lum, opacity);
	float4 metadata = float4(EncodeCloudDepth(lower.cloud_depth), 0.0, lower.motionMetadata * opacity);
	float4 motion = lower.stormMotion;
	float2 lightning = lower.lightning;
	if (hasUpper)
		CompositeUpperCloud(upper, combinedTransmission, color, metadata, motion, lightning);
	RWTexCubeTr[pixel] = combinedTransmission;
	RWTexCubeLum[pixel] = color;
	RWLightningCubeMask[pixel] = min(lightning, 65504.0);
	RWCubeStormMotion[pixel] = motion;
	RWTexCubeAux[pixel] = metadata;
}
#endif
#endif

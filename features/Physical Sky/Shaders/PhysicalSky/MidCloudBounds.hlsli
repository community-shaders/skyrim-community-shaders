#ifndef PHYSICAL_SKY_MID_CLOUD_BOUNDS_HLSLI
#define PHYSICAL_SKY_MID_CLOUD_BOUNDS_HLSLI

Texture2D<float2> TexMiddleBounds : register(t105);

bool MiddleIntervalEmpty(float3 start, float3 end)
{
#ifdef DISABLE_MIDDLE_BOUNDS
	return false;
#else
	uint width, height, levels;
	TexMiddleBounds.GetDimensions(0u, width, height, levels);
	if (levels == 0u)
		return false;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float2 a = (start.xy - info.cirrusWindOffset) * info.middleLayer.w + 0.5;
	const float2 b = (end.xy - info.cirrusWindOffset) * info.middleLayer.w + 0.5;
	const float2 span = abs(b - a) * float2(width, height);
	const uint mip = min(uint(ceil(log2(max(max(span.x, span.y), 1.0)))), levels - 1u);
	const int2 size = max(int2(width, height) >> mip, 1);
	const int2 lo = int2(floor(min(a, b) * size)), hi = int2(floor(max(a, b) * size));
	// The hierarchy is a power-of-two torus at every mip, including negative UVs.
	const int2 p = uint2(lo) & uint2(size - 1), q = uint2(hi) & uint2(size - 1);
	const float2 aa = TexMiddleBounds.Load(int3(p, mip)), bb = TexMiddleBounds.Load(int3(q, mip));
	const float2 ab = TexMiddleBounds.Load(int3(p.x, q.y, mip)), ba = TexMiddleBounds.Load(int3(q.x, p.y, mip));
	const float2 bounds = float2(min(min(aa.x, bb.x), min(ab.x, ba.x)), max(max(aa.y, bb.y), max(ab.y, ba.y)));
	if (bounds.y < bounds.x)
		return true;
	const float2 altitude = lerp(info.middleLayer.x, info.middleLayer.y, bounds) + float2(-1.0, 1.0) * abs(MiddleCloudBandShape.y);
	const float3 delta = end - start;
	const float curve = dot(delta.xy, delta.xy) / info.cloudCurvatureRadius;
	const float slope = delta.z + 2.0 * dot(start.xy - FrameBuffer::CameraPosAdjust.xy, delta.xy) / info.cloudCurvatureRadius;
	const float vertex = saturate(-slope / max(2.0 * curve, 1e-20));
	const float h0 = CloudProfileAltitude(start, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float h1 = CloudProfileAltitude(end, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float minimum = min(min(h0, h1), h0 + vertex * (slope + vertex * curve));
	return max(h0, h1) < altitude.x || minimum > altitude.y;
#endif
}

#endif

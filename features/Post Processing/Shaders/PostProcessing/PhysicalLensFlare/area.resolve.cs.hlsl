ByteAddressBuffer AreaArguments : register(t0);
Texture2D<float2> BlueViolet : register(t1);
Texture2D<float4> Area0 : register(t2);
Texture2D<float4> Area1 : register(t3);
Texture2D<float4> Area2 : register(t4);
RWTexture2DArray<float> Fine : register(u0);
RWStructuredBuffer<uint> GhostTiles : register(u1);
SamplerState LinearSampler : register(s0);
groupshared uint Occupied;
// Five-tap cubic reconstruction of the broad layers. Center alignment matches
// AreaRasterPosition, including odd dimensions and the four-texel guard band.
float3 Reconstruct(Texture2D<float4> tex, float2 pixel, float2 fineSize, float div)
{
	uint w, h;
	tex.GetDimensions(w, h);
	float2 size = float2(w, h), p = (pixel - .5 * fineSize) / div + .5 * size;
	float2 tc = floor(p - .5) + .5, f = p - tc, f2 = f * f, f3 = f2 * f;
	float2 a = f2 - .5 * (f3 + f), b = 1.5 * f3 - 2.5 * f2 + 1, c = -1.5 * f3 + 2 * f2 + .5 * f, d = .5 * (f3 - f2);
	float2 bc = b + c, t0 = (tc - 1) / size, t12 = (tc + c / bc) / size, t3 = (tc + 2) / size;
	return tex.SampleLevel(LinearSampler, float2(t12.x, t0.y), 0).rgb * (bc.x * a.y) + tex.SampleLevel(LinearSampler, float2(t0.x, t12.y), 0).rgb * (a.x * bc.y) + tex.SampleLevel(LinearSampler, t12, 0).rgb * (bc.x * bc.y) + tex.SampleLevel(LinearSampler, float2(t3.x, t12.y), 0).rgb * (d.x * bc.y) + tex.SampleLevel(LinearSampler, float2(t12.x, t3.y), 0).rgb * (bc.x * d.y);
}
[numthreads(8, 8, 1)] void ResolveArea(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	uint w, h, l;
	Fine.GetDimensions(w, h, l);
	if (lane == 0)
		Occupied = 0;
	GroupMemoryBarrierWithGroupSync();
	if (tid.x < w && tid.y < h) {
		float2 blue = BlueViolet.Load(int3(tid.xy, 0));
		float3 v = float3(Fine[uint3(tid.xy, 0)] + blue.y, Fine[uint3(tid.xy, 1)], blue.x);
		float3 broad = 0;
		[branch] if (AreaArguments.Load(68) != 0) broad += Reconstruct(Area0, tid.xy + .5, float2(w, h), 4);
		[branch] if (AreaArguments.Load(88) != 0) broad += Reconstruct(Area1, tid.xy + .5, float2(w, h), 16);
		[branch] if (AreaArguments.Load(108) != 0) broad += Reconstruct(Area2, tid.xy + .5, float2(w, h), 64);
		v += max(broad, 0);
		Fine[uint3(tid.xy, 0)] = v.x;
		Fine[uint3(tid.xy, 1)] = v.y;
		Fine[uint3(tid.xy, 2)] = v.z;
		if (any(v != 0))
			InterlockedOr(Occupied, 1);
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane == 0) {
		uint index = group.y * ((w + 7) / 8) + group.x;
		GhostTiles[index] = (GhostTiles[index] & 0x1ffff) | (Occupied << 17);
	}
}

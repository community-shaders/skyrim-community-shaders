Texture2D<float4> DeformationMap : register(t0);
RWTexture2D<float4> OutBermField : register(u0);
Texture2D<float> TrackClock : register(t2);
SamplerState TrackClockSampler : register(s0);
StructuredBuffer<uint> BermTilesIn : register(t1);

RWTexture2D<uint> OutBermOccupancy : register(u2);
groupshared uint gBermNonZero;

Texture2D<uint> DeformOccupancyIn : register(t3);
Texture2D<uint> BermOccupancyIn : register(t4);
RWTexture2D<uint> OutUntouched : register(u3);

RWTexture2D<uint> OutBermDirty : register(u6);

cbuffer PerFrame : register(b0)
{
#include "SnowDeformation/DeformationFrame.hlsli"
}

static const float kTrackAgeHorizon = 128.0;

static const float kWallBlurReach = 12.0;

static const float2 kBermTaps[16] = {
	float2(18.0, 0.0), float2(12.73, 12.73), float2(0.0, 18.0), float2(-12.73, 12.73),
	float2(-18.0, 0.0), float2(-12.73, -12.73), float2(0.0, -18.0), float2(12.73, -12.73),
	float2(36.96, 15.31), float2(15.31, 36.96), float2(-15.31, 36.96), float2(-36.96, 15.31),
	float2(-36.96, -15.31), float2(-15.31, -36.96), float2(15.31, -36.96), float2(36.96, -15.31)
};

float Displaced(float4 texel)
{
	return saturate(texel.x - max(texel.y, 0.0));
}

int3 DeformTexel(int2 t, int2 dims)
{
	return int3((t + MapOrigin) & (dims - 1), 0);
}

float2 DatedBilinear(float2 t, float2 dims, bool a_displaced)
{
	t = clamp(t, 0.0, dims - 1.001);
	int2 t0 = (int2)t;
	float2 f = t - t0;
	int2 t1 = min(t0 + 1, int2(dims) - 1);

	float4 m00 = DeformationMap.Load(DeformTexel(int2(t0.x, t0.y), int2(dims)));
	float4 m10 = DeformationMap.Load(DeformTexel(int2(t1.x, t0.y), int2(dims)));
	float4 m01 = DeformationMap.Load(DeformTexel(int2(t0.x, t1.y), int2(dims)));
	float4 m11 = DeformationMap.Load(DeformTexel(int2(t1.x, t1.y), int2(dims)));
	float4 d = a_displaced ? float4(Displaced(m00), Displaced(m10), Displaced(m01), Displaced(m11)) :
	                         saturate(float4(m00.x, m10.x, m01.x, m11.x));

	float4 date = TrackClock.GatherRed(TrackClockSampler, (float2(t0 + MapOrigin) + 1.0) / dims).wzxy;
	date = max(date, AgeingClock - kTrackAgeHorizon);
	float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float depth = lerp(lerp(d.x, d.y, f.x), lerp(d.z, d.w, f.x), f.y);
	return float2(a_displaced ? saturate(depth) : depth, dot(w, d * date));
}

float2 Tap(float2 texel, float2 dims, bool a_displaced)
{
	float2 uv = (texel + 0.5) / dims;
	if (any(uv < 0.0) || any(uv > 1.0))
		return 0.0;
	return DatedBilinear(texel, dims, a_displaced);
}

bool BermTexel(uint2 phys)
{
	float2 dims;
	DeformationMap.GetDimensions(dims.x, dims.y);
	if (any(float2(phys) >= dims))
		return false;

	float invTexel = 1.0 / max(TexelSize, 1e-4);
	float2 centre = float2((int2(phys) - MapOrigin) & (int2(dims) - 1));

	float2 b = Tap(centre, dims, true);
	[unroll] for (int i = 0; i < 16; i++)
		b += Tap(centre + kBermTaps[i] * (BermReach * invTexel), dims, true);

	const float blurReach = max(kWallBlurReach * invTexel, 1.0) / 18.0;
	float2 s = Tap(centre, dims, false);
	[unroll] for (int j = 0; j < 8; j++)
		s += Tap(centre + kBermTaps[j] * blurReach, dims, false);

	const float berm = saturate(b.x / 17.0);
	const float blur = s.x / 9.0;
	OutBermField[phys] = float4(berm, blur, s.y / 9.0, b.y / 17.0);
	return berm != 0.0 || blur != 0.0;
}

[numthreads(8, 8, 1)]
void BermTiledCS(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GIdx : SV_GroupIndex)
{
	const uint listIndex = Gid.y * 1024u + Gid.x;
	if (listIndex >= BermTilesIn[0])
		return;
	const uint packed = BermTilesIn[1 + listIndex];
	const uint2 tile = uint2(packed & 0xFFFFu, packed >> 16u);
	if (GIdx == 0)
		gBermNonZero = 0;
	GroupMemoryBarrierWithGroupSync();
	if (BermTexel(tile * 8u + GTid.xy))
		InterlockedOr(gBermNonZero, 1u);
	GroupMemoryBarrierWithGroupSync();
	if (GIdx == 0) {
		OutBermOccupancy[tile] = gBermNonZero;
		OutBermDirty[tile] = 0u;
	}
}

void UntouchedTile(uint2 tile)
{
	uint2 dims;
	DeformOccupancyIn.GetDimensions(dims.x, dims.y);
	if (any(tile >= dims))
		return;
	uint touched = 0;
	[unroll] for (int dy = -1; dy <= 1; dy++)
	{
		[unroll] for (int dx = -1; dx <= 1; dx++)
		{
			const uint2 t = (tile + uint2(int2(dx, dy) + int2(dims))) & (dims - 1);
			touched |= DeformOccupancyIn[t] | BermOccupancyIn[t];
		}
	}
	OutUntouched[tile] = touched == 0 ? 1u : 0u;
}

[numthreads(8, 8, 1)]
void UntouchedCS(uint3 dtid : SV_DispatchThreadID)
{
	UntouchedTile(dtid.xy);
}

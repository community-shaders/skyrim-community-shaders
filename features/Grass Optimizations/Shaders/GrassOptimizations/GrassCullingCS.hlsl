#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

cbuffer CullParams : register(b0)
{
    float4 FrustumPlanes[6];

    float MinPixelSize;
    float FullDetailPixelSize;
    float LODMinKeep;
    float LODFadeBand;

    float MeshCostBias;
    float ProjScale;
    float MaxDistSq;
    float EdgeFadeStart;

    float AlphaParam1;
    float AlphaParam2;
    float FadeNow;
    float FadeInTimeRcp;

    float InvisibleFadeCull;
    float SimpleShadingPixelSize;
    float CollisionDistSq;
    float MidLODPixelSize;

    float MeshLODBandPx;
    float HiZEnabled;
    float2 HiZSize;

    float HiZTexelPixels;
    float HiZMipCount;
    float OcclusionBias;
    float CostBiasStartDist;

    float FarLODPixelSize;
    float3 _pad0;
};

cbuffer CullBucket : register(b1)
{
    uint InstanceCount;
    float WavePeriod;
    float TimeBase;
    float PrevTimeBase;
    float3 BoundCenter;
    float ModelRadius;
    float DistScale;
    float MinPixelScale;
    float IsComplex;
    float MidLODEnabled;
    // The dispatch covers the instances referenced by this slice range.
    uint SliceTableOffset;
    uint SliceCount;
    float FarLODEnabled;
    float _pad2;
};

ByteAddressBuffer Instances : register(t0);
StructuredBuffer<float4> Origins : register(t1);
// Scene-depth max pyramid; see GrassHiZCS.hlsl.
Texture2D<float> HiZ : register(t2);
// .x is the first source instance; .y is the compacted start index.
StructuredBuffer<uint2> SliceTable : register(t3);

RWByteAddressBuffer Compacted : register(u0);
RWStructuredBuffer<float4> Extras : register(u1);
RWByteAddressBuffer Counter : register(u2);

RWByteAddressBuffer MidLODCompacted : register(u3);
RWStructuredBuffer<float4> MidLODExtras : register(u4);

RWByteAddressBuffer FarLODCompacted : register(u5);
RWStructuredBuffer<float4> FarLODExtras : register(u6);
// Both LOD counts share one UAV so the shader stays within D3D11's eight-UAV limit.
RWByteAddressBuffer LODCounters : register(u7);

static const uint MiddleLODCountOffset = 0;
static const uint FarLODCountOffset = 4;

// Convert hashed bits to a uniform value in [0, 1).
float RandFloat(uint bits)
{
    const uint mantissaMask = 0x007FFFFFu;
    const uint one = 0x3F800000u;

    bits &= mantissaMask;
    bits |= one;
    
    return asfloat(bits) - 1.0;
}

// Precompute the per-instance wind term used by the vertex shader.
float WindScalar(float basis, float timer)
{
    const float a = 0.4 * (basis + timer);
    float sa, ca;
    sincos(a, sa, ca);
    const float t3 = 0.2 * cos(Math::PI * ca);
    const float t1 = sin(Math::PI * sa);
    const float t2 = sin(Math::TAU * sa);
    return (t1 + t2) * 0.3 + t3;
}

[numthreads(64, 1, 1)] void main(uint3 tid : SV_DispatchThreadID)
{
    const uint compactIdx = tid.x;
    if (compactIdx >= InstanceCount || SliceCount == 0)
        return;

    // Find the source slice containing this compacted index.
    uint lo = 0;
    uint hi = SliceCount - 1;
    [loop] while (lo < hi)
    {
        const uint mid = (lo + hi + 1) >> 1;
        if (SliceTable[SliceTableOffset + mid].y <= compactIdx)
            lo = mid;
        else
            hi = mid - 1;
    }
    const uint2 slice = SliceTable[SliceTableOffset + lo];

    // Use the source index to keep dither decisions stable across slice changes.
    const uint idx = slice.x + (compactIdx - slice.y);

    // Generate independent density and LOD dither values with one hash.
    const uint2 rand = Random::pcg2d(uint2(idx, 0u));

    const uint base = idx * 32;
    const uint4 raw0 = Instances.Load4(base);
    const uint4 raw1 = Instances.Load4(base + 16);

    const float2 localXY = float2(f16tof32(raw0.x & 0xFFFF), f16tof32(raw0.x >> 16));
    const float localZ = f16tof32(raw0.y & 0xFFFF);

    const float4 og = Origins[idx];
    const float3 world = float3(localXY, localZ) + og.xyz;

    const float3 dv = world - FrameBuffer::CameraPosAdjust.xyz;
    const float distSq = dot(dv, dv);

    const float dist = sqrt(distSq);

    // Ramp the mesh-cost bias in beyond CostBiasStartDist.
    const float costRamp = saturate((dist - CostBiasStartDist) / max(CostBiasStartDist, 1e-4));
    const float effCostBias = MeshCostBias * costRamp;

    const float dScale = lerp(1.0, DistScale, effCostBias);
    const float effMaxDistSq = MaxDistSq * dScale * dScale;
    if (distSq > effMaxDistSq)
        return;

	[unroll]
    for (uint p = 0; p < 6; ++p)
    {
        if (dot(FrustumPlanes[p].xyz, world) - FrustumPlanes[p].w < 0.0)
            return;
    }

    // Conservatively approximate the vertex shader's ScaleMask-based size variation.
    const float sizeVariance = f16tof32(raw1.z >> 16);
    const float instanceRadius = ModelRadius * (1.0 + max(sizeVariance, 0.0));

    const float projPx = (instanceRadius / dist) * ProjScale;
    const float pxScale = lerp(1.0, MinPixelScale, effCostBias);
    const float effMinPx = MinPixelSize * pxScale;
    if (projPx < effMinPx)
        return;

    if (HiZEnabled > 0.5)
    {
        // Transform the model-space bounding-sphere center into instance space.
        const float3 rot0 = float3(f16tof32(raw0.z & 0xFFFF), f16tof32(raw0.z >> 16), f16tof32(raw0.w & 0xFFFF));
        const float3 rot1 = float3(f16tof32(raw1.x & 0xFFFF), f16tof32(raw1.x >> 16), f16tof32(raw1.y & 0xFFFF));
        const float3 rot2 = float3(f16tof32(raw1.z & 0xFFFF), f16tof32(raw0.w >> 16), f16tof32(raw1.y >> 16));

        const float3 msCentre = BoundCenter * (1.0 + max(sizeVariance, 0.0));
        const float3 dvC = dv + float3(dot(rot0, msCentre), dot(rot1, msCentre), dot(rot2, msCentre));

        const float occRadius = instanceRadius + length(BoundCenter) * abs(sizeVariance);
        const float distC = max(length(dvC), 1e-4);
        const float projPxOcc = (occRadius / distC) * ProjScale;

        const float4 clipC = mul(FrameBuffer::CameraViewProj, float4(dvC, 1.0));
        if (clipC.w > 0.0)
        {
            const float2 uv = (clipC.xy / clipC.w) * float2(0.5, -0.5) + 0.5;
            const float2 tc = uv * HiZSize;
            // Projected radius in level-0 Hi-Z texels.
            const float rT = projPxOcc / HiZTexelPixels;

            // Select a mip coarse enough for the 3x3 footprint to cover the sphere.
            const float wantLevel = ceil(log2(max(2.0 * rT, 1.0)));

            // Skip occlusion when no available mip can cover the sphere safely.
            [branch] if (wantLevel <= HiZMipCount - 1.0)
            {
            const int level = (int)wantLevel;
            const float scale = exp2((float)level);
            const float2 tcL = tc / scale;
            const float rTL = rT / scale;
            const int2 dimL = max(int2(ceil(HiZSize / scale)), int2(1, 1));

            const int2 t0 = int2(floor(tcL - rTL));
            const int2 t1 = int2(floor(tcL + rTL));

            // Test the sphere's nearest point; a camera inside it yields a non-occluded depth.
            const float3 dvNear = dvC * (max(distC - occRadius, 0.0) / distC);
            const float4 clipN = mul(FrameBuffer::CameraViewProj, float4(dvNear, 1.0));
            const float nearZ = clipN.z / max(clipN.w, 1e-4);

            float tileMax = 0.0;
			[unroll] for (int y = 0; y < 3; ++y)
            {
				[unroll] for (int x = 0; x < 3; ++x)
                {
                    if (t0.x + x <= t1.x && t0.y + y <= t1.y)
                    {
                        const int2 t = clamp(t0 + int2(x, y), int2(0, 0), dimL - 1);
                        tileMax = max(tileMax, HiZ.Load(int3(t, level)));
                    }
                }
            }

            // Cull only when the sphere is behind every sampled tile, allowing for depth error.
            if (nearZ > tileMax + OcclusionBias)
                return;
            }
        }
    }

    float lodFade = 1.0;
    const float effFullPx = FullDetailPixelSize * pxScale;
    if (projPx < effFullPx)
    {
        const float t = saturate((effFullPx - projPx) / max(effFullPx - effMinPx, 1e-4));
        const float keep = lerp(1.0, LODMinKeep, t);
        const float h = RandFloat(rand.x);
        if (h > keep + LODFadeBand)
            return;
        lodFade = saturate((keep + LODFadeBand - h) / LODFadeBand);
    }

    const float maxDist = sqrt(effMaxDistSq);
    const float edgeStart = maxDist * EdgeFadeStart;
    const float edgeFade = saturate((maxDist - dist) / max(maxDist - edgeStart, 1e-4));

    const float4 clip = mul(FrameBuffer::CameraViewProj, float4(dv, 1.0));
    const float distFade = 1.0 - saturate((length(clip.xyz) - AlphaParam1) / AlphaParam2);
    const float spawnFade = saturate((FadeNow - og.w) * FadeInTimeRcp);
    
    const float fade = distFade * spawnFade * lodFade * edgeFade;
    if (fade <= InvisibleFadeCull)
        return;

    const float basis = (localXY.x + localXY.y) * -0.0078125;

    const float4 e0 = float4(og.xyz, IsComplex);
    
    const float collisionFlag = (distSq < CollisionDistSq) ? 1.0 : 0.0;
    const float farFlag = (SimpleShadingPixelSize > 0.0 && projPx < SimpleShadingPixelSize) ? 2.0 : 0.0;

    const float4 e1 = float4(WindScalar(basis, TimeBase * WavePeriod), WindScalar(basis, PrevTimeBase * WavePeriod), fade, collisionFlag + farFlag);

    // Dither both LOD transitions with the same value to keep tier changes gradual and ordered.
    const float h = RandFloat(rand.y);
    const float halfBand = MeshLODBandPx * 0.5;
    const float bandRcp = 1.0 / max(MeshLODBandPx, 1e-4);

    uint tier = 0;
    if (MidLODEnabled > 0.5 && h < saturate((MidLODPixelSize + halfBand - projPx) * bandRcp))
        tier = 1;
    if (FarLODEnabled > 0.5 && h < saturate((FarLODPixelSize + halfBand - projPx) * bandRcp))
        tier = 2;

    // Store the tier above the two low flag bits in e1.w.
    const float4 e1Tier = float4(e1.xyz, e1.w + 4.0 * (float)tier);

    uint slot;
    if (tier == 2)
    {
        LODCounters.InterlockedAdd(FarLODCountOffset, 1, slot);
        FarLODCompacted.Store4(slot * 32, raw0);
        FarLODCompacted.Store4(slot * 32 + 16, raw1);
        FarLODExtras[slot * 2 + 0] = e0;
        FarLODExtras[slot * 2 + 1] = e1Tier;
    }
    else if (tier == 1)
    {
        LODCounters.InterlockedAdd(MiddleLODCountOffset, 1, slot);
        MidLODCompacted.Store4(slot * 32, raw0);
        MidLODCompacted.Store4(slot * 32 + 16, raw1);
        MidLODExtras[slot * 2 + 0] = e0;
        MidLODExtras[slot * 2 + 1] = e1Tier;
    }
    else
    {
        Counter.InterlockedAdd(0, 1, slot);
        Compacted.Store4(slot * 32, raw0);
        Compacted.Store4(slot * 32 + 16, raw1);
        Extras[slot * 2 + 0] = e0;
        Extras[slot * 2 + 1] = e1;
    }
}

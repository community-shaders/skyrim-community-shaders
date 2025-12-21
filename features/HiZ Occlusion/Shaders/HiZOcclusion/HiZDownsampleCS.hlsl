// HiZDownsampleCS.hlsl
// Downsamples mip N to mip N+1 using 2x2 MAX reduction (stores the farthest depth for conservative occlusion culling).

Texture2D<float> SrcMip : register(t0);
RWTexture2D<float> DstMip : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint w, h;
    DstMip.GetDimensions(w, h);

    uint2 dst = dispatchThreadID.xy;
    if (dst.x >= w || dst.y >= h)
        return;

    // Gather 2x2 texels from source (account for odd sizes by clamping)
    uint2 srcBase = dst * 2;

    uint sw, sh;
    SrcMip.GetDimensions(sw, sh);

    uint2 p0 = uint2(min(srcBase.x + 0, sw - 1), min(srcBase.y + 0, sh - 1));
    uint2 p1 = uint2(min(srcBase.x + 1, sw - 1), min(srcBase.y + 0, sh - 1));
    uint2 p2 = uint2(min(srcBase.x + 0, sw - 1), min(srcBase.y + 1, sh - 1));
    uint2 p3 = uint2(min(srcBase.x + 1, sw - 1), min(srcBase.y + 1, sh - 1));

    float d0 = SrcMip.Load(int3(p0, 0));
    float d1 = SrcMip.Load(int3(p1, 0));
    float d2 = SrcMip.Load(int3(p2, 0));
    float d3 = SrcMip.Load(int3(p3, 0));

    // Use MAX to store farthest depth - conservative for occlusion testing
    // Standard depth: 0=near, 1=far, so MAX = farthest = most conservative
    float maxDepth = max(max(d0, d1), max(d2, d3));
    DstMip[dst] = maxDepth;
}

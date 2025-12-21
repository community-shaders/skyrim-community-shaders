// HiZBuildLevel0CS.hlsl
// Copies previous-frame depth buffer into Hi-Z pyramid mip 0.
// Depth is assumed to be linearized 0..1 (standard D3D depth). For reverse-Z, adjust downsample pass accordingly.

Texture2D<float> DepthTex : register(t0);
RWTexture2D<float> OutMip0 : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint w, h;
    OutMip0.GetDimensions(w, h);

    uint2 pix = dispatchThreadID.xy;
    if (pix.x >= w || pix.y >= h)
        return;

    float d = DepthTex.Load(int3(pix, 0));
    OutMip0[pix] = d;
}

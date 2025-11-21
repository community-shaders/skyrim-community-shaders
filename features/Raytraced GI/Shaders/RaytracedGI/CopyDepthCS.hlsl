Texture2D<unorm float> DepthIn  : register(t0);
RWTexture2D<float> DepthOut     : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    DepthOut[id] = DepthIn[id];
}
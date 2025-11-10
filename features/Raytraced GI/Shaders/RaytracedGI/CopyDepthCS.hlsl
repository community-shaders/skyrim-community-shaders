Texture2D<unorm float> Depth            : register(t0);
RWTexture2D<float4> MeshNormalDepth     : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    MeshNormalDepth[id] = float4(MeshNormalDepth[id].xyz, Depth[id]);
}
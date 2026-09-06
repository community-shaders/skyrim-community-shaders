Texture2D<float4> BaseTex : register(t0);
RWStructuredBuffer<uint> Result : register(u0);

[numthreads(1, 1, 1)] void main()
{
    // Get the dimensions here as the rendertexture field reports them as 0 at this point while loading the cell.
    uint w, h;
    BaseTex.GetDimensions(w, h);

    // Complex grass packs a normal map into the lower half of the atlas, so the bottom-left texel decodes to unit length only when one is present.
    float3 complexTest = BaseTex.Load(int3(0, (int)h - 1, 0)).xyz * 2.0 - 1.0;

    Result[0] = asuint(length(complexTest));
}

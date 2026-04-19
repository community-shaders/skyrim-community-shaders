Texture2D<float> depth : register(t0);
RWTexture2D<float> outDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    uint depth_width, depth_height;
    depth.GetDimensions(depth_width, depth_height);
    float2 depth_dim = float2(depth_width, depth_height);
    
    uint out_depth_width, out_depth_height;
    outDepth.GetDimensions(out_depth_width, out_depth_height);
    float2 out_depth_dim = float2(out_depth_width, out_depth_height);
    
    float2 ratio = depth_dim / out_depth_dim;
    
    uint2 vReadCoord = DTid << 1;
    uint2 vWriteCoord = DTid;
    
    if (vWriteCoord.x >= out_depth_width || vWriteCoord.y >= out_depth_height)
        return;
    
    float4 depth_samples = float4(
        depth[vReadCoord].x,
        depth[vReadCoord + uint2(1, 0)].x,
        depth[vReadCoord + uint2(0, 1)].x,
        depth[vReadCoord + uint2(1, 1)].x
    );
    
    float min_depth = min(depth_samples.x, min(depth_samples.y, min(depth_samples.z, depth_samples.w)));
    
    bool needExtraSampleX = ratio.x > 2;
    bool needExtraSampleY = ratio.y > 2;
    
    if (needExtraSampleX)
    {
        min_depth = min(min_depth, min(depth[vReadCoord + uint2(2, 0)].x, depth[vReadCoord + uint2(2, 1)].x));
    }
    
    if (needExtraSampleY)
    {
        min_depth = min(min_depth, min(depth[vReadCoord + uint2(0, 2)].x, depth[vReadCoord + uint2(1, 2)].x));
    }
    
    if (needExtraSampleX && needExtraSampleY)
    {
        min_depth = min(min_depth, depth[vReadCoord + uint2(2, 2)].x);
    }
    
    outDepth[vWriteCoord] = min_depth;
}

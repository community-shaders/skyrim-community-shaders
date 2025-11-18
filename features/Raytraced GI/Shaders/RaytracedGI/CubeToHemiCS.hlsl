TextureCube<float4> CubeMap       : register(t0);
SamplerState Sampler              : register(s0);

RWTexture2D<float3> HemisphereOut : register(u0);

static const uint Resolution = 512;

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (id.x >= Resolution || id.y >= Resolution)
        return;

    float2 uv = float2(id.x + 0.5, id.y + 0.5) / float2(Resolution, Resolution);
    float2 xy = uv * 2.0 - 1.0;

    float r = length(xy);

    //if (r > 1) discard;

    float phi = atan2(xy.y, xy.x);

    float z = 1.0f - r*r;
    float k = sqrt(1.0f - z*z);

    float3 dir = float3(k * cos(phi), k * sin(phi), z);

    HemisphereOut[id.xy] = CubeMap.SampleLevel(Sampler, dir, 0.0f).rgb;
}

[numthreads(8, 8, 1)]
void main3(uint2 id : SV_DispatchThreadID)
{
    if (id.x >= Resolution || id.y >= Resolution)
        return;

    float2 uv = (float2(id) + 0.5) / float2(Resolution, Resolution);

    float2 p = uv * 2.0 - 1.0;


    float3 dir = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));

    if (dir.z < 0.0)
    {
        float2 folded = normalize(dir.xy);
        dir = float3(folded.x, folded.y, 0.0);
    }

    dir = normalize(dir);
    
    HemisphereOut[id.xy] = CubeMap.SampleLevel(Sampler, dir, 0.0f).rgb;
}

[numthreads(8, 8, 1)]
void main2(uint2 id : SV_DispatchThreadID)
{
    if (id.x >= Resolution || id.y >= Resolution)
        return;

    float2 uv = float2(id.x + 0.5, id.y + 0.5) / float2(Resolution, Resolution);
    float2 xy = uv * 2.0 - 1.0;

    float r2 = dot(xy, xy);

    if (r2 > 1.0)
    {
        HemisphereOut[id.xy] = float3(0, 0, 0);
        return;
    }

    float z = sqrt(1.0 - r2);

    
    float3 dir = float3(xy.x, xy.y, z);

    dir = normalize(dir);

    HemisphereOut[id.xy] = CubeMap.SampleLevel(Sampler, dir, 0.0f).rgb;
}
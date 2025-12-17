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

    float phi = atan2(xy.y, xy.x);

    float z = 1.0f - r*r;
    float k = sqrt(1.0f - z*z);

    float3 dir = float3(k * sin(phi), k * cos(phi), z);

    HemisphereOut[id.xy] = CubeMap.SampleLevel(Sampler, dir, 0.0f).rgb;
}
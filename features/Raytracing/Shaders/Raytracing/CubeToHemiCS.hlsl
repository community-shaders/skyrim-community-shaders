TextureCube<float4> CubeMap       : register(t0);
SamplerState Sampler              : register(s0);

RWTexture2D<float4> HemisphereOut : register(u0);

static const uint Resolution = 512;

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (id.x >= Resolution || id.y >= Resolution)
        return;

    const float2 uv = float2(id.xy + 0.5f) / float2(Resolution, Resolution);
    const float2 xy = uv * 2.0f - 1.0f;

    const float r = length(xy);

    const float phi = atan2(xy.y, xy.x);

    const float z = 1.0f - r*r;
    const float k = sqrt(1.0f - z*z);

    const float3 dir = float3(k * sin(phi), k * cos(phi), z);

    HemisphereOut[id.xy] = CubeMap.SampleLevel(Sampler, dir, 0.0f);
}
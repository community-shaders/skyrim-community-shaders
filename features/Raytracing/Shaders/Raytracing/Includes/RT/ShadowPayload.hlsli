#ifndef SHADOWPAYLOAD_HLSL
#define SHADOWPAYLOAD_HLSL

struct ShadowPayload
{
    float missed;
    float3 transmission;
    uint randomSeed;
};

#endif // SHADOWPAYLOAD_HLSL
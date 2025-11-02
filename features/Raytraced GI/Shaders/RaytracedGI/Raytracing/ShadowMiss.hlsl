#include "RaytracedGI/Raytracing/Types.hlsli"

[shader("miss")]
void main(inout ShadowPayload payload)
{
    payload.missed = true;
}

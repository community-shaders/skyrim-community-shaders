#include "Raytracing/Includes/Types.hlsli"

[shader("miss")]
void main(inout ShadowPayload payload)
{
    payload.missed = 1.0f;
}

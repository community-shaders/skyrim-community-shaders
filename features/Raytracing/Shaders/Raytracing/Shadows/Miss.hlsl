#include "Raytracing/Shadows/Payload.hlsli"

[shader("miss")]
void main(inout Payload payload)
{
    payload.missed = 1.0f;
}

#ifndef SPECULAR_PAYLOAD_HLSI
#define SPECULAR_PAYLOAD_HLSI

#include "RaytracedGI/Includes/RT/Payload.hlsli"

struct SpecularPayload : Payload
{
    float distance;
};

#endif // SPECULAR_PAYLOAD_HLSI
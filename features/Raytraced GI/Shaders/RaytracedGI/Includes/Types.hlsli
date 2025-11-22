#ifndef TYPES_HLSI
#define TYPES_HLSI

struct half2p
{
    uint packed;
    
    half2 unpack()
    {
        return half2(
            f16tof32(packed & 0xFFFF),
            f16tof32(packed >> 16));
    }
};

struct ubyte4f
{
    uint packed;

    half4 unpack()
    {
        half4 result;
        result.x = (packed & 0xFF) / 255.0;
        result.y = ((packed >> 8)  & 0xFF) / 255.0;
        result.z = ((packed >> 16) & 0xFF) / 255.0;
        result.w = (packed >> 24) / 255.0;
        return result;
    }
};

struct byte4f
{
    ubyte4f packed;

    half4 unpack()
    {
        return packed.unpack() * 2.0f - 1.0f;
    }
};


#include "RaytracedGI/Includes/Types/Vertex.hlsli"
#include "RaytracedGI/Includes/Types/Instance.hlsli"
#include "RaytracedGI/Includes/Types/Light.hlsli"
#include "RaytracedGI/Includes/RT.hlsli"
#include "RaytracedGI/Includes/Types/GIFrameData.hlsli"

#endif
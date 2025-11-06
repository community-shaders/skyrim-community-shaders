#ifndef FRAMEDATA_HLSI
#define FRAMEDATA_HLSI

#include "RaytracedGI/Raytracing/Types/Light.hlsli"

struct FrameData
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 Position;
    Light Directional;
    uint LightCount;
    uint FrameCount;
	uint Pad0;
	uint Pad1;    
};

#endif
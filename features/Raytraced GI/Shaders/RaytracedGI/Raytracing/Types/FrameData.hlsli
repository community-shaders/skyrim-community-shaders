#ifndef FRAMEDATA_HLSI
#define FRAMEDATA_HLSI

#include "RaytracedGI/Raytracing/Types/Light.hlsli"

struct FrameData
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;    
    Light Directional;
    float3 Position;
    uint FrameCount;   
    float SHARCScale;
    uint Pad0;
    uint Pad1;
    uint Pad2;    
};

#endif
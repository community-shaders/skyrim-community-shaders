#ifndef FRAMEDATA_HLSI
#define FRAMEDATA_HLSI

#include "RaytracedGI/Includes/Types/Light.hlsli"

struct FrameData
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;    
    Light Directional;
    float3 Position;
    uint FrameCount; 
    float Diffuse;
    float Specular;
    float Emissive;   
    #ifdef SHARC
    float SHARCScale;
    #else
    uint Pad1;
    #endif    
};

#endif
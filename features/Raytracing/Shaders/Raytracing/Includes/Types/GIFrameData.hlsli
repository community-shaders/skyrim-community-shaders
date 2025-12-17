#ifndef GI_FRAMEDATA_HLSI
#define GI_FRAMEDATA_HLSI

#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/SharedData.hlsli"

struct 
#ifdef __cplusplus
alignas(16)
#endif   
    GIFrameData
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;    
    Light Directional;
    float3 Position;
    uint FrameCount;
	float2 Roughness; 
 	float2 Metalness;   
    float Diffuse;
    float Specular;
    float Emissive;
	float Effect;
	float Sky;
    #ifdef SHARC
    float SHaRCScale;
    uint SHaRCCapacity;
    BOOL SHaRCUpdatePass;
    #else
    uint Pad0;
    uint Pad1;
    #endif      
	uint Pad2;
    float4 Pad3[11];
    FeatureData Features;
};
#ifdef __cplusplus
static_assert(sizeof(GIFrameData) % 256 == 0);
#endif

#endif
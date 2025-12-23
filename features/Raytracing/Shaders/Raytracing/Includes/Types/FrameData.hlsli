#ifndef GI_FRAMEDATA_HLSL
#define GI_FRAMEDATA_HLSL

#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/SharedData.hlsli"

struct
#ifdef __cplusplus
alignas(16)
#endif   
    SHaRCFrameData
{
    BOOL Enabled;
    float SceneScale;
    uint AccumFrameNum;
    uint StaleFrameNum;
    float RadianceScale;    
    BOOL AntifireflyFilter; 
    uint Capacity;
    BOOL UpdatePass;    
};
#ifdef __cplusplus
static_assert(sizeof(SHaRCFrameData) % 4 == 0);
#endif

struct
#ifdef __cplusplus
alignas(16)
#endif   
    FrameData
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 CameraData;
    float4 NDCToView;
    Light Directional;
    float3 Position;
    uint FrameCount;
    float3 PositionPrev;
    float2 Roughness;
    float2 Metalness;
    uint2 DispatchSize;
    float Emissive;
    float Effect;
    float Sky;
    BOOL RussianRoulette;
    uint3 Pad0;
    SHaRCFrameData SHaRC;
    FeatureData Features;
    uint4 Pad1[8];
};
#ifdef __cplusplus
static_assert(sizeof(FrameData) == 768);
#endif

#endif
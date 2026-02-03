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
    DirectionalLight Directional;
    float3 Position;
    uint FrameCount;
    float3 PositionPrev;
    BOOL RussianRoulette;
    float2 Roughness;
    float2 Metalness;
    uint2 DispatchSize;
    float Emissive;
    float Effect;
    float Sky;
    float3 EmittanceColor;
    SHaRCFrameData SHaRC;
    FeatureData Features;
    uint Lights;
    float PixelConeSpreadAngle;
    float TexLODBias;
    float CloudOpacity;
    int SSSSampleCount;
    int SSSTransmissionBsdfSampleCount;
    int SSSTransmissionPerBsdfScatteringSampleCount;
    BOOL EnableSssTransmission;
    float3x4 Pad0;
    float4x4 Pad1;
    float4x4 Pad2;
    float3x4 Pad3;
};
#ifdef __cplusplus
static_assert(sizeof(FrameData) == 1024);
#endif

#endif
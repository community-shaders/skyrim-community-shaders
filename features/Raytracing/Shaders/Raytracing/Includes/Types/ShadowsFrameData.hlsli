#ifndef SHADOW_FRAMEDATA_HLSL
#define SHADOW_FRAMEDATA_HLSL

struct
#ifdef __cplusplus
alignas(16)
#endif
    ShadowsFrameData
{
    float4 CameraData;
    float4 NDCToView;
    float4x4 ViewInverse;
    float4 Position;
    float4 Direction;
	float4x4 Pad0;
	float4x4 Pad1;
};
#ifdef __cplusplus
static_assert(sizeof(ShadowsFrameData) % 256 == 0);
#endif

#endif
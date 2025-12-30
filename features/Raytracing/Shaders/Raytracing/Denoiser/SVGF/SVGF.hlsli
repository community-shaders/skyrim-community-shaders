#ifndef SVGF_HLSI
#define SVGF_HLSI

struct SVGF
{
    float InvMaxAccumulatedFrames;
    uint AtrousIterations;
    float ColorPhi;
    float NormalPhi;
    uint2 Resolution;
    float2 ResolutionRcp;
    float4x4 CameraProjUnjitteredInverse;
    float4x4 CameraViewInverse;
    float4x4 CameraPreviousViewProjUnjittered;
    uint4 Pad0;
    uint4 Pad1;    
};
#ifdef __cplusplus
static_assert(sizeof(SVGF) % 256 == 0);
#endif

#endif // SVGF_HLSI
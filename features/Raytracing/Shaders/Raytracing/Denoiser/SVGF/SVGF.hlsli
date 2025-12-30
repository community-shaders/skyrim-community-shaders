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
};

#endif // SVGF_HLSI
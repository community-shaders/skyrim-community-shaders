#ifndef SVGF_HLSI
#define SVGF_HLSI

struct
#ifdef __cplusplus
alignas(16)
#endif
    SVGF
{
    float InvMaxAccumulatedFrames;
    uint AtrousIterations;
    float ColorPhi;
    float NormalPhi;
};
#ifdef __cplusplus
static_assert(sizeof(SVGF) % 16 == 0);
#endif

#endif // SVGF_HLSI
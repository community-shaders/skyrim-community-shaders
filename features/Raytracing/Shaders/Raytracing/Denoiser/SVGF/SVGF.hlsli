#ifndef SVGF_HLSI
#define SVGF_HLSI

struct
#ifdef __cplusplus
alignas(16)
#endif
    SVGF
{
    uint AtrousIterations;    
    float Alpha;
    float MomentsAlpha;
    float ColorPhi;
    float NormalPhi;
    float DepthPhi;    
    float DepthThreshold;
    float NormalThreshold;
    uint HistoryThreshold;
    float4 NDCToView;
    uint3 Pad;
};
#ifdef __cplusplus
static_assert(sizeof(SVGF) % 16 == 0);
#endif

#endif // SVGF_HLSI
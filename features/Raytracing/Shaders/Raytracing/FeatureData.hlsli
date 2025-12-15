#ifndef RT_FEATUREDATA_HLSI
#define RT_FEATUREDATA_HLSI

namespace RaytracingFD
{
// Used in shaders to get proper lighting per active RT effect. 
// GI does all of the indirect diffuse + specular, DDGI is indirect diffuse only, shadows need all of them enabled, etc...
    struct
#ifdef __cplusplus
    alignas(16)
#endif   
    FeatureData
    {
        float InteriorDirectional;
        float Ambient;
        float EnvMap;
        uint Pad;
    };
#ifdef __cplusplus
    static_assert(sizeof(FeatureData) % 16 == 0);
#endif
}

#endif
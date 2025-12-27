#ifndef RT_FEATUREDATA_HLSI
#define RT_FEATUREDATA_HLSI

#ifdef __cplusplus
namespace RaytracingFD
{
    // Used in shaders to get proper lighting for the active Raytracing effects.
    // GI does all of the indirect diffuse + specular, DDGI is indirect diffuse only, shadows need all of them enabled, etc...
    struct alignas(16) FeatureData
#else
    struct RaytracingSettings
#endif
    {
        float InteriorDirectional;
        float Ambient;
        float EnvMap;
        uint Albedo;
    };
#ifdef __cplusplus
    static_assert(sizeof(FeatureData) % 16 == 0);
}
#endif

#endif
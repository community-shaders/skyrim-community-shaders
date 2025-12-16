#ifndef SHARED_DATA_HLSI
#define SHARED_DATA_HLSI
// A lighter version of SharedData containing only the necessary structs for HLSL 6.0+ compatibility

#ifndef __cplusplus
typedef bool BOOL;
#endif

struct CPMSettings
{
    BOOL EnableComplexMaterial;
    BOOL EnableParallax;
    BOOL EnableTerrainParallax;
    BOOL EnableHeightBlending;
    BOOL EnableShadows;
    BOOL ExtendShadows;
    BOOL EnableParallaxWarpingFix;
    uint pad0;
};
#ifdef __cplusplus
static_assert(sizeof(CPMSettings) % 16 == 0);
#endif

struct WetnessEffectsSettings
{
    #ifndef __cplusplus
    row_major 
    #endif    
    float4x4 OcclusionViewProj;

    float Time;
    float Raining;
    float Wetness;
    float PuddleWetness;

    BOOL EnableWetnessEffects;
    float MaxRainWetness;
    float MaxPuddleWetness;
    float MaxShoreWetness;

    uint ShoreRange;
    float PuddleRadius;
    float PuddleMaxAngle;
    float PuddleMinWetness;

    float MinRainWetness;
    float SkinWetness;
    float WeatherTransitionSpeed;
    BOOL EnableRaindropFx;

    BOOL EnableSplashes;
    BOOL EnableRipples;
    uint EnableVanillaRipples;
    float RaindropFxRange;

    float RaindropGridSizeRcp;
    float RaindropIntervalRcp;
    float RaindropChance;
    float SplashesLifetime;

    float SplashesStrength;
    float SplashesMinRadius;
    float SplashesMaxRadius;
    float RippleStrength;

    float RippleRadius;
    float RippleBreadth;
    float RippleLifetimeRcp;
    float pad0;
};
#ifdef __cplusplus
static_assert(sizeof(WetnessEffectsSettings) % 16 == 0);
#endif

struct CloudShadowsSettings
{
    float Opacity;
    float3 pad0;
};
#ifdef __cplusplus
static_assert(sizeof(CloudShadowsSettings) % 16 == 0);
#endif

struct HairSpecularSettings
{
    uint Enabled;
    float HairGlossiness;
    float SpecularMult;
    float DiffuseMult;
    uint EnableTangentShift;
    float PrimaryTangentShift;
    float SecondaryTangentShift;
    float HairSaturation;
    float SpecularIndirectMult;
    float DiffuseIndirectMult;
    float BaseColorMult;
    float Transmission;
    uint EnableSelfShadow;
    float SelfShadowStrength;
    float SelfShadowExponent;
    float SelfShadowScale;
    uint HairMode; // 0: Kajiya-Kay, 1: Marschner
    uint pad0;
    uint pad1;
    uint pad2;    
};
#ifdef __cplusplus
static_assert(sizeof(HairSpecularSettings) % 16 == 0);
#endif

struct ExtendedTranslucencySettings
{
    uint MaterialModel; // [0,1,2,3] The MaterialModel
    float Reduction; // [0, 1.0] The factor to reduce the transparency to matain the average transparency [0,1]
    float Softness; // [0, 2.0] The soft remap upper limit [0,2]
    float Strength; // [0, 1.0] The inverse blend weight of the effect
};
#ifdef __cplusplus
static_assert(sizeof(ExtendedTranslucencySettings) % 16 == 0);
#endif

struct FeatureData
{
    CPMSettings ExtendedMaterial;
    WetnessEffectsSettings WetnessEffects;
    CloudShadowsSettings CloudShadows;
    HairSpecularSettings HairSpecular;
    ExtendedTranslucencySettings ExtendedTranslucency;
};
#ifdef __cplusplus
static_assert(sizeof(FeatureData) % 16 == 0);
#endif

#endif
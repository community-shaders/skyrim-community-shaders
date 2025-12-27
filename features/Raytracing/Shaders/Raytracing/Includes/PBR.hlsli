#ifndef PBR_HLSL
#define PBR_HLSL

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Math.hlsli"

#include "Raytracing/Includes/Common.hlsli"

namespace PBR
{
    namespace Flags
    {
        static const uint16_t HasEmissive = (1 << 0);
        static const uint16_t HasDisplacement = (1 << 1);
        static const uint16_t HasFeatureTexture0 = (1 << 2);
        static const uint16_t HasFeatureTexture1 = (1 << 3);
        static const uint16_t Subsurface = (1 << 4);
        static const uint16_t TwoLayer = (1 << 5);
        static const uint16_t ColoredCoat = (1 << 6);
        static const uint16_t InterlayerParallax = (1 << 7);
        static const uint16_t CoatNormal = (1 << 8);
        static const uint16_t Fuzz = (1 << 9);
        static const uint16_t HairMarschner = (1 << 10);
        static const uint16_t Glint = (1 << 11);
        static const uint16_t ProjectedGlint = (1 << 12);
    }

    namespace Defaults
    {
        static const float Roughness = 0.5f;
        static const float Metallic = 0.0f;
        static const float3 F0 = float3(0.04f, 0.04f, 0.04f);
    }

    namespace Constants
    {
        static const float MinRoughness = 0.04f;
        static const float MaxRoughness = 1.0f;
        static const float MinGlintDensity = 1.0f;
        static const float MaxGlintDensity = 40.0f;
        static const float MinGlintRoughness = 0.005f;
        static const float MaxGlintRoughness = 0.3f;
        static const float MinGlintDensityRandomization = 0.0f;
        static const float MaxGlintDensityRandomization = 5.0f;
    }

    float Roughness(float linearRoughness, float lower, float upper)
    {
        return clamp(Remap(linearRoughness, lower, upper), Constants::MinRoughness, Constants::MaxRoughness);
    }

    float3 F0(float3 albedo, float metalness)
    {
        return saturate(lerp(Defaults::F0, albedo, metalness));
    }

    float3 F0(float3 specularLevel, float3 albedo, float metalness)
    {
        return saturate(lerp(specularLevel, albedo, metalness));
    }
}

#endif  // PBR_HLSL
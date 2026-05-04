#ifndef VANILA_TO_PBR_HLSLI
#define VANILA_TO_PBR_HLSLI

#include "Common/Color.hlsli"

namespace VanillaToPBR
{
    float Specularity(float3 specularColor, float glossiness)
    {
        return Color::RGBToLuminance(specularColor * glossiness);
    }
    
    float ShininessToBaseRoughness(float shininess)
    {
        return pow(2.0f / (min(abs(shininess), 1024.0f) + 2.0f), 0.25f);
    }
    
    float Roughness(float shininess, float3 specularColor, float glossiness)   
    {
        return saturate(ShininessToBaseRoughness(shininess) + (1.0f - Specularity(specularColor, glossiness)) * 0.25f);

    }
}

#endif
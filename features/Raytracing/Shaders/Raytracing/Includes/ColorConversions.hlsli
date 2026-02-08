#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/SharedData.hlsli"

#define LLSETTINGS Frame.Features.LinearLighting
#define LLON LLSETTINGS.enableLinearLighting

float3 ColorToLinear(float3 color)
{
    return pow(abs(color), (LLON ? LLSETTINGS.colorGamma : 2.2f));
}

float3 EffectToLinear(float3 color)
{
    return pow(abs(color), (LLON ? LLSETTINGS.effectGamma : 2.2f)) * (LLON ? LLSETTINGS.effectLightingMult : 1.0);
}

float3 LightToLinear(float3 color)
{
    return pow(abs(color), LLSETTINGS.lightGamma);
}

float3 PointLightToLinear(float3 color, bool isLinear)
{
    return (isLinear && LLON) ? color : LightToLinear(color) * LLSETTINGS.pointLightMult;
}

float3 DirLightToLinear(float3 color)
{
    return (LLSETTINGS.isDirLightLinear && LLON) ? color : LightToLinear(color) * LLSETTINGS.directionalLightMult * LLSETTINGS.dirLightMult;
}

float3 GlowToLinear(float3 color)
{
    return LLON ? pow(abs(color), LLSETTINGS.glowmapGamma) * LLSETTINGS.glowmapMult : color;
}

float3 VanillaDiffuseColor(float3 color)
{
    return saturate(ColorToLinear(color) * LLSETTINGS.vanillaDiffuseColorMult);
}

float3 LLGammaToTrueLinear(float3 color)
{
    return LLON ? color : pow(abs(color), 2.2f);
}

float3 LLTrueLinearToGamma(float3 color)
{
    return LLON ? color : pow(abs(color), 1.0f / 2.2f);
}

float3 EmitColorToLinear(float3 color)
{
    return LLON ? (pow(abs(color), LLSETTINGS.emitColorGamma)) : color;
}

float EmitColorMult()
{
    return LLON ? LLSETTINGS.emitColorMult : 1.0f;
}
#endif
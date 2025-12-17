#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "Raytracing/Includes/PBR.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"

#define Surface(...) static Surface ctor(__VA_ARGS__)
struct Surface
{   
    float Position;
    float3 GeomNormal;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float3 Albedo;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float AO;
    float3 F0;
#if defined(FULL_MATERIAL)
    float3 SubsurfaceColor;
    float Thickness;
    float3 CoatColor;
    float CoatStrength;
    float CoatRoughness;
    float3 CoatF0;
    float3 FuzzColor;
    float FuzzWeight;
    float GlintScreenSpaceScale;
    float GlintLogMicrofacetDensity;
    float GlintMicrofacetRoughness;
    float GlintDensityRandomization;
    //Glints::GlintCachedVars GlintCache;
    float Noise;    
#endif

    Surface(float3 position, float3 geomNormal, float3 normal, float3 tangent, float3 bitangent, Material material) {
        Surface surface;
    
        surface.Position = position;
        surface.GeomNormal = geomNormal;
        surface.Normal = normal;
        surface.Tangent = tangent;
        surface.Bitangent = bitangent;
        surface.Albedo = albedo;
        surface.Roughness = roughness;
        surface.Metallic = metallic;
        surface.Emissive = emissive;
        surface.AO = ao;        
        
        surface.F0 = PBR::F0(albedo, metallic);
        
#if defined(FULL_MATERIAL)
        surface.SubsurfaceColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = make_float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = make_float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.FuzzWeight = 0.0f;
        surface.GlintScreenSpaceScale = 1.0f;
        surface.GlintLogMicrofacetDensity = 0.0f;
        surface.GlintMicrofacetRoughness = 0.0f;
        surface.GlintDensityRandomization = 0.0f;
        surface.Noise = 0.0f;
#endif        
        
        return surface;
    }    
    
    Surface(float3 position, float3 geomNormal, float3 normal, float3 tangent, float3 bitangent, float3 albedo, float roughness, float metallic, float3 emissive, float ao) {
        Surface surface;
    
        surface.Position = position;
        surface.GeomNormal = geomNormal;
        surface.Normal = normal;
        surface.Tangent = tangent;
        surface.Bitangent = bitangent;
        surface.Albedo = albedo;
        surface.Roughness = roughness;
        surface.Metallic = metallic;
        surface.Emissive = emissive;
        surface.AO = ao;        
        
        surface.F0 = PBR::F0(albedo, metallic);
        
#if defined(FULL_MATERIAL)
        surface.SubsurfaceColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = make_float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = make_float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.FuzzWeight = 0.0f;
        surface.GlintScreenSpaceScale = 1.0f;
        surface.GlintLogMicrofacetDensity = 0.0f;
        surface.GlintMicrofacetRoughness = 0.0f;
        surface.GlintDensityRandomization = 0.0f;
        surface.Noise = 0.0f;
#endif        
        
        return surface;
    }
#if defined(FULL_MATERIAL)    
    Surface(float3 position, float3 geomNormal, float3 normal, float3 tangent, float3 bitangent, float3 albedo, float roughness, float metallic, float3 emissive, float ao) {
        Surface surface;
    
        surface.Position = position;
        surface.GeomNormal = geomNormal;
        surface.Normal = normal;
        surface.Tangent = tangent;
        surface.Bitangent = bitangent;
        surface.Albedo = albedo;
        surface.Roughness = roughness;
        surface.Metallic = metallic;
        surface.Emissive = emissive;
        surface.AO = ao;
        surface.SubsurfaceColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = make_float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = make_float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = make_float3(0.0f, 0.0f, 0.0f);
        surface.FuzzWeight = 0.0f;
        surface.GlintScreenSpaceScale = 1.0f;
        surface.GlintLogMicrofacetDensity = 0.0f;
        surface.GlintMicrofacetRoughness = 0.0f;
        surface.GlintDensityRandomization = 0.0f;
        surface.Noise = 0.0f;
     
        return surface;
    }  
#endif    
};
#define Surface(...) Surface::ctor(__VA_ARGS__)

#endif // SURFACE_HLSL
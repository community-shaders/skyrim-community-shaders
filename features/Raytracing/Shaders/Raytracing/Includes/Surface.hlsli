#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/PBR.hlsli"
#include "Raytracing/Includes/MonteCarlo.hlsli"
#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#define Surface(...) static Surface ctor(__VA_ARGS__)
struct Surface
{   
    float3 Position;
    float3 GeomNormal;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float3 Albedo;
    float3 DiffuseAlbedo;
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

    float3 Mul(float3 tangentSample)
    {   
        return Tangent * tangentSample.x +
               Bitangent * tangentSample.y +
               Normal * tangentSample.z;
    }    
    
    Surface(float3 position, Payload payload, out Instance instance, out Material material)
    {
        Surface surface;

        surface.Position = position;
             
        uint shapeIndex = GetShapeIdx(payload, instance);

        // Loads all geometry releated data
        Vertex v0, v1, v2;
        GetVertices(shapeIndex, payload.primitiveIndex, v0, v1, v2);
        
        float3 uvw = GetBary(payload.Barycentrics());
        
        material = Materials[shapeIndex];

        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
        
        float3x3 objectToWorld3x3 = (float3x3) instance.Transform;
        
        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
        
        float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);

        Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture)];
        Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture)];        
        
        [branch]
        if (material.ShaderType == ShaderType::Effect)
        {
            float3 base = float3(1, 1, 1);
            
            if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
            {
                base *= baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
            }
            
            float3 baseColorMul = material.EffectColor.rgb;
            
            if (material.ShaderFlags & ShaderFlags::kVertexColors && !(material.ShaderFlags & ShaderFlags::kProjectedUV))
            {
                base *= vertexColor.rgb;
            }
            
            float3 baseColor = base * baseColorMul;
        
            float baseColorScale = material.EffectColor.a;
            
            if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
            {
                float2 grayscaleToColorUv = float2(base.g, baseColorMul.x);
        
                baseColor = baseColorScale * effectTexture.SampleLevel(BaseSampler, grayscaleToColorUv, 0).rgb;
            }
            
            float3 baseColorLinear = Color::GammaToTrueLinear(baseColor);
            
            surface.Albedo = baseColorLinear;
            surface.Emissive = baseColorLinear * Frame.Effect;
        }
        else
        {        
            float3 base = baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
            float3 effect = effectTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
        
            surface.Albedo = base * material.BaseColor.rgb * vertexColor.rgb;     
            surface.Emissive = effect * material.EffectColor.rgb * material.EffectColor.a;
        }
    
#ifdef DEBUG_WHITE_FURNACE
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
#endif        
        
        surface.GeomNormal = normalWS;
        
#ifdef PATH_TRACING        
        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
        Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture)];
        
        float handedness = (dot(cross(normalWS, tangentWS), tangentWS) < 0.0f) ? -1.0f : 1.0f;
        
        NormalMap(
            normalTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb,
            handedness,
            normalWS, tangentWS, bitangentWS, 
            surface.Normal, surface.Tangent, surface.Bitangent
        );
        
        float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord0, 0);

        surface.Roughness = saturate(rmaos.x * material.RoughnessScale);
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
#else 
        surface.Normal = normalWS;
        surface.Tangent = tangentWS;
        surface.Bitangent = bitangentWS;
        
        surface.Roughness = PBR::Defaults::Roughness * material.RoughnessScale;
        surface.Metallic = PBR::Defaults::Metallic;
        surface.AO = 1.0f;        
#endif  
        
        surface.Roughness = PBR::Roughness(surface.Roughness, Frame.Roughness.x, Frame.Roughness.y);
        surface.Metallic = Remap(surface.Metallic, Frame.Metalness.x, Frame.Metalness.y);

        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);
        
        surface.F0 = PBR::F0(material.SpecularLevel.xxx, surface.Albedo, surface.Metallic);

#if defined(FULL_MATERIAL)
        surface.SubsurfaceColor = float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = float3(0.0f, 0.0f, 0.0f);
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
        
#   ifdef DEBUG_WHITE_FURNACE
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
#   else
        surface.Albedo = albedo;
 #   endif
        
        surface.Roughness = PBR::Roughness(roughness, Frame.Roughness.x, Frame.Roughness.y);
        surface.Metallic = Remap(metallic, Frame.Metalness.x, Frame.Metalness.y);
        
        surface.Emissive = emissive;      
        surface.AO = ao;        
        
        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);
        
        surface.F0 = PBR::F0(albedo, metallic);

#if defined(FULL_MATERIAL)
        surface.SubsurfaceColor = float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = float3(0.0f, 0.0f, 0.0f);
        surface.FuzzWeight = 0.0f;
        surface.GlintScreenSpaceScale = 1.0f;
        surface.GlintLogMicrofacetDensity = 0.0f;
        surface.GlintMicrofacetRoughness = 0.0f;
        surface.GlintDensityRandomization = 0.0f;
        surface.Noise = 0.0f;
#endif        
        
        return surface;
    }    
};
#define Surface(...) Surface::ctor(__VA_ARGS__)

#define BRDFContext(...) static BRDFContext ctor(__VA_ARGS__)
struct BRDFContext {
    float3 ViewDirection;
    float NdotV;   
    
    BRDFContext(Surface surface, float3 viewDirection)
    {
        BRDFContext brdfContext;
        
        brdfContext.ViewDirection = viewDirection;
        brdfContext.NdotV = saturate(dot(surface.Normal, viewDirection));
        
        return brdfContext;
    }
};
#define BRDFContext(...) BRDFContext::ctor(__VA_ARGS__)

#endif // SURFACE_HLSL
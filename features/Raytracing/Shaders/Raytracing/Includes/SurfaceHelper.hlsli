#ifndef SURFACE_HELPER_HLSL
#define SURFACE_HELPER_HLSL

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/PBR.hlsli"
#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

void DefaultMaterial(in Vertex v0, in Vertex v1, in Vertex v2, float3 uvw, float3 normalWS, float3 tangentWS, float3 bitangentWS, in Material material, inout Surface surface)
{
    float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture())];
    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
    Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture())];
    
    float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);   
         
    [branch]
    if (material.ShaderType == ShaderType::Effect)
    {
        float3 base = float3(1, 1, 1);

        if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
        {
            base *= baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
        }

        float3 baseColorMul = material.EffectColor().rgb;

        if (material.ShaderFlags & ShaderFlags::kVertexColors && !(material.ShaderFlags & ShaderFlags::kProjectedUV))
        {
            base *= vertexColor.rgb;
        }

        float3 baseColor = base * baseColorMul;

        float baseColorScale = material.EffectColor().a;

        if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
        {
            float2 grayscaleToColorUv = float2(base.g, baseColorMul.x);

            baseColor = baseColorScale * effectTexture.SampleLevel(BaseSampler, grayscaleToColorUv, 0).rgb;
        }

        float3 baseColorLinear = Color::GammaToTrueLinear(baseColor);

        //surface.Albedo = baseColorLinear; // This breaks sharc
        surface.Albedo = 0;
        surface.Emissive = baseColorLinear * Frame.Effect;

        surface.Roughness = PBR::Defaults::Roughness;
        surface.Metallic = PBR::Defaults::Metallic;
        surface.AO = 1.0f;
        surface.F0 = 0.04f;
    }
    else
    {
        float3 base = baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
        float3 effect = effectTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;

        surface.Albedo = base * material.BaseColor().rgb * vertexColor.rgb;
        surface.Emissive = effect * material.EffectColor().rgb * material.EffectColor().a * Frame.Emissive;
    }
   
    float3 F0 = material.SpecularLevel().xxx;

    [branch]
    if (material.ShaderType == ShaderType::TruePBR)
    {
        Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture)];
        float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord0, 0);

        surface.Roughness = saturate(rmaos.x * material.RoughnessScale);
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
        F0 *= rmaos.w;
    } else if (material.ShaderType == ShaderType::Lighting) {
        surface.Albedo = Color::GammaToTrueLinear(surface.Albedo);
        F0 = 0.04f;

        if (material.ShaderFlags & ShaderFlags::kSpecular) {
            Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
            surface.Roughness = material.RoughnessScale() >= 0.0f ? saturate(material.RoughnessScale()) : 1.0f;
            surface.Metallic = PBR::Defaults::Metallic;
            surface.AO = 1.0f;
            float3 specularColor = specularTexture.SampleLevel(BaseSampler, texCoord0, 0).r * material.SpecularColor().rgb * material.SpecularColor().a;
            F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
        } else {
            surface.Roughness = PBR::Defaults::Roughness;
            surface.Metallic = PBR::Defaults::Metallic;
            surface.AO = 1.0f;
            F0 = PBR::Defaults::F0;
        }

        [branch]
        if (material.Feature & Feature::kEnvironmentMap || material.Feature & Feature::kEye) {
            //Texture2D envTexture = Textures[NonUniformResourceIndex(material.EnvTexture())];
            //float3 envColor = Color::GammaToTrueLinear(envTexture.SampleLevel(BaseSampler, texCoord0, 15).rgb);
            //surface.Albedo = lerp(surface.Albedo, envColor, envMask);
            
            Texture2D envMaskTexture = Textures[NonUniformResourceIndex(material.EnvMaskTexture())];          
            float envMask = envMaskTexture.SampleLevel(BaseSampler, texCoord0, 0).r;
            surface.Metallic = envMask;            
        }
    }    
    
#ifdef PATH_TRACING    
    float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

    NormalMap(
        normalTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb,
        handedness,
        normalWS, tangentWS, bitangentWS,
        surface.Normal, surface.Tangent, surface.Bitangent
    );    
#else
        surface.Normal = normalWS;
        surface.Tangent = tangentWS;
        surface.Bitangent = bitangentWS;

        surface.Roughness = PBR::Defaults::Roughness * material.RoughnessScale();
        surface.Metallic = PBR::Defaults::Metallic;
        surface.AO = 1.0f;
#endif  
}

void PBRMaterial(inout Surface surface, Material material)
{
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.Texture0)];
    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.Texture1)];
    Texture2D effectTexture = Textures[NonUniformResourceIndex(material.Texture2)];
}

void LandMaterial(in Vertex v0, in Vertex v1, in Vertex v2, float3 uvw, float3 normalWS, float3 tangentWS, float3 bitangentWS, in Material material, inout Surface surface)
{
    float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
    
    Texture2D baseTexture0 = Textures[NonUniformResourceIndex(material.Texture0)];
    Texture2D baseTexture1 = Textures[NonUniformResourceIndex(material.Texture1)];
    Texture2D baseTexture2 = Textures[NonUniformResourceIndex(material.Texture2)];
    Texture2D baseTexture3 = Textures[NonUniformResourceIndex(material.Texture3)];
    Texture2D baseTexture4 = Textures[NonUniformResourceIndex(material.Texture4)];
    
    Texture2D normalTexture0 = Textures[NonUniformResourceIndex(material.Texture5)];
    Texture2D normalTexture1 = Textures[NonUniformResourceIndex(material.Texture6)];
    Texture2D normalTexture2 = Textures[NonUniformResourceIndex(material.Texture7)];
    Texture2D normalTexture3 = Textures[NonUniformResourceIndex(material.Texture8)];
    Texture2D normalTexture4 = Textures[NonUniformResourceIndex(material.Texture9)];
    
    Texture2D overlayTexture = Textures[NonUniformResourceIndex(material.OverlayTexture())];
    Texture2D noiseTexture = Textures[NonUniformResourceIndex(material.NoiseTexture())];   
    
    float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);   
    
    half4 landBlend0 = Interpolate(v0.LandBlend0.unpack(), v1.LandBlend0.unpack(), v2.LandBlend0.unpack(), uvw);   
    half4 landBlend1 = Interpolate(v0.LandBlend1.unpack(), v1.LandBlend1.unpack(), v2.LandBlend1.unpack(), uvw);  
    
    surface.Albedo = baseTexture0.SampleLevel(BaseSampler, texCoord0, 0);
    surface.Emissive = 0;    
    
    surface.Normal = normalWS;
    surface.Tangent = tangentWS;
    surface.Bitangent = bitangentWS;

    surface.Roughness = PBR::Defaults::Roughness;
    surface.Metallic = PBR::Defaults::Metallic;
    surface.AO = 1.0f;    
}

#endif // SURFACE_HELPER_HLSL
#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/PBR.hlsli"
#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

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

    void DefaultMaterial(in Vertex v0, in Vertex v1, in Vertex v2, in float3 uvw, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Material material)
    {
        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

#if defined(DEBUG_SHADERTYPE)
        [branch]
        if (material.ShaderType == ShaderType::TruePBR) {
            Albedo = float3(1.0f, 0.0f, 0.0f);
        } else if (material.ShaderType == ShaderType::Lighting) {
            Albedo = float3(0.0f, 1.0f, 0.0f);
        } else if (material.ShaderType == ShaderType::Effect) {
            Albedo = float3(0.0f, 0.0f, 1.0f);
        } else {
            Albedo = float3(1.0f, 1.0f, 1.0f);
        }
#elif defined(DEBUG_NOSAMPLING)       
        Albedo = float3(0.5f, 0.5f, 0.5f);
#else
        Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture())];
        
        float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);
		vertexColor = saturate(vertexColor / max(max(vertexColor.r, vertexColor.g), vertexColor.b));
        
        [branch]
        if (material.ShaderType == ShaderType::TruePBR)
        {           
            Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture())];
            Texture2D emissiveTexture = Textures[NonUniformResourceIndex(material.EmissiveTexture())];
            
            float3 albedo = baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;
            float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord0, 0);
            float3 emissive = emissiveTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;

            Albedo = albedo * material.BaseColor().rgb * vertexColor.rgb;
            Emissive = emissive * material.EffectColor().rgb * material.EffectColor().a * Frame.Emissive;
            Roughness = saturate(rmaos.x * material.RoughnessScale());
            Metallic = saturate(rmaos.y);
            AO = rmaos.z;
            F0 = material.SpecularLevel() * rmaos.w;
        } else if (material.ShaderType == ShaderType::Lighting) {
            float3 diffuse = baseTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb;

            Albedo = Color::GammaToTrueLinear(diffuse * material.BaseColor().rgb * vertexColor.rgb);

            [branch]
            if (material.ShaderFlags & ShaderFlags::kSpecular) {
                Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
                Roughness = material.RoughnessScale() >= 0.0f ? saturate(material.RoughnessScale()) : 1.0f;

                float3 specularColor = specularTexture.SampleLevel(BaseSampler, texCoord0, 0).r * material.SpecularColor().rgb * material.SpecularColor().a;
                F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
            }

            [branch]
            if (material.ShaderFlags & ShaderFlags::kEnvMap || material.ShaderFlags & ShaderFlags::kEyeReflect) {
                Texture2D envTexture = Textures[NonUniformResourceIndex(material.EnvTexture())];
                Texture2D envMaskTexture = Textures[NonUniformResourceIndex(material.EnvMaskTexture())];

                float3 envColor = Color::GammaToTrueLinear(envTexture.SampleLevel(BaseSampler, texCoord0, 15).rgb);
                float envMask = envMaskTexture.SampleLevel(BaseSampler, texCoord0, 0).r;

                Albedo = lerp(Albedo, envColor, envMask);
                Metallic = envMask;
            }

            [branch]
            if (material.Feature == Feature::kGlowMap) {
                Texture2D glowTexture = Textures[NonUniformResourceIndex(material.GlowTexture())];
                Emissive = glowTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb * material.EffectColor().rgb * material.EffectColor().a * Frame.Emissive;
            }
        } else if (material.ShaderType == ShaderType::Effect) {
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
                Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture())];  
                
                float2 grayscaleToColorUv = float2(base.g, baseColorMul.x);

                baseColor = baseColorScale * effectTexture.SampleLevel(BaseSampler, grayscaleToColorUv, 0).rgb;
            }

            float3 baseColorLinear = Color::GammaToTrueLinear(baseColor);

            //Albedo = baseColorLinear; // This breaks sharc
            Albedo = 0;
            Emissive = baseColorLinear * Frame.Effect;
        }
        else
        {
            Albedo = float3(1.0f, 0.0f, 1.0f);
        }
#endif

#if defined(DEBUG_NONORMALMAP)
        Normal = normalWS;
        Tangent = tangentWS;
        Bitangent = bitangentWS;
#else
        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
        
        float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;
        
        NormalMap(
            normalTexture.SampleLevel(BaseSampler, texCoord0, 0).rgb,
            handedness,
            normalWS, tangentWS, bitangentWS,
            Normal, Tangent, Bitangent
        );
#endif       
    }

    float4 BlendLandTexture(uint16_t textureIndex, float2 texcoord, float weight)
    {
        if (weight > LAND_MIN_WEIGHT)
        {
            Texture2D texture = Textures[NonUniformResourceIndex(textureIndex)];
            return texture.SampleLevel(BaseSampler, texcoord, 0) * weight;
        }
        else
        {
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    void LandMaterial(in Vertex v0, in Vertex v1, in Vertex v2, float3 uvw, float3 normalWS, float3 tangentWS, float3 bitangentWS, in Material material)
    {
        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

        Texture2D overlayTexture = Textures[NonUniformResourceIndex(material.OverlayTexture())];
        Texture2D noiseTexture = Textures[NonUniformResourceIndex(material.NoiseTexture())];

        float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);

        float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

        float4 landBlend0 = Interpolate(v0.LandBlend0.unpack(), v1.LandBlend0.unpack(), v2.LandBlend0.unpack(), uvw);
        float4 landBlend1 = Interpolate(v0.LandBlend1.unpack(), v1.LandBlend1.unpack(), v2.LandBlend1.unpack(), uvw);

	    // Normalise blend weights
	    float totalWeight = landBlend0.x + landBlend0.y + landBlend0.z +
	                        landBlend0.w + landBlend1.x + landBlend1.y;

		landBlend0 /= totalWeight;
		landBlend1.xy /= totalWeight;

        float3 baseColor = BlendLandTexture(material.Texture0, texCoord0, landBlend0.x).rgb + BlendLandTexture(material.Texture1, texCoord0, landBlend0.y).rgb +
                           BlendLandTexture(material.Texture2, texCoord0, landBlend0.z).rgb + BlendLandTexture(material.Texture3, texCoord0, landBlend0.w).rgb +
                           BlendLandTexture(material.Texture4, texCoord0, landBlend1.x).rgb + BlendLandTexture(material.Texture5, texCoord0, landBlend1.y).rgb;

        baseColor *= vertexColor.rgb;

        [branch]
        if (material.ShaderType == ShaderType::TruePBR)
        {
            Albedo = baseColor;

            float4 rmaos = BlendLandTexture(material.Texture12, texCoord0, landBlend0.x) + BlendLandTexture(material.Texture13, texCoord0, landBlend0.y) +
                           BlendLandTexture(material.Texture14, texCoord0, landBlend0.z) + BlendLandTexture(material.Texture15, texCoord0, landBlend0.w) +
                           BlendLandTexture(material.Texture16, texCoord0, landBlend1.x) + BlendLandTexture(material.Texture17, texCoord0, landBlend1.y);

            Roughness = saturate(rmaos.x * 1.0f); // material.RoughnessScale()
            Metallic = saturate(rmaos.y);
            AO = rmaos.z;
            F0 = PBR::Defaults::F0 * rmaos.w; //material.SpecularLevel()
        }
        else if (material.ShaderType == ShaderType::Lighting)
        {
            Albedo = baseColor; // GammaToTrueLinear looks wonky
        }

        float3 normal = BlendLandTexture(material.Texture6, texCoord0, landBlend0.x).rgb + BlendLandTexture(material.Texture7, texCoord0, landBlend0.y).rgb +
                        BlendLandTexture(material.Texture8, texCoord0, landBlend0.z).rgb + BlendLandTexture(material.Texture9, texCoord0, landBlend0.w).rgb +
                        BlendLandTexture(material.Texture10, texCoord0, landBlend1.x).rgb + BlendLandTexture(material.Texture11, texCoord0, landBlend1.y).rgb;

        NormalMap(
            normal,
            handedness,
            normalWS, tangentWS, bitangentWS,
            Normal, Tangent, Bitangent
        );
    }


    void TestMaterial(in Vertex v0, in Vertex v1, in Vertex v2, in float3 uvw, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Material material)
    {
        Albedo = 0.5f;

        Normal = normalWS;
        Tangent = tangentWS;
        Bitangent = bitangentWS;
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

        surface.GeomNormal = normalWS;

        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.Roughness = PBR::Defaults::Roughness;
        surface.Metallic = PBR::Defaults::Metallic;
        surface.AO = 1.0f;
        surface.F0 = PBR::Defaults::F0;


#if defined(DEBUG_TESTMAT)
        surface.TestMaterial(v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, material);
#else
        if (material.Feature == Feature::kMultiTexLandLODBlend)
        {
#   if defined(DEBUG_LAND)
            surface.Albedo = float3(1.0f, 0.0f, 0.0f);
#   else
            surface.LandMaterial(v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, material);
#   endif
        }
        else
        {
            surface.DefaultMaterial(v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, material);
        }
#endif

#ifdef DEBUG_WHITE_FURNACE
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
#endif

        surface.Roughness = PBR::Roughness(surface.Roughness, Frame.Roughness.x, Frame.Roughness.y);
        surface.Metallic = Remap(surface.Metallic, Frame.Metalness.x, Frame.Metalness.y);

        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);

        surface.F0 = PBR::F0(surface.F0, surface.Albedo, surface.Metallic);

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

        surface.Emissive = emissive * Frame.Emissive;
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
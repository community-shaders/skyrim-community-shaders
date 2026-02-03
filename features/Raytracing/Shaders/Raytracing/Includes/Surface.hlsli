#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/ColorConversions.hlsli"
#include "Raytracing/Includes/PBR.hlsli"
#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

#include "Raytracing/Includes/Materials/TexLODHelpers.hlsli"

// Helpers

float3 SafeNormalize(float3 input)
{
    float lenSq = dot(input,input);
    return input * rsqrt(max( 1.175494351e-38, lenSq));
}

float3 FlipIfOpposite(float3 normal, float3 referenceNormal)
{
    return (dot(normal, referenceNormal)>=0)?(normal):(-normal);
}

#define Surface(...) static Surface ctor(__VA_ARGS__)
struct Surface
{
    float3 Position;
    float3 GeomNormal;
    float3 GeomTangent;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float3 FaceNormal;
    float3 Albedo;
    float3 DiffuseAlbedo;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float AO;
    float3 F0;
    float IOR;
    float3 TransmissionColor;

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

    float MipLevel;

    float3 Mul(float3 tangentSample)
    {
        return Tangent * tangentSample.x +
               Bitangent * tangentSample.y +
               Normal * tangentSample.z;
    }

    float3 ToLocal(float3 v)
    {
        return float3(
            dot(v, Tangent),
            dot(v, Bitangent),
            dot(v, Normal)
        );
    }

    float3 FromLocal(float3 v)
    {
        return Mul(v);
    }

    void FlipNormal()
    {
        Normal = -Normal;
        GeomNormal = -GeomNormal;
        FaceNormal = -FaceNormal;
    }

    void DefaultMaterial(in Vertex v0, in Vertex v1, in Vertex v2, in float3 uvw, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, float3x3 objectToWorld3x3, in Material material)
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

        const bool isWindows = (material.Feature == Feature::kGlowMap || material.PBRFlags & PBR::Flags::HasEmissive) && material.ShaderFlags & ShaderFlags::kAssumeShadowmask;
        float3 windowAlpha = float3(0.0f, 0.0f, 0.0f);

        [branch]
        if (material.ShaderType == ShaderType::TruePBR)
        {
            Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture())];
            Texture2D emissiveTexture = Textures[NonUniformResourceIndex(material.EmissiveTexture())];

            float3 albedo = baseTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;
            float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord0, MipLevel);
            float3 emissive = emissiveTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;

            if (isWindows) {
                windowAlpha = emissive;
            }

            Albedo = albedo * material.BaseColor().rgb * vertexColor.rgb;
            Emissive = emissive * EmitColorToLinear(material.EffectColor().rgb) * material.EffectColor().a * Frame.Emissive * EmitColorMult();
            Roughness = saturate(rmaos.x * material.RoughnessScale());
            Metallic = saturate(rmaos.y);
            AO = rmaos.z;
            F0 = material.SpecularLevel() * rmaos.w;
        } else if (material.ShaderType == ShaderType::Lighting) {
            float3 diffuse = baseTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;

            Albedo = VanillaDiffuseColor(diffuse * vertexColor.rgb);

            if (material.Feature == Feature::kHairTint) {
                float3 hairTint = material.BaseColor().rgb;
                Albedo *= VanillaDiffuseColor(hairTint);
            }

            [branch]
            if (material.ShaderFlags & ShaderFlags::kSpecular) {
                Roughness = material.RoughnessScale() >= 0.0f ? saturate(material.RoughnessScale()) : 1.0f;

                float3 specularColor = 0.0f;

                [branch]
                if (material.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
                    Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
                    specularColor = specularTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).r * material.SpecularColor().rgb * material.SpecularColor().a;
                } else {
                    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
                    specularColor = normalTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).a * material.SpecularColor().rgb * material.SpecularColor().a;
                }
                F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
            }

            [branch]
            if (material.ShaderFlags & ShaderFlags::kEnvMap || material.ShaderFlags & ShaderFlags::kEyeReflect) {
                Texture2D envTexture = Textures[NonUniformResourceIndex(material.EnvTexture())];
                Texture2D envMaskTexture = Textures[NonUniformResourceIndex(material.EnvMaskTexture())];

                float3 envColor = ColorToLinear(envTexture.SampleLevel(BaseSampler, texCoord0, 15).rgb);
                float envMask = envMaskTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).r;

                Albedo = lerp(Albedo, envColor, envMask);
                Metallic = envMask;
            }

            [branch]
            if (material.Feature == Feature::kGlowMap) {
                Texture2D glowTexture = Textures[NonUniformResourceIndex(material.GlowTexture())];
                float3 glow = glowTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;
                
                if (isWindows) {
                    windowAlpha = glow;
                }
                Emissive = GlowToLinear(glow) * EmitColorToLinear(material.EffectColor().rgb) * material.EffectColor().a * Frame.Emissive * EmitColorMult();
            }

            [branch]
            if (material.Feature == Feature::kFaceGen) {
                Texture2D detailTexture = Textures[NonUniformResourceIndex(material.DetailTexture())];
	            float3 detailColor = detailTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;
	            detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);

                Texture2D tintTexture = Textures[NonUniformResourceIndex(material.TintTexture())];
	            float3 tintColor = tintTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;
	            tintColor = tintColor * Albedo * 2.0f;
	            tintColor = tintColor - tintColor * Albedo;
	            Albedo = (Albedo * Albedo + tintColor) * detailColor;
                
            } else if (material.Feature == Feature::kFaceGenRGBTint) {
	            float3 tintColor = material.BaseColor().rgb * Albedo * 2.0f;
	            tintColor = tintColor - tintColor * Albedo;
	            Albedo = float3(1.01171875f, 0.99609375f, 1.01171875f) * (Albedo * Albedo + tintColor);
            }
            
        } else if (material.ShaderType == ShaderType::Effect) {
            float3 base = float3(1, 1, 1);

            [branch]
            if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
            {
                base *= baseTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).rgb;
            }

            float3 baseColorMul = material.EffectColor().rgb;

            [branch]
            if (material.ShaderFlags & ShaderFlags::kVertexColors && !(material.ShaderFlags & ShaderFlags::kProjectedUV))
            {
                base *= vertexColor.rgb;
            }

            float3 baseColor = base * baseColorMul;

            float baseColorScale = material.EffectColor().a;

            [branch]
            if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
            {
                Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture())];

                float2 grayscaleToColorUv = float2(base.g, baseColorMul.x);

                baseColor = baseColorScale * effectTexture.SampleLevel(BaseSampler, grayscaleToColorUv, MipLevel).rgb;
            }

            float3 baseColorLinear = EffectToLinear(baseColor);

            //Albedo = baseColorLinear; // This breaks sharc
            Albedo = 0;
            Emissive = baseColorLinear * Frame.Effect;
        }
        else
        {
            Albedo = float3(1.0f, 0.0f, 1.0f);
        }

        [branch]
        if (material.AlphaFlags == AlphaFlags::kAlphaBlend && !((material.Feature == Feature::kHairTint || material.Feature == Feature::kFaceGen || material.Feature == Feature::kFaceGenRGBTint || material.Feature == Feature::kEye))) {
            float alpha = baseTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).a * material.BaseColor().a;

            [branch]
            if (material.ShaderFlags & ShaderFlags::kVertexAlpha) {
                alpha *= vertexColor.a;
            }

            TransmissionColor = lerp(float3(1.0f, 1.0f, 1.0f), Albedo, alpha);
            Albedo *= alpha;
        } else {
            TransmissionColor = float3(0.0f, 0.0f, 0.0f);
        }

        [branch]
        if (isWindows) {
            TransmissionColor = windowAlpha;
            Albedo *= 1.0f - windowAlpha;
            Emissive *= 0;
            Roughness = max(Roughness, 0.08f); // prevent delta transmission
        }
        
        [branch]
        if (material.ShaderFlags & ShaderFlags::kExternalEmittance) {
            Emissive *= Frame.EmittanceColor;
        }
#endif

#if defined(DEBUG_NONORMALMAP)
        Normal = normalWS;
        Tangent = tangentWS;
        Bitangent = bitangentWS;
#else
        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
        float3 normal = normalTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).xyz;

        float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

        NormalMap(
            normal,
            handedness,
            normalWS, tangentWS, bitangentWS,
            Normal, Tangent, Bitangent
        );
#endif

        // Hair flowmap processing
        [branch]
        if (material.Feature == Feature::kHairTint && (material.ShaderFlags & ShaderFlags::kBackLighting)) {
            Texture2D hairFlowMapTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
            uint2 hairFlowDimensions;
            hairFlowMapTexture.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
            
            [branch]
            if (hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32) {
                float2 sampledHairFlow2D = hairFlowMapTexture.SampleLevel(BaseSampler, texCoord0, MipLevel).xy;
                
                [branch]
                if (sampledHairFlow2D.x > 0.0 || sampledHairFlow2D.y > 0.0) {
                    float3 sampledHairFlow = float3(sampledHairFlow2D * 2.0f - 1.0f, 0.0f);
                    float3x3 tbn = float3x3(Tangent, Bitangent, Normal);
                    float3 hairRootDirection = normalize(mul(sampledHairFlow, tbn));
                    
                    // Re-orthogonalize T and B to N and the new hair root direction
                    hairRootDirection = normalize(hairRootDirection - Normal * dot(hairRootDirection, Normal));
                    Bitangent = hairRootDirection;
                    
                    float hairHandedness = (dot(cross(Normal, Tangent), Bitangent) < 0.0f) ? -1.0f : 1.0f;
                    Tangent = normalize(cross(Bitangent, Normal)) * hairHandedness;
                }
            }
        }
    }

    float4 BlendLandTexture(uint16_t textureIndex, float2 texcoord, float weight)
    {
        if (weight > LAND_MIN_WEIGHT)
        {
            Texture2D texture = Textures[NonUniformResourceIndex(textureIndex)];
            return texture.SampleLevel(BaseSampler, texcoord, MipLevel) * weight;
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

#if defined(DEBUG_NONORMALMAP)
        Normal = normalWS;
        Tangent = tangentWS;
        Bitangent = bitangentWS;
#else          
        float3 normal = BlendLandTexture(material.Texture6, texCoord0, landBlend0.x).rgb + BlendLandTexture(material.Texture7, texCoord0, landBlend0.y).rgb +
                        BlendLandTexture(material.Texture8, texCoord0, landBlend0.z).rgb + BlendLandTexture(material.Texture9, texCoord0, landBlend0.w).rgb +
                        BlendLandTexture(material.Texture10, texCoord0, landBlend1.x).rgb + BlendLandTexture(material.Texture11, texCoord0, landBlend1.y).rgb;
        
        NormalMap(
            normal,
            handedness,
            normalWS, tangentWS, bitangentWS,
            Normal, Tangent, Bitangent
        );
#endif        
    }


    void TestMaterial(in Vertex v0, in Vertex v1, in Vertex v2, in float3 uvw, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Material material)
    {
        Albedo = 0.18f;  // Neutral grey
        TransmissionColor = float3(0.0f, 0.0f, 0.0f);

        Normal = normalWS;
        Tangent = tangentWS;
        Bitangent = bitangentWS;
    }

    float ComputeRayConeTriangleLODValue(in Vertex v0, in Vertex v1, in Vertex v2, float3x3 world)
    {
        float3 vertexPositions[3];
        vertexPositions[0] = v0.Position;
        vertexPositions[1] = v1.Position;
        vertexPositions[2] = v2.Position;

        float2 vertexTexcoords[3];
        vertexTexcoords[0] = v0.Texcoord0;
        vertexTexcoords[1] = v1.Texcoord0;
        vertexTexcoords[2] = v2.Texcoord0;

        return computeRayConeTriangleLODValue(
            vertexPositions,
            vertexTexcoords,
            world
        );
    }

    Surface(float3 position, Payload payload, float3 rayDir, RayCone rayCone, out Instance instance, out Material material)
    {
        Surface surface;

        surface.Position = position;

        Shape shape = GetShape(payload, instance);

        // Loads all geometry releated data
        Vertex v0, v1, v2;
        GetVertices(shape.GeometryIdx, payload.primitiveIndex, v0, v1, v2);

        float3 uvw = GetBary(payload.Barycentrics());

        material = shape.Material;

        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) shape.Transform);

        float coneTexLODValue = surface.ComputeRayConeTriangleLODValue(v0, v1, v2, objectToWorld3x3);

        float3 objectSpaceFlatNormal = SafeNormalize(cross(
            v1.Position - v0.Position,
            v2.Position - v0.Position));

        float3 normal0 = FlipIfOpposite(v0.Normal, objectSpaceFlatNormal);
        float3 normal1 = FlipIfOpposite(v1.Normal, objectSpaceFlatNormal);
        float3 normal2 = FlipIfOpposite(v2.Normal, objectSpaceFlatNormal);

        float3 normalWS = SafeNormalize(mul(objectToWorld3x3, Interpolate(normal0, normal1, normal2, uvw)));
        float3 tangentWS = SafeNormalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = SafeNormalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));

        surface.FaceNormal = SafeNormalize(mul(objectToWorld3x3, objectSpaceFlatNormal));

        surface.MipLevel = rayCone.computeLOD(coneTexLODValue, rayDir, normalWS, true) + Frame.TexLODBias;
        surface.GeomNormal = normalWS;
        surface.GeomTangent = tangentWS;

        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
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
            surface.DefaultMaterial(v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, objectToWorld3x3, material);
        }
#endif

#ifdef DEBUG_WHITE_FURNACE
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
#endif

        surface.Roughness = PBR::Roughness(surface.Roughness, Frame.Roughness.x, Frame.Roughness.y);
        surface.Metallic = Remap(surface.Metallic, Frame.Metalness.x, Frame.Metalness.y);

        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);

        surface.F0 = PBR::F0(surface.F0, surface.Albedo, surface.Metallic);
        surface.IOR = F0toIOR(surface.F0);

#   ifdef DEBUG_GLASS
        surface.TransmissionColor = 1.0f;
        surface.Albedo = float3(0.0f, 0.0f, 0.0f);
        surface.DiffuseAlbedo = float3(0.0f, 0.0f, 0.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.Metallic = 0.0f;
        surface.Roughness = 0.1f;
        surface.F0 = 0.04f;
        surface.IOR = 1.5f;
        surface.Normal = surface.GeomNormal;
        return surface;
#   endif

#   ifdef DEBUG_METAL
        surface.TransmissionColor = 0.0f;
        surface.Albedo = 0.18f;
        surface.DiffuseAlbedo = float3(0.0f, 0.0f, 0.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.Metallic = 1.0f;
        surface.Roughness = 0.1f;
        surface.F0 = 0.04f;
        surface.IOR = 1.5f;
        // surface.Normal = surface.GeomNormal;
        return surface;
#   endif

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

        surface.GeomTangent = tangent; // not needed for hybrid

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
        surface.IOR = F0toIOR(surface.F0);

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
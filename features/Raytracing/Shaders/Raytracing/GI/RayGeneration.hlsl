#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/RT/SHaRC.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/SHaRCHelper.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/ColorConversions.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/RT/SubsurfaceShading.hlsli"

#include "Common/Color.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Surface.hlsli"

#include "Raytracing/Includes/MonteCarlo.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

#include "Raytracing/Includes/Materials/BSDF.hlsli"
#include "Raytracing/Includes/Materials/TexLODHelpers.hlsli"

[shader("raygeneration")]
void main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

#if defined(CHECKERBOARD)    
    if ((idx.x + idx.y) & 1)
#elif defined(TEMPORAL_CHECKERBOARD)
    if ((idx.x + idx.y + Frame.FrameCount) & 1)
#endif
#if defined(CHECKERBOARD) || defined(TEMPORAL_CHECKERBOARD)
    {
        OutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        DiffuseAlbedoPathTracing[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        NormalRoughnessPathTracing[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = RAY_TMAX;
    
        return;
    }       
#endif   
    
    uint randomSeed = InitRandomSeed(idx, size, Frame.FrameCount);
    bool isSssPath = false;
  
#if defined(SHARC)
    SharcParameters sharcParameters = GetSharcParameters();

#    if defined(SHARC_UPDATE)
    [branch]
    if (Frame.SHaRC.UpdatePass)  {
        uint startIndex = Hash(idx) % 25;

        uint2 blockOrigin = idx * 5;

        uint pixelIndex = (startIndex + Frame.FrameCount) % 25;

        idx = blockOrigin + uint2(pixelIndex % 5, pixelIndex / 5);

        if (any(idx >= Frame.DispatchSize))
            return;

        size = Frame.DispatchSize;
    }
#   endif

#endif

#if defined(PATH_TRACING)
    const float2 uv = float2(idx + 0.5f) / size;
    
    float2 screenPos = uv * 2.0f - 1.0f;
    screenPos.y = -screenPos.y;

    const float4 clip = float4(screenPos, 1.0f, 1.0f);
    float4 view = mul(Frame.ProjInverse, clip);
    view /= view.w;

    float3 sourceDirection = normalize(mul((float3x3)Frame.ViewInverse, view.xyz));

    RayDesc sourceRay;
    sourceRay.Origin = Frame.Position.xyz;
    sourceRay.Direction = sourceDirection;
    sourceRay.TMin = 0.1f;
    sourceRay.TMax = 1e30;

    Payload sourcePayload;
    sourcePayload.hitDistance = -1.0f;
    sourcePayload.primitiveIndex = 0;
    sourcePayload.PackBarycentrics(float2(0.0f, 0.0f));
    sourcePayload.PackInstanceGeometryIndex(0, 0);
    sourcePayload.randomSeed = randomSeed;

    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, sourceRay, sourcePayload);
    randomSeed = sourcePayload.randomSeed;

    RayCone sourceRayCone = RayCone::make(Frame.PixelConeSpreadAngle * sourcePayload.hitDistance, Frame.PixelConeSpreadAngle);    

    if (!sourcePayload.Hit())
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        [branch]
        if (Frame.SHaRC.UpdatePass)
            return;
#endif

        const float4 mainColor = MainTexture.SampleLevel(BaseSampler, uv, 0);
    
        OutputTexture[idx] = float4(LLGammaToTrueLinear(mainColor.rgb), 0.0f);
        DiffuseAlbedoPathTracing[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        NormalRoughnessPathTracing[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = RAY_TMAX;
        return;
    }

    float3 sourcePosition = Frame.Position.xyz + sourceDirection * sourcePayload.hitDistance;

    Instance sourceInstance;
    Material sourceMaterial;

    Surface sourceSurface = Surface(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, -sourceDirection);
    if (dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) < 0.0f) sourceSurface.FlipNormal();

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, true);

    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);

    // Direct Light for PT
    float3 direct = sourceSurface.Emissive;
#ifdef SUBSURFACE_SCATTERING
    if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
        direct += EvaluateSubsurfaceNEE(sourceSurface, sourceBRDFContext, sourceMaterial, sourceInstance, sourcePayload, sourceRayCone, randomSeed);
        isSssPath = true;
    }
    else
#endif
        direct += EvaluateDirectRadiance(sourceMaterial, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed);
#else
    const float2 uv = float2(idx + 0.5f) / size;

    const float depth = DepthTexture.SampleLevel(BaseSampler, uv, 0) * 0.99998;

    const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    const float4 mainColor = MainTexture.SampleLevel(BaseSampler, uv, 0);
    
    [branch]
    if (depthView < FP_VIEW_Z || depth >= SKY_Z)
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        [branch]
        if (Frame.SHaRC.UpdatePass)
            return;
#endif

#if defined(RAW_RADIANCE)
        OutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        SpecularAlbedo[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
        OutputTexture[idx] = float4(LLGammaToTrueLinear(mainColor.rgb), mainColor.a);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = RAY_TMAX;
#endif
        return;
    }

    // Normal is pre-transformed into World-Space and Smoothness becomes Roughness when we copy the RT to DX12
    const snorm half4 normalRoughness = (half4) NormalRoughnessTexture[idx];

    // We should also scale the GBuffer for DLSSRR
    const unorm float linearRoughness = normalRoughness.w;

    const unorm float4 normalMetalnessAO = GNMAOTexture.SampleLevel(BaseSampler, uv, 0);

    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));
      
#if defined(DEBUG_GEOMNORMALOUT)
    OutputTexture[idx] = float4(geometryNormalWS * 0.5f + 0.5f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif
    
#if defined(DEBUG_DEPTHOUT)
    OutputTexture[idx] = float4(depth, 0.0f, 0.0f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif
    
#if defined(DEBUG_VIEWDEPTHOUT)
    OutputTexture[idx] = float4(depthView, 0.0f, 0.0f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif
    
    const float metalness = normalMetalnessAO.z;
    const float ao = 1.0f;

#if defined(DEBUG_ROUGHNESSOUT)
    OutputTexture[idx] = float4(linearRoughness, 0.0f, 0.0f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif    
 
#if defined(DEBUG_METALLICOUT)
    OutputTexture[idx] = float4(metalness, 0.0f, 0.0f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif        
    
#if defined(DEBUG_AOOUT)
    OutputTexture[idx] = float4(ao, 0.0f, 0.0f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif       
    
    const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
    const float3 positionWS = positionCS + Frame.Position.xyz;

    const float hitDistance = length(positionCS);
    
    const snorm half3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangentWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);

    float3 albedo = LLGammaToTrueLinear(AlbedoTexture.SampleLevel(BaseSampler, uv, 0).rgb);

    RayCone sourceRayCone = RayCone::make(Frame.PixelConeSpreadAngle * hitDistance, Frame.PixelConeSpreadAngle);
    
    Surface sourceSurface = Surface(positionWS, geometryNormalWS, normalWS, tangentWS, bitangentWS, albedo, linearRoughness, metalness, 0, ao);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, -positionCS / hitDistance);

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, true);
#endif

#if defined(DEBUG_MODELSPACE)
    [branch]
    if (sourceMaterial.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
        OutputTexture[idx] = float4(1.0f, 0.0f, 0.0f, 1.0f);
    } else {
        OutputTexture[idx] = float4(0.0f, 0.0f, 0.5f, 1.0f);
    }

    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif

#if defined(DEBUG_NORMALOUT) || defined(DEBUG_TANGENTOUT) || defined(DEBUG_BITANGENTOUT)
    
#if defined(DEBUG_NORMALOUT)
    float3 output = sourceSurface.Normal;
#elif defined(DEBUG_TANGENTOUT)
    float3 output = sourceSurface.Tangent;
#else
    float3 output = sourceSurface.Bitangent; 
#endif
    
    OutputTexture[idx] = float4(output * 0.5f + 0.5f, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif

#if defined(DEBUG_TRANSOUT)
    OutputTexture[idx] = float4(sourceSurface.TransmissionColor, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif

#if defined(DEBUG_MIPLEVEL)
    float3 output = TurboColormap(saturate(sourceSurface.MipLevel / 12.0f));
    OutputTexture[idx] = float4(output, 1.0f);
    SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
    SpecularHitDist[idx] = RAY_TMAX;
    return;
#endif

#if defined(SHARC) && defined(SHARC_DEBUG)
    HashGridParameters gridParameters = GetSharcGridParameters();

    OutputTexture[idx] = float4(HashGridDebugColoredHash(positionWS, geometryNormalWS, gridParameters), 1);
    return;
#endif

    float3 direction;
    MonteCarlo::BRDFWeight brdfWeight;

    float3 radiance = 0;
    bool isSpecular = false;
    float specHitDist = 0;

    RayDesc ray;
    Payload payload;

    Instance instance;
    Material material;

    Surface surface;
    BRDFContext brdfContext;

    StandardBSDF bsdf;
    
    RayCone rayCone;

#if defined(SHARC)
    SharcState sharcState;
    SharcHitData sharcHitData;
#endif

    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++)
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        [branch]
        if (Frame.SHaRC.UpdatePass)
        {
            SharcInit(sharcState);
        }
#endif

        surface = sourceSurface;
        brdfContext = sourceBRDFContext;
        bsdf = sourceBSDF;
        rayCone = sourceRayCone;        
#if defined(PATH_TRACING)
        material = sourceMaterial;
        instance = sourceInstance;
        payload = sourcePayload;
#endif

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
        float materialRoughnessPrev = 0.0f;
        bool isEnter = true;
        
#if defined(RAW_RADIANCE)
        float3 throughputDelta = float3(1.0f, 1.0f, 1.0f);
#endif

        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
            BSDFSample bsdfSample;
#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            float NdotD = saturate(dot(surface.Normal, direction));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
#else
            bool isValid = bsdf.SampleBSDF(brdfContext, material, surface, bsdfSample, randomSeed);
            isSpecular = bsdfSample.isLobe(LobeType::Specular);
            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);

            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;

            if (isValid)
                direction = bsdfSample.wo;
            else
                break;

            throughput *= bsdfSample.isLobe(LobeType::Transmission) ? 1.f : surface.AO;

            // Update isEnter state when transmission occurs
            if (hasTransmission) {
                isEnter = !isEnter;
            } else {
                isEnter = dot(direction, faceNormalOriented) >= 0.0f;
            }

            brdfWeight.diffuse = bsdfSample.isLobe(LobeType::DiffuseReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
#   if defined(RAW_RADIANCE)
            brdfWeight.diffuse /= max(surface.DiffuseAlbedo, 1e-4f);
#   endif
            brdfWeight.specular = bsdfSample.isLobe(LobeType::SpecularReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.transmission = bsdfSample.isLobe(LobeType::Transmission) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);

#   if defined(RAW_RADIANCE)
            float3 brdfWeightOriginal = brdfWeight.diffuse * surface.DiffuseAlbedo + brdfWeight.specular + brdfWeight.transmission;

#if defined(SHARC) && defined(SHARC_UPDATE)
            const bool sharcUpdatePass = Frame.SHaRC.UpdatePass;
#else
            const bool sharcUpdatePass = false;
#endif

            if (j > 0 || sharcUpdatePass) {
                throughput *= brdfWeightOriginal;
            } else {
                float3 brdfWeightRaw = bsdfSample.weight;

                throughputDelta = brdfWeightOriginal / brdfWeightRaw;

                throughput *= brdfWeightRaw;
            }
#   else
            throughput *= bsdfSample.weight;
#   endif
#endif

#if defined(SHARC) && defined(SHARC_UPDATE)
            [branch]
            if (Frame.SHaRC.UpdatePass)
            {
                SharcSetThroughput(sharcState, throughput);
            } else
#endif
            if (Frame.RussianRoulette)
            {
                float3 throughputColor;

#if defined(RAW_RADIANCE)
                throughputColor = throughput * throughputDelta;
#else
                throughputColor = throughput;
#endif
                const float rrVal = sqrt(Color::RGBToLuminance(throughputColor));
                float rrProb = saturate(0.85 - rrVal);
                rrProb *= rrProb;

                rrProb = saturate(rrProb + max(0, ((float)j / (float)MAX_BOUNCES - 0.4f)));

                if (Random(randomSeed) < rrProb)
                    break;

                throughput /= (1.0f - rrProb);
            }

#if defined(SHARC)
            materialRoughnessPrev += bsdfSample.isLobe(LobeType::Diffuse) ? 1.0f : surface.Roughness;
#endif

            ray.Origin = OffsetRay(surface.Position, faceNormalOriented, hasTransmission);
            ray.Direction = direction;
            ray.TMin = 0.0f;  // OffsetRay already handles precision, no additional offset needed
            ray.TMax = RAY_TMAX;

            payload.hitDistance = -1.0f;
            payload.primitiveIndex = 0;
            payload.PackBarycentrics(float2(0.0f, 0.0f));
            payload.PackInstanceGeometryIndex(0, 0);
            payload.randomSeed = randomSeed;

            if (!bsdfSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

            TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
            randomSeed = payload.randomSeed;
            rayCone = rayCone.propagateDistance(payload.hitDistance);

            if (isSpecular)
                specHitDist += payload.hitDistance;

            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(direction) * Frame.Sky;

#if defined(SHARC) && defined(SHARC_UPDATE)
                [branch]
                if (Frame.SHaRC.UpdatePass)
                {
                    SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
                    break;
                }
#endif

                sampleRadiance += skyIrradiance * throughput;
                break;
            }

            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = Surface(localPosition, payload, direction, rayCone, instance, material);

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = faceNormalOriented;

#   if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = surface.Emissive;
#   endif // SHARC_SEPARATE_EMISSIVE

            [branch]
            if (!Frame.SHaRC.UpdatePass)
            {
                uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.gridParameters);
                float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
                bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);

                if (isValidHit) {
                    materialRoughnessPrev = min(materialRoughnessPrev, 0.99f);
                    float a2 = materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev;
                    float footprint = payload.hitDistance * sqrt(0.5f * a2 / max(1.0f - a2, DIV_EPSILON));
                    isValidHit &= footprint > voxelSize;
                }

                float3 sharcRadiance;
                if (isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
                {
                    sampleRadiance += sharcRadiance * throughput;
                    break;
                }

            }
#endif

            brdfContext = BRDFContext(surface, -direction);
            if (dot(surface.FaceNormal, brdfContext.ViewDirection) < 0.0f) surface.FlipNormal();

            AdjustShadingNormal(surface, brdfContext, true, false);  // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
            bsdf = StandardBSDF::make(surface, isEnter);

            float3 directRadiance = 0.0f;
#ifdef SUBSURFACE_SCATTERING
            if (surface.SubsurfaceData.HasSubsurface != 0 && !isSssPath) {
                directRadiance += EvaluateSubsurfaceNEE(surface, brdfContext, material, instance, payload, rayCone, randomSeed);
                isSssPath = true;
            }
            else
#endif
                directRadiance += EvaluateDirectRadiance(material, surface, brdfContext, instance, bsdf, randomSeed);
            sampleRadiance += directRadiance * throughput;

#if defined(SHARC) && defined(SHARC_UPDATE)
            [branch]
            if (Frame.SHaRC.UpdatePass)
            {
                if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                    return;

                throughput = float3(1.0f, 1.0f, 1.0f);
            } else
#endif
            {
                sampleRadiance += surface.Emissive * throughput;
            }
        }

        radiance += sampleRadiance;

#if defined(SHARC) && defined(SHARC_UPDATE)
        // SHaRC is single sample only and does not write to texture outputs
        [branch]
        if (Frame.SHaRC.UpdatePass)
        {
            return;
        }
#endif
    }

    radiance /= MAX_SAMPLES;

    const float2 envBRDF = BRDF::EnvBRDFApproxHirvonen(sourceSurface.Roughness, sourceBRDFContext.NdotV);
    const float3 specularAlbedo = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);

#if defined(PATH_TRACING)
    OutputTexture[idx] = float4(direct + radiance, 0.0f);
    DiffuseAlbedoPathTracing[idx] = float4(sourceSurface.DiffuseAlbedo, 1.0f);
    NormalRoughnessPathTracing[idx] = float4(sourceSurface.Normal, sourceSurface.Roughness);
#else
#   if defined(RAW_RADIANCE)
    // Diffuse Output
    OutputTexture[idx] = float4(isSpecular ? 0.0f : radiance, 1.0f);

    // Specular Output (Reused texture from DLSS RR)
    SpecularAlbedo[idx] = float4(isSpecular ? radiance * specularAlbedo : 0.0f, specHitDist);
#   else
    OutputTexture[idx] = float4(LLGammaToTrueLinear(mainColor.rgb) + radiance, 1.0f);
#   endif
#endif

#if !defined(RAW_RADIANCE)
    SpecularAlbedo[idx] = float4(specularAlbedo, 0.0f);

    SpecularHitDist[idx] = specHitDist;
#endif
}

#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/RT/SHaRC.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/SHaRCHelper.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#include "Common/Color.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Surface.hlsli"

#include "Raytracing/Includes/MonteCarlo.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

[shader("raygeneration")]
void main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    uint randomSeed = InitRandomSeed(idx, size, Frame.FrameCount);

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
    float2 uv = ((float2(idx) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    uv.y = -uv.y;

    float4 clip = float4(uv, 1.0f, 1.0f);
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

    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, sourceRay, sourcePayload);

    if (!sourcePayload.Hit())
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        [branch]
        if (Frame.SHaRC.UpdatePass)
            return;
#endif

        float3 skyIrradiance = SampleSky(sourceDirection) * Frame.Sky;

        OutputTexture[idx] = float4(skyIrradiance, 0.0f);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = RAY_TMAX;
        return;
    }

    float3 sourcePosition = Frame.Position.xyz + sourceDirection * sourcePayload.hitDistance;

    Instance sourceInstance;
    Material sourceMaterial;

    Surface sourceSurface = Surface(sourcePosition, sourcePayload, sourceInstance, sourceMaterial);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, -sourceDirection);

    // Direct Light for PT
    float3 direct = EvaluateDirectRadiance(sourceSurface, sourceBRDFContext, sourceInstance, sourceMaterial, randomSeed) + sourceSurface.Emissive;
#else
    const float2 uv = float2(idx + 0.5f) / size;

    const unorm float4 normalMetalnessAO = GNMAOTexture.SampleLevel(BaseSampler, uv, 0);

    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));

    const float depth = DepthTexture.SampleLevel(BaseSampler, uv, 0) * 0.99998;

    const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    const float4 mainColor = MainTexture.SampleLevel(BaseSampler, uv, 0);

    [branch]
    if (depthView < FP_Z || depth >= SKY_Z)
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        [branch]
        if (Frame.SHaRC.UpdatePass)
            return;
#endif

        OutputTexture[idx] = float4(Color::GammaToTrueLinear(mainColor.rgb), mainColor.a);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = RAY_TMAX;
        return;
    }

    // Normal is pre-transformed into World-Space and Smoothness becomes Roughness when we copy the RT to DX12
    const snorm half4 normalRoughness = (half4) NormalRoughnessTexture[idx];

    // We should also scale the GBuffer for DLSSRR
    const unorm float linearRoughness = normalRoughness.w;

    // Metalness and AO packed in 16 bits
    float metalness, ao;
    UnpackMAO(normalMetalnessAO.z, metalness, ao);

    const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
    const float3 positionWS = positionCS + Frame.Position.xyz;

    const snorm half3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangetWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangetWS);

    float3 albedo = Color::GammaToTrueLinear(AlbedoTexture.SampleLevel(BaseSampler, uv, 0).rgb);

    Surface sourceSurface = Surface(positionWS, geometryNormalWS, normalWS, tangentWS, bitangetWS, albedo, linearRoughness, metalness, 0, ao);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, normalize(-positionCS));
#endif

#if defined(SHARC) && defined(SHARC_DEBUG)
    HashGridParameters gridParameters = GetSharcGridParameters();

    OutputTexture[idx] = float4(HashGridDebugColoredHash(positionWS, geometryNormalWS, gridParameters), 1);
    return;
#endif

    float3 direction;
    float3 brdfWeight;

    float3 radiance = 0;
    bool isSpecular = false;
    float specHitDist = 0;

    RayDesc ray;
    Payload payload;

    Instance instance;
    Material material;

    Surface surface;
    BRDFContext brdfContext;

#if defined(SHARC)
    SharcState sharcState;
    SharcHitData sharcHitData;
#endif

    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++)
    {
#if defined(SHARC)
        [branch]
        if (Frame.SHaRC.UpdatePass)
        {
            SharcInit(sharcState);
        }
#endif

        surface = sourceSurface;
        brdfContext = sourceBRDFContext;

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);

        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            float NdotD = saturate(dot(surface.Normal, direction));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
#else
#   if defined(FULL_MATERIAL)
            if ((material.PBRFlags & PBR::Flags::Fuzz) != 0)
                SampleFuzzBSDF(surface, brdfContext, randomSeed, direction, brdfWeight);
            else
#   endif
            isSpecular = SampleDefaultBSDF(surface, brdfContext, randomSeed, direction, brdfWeight);

            throughput *= surface.AO;
            throughput *= brdfWeight;
#endif
            if (dot(surface.GeomNormal, direction) <= 0.0)
                break;

#if defined(SHARC) && defined(SHARC_UPDATE)
            [branch]
            if (Frame.SHaRC.UpdatePass)
            {
                SharcSetThroughput(sharcState, throughput);
            } else
#endif
            if (Frame.RussianRoulette)
            {
                float rrProbability = j < RR_MIN_BOUNCE ? 1.0f : min(0.95f, Color::RGBToLuminance(throughput));

                if (rrProbability < Random(randomSeed))
                    break;
                else
                    throughput /= rrProbability;
            }

            ray.Origin = surface.Position + surface.GeomNormal * GN_BIAS;
            ray.Direction = direction;
            ray.TMin = 0.01f;
            ray.TMax = RAY_TMAX;

            payload.hitDistance = -1.0f;
            payload.primitiveIndex = 0;
            payload.PackBarycentrics(float2(0.0f, 0.0f));
            payload.PackInstanceGeometryIndex(0, 0);

            TraceRay(Scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);

            if (j == 0)
            {
                if (isSpecular)
                    specHitDist = max(specHitDist, payload.hitDistance);
            }

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

            surface = Surface(localPosition, payload, instance, material);

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = surface.GeomNormal;

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
                    float footprint = payload.hitDistance * sqrt(0.5f * surface.Roughness / max(1.0f - surface.Roughness, DIV_EPSILON));
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

            float3 directRadiance = EvaluateDirectRadiance(surface, brdfContext, instance, material, randomSeed);
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

#if defined(PATH_TRACING)
    OutputTexture[idx] = float4(direct + radiance, 0.0f);
#else
#   if defined(OUTPUT_RADIANCE)
    OutputTexture[idx] = float4(radiance, 1.0f);
#   else
    OutputTexture[idx] = float4(Color::GammaToTrueLinear(mainColor.rgb) + radiance, 1.0f);
#   endif
#endif

    const float2 envBRDF = BRDF::EnvBRDFApproxHirvonen(sourceSurface.Roughness, sourceBRDFContext.NdotV);
    const float3 specularAlbedo = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);
    SpecularAlbedo[idx] = float4(specularAlbedo, 0.0f);

    SpecularHitDist[idx] = specHitDist;
}

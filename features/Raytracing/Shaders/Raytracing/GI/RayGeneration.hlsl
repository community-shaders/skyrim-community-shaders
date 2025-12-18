#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Sharc.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#include "Common/Color.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/RT/SHaRCCommon.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

#include "Raytracing/Includes/BRDF.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

[shader("raygeneration")]
void main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

#if defined(SHARC) && defined(SHARC_UPDATE)
    if (Frame.SHaRCUpdatePass)  {
        uint startIndex = Hash(idx) % 25;

        uint2 blockOrigin = idx * 5;
    
        uint pixelIndex = (startIndex + Frame.FrameCount) % 25;
    
        idx = blockOrigin + uint2(pixelIndex % 5, pixelIndex / 5);
    
        if (any(idx >= Frame.DispatchSize))
            return;
    
        size = Frame.DispatchSize;
    }
#endif    
    
    float2 uv = (idx + 0.5f) / size;
    
    const unorm float4 normalMetalnessAO = GNMAOTexture[idx];
    
    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));
    
    const float depth = DepthTexture[idx] * 0.99998;

    const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        if (Frame.SHaRCUpdatePass)   
            return;
#endif
        
        OutputTexture[idx] = MainTexture[idx];
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = 0.0f;
        return;
    }

    // Normal is pre-transformed into World-Space and Smoothness becomes Roughness when we copy the RT to DX12
    const snorm half4 normalRoughness = (half4) NormalRoughnessTexture[idx];

    // We should also scale the GBuffer for DLSSRR
    const unorm float linearRoughness = normalRoughness.w;

    // Metalness and AO packed in 16 bits
    uint metalnessAO = normalMetalnessAO.z * 65535.0;
    
    const float metalness = (metalnessAO & 0xFF) / 255.0f;
    
    const float ao = saturate(((metalnessAO >> 8) & 0xFF) / 255.0f);
    
    const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
    const float3 positionWS = positionCS + Frame.Position.xyz;

    const snorm half3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangetWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangetWS);
    
    float3 albedo = Color::GammaToTrueLinear(AlbedoTexture[idx].rgb);
    
    Surface sourceSurface = Surface(positionWS, geometryNormalWS, normalWS, tangentWS, bitangetWS, albedo, linearRoughness, metalness, 0, ao);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, normalize(-positionCS));    
    
    uint randomSeed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);

#if defined(SHARC) && defined(SHARC_DEBUG)
    HashGridParameters gridParameters;
    gridParameters.cameraPosition = Frame.Position;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
    gridParameters.sceneScale = Frame.SHaRCScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;    

    OutputTexture[idx] = float4(HashGridDebugColoredHash(positionWS, geometryNormalWS, gridParameters), 1);
    return;
#endif
    
#if defined(SHARC)
    SharcState sharcState;
    SharcInit(sharcState); 
#   endif

    float3 direction;
    float3 BRDF_over_PDF;
    
    float3 radiance = 0;
    bool isDiffusePath = true;
    float hitDistance = 0;    
    
    [unroll]
    for (uint i = 0; i < SAMPLES; i++)
    {
        Surface surface = sourceSurface;
        BRDFContext brdfContext = sourceBRDFContext;
        
        float3 sampleRadiance = 0.0f;

        [unroll]
        for (uint j = 0; j < MAX_DEPTH; j++)
        {
#if defined(LAMBERT)
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));        
#else
            const bool isSpecular = BRDF::GGXBRDF(surface, brdfContext, randomSeed, direction, BRDF_over_PDF);
#endif            
            if (dot(surface.GeomNormal, direction) <= 0.0)
                break;
            
            RayDesc ray;
            ray.Origin = surface.Position + surface.GeomNormal * 0.01f;
            ray.Direction = direction;
            ray.TMin = 0.01f;
            ray.TMax = 1e30;
    
            Payload payload;
            payload.hitDistance = -1.0f;
            payload.primitiveIndex = 0;    
            payload.PackBarycentrics(float2(0.0f, 0.0f));            
            payload.PackInstanceShapeIndex(0, 0);

            TraceRay(Scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
            
            if (!payload.Hit())
            {
                float3 dir = normalize(direction);
                dir.z = max(dir.z, 0.0f);
    
                float r = sqrt(1.0f - dir.z);
                float phi = atan2(dir.y, dir.x);
    
                float2 disk = float2(r * cos(phi), r * sin(phi));
                float2 uv = disk * 0.5f + 0.5f;

                sampleRadiance += Color::GammaToTrueLinear(SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f).rgb) * Frame.Sky;
                break;
            }
            
            Instance instance;
            
            surface = Surface(direction * payload.hitDistance, payload, instance);
            brdfContext = BRDFContext(surface, -direction);

            // Local bounce radiance
            float3 localRadiance = surface.Emissive * Frame.Emissive;
            
            // Ideally we would call only one of these per bounce
#if defined(LAMBERT)
            localRadiance += LambertianDirectD(surface, Frame.Directional, randomSeed);
            localRadiance += LambertianDirectP(surface, instance.LightData, randomSeed);
#else
            localRadiance += GGXDirectD(surface, brdfContext, Frame.Directional, randomSeed);
            localRadiance += GGXDirectP(surface, brdfContext, instance.LightData, randomSeed);
#endif
            
            float3 diffuseAO = BRDF::DiffuseAO(surface.Albedo, surface.AO);
            
#if defined(LAMBERT)
            float3 diffuse = localRadiance.rgb * saturate(dot(n, direction)) * diffuseAO * Frame.Diffuse;
            
            sampleRadiance += surface.Albedo * diffuse;  
#else         
            float3 diffuse = isSpecular ? 0.0 : localRadiance.rgb * BRDF_over_PDF * diffuseAO * Frame.Diffuse;
            
            float3 specularAO = BRDF::SpecularAO(brdfContext.NdotV, surface.Roughness, surface.AO, surface.F0);
            float3 specular = isSpecular ? localRadiance.rgb * BRDF_over_PDF * (specularAO * Frame.Specular): 0.0;    
    
            sampleRadiance += surface.Albedo * diffuse + specular;       
 #endif
            
            if (j == 0)
            {
                isDiffusePath = !isSpecular;
                hitDistance = max(hitDistance, payload.hitDistance);
            }
        }
        
        radiance += sampleRadiance;    
    }

    OutputTexture[idx] = MainTexture[idx] + float4(Color::TrueLinearToGamma(albedo * radiance), 0.0f);

    float2 envBRDF = max(0.0f, BRDF::EnvBRDFApproxLazarov(linearRoughness, sourceBRDFContext.NdotV));
    SpecularAlbedo[idx] = float4(envBRDF.x * sourceSurface.F0 + envBRDF.y, 0.0f);
    
    SpecularHitDist[idx] = isDiffusePath ? 0.0f : max(0.0f, hitDistance);
}

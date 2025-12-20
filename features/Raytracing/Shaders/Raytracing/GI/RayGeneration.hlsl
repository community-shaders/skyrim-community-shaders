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

#include "Raytracing/Includes/BRDF.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

// Samples the sky hemisphere texture based on the given direction
// Output is in true linear space
float3 SampleSky(float3 dir)
{
    dir.z = max(dir.z, 0.0f);
    
    float r = sqrt(1.0f - dir.z);
    float phi = atan2(dir.y, dir.x);
    
    float2 disk = float2(r * cos(phi), r * sin(phi));
    float2 uv = disk * 0.5f + 0.5f;

    return Color::GammaToTrueLinear(SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f).rgb);
}

// Samples the direct radiance at the given surface point
float3 SampleRadiance(in Surface surface, in BRDFContext brdfContext, in Instance instance, in Material material, inout uint randomSeed)
{
    float3 radiance = surface.Emissive * Frame.Emissive;
    
    // Ideally we would call only one of these per bounce
#if defined(LAMBERT)
    radiance += LambertianDirectD(surface, Frame.Directional, randomSeed);
    radiance += LambertianDirectP(surface, instance.LightData, randomSeed);
#else
    radiance += GGXDirectD(surface, brdfContext, Frame.Directional, randomSeed);
    radiance += GGXDirectP(surface, brdfContext, instance.LightData, randomSeed);
#endif    
    
    return radiance;
}

[shader("raygeneration")]
void main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    uint randomSeed = InitRandomSeed(idx, size, Frame.FrameCount);    
    
#if defined(SHARC) 
    SharcParameters sharcParameters = GetSharcParameters();

#    if defined(SHARC_UPDATE)
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
    sourcePayload.PackInstanceShapeIndex(0, 0);
    
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, sourceRay, sourcePayload);
    
    if (!sourcePayload.Hit())
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        if (Frame.SHaRC.UpdatePass)   
            return;
#endif
    
        float3 skyIrradiance = SampleSky(sourceDirection) * Frame.Sky;

        OutputTexture[idx] = float4(skyIrradiance, 0.0f);
        SpecularAlbedo[idx] = float4(0.5f, 0.5f, 0.5f, 0.0f);
        SpecularHitDist[idx] = 0.0f;    
        return;
    }    
    
    float3 sourcePosition = Frame.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    Material sourceMaterial;
    
    Surface sourceSurface = Surface(sourcePosition, sourcePayload, sourceInstance, sourceMaterial);
    BRDFContext sourceBRDFContext = BRDFContext(sourceSurface, -sourceDirection);
    
    // Direct Light for PT
    float3 direct = sourceSurface.Albedo * SampleRadiance(sourceSurface, sourceBRDFContext, sourceInstance, sourceMaterial, randomSeed);
#else
    float2 uv = (idx + 0.5f) / size;
    
    const unorm float4 normalMetalnessAO = GNMAOTexture[idx];
    
    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));
    
    const float depth = DepthTexture[idx] * 0.99998;

    const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        if (Frame.SHaRC.UpdatePass)   
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
#endif

#if defined(SHARC) && defined(SHARC_DEBUG)
    HashGridParameters gridParameters;
    gridParameters.cameraPosition = Frame.Position;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
    gridParameters.sceneScale = Frame.SHaRCScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;    

    OutputTexture[idx] = float4(HashGridDebugColoredHash(positionWS, geometryNormalWS, gridParameters), 1);
    return;
#endif

    float3 direction;
    float3 BRDF_over_PDF;
    
    float3 radiance = 0;
    bool isDiffusePath = true;
    float hitDistance = 0;    
    
    [unroll]
    for (uint i = 0; i < SAMPLES; i++)
    {
#if defined(SHARC)
        SharcState sharcState;
        SharcInit(sharcState); 
#endif
        
        Surface surface = sourceSurface;
        BRDFContext brdfContext = sourceBRDFContext;

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);

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
                float3 skyIrradiance = SampleSky(direction) * Frame.Sky;
                
#if defined(SHARC) && defined(SHARC_UPDATE)
                if (Frame.SHaRC.UpdatePass)
                {
                    SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
                }
#endif                

                sampleRadiance += skyIrradiance * throughput;
                break;
            }                  
   
            if (j == 0)
            {
                isDiffusePath = !isSpecular;
                hitDistance = max(hitDistance, payload.hitDistance);
            }           
          
            float3 localPosition = ray.Origin + direction * payload.hitDistance;             
            
            Instance instance;
            Material material;
            
            surface = Surface(localPosition, payload, instance, material);
            
#if defined(SHARC)
            SharcHitData sharcHitData;
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = surface.GeomNormal;
            
            if (!Frame.SHaRC.UpdatePass)
            {
                uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.gridParameters);
                float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
                bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);
            
                if (isValidHit) {
                    float footprint = payload.hitDistance * sqrt(0.5f * surface.Roughness / (1.0f - surface.Roughness));
                    isValidHit &= footprint > voxelSize;      
                }
            
                float3 sharcRadiance;
                if (isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
                {            
                    sampleRadiance += sharcRadiance * throughput; // We probably have to apply BRDF here
                    break;
                }
            }
#endif 
            
            brdfContext = BRDFContext(surface, -direction);

            /*if (material.PBRFlags & PBR::Flags::Subsurface)
            {
                // Do something expensive
            }*/
            
            float3 localRadiance = SampleRadiance(surface, brdfContext, instance, material, randomSeed);

            float3 diffuseAO = BRDF::DiffuseAO(surface.Albedo, surface.AO);
            
#if defined(LAMBERT)
            float NdotD = saturate(dot(n, direction));
            float3 diffuse = localRadiance.rgb * NdotD * diffuseAO * Frame.Diffuse;
            
            sampleRadiance += surface.Albedo * diffuse * throughput; 
            
            throughput *= surface.Albedo;
#else                                
            float3 diffuse = isSpecular ? 0.0 : localRadiance.rgb * BRDF_over_PDF * diffuseAO * Frame.Diffuse;
            
            float3 specularAO = BRDF::SpecularAO(brdfContext.NdotV, surface.Roughness, surface.AO, surface.F0);
            float3 specular = isSpecular ? localRadiance.rgb * BRDF_over_PDF * (specularAO * Frame.Specular) : 0.0;

            sampleRadiance += (surface.Albedo * diffuse + specular) * throughput;
                
            throughput *= BRDF_over_PDF * (isSpecular ? 1.0 : surface.Albedo);
            
#endif          
       
#if defined(SHARC) && defined(SHARC_UPDATE)
            if (Frame.SHaRC.UpdatePass)
            {
                if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, sampleRadiance, Random(randomSeed)))
                    break;
            }
            
            SharcSetThroughput(sharcState, throughput);

            throughput = float3(0.0f, 0.0f, 0.0f);
#else                    
            float rrProbability = j < RR_MIN_BOUNCE ? 1.0f : min(0.95f, BRDF::CalcLuminance(throughput));
            
            if (Frame.RussianRoulette && rrProbability < Random(randomSeed))
                break;
            else
                throughput /= rrProbability;
            
            if (any(sampleRadiance < MIN_RADIANCE))
                break; // Ray was eaten by the surface :(
#endif            
        }
        
        radiance += sampleRadiance;
        
#if defined(SHARC) && defined(SHARC_UPDATE)
        // SHaRC is single sample only and does not write to texture outputs
        if (Frame.SHaRC.UpdatePass)
        {
            return;
        }
#endif            
    }

#if defined(PATH_TRACING)
    OutputTexture[idx] = float4(Color::TrueLinearToGamma(direct + radiance), 0.0f);
#else
    OutputTexture[idx] = MainTexture[idx] + float4(Color::TrueLinearToGamma(sourceSurface.Albedo * radiance), 0.0f);
#endif
    
    // Needs linear and PT doesn't have linear :(
    //float2 envBRDF = max(0.0f, BRDF::EnvBRDFApproxLazarov(linearRoughness, sourceBRDFContext.NdotV));
    //SpecularAlbedo[idx] = float4(envBRDF.x * sourceSurface.F0 + envBRDF.y, 0.0f);
    
    SpecularAlbedo[idx] = float4(BRDF::EnvBRDFApprox2(sourceSurface.F0, sourceSurface.Roughness, sourceBRDFContext.NdotV), 0.0f);

    SpecularHitDist[idx] = isDiffusePath ? 0.0f : max(0.0f, hitDistance);
}

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Sharc.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#include "Common/Color.hlsli"

[shader("raygeneration")]
void main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 uv = (idx + 0.5f) / size;
    
    const unorm float4 normalMetalnessAO = GNMAOTexture[idx];
    
    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));
    
    const float depth = DepthTexture[idx] * 0.99998;

    const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        OutputTexture[idx] = MainTexture[idx];
        ReflectanceTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        SpecularHitDist[idx] = 0.0f;
        return;
    }

    // Normal is pre-transformed into World-Space and Smoothness becomes Roughness when we copy the RT to DX12
    const snorm half4 normalRoughness = (half4) NormalRoughnessTexture[idx];

    // We should also scale the GBuffer for DLSSRR
    const unorm float perceptualRoughness = clamp(Scale01(normalRoughness.w, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS, MAX_ROUGHNESS);
    const unorm float roughness = perceptualRoughness * perceptualRoughness;

    // Metalness and AO packed in 16 bits
    uint metalnessAO = normalMetalnessAO.z * 65535.0;
    
    const float metalness = Scale01((metalnessAO & 0xFF) / 255.0f, Frame.Metalness.x, Frame.Metalness.y);
    
    const float ao = saturate(((metalnessAO >> 8) & 0xFF) / 255.0f);
    
    const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
    const float3 positionWS = positionCS + Frame.Position.xyz;

    const snorm half3 normalWS = normalRoughness.xyz;

    float3 albedo = Color::GammaToLinear(AlbedoTexture[idx].rgb);
    
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
    
#   if defined(SHARC)
    SharcParameters sharcParameters;
    {
        sharcParameters.gridParameters.cameraPosition = Frame.Position;
        sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
        sharcParameters.gridParameters.sceneScale = Frame.SHaRCScale;    
        sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

        sharcParameters.hashMapData.capacity = Frame.SHaRCCapacity;
        sharcParameters.hashMapData.hashEntriesBuffer = u_SharcHashEntriesBuffer;

#if !SHARC_ENABLE_64_BIT_ATOMICS
        sharcParameters.hashMapData.lockBuffer = u_HashCopyOffsetBuffer;
#endif // !SHARC_ENABLE_64_BIT_ATOMICS

        sharcParameters.radianceScale = 1e3f;
        sharcParameters.enableAntiFireflyFilter = false;   
    
        sharcParameters.accumulationBuffer = u_SharcAccumulationBuffer;
        sharcParameters.resolvedBuffer = u_SharcResolvedBuffer;
    }    
    
    SharcState sharcState;
    SharcInit(sharcState); 
#   endif

    float3 viewWS = normalize(-positionCS);

    float3 tangentWS, bitangetWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangetWS);
    float3x3 TBN = float3x3(tangentWS, bitangetWS, normalWS);

    float3 direction;
    float3 BRDF_over_PDF;
    
    float3 radiance = 0;
    bool isDiffusePath = true;
    float hitDistance = 0;    
    
    float3 f0 = F0(albedo, metalness);
    
    [unroll]
    for (uint i = 0; i < SAMPLES; i++)
    {
        float3 samplePosition = positionWS;
        float3 viewDirection = viewWS;
        float3 sampleAlbedo = albedo;
        float sampleRoughness = roughness;
        float sampleMetalness = metalness;
        float3 sampleF0 = f0;
        float3 sampleRadiance = 0.0f;
        float3x3 sampleTBN = TBN;
        float3 geomWorldNormal = geometryNormalWS;

        [unroll]
        for (uint j = 0; j < MAX_DEPTH; j++)
        {
            bool isSpecular = GGXBRDF(sampleTBN, viewDirection, sampleAlbedo, sampleRoughness, sampleMetalness, sampleF0, randomSeed, direction, BRDF_over_PDF);
            
            if (dot(geomWorldNormal, direction) <= 0.0)
                break;
            
            RayDesc ray;
            ray.Origin = samplePosition + geomWorldNormal * 0.01f;
            ray.Direction = direction;
            ray.TMin = 0.01f;
            ray.TMax = 1e30;
    
            Payload payload;
            payload.hitDistance = -1.0f;
            payload.instanceIndex = 0;
            payload.primitiveIndex = 0;
            payload.shapeIndex = 0;
            payload.barycentrics = 0;

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
            
            samplePosition += direction * payload.hitDistance;
            
            Instance instance = GetInstance(payload.instanceIndex);

            Vertex v0, v1, v2;
            GetVertices(payload, v0, v1, v2);

            float3 uvw = GetBary(payload.barycentrics);
    
            Material material = Materials[payload.shapeIndex];
            
            float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
            
            float3x3 objectToWorld3x3 = (float3x3) instance.Transform;
    
            geomWorldNormal = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
            float3 geomWorldTangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
            float3 geomWorldBitangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
            
            sampleTBN = float3x3(geomWorldTangent, geomWorldBitangent, geomWorldNormal);
    
            float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);
    
            float sampleRoughness = DEFAULT_ROUGHNESS;
            float sampleMetalness = DEFAULT_METALNESS;
            float sampleAO = 1.0f;
    
            Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture)];
            Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture)];
            
#ifdef PATH_TRACING     
            Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
            Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture)];
#endif            
            
            float3 base = baseTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
            float3 effect = effectTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
    
#ifdef PATH_TRACING
            float3 normal = normalTexture.SampleLevel(BaseSampler, texCoord, 0).rgb * 2.0f - 1.0f;  
            float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord, 0);
    
            // Normal mapping
            float tangentSign = (dot(cross(geomWorldNormal, geomWorldTangent), geomWorldBitangent) < 0.0f) ? -1.0f : 1.0f; 
    
            float3 worldNormal = normalize(mul(normal, TBN));  
            float3 worldTangent = normalize(geomWorldTangent - worldNormal * dot(geomWorldTangent, worldNormal)); 
            float3 worldBitangent = cross(worldNormal, worldTangent) * tangentSign;   
    
            // Normal mapped TBN
            TBN = float3x3(worldTangent, worldBitangent, worldNormal);
    
            // Roughness and Metalness from RMAOS
            sampleRoughness = saturate(rmaos.x * material.roughness);
            sampleMetalness = saturate(rmaos.y);
            sampleAO = rmaos.z;
#else
            float3 worldNormal = geomWorldNormal;
#endif

            viewDirection = normalize(-direction);

            const unorm float samplePerceptualRoughness = clamp(Scale01(sampleRoughness, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS, MAX_ROUGHNESS);
            sampleRoughness = samplePerceptualRoughness * samplePerceptualRoughness;
    
            sampleMetalness = Scale01(sampleMetalness, Frame.Metalness.x, Frame.Metalness.y);
            
            // Lighting/PBR
            sampleAlbedo = Color::GammaToLinear(base) * material.BaseColor.rgb * vertexColor.rgb;
            float3 sampleEmissive = Color::GammaToLinear(effect) * material.EffectColor.rgb * material.EffectColor.a;
            
            // Recalculate F0, it will be used by the next GGXBRDF call as well
            sampleF0 = F0(sampleAlbedo, sampleMetalness);
            
            float3 localRadiance = sampleEmissive * Frame.Emissive;
            
            // Ideally we would call only one of these per bounce
#if defined(LAMBERT)
            localRadiance += LambertianDirectD(samplePosition, worldNormal, sampleAlbedo, Frame.Directional, randomSeed);
            localRadiance += LambertianDirectP(samplePosition, worldNormal, sampleAlbedo, instance.LightData, randomSeed);
#else
            localRadiance += GGXDirectD(samplePosition, worldNormal, viewDirection, sampleAlbedo, sampleRoughness, sampleMetalness, Frame.Directional, randomSeed);
            localRadiance += GGXDirectP(samplePosition, worldNormal, viewDirection, sampleAlbedo, sampleRoughness, sampleMetalness, instance.LightData, randomSeed);
#endif
            
            float NoV = saturate(dot(worldNormal, viewDirection));
            
            float3 diffuse = isSpecular ? 0.0 : localRadiance.rgb * BRDF_over_PDF * (DiffuseAO(sampleAlbedo, sampleAO) * Frame.Diffuse);
            float3 specular = isSpecular ? localRadiance.rgb * BRDF_over_PDF * (SpecularAO(NoV, sampleRoughness, sampleAO, sampleF0) * Frame.Specular): 0.0;    
    
            sampleRadiance += diffuse * sampleAlbedo + specular;       
    
            if (j == 0)
            {
                isDiffusePath = !isSpecular;
                hitDistance = max(hitDistance, payload.hitDistance);
            }
        }
        
        radiance += sampleRadiance;    
    }

    OutputTexture[idx] = MainTexture[idx] + float4(Color::TrueLinearToGamma(albedo * radiance), 0.0f);
    ReflectanceTexture[idx] = float4(EnvBRDFApprox2(f0, roughness, dot(normalWS, viewWS)), 0.0f);
    SpecularHitDist[idx] = isDiffusePath ? 0.0f : max(0.0f, hitDistance);
}

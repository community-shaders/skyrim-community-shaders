#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Sharc.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"

#include "Common/Color.hlsli"

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
    
    const half3 geometryNormalVS = DecodeNormal((half2) normalMetalnessAO.xy);
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
    
#if defined(SHARC)
    SharcState sharcState;
    SharcInit(sharcState); 
#endif
    
    // Let's raytrace straight from GBuffer, we save one ray per pixel
#if defined(LAMBERT)
    float4 result = LambertianIndirect(positionWS, normalWS, albedo, 0, randomSeed);  
#else
    float3 viewWS = normalize(-positionCS);

    float3 tangentWS, bitangetWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangetWS);
    float3x3 TBN = float3x3(tangentWS, bitangetWS, normalWS);
    
    float4 result = GGXIndirect(positionWS, geometryNormalWS, TBN, viewWS, albedo, roughness, metalness, ao, 0, randomSeed);
#endif

#if defined(SHARC) && defined(SHARC_UPDATE)
    if (Frame.SHaRCUpdatePass)
        return;
#endif
    
    OutputTexture[idx] = MainTexture[idx] + float4(Color::TrueLinearToGamma(result.rgb), 0.0f);
    
#if !defined(LAMBERT)
    ReflectanceTexture[idx] = float4(EnvBRDFApprox2(F0(albedo, metalness), roughness, dot(normalWS, viewWS)), 0.0f);
    SpecularHitDist[idx] = max(0.0f, result.a);
#endif    
}

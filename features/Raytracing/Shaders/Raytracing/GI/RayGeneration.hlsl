#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"

#ifdef SHARC
#include "Raytracing/Includes/RT/SHARC/SharcCommon.hlsli"
#endif

#include "Common/Color.hlsli"

[shader("raygeneration")]
void main()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 uv = (idx + 0.5f) / size;
    
    const half4 normalMetalness = GNMXTexture[idx];  
    
 	const half3 geometryNormalVS = DecodeNormal(normalMetalness.xy);
	const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));	      

    const float metalness = Scale01(normalMetalness.z, Frame.Metalness.x, Frame.Metalness.y);
    
	const float depth = DepthTexture[idx];  

	const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        OutputTexture[idx] = MainTexture[idx];
        ReflectanceTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        SpecularHitDist[idx] = 0.0f;
        return;
    }

	const snorm half4 normalRoughness = (half4)NormalRoughnessTexture[idx];

    const unorm float perceptualRoughness = clamp(Scale01(normalRoughness.w, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS, MAX_ROUGHNESS);
    const unorm float roughness = perceptualRoughness * perceptualRoughness;    

 	const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;

	const snorm half3 normalWS = normalRoughness.xyz;

    float3 albedo = Color::GammaToLinear(AlbedoTexture[idx].rgb);
    
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);

    // Let's raytrace straight from GBuffer, we save one ray per pixel
    #if defined(LAMBERT)
    OutputTexture[idx] = float4(LambertianIndirect(positionWS, normalWS, albedo, 0, seed), 0.0f);
    #else
    float3 viewWS = normalize(-positionCS);

    float3 tangentWS, bitangetWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangetWS);
    float3x3 TBN = float3x3(tangentWS, bitangetWS, normalWS);
    
    float4 result = GGXIndirect(positionWS, geometryNormalWS, TBN, viewWS, albedo, roughness, metalness, 0, seed);

    OutputTexture[idx] = MainTexture[idx] + float4(result.rgb, 0.0f);
    
    float3 h_tan = GGXSample(seed, roughness);
    float3 h = normalize(mul(TBN, h_tan));
    float VdotH = max(dot(viewWS, h), EPSILON_DOT_CLAMP);
    
    ReflectanceTexture[idx] = float4(saturate(F_Schlick(VdotH, F0(albedo, metalness))), 0.0f);
    
    SpecularHitDist[idx] = result.a;
    #endif
    
    //SpecularOutputTexture[idx] = TraceRaySpecular(Scene, positionWS, reflectWS, 0, seed, Frame.Specular, roughness);
    
    /*HashGridParameters gridParameters;
    gridParameters.cameraPosition = Frame.Position;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
    gridParameters.sceneScale = Frame.SHARCScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;    
    
    float3 color = HashGridDebugColoredHash(positionWS, meshNormalWS, gridParameters);
    
    Output[idx] = float4(color, 1);*/ 
}

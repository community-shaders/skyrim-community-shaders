#include "RaytracedGI/Includes/Types.hlsli"
#include "RaytracedGI/Includes/Registers.hlsli"

#include "RaytracedGI/Includes/Common.hlsli"
#include "RaytracedGI/Includes/RT/CommonRT.hlsli"
#include "RaytracedGI/Includes/RT/Rays.hlsli"
#include "RaytracedGI/Includes/RT/Shading.hlsli"

#ifdef SHARC
#include "RaytracedGI/Includes/RT/SHARC/SharcCommon.hlsli"
#endif

#include "Common/Color.hlsli"

#define FP_Z (16.5)

[shader("raygeneration")]
void main()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 uv = (idx + 0.5f) / size;
    
    const float4 normalMetalnessDepth = NormalMetalnessDepthTexture[idx];  
    
 	const half3 geometryNormalVS = DecodeNormal(normalMetalnessDepth.xy);
	const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Frame.ViewInverse));	      

    const float metalness = normalMetalnessDepth.z;
    
	const unorm float depth = normalMetalnessDepth.w * 0.99920h;  

	const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= 0.9999f)
    {
        DiffuseOutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularOutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        return;
    }

	const snorm float4 normalRoughness = NormalRoughnessTexture[idx];
	const unorm float roughness = Scale01(normalRoughness.w, Frame.Roughness.x, Frame.Roughness.y);    
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;

	const snorm float3 normalWS = normalize(normalRoughness.xyz);

    float3 albedo = Color::GammaToLinear(AlbedoTexture[idx].rgb);
    
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);

    /*float3 specularLobeWeight = ReflectanceTexture[idx].rgb;
    float3 diffuseLobeWeight = (1.0f - specularLobeWeight) * (1.0f - metalness);
    
    float probDiffuse = probabilityToSampleDiffuse(diffuseLobeWeight, specularLobeWeight);
    float chooseDiffuse = (Random(seed) < probDiffuse);   
    
    if (chooseDiffuse)
    {
        
    }
    else
    {
    
    }*/
    
    // Let's raytrace straight from GBuffer, we save one ray per pixel
    #if defined(LAMBERT)
    DiffuseOutputTexture[idx] = float4(LambertianIndirect(positionWS, normalWS, albedo, 0, seed), 0.0f);
    #else
    float3 viewWS = normalize(Frame.Position.xyz - positionWS);
    DiffuseOutputTexture[idx] = float4(GGXIndirect(positionWS, geometryNormalWS, normalWS, viewWS, albedo, DEFAULT_SPECULAR, roughness, 0, seed), 0.0f);
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

#include "RaytracedGI/Includes/Types.hlsli"
#include "RaytracedGI/Includes/Common.hlsli"

#ifdef SHARC
#include "RaytracedGI/Includes/RT/SHARC/SharcCommon.hlsli"
#endif

#include "RaytracedGI/Includes/Registers.hlsli"

#define FP_Z (16.5)

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const float4 ndcToView)
{
	float3 ret;
	ret.xy = (ndcToView.xy * screenPos.xy + ndcToView.zw) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

float ScreenToViewDepth(const float screenDepth)
{
	return (Frame.CameraData.w / (-screenDepth * Frame.CameraData.z + Frame.CameraData.x));
}

half3 DecodeNormal(half2 f)
{
	f = f * 2.0 - 1.0;
	// https://twitter.com/Stubbesaurus/status/937994790553227264
	half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	half t = saturate(-n.z);
	n.xy += select(n.xy >= 0.0, -t, t);
	return -normalize(n);
}

[shader("raygeneration")]
void main()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 uv = (idx + 0.5f) / size;
    
    const unorm float4 geometryNormalDepth = GeometryNormalDepthTexture[idx];  
    const unorm float3 meshNormalWS = normalize(geometryNormalDepth.xyz);
    
	const unorm float depth = geometryNormalDepth.w;   
	const unorm float depthLinear = ScreenToViewDepth(depth);

    if (depthLinear < FP_Z || depth >= 0.9999f)
    {
        DiffuseOutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularOutputTexture[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        return;
    }
    
	const unorm float3 normalRoughness = NormalRoughnessTexture[idx];
	const unorm float roughness = normalRoughness.z;    
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthLinear, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;
	
	const half3 normalVS = DecodeNormal(normalRoughness.xy);
	const float3 normalWS = normalize(ViewToWorldVector(normalVS, Frame.ViewInverse));	   

 	const float3 invViewWS = normalize(positionCS);
	const float3 reflectWS = reflect(invViewWS, normalWS);
    
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);
    
    // Prevents Z-fighting caused by far depth precision
    float3 origin = positionWS + meshNormalWS * depthLinear * 0.0001f;
    
    // Let's raytrace straight from GBuffer, we save one ray per pixel
    DiffuseOutputTexture[idx] = float4(TraceRayDiffuse(Scene, origin, normalWS, 0, seed, Frame.Diffuse), 1);
    SpecularOutputTexture[idx] = TraceRaySpecular(Scene, origin, reflectWS, 0, seed, Frame.Specular, roughness);
    
    //NormalRoughness
    
    /*HashGridParameters gridParameters;
    gridParameters.cameraPosition = Frame.Position;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
    gridParameters.sceneScale = Frame.SHARCScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;    
    
    float3 color = HashGridDebugColoredHash(positionWS, meshNormalWS, gridParameters);
    
    Output[idx] = float4(color, 1);*/
    
    
}

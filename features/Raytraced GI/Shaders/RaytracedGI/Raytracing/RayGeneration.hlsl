#include "RaytracedGI/Raytracing/Types.hlsli"
#include "RaytracedGI/Raytracing/Common.hlsli"
#include "RaytracedGI/Raytracing/SHARC/SharcCommon.hlsli"

ConstantBuffer<FrameData> Frame                 : register(b0);

Texture2D<unorm float3> NormalRoughnessTexture  : register(t0);
Texture2D<float4> MeshNormalDepthTexture        : register(t1);

RaytracingAccelerationStructure Scene   : register(t2);

RWStructuredBuffer<uint64_t>                u_SharcHashEntriesBuffer    : register(u0, space3);
RWStructuredBuffer<SharcAccumulationData>   u_SharcAccumulationBuffer   : register(u1, space3);
RWStructuredBuffer<SharcPackedData>         u_SharcResolvedBuffer       : register(u2, space3);

RWTexture2D<float4> Output : register(u0);

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
    
    const unorm float4 meshNormalDepth = MeshNormalDepthTexture[idx];
    
    const unorm float3 meshNormalWS = normalize(meshNormalDepth.xyz);
    
	const unorm float depth = meshNormalDepth.w;
	const unorm float depthLinear = ScreenToViewDepth(depth);

	const unorm float3 normalRoughness = NormalRoughnessTexture[idx];
	const unorm float roughness = normalRoughness.z;    
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthLinear, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;
	
	const half3 normalVS = DecodeNormal(normalRoughness.xy);
	const float3 normalWS = normalize(ViewToWorldVector(normalVS, Frame.ViewInverse));	   

    float3 origin = Frame.Position.xyz;
    
    float4 clip = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    clip.y = -clip.y;
    
    float4 view = mul(Frame.ProjInverse, clip);
    view /= view.w;

    float3 direction = normalize(mul((float3x3)Frame.ViewInverse, view.xyz));   

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.1f;
    ray.TMax = 1e30;
    
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);
    
    Payload payload;
    payload.data = PayloadData::Create(false, 0, seed);

    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    Output[idx] = float4(payload.color, 1);
    
    //NormalRoughness
    
    /*HashGridParameters gridParameters;
    gridParameters.cameraPosition = Frame.Position;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
    gridParameters.sceneScale = Frame.SHARCScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;    
    
    float3 color = HashGridDebugColoredHash(positionWS, meshNormalWS, gridParameters);
    
    Output[idx] = float4(color, 1);*/
    
    
}

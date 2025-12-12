#ifndef COMMON_HLSI
#define COMMON_HLSI

#define DEPTH_SCALE (0.99920h)
#define FP_Z (16.5f)
#define SKY_Z (0.9999f)
#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

float ScreenToViewDepth(const float screenDepth, float4 cameraData)
{
	return (cameraData.w / (-screenDepth * cameraData.z + cameraData.x));
}

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

float Scale01(float x, float min, float max)
{
    return clamp(min + saturate(x) * (max - min), min, max);
}

half3 DecodeNormal(half2 f)
{
	f = f * 2.0 - 1.0;
	// https://twitter.com/Stubbesaurus/status/937994790553227264
	half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	half t = saturate(-n.z);
	#if !defined(DX11)
	n.xy += select(n.xy >= 0.0, -t, t);	
	#else
	n.xy += n.xy >= 0.0 ? -t : t;	
	#endif
	return -normalize(n);
}

#endif
// Resolves NRD REBLUR_DIFFUSE output (YCoCg + normHitDist) back to linear RGB.
// Preserves AO from original SSRT output.

#include "NRD/NRDReblurSH.hlsli"
#include "SSRT/common.hlsli"

Texture2D<float4> srcNRDDiffuse : register(t0);

RWTexture2D<float4> outGIOcclusion : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 pxCoord = DTid.xy;
	if (any(pxCoord >= (uint2)FrameDim))
		return;

	float3 radiance;
	float normHitDist;
	REBLUR_BackEnd_UnpackRadianceAndNormHitDist(srcNRDDiffuse[pxCoord], radiance, normHitDist);

	float ao = outGIOcclusion[pxCoord].a;
	outGIOcclusion[pxCoord] = float4(radiance, ao);
}

// Resolves NRD REBLUR_DIFFUSE_SH output back to usable GI color.
// Reads denoised SH0/SH1, resolves against surface normal, writes to GIOcclusion.

#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"
#include "NRD/NRDReblurSH.hlsli"
#include "SSRT/common.hlsli"

Texture2D<float4> srcSH0 : register(t0);
Texture2D<float4> srcSH1 : register(t1);
Texture2D<float2> srcNormal : register(t2);
Texture2D<float> srcDepth : register(t3);

RWTexture2D<float4> outGIOcclusion : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 pxCoord = DTid.xy;
	if (any(pxCoord >= (uint2)FrameDim))
		return;

	float4 sh0 = srcSH0[pxCoord];
	float4 sh1 = srcSH1[pxCoord];

	NRD_SG sg = REBLUR_BackEnd_UnpackSh(sh0, sh1);

	float3 normalVS = GBuffer::DecodeNormal(srcNormal[pxCoord]);

	float2 uv = (pxCoord + 0.5) * RcpFrameDim;
	float depth = srcDepth.SampleLevel(samplerPointClamp, uv * (FrameDim * RcpTexDim), 0);
	float3 posVS;
	posVS.xy = (NDCToViewMul.xy * uv * (FrameDim * RcpTexDim) + NDCToViewAdd.xy) * depth;
	posVS.z = depth;
	float3 viewDir = normalize(-posVS);

	float3 resolvedGI = NRD_SG_ResolveDiffuse(sg, normalVS, viewDir, 1.0);

	// Preserve AO from original output
	float ao = outGIOcclusion[pxCoord].a;
	outGIOcclusion[pxCoord] = float4(resolvedGI, ao);
}

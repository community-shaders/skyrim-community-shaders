#include "Common/GBuffer.hlsli"
#include "SSRT/common.hlsli"

Texture2D<float4> srcNormal : register(t0);

RWTexture2D<float2> outNormals0 : register(u0);
RWTexture2D<float2> outNormals1 : register(u1);
RWTexture2D<float2> outNormals2 : register(u2);
RWTexture2D<float2> outNormals3 : register(u3);
RWTexture2D<float2> outNormals4 : register(u4);

float2 NormalsMIPFilter(float2 enc0, float2 enc1, float2 enc2, float2 enc3)
{
	float3 avg = GBuffer::DecodeNormal(enc0) + GBuffer::DecodeNormal(enc1) + GBuffer::DecodeNormal(enc2) + GBuffer::DecodeNormal(enc3);
	return GBuffer::EncodeNormal(normalize(avg));
}

groupshared float2 g_scratchNormals[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 r0 = srcNormal.GatherRed(samplerPointClamp, uv * frameScale);
	float4 g0 = srcNormal.GatherGreen(samplerPointClamp, uv * frameScale);

	float2 normals0 = float2(r0.w, g0.w);
	float2 normals1 = float2(r0.z, g0.z);
	float2 normals2 = float2(r0.x, g0.x);
	float2 normals3 = float2(r0.y, g0.y);

	outNormals0[pixCoord + uint2(0, 0)] = normals0;
	outNormals0[pixCoord + uint2(1, 0)] = normals1;
	outNormals0[pixCoord + uint2(0, 1)] = normals2;
	outNormals0[pixCoord + uint2(1, 1)] = normals3;

	// MIP 1
	float2 nm1 = NormalsMIPFilter(normals0, normals1, normals2, normals3);
	outNormals1[baseCoord] = nm1;
	g_scratchNormals[groupThreadID.x][groupThreadID.y] = nm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float2 inTL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormals[groupThreadID.x + 1][groupThreadID.y + 0];
		float2 inBL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 1];
		float2 inBR = g_scratchNormals[groupThreadID.x + 1][groupThreadID.y + 1];

		float2 nm2 = NormalsMIPFilter(inTL, inTR, inBL, inBR);
		outNormals2[baseCoord / 2] = nm2;
		g_scratchNormals[groupThreadID.x][groupThreadID.y] = nm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float2 inTL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormals[groupThreadID.x + 2][groupThreadID.y + 0];
		float2 inBL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 2];
		float2 inBR = g_scratchNormals[groupThreadID.x + 2][groupThreadID.y + 2];

		float2 nm3 = NormalsMIPFilter(inTL, inTR, inBL, inBR);
		outNormals3[baseCoord / 4] = nm3;
		g_scratchNormals[groupThreadID.x][groupThreadID.y] = nm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float2 inTL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormals[groupThreadID.x + 4][groupThreadID.y + 0];
		float2 inBL = g_scratchNormals[groupThreadID.x + 0][groupThreadID.y + 4];
		float2 inBR = g_scratchNormals[groupThreadID.x + 4][groupThreadID.y + 4];

		float2 nm4 = NormalsMIPFilter(inTL, inTR, inBL, inBR);
		outNormals4[baseCoord / 8] = nm4;
	}
}

// Clear probe mask to invalid

#include "SSRT/GI1Common.hlsli"

RWTexture2D<uint> g_ProbeMask : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid
								 : SV_DispatchThreadID)
{
	g_ProbeMask[dtid] = kGI1_InvalidId;
}

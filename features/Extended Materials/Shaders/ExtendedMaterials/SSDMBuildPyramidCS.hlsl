// SSDM: Screen Space Displacement Mapping - Mip Pyramid Builder
// Downsamples the displacement vector texture from level 0 → levels 1..N
// by averaging 4 texels per upper-level texel.

cbuffer SSDMParams : register(b0)
{
	float2 SrcDim;
	float2 RcpSrcDim;
	int SrcMipLevel;
	int pad0;
	int pad1;
	int pad2;
};

Texture2D<float2> SrcTexture : register(t0);
RWTexture2D<float2> DstTexture : register(u0);
SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint2 dstDim;
	DstTexture.GetDimensions(dstDim.x, dstDim.y);
	if (any(dtid.xy >= dstDim))
		return;

	float2 uv = (float2(dtid.xy) + 0.5) / float2(dstDim);
	float2 result = SrcTexture.SampleLevel(LinearSampler, uv, SrcMipLevel);
	DstTexture[dtid.xy] = result;
}

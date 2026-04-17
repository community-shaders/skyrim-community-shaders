// SSDM: Screen Space Displacement Mapping - Iterative Refinement
// Runs from coarsest mip level down to level 0.
// At each level, samples 4 corners around the search center in Pyramid A,
// computes where each displacement vector points to, and averages
// (barycenter) to refine the source-pixel estimate for each destination pixel.

cbuffer SSDMParams : register(b0)
{
	float2 FullDim;
	float2 RcpFullDim;
	int MipLevel;
	int IsCoarsest;
	int pad0;
	int pad1;
};

Texture2D<float2> PyramidA : register(t0);
Texture2D<float2> PrevLevel : register(t1);
RWTexture2D<float2> CurLevel : register(u0);
SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint2 levelDim;
	CurLevel.GetDimensions(levelDim.x, levelDim.y);
	if (any(dtid.xy >= levelDim))
		return;

	float2 uv = (float2(dtid.xy) + 0.5) / float2(levelDim);
	float2 texelSize = 1.0 / float2(levelDim);

	float2 center;
	if (IsCoarsest)
		center = uv;
	else
		center = PrevLevel.SampleLevel(LinearSampler, uv, 0);

	float2 halfTexel = 0.5 * texelSize;

	float2 c00 = center + float2(-halfTexel.x, -halfTexel.y);
	float2 c10 = center + float2(+halfTexel.x, -halfTexel.y);
	float2 c01 = center + float2(-halfTexel.x, +halfTexel.y);
	float2 c11 = center + float2(+halfTexel.x, +halfTexel.y);

	float2 smp00 = PyramidA.SampleLevel(LinearSampler, c00, MipLevel);
	float2 smp10 = PyramidA.SampleLevel(LinearSampler, c10, MipLevel);
	float2 smp01 = PyramidA.SampleLevel(LinearSampler, c01, MipLevel);
	float2 smp11 = PyramidA.SampleLevel(LinearSampler, c11, MipLevel);

	float2 pos00 = c00 + smp00;
	float2 pos10 = c10 + smp10;
	float2 pos01 = c01 + smp01;
	float2 pos11 = c11 + smp11;

	float2 barycenter = (pos00 + pos10 + pos01 + pos11) * 0.25;

	CurLevel[dtid.xy] = barycenter;
}

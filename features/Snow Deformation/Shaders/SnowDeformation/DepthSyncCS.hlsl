Texture2D<float> MainDepth : register(t0);
Texture2D<float> ShellRasterDepth : register(t1);
Texture2D<float> PreSnowDepth : register(t2);
RWTexture2D<float> BlendedDepth : register(u0);
RWTexture2D<float> BlendedDepth16 : register(u1);

cbuffer DepthSyncCB : register(b2)
{
	uint2 SyncOrigin;
	uint2 SyncExtent;
}

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID) {
	uint2 dims;
	MainDepth.GetDimensions(dims.x, dims.y);
	const uint2 p = dtid.xy + SyncOrigin;
	if (any(dtid.xy >= SyncExtent) || any(p >= dims))
		return;

	float src = MainDepth[p];
	if (ShellRasterDepth[p] <= 0.0 && src >= PreSnowDepth[p])
		return;
	float blended = min(BlendedDepth[p], src);
	BlendedDepth[p] = blended;
	BlendedDepth16[p] = blended;
}

RWTexture2D<float> SavedDepth : register(u1);

[numthreads(8, 8, 1)] void SssSyncCS(uint3 dtid : SV_DispatchThreadID) {
	uint2 dims;
	ShellRasterDepth.GetDimensions(dims.x, dims.y);
	const uint2 p = dtid.xy + SyncOrigin;
	if (any(dtid.xy >= SyncExtent) || any(p >= dims))
		return;
	const float raster = ShellRasterDepth[p];
	if (raster == 0.0)
		return;
	const float blended = BlendedDepth[p];
	SavedDepth[p] = blended;
	BlendedDepth[p] = min(blended, abs(raster));
}

	[numthreads(8, 8, 1)] void SssRestoreCS(uint3 dtid : SV_DispatchThreadID)
{
	uint2 dims;
	ShellRasterDepth.GetDimensions(dims.x, dims.y);
	const uint2 p = dtid.xy + SyncOrigin;
	if (any(dtid.xy >= SyncExtent) || any(p >= dims))
		return;
	if (ShellRasterDepth[p] != 0.0)
		BlendedDepth[p] = SavedDepth[p];
}

Texture2D<float> FillSource : register(t9);

float4 FillVS(uint vertexID : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
	return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

#if defined(FILL_CLAIM)
float FillPS(float4 pos : SV_Position) : SV_Depth
{
	const float d = FillSource.Load(int3(pos.xy, 0));
	if (d <= 0.0)
		discard;
	return d;
}
#else
float FillPS(float4 pos : SV_Position) : SV_Depth
{
	return FillSource.Load(int3(pos.xy, 0));
}
#endif

cbuffer TerrainFineCB : register(b1)
{
	float2 FineOriginWorld;
	float2 WindowOriginWorld;
	uint FineDim;
	uint WindowDim;
	float TexelSize;
	float FineTexel;
}
Texture2D<float4> FineSrcWindow : register(t8);
RWTexture2D<float> FineDest : register(u5);

float FineCatmull(float p0, float p1, float p2, float p3, float t)
{
	return 0.5 * (2.0 * p1 + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);
}

[numthreads(8, 8, 1)] void TerrainFineCS(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= FineDim))
		return;
	float2 world = FineOriginWorld + float2(id.xy) * FineTexel;
	float2 uf = (world - WindowOriginWorld) / TexelSize;
	int2 i = (int2)floor(uf);
	float2 f = uf - float2(i);
	int2 lo = (i >> 5) << 5;
	int2 hi = lo + 32;
	float h[4][4];
	bool missing = false;
	[unroll] for (int j = 0; j < 4; j++)
	{
		[unroll] for (int k = 0; k < 4; k++)
		{
			int2 sIdx = i + int2(k - 1, j - 1);
			int2 c = clamp(sIdx, lo, hi);
			int2 m = 2 * c - sIdx;
			int2 wm = int2(WindowDim - 1, WindowDim - 1);
			float he = FineSrcWindow.Load(int3(clamp(c, int2(0, 0), wm), 0)).x;
			float hm = FineSrcWindow.Load(int3(clamp(m, int2(0, 0), wm), 0)).x;
			if (he < -50000.0 || hm < -50000.0)
				missing = true;
			h[j][k] = any(sIdx != c) ? 2.0 * he - hm : he;
		}
	}
	float outH = -100000.0;
	[branch] if (!missing)
	{
		float rows[4];
		[unroll] for (int r = 0; r < 4; r++)
			rows[r] = FineCatmull(h[r][0], h[r][1], h[r][2], h[r][3], f.x);
		outH = FineCatmull(rows[0], rows[1], rows[2], rows[3], f.y);
	}
	FineDest[id.xy] = outH;
}

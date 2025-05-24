// depth-aware upsampling: https://gist.github.com/pixelmager/a4364ea18305ed5ca707d89ddc5f8743

#include "Common/FastMath.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<half> srcDepth : register(t0);
Texture2D<half> srcAo : register(t1);           // half-res
Texture2D<half4> srcIlY : register(t2);         // half-res
Texture2D<half2> srcIlCoCg : register(t3);      // half-res
Texture2D<half4> srcGiSpecular : register(t4);  // half-res

RWTexture2D<half> outAo : register(u0);
RWTexture2D<half4> outIlY : register(u1);
RWTexture2D<half2> outIlCoCg : register(u2);
RWTexture2D<half4> outGiSpecular : register(u3);

#define min4(v) min(min(v.x, v.y), min(v.z, v.w))
#define max4(v) max(max(v.x, v.y), max(v.z, v.w))

#define BLEND_WEIGHT(a, b, c, d, w, sumw) ((a * w.x + b * w.y + c * w.z + d * w.w) / max(sumw, 1e-5))

[numthreads(8, 8, 1)] void main(const uint2 dtid
								: SV_DispatchThreadID) {
#ifdef HALF_RES
	int2 px00 = (dtid >> 1) + (dtid & 1) - 1;
#else  // QUARTER_RES
	int2 px00 = (dtid >> 2) + (dtid & 2) / 2 - 1;
#endif
	int2 px10 = px00 + int2(1, 0);
	int2 px01 = px00 + int2(0, 1);
	int2 px11 = px00 + int2(1, 1);

	float4 d = float4(
		srcDepth.Load(int3(px00, RES_MIP)),
		srcDepth.Load(int3(px01, RES_MIP)),
		srcDepth.Load(int3(px10, RES_MIP)),
		srcDepth.Load(int3(px11, RES_MIP)));

	// note: edge-detection
	float mind = min4(d);
	float maxd = max4(d);
	float diffd = maxd - mind;
	float avg = dot(d, 0.25.xxxx);
	bool d_edge = (diffd / avg) < 0.1;

	float ao;
	float4 y;
	float2 coCg;
	float4 giSpecular;

	[branch] if (d_edge)
	{
		float bgdepth = srcDepth[dtid];

		//note: depth weighing from https://www.ppsloan.org/publications/ProxyPG.pdf#page=5
		float4 dd = abs(d - bgdepth);
		float4 w = 1.0 / (dd + 0.00001);
		float sumw = w.x + w.y + w.z + w.w;

		ao = BLEND_WEIGHT(srcAo[px00], srcAo[px01], srcAo[px10], srcAo[px11], w, sumw);
		y = BLEND_WEIGHT(srcIlY[px00], srcIlY[px01], srcIlY[px10], srcIlY[px11], w, sumw);
		coCg = BLEND_WEIGHT(srcIlCoCg[px00], srcIlCoCg[px01], srcIlCoCg[px10], srcIlCoCg[px11], w, sumw);
		giSpecular = BLEND_WEIGHT(srcGiSpecular[px00], srcGiSpecular[px01], srcGiSpecular[px10], srcGiSpecular[px11], w, sumw);
	}
	else
	{
		float2 uv = (dtid + .5) * RcpFrameDim * OUT_FRAME_DIM * RcpTexDim;
		ao = srcAo.SampleLevel(samplerLinearClamp, uv, 0);
		y = srcIlY.SampleLevel(samplerLinearClamp, uv, 0);
		coCg = srcIlCoCg.SampleLevel(samplerLinearClamp, uv, 0);
		giSpecular = srcGiSpecular.SampleLevel(samplerLinearClamp, uv, 0);
	}

	outAo[dtid] = ao;
	outIlY[dtid] = y;
	outIlCoCg[dtid] = coCg;
	outGiSpecular[dtid] = giSpecular;
}
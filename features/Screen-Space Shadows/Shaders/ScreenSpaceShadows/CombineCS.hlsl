// Min-combine of 4 full-resolution shadow textures.
// Mips 1-3 have already been upscaled to full-res by UpscaleCS.

Texture2D<float> shadow0 : register(t0);
Texture2D<float> shadow1 : register(t1);
Texture2D<float> shadow2 : register(t2);
Texture2D<float> shadow3 : register(t3);
RWTexture2D<unorm float> combinedOut : register(u0);

cbuffer SSSCB : register(b1)
{
	float2 LightDirUV;
	float2 FrameDim;

	float2 RcpFrameDim;
	float2 TexDim;

	float2 RcpTexDim;
	float2 DynamicRes;

	float SurfaceThickness;
	float ShadowContrast;
	uint CurrentMip;
	uint pad;
};

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	if (any(float2(dtid) >= FrameDim))
		return;

	float s = min(min(shadow0.Load(int3(dtid, 0)), shadow1.Load(int3(dtid, 0))),
	              min(shadow2.Load(int3(dtid, 0)), shadow3.Load(int3(dtid, 0))));
	combinedOut[dtid] = s;
}

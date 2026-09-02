// Physical Glare — lossless RGB IFFT packing for vectorised upsampling

Texture2D<float2> TexIFFT_R : register(t0);
Texture2D<float2> TexIFFT_G : register(t1);
Texture2D<float2> TexIFFT_B : register(t2);

RWTexture2D<float4> RWTexPacked : register(u0);

[numthreads(8, 8, 1)] void CS_Pack(uint2 tid : SV_DispatchThreadID) {
	uint width, height;
	RWTexPacked.GetDimensions(width, height);
	if (tid.x >= width || tid.y >= height)
		return;

	RWTexPacked[tid] = float4(TexIFFT_R[tid].x, TexIFFT_G[tid].x, TexIFFT_B[tid].x, 0.0);
}

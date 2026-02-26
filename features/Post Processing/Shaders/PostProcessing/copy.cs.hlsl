Texture2D<float4> texSrc : register(t0);
RWTexture2D<float4> texOut : register(u0);

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	texOut[tid] = texSrc[tid];
}
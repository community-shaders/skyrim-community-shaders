Texture2D<float> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	OutputTexture[DTid.xy] = InputTexture[DTid.xy];
}

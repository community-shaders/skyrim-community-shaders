Texture2D<float2> TAAMask : register(t0);

RWTexture2D<float> AlphaMask : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float2 taaMask = TAAMask[dispatchID.xy];

	float alphaMask = taaMask.x * 0.5;

	alphaMask += sqrt(taaMask.y);

	AlphaMask[dispatchID.xy] = alphaMask;
}

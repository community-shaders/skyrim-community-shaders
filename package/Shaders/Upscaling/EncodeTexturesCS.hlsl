Texture2D<float2> TAAMask : register(t0);

RWTexture2D<float> AlphaMask : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float2 taaMask = pow(TAAMask[dispatchID.xy], 2.2);

	float alphaMask = taaMask.x * 0.25;

	alphaMask = lerp(alphaMask, 1.0, sqrt(taaMask.y));

	AlphaMask[dispatchID.xy] = alphaMask;
}

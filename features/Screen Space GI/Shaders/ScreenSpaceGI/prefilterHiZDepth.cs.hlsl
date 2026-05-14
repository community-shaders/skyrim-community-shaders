Texture2D<float> srcDepth : register(t0);
RWTexture2D<float> outDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	outDepth[dtid] = srcDepth[dtid];
}

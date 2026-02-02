Texture2DArray<float> InputTexture : register(t0);
RWTexture2DArray<float> OutputTexture : register(u0);

SamplerState PointSampler : register(s0);

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID
								: SV_DispatchThreadID) {
	uint w, h, slices;
	OutputTexture.GetDimensions(w, h, slices);

	if (dispatchThreadID.x < w && dispatchThreadID.y < h){
		// MIP 0 -> 1: each thread gathers a 2x2 block and computes max
		uint2 pixCoord = dispatchThreadID.xy * 2;

		uint inputW, inputH, inputSlices;
		InputTexture.GetDimensions(inputW, inputH, inputSlices);
		float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

		float4 depths4 = InputTexture.GatherRed(PointSampler, float3(uv, dispatchThreadID.z));
		OutputTexture[dispatchThreadID.xyz] = dot(depths4, 0.25);
	}
}
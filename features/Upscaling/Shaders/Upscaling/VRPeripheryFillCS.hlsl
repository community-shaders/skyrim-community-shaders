// Bilinear upscale from render-resolution per-eye buffer to display-resolution per-eye buffer.
// Used for VR viewport scaling: fills the full eye output with a cheap upscale so the
// periphery (outside the DLSS-processed center) is not black/empty.

cbuffer PeripheryFillCB : register(b0)
{
	uint SrcWidth;
	uint SrcHeight;
	uint DstWidth;
	uint DstHeight;
};

Texture2D<float4> SrcTexture : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (dispatchID.x >= DstWidth || dispatchID.y >= DstHeight)
		return;

	// Normalized UV with half-pixel offset for correct bilinear sampling
	float2 uv = (float2(dispatchID.xy) + 0.5) / float2(DstWidth, DstHeight);
	DstTexture[dispatchID.xy] = SrcTexture.SampleLevel(LinearSampler, uv, 0);
}

Texture2D<float4> UIBuffer : register(t0);

RWTexture2D<float4> FrameBuffer : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 texDims;
	FrameBuffer.GetDimensions(texDims.x, texDims.y);

	float2 uv = (float2(dispatchID.xy) + 0.5) / texDims.xy;

	float3 framebuffer = FrameBuffer[dispatchID.xy];
	float4 ui = UIBuffer.SampleLevel(LinearSampler, uv, 0);

	framebuffer = framebuffer.rgb * (1.0 - ui.a) + ui.rgb;
	FrameBuffer[dispatchID.xy] = float4(framebuffer, 1.0);
};
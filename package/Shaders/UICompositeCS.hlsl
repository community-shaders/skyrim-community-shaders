Texture2D<float4 unorm> UIBuffer : register(t0);

RWTexture2D<float4 unorm> FrameBuffer : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float4 ui = UIBuffer[dispatchID.xy];
	float3 framebuffer = FrameBuffer[dispatchID.xy];
	framebuffer = lerp(framebuffer, ui.rgb, ui.a);
	FrameBuffer[dispatchID.xy] = float4(framebuffer, 1.0);
};
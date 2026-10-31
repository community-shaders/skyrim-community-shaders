Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> OutputTex : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width, height;
	OutputTex.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float3 scene = SceneTex[dispatchID.xy].rgb;
	float4 ui = UITex[dispatchID.xy];
	OutputTex[dispatchID.xy] = float4(ui.rgb + scene * (1.0 - ui.a), 1.0);
}

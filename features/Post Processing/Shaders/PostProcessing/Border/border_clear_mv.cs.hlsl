Texture2D<float> DepthTexture : register(t0);

RWTexture2D<float4> MotionTexture : register(u0);

cbuffer BorderCB : register(b1)
{
	float4 BorderColor;  // color in xyz, depth threshold in w
	float4 Scale;        // xyzw: up, down, left, right
	float4 BufferDim;
};

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	if (any(DTid.xy >= uint2(BufferDim.xy)))
		return;

	float depth = DepthTexture[DTid.xy];
	float depthThreshold = BorderColor.w;
	if (depth > depthThreshold || depthThreshold == 0.0f) {
		float2 uv = (DTid.xy + 0.5f) * BufferDim.zw;
		if (uv.y < Scale.x || uv.y > (1 - Scale.y) || uv.x < Scale.z || uv.x > (1 - Scale.w)) {
			MotionTexture[DTid.xy] = float4(0.0, 0.0, 0.0, 1.0);
		}
	}
}

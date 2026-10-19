#include "Common/SharedData.hlsli"

Texture2D<float> DepthTexture : register(t0);

RWTexture2D<float4> MotionTexture : register(u0);

cbuffer BorderCB : register(b1)
{
	float4 BorderColor;  // color in xyz, depth threshold in w
	float4 Scale;        // xyzw: up, down, left, right
};

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	// This shader runs before upscaling, so account for dynamic resolution.
	float2 dynResDim = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;

	// Early exit for pixels outside the dynamic resolution area
	if (any(DTid.xy >= uint2(dynResDim)))
		return;

	float depth = DepthTexture[DTid.xy];
	float depthThreshold = BorderColor.w;
	if (depth > depthThreshold || depthThreshold == 0.0f) {
		// UV relative to the dynamic resolution viewport [0, 1]
		float2 uv = (DTid.xy + 0.5f) / dynResDim;
		if (uv.y < Scale.x || uv.y > (1 - Scale.y) || uv.x < Scale.z || uv.x > (1 - Scale.w)) {
			MotionTexture[DTid.xy] = float4(0.0, 0.0, 0.0, 1.0);
		}
	}
}

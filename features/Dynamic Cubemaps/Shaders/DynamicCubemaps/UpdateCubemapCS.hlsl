#include "DynamicCubemaps/CaptureCommon.hlsli"

float smoothbumpstep(float edge0, float edge1, float x)
{
	x = 1.0 - abs(saturate((x - edge0) / (edge1 - edge0)) - 0.5) * 2.0;
	return x * x * (3.0 - x - x);
}

[numthreads(8, 8, 1)] void main(uint3 ThreadID : SV_DispatchThreadID) {
	float4 history = 0.0;
	float4 historyPosition = 0.0;
	float4 capture = 0.0;
	if (LightingState[0].Reset == 0) {
		history = DynamicCubemapRaw[ThreadID];
		historyPosition = DynamicCubemapPosition[ThreadID];
		historyPosition.xyz = AdjustCapturePosition(historyPosition.xyz);
		capture = DynamicCubemap[ThreadID];
		if (CaptureHistoryExpired(historyPosition.w)) {
			history = 0.0;
			capture = 0.0;
		}
	}

	float3 position, color;
	float2 uv;
	if (SampleCapture(ThreadID, position, color, uv)) {
		float lerpFactor = history.a >= 0.9 && CaptureGeometryMatches(historyPosition.xyz, position) ? 0.5 : 1.0;
		float4 colorFinal = float4(color, 1.0);
		history = lerp(history, colorFinal, lerpFactor);
		historyPosition = float4(lerp(historyPosition.xyz, position, lerpFactor), GetCaptureTime());
		colorFinal *= sqrt(saturate(500.0 * length(position)));
		capture = lerp(capture, colorFinal, lerpFactor);
	} else {
		float distance = length(historyPosition.xyz);
		float distanceFactor = smoothbumpstep(0.0, 2.0, distance);
		if (distance < 1.0)
			distanceFactor = sqrt(distanceFactor);
#if defined(FAKEREFLECTIONS)
		distanceFactor = max(distanceFactor, smoothstep(0.0, 2.0, distance));
#endif
		capture = history * distanceFactor;
	}

	DynamicCubemapPosition[ThreadID] = historyPosition;
	DynamicCubemapRaw[ThreadID] = max(0.0, history);
	DynamicCubemap[ThreadID] = max(0.0, capture);
}

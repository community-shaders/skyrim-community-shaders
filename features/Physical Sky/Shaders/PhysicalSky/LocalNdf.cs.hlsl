cbuffer LocalNdfParameters : register(b1)
{
	float4 fieldRect;
	float4 centerSize;
	float4 rotationWeights;
	float4 heightFeather;
	float4 altitude;
	uint hasMask;
	uint modelingAlpha;
	uint dimension;
	uint modelingBlend;
};

Texture2D<float2> HeightInput : register(t0);
Texture2D<float4> ModelingInput : register(t1);
Texture2D<float> MaskInput : register(t2);
SamplerState ClampSampler : register(s0);
RWTexture2DArray<float4> HeightOutput : register(u0);
RWTexture2DArray<float4> ModelingOutput : register(u1);
RWTexture2DArray<float4> MaximumOutput : register(u2);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	if (any(pixel >= dimension))
		return;
	const uint3 target = uint3(pixel, 0);
	HeightOutput[target] = 0.0;
	ModelingOutput[target] = 0.0;
	MaximumOutput[target] = 0.0;
	const float2 world = fieldRect.xy + ((pixel + 0.5) / dimension) / fieldRect.zw;
	const float2 delta = world - centerSize.xy;
	const float2 local = float2(dot(delta, rotationWeights.xy), dot(delta, float2(-rotationWeights.y, rotationWeights.x)));
	const float2 uv = local / centerSize.zw + 0.5;
	if (any(uv <= 0.0) || any(uv >= 1.0))
		return;
	uint width, height;
	ModelingInput.GetDimensions(width, height);
	const float2 footprint = float2(width, height) / centerSize.zw / (dimension * fieldRect.zw);
	const float mip = max(log2(max(footprint.x, footprint.y)), 0.0);
	const float4 model = saturate(ModelingInput.SampleLevel(ClampSampler, uv, mip));
	float mask = 1.0;
	if (hasMask != 0u) {
		MaskInput.GetDimensions(width, height);
		const float2 maskFootprint = float2(width, height) / centerSize.zw / (dimension * fieldRect.zw);
		mask *= saturate(MaskInput.SampleLevel(ClampSampler, uv, max(log2(max(maskFootprint.x, maskFootprint.y)), 0.0)));
	}
	const float2 edge = min(uv, 1.0 - uv) * centerSize.zw;
	mask *= heightFeather.z > 0.0 ? smoothstep(0.0, heightFeather.z, min(edge.x, edge.y)) : 1.0;
	const float alpha = modelingAlpha != 0u ? model.a : 1.0;
	const float heightWeight = mask * alpha * rotationWeights.w;
	if (heightWeight > 0.0) {
		const float2 heights = saturate(HeightInput.SampleLevel(ClampSampler, uv, mip));
		const float2 normalizedHeight = (heightFeather.x + heights * heightFeather.y - altitude.x) / max(altitude.y, 1.0);
		HeightOutput[target] = float4(normalizedHeight * heightWeight, heightWeight, 0.0);
	}
	if (modelingBlend != 0u) {
		const float weight = mask * rotationWeights.z;
		MaximumOutput[target] = float4(model.rgb * weight, 0.0);
	} else {
		const float weight = mask * alpha * rotationWeights.z;
		ModelingOutput[target] = float4(model.rgb * weight, weight);
	}
}

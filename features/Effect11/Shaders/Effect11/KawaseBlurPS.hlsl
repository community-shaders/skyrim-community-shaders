Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
	float2 texelSize = 1.0 / 1024.0;
	float2 o = texelSize * 0.5;

	float4 sum = 0.0;
	sum += SourceTexture.SampleLevel(LinearSampler, input.txcoord0.xy + float2(-o.x, -o.y), 0);
	sum += SourceTexture.SampleLevel(LinearSampler, input.txcoord0.xy + float2( o.x, -o.y), 0);
	sum += SourceTexture.SampleLevel(LinearSampler, input.txcoord0.xy + float2(-o.x,  o.y), 0);
	sum += SourceTexture.SampleLevel(LinearSampler, input.txcoord0.xy + float2( o.x,  o.y), 0);

	return sum * 0.25;
}

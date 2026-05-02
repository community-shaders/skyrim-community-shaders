Texture2D SourceTexture : register(t0);

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
	return SourceTexture.Load(int3(input.pos.xy, 0));
}

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float2 TexCoord: TEXCOORD0;
};

#ifdef VSHADER
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	vsout.Position = float4(input.Position.xyz, 1);
	vsout.TexCoord = input.TexCoord.xy;

	return vsout;
}
#endif

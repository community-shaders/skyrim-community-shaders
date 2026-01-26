struct VS_INPUT
{
	float4 Position				: POSITION0;
	float2 TexCoord0			: TEXCOORD0;
	float4 Normal				: NORMAL0;
	float4 Tangent				: TANGENT0;
	float4 Color				: COLOR0;
	float4 Bitangent			: BINORMAL0;
};

struct VS_OUTPUT
{
    float4 Position		: SV_POSITION;
    float2 TexCoord0	: TEXCOORD0;
	float3 Normal		: TEXCOORD1;
	float3 Tangent		: TEXCOORD2;
	float3 Bitangent	: TEXCOORD3;
};

VS_OUTPUT vertex(VS_INPUT input)
{
    VS_OUTPUT output;

	float2 pos = input.TexCoord0.xy * 2.0f - 1.0f;

	output.Position = float4(pos.x, -pos.y, 1.0, 1.0);
	output.TexCoord0 = input.TexCoord0.xy;
	output.Normal = input.Normal.xyz;
	output.Tangent = input.Tangent.xyz;
	output.Bitangent = input.Bitangent.xyz;

	return output;
}

SamplerState MSNSampler			: register(s0);
Texture2D<float4> MSNormalMap	: register(t0);

float4 pixel(VS_OUTPUT input) : SV_Target
{
	float3 msnNormalMap = MSNormalMap.SampleLevel(MSNSampler, input.TexCoord0, 0.0f).rgb;
	
    float3 msNormals = normalize(msnNormalMap * 2.0f - 1.0f);

    float3 normal = normalize(input.Normal.xzy);
    float3 tangent = normalize(input.Tangent.xzy);
    float3 bitangent = normalize(input.Bitangent.xzy);

    float3x3 tbn = float3x3(tangent, bitangent, normal);

	float3 tangentNormal = mul(transpose(tbn), msNormals);

    return float4(tangentNormal * 0.5f + 0.5f, 1.0f);
}
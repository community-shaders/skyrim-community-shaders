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
	output.Normal = input.Normal.xzy;
	output.Tangent = input.Tangent.xzy;
	output.Bitangent = input.Bitangent.xzy;

	return output;
}

SamplerState MSNSampler			: register(s0);
Texture2D<float4> MSNormalMap	: register(t0);

float4 pixel(VS_OUTPUT input) : SV_Target
{
	float4 msnNormalMap = MSNormalMap.SampleLevel(MSNSampler, input.TexCoord0, 0.0f);	
    float3 msNormals = normalize(msnNormalMap.xyz * 2.0f - 1.0f);

    float3 normal = normalize(input.Normal);
    float3 tangent = normalize(input.Tangent);
    float3 bitangent = normalize(input.Bitangent);

    float3x3 tbn = float3x3(tangent, bitangent, normal);

	float3 tangentNormal = mul(tbn, msNormals - normal);
    tangentNormal.z = sqrt(saturate(1.0f - dot(tangentNormal.xy, tangentNormal.xy)));
	
    return float4(tangentNormal * 0.5f + 0.5f, msnNormalMap.w);
}
struct VS_OUTPUT
{
	float4 Position: SV_Position;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
	float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(uv * float2(2, -2) + float2(-1, 1), 1, 1);
	output.TexCoord = uv;
	return output;
}

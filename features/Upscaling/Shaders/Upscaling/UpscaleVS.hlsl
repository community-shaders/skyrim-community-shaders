struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

#if defined(VSHADER)

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
	VS_OUTPUT vsout;

	// Generate fullscreen triangle using vertex ID
	// Vertex 0: (-1, -1) -> (0, 1) UV
	// Vertex 1: (-1,  3) -> (0, -1) UV
	// Vertex 2: ( 3, -1) -> (2, 1) UV
	vsout.Position.x = (float)(vertexId / 2) * 4.0 - 1.0;
	vsout.Position.y = (float)(vertexId % 2) * 4.0 - 1.0;
	vsout.Position.z = 0.0;
	vsout.Position.w = 1.0;

	vsout.TexCoord.x = (float)(vertexId / 2) * 2.0;
	vsout.TexCoord.y = 1.0 - (float)(vertexId % 2) * 2.0;

	return vsout;
}

#endif
struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

#ifdef VSHADER

/// Procedural fullscreen triangle — no vertex buffer required.
/// Draw with: context->Draw(3, 0)
///
/// Generates an oversized triangle that covers the entire screen:
///   Vertex 0: (-1, -1) -> UV (0, 1)
///   Vertex 1: ( 3, -1) -> UV (2, 1)
///   Vertex 2: (-1,  3) -> UV (0,-1)
///
/// The GPU clips the triangle to the viewport, producing a fullscreen quad.
VS_OUTPUT main(uint vertexId : SV_VertexID)
{
	VS_OUTPUT vsout;

	vsout.TexCoord.x = (float)(vertexId / 2) * 2.0;
	vsout.TexCoord.y = 1.0 - (float)(vertexId % 2) * 2.0;

	vsout.Position.x = (float)(vertexId / 2) * 4.0 - 1.0;
	vsout.Position.y = (float)(vertexId % 2) * 4.0 - 1.0;
	vsout.Position.z = 0.0;
	vsout.Position.w = 1.0;

	return vsout;
}

#endif

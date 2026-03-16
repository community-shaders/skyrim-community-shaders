// VR Stereo Optimizations - Stencil Write Vertex Shader
//
// Procedural fullscreen triangle covering Eye 1 (right half of SBS buffer).
// No vertex buffer needed — vertex positions are generated from SV_VertexID.
// The viewport is set to Eye 1 by the C++ code, so we just emit a standard
// fullscreen triangle in clip space.

struct VS_OUTPUT
{
	float4 Position: SV_Position;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	// Fullscreen triangle: 3 vertices covering [-1,1] clip space
	float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	output.TexCoord = uv;

	return output;
}

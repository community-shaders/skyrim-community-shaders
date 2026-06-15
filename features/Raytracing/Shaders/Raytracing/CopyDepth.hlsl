struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut MainVS(uint id : SV_VertexID)
{
    VSOut o;

	// Generate fullscreen triangle using vertex ID
	// Vertex 0: (-1, -1) -> (0, 1) UV
	// Vertex 1: (-1,  3) -> (0, -1) UV
	// Vertex 2: ( 3, -1) -> (2, 1) UV
    o.pos.x = (float) (id / 2) * 4.0 - 1.0;
    o.pos.y = (float) (id % 2) * 4.0 - 1.0;
    o.pos.z = 0.0;
    o.pos.w = 1.0;

    o.uv.x = (float) (id / 2) * 2.0;
    o.uv.y = 1.0 - (float) (id % 2) * 2.0;
    
    return o;
}

Texture2D<float> DepthTex : register(t0);

float MainPS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Depth
{
    // Load is better than Sample for exact copy
    int2 pixel = int2(pos.xy);
    return DepthTex.Load(int3(pixel, 0));
}
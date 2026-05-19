struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut MainVS(uint id : SV_VertexID)
{
    VSOut o;

    float2 pos = float2(
        (id == 2) ? 3.0 : -1.0,
        (id == 1) ? 3.0 : -1.0
    );

    o.pos = float4(pos, 0.0, 1.0);
    o.uv = pos * float2(0.5, -0.5) + 0.5;

    return o;
}

Texture2D<float> DepthTex : register(t0);
Texture2D<float4> MotionTex : register(t1);

SamplerState s0 : register(s0);

struct PSOut
{
    float2 motion : SV_Target0;
    float depth : SV_Depth;
};

PSOut MainPS(float4 pos : SV_Position, float2 uv : TEXCOORD0)
{
    PSOut o;

    // Load is better than Sample for exact copy
    int2 pixel = int2(pos.xy);

    float depth = DepthTex.Load(int3(pixel, 0));
    float2 motion = MotionTex.Load(int3(pixel, 0)).xy;

    o.depth = saturate(depth); // important for UNORM
    o.motion = motion;

    return o;
}
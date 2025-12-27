Texture2D<unorm float> DepthIn  : register(t0);
RWTexture2D<float> DepthOut     : register(u0);

#ifndef RES_X
#define RES_X (1280.0f)
#endif

#ifndef RES_Y
#define RES_Y (720.0f)
#endif

const float2 res = float2(RES_X, RES_Y);

SamplerState Sampler : register(s0);


[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (any(id > res))
        return;
    
    float2 uv = (id.xy + 0.5f) / res;
    
    DepthOut[id] = DepthIn.SampleLevel(Sampler, uv, 0); //DepthIn[id];
}
#include "Common/SharedData.hlsli"
#include "Raytracing/Includes/Common.hlsli"

Texture2D<unorm float>  DepthIn         : register(t0);
RWTexture2D<float>      DepthOut        : register(u0);
RWTexture2D<float2>     DepthViewOut    : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    uint width, height;
    DepthIn.GetDimensions(width, height);

    if (id.x >= width || id.y >= height)
        return;

    const float depthScreen = DepthIn[id];
    DepthOut[id] = depthScreen;

    float depthLinear = ScreenToViewDepth(depthScreen, SharedData::CameraData);
    DepthViewOut[id] = float2(depthLinear, 0.0f);
}
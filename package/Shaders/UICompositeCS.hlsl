Texture2D<float4> UIBuffer : register(t0);
RWTexture2D<float4> FrameBuffer : register(u0);
SamplerState PointSampler : register(s0);

// Lanczos filter parameters
static const float LANCZOS_RADIUS = 2.0;
static const float PI = 3.14159265359;

// Lanczos kernel function
float lanczos(float x) {
    if (abs(x) < 0.001) return 1.0;
    if (abs(x) >= LANCZOS_RADIUS) return 0.0;
    
    float px = PI * x;
    return (sin(px) * sin(px / LANCZOS_RADIUS)) / (px * px / LANCZOS_RADIUS);
}

// Advanced Lanczos upscaling function
float4 sampleLanczos(Texture2D<float4> tex, float2 uv) {
    float2 texSize;
    tex.GetDimensions(texSize.x, texSize.y);
    
    float2 texelSize = 1.0 / texSize;
    float2 pixelPos = uv * texSize - 0.5;
    float2 floorPos = floor(pixelPos);
    float2 fracPos = pixelPos - floorPos;
    
    float4 color = float4(0, 0, 0, 0);
    float totalWeight = 0.0;
    
    // Sample in a 4x4 grid around the pixel
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            float2 samplePos = (floorPos + float2(x, y) + 0.5) * texelSize;
            
            // Calculate Lanczos weights
            float weightX = lanczos(fracPos.x - x);
            float weightY = lanczos(fracPos.y - y);
            float weight = weightX * weightY;
            
            // Sample the texture (with border clamping)
            samplePos = clamp(samplePos, texelSize * 0.5, 1.0 - texelSize * 0.5);
            float4 sample = tex.SampleLevel(PointSampler, samplePos, 0);
            
            color += sample * weight;
            totalWeight += weight;
        }
    }
    
    // Normalize by total weight
    return totalWeight > 0.001 ? color / totalWeight : float4(0, 0, 0, 0);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {
    float2 texDims;
    FrameBuffer.GetDimensions(texDims.x, texDims.y);
    
    float2 uv = (float2(dispatchID.xy) + 0.5) / texDims;
    
    float3 framebuffer = FrameBuffer[dispatchID.xy].rgb;
    float4 ui = sampleLanczos(UIBuffer, uv);
    
    // Alpha blend the UI over the framebuffer
    framebuffer = framebuffer * (1.0 - ui.a) + ui.rgb * ui.a;
    FrameBuffer[dispatchID.xy] = float4(framebuffer, 1.0);
}
// Composite Blur Pass Shader with Rounded Rectangle Mask
// Part of the BackgroundBlur system - applies blurred texture with rounded corners

cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;     // x = 1/width, y = 1/height, z = blur strength, w = unused
    int4   BlurParams;    // x = samples, y = unused, z = unused, w = unused
};

cbuffer WindowBuffer : register(b1)
{
    float4 WindowRect;    // x = minX, y = minY, z = maxX, w = maxY (in pixels)
    float4 WindowParams;  // x = cornerRadius, y = screenWidth, z = screenHeight, w = unused
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y;
    return output;
}

// Compute signed distance to a rounded rectangle
// Returns negative inside, positive outside
float RoundedRectSDF(float2 pixelPos, float2 rectMin, float2 rectMax, float radius)
{
    // Center of the rectangle
    float2 rectCenter = (rectMin + rectMax) * 0.5f;
    float2 rectHalfSize = (rectMax - rectMin) * 0.5f;

    // Clamp radius to not exceed half the smallest dimension
    radius = min(radius, min(rectHalfSize.x, rectHalfSize.y));

    // Distance from center
    float2 p = abs(pixelPos - rectCenter) - rectHalfSize + radius;

    // SDF for rounded rectangle
    return length(max(p, 0.0f)) + min(max(p.x, p.y), 0.0f) - radius;
}

float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    // Convert UV to pixel coordinates
    float2 pixelPos = input.TexCoord * float2(WindowParams.y, WindowParams.z);

    // Get window bounds and corner radius
    float2 rectMin = WindowRect.xy;
    float2 rectMax = WindowRect.zw;
    float cornerRadius = WindowParams.x;

    // Calculate signed distance to rounded rectangle
    float sdf = RoundedRectSDF(pixelPos, rectMin, rectMax, cornerRadius);

    // Create smooth edge (anti-aliased)
    // Negative = inside, positive outside
    // Use 1.0 pixel transition for smooth edge
    float alpha = saturate(-sdf);

    // Early out if completely outside
    if (alpha <= 0.0f)
    {
        discard;
    }

    // Sample the blurred texture
    float4 blurColor = InputTexture.Sample(LinearSampler, input.TexCoord);

    // Apply rounded corner mask to alpha
    // The blur strength is applied via blend state, so just use the rounded mask here
    blurColor.a = alpha;

    return blurColor;
}

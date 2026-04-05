// Composite Blur Pass Shader with Rounded Rectangle Mask
// Part of the BackgroundBlur system - applies blurred texture with rounded corners

cbuffer WindowBuffer : register(b1)
{
	float4 WindowRect;    // x = minX, y = minY, z = maxX, w = maxY (in pixels)
	float4 WindowParams;  // x = cornerRadius, y = screenWidth, z = screenHeight, w = fullscreen (1.0 = skip SDF)
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

static const float TWO_PI = 6.28318530718f;
static const int NUM_JITTER_SAMPLES = 4;
static const float DOWNSAMPLE_FACTOR = 8.0f;
static const float CLIP_EPSILON = 0.001f;

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
	output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
	output.Position.y = -output.Position.y;
	return output;
}

// High quality 2D hash - returns two independent random values in [0,1]
// Uses different prime multipliers to avoid correlation between x and y
float2 Hash22(float2 p)
{
	// Two independent hashes using different constants
	float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
	p3 += dot(p3, p3.yzx + 33.33f);
	return frac((p3.xx + p3.yz) * p3.zy);
}

// Soft sampling with blurred dithering - takes 4 samples with jittered offsets
// and averages them to smooth out the noise while still breaking up blocky pixels
float4 SampleWithSoftening(float2 uv, float2 pixelPos, float2 texelSize)
{
	// Get base random offset for this pixel
	float2 noise = Hash22(pixelPos);

	// Rotated grid offsets (45 degree rotation for better coverage)
	// This creates a smooth disc-like sampling pattern
	static const float2 offsets[NUM_JITTER_SAMPLES] = {
		float2(-0.25f, -0.25f),
		float2(0.25f, -0.25f),
		float2(-0.25f, 0.25f),
		float2(0.25f, 0.25f)
	};

	// Random rotation angle based on pixel position
	float angle = noise.x * TWO_PI;
	float s, c;
	sincos(angle, s, c);
	float2x2 rotation = float2x2(c, -s, s, c);

	// Sample 4 points with rotated jittered offsets and average
	float4 result = 0;
	[unroll] for (int i = 0; i < NUM_JITTER_SAMPLES; i++)
	{
		float2 jitter = mul(rotation, offsets[i]) * texelSize;
		result += InputTexture.Sample(LinearSampler, uv + jitter);
	}

	return result / (float)NUM_JITTER_SAMPLES;
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

float4 PS_Main(VS_OUTPUT input) :
	SV_TARGET
{
	// Convert UV to pixel coordinates
	float2 pixelPos = input.TexCoord * float2(WindowParams.y, WindowParams.z);

	// Get window bounds and corner radius
	float2 rectMin = WindowRect.xy;
	float2 rectMax = WindowRect.zw;
	float cornerRadius = WindowParams.x;

	float alpha = 1.0f;
	if (WindowParams.w < 0.5f) {
		// Calculate signed distance to rounded rectangle
		float sdf = RoundedRectSDF(pixelPos, rectMin, rectMax, cornerRadius);

		// Create smooth edge (anti-aliased)
		// Negative = inside, positive outside
		// Use 1.0 pixel transition for smooth edge
		alpha = saturate(-sdf);

		// Early out if completely outside
		if (alpha <= 0.0f) {
			discard;
		}
	}

	float2 blurTexelSize = DOWNSAMPLE_FACTOR / float2(WindowParams.y, WindowParams.z);

	// Sample with soft dithering to hide blocky pixels from the downsampled blur
	float4 blurColor = SampleWithSoftening(input.TexCoord, pixelPos, blurTexelSize);

	// Apply rounded corner mask to alpha
	// The blur strength is applied via blend state, so just use the rounded mask here
	blurColor.a = alpha;

	return blurColor;
}

// Clear shader entry point - outputs transparent black inside rounded rect only
// Used to clear UI buffer (HUD) in the exact same shape as the blur
float4 PS_Clear(VS_OUTPUT input) :
	SV_TARGET
{
	float2 pixelPos = input.TexCoord * float2(WindowParams.y, WindowParams.z);
	float sdf = RoundedRectSDF(pixelPos, WindowRect.xy, WindowRect.zw, WindowParams.x);

	// Discard pixels outside rounded rect to preserve HUD in corners
	clip(-sdf - CLIP_EPSILON);

	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

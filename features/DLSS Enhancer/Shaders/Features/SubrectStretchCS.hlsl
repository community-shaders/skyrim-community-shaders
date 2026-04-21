// Stretches the DRS-rendered region from a temporary render-resolution SBS texture
// to fill the entire eye in the display-resolution kMAIN SBS texture.
// Dispatched once per eye. Supports multiple sampling modes:
//   0 = Bilinear (clean upscale)
//   1 = Point / Nearest (cheapest, VRS-like broadcast)
//   2 = Gaussian Blur 3x3 (soft periphery background)

cbuffer StretchCB : register(b0)
{
	uint DstOffsetX;    // SBS destination X offset for this eye (0 or eyeWidthOut)
	uint DstWidth;      // display-resolution eye width
	uint DstHeight;     // display-resolution eye height
	uint SrcOffsetX;    // SBS source X offset for this eye (0 or renderEyeW)
	uint SrcWidth;      // render-resolution SBS total width (for UV normalisation)
	uint SrcHeight;     // render-resolution SBS total height
	uint SrcEyeWidth;   // render-resolution per-eye width
	uint SrcEyeHeight;  // render-resolution per-eye height
	uint StretchMode;   // 0=Bilinear, 1=Point, 2=GaussianBlur
	float BlurRadius;   // Texel-space radius for Gaussian blur (typical 0.5-4.0)
	uint2 _pad;
};

Texture2D<float4> SrcTex : register(t0);
SamplerState BilinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)] void main(uint3 tid : SV_DispatchThreadID) {
	if (tid.x >= DstWidth || tid.y >= DstHeight)
		return;

	// Map output pixel to normalised position within this eye [0,1]
	float u = ((float)tid.x + 0.5) / (float)DstWidth;
	float v = ((float)tid.y + 0.5) / (float)DstHeight;

	// Map to source texel coordinates within this eye's render region
	// then convert to full SBS texture UV (adding eye offset)
	float srcU = (u * (float)SrcEyeWidth + (float)SrcOffsetX) / (float)SrcWidth;
	float srcV = (v * (float)SrcEyeHeight) / (float)SrcHeight;

	float4 color;

	if (StretchMode == 1) {
		// Point / Nearest: integer texel lookup, cheapest
		uint2 srcPixel = uint2(
			(uint)(u * (float)SrcEyeWidth) + SrcOffsetX,
			(uint)(v * (float)SrcEyeHeight));
		color = SrcTex.Load(int3(srcPixel, 0));
	} else if (StretchMode == 2) {
		// Gaussian blur 3x3: 9-tap weighted average around center
		float2 texelSize = float2(1.0 / (float)SrcWidth, 1.0 / (float)SrcHeight);
		float2 center = float2(srcU, srcV);
		float2 step = texelSize * BlurRadius;

		// Gaussian weights for 3x3 kernel (sigma ~ 0.85 * radius)
		// Center=4, Edge=2, Corner=1, sum=16
		float4 sum = SrcTex.SampleLevel(BilinearSampler, center, 0) * 4.0;
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(-step.x, 0), 0) * 2.0;
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(step.x, 0), 0) * 2.0;
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(0, -step.y), 0) * 2.0;
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(0, step.y), 0) * 2.0;
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(-step.x, -step.y), 0);
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(step.x, -step.y), 0);
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(-step.x, step.y), 0);
		sum += SrcTex.SampleLevel(BilinearSampler, center + float2(step.x, step.y), 0);
		color = sum * (1.0 / 16.0);
	} else {
		// Bilinear (default): single hardware-filtered sample
		color = SrcTex.SampleLevel(BilinearSampler, float2(srcU, srcV), 0);
	}

	DstTex[uint2(tid.x + DstOffsetX, tid.y)] = color;
}

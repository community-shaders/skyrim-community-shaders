// BilinearCopyPS.hlsl — DLSSperf downscale pass
// Box 3×3 filter: testTexture (3k) → kMAIN (1k)
// For 3:1 downscale, each output pixel averages the 3×3 source region,
// ensuring all DLSS output pixels contribute (vs bilinear's 2×2 coverage).
// Reuses UpscaleVS.hlsl for fullscreen triangle generation (SV_VertexID)

#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)

typedef VS_OUTPUT PS_INPUT;

SamplerState LinearSampler : register(s0);
Texture2D<float4> SourceTex : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
	float2 srcSize;
	SourceTex.GetDimensions(srcSize.x, srcSize.y);
	float2 texelSize = 1.0 / srcSize;

	float4 sum = 0;
	[unroll] for (int y = -1; y <= 1; y++)
		[unroll] for (int x = -1; x <= 1; x++)
			sum += SourceTex.SampleLevel(LinearSampler, input.TexCoord + float2(x, y) * texelSize, 0);
	return sum * (1.0 / 9.0);
}

#endif

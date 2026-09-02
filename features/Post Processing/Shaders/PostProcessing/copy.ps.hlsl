// Format-conversion copy: renders the source texture into the conversion
// target when the pipeline output format differs from the game render target
// format. Loads point-sampled so no filtering is applied during conversion.

#include "PostProcessing/fullscreen.hlsli"

Texture2D<float4> texSrc : register(t0);

float4 main(FullscreenTriangleVSOutput input) : SV_Target
{
	return texSrc.Load(int3(input.Position.xy, 0));
}

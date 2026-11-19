#ifndef POST_PROCESSING_CONVERT_COLOR_HLSLI
#define POST_PROCESSING_CONVERT_COLOR_HLSLI

#include "Common/ColorSpaces.hlsli"

float4 ConvertColor(float4 color, uint inputGamut, uint outputGamut, float gamma, float scale)
{
	color.rgb *= scale;
	if (inputGamut != outputGamut) {
		if (inputGamut == 1)
			color.rgb = AP1TosRGB(color.rgb);
		else if (inputGamut == 2)
			color.rgb = mul(XYZ_2_sRGB_MAT, mul(Rec2020_2_XYZ_MAT, color.rgb));
		if (outputGamut == 1)
			color.rgb = sRGBToAP1(color.rgb);
		else if (outputGamut == 2)
			color.rgb = mul(XYZ_2_Rec2020_MAT, mul(sRGB_2_XYZ_MAT, color.rgb));
	}
	if (gamma != 1.0)
		color.rgb = sign(color.rgb) * pow(abs(color.rgb), gamma);
	return color;
}

#endif

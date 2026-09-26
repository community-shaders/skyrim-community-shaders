#ifndef __TRANSFER_FUNCTIONS_DEPENDENCY_HLSL__
#define __TRANSFER_FUNCTIONS_DEPENDENCY_HLSL__

// MIRROR OF: src/Utils/ColorSpace.h

namespace TransferFunctions
{
	static const float GAME_GAMMA = 1.8;
	static const float GAME_GAMMA_INV = 1.0 / GAME_GAMMA;
	static const float GAMMA_22 = 2.2;
	static const float GAMMA_22_INV = 1.0 / GAMMA_22;

	float SRGBToLinear(float encodedSRGB)
	{
		float magnitude = abs(encodedSRGB);
		float linearSRGB = magnitude <= 0.04045f ? magnitude / 12.92f : pow((magnitude + 0.055f) / 1.055f, 2.4f);
		return linearSRGB * sign(encodedSRGB);
	}

	float3 SRGBToLinear(float3 encodedSRGB)
	{
		return float3(
			SRGBToLinear(encodedSRGB.r),
			SRGBToLinear(encodedSRGB.g),
			SRGBToLinear(encodedSRGB.b));
	}

	float LinearToSRGB(float linearValue)
	{
		float magnitude = abs(linearValue);
		float encoded = magnitude <= 0.0031308f ? magnitude * 12.92f : 1.055f * pow(magnitude, 1.0f / 2.4f) - 0.055f;
		return encoded * sign(linearValue);
	}

	float3 LinearToSRGB(float3 linearValue)
	{
		return float3(
			LinearToSRGB(linearValue.r),
			LinearToSRGB(linearValue.g),
			LinearToSRGB(linearValue.b));
	}

	float GameGammaToLinear(float color)
	{
		return pow(abs(color), GAME_GAMMA);
	}

	float LinearToGameGamma(float color)
	{
		return pow(abs(color), GAME_GAMMA_INV);
	}

	float3 GameGammaToLinear(float3 color)
	{
		return pow(abs(color), GAME_GAMMA);
	}

	float3 LinearToGameGamma(float3 color)
	{
		return pow(abs(color), GAME_GAMMA_INV);
	}

	float3 Gamma22ToLinear(float3 color)
	{
		return pow(abs(color), GAMMA_22);
	}

	float3 LinearToGamma22(float3 color)
	{
		return pow(abs(color), GAMMA_22_INV);
	}

	float3 SignedGamma22ToLinear(float3 color)
	{
		return sign(color) * pow(abs(color), GAMMA_22);
	}

	float3 LinearToSignedGamma22(float3 color)
	{
		return sign(color) * pow(abs(color), GAMMA_22_INV);
	}
}

#endif  //__TRANSFER_FUNCTIONS_DEPENDENCY_HLSL__

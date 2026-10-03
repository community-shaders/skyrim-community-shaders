#include "ColorSpace.h"

#include <cmath>

namespace Util::ColorSpace
{
	float SRGBToLinear(float encodedSRGB)
	{
		const float magnitude = std::abs(encodedSRGB);
		const float linearSRGB = magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
		return std::copysign(linearSRGB, encodedSRGB);
	}

	float LinearToSRGB(float linearValue)
	{
		const float magnitude = std::abs(linearValue);
		const float encoded = magnitude <= 0.0031308f ? magnitude * 12.92f : 1.055f * std::pow(magnitude, 1.0f / 2.4f) - 0.055f;
		return std::copysign(encoded, linearValue);
	}

	float GameGammaToLinear(float encoded)
	{
		return std::copysign(std::pow(std::abs(encoded), GAME_GAMMA), encoded);
	}

	float LinearToGameGamma(float linearValue)
	{
		return std::copysign(std::pow(std::abs(linearValue), GAME_GAMMA_INV), linearValue);
	}

	void SRGBToLinear(float* a_rgb3)
	{
		for (int i = 0; i < 3; ++i) {
			if (std::isfinite(a_rgb3[i]))
				a_rgb3[i] = SRGBToLinear(a_rgb3[i]);
		}
	}

	void LinearToSRGB(float* a_rgb3)
	{
		for (int i = 0; i < 3; ++i) {
			if (std::isfinite(a_rgb3[i]))
				a_rgb3[i] = LinearToSRGB(a_rgb3[i]);
		}
	}

	void GameGammaToLinear(float* a_rgb3)
	{
		for (int i = 0; i < 3; ++i) {
			if (std::isfinite(a_rgb3[i]))
				a_rgb3[i] = GameGammaToLinear(a_rgb3[i]);
		}
	}

	void SRGBGamutToAP1(float* a_rgb3)
	{
		const float r = 0.6130974024f * a_rgb3[0] + 0.3395231461f * a_rgb3[1] + 0.0473794514f * a_rgb3[2];
		const float g = 0.0701937225f * a_rgb3[0] + 0.9163538791f * a_rgb3[1] + 0.0134523986f * a_rgb3[2];
		const float b = 0.0206155929f * a_rgb3[0] + 0.1095697729f * a_rgb3[1] + 0.8698146341f * a_rgb3[2];

		a_rgb3[0] = r;
		a_rgb3[1] = g;
		a_rgb3[2] = b;
	}
}

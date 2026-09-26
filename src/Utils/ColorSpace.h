#pragma once

// MIRROR OF: package/Shaders/Common/TransferFunctions.hlsli
//            package/Shaders/Common/ColorSpaces.hlsli (sRGBToAP1)

namespace Util::ColorSpace
{
	struct LightColor
	{
		RE::NiColor color{};
		RE::NiColor tint{ 1.f, 1.f, 1.f };
		float intensity = 1.f;
		float alpha = 0.f;

		float4 GetColor() const
		{
			const auto rgb = color * tint * intensity;
			return { rgb.red, rgb.green, rgb.blue, alpha };
		}
	};

	inline constexpr float GAME_GAMMA = 1.8f;
	inline constexpr float GAME_GAMMA_INV = 1.0f / GAME_GAMMA;

	/** @brief sRGB EOTF：Encoded -> Linear. */
	float SRGBToLinear(float encodedSRGB);
	/** @brief sRGB OETF：Linear -> Encoded. */
	float LinearToSRGB(float linearValue);
	/** @brief In game gamma 1.8 -> Linear. */
	float GameGammaToLinear(float encoded);
	/** @brief Linear -> In game gamma 1.8. */
	float LinearToGameGamma(float linearValue);

	/** @brief Apply SRGBToLinear to RGB 3-component in-place. */
	void SRGBToLinear(float* a_rgb3);
	/** @brief Apply LinearToSRGB to RGB 3-component in-place. */
	void LinearToSRGB(float* a_rgb3);
	/** @brief Apply GameGammaToLinear to RGB 3-component in-place. */
	void GameGammaToLinear(float* a_rgb3);

	/** @brief Linear sRGB primaries -> Linear AP1(ACEScg) primaries, in-place.
	 *  The matrix is equivalent to the one in package/Shaders/Common/ColorSpaces.hlsli
	 *  (sRGB_2_XYZ_MAT, D65_2_D60_CAT, then XYZ_2_AP1_MAT). */
	void SRGBGamutToAP1(float* a_rgb3);
}

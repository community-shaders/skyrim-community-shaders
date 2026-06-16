#pragma once
using float3x3 = DirectX::XMFLOAT3X3;

namespace SphericalHarmonics
{
	/** @brief Second-order (L=0..1) spherical harmonics with 4 scalar coefficients. */
	struct SH2
	{
		float c0;      /**< @brief L=0 (DC / constant) coefficient. */
		float c1[3];   /**< @brief L=1 coefficients for M=-1, M=0, M=1. */

		SH2() : c0(0.0f), c1{ 0.0f, 0.0f, 0.0f } {}
		SH2(float _c0, float _c1_0, float _c1_1, float _c1_2) : c0(_c0), c1{ _c1_0, _c1_1, _c1_2 } {}
	};

	/** @brief Per-channel (RGB) second-order spherical harmonics. */
	struct SH2Color
	{
		SH2 r;
		SH2 g;
		SH2 b;

		SH2Color() : r(), g(), b() {}

		/** @brief Construct with the same SH coefficients for all three channels. */
		explicit SH2Color(const SH2& sh) : r(sh), g(sh), b(sh) {}

		SH2Color(SH2 _r, SH2 _g, SH2 _b) : r(_r), g(_g), b(_b) {}
	};

	/**
	 * @brief Evaluate the SH2 basis functions for a given direction.
	 * @param dir Unit direction vector.
	 * @return SH2 coefficients representing a delta in that direction.
	 */
	SH2 Evaluate(float3 dir);

	/**
	 * @brief Compute the dot product of two SH2 coefficient sets.
	 * @param a First SH2 operand.
	 * @param b Second SH2 operand.
	 * @return The scalar dot product.
	 */
	float Dot(SH2 a, SH2 b);

	/**
	 * @brief Compute per-channel dot products of two SH2Color coefficient sets.
	 * @param a First SH2Color operand.
	 * @param b Second SH2Color operand.
	 * @return Per-channel (RGB) dot products.
	 */
	float3 Dot(SH2Color a, SH2Color b);

	/**
	 * @brief Reconstruct the scalar value of an SH function in a given direction.
	 * @param dir Unit direction to sample.
	 * @param sh The SH2 coefficients to unproject.
	 * @return The reconstructed scalar value.
	 */
	float Unproject(float3 dir, SH2 sh);

	/**
	 * @brief Reconstruct the RGB value of an SH colour function in a given direction.
	 * @param dir Unit direction to sample.
	 * @param sh The SH2Color coefficients to unproject.
	 * @return The reconstructed RGB value.
	 */
	float3 Unproject(float3 dir, SH2Color sh);

	/**
	 * @brief Evaluate a clamped cosine lobe oriented along the given direction in SH.
	 * @param dir Unit direction of the lobe axis.
	 * @return SH2 representation of the cosine lobe.
	 */
	SH2 EvaluateCosineLobe(float3 dir);

	/**
	 * @brief Evaluate the Henyey-Greenstein phase function in SH.
	 * @param dir Unit direction of the phase function axis.
	 * @param g Asymmetry parameter in [-1, 1].
	 * @return SH2 representation of the phase function.
	 */
	SH2 EvaluatePhaseHG(float3 dir, float g);

	/**
	 * @brief Add two SH2 coefficient sets together.
	 * @param a First operand.
	 * @param b Second operand.
	 * @return The coefficient-wise sum.
	 */
	SH2 Add(SH2 a, SH2 b);

	/** @brief Add two SH2Color coefficient sets together, per channel. */
	SH2Color Add(SH2Color a, SH2Color b);

	/**
	 * @brief Uniformly scale all SH2 coefficients.
	 * @param sh The coefficients to scale.
	 * @param scale The scalar multiplier.
	 * @return The scaled SH2.
	 */
	SH2 Scale(SH2 sh, float scale);

	/** @brief Uniformly scale all SH2Color coefficients, per channel. */
	SH2Color Scale(SH2Color sh, float scale);

	/**
	 * @brief Rotate SH2 coefficients by a 3x3 rotation matrix.
	 * @param sh The coefficients to rotate.
	 * @param rotMatrix The rotation matrix to apply (L=1 band only; L=0 is invariant).
	 * @return The rotated SH2.
	 */
	SH2 Rotate(SH2 sh, float3x3 rotMatrix);

	/** @brief Rotate SH2Color coefficients by a 3x3 rotation matrix, per channel. */
	SH2Color Rotate(SH2Color sh, float3x3 rotMatrix);

	/**
	 * @brief Compute the integral of the product of two SH functions over the sphere.
	 * @param shL Left SH2 operand.
	 * @param shR Right SH2 operand.
	 * @return The scalar integral value (equivalent to Dot).
	 */
	float FuncProductIntegral(SH2 shL, SH2 shR);

	/** @brief Compute per-channel product integrals of two SH2Color functions. */
	float3 FuncProductIntegral(SH2Color shL, SH2Color shR);

	/**
	 * @brief Approximate the product of two SH functions in SH space.
	 * @param shL Left SH2 operand.
	 * @param shR Right SH2 operand.
	 * @return The SH2 product approximation.
	 */
	SH2 Product(SH2 shL, SH2 shR);

	/** @brief Approximate the per-channel product of two SH2Color functions. */
	SH2Color Product(SH2Color shL, SH2Color shR);

	/**
	 * @brief Apply a Hanning windowed convolution to attenuate higher SH bands.
	 * @param sh The coefficients to filter.
	 * @param w Window width parameter (larger values preserve more of band 1).
	 * @return The filtered SH2.
	 */
	SH2 HanningConvolution(SH2 sh, float w);

	/** @brief Apply a Hanning windowed convolution per channel. */
	SH2Color HanningConvolution(SH2Color sh, float w);

	/**
	 * @brief Convolve SH coefficients with the normalised clamped cosine kernel for diffuse irradiance.
	 * @param sh The radiance SH to convolve.
	 * @return The diffuse irradiance SH.
	 */
	SH2 DiffuseConvolution(SH2 sh);

	/** @brief Convolve SH2Color coefficients with the cosine kernel for diffuse irradiance, per channel. */
	SH2Color DiffuseConvolution(SH2Color sh);

	/**
	 * @brief Construct an approximate specular lobe in SH space using the dominant GGX reflection direction.
	 * @param N Surface normal.
	 * @param V View direction.
	 * @param roughness Surface roughness in [0, 1].
	 * @return SH2 representing the specular lobe.
	 */
	SH2 FauxSpecularLobe(float3 N, float3 V, float roughness);

	/**
	 * @brief Estimate irradiance from SH2 by hallucinating a third-order zonal harmonic term.
	 * @param sh The SH2 radiance coefficients.
	 * @param direction The surface normal direction to evaluate irradiance for.
	 * @return The estimated non-negative irradiance value.
	 */
	float SHHallucinateZH3Irradiance(SH2 sh, float3 direction);

	/** @brief Estimate per-channel irradiance from SH2Color with hallucinated ZH3 terms. */
	float3 SHHallucinateZH3Irradiance(SH2Color sh, float3 direction);

	/**
	 * @brief Convert six directional ambient light colours (DALC) to second-order colour SH.
	 * @param dalcColors Array of 6 RGB colours for X+, X-, Y+, Y-, Z+, Z- directions.
	 * @return The SH2Color representation of the directional ambient lighting.
	 */
	SH2Color DALCToSH(const float3 dalcColors[6]);
}
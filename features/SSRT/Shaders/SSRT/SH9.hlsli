// AMD Capsaicin GI-1.1 — Spherical Harmonics (L0+L1+L2 = 9 coefficients)
// Ported from Capsaicin math/spherical_harmonics.hlsl

#ifndef SH9_HLSLI
#define SH9_HLSLI

#include "SSRT/GI1Common.hlsli"

// Compute SH9 basis coefficients for a direction
void SH_GetCoefficients(in float3 direction, out float coefficients[9])
{
	float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
	float pz2 = direction.z * direction.z;
	coefficients[0] = 0.2820947917738781f;
	coefficients[2] = 0.4886025119029199f * direction.z;
	coefficients[6] = 0.9461746957575601f * pz2 + -0.3153915652525201f;
	fC0 = direction.x;
	fS0 = direction.y;
	fTmpA = -0.48860251190292f;
	coefficients[3] = fTmpA * fC0;
	coefficients[1] = fTmpA * fS0;
	fTmpB = -1.092548430592079f * direction.z;
	coefficients[7] = fTmpB * fC0;
	coefficients[5] = fTmpB * fS0;
	fC1 = direction.x * fC0 - direction.y * fS0;
	fS1 = direction.x * fS0 + direction.y * fC0;
	fTmpC = 0.5462742152960395f;
	coefficients[8] = fTmpC * fC1;
	coefficients[4] = fTmpC * fS1;
}

// Clamped cosine SH coefficients for irradiance evaluation
void SH_GetCoefficients_ClampedCosine(in float3 cosine_lobe_dir, out float coefficients[9])
{
	const float CosineA0 = PI;
	const float CosineA1 = (2.0f * PI) / 3.0f;
	const float CosineA2 = PI / 4.0f;
	float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
	float pz2 = cosine_lobe_dir.z * cosine_lobe_dir.z;
	coefficients[0] = 0.2820947917738781f * CosineA0;
	coefficients[2] = 0.4886025119029199f * cosine_lobe_dir.z * CosineA1;
	coefficients[6] = 0.7431238683011271f * pz2 + -0.2477079561003757f;
	fC0 = cosine_lobe_dir.x;
	fS0 = cosine_lobe_dir.y;
	fTmpA = -0.48860251190292f;
	coefficients[3] = fTmpA * fC0 * CosineA1;
	coefficients[1] = fTmpA * fS0 * CosineA1;
	fTmpB = -1.092548430592079f * cosine_lobe_dir.z;
	coefficients[7] = fTmpB * fC0 * CosineA2;
	coefficients[5] = fTmpB * fS0 * CosineA2;
	fC1 = cosine_lobe_dir.x * fC0 - cosine_lobe_dir.y * fS0;
	fS1 = cosine_lobe_dir.x * fS0 + cosine_lobe_dir.y * fC0;
	fTmpC = 0.5462742152960395f;
	coefficients[8] = fTmpC * fC1 * CosineA2;
	coefficients[4] = fTmpC * fS1 * CosineA2;
}

// Clamped cosine cone SH for AO-modulated irradiance
void SH_GetCoefficients_ClampedCosine_Cone(in float3 cosine_lobe_dir, in float cone_theta_max, out float coefficients[9])
{
	float sin_theta_max, cos_theta_max;
	sincos(cone_theta_max, sin_theta_max, cos_theta_max);
	float sin_theta_max2 = sin_theta_max * sin_theta_max;
	float sin_theta_max3 = sin_theta_max2 * sin_theta_max;
	float cos_theta_max3 = cos_theta_max * cos_theta_max * cos_theta_max;

	float band1_factor = 1.023326707946489f * (1.0f - cos_theta_max3);
	float band2_factor = (4.0f - 3.0f * sin_theta_max3) * sin_theta_max2;

	coefficients[0] = 0.886226925452758f * sin_theta_max2;

	coefficients[1] = -band1_factor * cosine_lobe_dir.y;
	coefficients[2] = +band1_factor * cosine_lobe_dir.z;
	coefficients[3] = -band1_factor * cosine_lobe_dir.x;

	coefficients[4] = +0.8580855308097834f * band2_factor * cosine_lobe_dir.x * cosine_lobe_dir.y;
	coefficients[5] = -0.8580855308097834f * band2_factor * cosine_lobe_dir.y * cosine_lobe_dir.z;
	coefficients[6] = +0.2477079561003757f * band2_factor * (3.0f * cosine_lobe_dir.z * cosine_lobe_dir.z - 1.0f);
	coefficients[7] = -0.8580855308097834f * band2_factor * cosine_lobe_dir.x * cosine_lobe_dir.z;
	coefficients[8] = +0.4290427654048917f * band2_factor * (cosine_lobe_dir.x * cosine_lobe_dir.x - cosine_lobe_dir.y * cosine_lobe_dir.y);
}

#endif  // SH9_HLSLI

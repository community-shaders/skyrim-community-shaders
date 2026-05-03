#include "SphericalHarmonics.h"

using namespace SphericalHarmonics;

SH2 SphericalHarmonics::Evaluate(float3 dir)
{
	SH2 result;
	result.c0 = 0.28209479177387814347403972578039f;              // L=0 , M= 0
	result.c1[0] = -0.48860251190291992158638462283836f * dir.y;  // L=1 , M=-1
	result.c1[1] = 0.48860251190291992158638462283836f * dir.z;   // L=1 , M= 0
	result.c1[2] = -0.48860251190291992158638462283836f * dir.x;  // L=1 , M= 1
	return result;
}

float SphericalHarmonics::Dot(SH2 a, SH2 b)
{
	float4 aVec = float4(a.c0, a.c1[0], a.c1[1], a.c1[2]);
	float4 bVec = float4(b.c0, b.c1[0], b.c1[1], b.c1[2]);
	return aVec.Dot(bVec);
}

float3 SphericalHarmonics::Dot(SH2Color a, SH2Color b)
{
	return float3(
		Dot(a.r, b.r),
		Dot(a.g, b.g),
		Dot(a.b, b.b));
}

float SphericalHarmonics::Unproject(float3 dir, SH2 sh)
{
	SH2 basis = Evaluate(dir);
	return Dot(sh, basis);
}

float3 SphericalHarmonics::Unproject(float3 dir, SH2Color sh)
{
	return float3(
		Unproject(dir, sh.r),
		Unproject(dir, sh.g),
		Unproject(dir, sh.b));
}

SH2 SphericalHarmonics::EvaluateCosineLobe(float3 dir)
{
	SH2 result;
	result.c0 = 0.8862269254527580137f;              // L=0 , M= 0
	result.c1[0] = -1.0233267079464884885f * dir.y;  // L=1 , M=-1
	result.c1[1] = 1.0233267079464884885f * dir.z;   // L=1 , M= 0
	result.c1[2] = -1.0233267079464884885f * dir.x;  // L=1 , M= 1
	return result;
}

SH2 SphericalHarmonics::EvaluatePhaseHG(float3 dir, float g)
{
	SH2 result;
	const float factor = 0.48860251190291992158638462283836f * g;
	result.c0 = 0.28209479177387814347403972578039f;  // L=0 , M= 0
	result.c1[0] = -factor * dir.y;                   // L=1 , M=-1
	result.c1[1] = factor * dir.z;                    // L=1 , M= 0
	result.c1[2] = -factor * dir.x;                   // L=1 , M= 1
	return result;
}

SH2 SphericalHarmonics::Add(SH2 a, SH2 b)
{
	SH2 result;
	result.c0 = a.c0 + b.c0;
	result.c1[0] = a.c1[0] + b.c1[0];
	result.c1[1] = a.c1[1] + b.c1[1];
	result.c1[2] = a.c1[2] + b.c1[2];
	return result;
}

SH2Color SphericalHarmonics::Add(SH2Color a, SH2Color b)
{
	SH2Color result;
	result.r = Add(a.r, b.r);
	result.g = Add(a.g, b.g);
	result.b = Add(a.b, b.b);
	return result;
}

SH2 SphericalHarmonics::Scale(SH2 sh, float scale)
{
	SH2 result;
	result.c0 = sh.c0 * scale;
	result.c1[0] = sh.c1[0] * scale;
	result.c1[1] = sh.c1[1] * scale;
	result.c1[2] = sh.c1[2] * scale;
	return result;
}

SH2Color SphericalHarmonics::Scale(SH2Color sh, float scale)
{
	SH2Color result;
	result.r = Scale(sh.r, scale);
	result.g = Scale(sh.g, scale);
	result.b = Scale(sh.b, scale);
	return result;
}

SH2 SphericalHarmonics::Rotate(SH2 sh, float3x3 rotMatrix)
{
	SH2 result;
	result.c0 = sh.c0;

	float3 c1Vec = float3(sh.c1[0], sh.c1[1], sh.c1[2]);
	DirectX::XMVECTOR v = XMLoadFloat3(&c1Vec);
	v = DirectX::XMVector3Transform(v, XMLoadFloat3x3(&rotMatrix));
	DirectX::XMStoreFloat3(&c1Vec, v);

	result.c1[0] = c1Vec.x;
	result.c1[1] = c1Vec.y;
	result.c1[2] = c1Vec.z;
	return result;
}

SH2Color SphericalHarmonics::Rotate(SH2Color sh, float3x3 rotMatrix)
{
	SH2Color result;
	result.r = Rotate(sh.r, rotMatrix);
	result.g = Rotate(sh.g, rotMatrix);
	result.b = Rotate(sh.b, rotMatrix);
	return result;
}

float SphericalHarmonics::FuncProductIntegral(SH2 shL, SH2 shR)
{
	return Dot(shL, shR);
}

float3 SphericalHarmonics::FuncProductIntegral(SH2Color shL, SH2Color shR)
{
	return float3(
		FuncProductIntegral(shL.r, shR.r),
		FuncProductIntegral(shL.g, shR.g),
		FuncProductIntegral(shL.b, shR.b));
}

SH2 SphericalHarmonics::Product(SH2 shL, SH2 shR)
{
	const float factor = 1.0f / (2.0f * sqrt(3.14159265358979323846f));
	SH2 result;
	result.c0 = factor * Dot(shL, shR);
	result.c1[0] = factor * (shL.c0 * shR.c1[0] + shL.c1[0] * shR.c0);
	result.c1[1] = factor * (shL.c0 * shR.c1[1] + shL.c1[1] * shR.c0);
	result.c1[2] = factor * (shL.c0 * shR.c1[2] + shL.c1[2] * shR.c0);
	return result;
}

SH2Color SphericalHarmonics::Product(SH2Color shL, SH2Color shR)
{
	SH2Color result;
	result.r = Product(shL.r, shR.r);
	result.g = Product(shL.g, shR.g);
	result.b = Product(shL.b, shR.b);
	return result;
}

SH2 SphericalHarmonics::HanningConvolution(SH2 sh, float w)
{
	if (w <= 0)
		return sh;
	SH2 result;
	float invW = 1.0f / w;
	float factorBand1 = (1.0f + cos(3.14159265358979323846f * invW)) / 2.0f;
	result.c0 = sh.c0;
	result.c1[0] = sh.c1[0] * factorBand1;
	result.c1[1] = sh.c1[1] * factorBand1;
	result.c1[2] = sh.c1[2] * factorBand1;
	return result;
}

SH2Color SphericalHarmonics::HanningConvolution(SH2Color sh, float w)
{
	SH2Color result;
	result.r = HanningConvolution(sh.r, w);
	result.g = HanningConvolution(sh.g, w);
	result.b = HanningConvolution(sh.b, w);
	return result;
}

SH2 SphericalHarmonics::DiffuseConvolution(SH2 sh)
{
	SH2 result = sh;
	result.c0 *= 3.14159265358979323846f;
	result.c1[0] *= 2.0943951023931954923f;
	result.c1[1] *= 2.0943951023931954923f;
	result.c1[2] *= 2.0943951023931954923f;
	return result;
}

SH2Color SphericalHarmonics::DiffuseConvolution(SH2Color sh)
{
	SH2Color result;
	result.r = DiffuseConvolution(sh.r);
	result.g = DiffuseConvolution(sh.g);
	result.b = DiffuseConvolution(sh.b);
	return result;
}

template <typename T>
static T reflect(const T& i, const T& n)
{
	return i - n * (2.0f * i.Dot(n));
}

template <typename T>
static T normalize(const T& v)
{
	const float len2 = v.Dot(v);
	if (len2 <= 1e-8f) {
		return v;
	}
	return v * (1.0f / sqrtf(len2));
}

// Author: ProfJack
// Constructs the SH of an approximate specular lobe
SH2 SphericalHarmonics::FauxSpecularLobe(float3 N, float3 V, float roughness)
{
	// https://www.gdcvault.com/play/1026701/Fast-Denoising-With-Self-Stabilizing
	// get dominant ggx reflection direction
	roughness = std::clamp(roughness, 0.0f, 1.0f);
	float f = (1 - roughness) * (sqrt(1 - roughness) + roughness);
	float3 R = reflect(-V, N);
	float3 D = R * f + N * (1 - f);
	float3 dominantDir = normalize(D);

	// lobe half angle
	// credit: Olivier Therrien
	float roughness2 = roughness * roughness;
	float halfAngle = std::clamp(4.1679f * roughness2 * roughness2 - 9.0127f * roughness2 * roughness + 4.6161f * roughness2 + 1.7048f * roughness + 0.1f, 0.0f, 3.14159265358979323846f / 2.0f);
	float lerpFactor = halfAngle / (3.14159265358979323846f / 2.0f);
	SH2 directional = Evaluate(dominantDir);
	SH2 cosineLobe = Scale(EvaluateCosineLobe(dominantDir), 1.0f / 3.14159265358979323846f);
	return Add(Scale(directional, lerpFactor), Scale(cosineLobe, 1.0f - lerpFactor));
}

float SphericalHarmonics::SHHallucinateZH3Irradiance(SH2 sh, float3 direction)
{
	float3 zonalAxis = normalize(float3(sh.c1[2], sh.c1[0], sh.c1[1]));
	float ratio = 0.0;
	ratio = abs(zonalAxis.Dot(float3(-sh.c1[2], -sh.c1[0], sh.c1[1])));
	ratio /= std::max(1e-8f, sh.c0);
	float zonalL2Coeff = sh.c0 * (0.08f * ratio + 0.6f * ratio * ratio);  // Curve-fit; Section 3.4.3
	float fZ = zonalAxis.Dot(direction);
	float zhDir = sqrt(5.0f / (16.0f * 3.14159265358979323846f)) * (3.0f * fZ * fZ - 1.0f);
	// Convolve inSH with the normalized cosine kernel (multiply the L1 band by the zonal scale 2/3), then dot with
	// inSH(direction) for linear inSH (Equation 5).
	float result = SphericalHarmonics::FuncProductIntegral(sh, SphericalHarmonics::EvaluateCosineLobe(direction));
	// Add irradiance from the ZH3 term. zonalL2Coeff is the ZH3 coefficient for a radiance signal, so we need to
	// multiply by 1/4 (the L2 zonal scale for a normalized clamped cosine kernel) to evaluate irradiance.
	result += 0.25f * zonalL2Coeff * zhDir;
	return std::max(0.0f, result);
}

float3 SphericalHarmonics::SHHallucinateZH3Irradiance(SH2Color sh, float3 direction)
{
	return float3(
		SHHallucinateZH3Irradiance(sh.r, direction),
		SHHallucinateZH3Irradiance(sh.g, direction),
		SHHallucinateZH3Irradiance(sh.b, direction));
}

SH2Color SphericalHarmonics::DALCToSH(const float3 dalcColors[6])
{
	SH2Color result;

	const float weight = (4.0f * 3.14159265358979323846f) / 6.0f;

	float3 dirs[6] = {
		float3(1, 0, 0),   // X+
		float3(-1, 0, 0),  // X-
		float3(0, 1, 0),   // Y+
		float3(0, -1, 0),  // Y-
		float3(0, 0, 1),   // Z+
		float3(0, 0, -1)   // Z-
	};

	for (int i = 0; i < 6; i++) {
		SH2 shBasis = Evaluate(dirs[i]);
		result.r = Add(result.r, Scale(shBasis, dalcColors[i].x * weight));
		result.g = Add(result.g, Scale(shBasis, dalcColors[i].y * weight));
		result.b = Add(result.b, Scale(shBasis, dalcColors[i].z * weight));
	}
	return result;
}

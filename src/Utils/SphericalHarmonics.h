#pragma once
using float3x3 = DirectX::XMFLOAT3X3;

namespace SphericalHarmonics
{
	struct SH2
	{
		float c0;
		float c1[3];

		SH2() : c0(0.0f), c1{ 0.0f, 0.0f, 0.0f } {}
		SH2(float _c0, float _c1_0, float _c1_1, float _c1_2) : c0(_c0), c1{ _c1_0, _c1_1, _c1_2 } {}
	};

	struct SH2Color
	{
		SH2 r;
		SH2 g;
		SH2 b;

		SH2Color() : r(), g(), b() {}

		explicit SH2Color(const SH2& sh) : r(sh), g(sh), b(sh) {}

		SH2Color(SH2 _r, SH2 _g, SH2 _b) : r(_r), g(_g), b(_b) {}
	};

	SH2 Evaluate(float3 dir);
	float Dot(SH2 a, SH2 b);
	float3 Dot(SH2Color a, SH2Color b);
	float Unproject(float3 dir, SH2 sh);
	float3 Unproject(float3 dir, SH2Color sh);
	SH2 EvaluateCosineLobe(float3 dir);
	SH2 EvaluatePhaseHG(float3 dir, float g);
	SH2 Add(SH2 a, SH2 b);
	SH2Color Add(SH2Color a, SH2Color b);
	SH2 Scale(SH2 sh, float scale);
	SH2Color Scale(SH2Color sh, float scale);
	SH2 Rotate(SH2 sh, float3x3 rotMatrix);
	SH2Color Rotate(SH2Color sh, float3x3 rotMatrix);
	float FuncProductIntegral(SH2 shL, SH2 shR);
	float3 FuncProductIntegral(SH2Color shL, SH2Color shR);
	SH2 Product(SH2 shL, SH2 shR);
	SH2Color Product(SH2Color shL, SH2Color shR);
	SH2 HanningConvolution(SH2 sh, float w);
	SH2Color HanningConvolution(SH2Color sh, float w);
	SH2 DiffuseConvolution(SH2 sh);
	SH2Color DiffuseConvolution(SH2Color sh);
	SH2 FauxSpecularLobe(float3 N, float3 V, float roughness);
	float SHHallucinateZH3Irradiance(SH2 sh, float3 direction);
	float3 SHHallucinateZH3Irradiance(SH2Color sh, float3 direction);

	SH2Color DALCToSH(const float3 dalcColors[6]);
}
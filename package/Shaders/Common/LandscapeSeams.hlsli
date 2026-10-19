#ifndef __LANDSCAPE_SEAMS_HLSLI__
#define __LANDSCAPE_SEAMS_HLSLI__

// Borrowed layers blend color, normal and material only; parallax height stays on the engine's six layers.
namespace LandscapeSeams
{
	struct QuadData
	{
		float2 Origin;
		uint Flags;
		uint ExtraCount;
		float2 IsSnow;
		float2 SpecPower;
		float4 PBRParams[2];
		float4 GlintParams[2];
	};

	static const uint FlagPbr = 1u << 0;
	static const uint FlagGlint = 1u << 8;
	static const uint FlagValid = 1u << 31;

	static const float QuadSize = 2048.0;
	static const float GridCells = 16.0;

	Texture2DArray<float4> Weights : register(t104);
	StructuredBuffer<QuadData> Quad : register(t105);

	Texture2D<float4> Color0 : register(t106);
	Texture2D<float4> Normal0 : register(t107);
	Texture2D<float4> Rmaos0 : register(t108);

	Texture2D<float4> Color1 : register(t109);
	Texture2D<float4> Normal1 : register(t110);
	Texture2D<float4> Rmaos1 : register(t111);

	static QuadData Data;
	static float2 ExtraWeights = 0;

	bool HasFlag(uint flag, uint layer)
	{
		return (Data.Flags & (flag << layer)) != 0;
	}

	float4 FetchSlice(int2 cell, float2 blend, int slice)
	{
		float4 w00 = Weights.Load(int4(cell, slice, 0));
		float4 w10 = Weights.Load(int4(cell + int2(1, 0), slice, 0));
		float4 w01 = Weights.Load(int4(cell + int2(0, 1), slice, 0));
		float4 w11 = Weights.Load(int4(cell + int2(1, 1), slice, 0));
		return lerp(lerp(w00, w10, blend.x), lerp(w01, w11, blend.x), blend.y);
	}

	void Load(float2 worldXY, inout float4 weights1, inout float4 weights2)
	{
		Data = Quad[0];
		ExtraWeights = 0;
		[branch] if ((Data.Flags & FlagValid) != 0)
		{
			float2 grid = saturate((worldXY - Data.Origin) / QuadSize) * GridCells;
			float2 cellF = min(floor(grid), GridCells - 1.0);
			float2 blend = grid - cellF;
			int2 cell = int2(cellF);

			float4 slice0 = FetchSlice(cell, blend, 0);
			float4 slice1 = FetchSlice(cell, blend, 1);

			weights1 = slice0;
			weights2.xy = slice1.xy;
			ExtraWeights = slice1.zw;
		}
	}
}

#if defined(TRUE_PBR)
#	define LANDSCAPE_SEAMS_BLEND_EXTRA(INDEX, COLOR_TEX, NORM_TEX, RMAOS_TEX, WEIGHT)                                                                                                     \
		[branch] if ((WEIGHT) > 0.01)                                                                                                                                                      \
		{                                                                                                                                                                                  \
			float weight = WEIGHT;                                                                                                                                                         \
			float4 landColor = SampleTerrain(COLOR_TEX, SampColorSampler, uv, sharedOffset);                                                                                               \
			float3 landColorRGB = landColor.rgb;                                                                                                                                           \
			[branch] if (!LandscapeSeams::HasFlag(LandscapeSeams::FlagPbr, INDEX))                                                                                                         \
			{                                                                                                                                                                              \
				landColorRGB = Color::SrgbToLinear(landColorRGB / Color::PBRLightingScale);                                                                                                \
			}                                                                                                                                                                              \
			float landAlpha = landColor.a;                                                                                                                                                 \
			float4 landNormal = SampleTerrain(NORM_TEX, SampColorSampler, uv, sharedOffset);                                                                                               \
			float3 landNormalRGB = landNormal.rgb;                                                                                                                                         \
			float landNormalAlpha = landNormal.a;                                                                                                                                          \
			float4 landRMAOS;                                                                                                                                                              \
			[branch] if (LandscapeSeams::HasFlag(LandscapeSeams::FlagPbr, INDEX))                                                                                                          \
			{                                                                                                                                                                              \
				landRMAOS = SampleTerrain(RMAOS_TEX, SampColorSampler, uv, sharedOffset) * float4(LandscapeSeams::Data.PBRParams[INDEX].x, 1, 1, LandscapeSeams::Data.PBRParams[INDEX].z); \
				[branch] if (LandscapeSeams::HasFlag(LandscapeSeams::FlagGlint, INDEX))                                                                                                    \
				{                                                                                                                                                                          \
					glintParameters += weight * LandscapeSeams::Data.GlintParams[INDEX];                                                                                                   \
				}                                                                                                                                                                          \
			}                                                                                                                                                                              \
			else                                                                                                                                                                           \
			{                                                                                                                                                                              \
				landRMAOS = float4(1 - landNormalAlpha, 0, 1, 0);                                                                                                                          \
			}                                                                                                                                                                              \
			blendedRMAOS += landRMAOS * weight;                                                                                                                                            \
			blendedRGB += landColorRGB * weight;                                                                                                                                           \
			blendedAlpha += landAlpha * weight;                                                                                                                                            \
			blendedNormalRGB += landNormalRGB * weight;                                                                                                                                    \
			blendedNormalAlpha += landNormalAlpha * weight;                                                                                                                                \
		}
#else
#	define LANDSCAPE_SEAMS_BLEND_EXTRA(INDEX, COLOR_TEX, NORM_TEX, RMAOS_TEX, WEIGHT) \
		LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(INDEX, COLOR_TEX, SampColorSampler, NORM_TEX, SampColorSampler, WEIGHT, LandscapeSeams::Data.IsSnow[INDEX])
#endif

#define LANDSCAPE_SEAMS_BLEND_EXTRAS                                                                                                        \
	LANDSCAPE_SEAMS_BLEND_EXTRA(0, LandscapeSeams::Color0, LandscapeSeams::Normal0, LandscapeSeams::Rmaos0, LandscapeSeams::ExtraWeights.x) \
	LANDSCAPE_SEAMS_BLEND_EXTRA(1, LandscapeSeams::Color1, LandscapeSeams::Normal1, LandscapeSeams::Rmaos1, LandscapeSeams::ExtraWeights.y)

#endif

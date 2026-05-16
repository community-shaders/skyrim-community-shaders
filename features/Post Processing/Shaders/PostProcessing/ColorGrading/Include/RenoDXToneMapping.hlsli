#ifndef COLORGRADING_RENODX_TONE_MAPPING_HLSLI
#define COLORGRADING_RENODX_TONE_MAPPING_HLSLI

#include "PostProcessing/ColorGrading/Include/renodx/tonemap/aces.hlsl"
#include "PostProcessing/ColorGrading/Include/renodx/tonemap/frostbite.hlsl"
#include "PostProcessing/ColorGrading/Include/renodx/tonemap/hermite_spline.hlsl"
#include "PostProcessing/ColorGrading/Include/renodx/tonemap/neutwo.hlsl"
#include "PostProcessing/ColorGrading/Include/renodx/tonemap/psychov_17.hlsl"

namespace renodx
{
	namespace color
	{
		namespace ictcp
		{
			namespace from
			{
				float3 BT2020(float3 bt2020Color, float scaling = 100.0f)
				{
					float3 lms = mul(mul(renodx::color::ictcp::XYZ_TO_DOLBY_LMS_MAT, renodx::color::BT2020_TO_XYZ_MAT), bt2020Color);
					float3 plms = renodx::color::pq::Encode(max(0.0f, lms), scaling);
					return mul(renodx::color::ictcp::PLMS_TO_ICTCP_MAT, plms);
				}
			}
		}

		namespace bt2020
		{
			namespace from
			{
				float3 ICtCp(float3 ictcpColor, float scaling = 100.0f)
				{
					float3 plms = mul(renodx::math::Invert3x3(renodx::color::ictcp::PLMS_TO_ICTCP_MAT), ictcpColor);
					float3 lms = renodx::color::pq::Decode(plms, scaling);
					return mul(
						mul(
							renodx::math::Invert3x3(renodx::color::BT2020_TO_XYZ_MAT),
							renodx::math::Invert3x3(renodx::color::ictcp::XYZ_TO_DOLBY_LMS_MAT)),
						lms);
				}
			}
		}
	}
}

namespace ColorGradingRenoDX
{
	float3 NeutwoBT2020(float3 color, float peak, float clipPoint)
	{
		return renodx::tonemap::neutwo::BT2020(color, peak, clipPoint);
	}

	float3 ACESBT2020(float3 color, float minY, float maxY)
	{
		color = mul(renodx::color::BT2020_TO_AP1_MAT, color);
		color = renodx::tonemap::aces::GamutCompress(color);
		color = mul(renodx::color::AP1_TO_AP0_MAT, color);
		color = renodx::tonemap::aces::RRT(color);
		return renodx::tonemap::aces::ODT(color, minY, maxY, renodx::color::AP1_TO_BT2020_MAT);
	}

	float3 FrostbiteBT2020(float3 color, float maxValue, float rolloffStart, float saturationBoostAmount, float hueCorrectAmount)
	{
		float3 ictcp = renodx::color::ictcp::from::BT2020(color);

		float saturationAmount = pow(smoothstep(1.0f, 0.3f, ictcp.x), 1.3f);
		color = renodx::color::bt2020::from::ICtCp(ictcp * float3(1.0f, saturationAmount, saturationAmount));

		float maxCol = max(color.x, max(color.y, color.z));
		float mappedMax = renodx::tonemap::frostbite::RangeCompress(maxCol, rolloffStart, maxValue);
		float3 compressedHuePreserving = color * mappedMax / maxCol;
		float3 perChannelCompressed = renodx::tonemap::frostbite::RangeCompress(color, rolloffStart, maxValue);
		color = lerp(perChannelCompressed, compressedHuePreserving, hueCorrectAmount);

		float3 ictcpMapped = renodx::color::ictcp::from::BT2020(color);
		float postCompressionSaturationBoost = saturationBoostAmount * smoothstep(1.0f, 0.5f, ictcp.x);
		ictcpMapped.yz = lerp(ictcpMapped.yz, ictcp.yz * ictcpMapped.x / max(1e-3f, ictcp.x), postCompressionSaturationBoost);

		return renodx::color::bt2020::from::ICtCp(ictcpMapped);
	}

	float3 HermiteSplineBT2020(float3 color, float targetWhite, float maxWhite, float targetBlack, float minBlack, float nits)
	{
		float y = renodx::color::y::from::BT2020(color);
		float newY = renodx::tonemap::HermiteSplineLuminanceRolloff(y, targetWhite, maxWhite, targetBlack, minBlack, nits);
		return renodx::color::correct::Luminance(color, y, newY);
	}
}

#endif  // COLORGRADING_RENODX_TONE_MAPPING_HLSLI

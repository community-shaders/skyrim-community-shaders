#ifndef RENODX_SHADERS_TONEMAP_NEUTWO_HLSL_
#define RENODX_SHADERS_TONEMAP_NEUTWO_HLSL_

/*
 * Copyright (C) 2026 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

namespace renodx
{

	namespace math
	{

		float Max(float3 value)
		{
			return max(max(value.r, value.g), value.b);
		}

	}  // namespace math

	namespace color
	{

		static const float3x3 NTSC_U_1953_TO_XYZ_MAT = float3x3(
			0.60689090f, 0.17350110f, 0.20034800f,
			0.29891640f, 0.58659900f, 0.11448450f,
			0.00000000f, 0.06609570f, 1.11622430f);

		static const float3x3 BT709_TO_XYZ_MAT = float3x3(
			0.41239080f, 0.35758434f, 0.18048079f,
			0.21263901f, 0.71516868f, 0.07219232f,
			0.01933082f, 0.11919478f, 0.95053215f);

		static const float3x3 BT2020_TO_XYZ_MAT = float3x3(
			0.63695805f, 0.14461690f, 0.16888098f,
			0.26270021f, 0.67799807f, 0.05930172f,
			0.00000000f, 0.02807269f, 1.06098506f);

		static const float3x3 AP1_TO_XYZ_MAT = float3x3(
			0.66245418f, 0.13400421f, 0.15618769f,
			0.27222872f, 0.67408177f, 0.05368952f,
			-0.00557465f, 0.00406073f, 1.01033910f);

		namespace y
		{
			namespace from
			{

				float XYZMatrix(float3 color, float3x3 toXYZMatrix)
				{
					return dot(color, toXYZMatrix[1].rgb);
				}

				float NTSC1953(float3 ntsc)
				{
					return XYZMatrix(ntsc, NTSC_U_1953_TO_XYZ_MAT);
				}

				float BT709(float3 bt709)
				{
					return XYZMatrix(bt709, BT709_TO_XYZ_MAT);
				}

				float BT2020(float3 bt2020)
				{
					return XYZMatrix(bt2020, BT2020_TO_XYZ_MAT);
				}

				float AP1(float3 ap1)
				{
					return XYZMatrix(ap1, AP1_TO_XYZ_MAT);
				}

			}  // namespace from
		}  // namespace y

	}  // namespace color

	namespace tonemap
	{

		// Neutral tonemap
		// Based on power of 2 (squared/sqrt)
		// Naka-Rushton/Reinhard style tonemapper x/(x^2+k)^(1/2)
		// Newton-Raphson friendly with rsqrt (faster than division)
		// f'''(x) = 0 at x = 0.5 (half peak)
		// https://www.desmos.com/calculator/gy1edro6nd
		// Polar/Cartesian form of peak * cos(atan2(x, peak))
		// Invertible with same complexity as forward

		// f\left(x\right)=\frac{x}{\sqrt{xx+1}}
		float Neutwo(float x)
		{
			// also written as x * rhypot(x, 1.0)
			float numerator = x;
			float denominator_squared = mad(x, x, 1.0);
			return numerator * rsqrt(denominator_squared);
		}

		// f_{p}\left(x\right)=\frac{px}{\sqrt{xx+pp}}
		float Neutwo(float x, float peak)
		{
			// also written as x * rhypot(x, peak)
			float p = peak;

			float numerator = p * x;
			float denominator_squared = mad(x, x, p * p);
			return numerator * rsqrt(denominator_squared);
		}

		// f_{c}\left(x\right)=\frac{cpx}{\sqrt{xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
		float Neutwo(float x, float peak, float clip)
		{
			float p = peak;
			float c = clip;
			float cc = c * c;
			float pp = p * p;
			float xx = x * x;

			float numerator = c * p * x;
			float denominator_squared = mad(xx, (cc - pp), cc * pp);

			return numerator * rsqrt(denominator_squared);
		}

		// f_{g}\left(x\right)=\frac{pgx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot gg\cdot\left(xx\cdot\left(cc-pp\right)+cc\cdot\left(pp-gg\right)\right)}}
		float Neutwo(float x, float peak, float clip, float gray)
		{
			float p = peak;
			float g = gray;
			float c = clip;

			float cc = c * c;
			float pp = p * p;
			float gg = g * g;
			float xx = x * x;
			float cc_minus_gg = cc - gg;

			float numerator = p * g * x * cc_minus_gg;
			float denominator_squared = cc_minus_gg * gg * (mad(xx, (cc - pp), cc * (pp - gg)));
			return numerator * rsqrt(denominator_squared);
		}

		// f_{o}\left(x\right)=\frac{pox\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(xx\cdot\left(ccoo-ppgg\right)+ccgg\cdot\left(pp-oo\right)\right)}}
		float Neutwo(float x, float peak, float clip, float gray_in, float gray_out)
		{
			float p = peak;
			float g = gray_in;
			float o = gray_out;

			float cc = clip * clip;
			float pp = peak * peak;
			float gg = g * g;
			float oo = o * o;
			float xx = x * x;

			float cc_minus_gg = cc - gg;

			float numerator = p * o * x * cc_minus_gg;

			float ccoo = cc * oo;
			float ppgg = pp * gg;
			float ccgg = cc * gg;

			float denominator_squared = cc_minus_gg * mad(xx, (ccoo - ppgg), ccgg * (pp - oo));

			return numerator * rsqrt(denominator_squared);
		}

		// f_{m}\left(x\right)=\frac{qzx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(xx\cdot\left(cczz-qqgg\right)+ccgg\cdot\left(qq-zz\right)\right)}}+m
		float Neutwo(float x, float peak, float clip, float gray_in, float gray_out, float minimum)
		{
			float m = minimum;
			float g = gray_in;
			float z = gray_out - m;
			float q = peak - m;
			float c = clip;

			float cc = c * c;
			float gg = g * g;
			float cc_minus_gg = cc - gg;

			float numerator = q * z * x * cc_minus_gg;

			float xx = x * x;
			float zz = z * z;
			float qq = q * q;

			float cczz = cc * zz;
			float qqgg = qq * gg;
			float ccgg = cc * gg;

			float denominator_squared = cc_minus_gg * mad(xx, (cczz - qqgg), ccgg * (qq - zz));

			return mad(numerator, rsqrt(denominator_squared), m);
		}

		namespace neutwo
		{

			float ComputeBT709Scale(float3 color)
			{
				float y = renodx::color::y::from::BT709(color);
				float new_y = renodx::tonemap::Neutwo(y);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeBT709Scale(float3 color, float peak)
			{
				float y = renodx::color::y::from::BT709(color);
				float new_y = renodx::tonemap::Neutwo(y, peak);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeBT709Scale(float3 color, float peak, float clip)
			{
				float y = renodx::color::y::from::BT709(color);
				float new_y = renodx::tonemap::Neutwo(y, peak, clip);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeBT2020Scale(float3 color)
			{
				float y = renodx::color::y::from::BT2020(color);
				float new_y = renodx::tonemap::Neutwo(y);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeBT2020Scale(float3 color, float peak)
			{
				float y = renodx::color::y::from::BT2020(color);
				float new_y = renodx::tonemap::Neutwo(y, peak);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeBT2020Scale(float3 color, float peak, float clip)
			{
				float y = renodx::color::y::from::BT2020(color);
				float new_y = renodx::tonemap::Neutwo(y, peak, clip);
				float scale = y != 0 ? (new_y / y) : 1.f;
				return scale;
			}

			float ComputeMaxChannelScale(float3 color)
			{
				float max_channel = renodx::math::Max(abs(color.rgb));
				float new_max = renodx::tonemap::Neutwo(max_channel);
				float scale = max_channel != 0 ? (new_max / max_channel) : 1.f;
				return scale;
			}

			float ComputeMaxChannelScale(float3 color, float peak)
			{
				float max_channel = renodx::math::Max(abs(color.rgb));
				float new_max = renodx::tonemap::Neutwo(max_channel, peak);
				float scale = max_channel != 0 ? (new_max / max_channel) : 1.f;
				return scale;
			}

			float ComputeMaxChannelScale(float3 color, float peak, float clip)
			{
				float max_channel = renodx::math::Max(abs(color.rgb));
				float new_max = renodx::tonemap::Neutwo(max_channel, peak, clip);
				float scale = max_channel != 0 ? (new_max / max_channel) : 1.f;
				return scale;
			}

			float3 BT709(float3 color)
			{
				return color * ComputeBT709Scale(color);
			}

			float3 BT709(float3 color, float peak)
			{
				return color * ComputeBT709Scale(color, peak);
			}

			float3 BT709(float3 color, float peak, float clip)
			{
				return color * ComputeBT709Scale(color, peak, clip);
			}

			float3 BT2020(float3 color)
			{
				return color * ComputeBT2020Scale(color);
			}

			float3 BT2020(float3 color, float peak)
			{
				return color * ComputeBT2020Scale(color, peak);
			}

			float3 BT2020(float3 color, float peak, float clip)
			{
				return color * ComputeBT2020Scale(color, peak, clip);
			}

			float3 MaxChannel(float3 color)
			{
				return color * ComputeMaxChannelScale(color);
			}

			float3 MaxChannel(float3 color, float peak)
			{
				return color * ComputeMaxChannelScale(color, peak);
			}

			float3 MaxChannel(float3 color, float peak, float clip)
			{
				return color * ComputeMaxChannelScale(color, peak, clip);
			}

			float3 PerChannel(float3 color)
			{
				return float3(renodx::tonemap::Neutwo(color.r),
					renodx::tonemap::Neutwo(color.g),
					renodx::tonemap::Neutwo(color.b));
			}

			float3 PerChannel(float3 color, float3 peak)
			{
				return float3(renodx::tonemap::Neutwo(color.r, peak.r),
					renodx::tonemap::Neutwo(color.g, peak.g),
					renodx::tonemap::Neutwo(color.b, peak.b));
			}

			float3 PerChannel(float3 color, float3 peak, float3 clip)
			{
				return float3(renodx::tonemap::Neutwo(color.r, peak.r, clip.r),
					renodx::tonemap::Neutwo(color.g, peak.g, clip.g),
					renodx::tonemap::Neutwo(color.b, peak.b, clip.b));
			}
		}

		namespace inverse
		{
			// f_{i}\left(x\right)=\frac{x}{\sqrt{-xx+1}}
			float Neutwo(float x)
			{
				float numerator = x;
				float denominator_squared = mad(-x, x, 1.0);
				return numerator * rsqrt(denominator_squared);
			}

			// f_{pi}\left(x\right)=\frac{px}{\sqrt{-xx+pp}}
			float Neutwo(float x, float peak)
			{
				float p = peak;

				float numerator = p * x;
				float denominator_squared = mad(-x, x, p * p);
				return numerator * rsqrt(denominator_squared);
			}

			// f_{ci}\left(x\right)=\frac{cpx}{\sqrt{-xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
			float Neutwo(float x, float peak, float clip)
			{
				float p = peak;
				float c = clip;
				float cc = c * c;
				float pp = p * p;
				float xx = x * x;

				float numerator = c * p * x;
				float denominator_squared = mad(-xx, (cc - pp), cc * pp);

				return numerator * rsqrt(denominator_squared);
			}

			// f_{gi}\left(x\right)=\frac{pgx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot gg\cdot\left(-xx\cdot\left(cc-pp\right)+cc\cdot\left(pp-gg\right)\right)}}
			float Neutwo(float x, float peak, float clip, float gray)
			{
				float p = peak;
				float g = gray;
				float c = clip;

				float cc = c * c;
				float pp = p * p;
				float gg = g * g;
				float xx = x * x;
				float cc_minus_gg = cc - gg;

				float numerator = p * g * x * cc_minus_gg;
				float denominator_squared = cc_minus_gg * gg * (mad(-xx, (cc - pp), cc * (pp - gg)));
				return numerator * rsqrt(denominator_squared);
			}

			// f_{oi}\left(x\right)=\frac{pox\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(-xx\cdot\left(ccoo-ppgg\right)+ccgg\cdot\left(pp-oo\right)\right)}}
			float Neutwo(float x, float peak, float clip, float gray_in, float gray_out)
			{
				float p = peak;
				float g = gray_in;
				float o = gray_out;

				float cc = clip * clip;
				float pp = peak * peak;
				float gg = g * g;
				float oo = o * o;
				float xx = x * x;

				float cc_minus_gg = cc - gg;

				float numerator = p * o * x * cc_minus_gg;

				float ccoo = cc * oo;
				float ppgg = pp * gg;
				float ccgg = cc * gg;

				float denominator_squared = cc_minus_gg * mad(-xx, (ccoo - ppgg), ccgg * (pp - oo));

				return numerator * rsqrt(denominator_squared);
			}

			// f_{mi}\left(x\right)=\frac{qzx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(-xx\cdot\left(cczz-qqgg\right)+ccgg\cdot\left(qq-zz\right)\right)}}-m
			float Neutwo(float x, float peak, float clip, float gray_in, float gray_out, float minimum)
			{
				float m = minimum;
				float g = gray_in;
				float z = gray_out - m;
				float q = peak - m;
				float c = clip;

				float cc = c * c;
				float gg = g * g;
				float cc_minus_gg = cc - gg;

				float numerator = q * z * x * cc_minus_gg;

				float xx = x * x;
				float zz = z * z;
				float qq = q * q;

				float cczz = cc * zz;
				float qqgg = qq * gg;
				float ccgg = cc * gg;

				float denominator_squared = cc_minus_gg * mad(-xx, (cczz - qqgg), ccgg * (qq - zz));

				return mad(numerator, rsqrt(denominator_squared), -m);
			}

			namespace neutwo
			{
				float3 BT709(float3 color)
				{
					float y = renodx::color::y::from::BT709(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}

				float3 BT709(float3 color, float peak)
				{
					float y = renodx::color::y::from::BT709(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y, peak);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}

				float3 BT709(float3 color, float peak, float clip)
				{
					float y = renodx::color::y::from::BT709(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y, peak, clip);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}

				float3 BT2020(float3 color)
				{
					float y = renodx::color::y::from::BT2020(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}
				float3 BT2020(float3 color, float peak)
				{
					float y = renodx::color::y::from::BT2020(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y, peak);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}
				float3 BT2020(float3 color, float peak, float clip)
				{
					float y = renodx::color::y::from::BT2020(color);
					float new_y = renodx::tonemap::inverse::Neutwo(y, peak, clip);
					float scale = y != 0 ? (new_y / y) : 0.f;
					return color * scale;
				}

				float3 MaxChannel(float3 color)
				{
					float max_channel = max(max(abs(color.r), abs(color.g)), abs(color.b));
					float new_max = renodx::tonemap::inverse::Neutwo(max_channel);
					float scale = max_channel != 0 ? (new_max / max_channel) : 0.f;
					return color * scale;
				}
				float3 MaxChannel(float3 color, float peak)
				{
					float max_channel = max(max(abs(color.r), abs(color.g)), abs(color.b));
					float new_max = renodx::tonemap::inverse::Neutwo(max_channel, peak);
					float scale = max_channel != 0 ? (new_max / max_channel) : 0.f;
					return color * scale;
				}
				float3 MaxChannel(float3 color, float peak, float clip)
				{
					float max_channel = max(max(abs(color.r), abs(color.g)), abs(color.b));
					float new_max = renodx::tonemap::inverse::Neutwo(max_channel, peak, clip);
					float scale = max_channel != 0 ? (new_max / max_channel) : 0.f;
					return color * scale;
				}
				float3 PerChannel(float3 color)
				{
					return float3(renodx::tonemap::inverse::Neutwo(color.r),
						renodx::tonemap::inverse::Neutwo(color.g),
						renodx::tonemap::inverse::Neutwo(color.b));
				}
				float3 PerChannel(float3 color, float3 peak)
				{
					return float3(renodx::tonemap::inverse::Neutwo(color.r, peak.r),
						renodx::tonemap::inverse::Neutwo(color.g, peak.g),
						renodx::tonemap::inverse::Neutwo(color.b, peak.b));
				}
				float3 PerChannel(float3 color, float3 peak, float3 clip)
				{
					return float3(renodx::tonemap::inverse::Neutwo(color.r, peak.r, clip.r),
						renodx::tonemap::inverse::Neutwo(color.g, peak.g, clip.g),
						renodx::tonemap::inverse::Neutwo(color.b, peak.b, clip.b));
				}
			}  // namespace neutwo
		}  // namespace inverse

	}  // namespace tonemap

}  // namespace renodx

#endif  // RENODX_SHADERS_TONEMAP_NEUTWO_HLSL_

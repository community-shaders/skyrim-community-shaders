#ifndef SRC_SHADERS_RENODX_HLSL_
#define SRC_SHADERS_RENODX_HLSL_

namespace renodx
{

	namespace math
	{

#if __SHADER_TARGET_MAJOR <= 5
#	define SIGN_FUNCTION_GENERATOR(T)                                          \
		T Sign(T x)                                                             \
		{                                                                       \
			return mad(saturate(mad(x, asfloat(0x7F7FFFFF), 0.5f)), 2.f, -1.f); \
		}
#else
#	define SIGN_FUNCTION_GENERATOR(T) \
		T Sign(T x)                    \
		{                              \
			return sign(x);            \
		}
#endif

		SIGN_FUNCTION_GENERATOR(float);
		SIGN_FUNCTION_GENERATOR(float2);
		SIGN_FUNCTION_GENERATOR(float3);
		SIGN_FUNCTION_GENERATOR(float4);

#undef SIGN_FUNCTION_GENERATOR

#define SIGNPOW_FUNCTION_GENERATOR(struct)      \
	struct SignPow(struct x, float exponent)    \
	{                                           \
		return Sign(x) * pow(abs(x), exponent); \
	}

		SIGNPOW_FUNCTION_GENERATOR(float);
		SIGNPOW_FUNCTION_GENERATOR(float2);
		SIGNPOW_FUNCTION_GENERATOR(float3);
		SIGNPOW_FUNCTION_GENERATOR(float4);
#undef SIGNPOW_FUNCTION_GENERATOR

#define SIGNSQRT_FUNCTION_GENERATOR(struct) \
	struct SignSqrt(struct x)               \
	{                                       \
		return Sign(x) * sqrt(abs(x));      \
	}

		SIGNSQRT_FUNCTION_GENERATOR(float);
		SIGNSQRT_FUNCTION_GENERATOR(float2);
		SIGNSQRT_FUNCTION_GENERATOR(float3);
		SIGNSQRT_FUNCTION_GENERATOR(float4);
#undef SIGNSQRT_FUNCTION_GENERATOR

#define CBRT_FUNCTION_GENERATOR(struct) \
	struct Cbrt(struct x)               \
	{                                   \
		return SignPow(x, 1.f / 3.f);   \
	}

		CBRT_FUNCTION_GENERATOR(float);
		CBRT_FUNCTION_GENERATOR(float2);
		CBRT_FUNCTION_GENERATOR(float3);
		CBRT_FUNCTION_GENERATOR(float4);
#undef CBRT_FUNCTION_GENERATOR

		float Average(float3 color)
		{
			return (color.x + color.y + color.z) / 3.f;
		}

		float DivideSafe(float dividend, float divisor)
		{
			return (divisor == 0.f) ? asfloat(0x7F7FFFFF) * Sign(dividend) : (dividend / divisor);
		}

		float DivideSafe(float dividend, float divisor, float fallback)
		{
			return (divisor == 0.f) ? fallback : (dividend / divisor);
		}

		float2 DivideSafe(float2 dividend, float2 divisor)
		{
			return float2(DivideSafe(dividend.x, divisor.x, asfloat(0x7F7FFFFF) * Sign(dividend.x)),
				DivideSafe(dividend.y, divisor.y, asfloat(0x7F7FFFFF) * Sign(dividend.y)));
		}

		float2 DivideSafe(float2 dividend, float2 divisor, float2 fallback)
		{
			return float2(DivideSafe(dividend.x, divisor.x, fallback.x),
				DivideSafe(dividend.y, divisor.y, fallback.y));
		}

		float3 DivideSafe(float3 dividend, float3 divisor)
		{
			return float3(DivideSafe(dividend.x, divisor.x, asfloat(0x7F7FFFFF) * Sign(dividend.x)),
				DivideSafe(dividend.y, divisor.y, asfloat(0x7F7FFFFF) * Sign(dividend.y)),
				DivideSafe(dividend.z, divisor.z, asfloat(0x7F7FFFFF) * Sign(dividend.z)));
		}

		float3 DivideSafe(float3 dividend, float3 divisor, float3 fallback)
		{
			return float3(DivideSafe(dividend.x, divisor.x, fallback.x),
				DivideSafe(dividend.y, divisor.y, fallback.y),
				DivideSafe(dividend.z, divisor.z, fallback.z));
		}

	}  // namespace math

	namespace color
	{

		static const float3x3 BT709_TO_XYZ_MAT = float3x3(
			0.4123907993f, 0.3575843394f, 0.1804807884f,
			0.2126390059f, 0.7151686788f, 0.0721923154f,
			0.0193308187f, 0.1191947798f, 0.9505321522f);

		static const float3x3 XYZ_TO_BT709_MAT = float3x3(
			3.2409699419f, -1.5373831776f, -0.4986107603f,
			-0.9692436363f, 1.8759675015f, 0.0415550574f,
			0.0556300797f, -0.2039769589f, 1.0569715142f);

		static const float3x3 BT2020_TO_XYZ_MAT = float3x3(
			0.6369580483f, 0.1446169036f, 0.1688809752f,
			0.2627002120f, 0.6779980715f, 0.0593017165f,
			0.0000000000f, 0.0280726930f, 1.0609850577f);

		static const float3x3 XYZ_TO_BT2020_MAT = float3x3(
			1.7166511880f, -0.3556707838f, -0.2533662814f,
			-0.6666843518f, 1.6164812366f, 0.0157685458f,
			0.0176398574f, -0.0427706133f, 0.9421031212f);

		static const float3x3 AP0_TO_XYZ_MAT = float3x3(
			0.9525523959f, 0.0000000000f, 0.0000936786f,
			0.3439664498f, 0.7281660966f, -0.0721325464f,
			0.0000000000f, 0.0000000000f, 1.0088251844f);

		static const float3x3 XYZ_TO_AP0_MAT = float3x3(
			1.0498110175f, 0.0000000000f, -0.0000974845f,
			-0.4959030231f, 1.3733130458f, 0.0982400361f,
			0.0000000000f, 0.0000000000f, 0.9912520182f);

		static const float3x3 AP1_TO_XYZ_MAT = float3x3(
			0.6624541811f, 0.1340042065f, 0.1561876870f,
			0.2722287168f, 0.6740817658f, 0.0536895174f,
			-0.0055746495f, 0.0040607335f, 1.0103391003f);

		static const float3x3 XYZ_TO_AP1_MAT = float3x3(
			1.6410233797f, -0.3248032942f, -0.2364246952f,
			-0.6636628587f, 1.6153315917f, 0.0167563477f,
			0.0117218943f, -0.0082844420f, 0.9883948585f);

		static const float3x3 XYZ_TO_ICTCP_LMS_MAT = float3x3(
			0.359168797f, 0.697604775f, -0.0357883982f,
			-0.192186400f, 1.10039842f, 0.0755404010f,
			0.00695759989f, 0.0749168023f, 0.843357980f);

		static const float3x3 ICTCP_LMS_TO_XYZ_MAT = float3x3(
			2.07036161f, -1.32659053f, 0.206681042f,
			0.364990383f, 0.680468797f, -0.0454616732f,
			-0.0495028905f, -0.0495028905f, 1.18806946f);

		static const float3x3 BT709_TO_ICTCP_LMS_MAT = float3x3(
			0.295764088f, 0.623072445f, 0.0811667516f,
			0.156191974f, 0.727251648f, 0.116557933f,
			0.0351022854f, 0.156589955f, 0.808302998f);

		static const float3x3 ICTCP_LMS_TO_BT709_MAT = float3x3(
			6.17353248f, -5.32089900f, 0.147354885f,
			-1.32403194f, 2.56026983f, -0.236238613f,
			-0.0115983877f, -0.264921456f, 1.27652633f);

		static const float3x3 DISPLAYP3_TO_XYZ_MAT = float3x3(
			0.4865709486f, 0.2656676932f, 0.1982172852f,
			0.2289745641f, 0.6917385218f, 0.0792869141f,
			0.0000000000f, 0.0451133819f, 1.0439443689f);

		static const float3x3 XYZ_TO_DISPLAYP3_MAT = float3x3(
			2.4934969119f, -0.9313836179f, -0.4027107845f,
			-0.8294889696f, 1.7626640603f, 0.0236246858f,
			0.0358458302f, -0.0761723893f, 0.9568845240);

		static const float3x3 BT470_PAL_TO_BT709_MAT = float3x3(
			1.04404318f, -0.0440432094f, 0.f,
			0.f, 1.f, 0.f,
			0.f, 0.0117933787f, 0.988206624f);

		static const float3x3 BT601_NTSC_U_TO_BT709_MAT = float3x3(
			0.939542055f, 0.0501813553f, 0.0102765792f,
			0.0177722238f, 0.965792834f, 0.0164349135f,
			-0.00162159989f, -0.00436974968f, 1.00599133f);

		static const float3x3 NTSC_U_1953_TO_XYZ_MAT = float3x3(
			0.6068638093f, 0.1735072810f, 0.2003348814f,
			0.2989030703f, 0.5866198547f, 0.1144770751f,
			-0.0000000000f, 0.0660980118f, 1.1161514821f);

		// chromatic adaptation method: vK20
		// chromatic adaptation transform: CAT02
		static const float3x3 ARIB_TR_B9_D93_TO_BT709_D65_MAT = float3x3(
			0.897676467f, -0.129552796f, 0.00210331683f,
			0.0400346256f, 0.970825016f, 0.00575808621f,
			0.00136304146f, 0.0323694758f, 1.48031127f);

		// chromatic adaptation method: vK20
		// chromatic adaptation transform: CAT02
		static const float3x3 ARIB_TR_B9_9300K_27_MPCD_TO_BT709_D65_MAT = float3x3(
			0.783664464f, -0.178418442f, 0.00223907502f,
			0.0380520112f, 1.03919935f, 0.00543892197f,
			0.000365949701f, 0.0269012674f, 1.31387364f);

		// chromatic adaptation method: vK20
		// chromatic adaptation transform: CAT02
		static const float3x3 BT709_D93_TO_BT709_D65_MAT = float3x3(
			0.968665063f, -0.0445920750f, -0.00335013796f,
			0.00231231073f, 1.00339293f, 0.0000867190974f,
			0.00326244067f, 0.0161521788f, 1.11353743f);

		// chromatic adaptation method: von Kries
		// chromatic adaptation transform: Bradford
		static const float3x3 D65_TO_D60_CAT = float3x3(
			1.01303493f, 0.00610525766f, -0.0149709433f,
			0.00769822997f, 0.998163342f, -0.00503203831f,
			-0.00284131732f, 0.00468515651f, 0.924506127f);

		// chromatic adaptation method: von Kries
		// chromatic adaptation transform: Bradford
		static const float3x3 D60_TO_D65_MAT = float3x3(
			0.987223982f, -0.00611322838f, 0.0159532874f,
			-0.00759837171f, 1.00186145f, 0.00533003592f,
			0.00307257706f, -0.00509596150f, 1.08168065f);

		static const float3x3 IDENTITY_MAT = float3x3(
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 1.0f);

		static const float3x3 BT709_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));

		// With Bradford
		static const float3x3 BT709_TO_AP1_MAT = float3x3(
			0.6130974024, 0.3395231462, 0.0473794514,
			0.0701937225, 0.9163538791, 0.0134523985,
			0.0206155929, 0.1095697729, 0.8698146342);

		// With Bradford
		static const float3x3 BT2020_TO_AP1_MAT = float3x3(
			0.9748949779f, 0.0195991086f, 0.0055059134f,
			0.0021795628f, 0.9955354689f, 0.0022849683f,
			0.0047972397f, 0.0245320166f, 0.9706707437f);

		static const float3x3 BT709_TO_BT2020_MAT = mul(XYZ_TO_BT2020_MAT, BT709_TO_XYZ_MAT);
		static const float3x3 BT709_TO_BT709D60_MAT = mul(XYZ_TO_BT709_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));
		static const float3x3 BT709_TO_BT2020D60_MAT = mul(XYZ_TO_BT2020_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));
		static const float3x3 BT709_TO_DISPLAYP3_MAT = mul(XYZ_TO_DISPLAYP3_MAT, BT709_TO_XYZ_MAT);
		static const float3x3 BT709_TO_DISPLAYP3D60_MAT = mul(XYZ_TO_DISPLAYP3_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));

		static const float3x3 BT2020_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, BT2020_TO_XYZ_MAT));
		static const float3x3 BT2020_TO_BT709_MAT = mul(XYZ_TO_BT709_MAT, BT2020_TO_XYZ_MAT);

		static const float3x3 DISPLAYP3_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, DISPLAYP3_TO_XYZ_MAT));
		static const float3x3 DISPLAYP3_TO_BT709_MAT = mul(XYZ_TO_BT709_MAT, DISPLAYP3_TO_XYZ_MAT);

		static const float3x3 AP0_TO_AP1_MAT = mul(XYZ_TO_AP1_MAT, AP0_TO_XYZ_MAT);

		static const float3x3 AP1_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, AP1_TO_XYZ_MAT);

		// With Bradford
		static const float3x3 AP1_TO_BT709_MAT = float3x3(
			1.7050509927, -0.6217921207, -0.0832588720,
			-0.1302564175, 1.1408047366, -0.0105483191,
			-0.0240033568, -0.1289689761, 1.1529723329);

		// With Bradford
		static const float3x3 AP1_TO_BT2020_MAT = float3x3(
			1.0258247477f, -0.0200531908f, -0.0057715568f,
			-0.0022343695f, 1.0045865019f, -0.0023521324f,
			-0.0050133515f, -0.0252900718f, 1.0303034233f);

		static const float3x3 AP1_TO_BT709D60_MAT = mul(XYZ_TO_BT709_MAT, AP1_TO_XYZ_MAT);
		static const float3x3 AP1_TO_BT2020D60_MAT = mul(XYZ_TO_BT2020_MAT, AP1_TO_XYZ_MAT);
		static const float3x3 AP1_TO_AP1D65_MAT = mul(XYZ_TO_AP1_MAT, mul(D60_TO_D65_MAT, AP1_TO_XYZ_MAT));

		// https://www.ilkeratalay.com/colorspacesfaq.php
		static const float3 BOURGIN_D65_Y = float3(0.222015, 0.706655, 0.071330);

		namespace XYZ
		{
			namespace from
			{
				float3 xyY(float3 xyY)
				{
					float3 XYZ;

					XYZ.xz = float2(xyY.x, (1.f - xyY.xy.x - xyY.xy.y)) / xyY.y * xyY[2];

					XYZ.y = xyY[2];

					return XYZ;
				}

				float3 BT709(float3 bt709)
				{
					return mul(BT709_TO_XYZ_MAT, bt709);
				}
			}  // namespace from
		}      // namespace XYZ

		namespace xyY
		{
			namespace from
			{
				float3 XYZ(float3 XYZ)
				{
					float xyz = XYZ.x + XYZ.y + XYZ.z;

					float3 xyY;

					xyY.xy = XYZ.xy / xyz;

					xyY[2] = XYZ.y;

					return xyY;
				}

				float3 BT709(float3 bt709)
				{
					float3 XYZ = XYZ::from::BT709(bt709);

					return xyY::from::XYZ(XYZ);
				}
			}  // namespace from
		}      // namespace xyY

		namespace bt709
		{
			static const float REFERENCE_WHITE = 100.f;

			namespace from
			{
				float3 XYZ(float3 XYZ)
				{
					return mul(XYZ_TO_BT709_MAT, XYZ);
				}

				float3 xyY(float3 xyY)
				{
					float3 XYZ = XYZ::from::xyY(xyY);

					return bt709::from::XYZ(XYZ);
				}

				float3 AP1(float3 ap1)
				{
					return mul(AP1_TO_BT709_MAT, ap1);
				}

				float3 BT2020(float3 bt2020)
				{
					return mul(BT2020_TO_BT709_MAT, bt2020);
				}

				float3 BT601NTSCU(float3 bt601)
				{
					return mul(BT601_NTSC_U_TO_BT709_MAT, bt601);
				}

				float3 ARIBTRB9(float3 aribtrb9)
				{
					return mul(ARIB_TR_B9_D93_TO_BT709_D65_MAT, aribtrb9);
				}

				float3 ARIBTRB927MPCD(float3 aribtrb9)
				{
					return mul(ARIB_TR_B9_9300K_27_MPCD_TO_BT709_D65_MAT, aribtrb9);
				}

				float3 BT709D93(float3 bt709d93)
				{
					return mul(BT709_D93_TO_BT709_D65_MAT, bt709d93);
				}

				float3 OkLab(float3 oklab)
				{
					static const float3x3 OKLAB_2_OKLABLMS = {
						1.f, 0.3963377774f, 0.2158037573f,
						1.f, -0.1055613458f, -0.0638541728f,
						1.f, -0.0894841775f, -1.2914855480f
					};

					static const float3x3 OKLABLMS_2_BT709 = {
						4.0767416621f, -3.3077115913f, 0.2309699292f,
						-1.2684380046f, 2.6097574011f, -0.3413193965f,
						-0.0041960863f, -0.7034186147f, 1.7076147010f
					};

					float3 lms = mul(OKLAB_2_OKLABLMS, oklab);

					lms = lms * lms * lms;

					return mul(OKLABLMS_2_BT709, lms);
				}
			}  // namespace from
		}      // namespace bt709

		namespace bt2020
		{
			namespace from
			{
				float3 BT709(float3 bt709)
				{
					return mul(BT709_TO_BT2020_MAT, bt709);
				}

				float3 AP1(float3 ap1)
				{
					return mul(AP1_TO_BT2020_MAT, ap1);
				}

			}  // namespace from
		}      // namespace bt2020

		namespace ap1
		{
			namespace from
			{
				float3 BT709(float3 bt709)
				{
					return mul(BT709_TO_AP1_MAT, bt709);
				}

				float3 BT2020(float3 bt2020)
				{
					return mul(BT2020_TO_AP1_MAT, bt2020);
				}
			}  // namespace from
		}      // namespace ap1

		namespace y
		{
			namespace from
			{
				float NTSC1953(float3 ntsc)
				{
					return dot(ntsc, NTSC_U_1953_TO_XYZ_MAT[1].rgb);
				}

				float BT709(float3 bt709)
				{
					return dot(bt709, BT709_TO_XYZ_MAT[1].rgb);
				}
				float BT2020(float3 bt2020)
				{
					return dot(bt2020, BT2020_TO_XYZ_MAT[1].rgb);
				}
				float AP1(float3 ap1)
				{
					return dot(ap1, AP1_TO_XYZ_MAT[1].rgb);
				}
			}  // namespace from
		}      // namespace y

		namespace luma
		{
			namespace from
			{
				float BT601(float3 bt601)
				{
					return y::from::NTSC1953(bt601);
				}
			}  // namespace from
		}      // namespace luma

		namespace pq
		{
			static const float M1 = 2610.f / 16384.f;           // 0.1593017578125f;
			static const float M2 = 128.f * (2523.f / 4096.f);  // 78.84375f;
			static const float C1 = 3424.f / 4096.f;            // 0.8359375f;
			static const float C2 = 32.f * (2413.f / 4096.f);   // 18.8515625f;
			static const float C3 = 32.f * (2392.f / 4096.f);   // 18.6875f;

			float3 Encode(float3 color, float scaling = 10000.f)
			{
				color *= (scaling / 10000.f);
				float3 y_m1 = pow(color, M1);
				return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
			}

			float3 Decode(float3 color, float scaling = 10000.f)
			{
				float3 e_m12 = pow(color, 1.f / M2);
				float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
				return out_color * (10000.f / scaling);
			}

			float3 EncodeSafe(float3 color, float scaling = 10000.f)
			{
				return Encode(max(0, color), scaling);
			}

			float3 DecodeSafe(float3 color, float scaling = 10000.f)
			{
				return Decode(max(0, color), scaling);
			}

		}  // namespace pq

		namespace ictcp
		{
			namespace from
			{
				float3 BT709(float3 bt709_color)
				{
					float3 lms_color = mul(BT709_TO_ICTCP_LMS_MAT, bt709_color);

					// L'M'S' -> ICtCp
					float3x3 lms_to_ictcp = {
						0.5f, 0.5f, 0.f,
						1.61370003f, -3.32339620f, 1.70969617f,
						4.37806224f, -4.24553966f, -0.132522642f
					};

					return mul(lms_to_ictcp, pq::Encode(max(0, lms_color), 100.0f));
				}
			}  // namespace from
		}      // namespace ictcp

		namespace srgb
		{
			static const float REFERENCE_WHITE = 80.f;

#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
#	define ENCODE(T)                                                                         \
		T Encode(T c)                                                                         \
		{                                                                                     \
			return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * pow(c, 1.f / 2.4f) - 0.055f); \
		}
#else
#	define ENCODE(T)                                                                         \
		T Encode(T c)                                                                         \
		{                                                                                     \
			return select(c <= 0.0031308f, c * 12.92f, 1.055f * pow(c, 1.f / 2.4f) - 0.055f); \
		}
#endif

			ENCODE(float)
			ENCODE(float2)
			ENCODE(float3)

			float4 Encode(float4 color)
			{
				return float4(Encode(color.rgb), color.a);
			}

#define ENCODE_SAFE(T)                                 \
	T EncodeSafe(T c)                                  \
	{                                                  \
		return renodx::math::Sign(c) * Encode(abs(c)); \
	}

			ENCODE_SAFE(float)
			ENCODE_SAFE(float2)
			ENCODE_SAFE(float3)

			float4 EncodeSafe(float4 color)
			{
				return float4(EncodeSafe(color.rgb), color.a);
			}

#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
#	define DECODE(T)                                                                   \
		T Decode(T c)                                                                   \
		{                                                                               \
			return (c <= 0.04045f) ? (c / 12.92f) : (pow((c + 0.055f) / 1.055f, 2.4f)); \
		}
#else
#	define DECODE(T)                                                                   \
		T Decode(T c)                                                                   \
		{                                                                               \
			return select(c <= 0.04045f, c / 12.92f, pow((c + 0.055f) / 1.055f, 2.4f)); \
		}
#endif

			DECODE(float)
			DECODE(float2)
			DECODE(float3)

			float4 Decode(float4 color)
			{
				return float4(Decode(color.rgb), color.a);
			}

#define DECODE_SAFE(T)                                 \
	T DecodeSafe(T c)                                  \
	{                                                  \
		return renodx::math::Sign(c) * Decode(abs(c)); \
	}

			DECODE_SAFE(float)
			DECODE_SAFE(float2)
			DECODE_SAFE(float3)

			float4 DecodeSafe(float4 color)
			{
				return float4(DecodeSafe(color.rgb), color.a);
			}

#undef ENCODE
#undef ENCODE_SAFE
#undef DECODE
#undef DECODE_SAFE

		}  // namespace srgb

		namespace srgba
		{

			float4 Encode(float4 color)
			{
#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
				return (color <= 0.0031308f) ? (color * 12.92f) : (1.055f * pow(color, 1.f / 2.4f) - 0.055f);
#else
				return select(color <= 0.0031308f, color * 12.92f, 1.055f * pow(color, 1.f / 2.4f) - 0.055f);
#endif
			}

			float4 EncodeSafe(float4 color)
			{
				return renodx::math::Sign(color) * Encode(abs(color));
			}

			float4 Decode(float4 color)
			{
#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
				return (color <= 0.04045f) ? (color / 12.92f) : pow((color + 0.055f) / 1.055f, 2.4f);
#else
				return select(color <= 0.04045f, color / 12.92f, pow((color + 0.055f) / 1.055f, 2.4f));
#endif
			}

			float4 DecodeSafe(float4 color)
			{
				return renodx::math::Sign(color) * Decode(abs(color));
			}

		}  // namespace srgba

		namespace gamma
		{

#define ENCODE(T)                     \
	T Encode(T c, float gamma = 2.2f) \
	{                                 \
		return pow(c, 1.f / gamma);   \
	}

			ENCODE(float)
			ENCODE(float2)
			ENCODE(float3)

#define ENCODE_SAFE(T)                                \
	T EncodeSafe(T c, float gamma = 2.2f)             \
	{                                                 \
		return renodx::math::SignPow(c, 1.f / gamma); \
	}

			ENCODE_SAFE(float)
			ENCODE_SAFE(float2)
			ENCODE_SAFE(float3)

#define DECODE(T)                     \
	T Decode(T c, float gamma = 2.2f) \
	{                                 \
		return pow(c, gamma);         \
	}

			DECODE(float)
			DECODE(float2)
			DECODE(float3)

#define DECODE_SAFE(T)                          \
	T DecodeSafe(T c, float gamma = 2.2f)       \
	{                                           \
		return renodx::math::SignPow(c, gamma); \
	}

			DECODE_SAFE(float)
			DECODE_SAFE(float2)
			DECODE_SAFE(float3)

#undef ENCODE
#undef ENCODE_SAFE
#undef DECODE
#undef DECODE_SAFE

		}  // namespace gamma

		namespace arri
		{
			namespace logc
			{

				struct EncodingParams
				{
					float a;
					float b;
					float c;
					float d;
					float e;
					float f;
					float cut;
				};

#define ENCODE_CONDS (c > params.cut)
#define ENCODE_COND_TRUE (params.c * log10((params.a * c) + params.b) + params.d)
#define ENCODE_COND_FALSE (params.e * c + params.f)

#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
#	define ENCODE(T)                                                       \
		T Encode(T c, EncodingParams params, bool use_cut = true)           \
		{                                                                   \
			if (!use_cut) {                                                 \
				return ENCODE_COND_TRUE;                                    \
			} else {                                                        \
				return ENCODE_CONDS ? ENCODE_COND_TRUE : ENCODE_COND_FALSE; \
			}                                                               \
		}
#else
#	define ENCODE(T)                                                         \
		T Encode(T c, EncodingParams params, bool use_cut = true)             \
		{                                                                     \
			if (!use_cut) {                                                   \
				return ENCODE_COND_TRUE;                                      \
			}                                                                 \
			return select(ENCODE_CONDS, ENCODE_COND_TRUE, ENCODE_COND_FALSE); \
		}
#endif

				ENCODE(float)
				ENCODE(float2)
				ENCODE(float3)

#define DECODE_CONDS_CUT (c > (params.e * params.cut + params.f))
#define DECODE_CONDS_NO_CUT (c > params.f)
#define DECODE_COND_TRUE ((pow(10.f, (c - params.d) / params.c) - params.b) / params.a)
#define DECODE_COND_FALSE ((c - params.f) / params.e)

#if (!defined(__SHADER_TARGET_MAJOR) || __SHADER_TARGET_MAJOR <= 5)
#	define DECODE(T)                                                           \
		T Decode(T c, EncodingParams params, bool use_cut = true)               \
		{                                                                       \
			if (use_cut) {                                                      \
				return DECODE_CONDS_CUT ? DECODE_COND_TRUE : DECODE_COND_FALSE; \
			}                                                                   \
			return DECODE_CONDS_NO_CUT ? DECODE_COND_TRUE : DECODE_COND_FALSE;  \
		}
#else
#	define DECODE(T)                                                                 \
		T Decode(T c, EncodingParams params, bool use_cut = true)                     \
		{                                                                             \
			if (use_cut) {                                                            \
				return select(DECODE_CONDS_CUT, DECODE_COND_TRUE, DECODE_COND_FALSE); \
			}                                                                         \
			return select(DECODE_CONDS_NO_CUT, DECODE_COND_TRUE, DECODE_COND_FALSE);  \
		}
#endif

				DECODE(float)
				DECODE(float2)
				DECODE(float3)

#undef ENCODE
#undef ENCODE_CONDS
#undef ENCODE_COND_TRUE
#undef ENCODE_COND_FALSE
#undef DECODE
#undef DECODE_CONDS_CUT
#undef DECODE_CONDS_NO_CUT
#undef DECODE_COND_TRUE
#undef DECODE_COND_FALSE

#define GENERATE_ARRI_LOGC_FUNCTIONS(T)          \
	T Encode(T c, bool use_cut = true)           \
	{                                            \
		return logc::Encode(c, PARAMS, use_cut); \
	}                                            \
	T Decode(T c, bool use_cut = true)           \
	{                                            \
		return logc::Decode(c, PARAMS, use_cut); \
	}

				namespace c800
				{
					static const EncodingParams PARAMS = {
						5.555556f,
						0.052272f,
						0.247190f,
						0.385537f,
						5.367655f,
						0.092809f,
						0.010591f,
					};

					GENERATE_ARRI_LOGC_FUNCTIONS(float)
					GENERATE_ARRI_LOGC_FUNCTIONS(float2)
					GENERATE_ARRI_LOGC_FUNCTIONS(float3)

				}  // namespace c800

				namespace c1000
				{

					static const EncodingParams PARAMS = {
						5.555556f,
						0.047996f,
						0.244161f,
						0.386036f,
						5.301883f,
						0.092814f,
						0.011361f
					};

					GENERATE_ARRI_LOGC_FUNCTIONS(float)
					GENERATE_ARRI_LOGC_FUNCTIONS(float2)
					GENERATE_ARRI_LOGC_FUNCTIONS(float3)

				}  // namespace c1000

#undef GENERATE_ARRI_LOGC_FUNCTIONS

			}  // namespace logc
		}      // namespace arri

		namespace oklab
		{
			namespace from
			{
				float3 BT709(float3 bt709)
				{
					static const float3x3 BT709_2_OKLABLMS = {
						0.4122214708f, 0.5363325363f, 0.0514459929f,
						0.2119034982f, 0.6806995451f, 0.1073969566f,
						0.0883024619f, 0.2817188376f, 0.6299787005f
					};
					static const float3x3 OKLABLMS_2_OKLAB = {
						0.2104542553f, 0.7936177850f, -0.0040720468f,
						1.9779984951f, -2.4285922050f, 0.4505937099f,
						0.0259040371f, 0.7827717662f, -0.8086757660f
					};

					float3 lms = mul(BT709_2_OKLABLMS, bt709);

					lms = renodx::math::Cbrt(lms);

					return mul(OKLABLMS_2_OKLAB, lms);
				}

				float3 OkLCh(float3 oklch)
				{
					float l = oklch[0];
					float c = oklch[1];
					float h = oklch[2];
					return float3(l, c * cos(h), c * sin(h));
				}
			}  // namespace from
		}      // namespace oklab

		//  Copyright 2022 - Aurélien PIERRE / darktable project
		//  URL: https://eng.aurelienpierre.com/2022/02/color-saturation-control-for-the-21th-century/
		//  The following source code is released under the MIT license
		//  (https://opensource.org/licenses/MIT) with the following addenda:
		//  * Any reuse of this code shall include the names of the author and of the project, as well as the source URL,
		//  * Any implementation of this colour space MUST call it "darktable Uniform Color Space" or
		//    "darktable UCS" in the end - user interface of the software.
		namespace dtucs
		{
			const static float L_WHITE_VALUE = 1.f;
			const static float L_WHITE_HAT = pow(L_WHITE_VALUE, 0.631651345306265f);
			const static float L_WHITE = (2.098883786377f * L_WHITE_HAT) / (L_WHITE_HAT + 1.12426773749357f);

			namespace uvY
			{
				namespace from
				{
					static const float3x3 xyToUVD = {
						-0.783941002840055f, 0.277512987809202f, 0.153836578598858f,
						0.745273540913283f, -0.205375866083878f, -0.165478376301988f,
						0.318707282433486f, 2.16743692732158f, 0.291320554395942f
					};

					static const float2x2 UVStarToUVStarPrime = {
						-1.124983854323892f, -0.980483721769325f,
						1.86323315098672f, 1.971853092390862f
					};

					float3 BT709(float3 bt709)
					{
						float3 xyY = xyY::from::BT709(bt709);

						float3 UVD = mul(xyToUVD, float3(xyY.xy, 1.f));

						float2 UV = UVD.xy /= UVD.z;

						float2 UVStar = float2(1.39656225667f, 1.4513954287f) * UV / (abs(UV) + float2(1.49217352929f, 1.52488637914f));

						float2 UVStarPrime = mul(UVStarToUVStarPrime, UVStar);

						return float3(UVStarPrime, xyY[2]);
					}
				}  // namespace from
			}      // namespace uvY

			namespace jch
			{
				namespace from
				{
					float3 BT709(float3 bt709, float cz = 1.f)
					{
						float3 uvY = uvY::from::BT709(bt709);

						float L_star_hat = pow(uvY[2], 0.631651345306265f);
						float L_star = 2.098883786377f * L_star_hat / (L_star_hat + 1.12426773749357f);

						float M2 = dot(uvY.xy, uvY.xy);

						float C = 15.932993652962535 * pow(L_star, 0.6523997524738018) * pow(M2, 0.6007557017508491) / L_WHITE;
						float J = pow(L_star / L_WHITE, cz);
						float H = atan2(uvY[1], uvY[0]);

						return float3(J, C, H);
					}
				}  // from
			}      // jch

			namespace hcb
			{
				namespace from
				{
					float3 BT709(float3 bt709, float cz = 1.f)
					{
						float3 jch = jch::from::BT709(bt709);
						float J = jch[0];
						float C = jch[1];
						float H = jch[2];

						float B = J * (pow(C, 1.33654221029386) + 1.f);

						return float3(H, C, B);
					}
				}  // from
			}      // hcb

			namespace hsb
			{
				namespace from
				{
					float3 BT709(float3 bt709, float cz = 1.f)
					{
						float3 hcb = hcb::from::BT709(bt709);
						float H = hcb[0];
						float C = hcb[1];
						float B = hcb[2];

						float S = C / B;

						return float3(H, S, B);
					}
				}  // from
			}      // hsb

		}  // namespace dtucs

		namespace bt709
		{
			namespace from
			{
				namespace dtucs
				{
					static const float2x2 UVStarPrimeToUVStar = {
						-5.037522385190711f, -2.504856328185843f,
						4.760029407436461f, 2.874012963239247f
					};

					static const float3x3 UVToxyD = {
						0.167171472114775f, 0.141299802443708f, -0.00801531300850582f,
						-0.150959086409163f, -0.155185060382272f, -0.00843312433578007f,
						0.940254742367256f, 1.f, -0.0256325967652889f
					};

					float3 uvY(float3 uvY)
					{
						float2 UVStar = mul(UVStarPrimeToUVStar, uvY.xy);

						float2 UV = float2(-1.49217352929f, -1.52488637914f) * UVStar / (abs(UVStar) - float2(1.39656225667f, 1.4513954287f));

						float3 xyD = mul(UVToxyD, float3(UV, 1.f));

						float3 xyY;

						xyY.xy = xyD.xy / xyD.z;

						xyY[2] = uvY[2];

						return bt709::from::xyY(xyY);
					}

					float3 JCH(float3 jch, float cz = 1.f)
					{
						float J = jch[0];
						float C = jch[1];
						float H = jch[2];

						float L_star = pow(J, (1 / cz)) * color::dtucs::L_WHITE;

						float M = pow(C * color::dtucs::L_WHITE / (15.932993652962535 * pow(L_star, 0.6523997524738018)), 0.8322850678616855);

						float Y = pow(-1.12426773749357f * L_star / (L_star - 2.098883786377), 1.5831518565279648f);

						return bt709::from::dtucs::uvY(float3(M * cos(H), M * sin(H), Y));
					}

					float3 HCB(float3 hcb, float cz = 1.f)
					{
						float H = hcb[0];
						float C = hcb[1];
						float B = hcb[2];

						float J = B / (pow(C, 1.33654221029386) + 1.f);

						return bt709::from::dtucs::JCH(float3(J, C, H), cz);
					}

					float3 HSB(float3 hsb, float cz = 1.f)
					{
						float H = hsb[0];
						float S = hsb[1];
						float B = hsb[2];

						float C = S * B;

						return bt709::from::dtucs::HCB(float3(H, C, B), cz);
					}

				}  // dtucs
			}      // namespace from
		}          // namespace bt709

		namespace bt2408
		{
			static const float REFERENCE_WHITE = 203.f;
			static const float GRAPHICS_WHITE = 203.f;
		}  // namespace bt2408

		namespace oklch
		{
			namespace from
			{
				float3 OkLab(float3 oklab)
				{
					float l = oklab[0];
					float a = oklab[1];
					float b = oklab[2];
					return float3(l, distance(oklab.yz, 0), atan2(b, a));
				}
				float3 BT709(float3 bt709)
				{
					float3 ok_lab = renodx::color::oklab::from::BT709(bt709);
					return OkLab(ok_lab);
				}
			}  // namespace from
		}      // namespace oklch

		namespace bt709
		{
			namespace from
			{
				float3 OkLCh(float3 oklch)
				{
					float3 ok_lab = renodx::color::oklab::from::OkLCh(oklch);
					return OkLab(ok_lab);
				}

				float3 ICtCp(float3 col)
				{
					// ICtCp -> L'M'S'
					float3x3 ictcp_to_lms = float3x3(
						1.f, 0.00860647484f, 0.111033529f,
						1.f, -0.00860647484f, -0.111033529f,
						1.f, 0.560046315f, -0.320631951f);

					col = mul(ictcp_to_lms, col);

					// 1.0f = 100 nits, 100.0f = 10k nits
					col = pq::DecodeSafe(col, 100.f);
					return mul(ICTCP_LMS_TO_BT709_MAT, col);
				}

			}  // namespace from

			namespace clamp
			{
				float3 BT709(float3 bt709)
				{
					return max(0, bt709);
				}
				float3 BT2020(float3 bt709)
				{
					float3 bt2020 = renodx::color::bt2020::from::BT709(bt709);
					bt2020 = max(0, bt2020);
					return renodx::color::bt709::from::BT2020(bt2020);
				}

				float3 AP1(float3 bt709)
				{
					float3 ap1 = renodx::color::ap1::from::BT709(bt709);
					ap1 = max(0, ap1);
					return renodx::color::bt709::from::AP1(ap1);
				}
			}  // namespace clamp
		}      // namespace bt709

		namespace convert
		{
			static const float COLOR_SPACE_UNKNOWN = -1;
			static const float COLOR_SPACE_NONE = -1;
			static const float COLOR_SPACE_BT709 = 0;
			static const float COLOR_SPACE_BT2020 = 1.f;
			static const float COLOR_SPACE_AP1 = 2.f;

			float3 ColorSpaceFromBT709(float3 color, float color_space)
			{
				[branch] if (color_space == COLOR_SPACE_BT2020)
				{
					color = renodx::color::bt2020::from::BT709(color);
				}
				else
				{
					[branch] if (color_space == COLOR_SPACE_AP1)
					{
						color = renodx::color::ap1::from::BT709(color);
					}
					else
					{
						color = color;
					}
				}
				return color;
			}

			float3 ColorSpaceFromBT2020(float3 color, float color_space)
			{
				[branch] if (color_space == COLOR_SPACE_BT709)
				{
					color = renodx::color::bt709::from::BT2020(color);
				}
				else
				{
					[branch] if (color_space == COLOR_SPACE_AP1)
					{
						color = renodx::color::ap1::from::BT2020(color);
					}
					else
					{
						color = color;
					}
				}
				return color;
			}

			float3 ColorSpaceFromAP1(float3 color, float color_space)
			{
				[branch] if (color_space == COLOR_SPACE_BT709)
				{
					color = renodx::color::bt709::from::AP1(color);
				}
				else
				{
					[branch] if (color_space == COLOR_SPACE_BT2020)
					{
						color = renodx::color::bt2020::from::AP1(color);
					}
					else
					{
						color = color;
					}
				}
				return color;
			}

			float3 ColorSpaces(float3 color, float input_color_space, float output_color_space)
			{
				[branch] if (input_color_space == COLOR_SPACE_BT709)
				{
					color = ColorSpaceFromBT709(color, output_color_space);
				}
				else
				{
					[branch] if (input_color_space == COLOR_SPACE_BT2020)
					{
						color = ColorSpaceFromBT2020(color, output_color_space);
					}
					else
					{
						[branch] if (input_color_space == COLOR_SPACE_AP1)
						{
							color = ColorSpaceFromAP1(color, output_color_space);
						}
						else
						{
							color = color;
						}
					}
				}
				return color;
			}

		}  // namespace convert

		namespace correct
		{

#define GAMMA(T)                                                 \
	T Gamma(T c, bool pow_to_srgb = false, float gamma = 2.2f)   \
	{                                                            \
		if (pow_to_srgb) {                                       \
			return srgb::Decode(color::gamma::Encode(c, gamma)); \
		} else {                                                 \
			return color::gamma::Decode(srgb::Encode(c), gamma); \
		}                                                        \
	}

			GAMMA(float)
			GAMMA(float2)
			GAMMA(float3)

			float4 Gamma(float4 color, bool pow_to_srgb = false, float gamma = 2.2f)
			{
				return float4(Gamma(color.rgb, pow_to_srgb, gamma), color.a);
			}

#define GAMMA_SAFE(T)                                                                         \
	T GammaSafe(T c, bool pow_to_srgb = false, float gamma = 2.2f)                            \
	{                                                                                         \
		if (pow_to_srgb) {                                                                    \
			return renodx::math::Sign(c) * srgb::Decode(color::gamma::Encode(abs(c), gamma)); \
		} else {                                                                              \
			return renodx::math::Sign(c) * color::gamma::Decode(srgb::Encode(abs(c)), gamma); \
		}                                                                                     \
	}

			GAMMA_SAFE(float)
			GAMMA_SAFE(float2)
			GAMMA_SAFE(float3)

			float4 GammaSafe(float4 color, bool pow_to_srgb = false, float gamma = 2.2f)
			{
				return float4(Gamma(color.rgb, pow_to_srgb, gamma), color.a);
			}

#undef GAMMA
#undef GAMMA_SAFE

			float3 HueOKLab(float3 incorrect_color, float3 correct_color, float strength = 1.f)
			{
				if (strength == 0.f)
					return incorrect_color;

				float3 correct_lab = renodx::color::oklab::from::BT709(correct_color);

				float3 incorrect_lab = renodx::color::oklab::from::BT709(incorrect_color);

				float chrominance_pre_adjust = distance(incorrect_lab.yz, 0);

				incorrect_lab.yz = lerp(incorrect_lab.yz, correct_lab.yz, strength);

				float chrominance_post_adjust = distance(incorrect_lab.yz, 0);

				incorrect_lab.yz *= renodx::math::DivideSafe(chrominance_pre_adjust, chrominance_post_adjust, 1.f);

				float3 color = renodx::color::bt709::from::OkLab(incorrect_lab);
				color = renodx::color::bt709::clamp::AP1(color);
				return color;
			}

			float3 HueICtCp(float3 incorrect_color, float3 correct_color, float strength = 1.f)
			{
				if (strength == 0.f)
					return incorrect_color;

				float3 correct_perceptual = renodx::color::ictcp::from::BT709(correct_color);

				float3 incorrect_perceptual = renodx::color::ictcp::from::BT709(incorrect_color);

				float chrominance_pre_adjust = distance(incorrect_perceptual.yz, 0);

				incorrect_perceptual.yz = lerp(incorrect_perceptual.yz, correct_perceptual.yz, strength);

				float chrominance_post_adjust = distance(incorrect_perceptual.yz, 0);

				incorrect_perceptual.yz *= renodx::math::DivideSafe(chrominance_pre_adjust, chrominance_post_adjust, 1.f);

				float3 color = renodx::color::bt709::from::ICtCp(incorrect_perceptual);
				color = renodx::color::bt709::clamp::AP1(color);
				return color;
			}

			float3 HuedtUCS(float3 incorrect_color, float3 correct_color, float strength = 1.f)
			{
				if (strength == 0.f)
					return incorrect_color;

				float3 correct_perceptual = renodx::color::dtucs::jch::from::BT709(correct_color);

				float3 incorrect_perceptual = renodx::color::dtucs::jch::from::BT709(incorrect_color);

				float chrominance_pre_adjust = incorrect_perceptual[1];

				incorrect_perceptual[2] = lerp(incorrect_perceptual[2], correct_perceptual[2], strength);

				incorrect_perceptual[1] = chrominance_pre_adjust;

				float3 color = renodx::color::bt709::from::dtucs::JCH(incorrect_perceptual);
				color = renodx::color::bt709::clamp::AP1(color);
				return color;
			}

			float3 Hue(float3 incorrect_color, float3 correct_color, float strength = 1.f, uint method = 0u)
			{
				if (method == 1u)
					return HueICtCp(incorrect_color, correct_color, strength);
				if (method == 2u)
					return HuedtUCS(incorrect_color, correct_color, strength);
				return HueOKLab(incorrect_color, correct_color, strength);
			}

		}  // namespace correct

		namespace grade
		{

			float3 Contrast(float3 color, float contrast, float mid_gray = 0.18f, float3x3 color_space = renodx::color::BT709_TO_XYZ_MAT)
			{
				float3 signs = renodx::math::Sign(color);
				color = abs(color);
				float3 working_color = pow(color / mid_gray, contrast) * mid_gray;
				float working_y = dot(working_color, float3(color_space[1].r, color_space[1].g, color_space[1].b));
				float color_y = dot(color, float3(color_space[1].r, color_space[1].g, color_space[1].b));
				return signs * color * (color_y > 0 ? (working_y / color_y) : 1.f);
			}

			float3 Saturation(float3 bt709, float saturation = 1.f)
			{
				float3 perceptual = renodx::color::oklab::from::BT709(bt709);
				perceptual.yz *= saturation;
				float3 color = renodx::color::bt709::from::OkLab(perceptual);
				color = renodx::color::bt709::clamp::AP1(color);
				return color;
			}

			float3 UserColorGrading(
				float3 bt709,
				float exposure,
				float highlights,
				float shadows,
				float contrast,
				float saturation,
				float dechroma,
				float hue_correction_strength,
				float3 hue_correction_source)
			{
				if (exposure == 1.f && saturation == 1.f && dechroma == 0.f && shadows == 1.f && highlights == 1.f && contrast == 1.f && hue_correction_strength == 0.f) {
					return bt709;
				}

				float3 color = bt709;

				color *= exposure;

				float y = renodx::color::y::from::BT709(abs(color));
				const float y_normalized = y / 0.18f;

				const float y_contrasted = pow(y_normalized, contrast);

				float y_highlighted = pow(y_contrasted, highlights);
				y_highlighted = lerp(y_contrasted, y_highlighted, saturate(y_contrasted));

				float y_shadowed = pow(y_highlighted, -1.f * (shadows - 2.f));
				y_shadowed = lerp(y_shadowed, y_highlighted, saturate(y_highlighted));

				const float y_final = y_shadowed * 0.18f;

				color *= (y > 0 ? (y_final / y) : 0);

				if (saturation != 1.f || dechroma != 0.f || hue_correction_strength != 0.f) {
					float3 perceptual_new = renodx::color::oklab::from::BT709(color);

					if (hue_correction_strength != 0.f) {
						float3 perceptual_old = renodx::color::oklab::from::BT709(hue_correction_source);

						// Save chrominance to apply black
						float chrominance_pre_adjust = distance(perceptual_new.yz, 0);

						perceptual_new.yz = lerp(perceptual_new.yz, perceptual_old.yz, hue_correction_strength);

						float chrominance_post_adjust = distance(perceptual_new.yz, 0);

						// Apply back previous chrominance
						perceptual_new.yz *= renodx::math::DivideSafe(chrominance_pre_adjust, chrominance_post_adjust, 1.f);
					}

					if (dechroma != 0.f) {
						perceptual_new.yz *= lerp(1.f, 0.f, saturate(pow(y / (10000.f / 100.f), (1.f - dechroma))));
					}

					perceptual_new.yz *= saturation;

					color = renodx::color::bt709::from::OkLab(perceptual_new);

					color = renodx::color::bt709::clamp::AP1(color);
				}

				return color;
			}

			float3 UserColorGrading(
				float3 color,
				float exposure = 1.f,
				float highlights = 1.f,
				float shadows = 1.f,
				float contrast = 1.f,
				float saturation = 1.f,
				float dechroma = 0.f,
				float hue_correction_strength = 1.f)
			{
				return UserColorGrading(
					color,
					exposure,
					highlights,
					shadows,
					contrast,
					saturation,
					dechroma,
					hue_correction_strength,
					color);
			}
		}  // namespace grade

	}  // namespace color

	namespace tonemap
	{

		float Reinhard(float x, float peak = 1.f)
		{
			return x / (x / peak + 1.f);
		}

		float3 Reinhard(float3 x, float peak = 1.f)
		{
			return x / (x / peak + 1.f);
		}

		float ReinhardExtended(float color, float white_max = 1000.f / 203.f, float peak = 1.f)
		{
			return Reinhard(color, peak) * (1.f + (peak * color) / (white_max * white_max));
		}

		float3 ReinhardExtended(float3 color, float white_max = 1000.f / 203.f, float peak = 1.f)
		{
			return Reinhard(color, peak) * (1.f + (peak * color) / (white_max * white_max));
		}

		float ComputeReinhardScale(float channel_max = 1.f, float channel_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
		{
			return (channel_max * (channel_min * gray_out + channel_min - gray_out)) / (gray_in * (gray_out - channel_max));
		}

		float ReinhardScalable(float x, float x_max = 1.f, float x_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
		{
			float exposure = ComputeReinhardScale(x_max, x_min, gray_in, gray_out);
			return mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
		}

		float3 ReinhardScalable(float3 x, float x_max = 1.f, float x_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
		{
			float exposure = ComputeReinhardScale(x_max, x_min, gray_in, gray_out);
			return mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
		}

		float ComputeReinhardExtendableScale(float w = 100.f, float p = 1.f, float m = 0.f, float x = 0.18f, float y = 0.18f)
		{
			// y = (sx / (sx/p + 1) * (1 + (psx)/(sw*sw))
			// solve for s (scale)
			// Min not currently supported
			return p * (w * w * y - (p * x * x)) / (w * w * x * (p - y));
		}

		float ReinhardScalableExtended(float x, float white_max = 100.f, float x_max = 1.f, float x_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
		{
			float exposure = ComputeReinhardExtendableScale(white_max, x_max, x_min, gray_in, gray_out);
			float extended = ReinhardExtended(x * exposure, white_max * exposure, x_max);
			return min(extended, x_max);
		}

		float3 ReinhardScalableExtended(float3 x, float white_max = 100.f, float x_max = 1.f, float x_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
		{
			float exposure = ComputeReinhardExtendableScale(white_max, x_max, x_min, gray_in, gray_out);
			float3 extended = ReinhardExtended(x * exposure, white_max * exposure, x_max);
			return min(extended, x_max);
		}

		namespace renodrt
		{

			struct Config
			{
				float nits_peak;
				float mid_gray_value;
				float mid_gray_nits;
				float exposure;
				float highlights;
				float shadows;
				float contrast;
				float saturation;
				float dechroma;
				float flare;
				float hue_correction_strength;
				float3 hue_correction_source;
				uint hue_correction_method;
				uint tone_map_method;
				uint hue_correction_type;
				uint working_color_space;
				bool per_channel;
				float blowout;
				float clamp_color_space;
				float clamp_peak;
				float white_clip;
			};

			namespace config
			{

				namespace hue_correction_type
				{
					static const uint INPUT = 0u;
					static const uint CUSTOM = 1u;
				}

				namespace hue_correction_method
				{
					static const uint OKLAB = 0u;
					static const uint ICTCP = 1u;
					static const uint DARKTABLE_UCS = 2u;
				}

				namespace tone_map_method
				{
					static const uint DANIELE = 0u;
					static const uint REINHARD = 1u;
				}

				Config Create(
					float nits_peak = 1000.f / 203.f * 100.f,
					float mid_gray_value = 0.18f,
					float mid_gray_nits = 10.f,
					float exposure = 1.f,
					float highlights = 1.f,
					float shadows = 1.f,
					float contrast = 1.1f,
					float saturation = 1.f,
					float dechroma = 0.5f,
					float flare = 0.f,
					float hue_correction_strength = 1.f,
					float3 hue_correction_source = 0,
					uint hue_correction_method = config::hue_correction_method::OKLAB,
					uint tone_map_method = config::tone_map_method::DANIELE,
					uint hue_correction_type = config::hue_correction_type::INPUT,
					uint working_color_space = 0u,
					bool per_channel = false,
					float blowout = 0,
					float clamp_color_space = 2.f,
					float clamp_peak = 0.f,
					float white_clip = 100.f)
				{
					const Config renodrt_config = {
						nits_peak,
						mid_gray_value,
						mid_gray_nits,
						exposure,
						highlights,
						shadows,
						contrast,
						saturation,
						dechroma,
						flare,
						hue_correction_strength,
						hue_correction_source,
						hue_correction_method,
						tone_map_method,
						hue_correction_type,
						working_color_space,
						per_channel,
						blowout,
						clamp_color_space,
						clamp_peak,
						white_clip
					};
					return renodrt_config;
				}
			}

			float CustomizeLuminance(float value, float highlights = 1.f, float shadows = 1.f, float contrast = 1.f)
			{
				value = value / 0.18f;
				[branch] if (highlights != 1.f)
				{
					value = lerp(
						value,
						pow(value, highlights),
						saturate(value));
				}

				[branch] if (shadows != 1.f)
				{
					value = lerp(
						pow(value, 2.f - shadows),
						value,
						saturate(value));
				}

				[branch] if (contrast != 1.f)
				{
					value = pow(value, contrast);
				}
				value *= 0.18f;
				return value;
			}

			float3 BT709(float3 bt709, Config current_config)
			{
				const float n_r = 100.f;
				float n = 1000.f;

				// drt cam
				// n_r = 100
				// g = 1.15
				// c = 0.18
				// c_d = 10.013
				// w_g = 0.14
				// t_1 = 0.04
				// r_hit_min = 128
				// r_hit_max = 896

				float g = 1.1;            // gamma/contrast
				float c = 0.18;           // scene-referred gray
				float c_d = 10.013;       // output gray in nits
				const float w_g = 0.00f;  // gray change
				float t_1 = 0.01;         // shadow toe
				const float r_hit_min = 128;
				const float r_hit_max = 256;

				float white_clip = 100.f;

				g = current_config.contrast;
				c = current_config.mid_gray_value;
				c_d = current_config.mid_gray_nits;
				n = current_config.nits_peak;
				t_1 = current_config.flare;
				white_clip = current_config.white_clip;

				float3 input_color;
				float y_original;

				float current_color_space = current_config.working_color_space;

				if (current_config.working_color_space == 2u) {
					input_color = max(0, renodx::color::ap1::from::BT709(bt709));
					y_original = renodx::color::y::from::AP1(input_color);
				} else if (current_config.working_color_space == 1u) {
					input_color = renodx::color::bt2020::from::BT709(bt709);
					y_original = renodx::color::y::from::BT2020(abs(input_color));
				} else {
					input_color = bt709;
					y_original = renodx::color::y::from::BT709(abs(bt709));
				}

				float y = y_original;

				y *= current_config.exposure;
				y = CustomizeLuminance(y, current_config.highlights, current_config.shadows);

				float3 per_channel_color;
				[branch] if (current_config.per_channel)
				{
					per_channel_color = input_color * (y_original > 0 ? (y / y_original) : 0);
				}
				else
				{
					per_channel_color = input_color;
				}

				float m_0 = (n / n_r);

				float3 color_output;
				[branch] if (current_config.tone_map_method == config::tone_map_method::DANIELE)
				{
					float m_1 = 0.5 * (m_0 + sqrt(m_0 * (m_0 + (4.0 * t_1))));
					float r_hit = r_hit_min + ((r_hit_max - r_hit_min) * (log(m_0) / log(10000.0 / 100.0)));

					float u = pow((r_hit / m_1) / ((r_hit / m_1) + 1.0), g);
					const float m = m_1 / u;
					const float w_i = log(n / 100.0) / log(2.0);
					const float c_t = (c_d / n_r) * (1.0 + (w_i * w_g));
					const float g_ip = 0.5 * (c_t + sqrt(c_t * (c_t + (4.0 * t_1))));
					const float g_ipp2 = -m_1 * pow(g_ip / m, 1.0 / g) / (pow(g_ip / m, 1.0 / g) - 1.0);
					const float w_2 = c / g_ipp2;
					const float s_2 = w_2 * m_1;
					float u_2 = pow((r_hit / m_1) / ((r_hit / m_1) + w_2), g);
					float m_2 = m_1 / u_2;

					[branch] if (current_config.per_channel)
					{
						float3 ts3 = pow(max(0, per_channel_color) / (per_channel_color + s_2), g) * m_2;
						float3 flared3 = max(0, (ts3 * ts3) / (ts3 + t_1));

						color_output = clamp(flared3, 0, m_0);
					}
					else
					{
						float ts = pow(max(0, y) / (y + s_2), g) * m_2;
						float flared = max(0, (ts * ts) / (ts + t_1));

						float y_new = clamp(flared, 0, m_0);

						color_output = input_color * (y_original > 0 ? (y_new / y_original) : 0);
					}
				}
				else if (current_config.tone_map_method == config::tone_map_method::REINHARD)
				{
					white_clip = max(white_clip, m_0);
					white_clip = CustomizeLuminance(white_clip, current_config.highlights, current_config.shadows, current_config.contrast);

					[branch] if (current_config.per_channel)
					{
						color_output = per_channel_color;
						color_output /= 0.18f;
						float3 signs = sign(color_output);
						color_output = abs(color_output);

						// No guard for oversized flare
						float3 new_flare = math::DivideSafe(color_output + current_config.flare, color_output, 1.f);

						float3 exponent = current_config.contrast * new_flare;

						color_output = pow(color_output, exponent);

						color_output *= 0.18f;

						color_output = ReinhardScalableExtended(
							color_output,
							white_clip,
							m_0,
							0,
							0.18f,
							current_config.mid_gray_nits / 100.f);

						color_output *= signs;
					}
					else
					{
						y /= 0.18f;

						// No guard for oversized flare
						float new_flare = math::DivideSafe(y + current_config.flare, y, 1.f);
						float exponent = current_config.contrast * new_flare;
						y = math::SignPow(y, exponent);
						y *= 0.18f;

						float y_new = ReinhardScalableExtended(
							y,
							white_clip,
							m_0,
							0,
							0.18f,
							current_config.mid_gray_nits / 100.f);

						color_output = input_color * (y_original > 0 ? (y_new / y_original) : 0);
					}
				}

				[branch] if (current_config.dechroma != 0.f || current_config.saturation != 1.f || current_config.hue_correction_strength != 0.f || current_config.blowout != 0.f)
				{
					color_output = renodx::color::convert::ColorSpaces(color_output, current_color_space, renodx::color::convert::COLOR_SPACE_BT709);
					current_color_space = renodx::color::convert::COLOR_SPACE_BT709;

					float3 perceptual_new;

					if (current_config.hue_correction_strength != 0.f) {
						float3 perceptual_old;
						float3 source = (current_config.hue_correction_type == config::hue_correction_type::INPUT) ? bt709 : current_config.hue_correction_source;

						[branch] switch (current_config.hue_correction_method)
						{
						case config::hue_correction_method::OKLAB:
						default:
							perceptual_new = renodx::color::oklab::from::BT709(color_output);
							perceptual_old = renodx::color::oklab::from::BT709(source);
							break;
						case config::hue_correction_method::ICTCP:
							perceptual_new = renodx::color::ictcp::from::BT709(color_output);
							perceptual_old = renodx::color::ictcp::from::BT709(source);
							break;
						case config::hue_correction_method::DARKTABLE_UCS:
							perceptual_new = renodx::color::dtucs::uvY::from::BT709(color_output).zxy;
							perceptual_old = renodx::color::dtucs::uvY::from::BT709(source).zxy;
							break;
						}

						// Save chrominance to apply back
						float chrominance_pre_adjust = length(perceptual_new.yz);

						perceptual_new.yz = lerp(perceptual_new.yz, perceptual_old.yz, current_config.hue_correction_strength);

						float chrominance_post_adjust = length(perceptual_new.yz);

						// Apply back previous chrominance

						perceptual_new.yz *= renodx::math::DivideSafe(chrominance_pre_adjust, chrominance_post_adjust, 1.f);
					} else {
						[branch] switch (current_config.hue_correction_method)
						{
						default:
						case config::hue_correction_method::OKLAB:
							perceptual_new = renodx::color::oklab::from::BT709(color_output);
							break;
						case config::hue_correction_method::ICTCP:
							perceptual_new = renodx::color::ictcp::from::BT709(color_output);
							break;
						case config::hue_correction_method::DARKTABLE_UCS:
							perceptual_new = renodx::color::dtucs::uvY::from::BT709(color_output).zxy;
							break;
						}
					}

					if (current_config.dechroma != 0.f) {
						perceptual_new.yz *= lerp(1.f, 0.f, saturate(pow(y_original / (10000.f / 100.f), (1.f - current_config.dechroma))));
					}

					if (current_config.blowout != 0.f) {
						float percent_max = saturate(y_original * 100.f / 10000.f);
						// positive = 1 to 0, negative = 1 to 2
						float blowout_strength = 100.f;
						float blowout_change = pow(1.f - percent_max, blowout_strength * abs(current_config.blowout));
						if (current_config.blowout < 0) {
							blowout_change = (2.f - blowout_change);
						}

						perceptual_new.yz *= blowout_change;
					}

					perceptual_new.yz *= current_config.saturation;

					[branch] if (current_config.hue_correction_method == config::hue_correction_method::OKLAB)
					{
						color_output = renodx::color::bt709::from::OkLab(perceptual_new);
					}
					else if (current_config.hue_correction_method == config::hue_correction_method::ICTCP)
					{
						color_output = renodx::color::bt709::from::ICtCp(perceptual_new);
					}
					else if (current_config.hue_correction_method == config::hue_correction_method::DARKTABLE_UCS)
					{
						color_output = renodx::color::bt709::from::dtucs::uvY(perceptual_new.yzx);
					}
				}
				else {
					// noop
				}

					[branch] if (current_config.clamp_color_space != -1.f)
				{
					color_output = renodx::color::convert::ColorSpaces(color_output, current_color_space, current_config.clamp_color_space);
					color_output = max(0, color_output);
					current_color_space = current_config.clamp_color_space;
				}

				[branch] if (current_config.clamp_peak != -1.f)
				{
					color_output = renodx::color::convert::ColorSpaces(color_output, current_color_space, current_config.clamp_peak);
					color_output = min(color_output, m_0);
					current_color_space = current_config.clamp_peak;
				}

				color_output = renodx::color::convert::ColorSpaces(color_output, current_color_space, renodx::color::convert::COLOR_SPACE_BT709);

				return color_output;
			}

			float3 BT709(
				float3 bt709,
				float nits_peak,
				float mid_gray_value,
				float mid_gray_nits,
				float exposure,
				float highlights,
				float shadows,  // 0 = 0.10, 1.f = 0, >1 = contrast
				float contrast,
				float saturation,
				float dechroma,
				float flare,
				float hue_correction_strength,
				float3 hue_correction_source)
			{
				Config config = config::Create();
				config.nits_peak = nits_peak;
				config.mid_gray_value = mid_gray_value;
				config.mid_gray_nits = mid_gray_nits;
				config.exposure = exposure;
				config.highlights = highlights;
				config.shadows = shadows;
				config.contrast = contrast;
				config.saturation = saturation;
				config.dechroma = dechroma;
				config.flare = flare;
				config.hue_correction_strength = hue_correction_strength;
				config.hue_correction_source = hue_correction_source;
				config.hue_correction_type = renodrt::config::hue_correction_type::CUSTOM;
				return BT709(bt709, config);
			}
			float3 BT709(
				float3 bt709,
				float nits_peak = 1000.f / 203.f * 100.f,
				float mid_gray_value = 0.18f,
				float mid_gray_nits = 10.f,
				float exposure = 1.f,
				float highlights = 1.f,
				float shadows = 1.f,
				float contrast = 1.1f,
				float saturation = 1.f,
				float dechroma = 0.5f,
				float flare = 0.f,
				float hue_correction_strength = 1.f)
			{
				return BT709(
					bt709,
					nits_peak,
					mid_gray_value,
					mid_gray_nits,
					exposure,
					highlights,
					shadows,
					contrast,
					saturation,
					dechroma,
					flare,
					hue_correction_strength,
					bt709);
			}

			float3 NeutralSDR(float3 bt709, bool per_channel = false)
			{
				Config renodrt_config = config::Create();
				renodrt_config.nits_peak = 100.f;
				renodrt_config.mid_gray_value = 0.18f;
				renodrt_config.mid_gray_nits = 18.f;
				renodrt_config.exposure = 1.f;
				renodrt_config.highlights = 1.f;
				renodrt_config.shadows = 1.f;
				renodrt_config.contrast = 1.f;
				renodrt_config.saturation = 1.f;
				renodrt_config.dechroma = 0.f;
				renodrt_config.flare = 0.f;
				renodrt_config.per_channel = per_channel;
				renodrt_config.hue_correction_strength = 0.f;
				renodrt_config.working_color_space = 0u;
				renodrt_config.clamp_color_space = 0u;

				return BT709(bt709, renodrt_config);
			}

		}  // namespace renodrt

		namespace DICEPlus
		{

			struct DICEConfig
			{
				float peak_nits;
				float game_nits;
				float gamma_correction;
				float exposure;
				float highlights;
				float shadows;
				float contrast;
				float saturation;
				float mid_gray_value;
				float mid_gray_nits;
				float reno_drt_highlights;
				float reno_drt_shadows;
				float reno_drt_contrast;
				float reno_drt_saturation;
				float reno_drt_dechroma;
				float reno_drt_flare;
				float hue_correction_type;
				float hue_correction_strength;
				float3 hue_correction_color;
				uint reno_drt_hue_correction_method;
				uint reno_drt_tone_map_method;
				uint reno_drt_working_color_space;
				bool reno_drt_per_channel;
				float reno_drt_blowout;
				float reno_drt_clamp_color_space;
				float reno_drt_clamp_peak;
				float reno_drt_white_clip;
			};

			namespace config
			{

				DICEConfig Create(
					float peak_nits = 203.f,
					float game_nits = 203.f,
					float gamma_correction = 0,
					float exposure = 1.f,
					float highlights = 1.f,
					float shadows = 1.f,
					float contrast = 1.f,
					float saturation = 1.f,
					float mid_gray_value = 0.18f,
					float mid_gray_nits = 18.f,
					float reno_drt_highlights = 1.f,
					float reno_drt_shadows = 1.f,
					float reno_drt_contrast = 1.f,
					float reno_drt_saturation = 1.f,
					float reno_drt_dechroma = 0.5f,
					float reno_drt_flare = 0.f,
					float hue_correction_type = renodx::tonemap::renodrt::config::hue_correction_type::INPUT,
					float hue_correction_strength = 1.f,
					float3 hue_correction_color = 0,
					uint reno_drt_hue_correction_method = renodrt::config::hue_correction_method::OKLAB,
					uint reno_drt_tone_map_method = renodrt::config::tone_map_method::DANIELE,
					uint reno_drt_working_color_space = 0u,
					bool reno_drt_per_channel = false,
					float reno_drt_blowout = 0,
					float reno_drt_clamp_color_space = 2.f,
					float reno_drt_clamp_peak = 1.f,
					float reno_drt_white_clip = 100.f)
				{
					const DICEConfig tm_config = {
						peak_nits,
						game_nits,
						gamma_correction,
						exposure,
						highlights,
						shadows,
						contrast,
						saturation,
						mid_gray_value,
						mid_gray_nits,
						reno_drt_highlights,
						reno_drt_shadows,
						reno_drt_contrast,
						reno_drt_saturation,
						reno_drt_dechroma,
						reno_drt_flare,
						hue_correction_type,
						hue_correction_strength,
						hue_correction_color,
						reno_drt_hue_correction_method,
						reno_drt_tone_map_method,
						reno_drt_working_color_space,
						reno_drt_per_channel,
						reno_drt_blowout,
						reno_drt_clamp_color_space,
						reno_drt_clamp_peak,
						reno_drt_white_clip
					};
					return tm_config;
				}
			}  // namespace config

			float3 ApplyDICEPlus(float3 color, DICEConfig DICEConfig)
			{
				const float RhPeak = DICEConfig.peak_nits / DICEConfig.game_nits;
				float y;
				if (DICEConfig.reno_drt_working_color_space == 0u) {
					color = max(0, color);
					y = renodx::color::y::from::BT709(color * DICEConfig.exposure);
				} else if (DICEConfig.reno_drt_working_color_space == 1u) {
					color = renodx::color::bt2020::from::BT709(color);
					y = renodx::color::y::from::BT2020(abs(color * DICEConfig.exposure));
				} else if (DICEConfig.reno_drt_working_color_space == 2u) {
					color = renodx::color::ap1::from::BT709(color);
					y = renodx::color::y::from::AP1(color * DICEConfig.exposure);
				}
				color = renodx::color::grade::UserColorGrading(color, DICEConfig.exposure, DICEConfig.highlights, DICEConfig.shadows, DICEConfig.contrast);
				color = renodx::tonemap::ReinhardScalable(color, RhPeak, 0.f, 0.18f, DICEConfig.mid_gray_value);
				if (DICEConfig.reno_drt_working_color_space == 1u) {
					color = renodx::color::bt709::from::BT2020(color);
				} else if (DICEConfig.reno_drt_working_color_space == 2u) {
					color = renodx::color::bt709::from::AP1(color);
				}
				if (DICEConfig.reno_drt_dechroma != 0.f || DICEConfig.saturation != 1.f || DICEConfig.reno_drt_blowout != 0.f || DICEConfig.hue_correction_strength != 0.f) {
					float3 perceptual_new;

					if (DICEConfig.reno_drt_hue_correction_method == 0u) {
						perceptual_new = renodx::color::oklab::from::BT709(color);
					} else if (DICEConfig.reno_drt_hue_correction_method == 1u) {
						perceptual_new = renodx::color::ictcp::from::BT709(color);
					} else if (DICEConfig.reno_drt_hue_correction_method == 2u) {
						perceptual_new = renodx::color::dtucs::uvY::from::BT709(color).zxy;
					}

					if (DICEConfig.hue_correction_strength != 0.f) {
						float3 perceptual_old;
						if (DICEConfig.hue_correction_type != renodx::tonemap::renodrt::config::hue_correction_type::CUSTOM) {
							DICEConfig.hue_correction_color = color;
						}

						if (DICEConfig.reno_drt_hue_correction_method == 0u) {
							perceptual_old = renodx::color::oklab::from::BT709(DICEConfig.hue_correction_color);
						} else if (DICEConfig.reno_drt_hue_correction_method == 1u) {
							perceptual_old = renodx::color::ictcp::from::BT709(DICEConfig.hue_correction_color);
						} else if (DICEConfig.reno_drt_hue_correction_method == 2u) {
							perceptual_old = renodx::color::dtucs::uvY::from::BT709(DICEConfig.hue_correction_color).zxy;
						}
						// Save chrominance to apply black
						float chrominance_pre_adjust = distance(perceptual_new.yz, 0);
						perceptual_new.yz = lerp(perceptual_new.yz, perceptual_old.yz, DICEConfig.hue_correction_strength);
						float chrominance_post_adjust = distance(perceptual_new.yz, 0);
						// Apply back previous chrominance
						perceptual_new.yz *= renodx::math::DivideSafe(chrominance_pre_adjust, chrominance_post_adjust, 1.f);
					}
					if (DICEConfig.reno_drt_dechroma != 0.f) {
						perceptual_new.yz *= lerp(1.f, 0.f, saturate(pow(y / (10000.f / 100.f), (1.f - DICEConfig.reno_drt_dechroma))));
					}

					if (DICEConfig.reno_drt_blowout != 0.f) {
						float percent_max = saturate(y * 100.f / 10000.f);
						// positive = 1 to 0, negative = 1 to 2
						float blowout_strength = 100.f;
						float blowout_change = pow(1.f - percent_max, blowout_strength * abs(DICEConfig.reno_drt_blowout));
						if (DICEConfig.reno_drt_blowout < 0) {
							blowout_change = (2.f - blowout_change);
						}
						perceptual_new.yz *= blowout_change;
					}
					perceptual_new.yz *= DICEConfig.saturation;

					if (DICEConfig.reno_drt_hue_correction_method == 0u) {
						color = renodx::color::bt709::from::OkLab(perceptual_new);
					} else if (DICEConfig.reno_drt_hue_correction_method == 1u) {
						color = renodx::color::bt709::from::ICtCp(perceptual_new);
					} else if (DICEConfig.reno_drt_hue_correction_method == 2u) {
						color = renodx::color::bt709::from::dtucs::uvY(perceptual_new.yzx);
					}
				}
				color = renodx::color::bt709::clamp::BT2020(color);
				return color;
			}

		}  // namespace ReinhardPlus

	}  // namespace tonemap
}  // namespace renodx

#endif  // SRC_SHADERS_RENODX_HLSL_

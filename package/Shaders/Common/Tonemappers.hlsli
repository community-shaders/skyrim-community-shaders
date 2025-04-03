namespace Tonemap
{

	namespace Uncharted2
	{
		float ApplyCurve(float x, float a, float b, float c, float d, float e, float f)
		{
			return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
		}

		float3 ApplyCurve(float3 x, float a, float b, float c, float d, float e, float f)
		{
			return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
		}

		float3 Apply(float3 x, float a = 0.22, float b = 0.30, float c = 0.10, float d = 0.20, float e = 0.01, float f = 0.30, float W = 11.2)
		{
			return ApplyCurve(x * 2.f, a, b, c, d, e, f) / ApplyCurve(W, a, b, c, d, e, f);
		}

		float Apply(float x, float a = 0.22, float b = 0.30, float c = 0.10, float d = 0.20, float e = 0.01, float f = 0.30, float W = 11.2)
		{
			return ApplyCurve(x * 2.f, a, b, c, d, e, f) / ApplyCurve(W, a, b, c, d, e, f);
		}

	}  // namespace Uncharted2

	namespace Reinhard
	{
		// Reinhard tonemap operator
		// Reinhard et al. "Photographic tone reproduction for digital images." ACM Transactions on Graphics. 21. 2002.
		// http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
		static float3 Apply(float3 color)
		{
			return color / (1.0f + color);
		}
	}  // namespace Reinhard

	namespace ReinhardJodie
	{
		static float3 Apply(float3 color)
		{
			float l = dot(color, float3(0.2126f, 0.7152f, 0.0722f));  // BT.709 luminance
			float3 tv = color / (1.0f + color);
			return lerp(color / (1.0f + l), tv, tv);
		}
	}  // namespace ReinhardJodie

	namespace NarkowiczACES
	{
		// ACES Filmic tonemap operator
		// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
		float3 Apply(float3 x)
		{
			float a = 2.51f;
			float b = 0.03f;
			float c = 2.43f;
			float d = 0.59f;
			float e = 0.14f;
			return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
		}

	}  // namespace NarkowiczACES
	namespace ExponentialRolloff
	{
/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_GENERATOR(T)                                    \
	T Apply(T input, float rolloff_start = 0.20f, float output_max = 1.0f) \
	{                                                                      \
		T rolloff_size = output_max - rolloff_start;                       \
		T overage = -max((T)0, input - rolloff_start);                     \
		T rolloff_value = (T)1.0f - exp(overage / rolloff_size);           \
		T new_overage = mad(rolloff_size, rolloff_value, overage);         \
		return input + new_overage;                                        \
	}

		EXPONENTIALROLLOFF_GENERATOR(float)
		EXPONENTIALROLLOFF_GENERATOR(float3)
#undef EXPONENTIALROLLOFF_GENERATOR
	}

	namespace Lottes
	{

// Lottes 2016 Tonemapper
// https://gpuopen.com/wp-content/uploads/2016/03/GdcVdrLottes.pdf
// Tonemap(x) = (x^a) / ((x^a)^d * b + c)
#define LOTTES_TONEMAP_GENERATOR(T)                                                                                                   \
	T Apply(T input,                                                                                                                  \
		float contrast, /* a */                                                                                                       \
		float shoulder, /* d */                                                                                                       \
		float hdr_max,                                                                                                                \
		float mid_in,                                                                                                                 \
		float mid_out)                                                                                                                \
	{                                                                                                                                 \
		float mid_in_pow_a = pow(mid_in, contrast);                                                                                   \
		float mid_in_pow_ad = pow(mid_in_pow_a, shoulder);                                                                            \
		float hdr_max_pow_a = pow(hdr_max, contrast);                                                                                 \
		float hdr_max_pow_ad = pow(hdr_max_pow_a, shoulder);                                                                          \
                                                                                                                                      \
		float b = (-mid_in_pow_a + hdr_max_pow_a * mid_out) / ((hdr_max_pow_ad - mid_in_pow_ad) * mid_out);                           \
                                                                                                                                      \
		float c = (hdr_max_pow_ad * mid_in - hdr_max_pow_a * mid_in_pow_ad * mid_out) / ((hdr_max_pow_ad - mid_in_pow_ad) * mid_out); \
                                                                                                                                      \
		T input_pow_a = pow(input, contrast);                                                                                         \
		T denominator = pow(input_pow_a, shoulder) * b + c;                                                                           \
		return input_pow_a / denominator;                                                                                             \
	}

		LOTTES_TONEMAP_GENERATOR(float)
		LOTTES_TONEMAP_GENERATOR(float3)
#undef LOTTES_TONEMAP_GENERATOR

	}

}

#include "ACES2.h"

// ACES 2.0 Output Transform — CPU-side parameter and table computation
// Faithfully ported from the official aces-aswf/aces-core CTL reference (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)
//
// Key CTL source files:
//   Lib.Academy.OutputTransform.ctl — all color appearance model, compression, and gamut mapping
//   Lib.Academy.Tonescale.ctl      — Michaelis-Menten based tonescale

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ACES2
{
	// ========================================================================
	// Math helpers
	// ========================================================================

	static constexpr float PI = 3.14159265358979323846f;

	struct float3
	{
		float x, y, z;
		float& operator[](int i) { return (&x)[i]; }
		float operator[](int i) const { return (&x)[i]; }
	};

	struct Mat3
	{
		float m[3][3];
		float* operator[](int i) { return m[i]; }
		const float* operator[](int i) const { return m[i]; }
	};

	static float3 operator*(float s, float3 v) { return { s * v.x, s * v.y, s * v.z }; }
	static float3 operator+(float3 a, float3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	static float3 operator-(float3 a, float3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	static float3 lerp3(float3 a, float3 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }

	static float3 mul_v_m(float3 v, const Mat3& m)
	{
		// v * M (row vector times matrix): v[0]*m[0][j] + v[1]*m[1][j] + v[2]*m[2][j]
		return {
			v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
			v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
			v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]
		};
	}

	static float3 mul_m_v(const Mat3& m, float3 v)
	{
		// M * v (matrix times column vector)
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
		};
	}

	static Mat3 mul_mm(const Mat3& a, const Mat3& b)
	{
		Mat3 r{};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++)
					r[i][j] += a[i][k] * b[k][j];
		return r;
	}

	static Mat3 scale_diag(const Mat3& m, float3 s)
	{
		// Scale each column by corresponding s component
		Mat3 r = m;
		for (int i = 0; i < 3; i++) {
			r[i][0] *= s.x;
			r[i][1] *= s.y;
			r[i][2] *= s.z;
		}
		return r;
	}

	static Mat3 scale_mat(const Mat3& m, float s)
	{
		Mat3 r;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				r[i][j] = m[i][j] * s;
		return r;
	}

	static float det3(const Mat3& m)
	{
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
		       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	}

	static Mat3 inverse(const Mat3& m)
	{
		float d = det3(m);
		if (std::fabs(d) < 1e-30f)
			d = 1e-30f;
		Mat3 r;
		r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d;
		r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d;
		r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
		r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d;
		r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d;
		r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d;
		r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d;
		r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d;
		r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
		return r;
	}

	static float maxf(float a, float b) { return a > b ? a : b; }
	static float minf(float a, float b) { return a < b ? a : b; }

	static float wrap_to_360(float hue)
	{
		float y = std::fmod(hue, 360.f);
		if (y < 0.f)
			y += 360.f;
		return y;
	}

	static int hue_position_in_uniform_table(float hue, int table_size)
	{
		float wrapped = wrap_to_360(hue);
		return (int)(wrapped / 360.f * table_size);
	}

	static float base_hue_for_position(int i, int table_size)
	{
		return (float)i * 360.f / (float)table_size;
	}

	// ========================================================================
	// Color space definitions
	// ========================================================================

	// Chromaticities: {rx, ry, gx, gy, bx, by, wx, wy}
	struct Chromaticities
	{
		float rx, ry, gx, gy, bx, by, wx, wy;
	};

	// AP0 (ACES 2065-1)
	static constexpr Chromaticities AP0_CHROM = { 0.73470f, 0.26530f, 0.00000f, 1.00000f, 0.00010f, -0.07700f, 0.32168f, 0.33767f };
	// AP1 (ACEScg) = Reach primaries
	static constexpr Chromaticities AP1_CHROM = { 0.713f, 0.293f, 0.165f, 0.830f, 0.128f, 0.044f, 0.32168f, 0.33767f };
	// sRGB / Rec.709
	static constexpr Chromaticities SRGB_CHROM = { 0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f };
	// P3-D65
	static constexpr Chromaticities P3D65_CHROM = { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f };
	// Rec.2020
	static constexpr Chromaticities REC2020_CHROM = { 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f };

	// CAM16 matrix (fixed)
	static const Mat3 MATRIX_16 = { {
		{ 0.3640744835f, 0.5947008156f, 0.04110127349f },
		{ -0.2222450987f, 1.0738554823f, 0.14794533610f },
		{ -0.0020676190f, 0.0488260453f, 0.95038755696f },
	} };

	// Build XYZ<->RGB matrices from chromaticities
	static Mat3 RGBtoXYZ(const Chromaticities& c, float Y = 1.0f)
	{
		// Build XYZ primaries from chromaticities
		float3 r = { c.rx / c.ry, 1.0f, (1.0f - c.rx - c.ry) / c.ry };
		float3 g = { c.gx / c.gy, 1.0f, (1.0f - c.gx - c.gy) / c.gy };
		float3 b = { c.bx / c.by, 1.0f, (1.0f - c.bx - c.by) / c.by };
		float3 w = { c.wx / c.wy * Y, Y, (1.0f - c.wx - c.wy) / c.wy * Y };

		// Build matrix from primaries (columns)
		Mat3 P = { {
			{ r.x, g.x, b.x },
			{ r.y, g.y, b.y },
			{ r.z, g.z, b.z },
		} };
		Mat3 Pinv = inverse(P);
		float3 S = mul_m_v(Pinv, w);

		// Scale columns by S
		Mat3 M;
		for (int i = 0; i < 3; i++) {
			M[i][0] = P[i][0] * S.x;
			M[i][1] = P[i][1] * S.y;
			M[i][2] = P[i][2] * S.z;
		}
		return M;
	}

	static Mat3 XYZtoRGB(const Chromaticities& c, float Y = 1.0f)
	{
		return inverse(RGBtoXYZ(c, Y));
	}

	// ========================================================================
	// CAM parameters (constants from official CTL)
	// ========================================================================
	static constexpr float ref_luminance = 100.f;
	static constexpr float L_A = 100.f;
	static constexpr float Y_b = 20.f;
	static constexpr float surround_c = 0.9f;    // Dim surround
	static constexpr float surround_Nc = 0.59f;  // Dim surround
	static constexpr float surround_F_L = 0.9f;  // Dim surround

	static constexpr float J_scale = 100.f;
	static constexpr float cam_nl_Y_reference = 100.f;
	static constexpr float cam_nl_offset = 0.2713f * cam_nl_Y_reference;  // 27.13
	static constexpr float cam_nl_scale = 4.0f * cam_nl_Y_reference;      // 400.0

	static const float model_gamma_value = surround_Nc * (1.48f + std::sqrt(Y_b / ref_luminance));  // ~1.1357

	// Chroma compression constants
	static constexpr float chroma_compress = 2.4f;
	static constexpr float chroma_compress_fact = 3.3f;
	static constexpr float chroma_expand = 1.3f;
	static constexpr float chroma_expand_fact = 0.69f;
	static constexpr float chroma_expand_thr = 0.5f;

	// Gamut compression constants
	static constexpr float smooth_cusps = 0.12f;
	static constexpr float smooth_m = 0.27f;
	static constexpr float cusp_mid_blend = 1.3f;
	static constexpr float focus_gain_blend = 0.3f;
	static constexpr float focus_adjust_gain = 0.55f;
	static constexpr float focus_distance = 1.35f;
	static constexpr float focus_distance_scaling = 1.75f;
	static constexpr float compression_threshold = 0.75f;

	static constexpr int cuspCornerCount = 6;
	static constexpr int totalCornerCount = cuspCornerCount + 2;
	static constexpr int max_sorted_corners = 2 * cuspCornerCount;

	static constexpr float gamma_minimum = 0.0f;
	static constexpr float gamma_maximum = 5.0f;
	static constexpr float gamma_search_step = 0.4f;
	static constexpr float gamma_accuracy = 1e-5f;

	static constexpr float reach_cusp_tolerance = 1e-3f;
	static constexpr float display_cusp_tolerance = 1e-7f;

	static constexpr int test_count = 5;
	static constexpr float testPositions[test_count] = { 0.01f, 0.1f, 0.5f, 0.8f, 0.99f };

	// ========================================================================
	// Post-adaptation cone response compression (official CTL)
	// ========================================================================

	static float _post_adaptation_fwd(float Rc)
	{
		float F_L_Y = std::pow(Rc, 0.42f);
		return F_L_Y / (cam_nl_offset + F_L_Y);
	}

	static float _post_adaptation_inv(float Ra)
	{
		float Ra_lim = minf(Ra, 0.99f);
		float F_L_Y = cam_nl_offset * Ra_lim / (1.f - Ra_lim);
		return std::pow(F_L_Y, 1.f / 0.42f);
	}

	static float post_adaptation_fwd(float v)
	{
		return std::copysign(_post_adaptation_fwd(std::fabs(v)), v);
	}

	static float post_adaptation_inv(float v)
	{
		return std::copysign(_post_adaptation_inv(std::fabs(v)), v);
	}

	// ========================================================================
	// CAM model: J<->Y conversions (optimized achromatic path)
	// ========================================================================

	// Internal JMh parameter set (CPU-side, full)
	struct JMhParamsCPU
	{
		Mat3 MATRIX_RGB_to_CAM16_c;
		Mat3 MATRIX_CAM16_c_to_RGB;
		Mat3 MATRIX_cone_response_to_Aab;
		Mat3 MATRIX_Aab_to_cone_response;
		float F_L_n;
		float cz;
		float inv_cz;
		float A_w_J;
		float inv_A_w_J;
	};

	static float Achromatic_n_to_J(float A, float cz)
	{
		return J_scale * std::pow(A, cz);
	}

	static float J_to_Achromatic_n(float J, float inv_cz)
	{
		return std::pow(J / J_scale, inv_cz);
	}

	static float _A_to_Y(float A, const JMhParamsCPU& p)
	{
		float Ra = p.A_w_J * A;
		return _post_adaptation_inv(Ra) / p.F_L_n;
	}

	static float J_to_Y(float J, const JMhParamsCPU& p)
	{
		float abs_J = std::fabs(J);
		return _A_to_Y(J_to_Achromatic_n(abs_J, p.inv_cz), p);
	}

	static float Y_to_J(float Y, const JMhParamsCPU& p)
	{
		float abs_Y = std::fabs(Y);
		float Ra = _post_adaptation_fwd(abs_Y * p.F_L_n);
		float J = Achromatic_n_to_J(Ra * p.inv_A_w_J, p.cz);
		return std::copysign(J, Y);
	}

	// ========================================================================
	// CAM model: RGB <-> JMh conversions
	// ========================================================================

	static float3 RGB_to_Aab(float3 RGB, const JMhParamsCPU& p)
	{
		float3 rgb_m = mul_v_m(RGB, p.MATRIX_RGB_to_CAM16_c);
		float3 rgb_a = {
			post_adaptation_fwd(rgb_m.x),
			post_adaptation_fwd(rgb_m.y),
			post_adaptation_fwd(rgb_m.z)
		};
		return mul_v_m(rgb_a, p.MATRIX_cone_response_to_Aab);
	}

	static float3 Aab_to_JMh(float3 Aab, const JMhParamsCPU& p)
	{
		if (Aab.x <= 0.f)
			return { 0.f, 0.f, 0.f };
		float J = Achromatic_n_to_J(Aab.x, p.cz);
		float M = std::sqrt(Aab.y * Aab.y + Aab.z * Aab.z);
		float h_rad = std::atan2(Aab.z, Aab.y);
		float h = wrap_to_360(h_rad * 180.f / PI);
		return { J, M, h };
	}

	static float3 RGB_to_JMh(float3 RGB, const JMhParamsCPU& p)
	{
		return Aab_to_JMh(RGB_to_Aab(RGB, p), p);
	}

	static float3 JMh_to_Aab(float3 JMh, const JMhParamsCPU& p)
	{
		float hr = JMh.z * PI / 180.f;
		float A = J_to_Achromatic_n(JMh.x, p.inv_cz);
		float a = JMh.y * std::cos(hr);
		float b = JMh.y * std::sin(hr);
		return { A, a, b };
	}

	static float3 Aab_to_RGB(float3 Aab, const JMhParamsCPU& p)
	{
		float3 rgb_a = mul_v_m(Aab, p.MATRIX_Aab_to_cone_response);
		float3 rgb_m = {
			post_adaptation_inv(rgb_a.x),
			post_adaptation_inv(rgb_a.y),
			post_adaptation_inv(rgb_a.z)
		};
		return mul_v_m(rgb_m, p.MATRIX_CAM16_c_to_RGB);
	}

	static float3 JMh_to_RGB(float3 JMh, const JMhParamsCPU& p)
	{
		return Aab_to_RGB(JMh_to_Aab(JMh, p), p);
	}

	// ========================================================================
	// init_JMhParams — faithful port of official CTL
	// ========================================================================

	static JMhParamsCPU init_JMhParams(const Chromaticities& prims)
	{
		Mat3 RGB_TO_XYZ = RGBtoXYZ(prims, 1.0f);

		// Reference white in XYZ
		float3 one = { ref_luminance, ref_luminance, ref_luminance };
		float3 XYZ_w = mul_v_m(one, RGB_TO_XYZ);
		float Y_w = XYZ_w.y;

		// Step 0: sharpened RGB of white
		float3 RGB_w = mul_v_m(XYZ_w, MATRIX_16);

		// Viewing condition parameters
		float k = 1.f / (5.f * L_A + 1.f);
		float k4 = k * k * k * k;
		float F_L = 0.2f * k4 * (5.f * L_A) + 0.1f * std::pow(1.f - k4, 2.f) * std::pow(5.f * L_A, 1.f / 3.f);

		float F_L_n = F_L / ref_luminance;
		float cz = model_gamma_value;

		// Chromatic adaptation
		float3 D_RGB = {
			F_L_n * Y_w / RGB_w.x,
			F_L_n * Y_w / RGB_w.y,
			F_L_n * Y_w / RGB_w.z
		};

		float3 RGB_wc = {
			D_RGB.x * RGB_w.x,
			D_RGB.y * RGB_w.y,
			D_RGB.z * RGB_w.z
		};

		float3 RGB_Aw = {
			post_adaptation_fwd(RGB_wc.x),
			post_adaptation_fwd(RGB_wc.y),
			post_adaptation_fwd(RGB_wc.z)
		};

		// Base cone-response-to-Aab matrix
		// In official CTL: base_cone_response_to_Aab = {{2, 1, 1/9}, {1, -12/11, 1/9}, {1/20, 1/11, -2/9}}
		Mat3 base_crtab = { {
			{ 2.f, 1.f, 1.f / 9.f },
			{ 1.f, -12.f / 11.f, 1.f / 9.f },
			{ 1.f / 20.f, 1.f / 11.f, -2.f / 9.f },
		} };

		// Prescale: mult_f_f33(cam_nl_scale, IDENTITY) * base = cam_nl_scale * base
		Mat3 cone_response_to_Aab = scale_mat(base_crtab, cam_nl_scale);

		// Achromatic white: A_w = crtab[0] . RGB_Aw (first column of crtab transposed)
		float A_w = cone_response_to_Aab[0][0] * RGB_Aw.x + cone_response_to_Aab[1][0] * RGB_Aw.y + cone_response_to_Aab[2][0] * RGB_Aw.z;
		float A_w_J = _post_adaptation_fwd(F_L);

		// Build MATRIX_RGB_to_CAM16_c: combined RGB -> XYZ -> CAM16 with chromatic adaptation
		// M1 = RGB_TO_XYZ * MATRIX_16
		Mat3 M1 = mul_mm(RGB_TO_XYZ, MATRIX_16);
		// M2 = ref_luminance * IDENTITY
		Mat3 IDENT = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
		Mat3 M2 = scale_mat(IDENT, ref_luminance);
		Mat3 MATRIX_RGB_to_CAM16 = mul_mm(M1, M2);
		// Apply per-channel D_RGB scaling
		Mat3 MATRIX_RGB_to_CAM16_c = scale_diag(MATRIX_RGB_to_CAM16, D_RGB);

		// Build final cone_response_to_Aab with normalization
		// Official: row[*][0] /= A_w, row[*][1,2] *= 43 * surround_F_L
		float eccentricity_factor = 43.f * surround_F_L;
		Mat3 MATRIX_cone_response_to_Aab;
		for (int i = 0; i < 3; i++) {
			MATRIX_cone_response_to_Aab[i][0] = cone_response_to_Aab[i][0] / A_w;
			MATRIX_cone_response_to_Aab[i][1] = cone_response_to_Aab[i][1] * eccentricity_factor;
			MATRIX_cone_response_to_Aab[i][2] = cone_response_to_Aab[i][2] * eccentricity_factor;
		}

		JMhParamsCPU p;
		p.MATRIX_RGB_to_CAM16_c = MATRIX_RGB_to_CAM16_c;
		p.MATRIX_CAM16_c_to_RGB = inverse(MATRIX_RGB_to_CAM16_c);
		p.MATRIX_cone_response_to_Aab = MATRIX_cone_response_to_Aab;
		p.MATRIX_Aab_to_cone_response = inverse(MATRIX_cone_response_to_Aab);
		p.F_L_n = F_L_n;
		p.cz = cz;
		p.inv_cz = 1.f / cz;
		p.A_w_J = A_w_J;
		p.inv_A_w_J = 1.f / A_w_J;

		return p;
	}

	// ========================================================================
	// Tonescale — Michaelis-Menten curve (official CTL)
	// ========================================================================

	struct TSParamsCPU
	{
		float n, n_r, g, t_1, c_t, s_2, u_2, m_2;
		float forward_limit, inverse_limit, log_peak;
	};

	static TSParamsCPU init_TSParams(float peakLuminance)
	{
		TSParamsCPU p{};
		const float n = peakLuminance;
		const float n_r = 100.0f;
		const float g = 1.15f;
		const float c = 0.18f;
		const float c_d = 10.013f;
		const float w_g = 0.14f;
		const float t_1 = 0.04f;
		const float r_hit_min = 128.f;
		const float r_hit_max = 896.f;

		const float r_hit = r_hit_min + (r_hit_max - r_hit_min) * (std::log(n / n_r) / std::log(10000.f / 100.f));
		const float m_0 = n / n_r;
		const float m_1 = 0.5f * (m_0 + std::sqrt(m_0 * (m_0 + 4.f * t_1)));
		const float u = std::pow((r_hit / m_1) / ((r_hit / m_1) + 1.f), g);
		const float w_i = std::log(n / 100.f) / std::log(2.f);
		const float c_t = c_d / n_r * (1.f + w_i * w_g);
		const float g_ip = 0.5f * (c_t + std::sqrt(c_t * (c_t + 4.f * t_1)));
		const float g_ipp2_base = std::pow(g_ip / (m_1 / u), 1.f / g);
		const float g_ipp2 = -(m_1 * g_ipp2_base) / (g_ipp2_base - 1.f);
		const float w_2 = c / g_ipp2;
		const float s_2 = w_2 * m_1;
		const float u_2 = std::pow((r_hit / m_1) / ((r_hit / m_1) + w_2), g);
		const float m_2 = m_1 / u_2;

		p.n = n;
		p.n_r = n_r;
		p.g = g;
		p.t_1 = t_1;
		p.c_t = c_t;
		p.s_2 = s_2;
		p.u_2 = u_2;
		p.m_2 = m_2;
		p.forward_limit = 8.f * r_hit;
		p.inverse_limit = n / (u_2 * n_r);
		p.log_peak = std::log10(n / n_r);

		return p;
	}

	// ========================================================================
	// Table building — faithful port from official CTL
	// ========================================================================

	// Generate unit cube cusp corner RGB (R, Y, G, C, B, M)
	static float3 generate_unit_cube_cusp_corner(int corner)
	{
		float3 result;
		result.x = (((corner + 1) % cuspCornerCount) < 3) ? 1.f : 0.f;
		result.y = (((corner + 5) % cuspCornerCount) < 3) ? 1.f : 0.f;
		result.z = (((corner + 3) % cuspCornerCount) < 3) ? 1.f : 0.f;
		return result;
	}

	static bool any_below_zero(float3 v)
	{
		return v.x < 0.f || v.y < 0.f || v.z < 0.f;
	}

	static bool outside_hull(float3 rgb, float maxVal)
	{
		return rgb.x > maxVal || rgb.y > maxVal || rgb.z > maxVal;
	}

	// Build limiting gamut cusp corners
	static void build_limiting_cusp_corners(
		float3 RGB_corners[totalCornerCount],
		float3 JMh_corners[totalCornerCount],
		const JMhParamsCPU& params,
		float peakLuminance)
	{
		float3 temp_RGB[cuspCornerCount];
		float3 temp_JMh[cuspCornerCount];

		int min_index = 0;
		for (int i = 0; i < cuspCornerCount; i++) {
			temp_RGB[i] = (peakLuminance / ref_luminance) * generate_unit_cube_cusp_corner(i);
			temp_JMh[i] = RGB_to_JMh(temp_RGB[i], params);
			if (temp_JMh[i].z < temp_JMh[min_index].z)
				min_index = i;
		}

		// Rotate placing lowest hue at [1]
		for (int i = 0; i < cuspCornerCount; i++) {
			RGB_corners[i + 1] = temp_RGB[(i + min_index) % cuspCornerCount];
			JMh_corners[i + 1] = temp_JMh[(i + min_index) % cuspCornerCount];
		}

		// Wrap
		RGB_corners[0] = RGB_corners[cuspCornerCount];
		RGB_corners[cuspCornerCount + 1] = RGB_corners[1];
		JMh_corners[0] = JMh_corners[cuspCornerCount];
		JMh_corners[cuspCornerCount + 1] = JMh_corners[1];
		JMh_corners[0].z -= 360.f;
		JMh_corners[cuspCornerCount + 1].z += 360.f;
	}

	// Find reach gamut corners at limitJMax
	static void find_reach_corners(
		float3 JMh_corners[totalCornerCount],
		const JMhParamsCPU& reach_params,
		float limitJMax,
		float forward_limit)
	{
		float3 temp_JMh[cuspCornerCount];
		float limitA = J_to_Achromatic_n(limitJMax, reach_params.inv_cz);

		int min_index = 0;
		for (int i = 0; i < cuspCornerCount; i++) {
			float3 rgb_vector = generate_unit_cube_cusp_corner(i);
			float lower = 0.0f;
			float upper = forward_limit;

			while ((upper - lower) > reach_cusp_tolerance) {
				float test = (lower + upper) / 2.f;
				float3 test_corner = test * rgb_vector;
				float A = RGB_to_Aab(test_corner, reach_params).x;
				if (A < limitA)
					lower = test;
				else
					upper = test;
			}

			temp_JMh[i] = RGB_to_JMh(upper * rgb_vector, reach_params);
			if (temp_JMh[i].z < temp_JMh[min_index].z)
				min_index = i;
		}

		for (int i = 0; i < cuspCornerCount; i++)
			JMh_corners[i + 1] = temp_JMh[(i + min_index) % cuspCornerCount];

		JMh_corners[0] = JMh_corners[cuspCornerCount];
		JMh_corners[cuspCornerCount + 1] = JMh_corners[1];
		JMh_corners[0].z -= 360.f;
		JMh_corners[cuspCornerCount + 1].z += 360.f;
	}

	// Merge sorted corner hues
	static int extract_sorted_cube_hues(
		float sorted_hues[max_sorted_corners],
		const float3 reach_JMh[totalCornerCount],
		const float3 limit_JMh[totalCornerCount])
	{
		int idx = 0, ri = 1, li = 1;
		while (ri < cuspCornerCount + 1 || li < cuspCornerCount + 1) {
			float rh = (ri < cuspCornerCount + 1) ? reach_JMh[ri].z : 999.f;
			float lh = (li < cuspCornerCount + 1) ? limit_JMh[li].z : 999.f;
			if (std::fabs(rh - lh) < 1e-6f) {
				sorted_hues[idx] = rh;
				ri++;
				li++;
			} else if (rh < lh) {
				sorted_hues[idx] = rh;
				ri++;
			} else {
				sorted_hues[idx] = lh;
				li++;
			}
			idx++;
		}
		return idx;
	}

	// Build non-uniform hue table
	static void build_hue_table(
		float hue_table[TOTAL_TABLE_SIZE],
		const float sorted_hues[max_sorted_corners],
		int num_hues)
	{
		float ideal_spacing = (float)TABLE_SIZE / 360.f;
		int samples_count[max_sorted_corners + 2];
		int last_idx = 0;
		int min_index_val = (sorted_hues[0] == 0.0f) ? 0 : 1;

		for (int hi = 0; hi < num_hues; hi++) {
			int nominal_idx = std::min(std::max((int)std::round(sorted_hues[hi] * ideal_spacing), min_index_val), TABLE_SIZE - 1);
			if (hi > 0 && last_idx == nominal_idx) {
				if (hi > 1 && samples_count[hi - 2] != (samples_count[hi - 1] - 1))
					samples_count[hi - 1]--;
				else
					nominal_idx++;
			}
			samples_count[hi] = std::min(nominal_idx, TABLE_SIZE - 1);
			min_index_val = nominal_idx;
			last_idx = min_index_val;
		}

		// Fill hue table with intervals
		int total_samples = 0;

		// First interval: [0, sorted_hues[0]]
		{
			int count = samples_count[0];
			float lo = 0.f, hi = sorted_hues[0];
			for (int i = 0; i < count; i++)
				hue_table[total_samples + BASE_INDEX + i] = lo + (hi - lo) * i / count;
			total_samples += count;
		}

		// Middle intervals
		for (int si = 1; si < num_hues; si++) {
			int count = samples_count[si] - samples_count[si - 1];
			float lo = sorted_hues[si - 1], hi = sorted_hues[si];
			for (int i = 0; i < count; i++)
				hue_table[total_samples + BASE_INDEX + i] = lo + (hi - lo) * i / count;
			total_samples += count;
		}

		// Last interval: [sorted_hues[last], 360]
		{
			int count = TABLE_SIZE - total_samples;
			float lo = sorted_hues[num_hues - 1], hi = 360.f;
			for (int i = 0; i < count; i++)
				hue_table[total_samples + BASE_INDEX + i] = lo + (hi - lo) * i / count;
		}

		// Wrap entries
		hue_table[0] = hue_table[BASE_INDEX + TABLE_SIZE - 1] - 360.f;
		hue_table[BASE_INDEX + TABLE_SIZE] = hue_table[BASE_INDEX] + 360.f;
	}

	// Find display cusp for a given hue (binary search along RGB corner edges)
	static void find_display_cusp(
		float hue,
		const float3 RGB_corners[totalCornerCount],
		const float3 JMh_corners[totalCornerCount],
		const JMhParamsCPU& params,
		float& out_J, float& out_M)
	{
		// Find which corner interval contains this hue
		int upper_corner = 1;
		for (int i = 1; i < totalCornerCount; i++) {
			if (JMh_corners[i].z > hue) {
				upper_corner = i;
				break;
			}
		}
		int lower_corner = upper_corner - 1;

		if (std::fabs(JMh_corners[lower_corner].z - hue) < 1e-10f) {
			out_J = JMh_corners[lower_corner].x;
			out_M = JMh_corners[lower_corner].y;
			return;
		}

		// Binary search along the line between corners
		float lower_t = 0.f, upper_t = 1.f;
		float3 cusp_lower = RGB_corners[lower_corner];
		float3 cusp_upper = RGB_corners[upper_corner];

		while ((upper_t - lower_t) > display_cusp_tolerance) {
			float sample_t = (lower_t + upper_t) / 2.f;
			float3 sample = lerp3(cusp_lower, cusp_upper, sample_t);
			float3 JMh = RGB_to_JMh(sample, params);

			if (JMh.z < JMh_corners[lower_corner].z)
				upper_t = sample_t;
			else if (JMh.z >= JMh_corners[upper_corner].z)
				lower_t = sample_t;
			else if (JMh.z > hue)
				upper_t = sample_t;
			else
				lower_t = sample_t;
		}

		float sample_t = (lower_t + upper_t) / 2.f;
		float3 sample = lerp3(cusp_lower, cusp_upper, sample_t);
		float3 JMh = RGB_to_JMh(sample, params);

		out_J = JMh.x;
		out_M = JMh.y;
	}

	// Build reach M table (uniform hue, search for max M at limitJMax)
	static void build_reach_m_table(
		float reachTable[TOTAL_TABLE_SIZE],
		const JMhParamsCPU& params,
		float limitJMax)
	{
		for (int i = 0; i < TABLE_SIZE; i++) {
			float hue = base_hue_for_position(i, TABLE_SIZE);

			const float search_range = 50.f;
			const float search_maximum = 1300.f;
			float low = 0.f;
			float high = low + search_range;
			bool found_outside = false;

			while (!found_outside && high < search_maximum) {
				float3 searchJMh = { limitJMax, high, hue };
				float3 rgb = JMh_to_RGB(searchJMh, params);
				found_outside = any_below_zero(rgb);
				if (!found_outside) {
					low = high;
					high = high + search_range;
				}
			}

			while (high - low > 1e-2f) {
				float sampleM = (high + low) / 2.f;
				float3 searchJMh = { limitJMax, sampleM, hue };
				float3 rgb = JMh_to_RGB(searchJMh, params);
				if (any_below_zero(rgb))
					high = sampleM;
				else
					low = sampleM;
			}

			reachTable[i + BASE_INDEX] = high;
		}

		reachTable[0] = reachTable[TABLE_SIZE];
		reachTable[BASE_INDEX + TABLE_SIZE] = reachTable[BASE_INDEX];
	}

	// ========================================================================
	// Gamut compression helpers (used during table building)
	// ========================================================================

	static float compute_focus_J(float cusp_J, float mid_J, float limit_J_max)
	{
		float t = minf(1.f, cusp_mid_blend - (cusp_J / limit_J_max));
		return cusp_J + (mid_J - cusp_J) * t;
	}

	static float get_focus_gain(float J, float analytical_threshold, float limit_J_max, float focus_dist)
	{
		float gain = limit_J_max * focus_dist;
		if (J > analytical_threshold) {
			float adj = std::log10((limit_J_max - analytical_threshold) / maxf(0.0001f, limit_J_max - J));
			adj = adj * adj + 1.f;
			gain *= adj;
		}
		return gain;
	}

	static float solve_J_intersect(float J, float M, float focusJ, float maxJ, float slope_gain)
	{
		float M_scaled = M / slope_gain;
		float a = M_scaled / focusJ;
		if (J < focusJ) {
			float b = 1.f - M_scaled;
			float c = -J;
			float det = b * b - 4.f * a * c;
			return -2.f * c / (b + std::sqrt(det));
		} else {
			float b = -(1.f + M_scaled + maxJ * a);
			float c = maxJ * M_scaled + J;
			float det = b * b - 4.f * a * c;
			return -2.f * c / (b - std::sqrt(det));
		}
	}

	static float compute_compression_vector_slope(float intersect_J, float focus_J, float limit_J_max, float slope_gain)
	{
		float direction_scalar = (intersect_J < focus_J) ? intersect_J : (limit_J_max - intersect_J);
		return direction_scalar * (intersect_J - focus_J) / (focus_J * slope_gain);
	}

	static float smin_scaled(float a, float b, float scale_reference)
	{
		float s = smooth_cusps * scale_reference;
		float h = maxf(s - std::fabs(a - b), 0.f) / s;
		return minf(a, b) - h * h * h * s / 6.f;
	}

	static float estimate_line_boundary_M(float J_axis_intersect, float slope, float inv_gamma, float J_max, float M_max, float J_ref)
	{
		float nJ = J_axis_intersect / J_ref;
		float shifted = J_ref * std::pow(nJ, inv_gamma);
		return shifted * M_max / (J_max - slope * M_max);
	}

	static float find_gamut_boundary_intersection(
		float cusp_J, float cusp_M,
		float J_max,
		float gamma_top_inv, float gamma_bottom_inv,
		float J_intersect_source, float slope,
		float J_intersect_cusp)
	{
		float M_lower = estimate_line_boundary_M(J_intersect_source, slope, gamma_bottom_inv, cusp_J, cusp_M, J_intersect_cusp);
		float f_J_cusp = J_max - J_intersect_cusp;
		float f_J_src = J_max - J_intersect_source;
		float f_cusp_J = J_max - cusp_J;
		float M_upper = estimate_line_boundary_M(f_J_src, -slope, gamma_top_inv, f_cusp_J, cusp_M, f_J_cusp);
		return smin_scaled(M_lower, M_upper, cusp_M);
	}

	// Build upper hull gamma table
	static void build_upper_hull_gamma_table(
		float upper_hull_gamma[TOTAL_TABLE_SIZE],
		const float cusp_table_J[TOTAL_TABLE_SIZE],
		const float cusp_table_M[TOTAL_TABLE_SIZE],
		const float cusp_table_h[TOTAL_TABLE_SIZE],
		float limit_J_max,
		float mid_J,
		float focus_dist,
		float lower_hull_gamma_inv,
		float peakLuminance,
		const JMhParamsCPU& limit_params)
	{
		float luminance_limit = peakLuminance / ref_luminance;

		for (int i = BASE_INDEX; i < BASE_INDEX + TABLE_SIZE; i++) {
			float hue = cusp_table_h[i];
			float cusp_J = cusp_table_J[i];
			float cusp_M = cusp_table_M[i];

			float analytical_threshold = cusp_J + (limit_J_max - cusp_J) * focus_gain_blend;
			float focusJ = compute_focus_J(cusp_J, mid_J, limit_J_max);

			// Generate test data
			float test_J[test_count], J_intersect[test_count], slopes[test_count], J_cusp_intersect[test_count];
			for (int t = 0; t < test_count; t++) {
				test_J[t] = cusp_J + (limit_J_max - cusp_J) * testPositions[t];
				float sg = get_focus_gain(test_J[t], analytical_threshold, limit_J_max, focus_dist);
				J_intersect[t] = solve_J_intersect(test_J[t], cusp_M, focusJ, limit_J_max, sg);
				slopes[t] = compute_compression_vector_slope(J_intersect[t], focusJ, limit_J_max, sg);
				float sg2 = get_focus_gain(cusp_J, analytical_threshold, limit_J_max, focus_dist);
				J_cusp_intersect[t] = solve_J_intersect(cusp_J, cusp_M, focusJ, limit_J_max, sg2);
			}

			// Evaluate gamma fit: search for the gamma where approximate boundary just exits the actual hull
			auto evaluate_fit = [&](float top_gamma_inv) -> bool {
				for (int t = 0; t < test_count; t++) {
					float approxM = find_gamut_boundary_intersection(cusp_J, cusp_M, limit_J_max, top_gamma_inv, lower_hull_gamma_inv, J_intersect[t], slopes[t], J_cusp_intersect[t]);
					float approxJ = J_intersect[t] + slopes[t] * approxM;
					float3 approx_JMh = { approxJ, approxM, hue };
					float3 rgb = JMh_to_RGB(approx_JMh, limit_params);
					if (!outside_hull(rgb, luminance_limit))
						return false;
				}
				return true;
			};

			float low = gamma_minimum;
			float high = low + gamma_search_step;
			bool found = false;
			while (!found && high < gamma_maximum) {
				if (!evaluate_fit(1.f / high)) {
					low = high;
					high += gamma_search_step;
				} else {
					found = true;
				}
			}

			while (high - low > gamma_accuracy) {
				float mid = (high + low) / 2.f;
				if (evaluate_fit(1.f / mid))
					high = mid;
				else
					low = mid;
			}

			upper_hull_gamma[i] = 1.f / high;
		}

		upper_hull_gamma[0] = upper_hull_gamma[TABLE_SIZE];
		upper_hull_gamma[BASE_INDEX + TABLE_SIZE] = upper_hull_gamma[BASE_INDEX];
	}

	// Determine hue linearity search range
	static void determine_search_range(const float hue_table[TOTAL_TABLE_SIZE], int& lo, int& hi)
	{
		lo = 0;
		hi = 0;
		const int lower_padding = -2;
		const int upper_padding = 3;
		for (int i = BASE_INDEX; i < BASE_INDEX + TABLE_SIZE; i++) {
			int pos = hue_position_in_uniform_table(hue_table[i], TOTAL_TABLE_SIZE);
			int delta = i - pos;
			lo = std::min(lo, delta + lower_padding);
			hi = std::max(hi, delta + upper_padding);
		}
	}

	// ========================================================================
	// Store matrix into float[12] (3x4 row-major, w=0 padding)
	// ========================================================================

	static void storeMat3(float* dst, const Mat3& m)
	{
		dst[0] = m[0][0];
		dst[1] = m[0][1];
		dst[2] = m[0][2];
		dst[3] = 0.f;
		dst[4] = m[1][0];
		dst[5] = m[1][1];
		dst[6] = m[1][2];
		dst[7] = 0.f;
		dst[8] = m[2][0];
		dst[9] = m[2][1];
		dst[10] = m[2][2];
		dst[11] = 0.f;
	}

	static void storeJMhParams(JMhParamsGPU& gpu, const JMhParamsCPU& cpu)
	{
		storeMat3(gpu.mtxRGBtoCAM16c, cpu.MATRIX_RGB_to_CAM16_c);
		storeMat3(gpu.mtxCAM16cToRGB, cpu.MATRIX_CAM16_c_to_RGB);
		storeMat3(gpu.mtxConeRespToAab, cpu.MATRIX_cone_response_to_Aab);
		storeMat3(gpu.mtxAabToConeResp, cpu.MATRIX_Aab_to_cone_response);
		gpu.F_L_n = cpu.F_L_n;
		gpu.cz = cpu.cz;
		gpu.inv_cz = cpu.inv_cz;
		gpu.A_w_J = cpu.A_w_J;
		gpu.inv_A_w_J = cpu.inv_A_w_J;
		gpu.pad0 = gpu.pad1 = gpu.pad2 = 0.f;
	}

	// ========================================================================
	// Public API
	// ========================================================================

	ACES2CB ComputeParams(float peakLuminance)
	{
		ACES2CB cb{};
		std::memset(&cb, 0, sizeof(cb));

		// ---- Initialize JMh params for each set of primaries ----
		JMhParamsCPU input_params = init_JMhParams(AP0_CHROM);  // ACES 2065-1
		JMhParamsCPU reach_params = init_JMhParams(AP1_CHROM);  // ACEScg / AP1
		JMhParamsCPU limit_params = init_JMhParams(SRGB_CHROM);

		storeJMhParams(cb.inputParams, input_params);
		storeJMhParams(cb.limitParams, limit_params);

		// ---- Tonescale ----
		TSParamsCPU ts = init_TSParams(peakLuminance);
		cb.ts.n = ts.n;
		cb.ts.n_r = ts.n_r;
		cb.ts.g = ts.g;
		cb.ts.t_1 = ts.t_1;
		cb.ts.c_t = ts.c_t;
		cb.ts.s_2 = ts.s_2;
		cb.ts.u_2 = ts.u_2;
		cb.ts.m_2 = ts.m_2;
		cb.ts.forward_limit = ts.forward_limit;
		cb.ts.inverse_limit = ts.inverse_limit;
		cb.ts.log_peak = ts.log_peak;

		// ---- Shared compression parameters ----
		cb.peakLuminance = peakLuminance;
		cb.limitJMax = Y_to_J(peakLuminance, input_params);
		cb.modelGamma = model_gamma_value;
		cb.modelGammaInv = 1.f / model_gamma_value;

		// ---- Chroma compression parameters ----
		cb.chromaSat = maxf(0.2f, chroma_expand - (chroma_expand * chroma_expand_fact) * ts.log_peak);
		cb.chromaSatThr = chroma_expand_thr / peakLuminance;
		cb.chromaCompr = chroma_compress + (chroma_compress * chroma_compress_fact) * ts.log_peak;
		cb.chromaCompressScale = std::pow(0.03379f * peakLuminance, 0.30596f) - 0.45135f;

		// ---- Gamut compression parameters ----
		cb.midJ = Y_to_J(ts.c_t * ref_luminance, input_params);
		cb.focusDist = focus_distance + focus_distance * focus_distance_scaling * ts.log_peak;
		float lower_hull_gamma = 1.14f + 0.07f * ts.log_peak;
		cb.lowerHullGamma = lower_hull_gamma;
		cb.lowerHullGammaInv = 1.f / lower_hull_gamma;

		// ---- Build reach M table (uniform hue, from reach_params) ----
		build_reach_m_table(cb.tableReachM, reach_params, cb.limitJMax);

		// ---- Build non-uniform hue gamut table ----
		// Step 1: Find cusp corners for limiting gamut and reach gamut
		float3 limit_RGB_corners[totalCornerCount];
		float3 limit_JMh_corners[totalCornerCount];
		float3 reach_JMh_corners[totalCornerCount];

		build_limiting_cusp_corners(limit_RGB_corners, limit_JMh_corners, limit_params, peakLuminance);
		find_reach_corners(reach_JMh_corners, reach_params, cb.limitJMax, ts.forward_limit);

		// Step 2: Merge and sort corner hues
		float sorted_hues[max_sorted_corners];
		int num_hues = extract_sorted_cube_hues(sorted_hues, reach_JMh_corners, limit_JMh_corners);

		// Step 3: Build non-uniform hue table
		build_hue_table(cb.tableHues, sorted_hues, num_hues);

		// Step 4: Build cusp table by binary searching along corner edges at each hue
		for (int i = BASE_INDEX; i < BASE_INDEX + TABLE_SIZE; i++) {
			float hue = cb.tableHues[i];
			float cusp_J, cusp_M;
			find_display_cusp(hue, limit_RGB_corners, limit_JMh_corners, limit_params, cusp_J, cusp_M);
			cb.tableCuspsJ[i] = cusp_J;
			cb.tableCuspsM[i] = cusp_M * (1.f + smooth_m * smooth_cusps);  // Official: slight M expansion for smoothness
		}
		// Wrap cusp entries
		cb.tableCuspsJ[0] = cb.tableCuspsJ[TABLE_SIZE];
		cb.tableCuspsM[0] = cb.tableCuspsM[TABLE_SIZE];
		cb.tableCuspsJ[BASE_INDEX + TABLE_SIZE] = cb.tableCuspsJ[BASE_INDEX];
		cb.tableCuspsM[BASE_INDEX + TABLE_SIZE] = cb.tableCuspsM[BASE_INDEX];

		// Step 5: Build upper hull gamma table
		build_upper_hull_gamma_table(
			cb.tableUpperHullGamma,
			cb.tableCuspsJ, cb.tableCuspsM, cb.tableHues,
			cb.limitJMax, cb.midJ, cb.focusDist,
			cb.lowerHullGammaInv, peakLuminance, limit_params);

		// Step 6: Hue linearity search range
		determine_search_range(cb.tableHues, cb.hueSearchLo, cb.hueSearchHi);

		// ---- Limiting gamut -> display encoding gamut matrix ----
		// pp is always SDR: identity matrix (sRGB -> sRGB)
		{
			Mat3 identity = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
			storeMat3(cb.limitToDisplayMtx, identity);
		}

		// ---- AP0 <-> AP1 matrices ----
		Mat3 AP0_to_XYZ = RGBtoXYZ(AP0_CHROM, 1.0f);
		Mat3 AP1_to_XYZ = RGBtoXYZ(AP1_CHROM, 1.0f);
		Mat3 XYZ_to_AP0 = XYZtoRGB(AP0_CHROM, 1.0f);
		Mat3 XYZ_to_AP1 = XYZtoRGB(AP1_CHROM, 1.0f);
		Mat3 AP0_to_AP1 = mul_mm(AP0_to_XYZ, XYZ_to_AP1);
		Mat3 AP1_to_AP0 = mul_mm(AP1_to_XYZ, XYZ_to_AP0);
		storeMat3(cb.AP0toAP1, AP0_to_AP1);
		storeMat3(cb.AP1toAP0, AP1_to_AP0);

		return cb;
	}

}  // namespace ACES2

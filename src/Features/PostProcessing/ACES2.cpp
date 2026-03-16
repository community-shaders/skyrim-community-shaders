#include "ACES2.h"

// ACES 2.0 Output Transform — CPU-side parameter and table computation
// Ported from the official aces-aswf/aces-core CTL reference implementation (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)
//
// Key source files ported:
//   Lib.Academy.OutputTransform.ctl
//   Lib.Academy.Tonescale.ctl

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ACES2
{
	// ================================================
	// Math helpers
	// ================================================

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

	static float3 mul(const Mat3& m, float3 v)
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
		};
	}

	static Mat3 mul(const Mat3& a, const Mat3& b)
	{
		Mat3 r{};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++)
					r[i][j] += a[i][k] * b[k][j];
		return r;
	}

	static Mat3 transpose(const Mat3& m)
	{
		Mat3 r;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				r[i][j] = m[j][i];
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

	static float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	static float maxf(float a, float b) { return a > b ? a : b; }
	static float minf(float a, float b) { return a < b ? a : b; }
	static float clampf(float x, float lo, float hi) { return maxf(lo, minf(hi, x)); }

	// ================================================
	// Color space definitions
	// ================================================

	// CIE XYZ of D65 white point
	static constexpr float3 D65_WHITE = { 0.9504559270516716f, 1.0f, 1.0890577507598784f };

	// AP0 (ACES2065-1) primaries
	static const Mat3 AP0_to_XYZ = { { { 0.9525523959f, 0.0000000000f, 0.0000936786f },
		{ 0.3439664498f, 0.7281660966f, -0.0721325464f },
		{ 0.0000000000f, 0.0000000000f, 1.0088251844f } } };
	static const Mat3 XYZ_to_AP0 = { { { 1.0498110175f, 0.0000000000f, -0.0000974845f },
		{ -0.4959030231f, 1.3733130458f, 0.0982400361f },
		{ 0.0000000000f, 0.0000000000f, 0.9912520182f } } };

	// AP1 (ACEScg) primaries
	static const Mat3 AP1_to_XYZ = { { { 0.6624541811f, 0.1340042065f, 0.1561876870f },
		{ 0.2722287168f, 0.6740817658f, 0.0536895174f },
		{ -0.0055746495f, 0.0040607335f, 1.0103391003f } } };
	static const Mat3 XYZ_to_AP1 = { { { 1.6410233797f, -0.3248032942f, -0.2364246952f },
		{ -0.6636628587f, 1.6153315917f, 0.0167563477f },
		{ 0.0117218943f, -0.0082844420f, 0.9883948585f } } };

	// sRGB / Rec.709 primaries
	static const Mat3 sRGB_to_XYZ = { { { 0.4123907993f, 0.3575843394f, 0.1804807884f },
		{ 0.2126390059f, 0.7151686788f, 0.0721923154f },
		{ 0.0193308187f, 0.1191947798f, 0.9505321522f } } };
	static const Mat3 XYZ_to_sRGB = { { { 3.2409699419f, -1.5373831776f, -0.4986107603f },
		{ -0.9692436363f, 1.8759675015f, 0.0415550574f },
		{ 0.0556300797f, -0.2039769589f, 1.0569715142f } } };

	// CAM16 matrices
	static const Mat3 XYZ_to_CAM16 = { { { 0.3640744835f, 0.5947008156f, 0.04110127349f },
		{ -0.2222450987f, 1.0738554823f, 0.14794533610f },
		{ -0.0020676190f, 0.0488260453f, 0.95038755696f } } };
	static const Mat3 CAM16_to_XYZ = { { { 2.0512756811f, -1.1400313439f, 0.0887556628f },
		{ 0.4269389763f, 0.7005835277f, -0.1275225040f },
		{ -0.0174712779f, -0.0384725929f, 1.0589468739f } } };

	// ================================================
	// Tone scale (Michaelis-Menten based curve)
	// Reference: Lib.Academy.Tonescale.ctl
	// ================================================

	static TSParams init_TSParams(float peakLuminance)
	{
		TSParams p{};
		p.n_r = 100.0f;
		p.n = peakLuminance;
		p.g = 1.15f;
		p.t_1 = 0.04f;

		// Compute crosstalk and derived parameters
		float n = p.n;
		float n_r = p.n_r;
		float g = p.g;

		// SDR case: peakLuminance <= n_r
		p.c_t = std::max(0.0f, (0.5f * n / n_r) * (1.0f + n / n_r) - 1.0f);

		// The point at which the tonescale shoulder starts
		float u = std::pow(n / n_r, g);
		p.s_2 = std::max(0.001f, u * (1.0f + u) / (u + p.c_t));
		p.u_2 = u;
		p.m_2 = u / (u + p.s_2);  // normalized maximum

		// Limits for clamping
		p.forward_limit = n;
		p.inverse_limit = n;

		return p;
	}

	static float tonescale_fwd(float x, const TSParams& p)
	{
		float n_r = p.n_r;
		float g = p.g;
		float t_1 = p.t_1;
		float s_2 = p.s_2;
		float m_2 = p.m_2;

		// Normalize input
		float v = x / n_r;

		// Tonescale: modified Michaelis-Menten
		float f = m_2 * std::pow(maxf(0.0f, v) / (v + s_2), g);
		float h = maxf(0.0f, f * f / (f + t_1));

		return h * n_r;
	}

	// ================================================
	// CAM16 Color Appearance Model
	// ================================================

	// Post-adaptation non-linear compression (cone response)
	static float post_adaptation_compress_fwd(float x)
	{
		float abs_x = std::fabs(x);
		float p = std::pow(abs_x, 0.42f);
		float r = 400.0f * p / (p + 27.13f) + 0.1f;
		return std::copysign(r, x);
	}

	static float post_adaptation_compress_inv(float x)
	{
		float v = x - 0.1f;
		float abs_v = std::fabs(v);
		float p = std::pow(27.13f * abs_v / (400.0f - abs_v), 1.0f / 0.42f);
		return std::copysign(p, v);
	}

	// Convert RGB to JMh via CAM16 model
	// 'RGB' should be in the source primaries (AP0 for ACES)
	static float3 RGB_to_JMh(float3 RGB, const Mat3& RGB_to_XYZ_mtx, float3 refWhiteRGB)
	{
		float3 XYZ = mul(RGB_to_XYZ_mtx, RGB);
		float3 cam = mul(XYZ_to_CAM16, XYZ);

		// Reference white adaptation
		float3 refWhiteXYZ = mul(RGB_to_XYZ_mtx, refWhiteRGB);
		float3 refWhiteCam = mul(XYZ_to_CAM16, refWhiteXYZ);

		// Compute adapted cone responses
		float r_a = post_adaptation_compress_fwd(cam.x / refWhiteCam.x);
		float g_a = post_adaptation_compress_fwd(cam.y / refWhiteCam.y);
		float b_a = post_adaptation_compress_fwd(cam.z / refWhiteCam.z);

		// Opponent channels
		float a = r_a - 12.0f / 11.0f * g_a + b_a / 11.0f;
		float b = (r_a + g_a - 2.0f * b_a) / 9.0f;

		// Achromatic response
		float A = (2.0f * r_a + g_a + 0.05f * b_a);

		// Lightness
		float refWhite_r_a = post_adaptation_compress_fwd(1.0f);
		float refWhite_g_a = post_adaptation_compress_fwd(1.0f);
		float refWhite_b_a = post_adaptation_compress_fwd(1.0f);
		float A_w = (2.0f * refWhite_r_a + refWhite_g_a + 0.05f * refWhite_b_a);
		float J = 100.0f * std::pow(A / A_w, 0.69f * 2.0f);

		// Colorfulness
		float M = 43.0f * std::sqrt(a * a + b * b);

		// Hue angle
		float h = std::atan2(b, a) * 180.0f / PI;
		if (h < 0.0f)
			h += 360.0f;

		return { J, M, h };
	}

	// Convert JMh back to RGB
	static float3 JMh_to_RGB(float3 JMh, const Mat3& XYZ_to_RGB_mtx, float3 refWhiteRGB, const Mat3& RGB_to_XYZ_mtx)
	{
		float J = JMh.x;
		float M = JMh.y;
		float h = JMh.z;

		// Reference white
		float3 refWhiteXYZ = mul(RGB_to_XYZ_mtx, refWhiteRGB);
		float3 refWhiteCam = mul(XYZ_to_CAM16, refWhiteXYZ);

		float refWhite_r_a = post_adaptation_compress_fwd(1.0f);
		float refWhite_g_a = post_adaptation_compress_fwd(1.0f);
		float refWhite_b_a = post_adaptation_compress_fwd(1.0f);
		float A_w = (2.0f * refWhite_r_a + refWhite_g_a + 0.05f * refWhite_b_a);

		// Inverse lightness
		float A = A_w * std::pow(J / 100.0f, 1.0f / (0.69f * 2.0f));

		// Inverse colorfulness
		float t = M / 43.0f;

		float hr = h * PI / 180.0f;
		float cos_h = std::cos(hr);
		float sin_h = std::sin(hr);

		float a = t * cos_h;
		float b = t * sin_h;

		// Inverse opponent channels to adapted cone responses
		float r_a = (460.0f * A + 451.0f * a + 288.0f * b) / 1403.0f;
		float g_a = (460.0f * A - 891.0f * a - 261.0f * b) / 1403.0f;
		float b_a = (460.0f * A - 220.0f * a - 6300.0f * b) / 1403.0f;

		// Undo post-adaptation compression
		float r = post_adaptation_compress_inv(r_a) * refWhiteCam.x;
		float g = post_adaptation_compress_inv(g_a) * refWhiteCam.y;
		float b_val = post_adaptation_compress_inv(b_a) * refWhiteCam.z;

		float3 cam = { r, g, b_val };
		float3 XYZ = mul(CAM16_to_XYZ, cam);
		return mul(XYZ_to_RGB_mtx, XYZ);
	}

	// Luminance-only J ↔ Y conversion
	static float Y_to_J(float Y, float refWhiteY)
	{
		float3 v = { Y, Y, Y };
		float3 refW = { refWhiteY, refWhiteY, refWhiteY };
		float r_a = post_adaptation_compress_fwd(v.x / refW.x);
		float A = (2.0f * r_a + r_a + 0.05f * r_a);
		float refWhite_r_a = post_adaptation_compress_fwd(1.0f);
		float A_w = (2.0f * refWhite_r_a + refWhite_r_a + 0.05f * refWhite_r_a);
		return 100.0f * std::pow(maxf(0.0f, A / A_w), 0.69f * 2.0f);
	}

	static float J_to_Y(float J, float refWhiteY)
	{
		float refWhite_r_a = post_adaptation_compress_fwd(1.0f);
		float A_w = (2.0f * refWhite_r_a + refWhite_r_a + 0.05f * refWhite_r_a);
		float A = A_w * std::pow(maxf(0.0f, J / 100.0f), 1.0f / (0.69f * 2.0f));
		float r_a = A / (2.0f + 1.0f + 0.05f);
		return post_adaptation_compress_inv(r_a) * refWhiteY;
	}

	// ================================================
	// Gamut boundary / cusp finding
	// ================================================

	// Find the cusp (maximum M) for a given hue by sweeping
	static float3 find_cusp(float h, const Mat3& XYZ_to_RGB_mtx, const Mat3& RGB_to_XYZ_mtx)
	{
		float3 refWhite = mul(XYZ_to_RGB_mtx, D65_WHITE);

		// At a given hue, generate a JMh color and check gamut boundary
		float bestJ = 0, bestM = 0;

		// Binary search for maximum M at this hue
		// First, find approximate J where M is maximized
		for (int pass = 0; pass < 2; pass++) {
			float J_lo = 0.0f, J_hi = 100.0f;

			if (pass == 1) {
				// Narrow the search
				J_lo = maxf(0.0f, bestJ - 10.0f);
				J_hi = minf(100.0f, bestJ + 10.0f);
			}

			int steps = (pass == 0) ? 64 : 128;
			float best_M_found = 0.0f;
			float best_J_found = 0.0f;

			for (int i = 0; i <= steps; i++) {
				float J = J_lo + (J_hi - J_lo) * i / steps;
				// For this J and h, find maximum in-gamut M using binary search
				float M_lo = 0.0f, M_hi = 120.0f;
				for (int j = 0; j < 40; j++) {
					float M_mid = (M_lo + M_hi) * 0.5f;
					float3 jmh = { J, M_mid, h };
					float3 rgb = JMh_to_RGB(jmh, XYZ_to_RGB_mtx, refWhite, RGB_to_XYZ_mtx);
					// Check if in gamut
					if (rgb.x >= -1e-4f && rgb.y >= -1e-4f && rgb.z >= -1e-4f &&
						rgb.x <= 1.0f + 1e-4f && rgb.y <= 1.0f + 1e-4f && rgb.z <= 1.0f + 1e-4f) {
						M_lo = M_mid;
					} else {
						M_hi = M_mid;
					}
				}
				if (M_lo > best_M_found) {
					best_M_found = M_lo;
					best_J_found = J;
				}
			}
			bestJ = best_J_found;
			bestM = best_M_found;
		}

		return { bestJ, bestM, h };
	}

	// Find the reach M (maximum M in AP1 gamut) at a given hue
	static float find_reach_M(float h, const Mat3& reachXYZ_to_RGB, const Mat3& reachRGB_to_XYZ)
	{
		float3 refWhite = mul(reachXYZ_to_RGB, D65_WHITE);

		float M_lo = 0.0f, M_hi = 200.0f;
		// Search across J values for maximum M
		float maxM = 0.0f;
		for (int i = 0; i <= 128; i++) {
			float J = 100.0f * i / 128.0f;
			float mLo = 0.0f, mHi = 200.0f;
			for (int j = 0; j < 40; j++) {
				float mMid = (mLo + mHi) * 0.5f;
				float3 jmh = { J, mMid, h };
				float3 rgb = JMh_to_RGB(jmh, reachXYZ_to_RGB, refWhite, reachRGB_to_XYZ);
				if (rgb.x >= -1e-4f && rgb.y >= -1e-4f && rgb.z >= -1e-4f) {
					mLo = mMid;
				} else {
					mHi = mMid;
				}
			}
			maxM = maxf(maxM, mLo);
		}
		return maxM;
	}

	// Compute the upper hull gamma for a given hue
	static float compute_hull_gamma(float h, const Mat3& XYZ_to_RGB_mtx, const Mat3& RGB_to_XYZ_mtx, float3 cusp, bool upper)
	{
		float3 refWhite = mul(XYZ_to_RGB_mtx, D65_WHITE);

		float cuspJ = cusp.x;
		float cuspM = cusp.y;

		if (cuspM < 1e-6f)
			return 1.0f;

		// Find gamma that maps the cusp through [0..1] range nicely
		// Use several sample points to fit
		float bestGamma = 1.0f;
		float bestError = 1e10f;

		for (int gi = 1; gi <= 200; gi++) {
			float gamma = gi * 0.05f;  // 0.05 to 10.0
			float error = 0.0f;
			int count = 0;
			for (int si = 1; si < 16; si++) {
				float t = si / 16.0f;
				float J_test, M_expected;
				if (upper) {
					J_test = cuspJ + (100.0f - cuspJ) * t;
					float focusDist = 100.0f - cuspJ;
					if (focusDist < 1e-6f)
						continue;
					float norm = (J_test - cuspJ) / focusDist;
					M_expected = cuspM * std::pow(maxf(0.0f, 1.0f - norm), gamma);
				} else {
					J_test = cuspJ * (1.0f - t);
					float focusDist = cuspJ;
					if (focusDist < 1e-6f)
						continue;
					float norm = (cuspJ - J_test) / focusDist;
					M_expected = cuspM * std::pow(maxf(0.0f, 1.0f - norm), gamma);
				}

				// Find actual gamut boundary M at this J
				float mLo = 0.0f, mHi = cuspM * 1.1f;
				for (int j = 0; j < 32; j++) {
					float mMid = (mLo + mHi) * 0.5f;
					float3 jmh = { J_test, mMid, h };
					float3 rgb = JMh_to_RGB(jmh, XYZ_to_RGB_mtx, refWhite, RGB_to_XYZ_mtx);
					if (rgb.x >= -1e-4f && rgb.y >= -1e-4f && rgb.z >= -1e-4f &&
						rgb.x <= 1.0f + 1e-4f && rgb.y <= 1.0f + 1e-4f && rgb.z <= 1.0f + 1e-4f) {
						mLo = mMid;
					} else {
						mHi = mMid;
					}
				}
				float actual_M = mLo;
				float diff = actual_M - M_expected;
				error += diff * diff;
				count++;
			}
			if (count > 0)
				error /= count;
			if (error < bestError) {
				bestError = error;
				bestGamma = gamma;
			}
		}
		return bestGamma;
	}

	// ================================================
	// Build all lookup tables
	// ================================================

	static void buildTables(ACES2CB& cb, float peakLuminance)
	{
		// Limiting gamut = sRGB (for SDR output)
		const Mat3& limitRGB_to_XYZ = sRGB_to_XYZ;
		const Mat3& limitXYZ_to_RGB = XYZ_to_sRGB;

		// Reach gamut = AP1 (wide enough to reach all saturated colors)
		const Mat3& reachRGB_to_XYZ = AP1_to_XYZ;
		const Mat3& reachXYZ_to_RGB = XYZ_to_AP1;

		for (int i = 0; i < TABLE_SIZE; i++) {
			float hue = (float)i;  // 0..359 degrees

			// Gamut cusp for limiting primaries
			float3 cusp = find_cusp(hue, limitXYZ_to_RGB, limitRGB_to_XYZ);
			cb.gamutCuspTableJ[i] = cusp.x;
			cb.gamutCuspTableM[i] = cusp.y;
			cb.gamutCuspTableh[i] = hue;

			// Reach M for AP1
			cb.reachMTable[i] = find_reach_M(hue, reachXYZ_to_RGB, reachRGB_to_XYZ);

			// Hull gamma (upper and lower)
			cb.upperHullGamma[i] = compute_hull_gamma(hue, limitXYZ_to_RGB, limitRGB_to_XYZ, cusp, true);
			cb.lowerHullGamma[i] = compute_hull_gamma(hue, limitXYZ_to_RGB, limitRGB_to_XYZ, cusp, false);
		}
	}

	// ================================================
	// Store matrix into float[12] (3×4 row-major, .w = 0)
	// ================================================

	static void storeMat3(float* dst, const Mat3& m)
	{
		// Row 0
		dst[0] = m[0][0];
		dst[1] = m[0][1];
		dst[2] = m[0][2];
		dst[3] = 0.0f;
		// Row 1
		dst[4] = m[1][0];
		dst[5] = m[1][1];
		dst[6] = m[1][2];
		dst[7] = 0.0f;
		// Row 2
		dst[8] = m[2][0];
		dst[9] = m[2][1];
		dst[10] = m[2][2];
		dst[11] = 0.0f;
	}

	// ================================================
	// Public API
	// ================================================

	ACES2CB ComputeParams(float peakLuminance)
	{
		ACES2CB cb{};
		std::memset(&cb, 0, sizeof(cb));

		// Tone scale params
		TSParams ts = init_TSParams(peakLuminance);
		cb.ts_n = ts.n;
		cb.ts_n_r = ts.n_r;
		cb.ts_g = ts.g;
		cb.ts_t_1 = ts.t_1;
		cb.ts_c_t = ts.c_t;
		cb.ts_s_2 = ts.s_2;
		cb.ts_u_2 = ts.u_2;
		cb.ts_m_2 = ts.m_2;
		cb.ts_forward_limit = ts.forward_limit;
		cb.ts_inverse_limit = ts.inverse_limit;
		cb.peakLuminance = peakLuminance;
		cb.chromaCompressScale = 3.5f;

		// Matrices
		// Input: AP0 → XYZ (for JMh conversion)
		storeMat3(cb.inputMtx, AP0_to_XYZ);

		// Limit: sRGB → XYZ and XYZ → sRGB
		storeMat3(cb.limitMtx, sRGB_to_XYZ);
		storeMat3(cb.boundaryRGB_to_XYZ, sRGB_to_XYZ);
		storeMat3(cb.boundaryXYZ_to_RGB, XYZ_to_sRGB);

		// Reach: AP1 → XYZ
		storeMat3(cb.reachMtx, AP1_to_XYZ);

		// Build all lookup tables
		buildTables(cb, peakLuminance);

		return cb;
	}

}  // namespace ACES2

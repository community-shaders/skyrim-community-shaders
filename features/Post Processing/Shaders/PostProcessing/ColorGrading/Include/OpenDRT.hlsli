// OpenDRT v1.1.0 HLSL compute shader conversion.
// Original DCTL written by Jed Smith: https://github.com/jedypod/open-display-transform
// License: GPLv3
#include "Common/Math.hlsli"

/******************************************************************
  Color Conversion Matrices
 ******************************************************************/
// Linear ACEScg (AP1) to XYZ D65 (CAT02).
#define matrix_ap1_to_xyz float3x3(float3(0.652418717671912951f, 0.127179925537538263f, 0.170857283842220459f), float3(0.268064059194271287f, 0.672464478992617742f, 0.0594714618131108388f), float3(-0.0054699285104975676f, 0.00518279997697511721f, 1.08934487929340107f))
// Linear Rec.709/sRGB to XYZ D65.
#define matrix_rec709_to_xyz float3x3(float3(0.412390799265959229f, 0.357584339383878125f, 0.180480788401834347f), float3(0.212639005871510217f, 0.71516867876775625f, 0.0721923153607337414f), float3(0.0193308187155918181f, 0.119194779794626018f, 0.950532152249661033f))
// XYZ D65 to linear Rec.709/sRGB.
#define matrix_xyz_to_rec709 float3x3(float3(3.24096994190452348f, -1.53738317757009435f, -0.498610760293003552f), float3(-0.969243636280879506f, 1.87596750150771996f, 0.0415550574071755843f), float3(0.0556300796969936354f, -0.20397695888897649f, 1.05697151424287816f))
// P3D65 to XYZ D65.
#define matrix_p3d65_to_xyz float3x3(float3(0.486570948648216151f, 0.265667693169093f, 0.198217285234362467f), float3(0.228974564069748754f, 0.691738521836506193f, 0.079286914093744984f), float3(-4.00000000000000029e-17f, 0.0451133818589026167f, 1.04394436890097575f))
// XYZ D65 to P3D65.
#define matrix_xyz_to_p3d65 float3x3(float3(2.49349691194142542f, -0.93138361791912383f, -0.402710784450716841f), float3(-0.829488969561574696f, 1.76266406031834655f, 0.0236246858419435941f), float3(0.0358458302437844531f, -0.0761723892680418041f, 0.956884524007687309f))
// P3D65 to Rec2020
#define matrix_p3_to_rec2020 float3x3(float3(0.753833034361722221f, 0.198597369052616435f, 0.0475695965856618441f), float3(0.0457438489653582137f, 0.9417772198116936f, 0.0124789312229481135f), float3(-0.0012103403545183941f, 0.0176017173010899926f, 0.983608623053428777f))

/******************************************************************
  CAT02 Chromatic Adaptation Matrices
 ******************************************************************/
// D65 to D93 : [0.3127, 0.329] to [0.283, 0.297]
#define matrix_cat_d65_to_d93 float3x3(float3(0.95703423023223877f, -0.0247171502560377121f, 0.0624028593301773071f), float3(-0.0179296955466270447f, 0.990019857883453369f, 0.0248119533061981201f), float3(0.00127589143812656403f, 0.00427919067442417058f, 1.29345715045928955f))
// D65 to D75 : [0.3127, 0.329] to [0.29903, 0.31488]
#define matrix_cat_d65_to_d75 float3x3(float3(0.981001079082489014f, -0.0116619253531098366f, 0.0265614092350006104f), float3(-0.00843488052487373352f, 0.996506094932556152f, 0.0105696544051170349f), float3(0.000552809564396739006f, 0.00179840810596942902f, 1.12374722957611084f))
// D65 to D60 : [0.3127, 0.329] to [0.32162624, 0.337737]
#define matrix_cat_d65_to_d60 float3x3(float3(1.01182246208190918f, 0.00778879318386316299f, -0.0157783031463623047f), float3(0.00561682833358645439f, 1.00150644779205322f, -0.00628517568111419678f), float3(-0.000335735734552145004f, -0.0010509500280022619f, 0.927366673946380615f))
// D65 to D55 : [0.3127, 0.329] to [0.33243, 0.34744]
#define matrix_cat_d65_to_d55 float3x3(float3(1.02585089206695557f, 0.0179439820349216461f, -0.0332137793302536011f), float3(0.0129133854061365128f, 1.00214779376983643f, -0.0132421031594276428f), float3(-0.000719940289855003032f, -0.00218106806278228803f, 0.84868013858795166f))
// D65 to D50 : [0.3127, 0.329] to [0.3457, 0.3585]
#define matrix_cat_d65_to_d50 float3x3(float3(1.04257404804229736f, 0.03089117631316185f, -0.052812620997428894f), float3(0.0221935361623764038f, 1.00185668468475342f, -0.0210737623274326324f), float3(-0.00116488314233720303f, -0.00342052709311246915f, 0.761789083480834961f))

/* Math helper functions ----------------------------*/
// Return identity 3x3 matrix
float3x3 identity()
{
	return float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f));
}

// Safe division of float a by float b
float sdivf(float a, float b)
{
	if (b == 0.0f)
		return 0.0f;
	else
		return a / b;
}

// Safe division of float3 a by float b
float3 sdivf3f(float3 a, float b)
{
	return float3(sdivf(a.x, b), sdivf(a.y, b), sdivf(a.z, b));
}

// Safe element-wise division of float3 a by float3 b
float3 sdivf3f3(float3 a, float3 b)
{
	return float3(sdivf(a.x, b.x), sdivf(a.y, b.y), sdivf(a.z, b.z));
}

// Safe power function raising float a to power float b
float spowf(float a, float b)
{
	if (a <= 0.0f)
		return a;
	else
		return pow(a, b);
}

// Safe power function raising float3 a to power float b
float3 spowf3(float3 a, float b)
{
	return float3(spowf(a.x, b), spowf(a.y, b), spowf(a.z, b));
}
// Return the hypot or vector length of float2 v
float hypotf2(float2 v) { return sqrt(max(0.0f, v.x * v.x + v.y * v.y)); }

// Return the hypot or vector length of float3 v
float hypotf3(float3 v) { return sqrt(max(0.0f, v.x * v.x + v.y * v.y + v.z * v.z)); }

// Return the min of float3 a
float fmaxf3(float3 a) { return max(a.x, max(a.y, a.z)); }

// Return the max of float3 a
float fminf3(float3 a) { return min(a.x, min(a.y, a.z)); }

// Clamp float3 a to max value mx
float3 clampmaxf3(float3 a, float mx) { return float3(min(a.x, mx), min(a.y, mx), min(a.z, mx)); }

// Clamp float3 a to min value mn
float3 clampminf3(float3 a, float mn) { return float3(max(a.x, mn), max(a.y, mn), max(a.z, mn)); }

// Clamp float3 a to min value mn and max value mx
float clampf(float a, float mn, float mx) { return min(max(a, mn), mx); }
float3 clampf3(float3 a, float mn, float mx) { return float3(clampf(a.x, mn, mx), clampf(a.y, mn, mx), clampf(a.z, mn, mx)); }

/* Output and overlay transfer helpers ---------------------------------------- */

float tonescale_overlay_to_scene_linear(float x)
{
	return x < 0.075f ? (x - 0.075f) / 16.184376489665897f : exp((x - 0.5520126568606655f) / 0.09232902596577353f) - 0.0057048244042473785f;
}

/* Functions for OpenDRT ---------------------------------------- */

float compress_hyperbolic_power(float x, float s, float p)
{
	// Simple hyperbolic compression function https://www.desmos.com/calculator/ofwtcmzc3w
	return spowf(x / (x + s), p);
}

float compress_toe_quadratic(float x, float toe, int inv)
{
	// Quadratic toe compress function https://www.desmos.com/calculator/skk8ahmnws
	if (toe == 0.0f)
		return x;
	if (inv == 0) {
		return spowf(x, 2.0f) / (x + toe);
	} else {
		return (x + sqrt(x * (4.0f * toe + x))) / 2.0f;
	}
}

float compress_toe_cubic(float x, float m, float w, int inv)
{
	// https://www.desmos.com/calculator/ubgteikoke
	if (m == 1.0f)
		return x;
	float x2 = x * x;
	if (inv == 0) {
		return x * (x2 + m * w) / (x2 + w);
	} else {
		float p0 = x2 - 3.0f * m * w;
		float p1 = 2.0f * x2 + 27.0f * w - 9.0f * m * w;
		float p2 = pow(sqrt(x2 * p1 * p1 - 4 * p0 * p0 * p0) / 2.0f + x * p1 / 2.0f, 1.0f / 3.0f);
		return p0 / (3.0f * p2) + p2 / 3.0f + x / 3.0f;
	}
}

float complement_power(float x, float p)
{
	return 1.0f - spowf(1.0f - x, 1.0f / p);
}

float sigmoid_cubic(float x, float s)
{
	// Simple cubic sigmoid: https://www.desmos.com/calculator/hzgib42en6
	if (x < 0.0f || x > 1.0f)
		return 1.0f;
	return 1.0f + s * (1.0f - 3.0f * x * x + 2.0f * x * x * x);
}

float contrast_high(float x, float p, float pv, float pv_lx, int inv)
{
	// High exposure adjustment with linear extension
	// https://www.desmos.com/calculator/etjgwyrgad
	const float x0 = 0.18f * pow(2.0f, pv);
	if (x < x0 || p == 1.0f)
		return x;

	const float o = x0 - x0 / p;
	const float s0 = pow(x0, 1.0f - p) / p;
	const float x1 = x0 * pow(2.0f, pv_lx);
	const float k1 = p * s0 * pow(x1, p) / x1;
	const float y1 = s0 * pow(x1, p) + o;
	if (inv == 1)
		return x > y1 ? (x - y1) / k1 + x1 : pow((x - o) / s0, 1.0f / p);
	else
		return x > x1 ? k1 * (x - x1) + y1 : s0 * pow(x, p) + o;
}

float softplus_constraint(float x, float s, float x0, float y0)
{
	// Softplus with (x0, y0) intersection constraint
	// https://www.desmos.com/calculator/doipi4u0ce
	if (x > 10.0f * s + y0 || s < 1e-3f)
		return x;
	float m = 1.0f;
	if (abs(y0) > 1e-6f)
		m = exp(y0 / s);
	m -= exp(x0 / s);
	return s * log(max(0.0f, m + exp(x / s)));
}

float softplus(float x, float s)
{
	// Softplus unconstrained
	// https://www.desmos.com/calculator/mr9rmujsmn
	if (x > 10.0f * s || s < 1e-4f)
		return x;
	return s * log(max(0.0f, 1.0f + exp(x / s)));
}

float gauss_window(float x, float w)
{
	// Simple gaussian window https://www.desmos.com/calculator/vhr9hstlyk
	return exp(-x * x / w);
}

float2 opponent(float3 rgb)
{
	// Simple Cyan-Yellow / Green-Magenta opponent space for calculating smooth achromatic distance and hue angles
	return float2(rgb.x - rgb.z, rgb.y - (rgb.x + rgb.z) / 2.0f);
}

float hue_offset(float h, float o)
{
	// Offset hue maintaining 0-2*Math::PI range with modulo
	return fmod(h - o + Math::PI, 2.0 * Math::PI) - Math::PI;
}

float3 display_gamut_whitepoint(float3 rgb, float tsn, float creative_white_limit, int display_gamut, int creative_white)
{
	rgb = mul(matrix_p3d65_to_xyz, rgb);

	float3 cwp_neutral = rgb;
	float cwp_f = pow(tsn, 2.0f * creative_white_limit);

	if (creative_white == 0)
		rgb = mul(matrix_cat_d65_to_d93, rgb);
	else if (creative_white == 1)
		rgb = mul(matrix_cat_d65_to_d75, rgb);
	else if (creative_white == 3)
		rgb = mul(matrix_cat_d65_to_d60, rgb);
	else if (creative_white == 4)
		rgb = mul(matrix_cat_d65_to_d55, rgb);
	else if (creative_white == 5)
		rgb = mul(matrix_cat_d65_to_d50, rgb);

	rgb = rgb * cwp_f + cwp_neutral * (1.0f - cwp_f);
	rgb = mul(matrix_xyz_to_rec709, rgb);

	float cwp_norm = 1.0f;
	if (display_gamut == 0) {
		if (creative_white == 0)
			cwp_norm = 0.744192699063f;
		else if (creative_white == 1)
			cwp_norm = 0.873470832146f;
		else if (creative_white == 3)
			cwp_norm = 0.955936992163f;
		else if (creative_white == 4)
			cwp_norm = 0.905671332781f;
		else if (creative_white == 5)
			cwp_norm = 0.850004385027f;
	} else if (display_gamut == 1) {  // P3D65 or P3 Limited Rec.2020
		if (creative_white == 0)
			cwp_norm = 0.762687057298f;  // D93
		else if (creative_white == 1)
			cwp_norm = 0.884054083328f;  // D75
		// else if (cwp == 2) cwp_norm = 1.0f; // D65
		else if (creative_white == 3)
			cwp_norm = 0.964320186739f;  // D60
		else if (creative_white == 4)
			cwp_norm = 0.923076518860f;  // D55
		else if (creative_white == 5)
			cwp_norm = 0.876572837784f;  // D50
	}

	rgb *= cwp_norm * cwp_f + 1.0f - cwp_f;
	return rgb;
}

float3 OpenDRTTransform(float3 rgb)
{
	// Move variables here
	// int input_color_space = odrtConfig.input_color_space;
	int output_encoding = enableHDR ? 1 : 0;
	int clamp = odrtConfig.clamp;
	int tn_su = odrtConfig.tn_su;

	float hdr_paper_white_nits = max(hdrPaperWhiteNits, 1.0f);
	float hdr_peak_nits = max(hdrPeakNits, hdr_paper_white_nits);
	float hdr_peak_ratio = hdr_peak_nits / hdr_paper_white_nits;

	float tn_Lp = enableHDR ? hdr_peak_nits : odrtConfig.tn_Lp;
	float tn_gb = odrtConfig.tn_gb;
	float pt_hdr = odrtConfig.pt_hdr;
	float tn_Lg = odrtConfig.tn_Lg;

	float tn_con = odrtConfig.tn_con;
	float tn_sh = odrtConfig.tn_sh;
	float tn_toe = odrtConfig.tn_toe;
	float tn_off = odrtConfig.tn_off;

	int tn_hcon_enable = odrtConfig.tn_hcon_enable;
	float tn_hcon = odrtConfig.tn_hcon;
	float tn_hcon_pv = odrtConfig.tn_hcon_pv;
	float tn_hcon_st = odrtConfig.tn_hcon_st;

	int tn_lcon_enable = odrtConfig.tn_lcon_enable;
	float tn_lcon = odrtConfig.tn_lcon;
	float tn_lcon_w = odrtConfig.tn_lcon_w;
	int cwp = odrtConfig.cwp;

	float cwp_lm = odrtConfig.cwp_lm;
	float rs_sa = odrtConfig.rs_sa;
	float rs_rw = odrtConfig.rs_rw;
	float rs_bw = odrtConfig.rs_bw;

	int pt_enable = odrtConfig.pt_enable;
	float pt_lml = odrtConfig.pt_lml;
	float pt_lml_r = odrtConfig.pt_lml_r;
	float pt_lml_g = odrtConfig.pt_lml_g;

	float pt_lml_b = odrtConfig.pt_lml_b;
	float pt_lmh = odrtConfig.pt_lmh;
	float pt_lmh_r = odrtConfig.pt_lmh_r;
	float pt_lmh_b = odrtConfig.pt_lmh_b;

	int ptl_enable = odrtConfig.ptl_enable;
	float ptl_c = odrtConfig.ptl_c;
	float ptl_m = odrtConfig.ptl_m;
	float ptl_y = odrtConfig.ptl_y;

	int ptm_enable = odrtConfig.ptm_enable;
	float ptm_low = odrtConfig.ptm_low;
	float ptm_low_rng = odrtConfig.ptm_low_rng;
	float ptm_low_st = odrtConfig.ptm_low_st;

	float ptm_high = odrtConfig.ptm_high;
	float ptm_high_rng = odrtConfig.ptm_high_rng;
	float ptm_high_st = odrtConfig.ptm_high_st;
	int brl_enable = odrtConfig.brl_enable;

	float brl = odrtConfig.brl;
	float brl_r = odrtConfig.brl_r;
	float brl_g = odrtConfig.brl_g;
	float brl_b = odrtConfig.brl_b;

	float brl_rng = odrtConfig.brl_rng;
	float brl_st = odrtConfig.brl_st;
	int brlp_enable = odrtConfig.brlp_enable;
	float brlp = odrtConfig.brlp;

	float brlp_r = odrtConfig.brlp_r;
	float brlp_g = odrtConfig.brlp_g;
	float brlp_b = odrtConfig.brlp_b;
	int hc_enable = odrtConfig.hc_enable;

	float hc_r = odrtConfig.hc_r;
	float hc_r_rng = odrtConfig.hc_r_rng;
	int hs_rgb_enable = odrtConfig.hs_rgb_enable;
	float hs_r = odrtConfig.hs_r;

	float hs_r_rng = odrtConfig.hs_r_rng;
	float hs_g = odrtConfig.hs_g;
	float hs_g_rng = odrtConfig.hs_g_rng;
	float hs_b = odrtConfig.hs_b;

	float hs_b_rng = odrtConfig.hs_b_rng;
	int hs_cmy_enable = odrtConfig.hs_cmy_enable;
	float hs_c = odrtConfig.hs_c;
	float hs_c_rng = odrtConfig.hs_c_rng;

	float hs_m = odrtConfig.hs_m;
	float hs_m_rng = odrtConfig.hs_m_rng;
	float hs_y = odrtConfig.hs_y;
	float hs_y_rng = odrtConfig.hs_y_rng;

	// float3x3 in_to_xyz = input_color_space == 1 ? matrix_ap1_to_xyz : matrix_rec709_to_xyz;

	/***************************************************
    Tonescale Constraint Calculations
    https://www.desmos.com/calculator/1c4fhzy3bw

    These should be pre-calculated but there is no way to do this in DCTL.
    Anything that is const should be precalculated and not run per-pixel
    --------------------------------------------------*/
	const float ts_x1 = pow(2.0f, 6.0f * tn_sh + 4.0f);
	const float ts_y1 = tn_Lp / 100.0f;
	const float ts_x0 = 0.18f + tn_off;
	const float ts_y0 = tn_Lg / 100.0f * (1.0f + tn_gb * log2(ts_y1));
	const float ts_s0 = compress_toe_quadratic(ts_y0, tn_toe, 1);
	const float ts_p = tn_con / (1.0f + (float)tn_su * 0.05f);  // unconstrained surround compensation
	const float ts_s10 = ts_x0 * (pow(ts_s0, -1.0f / tn_con) - 1.0f);
	const float ts_m1 = ts_y1 / pow(ts_x1 / (ts_x1 + ts_s10), tn_con);
	const float ts_m2 = compress_toe_quadratic(ts_m1, tn_toe, 1);
	const float ts_s = ts_x0 * (pow(ts_s0 / ts_m2, -1.0f / tn_con) - 1.0f);
	const float ts_dsc = 100.0f / tn_Lp;

	// Lerp from pt_cmp at 100 nits to pt_cmp_hdr at 1000 nits
	const float pt_cmp_Lf = pt_hdr * min(1.0f, (tn_Lp - 100.0f) / 900.0f);
	// Approximate scene-linear scale at Lp=100 nits
	const float s_Lp100 = ts_x0 * (pow((tn_Lg / 100.0f), -1.0f / tn_con) - 1.0f);
	const float ts_s1 = ts_s * pt_cmp_Lf + s_Lp100 * (1.0f - pt_cmp_Lf);

	// Convert from input gamut into P3-D65
	// rgb = mul(in_to_xyz, rgb);
	rgb = mul(matrix_xyz_to_p3d65, rgb);

	// Rendering Space: "Desaturate" to control scale of the color volume in the rgb ratios.
	// Controlled by rs_sa (saturation) and red and blue weights (rs_rw and rs_bw)
	float3 rs_w = float3(rs_rw, 1.0f - rs_rw - rs_bw, rs_bw);
	float sat_L = rgb.x * rs_w.x + rgb.y * rs_w.y + rgb.z * rs_w.z;
	rgb = sat_L * rs_sa + rgb * (1.0f - rs_sa);

	// Offset
	rgb += tn_off;

	// Tonescale Norm
	float tsn = hypotf3(rgb) / 1.73205080756887729353f;  // 1 / sqrt3

	// RGB Ratios
	rgb = sdivf3f(rgb, tsn);

	float2 opp = opponent(rgb);
	float ach_d = hypotf2(opp) / 2.0f;

	// Smooth ach_d, normalized so 1.0 doesn't change https://www.desmos.com/calculator/ozjg09hzef
	ach_d = (1.25f) * compress_toe_quadratic(ach_d, 0.25f, 0);

	// Hue angle, rotated so that red = 0.0
	float hue = fmod(atan2(opp.x, opp.y) + Math::PI + 1.10714931f, 2.0f * Math::PI);

	// RGB Hue Angles
	// Wider than CMY by default. R towards M, G towards Y, B towards C
	float3 ha_rgb = float3(
		gauss_window(hue_offset(hue, 0.1f), 0.66f),
		gauss_window(hue_offset(hue, 4.3f), 0.66f),
		gauss_window(hue_offset(hue, 2.3f), 0.66f));

	// RGB Hue Angles for hue shift: red shifted more orange
	float3 ha_rgb_hs = float3(
		gauss_window(hue_offset(hue, -0.4f), 0.66f),
		ha_rgb.y,
		gauss_window(hue_offset(hue, 2.5f), 0.66f));

	// CMY Hue Angles
	// Exact alignment to Cyan/Magenta/Yellow secondaries would be Math::PI, Math::PI/3 and -Math::PI/3, but
	// we customize these a bit for creative purposes: M towards B, Y towards G, C towards G
	float3 ha_cmy = float3(
		gauss_window(hue_offset(hue, 3.3f), 0.5f),
		gauss_window(hue_offset(hue, 1.3f), 0.5f),
		gauss_window(hue_offset(hue, -1.15f), 0.5f));

	// Brilliance
	if (brl_enable != 0) {
		float brl_tsf = pow(tsn / (tsn + 1.0f), 1.0f - brl_rng);
		float brl_exf = (brl + brl_r * ha_rgb.x + brl_g * ha_rgb.y + brl_b * ha_rgb.z) * pow(ach_d, 1.0f / brl_st);
		float brl_ex = pow(2.0f, brl_exf * (brl_exf < 0.0f ? brl_tsf : 1.0f - brl_tsf));
		tsn *= brl_ex;
	}

	// Contrast Low
	if (tn_lcon_enable != 0) {
		float lcon_m = pow(2.0f, -tn_lcon);
		float lcon_w = tn_lcon_w / 4.0f;
		lcon_w *= lcon_w;

		// Normalize for ts_x0 intersection constraint: https://www.desmos.com/calculator/blyvi8t2b2
		const float lcon_cnst_sc = compress_toe_cubic(ts_x0, lcon_m, lcon_w, 1) / ts_x0;
		tsn *= lcon_cnst_sc;
		tsn = compress_toe_cubic(tsn, lcon_m, lcon_w, 0);
	}

	// Contrast High
	if (tn_hcon_enable != 0) {
		float hcon_p = pow(2.0f, tn_hcon);
		tsn = contrast_high(tsn, hcon_p, tn_hcon_pv, tn_hcon_st, 0);
	}

	// Hyperbolic Compression
	float tsn_pt = compress_hyperbolic_power(tsn, ts_s1, ts_p);
	float tsn_const = compress_hyperbolic_power(tsn, s_Lp100, ts_p);
	tsn = compress_hyperbolic_power(tsn, ts_s, ts_p);

	/***************************************************
    Hue Contrast R
  --------------------------------------------------*/
	if (hc_enable != 0) {
		float hc_ts = 1.0f - tsn_const;
		// Limit high purity on bottom end and low purity on top end by ach_d.
		// This helps reduce artifacts and over-saturation.
		float hc_c = hc_ts * (1.0f - ach_d) + ach_d * (1.0f - hc_ts);
		hc_c *= ach_d * ha_rgb.x;
		hc_ts = pow(hc_ts, 1.0f / hc_r_rng);
		// Bias contrast based on tonescale using Lift/Mult: https://www.desmos.com/calculator/gzbgov62hl
		float hc_f = hc_r * (hc_c - 2.0f * hc_c * hc_ts) + 1.0f;
		rgb = float3(rgb.x, rgb.y * hc_f, rgb.z * hc_f);
	}

	/***************************************************
    Hue Shift
  --------------------------------------------------*/
	// Hue Shift RGB by purity compress tonescale, shifting more as intensity increases
	if (hs_rgb_enable != 0) {
		float3 hs_rgb = float3(
			ha_rgb_hs.x * ach_d * pow(tsn_pt, 1.0f / hs_r_rng),
			ha_rgb_hs.y * ach_d * pow(tsn_pt, 1.0f / hs_g_rng),
			ha_rgb_hs.z * ach_d * pow(tsn_pt, 1.0f / hs_b_rng));
		float3 hsf = float3(hs_rgb.x * hs_r, hs_rgb.y * -hs_g, hs_rgb.z * -hs_b);
		hsf = float3(hsf.z - hsf.y, hsf.x - hsf.z, hsf.y - hsf.x);
		rgb += hsf;
	}

	// Hue Shift CMY by tonescale, shifting less as intensity increases
	if (hs_cmy_enable != 0) {
		float tsn_pt_compl = 1.0f - tsn_pt;
		float3 hs_cmy = float3(
			ha_cmy.x * ach_d * pow(tsn_pt_compl, 1.0f / hs_c_rng),
			ha_cmy.y * ach_d * pow(tsn_pt_compl, 1.0f / hs_m_rng),
			ha_cmy.z * ach_d * pow(tsn_pt_compl, 1.0f / hs_y_rng));
		float3 hsf = float3(hs_cmy.x * -hs_c, hs_cmy.y * hs_m, hs_cmy.z * hs_y);
		hsf = float3(hsf.z - hsf.y, hsf.x - hsf.z, hsf.y - hsf.x);
		rgb += hsf;
	}

	/***************************************************
    Purity Compression
      https://www.desmos.com/calculator/adtzkjofgn
  --------------------------------------------------*/
	float ptf = 1.0f;
	if (pt_enable != 0) {
		// Purity Limit Low
		float pt_lml_p = 1.0f + 4.0f * (1.0f - tsn_pt) * (pt_lml + pt_lml_r * ha_rgb_hs.x + pt_lml_g * ha_rgb_hs.y + pt_lml_b * ha_rgb_hs.z);
		ptf = 1.0f - pow(tsn_pt, pt_lml_p);

		// Purity Limit High
		float pt_lmh_p = (1.0f - ach_d * (pt_lmh_r * ha_rgb_hs.x + pt_lmh_b * ha_rgb_hs.z)) * (1.0f - pt_lmh * ach_d);
		ptf = pow(ptf, pt_lmh_p);
	}

	/***************************************************
    Mid-Range Purity
      This boosts mid-range purity on the low end
      and reduces mid-range purity on the high end
  --------------------------------------------------*/
	if (ptm_enable != 0) {
		float ptm_low_f;
		if (ptm_low_st == 0.0f || ptm_low_rng == 0.0f)
			ptm_low_f = 1.0f;
		else
			ptm_low_f = 1.0f + ptm_low * exp(-2.0f * ach_d * ach_d / ptm_low_st) * pow(1.0f - tsn_const, 1.0f / ptm_low_rng);
		float ptm_high_f;
		if (ptm_high_st == 0.0f || ptm_high_rng == 0.0f)
			ptm_high_f = 1.0f;
		else
			ptm_high_f = 1.0f + ptm_high * exp(-2.0f * ach_d * ach_d / ptm_high_st) * pow(tsn_pt, 1.0f / (4.0f * ptm_high_rng));
		ptf *= ptm_low_f * ptm_high_f;
	}

	// Lerp to peak achromatic by ptf in rgb ratios
	rgb = rgb * ptf + 1.0f - ptf;

	// Inverse Rendering Space
	sat_L = rgb.x * rs_w.x + rgb.y * rs_w.y + rgb.z * rs_w.z;
	rgb = (sat_L * rs_sa - rgb) / (rs_sa - 1.0f);

	// Convert to linear Rec.709/sRGB and set whitepoint.
	rgb = display_gamut_whitepoint(rgb, tsn_const, cwp_lm, output_encoding, cwp);

	// Post Brilliance
	if (brlp_enable != 0) {
		float2 brlp_opp = opponent(rgb);
		float brlp_ach_d = hypotf2(brlp_opp) / 4.0f;
		// brlp_ach_d = 1.0f - gauss_window(brlp_ach_d, 8.0f);
		brlp_ach_d = 1.1f * (brlp_ach_d * brlp_ach_d / (brlp_ach_d + 0.1f));
		float3 brlp_ha_rgb = ach_d * ha_rgb;
		float brlp_m = brlp + brlp_r * brlp_ha_rgb.x + brlp_g * brlp_ha_rgb.y + brlp_b * brlp_ha_rgb.z;
		float brlp_ex = pow(2.0f, brlp_m * brlp_ach_d * tsn);
		rgb *= brlp_ex;
	}

	// Purity Compress Low
	if (ptl_enable != 0)
		rgb = float3(softplus(rgb.x, ptl_c), softplus(rgb.y, ptl_m), softplus(rgb.z, ptl_y));

	// Final tonescale adjustments
	tsn *= ts_m2;  // scale for inverse toe
	tsn = compress_toe_quadratic(tsn, tn_toe, 0);
	tsn *= ts_dsc;  // scale for display encoding

	// Return from RGB ratios
	rgb *= tsn;

	// Rec.2020 (P3 Limited)
	if (output_encoding == 1) {
		rgb = clampminf3(rgb, 0.0f);  // Limit to P3 gamut
		rgb = mul(matrix_p3_to_rec2020, rgb);
	}

	// OpenDRT returns display-linear normalized to display peak.
	// HDRDisplay expects scene output normalized to paper white: 1.0 = paper white.
	if (enableHDR)
		rgb *= hdr_peak_ratio;

	// Clamp
	if (clamp != 0)
		rgb = clampf3(rgb, 0.0f, enableHDR ? hdr_peak_ratio : 1.0f);

	return rgb;
}

#pragma once

#include "OpenDRT.h"

// Helper structs for JSON serialization (split due to macro limitations)
struct OpenDRTSettingsPart1
{
	int32_t input_color_space = 0;
	int32_t output_encoding = 0;
	int32_t clamp = 1;
	int32_t tn_su = 1;
	float tn_Lp = 100.0f;
	float tn_gb = 0.13f;
	float pt_hdr = 0.5f;
	float tn_Lg = 10.0f;
	float tn_con = 1.66f;
	float tn_sh = 0.5f;
	float tn_toe = 0.003f;
	float tn_off = 0.005f;
	int32_t tn_hcon_enable = 0;
	float tn_hcon = 0.0f;
	float tn_hcon_pv = 1.0f;
	float tn_hcon_st = 4.0f;
	int32_t tn_lcon_enable = 0;
	float tn_lcon = 0.0f;
	float tn_lcon_w = 0.5f;
	int32_t cwp = 2;
	float cwp_lm = 0.25f;
	float rs_sa = 0.35f;
	float rs_rw = 0.25f;
	float rs_bw = 0.55f;
	int32_t pt_enable = 1;
	float pt_lml = 0.25f;
	float pt_lml_r = 0.5f;
	float pt_lml_g = 0.0f;
	float pt_lml_b = 0.1f;
	float pt_lmh = 0.25f;
	float pt_lmh_r = 0.5f;
	float pt_lmh_b = 0.0f;
	int32_t ptl_enable = 1;
	float ptl_c = 0.06f;
	float ptl_m = 0.08f;
	float ptl_y = 0.06f;
	int32_t ptm_enable = 1;
	float ptm_low = 0.4f;
	float ptm_low_rng = 0.25f;
	float ptm_low_st = 0.5f;
	float ptm_high = -0.8f;
	float ptm_high_rng = 0.35f;
	float ptm_high_st = 0.4f;
};
static_assert(sizeof(OpenDRTSettingsPart1) == offsetof(OpenDRTSettings, brl_enable));

struct OpenDRTSettingsPart2
{
	int32_t brl_enable = 1;
	float brl = 0.0f;
	float brl_r = -2.5f;
	float brl_g = -1.5f;
	float brl_b = -1.5f;
	float brl_rng = 0.5f;
	float brl_st = 0.35f;
	int32_t brlp_enable = 1;
	float brlp = -0.5f;
	float brlp_r = -1.25f;
	float brlp_g = -1.25f;
	float brlp_b = -0.25f;
	int32_t hc_enable = 1;
	float hc_r = 1.0f;
	float hc_r_rng = 0.3f;
	int32_t hs_rgb_enable = 1;
	float hs_r = 0.6f;
	float hs_r_rng = 0.6f;
	float hs_g = 0.35f;
	float hs_g_rng = 1.0f;
	float hs_b = 0.66f;
	float hs_b_rng = 1.0f;
	int32_t hs_cmy_enable = 1;
	float hs_c = 0.25f;
	float hs_c_rng = 1.0f;
	float hs_m = 0.0f;
	float hs_m_rng = 1.0f;
	float hs_y = 0.0f;
	float hs_y_rng = 1.0f;
};
static_assert(sizeof(OpenDRTSettingsPart1) + sizeof(OpenDRTSettingsPart2) == sizeof(OpenDRTSettings));

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	OpenDRTSettingsPart1,
	input_color_space,
	output_encoding,
	clamp,
	tn_su,
	tn_Lp,
	tn_gb,
	pt_hdr,
	tn_Lg,
	tn_con,
	tn_sh,
	tn_toe,
	tn_off,
	tn_hcon_enable,
	tn_hcon,
	tn_hcon_pv,
	tn_hcon_st,
	tn_lcon_enable,
	tn_lcon,
	tn_lcon_w,
	cwp,
	cwp_lm,
	rs_sa,
	rs_rw,
	rs_bw,
	pt_enable,
	pt_lml,
	pt_lml_r,
	pt_lml_g,
	pt_lml_b,
	pt_lmh,
	pt_lmh_r,
	pt_lmh_b,
	ptl_enable,
	ptl_c,
	ptl_m,
	ptl_y,
	ptm_enable,
	ptm_low,
	ptm_low_rng,
	ptm_low_st,
	ptm_high,
	ptm_high_rng,
	ptm_high_st)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	OpenDRTSettingsPart2,
	brl_enable,
	brl,
	brl_r,
	brl_g,
	brl_b,
	brl_rng,
	brl_st,
	brlp_enable,
	brlp,
	brlp_r,
	brlp_g,
	brlp_b,
	hc_enable,
	hc_r,
	hc_r_rng,
	hs_rgb_enable,
	hs_r,
	hs_r_rng,
	hs_g,
	hs_g_rng,
	hs_b,
	hs_b_rng,
	hs_cmy_enable,
	hs_c,
	hs_c_rng,
	hs_m,
	hs_m_rng,
	hs_y,
	hs_y_rng)

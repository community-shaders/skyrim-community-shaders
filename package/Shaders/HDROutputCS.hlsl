#include "Common/ACES.hlsli"
#include "Common/Color.hlsli"
#include "Common/Tonemappers.hlsli"
#include "Common/frostbite.hlsli"
#include "Common/renodx.hlsl"

Texture2D<float4> Framebuffer : register(t0);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	// parameters0.x = tonemapOperator
	// parameters0.y = paperWhite
	// parameters0.z = peakNits
	// parameters0.w = exposure
	float4 parameters0 : packoffset(c0);
	// parameters1.x = highlights
	// parameters1.y = shadows
	// parameters1.z = contrast
	// parameters1.w = saturation
	float4 parameters1 : packoffset(c1);
	// parameters2.x = dechroma
	// parameters2.y = hueCorrectionStrength
	// parameters2.z = 0.f // Currently unused
	// parameters2.w = 0.f // Currently unused
	float4 parameters2 : packoffset(c2);
}

float3 ApplyRenoDRT(float3 untonemapped)
{
	renodx::tonemap::renodrt::Config renoDRTConfig = renodx::tonemap::renodrt::config::Create();

	renoDRTConfig.nits_peak = 10000.f;
	renoDRTConfig.mid_gray_value = 0.18f;
	renoDRTConfig.mid_gray_nits = 18.f;
	renoDRTConfig.exposure = 1.f;
	renoDRTConfig.highlights = parameters1.x;
	renoDRTConfig.shadows = parameters1.y;
	renoDRTConfig.contrast = parameters1.z;
	renoDRTConfig.saturation = parameters1.w;
	renoDRTConfig.dechroma = parameters2.x;
	renoDRTConfig.flare = 0.f;
	renoDRTConfig.hue_correction_strength = parameters2.y;
	renoDRTConfig.hue_correction_source = 0;
	renoDRTConfig.hue_correction_method = renodx::tonemap::renodrt::config::hue_correction_method::OKLAB;
	renoDRTConfig.tone_map_method = renodx::tonemap::renodrt::config::tone_map_method::REINHARD;
	renoDRTConfig.hue_correction_type = renodx::tonemap::renodrt::config::hue_correction_type::INPUT;
	renoDRTConfig.working_color_space = 1u;
	renoDRTConfig.per_channel = false;
	renoDRTConfig.blowout = 0.f;
	renoDRTConfig.clamp_color_space = 2.f;
	renoDRTConfig.clamp_peak = 0.f;
	renoDRTConfig.white_clip = 100.f;

	float3 tonemapped = renodx::tonemap::renodrt::BT709(untonemapped, renoDRTConfig);

	tonemapped = Tonemap::ExponentialRolloff::Apply(tonemapped, renoDRTConfig.mid_gray_nits / 100.f, max(1, parameters0.z / parameters0.y));
	return tonemapped;
}

float3 DICEPlus(float3 color, renodx::tonemap::DICEPlus::DICEConfig DICEConfig)
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
	color = Tonemap::ExponentialRolloff::Apply(color, 0.2f, RhPeak);
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

float3 ApplyDICEPlus(float3 untonemapped)
{
	float3 tonemapped = untonemapped;

	renodx::tonemap::DICEPlus::DICEConfig diceConfig = renodx::tonemap::DICEPlus::config::Create();
	diceConfig.peak_nits = max(parameters0.z, parameters0.y);
	diceConfig.game_nits = parameters0.y;
	diceConfig.highlights = parameters1.x;
	diceConfig.shadows = parameters1.y;
	diceConfig.contrast = parameters1.z;
	diceConfig.saturation = parameters1.w;
	// diceConfig.dechroma = parameters2.x;
	diceConfig.hue_correction_strength = parameters2.y;

	tonemapped = DICEPlus(tonemapped, diceConfig);

	return tonemapped;
}

float3 ApplyReinhardJodieHDR(float3 untonemapped)
{
	float midGray = Tonemap::ReinhardJodie::Apply(0.18f);

	float3 hdrColor = untonemapped * (midGray / 0.18f);  // match midgray

	float3 sdrColor = Tonemap::ReinhardJodie::Apply(untonemapped);
	hdrColor = Tonemap::ExponentialRolloff::Apply(hdrColor, midGray, max(1.f, parameters0.z / parameters0.y));

	float3 blendedColor = lerp(sdrColor, hdrColor, saturate(sdrColor));

	return blendedColor;
}

float3 ApplyUncharted2HDR(float3 untonemapped)
{
	float midGray = Tonemap::Uncharted2::Apply(0.18f, 0.15f, 0.50f, 0.10f, 0.20f, 0.02f, 0.30f, 11.2f);

	float3 hdrColor = untonemapped * (midGray / 0.18f);  // match midgray

	float3 sdrColor = Tonemap::Uncharted2::Apply(untonemapped, 0.15f, 0.50f, 0.10f, 0.20f, 0.02f, 0.30f, 11.2f);
	hdrColor = Tonemap::ExponentialRolloff::Apply(hdrColor, midGray, max(1.f, parameters0.z / parameters0.y));

	float3 blendedColor = lerp(sdrColor, hdrColor, saturate(sdrColor));

	return blendedColor;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float4 framebuffer = Framebuffer[dispatchID.xy];

	// Linearize the incoming HDR buffer
	float3 untonemapped = Color::GammaToLinearSafe(framebuffer.xyz);

	float3 linearExposed = untonemapped * parameters0.w;

	float3 tonemapped;
	switch ((int)parameters0.x) {
	default:
	case 0:  // untonemapped
		tonemapped = linearExposed;
		break;
	case 1:  // saturate()
		tonemapped = saturate(linearExposed);
		break;
	case 2:  // Gamma Frostbite
		float3 gammaExposed = Color::LinearToGammaSafe(linearExposed);
		tonemapped = tonemap::frostbite::BT709(gammaExposed, max(1.f, (float)Color::LinearToGammaSafe(parameters0.z / parameters0.y)), 0.5);
		tonemapped = Color::GammaToLinearSafe(tonemapped);
		break;
	case 3:  // Reinhard-Jodie
		tonemapped = ApplyReinhardJodieHDR(linearExposed);
		break;
	case 4:  // ACES
		const float ACES_MIN = 0.0001f / parameters0.y;
		const float ACES_MAX = parameters0.z / parameters0.y;
		tonemapped = Tonemap::ACES::RRTAndODT(linearExposed, ACES_MIN * 48.f, ACES_MAX * 48.f) / 48.f;

		break;
	case 5:  // Uncharted 2
		tonemapped = ApplyUncharted2HDR(linearExposed);
		break;
	case 6:  // DICE Plus
		tonemapped = ApplyDICEPlus(linearExposed);
		break;
	case 7:  // RenoDRT
		tonemapped = ApplyRenoDRT(linearExposed);
		break;
	}

	float3 bt2020Color = Color::BT709ToBT2020(tonemapped);
	float3 pqColor = Color::pq::Encode(bt2020Color, parameters0.y);

	HDROutput[dispatchID.xy] = float4(pqColor, framebuffer.w);
}

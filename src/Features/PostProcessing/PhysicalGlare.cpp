#include "PhysicalGlare.h"

#include "Features/LinearLighting.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

namespace
{
	uint NormaliseFFTResolution(int resolution)
	{
		const uint clamped = std::clamp(static_cast<uint>(std::max(resolution, 0)), PhysicalGlare::FFT_MIN, PhysicalGlare::FFT_MAX);
		// Preserve at least the requested detail for hand-edited configurations.
		return std::bit_ceil(clamped);
	}

	uint GetFFTVariant(uint resolution)
	{
		return static_cast<uint>(std::countr_zero(resolution) - std::countr_zero(PhysicalGlare::FFT_MIN));
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PhysicalGlare::Settings,
	ThresholdEV,
	Intensity,
	ApertureMode,
	ApertureBlades,
	ApertureRotation,
	ScatterStrength,
	AdaptSpeed,
	FFTResolution,
	FresnelExponent,
	ChromaticSpread,
	FStop,
	SphericalAberration,
	KernelScale,
	PSFSharpness,
	PSFNoiseFloor,
	PaddingRatio,
	EnableEyelashes,
	EyelashCount,
	EyelashLength,
	EyelashCurvature,
	ParticleCount,
	ParticleSize,
	GratingCount,
	GratingStrength,
	TearFilmStrength,
	TearFilmSpeed,
	TearFilmComplexity,
	SutureBranches,
	SutureStrength,
	SutureWidth,
	StarburstCount,
	StarburstStrength,
	StarburstIrregularity,
	DustCount,
	DustSize,
	BladeRoughnessFreq,
	BladeRoughnessAmp,
	ScratchCount,
	ScratchOpacity,
	ScratchLength,
	ScratchWidth)

void PhysicalGlare::DrawSettings()
{
	ImGui::SliderFloat(T("feature.post_processing.physical_glare.threshold", "Threshold"), &settings.ThresholdEV, -7.f, 23.f, "%+.2f EV100");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.per_channel_brightness_threshold_for_glare_extraction_in", "Per-channel brightness threshold for glare extraction in EV100 (0 EV100 = 0.125 linear luminance)."));

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.intensity", "Intensity"), &settings.Intensity, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.overall_glare_intensity", "Overall glare intensity."));

	{
		const char* modeNames[] = { "Lens (N-polygon)", "Pupil (Circle)" };
		ImGui::Combo(T("feature.post_processing.physical_glare.aperture_mode", "Aperture Mode"), &settings.ApertureMode, modeNames, 2);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.lens_camera_lens_polygon_starburst_pupil_circular_human", "Lens: camera lens polygon starburst. Pupil: circular human eye aperture."));
	}

	if (settings.ApertureMode == 0) {
		ImGui::SliderInt(T("feature.post_processing.physical_glare.aperture_blades", "Aperture Blades"), &settings.ApertureBlades, 3, 10);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.number_of_aperture_blades_controls_starburst_pattern", "Number of aperture blades. Controls starburst pattern."));

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.f_stop", "F-Stop"), &settings.FStop, 1.0f, 22.0f, "F%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.aperture_f_number_e_g_f2_8_smaller", "Aperture f-number (e.g. F2.8). Smaller = larger aperture = wider diffraction spikes.\nPhysically: aperture radius = 1 / f-number."));

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.spherical_aberration", "Spherical Aberration"), &settings.SphericalAberration, 0.f, 100.f, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				T("feature.post_processing.physical_glare.seidel_spherical_aberration_r_4_wavefront_error_models",
					"Seidel spherical aberration (r^4 wavefront error).\n"
					"Models lens curvature: outer rays focus at a different point\n"
					"than central rays, producing concentric ring structure in the\n"
					"PSF and softer glare edges. Physical range: 0-50."));

		ImGui::SliderInt(T("feature.post_processing.physical_glare.dust_count", "Dust Count"), &settings.DustCount, 0, 500);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.dust_particles_on_lens_element_surfaces_produces_scattered", "Dust particles on lens element surfaces.\nProduces scattered haze via Babinet's principle."));

		if (settings.DustCount > 0) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.dust_size", "Dust Size"), &settings.DustSize, 0.5f, 5.f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.radius_of_each_dust_particle_in_pixels", "Radius of each dust particle in pixels."));
		}

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.blade_roughness", "Blade Roughness"), &settings.BladeRoughnessAmp, 0.f, 2.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.micro_serrations_on_aperture_blade_edges_manufacturing_imperfections", "Micro-serrations on aperture blade edges (manufacturing imperfections).\nMakes star spikes slightly fuzzy/irregular. 0 = perfect edges."));

		if (settings.BladeRoughnessAmp > 0.f) {
			ImGui::SliderInt(T("feature.post_processing.physical_glare.roughness_frequency", "Roughness Frequency"), &settings.BladeRoughnessFreq, 5, 100);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.number_of_bumps_per_blade_edge_higher_finer", "Number of bumps per blade edge. Higher = finer serrations."));
		}

		ImGui::SliderInt(T("feature.post_processing.physical_glare.scratch_count", "Scratch Count"), &settings.ScratchCount, 0, 20);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.linear_scratches_on_lens_element_surfaces_each_scratch", "Linear scratches on lens element surfaces.\nEach scratch produces a perpendicular streak in the glare."));

		if (settings.ScratchCount > 0) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.scratch_opacity", "Scratch Opacity"), &settings.ScratchOpacity, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.how_opaque_each_scratch_is_higher_more_visible", "How opaque each scratch is. Higher = more visible streaks."));

			ImGui::SliderFloat(T("feature.post_processing.physical_glare.scratch_length", "Scratch Length"), &settings.ScratchLength, 0.2f, 1.5f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.length_of_scratches_relative_to_aperture_size", "Length of scratches relative to aperture size."));

			ImGui::SliderFloat(T("feature.post_processing.physical_glare.scratch_width", "Scratch Width"), &settings.ScratchWidth, 0.5f, 4.f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.pixel_width_of_each_scratch", "Pixel width of each scratch."));
		}
	}

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.aperture_rotation", "Aperture Rotation"), &settings.ApertureRotation, -180.f, 180.f, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.rotation_angle_of_the_aperture", "Rotation angle of the aperture."));

	if (settings.ApertureMode == 1) {
		ImGui::SliderFloat(T("feature.post_processing.physical_glare.scatter_strength", "Scatter Strength"), &settings.ScatterStrength, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.opacity_of_scatter_particles_in_pupil_mode_paper", "Opacity of scatter particles in pupil mode (paper section 2.4).\n0 = transparent (no scatter), 1 = fully opaque."));

		ImGui::SliderInt(T("feature.post_processing.physical_glare.particle_count", "Particle Count"), &settings.ParticleCount, 0, 1000);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.number_of_scatter_particles_in_lens_vitreous_ritschel", "Number of scatter particles in lens/vitreous (Ritschel: 750).\nProduces ciliary corona needle pattern via Babinet's principle."));

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.particle_size", "Particle Size"), &settings.ParticleSize, 0.5f, 5.f, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.radius_of_each_particle_in_pixels", "Radius of each particle in pixels."));

		ImGui::SliderInt(T("feature.post_processing.physical_glare.grating_count", "Grating Count"), &settings.GratingCount, 0, 400);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.number_of_radial_lens_gratings_paper_section_2", "Number of radial lens gratings (paper section 2.4: Ritschel uses 200).\nProduces lenticular halo via edge diffraction."));

		if (settings.GratingCount > 0) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.grating_strength", "Grating Strength"), &settings.GratingStrength, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.opacity_of_lens_gratings_higher_stronger_lenticular_halo", "Opacity of lens gratings. Higher = stronger lenticular halo."));
		}

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.tear_film_strength", "Tear Film Strength"), &settings.TearFilmStrength, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.simulates_tear_film_irregularities_on_the_cornea_surface", "Simulates tear film irregularities on the cornea surface.\nProduces flickering, sharp, irregular star spikes.\n0 = disabled (static PSF)."));

		if (settings.TearFilmStrength > 0.f) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.tear_film_speed", "Tear Film Speed"), &settings.TearFilmSpeed, 0.1f, 8.f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.how_fast_the_tear_film_fluctuates_blink_refresh", "How fast the tear film fluctuates (blink refresh rate ~0.3Hz, breakup ~2-5Hz)."));

			ImGui::SliderInt(T("feature.post_processing.physical_glare.tear_film_complexity", "Tear Film Complexity"), &settings.TearFilmComplexity, 3, 16);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.number_of_angular_harmonics_more_more_spikes_finer", "Number of angular harmonics. More = more spikes, finer detail."));
		}

		ImGui::SliderInt(T("feature.post_processing.physical_glare.suture_branches", "Suture Branches"), &settings.SutureBranches, 0, 8);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				T("feature.post_processing.physical_glare.lens_suture_lines_y_shaped_junctions_where_lens",
					"Lens suture lines: Y-shaped junctions where lens fiber cells meet.\n"
					"3 = young eye (anterior Y + posterior inverted Y = 6 spikes).\n"
					"More branches = older/more complex lens. 0 = disabled."));

		if (settings.SutureBranches > 0) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.suture_strength", "Suture Strength"), &settings.SutureStrength, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.opacity_of_suture_lines_higher_stronger_star_spikes", "Opacity of suture lines. Higher = stronger star spikes."));

			ImGui::SliderFloat(T("feature.post_processing.physical_glare.suture_width", "Suture Width"), &settings.SutureWidth, 0.5f, 5.f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.pixel_width_of_each_suture_line_thinner_sharper", "Pixel width of each suture line. Thinner = sharper spikes."));
		}

		ImGui::SliderInt(T("feature.post_processing.physical_glare.starburst_spikes", "Starburst Spikes"), &settings.StarburstCount, 0, 128);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				T("feature.post_processing.physical_glare.lens_fiber_radial_phase_grating_creates_many_thin",
					"Lens fiber radial phase grating.\nCreates many thin, sharp radial star spikes.\n"
					"Higher count = more spikes (typical human eye: 20-80). 0 = disabled."));

		if (settings.StarburstCount > 0) {
			ImGui::SliderFloat(T("feature.post_processing.physical_glare.starburst_strength", "Starburst Strength"), &settings.StarburstStrength, 0.f, 2.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.phase_shift_strength_per_fiber_higher_brighter_spikes", "Phase shift strength per fiber. Higher = brighter spikes."));

			ImGui::SliderFloat(T("feature.post_processing.physical_glare.starburst_irregularity", "Starburst Irregularity"), &settings.StarburstIrregularity, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					T("feature.post_processing.physical_glare.random_variation_in_fiber_spacing_and_strength_0",
						"Random variation in fiber spacing and strength.\n"
						"0 = perfectly regular (even spikes).\n"
						"1 = maximally irregular (natural look)."));
		}

		if (ImGui::CollapsingHeader(T("feature.post_processing.physical_glare.eyelashes", "Eyelashes"))) {
			ImGui::Checkbox(T("feature.post_processing.physical_glare.enable_eyelashes", "Enable Eyelashes"), &settings.EnableEyelashes);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.physical_glare.simulate_eyelash_occlusion_for_streak_effects_paper_section", "Simulate eyelash occlusion for streak effects (paper section 3.1)."));

			if (settings.EnableEyelashes) {
				ImGui::SliderInt(T("feature.post_processing.physical_glare.eyelash_count", "Eyelash Count"), &settings.EyelashCount, 5, 80);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(T("feature.post_processing.physical_glare.total_number_of_eyelash_hairs_upper_lower", "Total number of eyelash hairs (upper + lower)."));

				ImGui::SliderFloat(T("feature.post_processing.physical_glare.eyelash_length", "Eyelash Length"), &settings.EyelashLength, 0.1f, 0.8f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(T("feature.post_processing.physical_glare.length_of_eyelashes_relative_to_aperture_radius", "Length of eyelashes relative to aperture radius."));

				ImGui::SliderFloat(T("feature.post_processing.physical_glare.eyelash_curvature", "Eyelash Curvature"), &settings.EyelashCurvature, 0.f, 1.f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(T("feature.post_processing.physical_glare.streak_curvature_via_uv_bending_paper_fig_3", "Streak curvature via UV bending (paper fig 3.7: sin(x) vertical offset)."));
			}
		}
	}

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.adapt_speed", "Adapt Speed"), &settings.AdaptSpeed, 0.5f, 10.f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.how_fast_the_glare_adapts_to_brightness_changes", "How fast the glare adapts to brightness changes."));

	{
		const char* resNames[] = { "128", "256", "512", "1024" };
		int resValues[] = { 128, 256, 512, 1024 };
		int curIdx = 1;
		for (int i = 0; i < 4; i++)
			if (resValues[i] == settings.FFTResolution)
				curIdx = i;

		if (ImGui::Combo(T("feature.post_processing.physical_glare.fft_resolution", "FFT Resolution"), &curIdx, resNames, 4))
			settings.FFTResolution = resValues[curIdx];

		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.physical_glare.resolution_of_the_fft_convolution_higher_sharper_starburst", "Resolution of the FFT convolution. Higher = sharper starburst but more expensive."));
	}

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.padding_ratio", "Padding Ratio"), &settings.PaddingRatio, 0.f, 0.25f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			T("feature.post_processing.physical_glare.zero_padding_per_side_to_prevent_fft_wrap",
				"Zero-padding per side to prevent FFT wrap-around.\n"
				"0.25 = paper default (50%% effective resolution).\n"
				"0.1  = 80%% effective (recommended for high-res).\n"
				"0.0  = 100%% (maximum sharpness, may wrap at edges).\n"
				"Lower = sharper glare on high-res screens."));

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.kernel_scale", "Kernel Scale"), &settings.KernelScale, 0.01f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			T("feature.post_processing.physical_glare.scale_of_the_glare_kernel_size_on_screen",
				"Scale of the glare kernel size on screen.\n"
				"1.0 = default. Smaller = more concentrated glare.\n"
				"Does not affect aperture physics."));

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.fresnel_exponent", "Fresnel Exponent"), &settings.FresnelExponent, 0.f, 80.f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.fresnel_phase_at_aperture_edge_radians_paper_eq", "Fresnel phase at aperture edge (radians). Paper eq 2.12: e^(i*pi/(lambda*z) * r^2).\nHigher = more Fresnel rings. 0 = pure Fraunhofer (no rings)."));

	ImGui::SliderFloat(T("feature.post_processing.physical_glare.chromatic_spread", "Chromatic Spread"), &settings.ChromaticSpread, 0.f, 3.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.physical_glare.multiplier_on_wavelength_dependent_uv_scaling_paper_section", "Multiplier on wavelength-dependent UV scaling (paper section 2.3: lambda/575nm).\n1.0 = physically correct. Higher = more rainbow spread. 0 = monochrome."));

	if (ImGui::CollapsingHeader(T("feature.post_processing.physical_glare.psf_shaping", "PSF Shaping"))) {
		ImGui::SliderFloat(T("feature.post_processing.physical_glare.psf_sharpness", "PSF Sharpness"), &settings.PSFSharpness, 0.2f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				T("feature.post_processing.physical_glare.dynamic_range_compression_exponent_paper_table_3_9",
					"Dynamic range compression exponent (paper Table 3.9: 0.45).\n"
					"Lower = wider/softer glare, higher = concentrated near light source.\n"
					"Increase if glare looks too blurry/spreads too far."));

		ImGui::SliderFloat(T("feature.post_processing.physical_glare.psf_noise_floor", "PSF Noise Floor"), &settings.PSFNoiseFloor, 0.f, 0.01f, "%.4f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				T("feature.post_processing.physical_glare.threshold_to_remove_low_level_fft_noise_from",
					"Threshold to remove low-level FFT noise from the PSF.\n"
					"Paper default: 0.001. Higher = cleaner glare wings."));
	}

	if (ImGui::CollapsingHeader(T("feature.post_processing.physical_glare.debug", "Debug"))) {
		if (texGlarePacked)
			ImGui::Image(texGlarePacked->srv.get(), { 256.f, 256.f });
	}
}

void PhysicalGlare::RestoreDefaultSettings()
{
	settings = {};
}

void PhysicalGlare::LoadSettings(json& o_json)
{
	settings = o_json;
}

void PhysicalGlare::SaveSettings(json& o_json)
{
	o_json = settings;
}

void PhysicalGlare::CreateFFTTextures(uint resolution)
{
	currentFFTResolution = resolution;
	psfDirty = true;
	apertureDirty = true;

	D3D11_TEXTURE2D_DESC texDesc = {
		.Width = resolution,
		.Height = resolution,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32G32_FLOAT,
		.SampleDesc = { .Count = 1, .Quality = 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
	};

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	// FFT ping-pong textures (RG32F) for 3 channels
	for (int ch = 0; ch < 3; ch++) {
		for (int pp = 0; pp < 2; pp++) {
			texFFT[ch][pp] = eastl::make_unique<Texture2D>(texDesc);
			texFFT[ch][pp]->CreateSRV(srvDesc);
			texFFT[ch][pp]->CreateUAV(uavDesc);
		}
	}

	// PSF FFT cache (RG32F) for 3 channels
	for (int ch = 0; ch < 3; ch++) {
		texPSF_FFT[ch] = eastl::make_unique<Texture2D>(texDesc);
		texPSF_FFT[ch]->CreateSRV(srvDesc);
		texPSF_FFT[ch]->CreateUAV(uavDesc);
	}

	texApertureBase = eastl::make_unique<Texture2D>(texDesc);
	texApertureBase->CreateSRV(srvDesc);
	texApertureBase->CreateUAV(uavDesc);

	// Pack the three R32F IFFT real components without precision loss so the
	// full-resolution upsample can filter RGB in one operation.
	D3D11_TEXTURE2D_DESC packedDesc = texDesc;
	packedDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srvDesc.Format = packedDesc.Format;
	uavDesc.Format = packedDesc.Format;

	texGlarePacked = eastl::make_unique<Texture2D>(packedDesc);
	texGlarePacked->CreateSRV(srvDesc);
	texGlarePacked->CreateUAV(uavDesc);
}

void PhysicalGlare::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("PhysicalGlare: Creating buffers...");
	{
		glareCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<GlareCB>());
	}

	logger::debug("PhysicalGlare: Creating FFT textures...");
	{
		currentFFTResolution = NormaliseFFTResolution(settings.FFTResolution);
		CreateFFTTextures(currentFFTResolution);
	}

	logger::debug("PhysicalGlare: Creating output texture...");
	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);
	}

	logger::debug("PhysicalGlare: Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));

		D3D11_SAMPLER_DESC wrapSamplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
			.AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
			.AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&wrapSamplerDesc, wrapSampler.put()));
	}

	CompileComputeShaders();
}

void PhysicalGlare::ClearShaderCache()
{
	BumpShaderGeneration();
	auto const shaderPtrs = std::array{
		&thresholdCS, &apertureCS, &tearFilmCS, &psfCS, &multiplyCS, &packCS, &compositeCS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}

		for (auto shaders : { &fftRowCS, &fftColCS, &fftRowInvCS, &fftColInvCS }) {
			for (auto& shader : *shaders) {
				if (shader) {
					shader->Release();
					shader.detach();
				}
			}
		}
	}
	psfDirty = true;
	apertureDirty = true;

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/PhysicalGlare");
	CompileComputeShaders();
}

void PhysicalGlare::CompileComputeShaders()
{
	std::vector<ComputeShaderCompileInfo> shaderInfos = {
		{ &thresholdCS, "threshold.cs.hlsl", {}, "CS_Threshold" },
		{ &apertureCS, "aperture.cs.hlsl", {}, "CS_Aperture" },
		{ &tearFilmCS, "tearfilm.cs.hlsl", {}, "CS_TearFilm" },
		{ &psfCS, "psf.cs.hlsl", {}, "CS_ChromaticBlur" },
		{ &multiplyCS, "multiply.cs.hlsl", {}, "CS_Multiply" },
		{ &packCS, "pack.cs.hlsl", {}, "CS_Pack" },
		{ &compositeCS, "composite.cs.hlsl", {}, "CS_Composite" },
	};

	static constexpr std::array<const char*, FFT_VARIANT_COUNT> fftSizes = { "128", "256", "512", "1024" };
	for (uint i = 0; i < FFT_VARIANT_COUNT; ++i) {
		shaderInfos.push_back({ &fftRowCS[i], "fft.cs.hlsl", { { "ROW_PASS", "" }, { "FORWARD", "" }, { "FFT_SIZE", fftSizes[i] } }, "CS_FFT" });
		shaderInfos.push_back({ &fftColCS[i], "fft.cs.hlsl", { { "COL_PASS", "" }, { "FORWARD", "" }, { "FFT_SIZE", fftSizes[i] } }, "CS_FFT" });
		shaderInfos.push_back({ &fftRowInvCS[i], "fft.cs.hlsl", { { "ROW_PASS", "" }, { "INVERSE", "" }, { "FFT_SIZE", fftSizes[i] } }, "CS_FFT" });
		shaderInfos.push_back({ &fftColInvCS[i], "fft.cs.hlsl", { { "COL_PASS", "" }, { "INVERSE", "" }, { "FFT_SIZE", fftSizes[i] } }, "CS_FFT" });
	}

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\PhysicalGlare", shaderInfos);
}

bool PhysicalGlare::NeedsPSFRegeneration() const
{
	const bool pupilMode = settings.ApertureMode == 1;
	const bool lensMode = !pupilMode;

	return psfDirty ||
	       cachedPSFParams.ApertureMode != settings.ApertureMode ||
	       (lensMode && cachedPSFParams.ApertureBlades != settings.ApertureBlades) ||
	       (lensMode && cachedPSFParams.ApertureRotation != settings.ApertureRotation) ||
	       (pupilMode && cachedPSFParams.ScatterStrength != settings.ScatterStrength) ||
	       cachedPSFParams.FFTResolution != static_cast<int>(currentFFTResolution) ||
	       (pupilMode && cachedPSFParams.EnableEyelashes != settings.EnableEyelashes) ||
	       (pupilMode && settings.EnableEyelashes && cachedPSFParams.EyelashCount != settings.EyelashCount) ||
	       (pupilMode && settings.EnableEyelashes && cachedPSFParams.EyelashLength != settings.EyelashLength) ||
	       (pupilMode && settings.EnableEyelashes && cachedPSFParams.EyelashCurvature != settings.EyelashCurvature) ||
	       cachedPSFParams.FresnelExponent != settings.FresnelExponent ||
	       cachedPSFParams.ChromaticSpread != settings.ChromaticSpread ||
	       cachedPSFParams.FStop != settings.FStop ||
	       cachedPSFParams.PSFSharpness != settings.PSFSharpness ||
	       cachedPSFParams.PSFNoiseFloor != settings.PSFNoiseFloor ||
	       (pupilMode && cachedPSFParams.ParticleCount != settings.ParticleCount) ||
	       (pupilMode && cachedPSFParams.ParticleSize != settings.ParticleSize) ||
	       (pupilMode && cachedPSFParams.GratingCount != settings.GratingCount) ||
	       (pupilMode && cachedPSFParams.GratingStrength != settings.GratingStrength) ||
	       (pupilMode && cachedPSFParams.TearFilmStrength != settings.TearFilmStrength) ||
	       (pupilMode && settings.TearFilmStrength > 0.f && cachedPSFParams.TearFilmSpeed != settings.TearFilmSpeed) ||
	       (pupilMode && settings.TearFilmStrength > 0.f && cachedPSFParams.TearFilmComplexity != settings.TearFilmComplexity) ||
	       (pupilMode && cachedPSFParams.SutureBranches != settings.SutureBranches) ||
	       (pupilMode && settings.SutureBranches > 0 && cachedPSFParams.SutureStrength != settings.SutureStrength) ||
	       (pupilMode && settings.SutureBranches > 0 && cachedPSFParams.SutureWidth != settings.SutureWidth) ||
	       (pupilMode && cachedPSFParams.StarburstCount != settings.StarburstCount) ||
	       (pupilMode && settings.StarburstCount > 0 && cachedPSFParams.StarburstStrength != settings.StarburstStrength) ||
	       (pupilMode && settings.StarburstCount > 0 && cachedPSFParams.StarburstIrregularity != settings.StarburstIrregularity) ||
	       (lensMode && cachedPSFParams.DustCount != settings.DustCount) ||
	       (lensMode && settings.DustCount > 0 && cachedPSFParams.DustSize != settings.DustSize) ||
	       (lensMode && cachedPSFParams.BladeRoughnessAmp != settings.BladeRoughnessAmp) ||
	       (lensMode && settings.BladeRoughnessAmp > 0.f && cachedPSFParams.BladeRoughnessFreq != settings.BladeRoughnessFreq) ||
	       (lensMode && cachedPSFParams.ScratchCount != settings.ScratchCount) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchOpacity != settings.ScratchOpacity) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchLength != settings.ScratchLength) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchWidth != settings.ScratchWidth) ||
	       cachedPSFParams.SphericalAberration != settings.SphericalAberration ||
	       cachedPSFParams.KernelScale != settings.KernelScale ||
	       cachedPSFParams.UseAP1 != (globals::features::linearLighting.settings.enableACEScg && globals::features::linearLighting.settings.enableLinearLighting) ||
	       (pupilMode && settings.TearFilmStrength > 0.f);  // animated tear film changes the PSF every frame
}

bool PhysicalGlare::NeedsApertureRegeneration() const
{
	const bool pupilMode = settings.ApertureMode == 1;
	const bool lensMode = !pupilMode;

	return apertureDirty ||
	       cachedPSFParams.ApertureMode != settings.ApertureMode ||
	       (lensMode && cachedPSFParams.ApertureBlades != settings.ApertureBlades) ||
	       (lensMode && cachedPSFParams.ApertureRotation != settings.ApertureRotation) ||
	       (pupilMode && cachedPSFParams.ScatterStrength != settings.ScatterStrength) ||
	       cachedPSFParams.FFTResolution != static_cast<int>(currentFFTResolution) ||
	       cachedPSFParams.FresnelExponent != settings.FresnelExponent ||
	       cachedPSFParams.FStop != settings.FStop ||
	       (pupilMode && cachedPSFParams.EnableEyelashes != settings.EnableEyelashes) ||
	       (pupilMode && settings.EnableEyelashes && cachedPSFParams.EyelashCount != settings.EyelashCount) ||
	       (pupilMode && settings.EnableEyelashes && cachedPSFParams.EyelashLength != settings.EyelashLength) ||
	       (pupilMode && cachedPSFParams.ParticleCount != settings.ParticleCount) ||
	       (pupilMode && cachedPSFParams.ParticleSize != settings.ParticleSize) ||
	       (pupilMode && cachedPSFParams.GratingCount != settings.GratingCount) ||
	       (pupilMode && cachedPSFParams.GratingStrength != settings.GratingStrength) ||
	       (pupilMode && cachedPSFParams.SutureBranches != settings.SutureBranches) ||
	       (pupilMode && settings.SutureBranches > 0 && cachedPSFParams.SutureStrength != settings.SutureStrength) ||
	       (pupilMode && settings.SutureBranches > 0 && cachedPSFParams.SutureWidth != settings.SutureWidth) ||
	       (pupilMode && cachedPSFParams.StarburstCount != settings.StarburstCount) ||
	       (pupilMode && settings.StarburstCount > 0 && cachedPSFParams.StarburstStrength != settings.StarburstStrength) ||
	       (pupilMode && settings.StarburstCount > 0 && cachedPSFParams.StarburstIrregularity != settings.StarburstIrregularity) ||
	       (lensMode && cachedPSFParams.DustCount != settings.DustCount) ||
	       (lensMode && settings.DustCount > 0 && cachedPSFParams.DustSize != settings.DustSize) ||
	       (lensMode && cachedPSFParams.BladeRoughnessAmp != settings.BladeRoughnessAmp) ||
	       (lensMode && settings.BladeRoughnessAmp > 0.f && cachedPSFParams.BladeRoughnessFreq != settings.BladeRoughnessFreq) ||
	       (lensMode && cachedPSFParams.ScratchCount != settings.ScratchCount) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchOpacity != settings.ScratchOpacity) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchLength != settings.ScratchLength) ||
	       (lensMode && settings.ScratchCount > 0 && cachedPSFParams.ScratchWidth != settings.ScratchWidth) ||
	       cachedPSFParams.SphericalAberration != settings.SphericalAberration;
}

void PhysicalGlare::GeneratePSF()
{
	auto context = globals::d3d::context;

	// Build the CB data for PSF generation
	GlareCB cbData = {
		.Threshold = exp2(settings.ThresholdEV - 3.0f),
		.Intensity = settings.Intensity,
		.ScatterStrength = settings.ScatterStrength,
		.ApertureMode = (uint)settings.ApertureMode,
		.ApertureBlades = settings.ApertureBlades,
		.ApertureRotation = settings.ApertureRotation * 3.14159265f / 180.f,
		.AdaptSpeed = settings.AdaptSpeed,
		.DeltaTime = 0.f,
		.FFTResolution = currentFFTResolution,
		.PaddingRatio = settings.PaddingRatio,
		.ScreenWidth = texOutput ? (float)texOutput->desc.Width : 1920.f,
		.ScreenHeight = texOutput ? (float)texOutput->desc.Height : 1080.f,
		.ChannelIndex = 0,
		.FresnelExponent = settings.FresnelExponent,
		.ChromaticSpread = settings.ChromaticSpread,
		.ApertureSize = 1.0f / std::max(settings.FStop, 1.0f),
		.PSFSharpness = settings.PSFSharpness,
		.PSFNoiseFloor = settings.PSFNoiseFloor,
		.EnableEyelashes = settings.EnableEyelashes ? 1u : 0u,
		.EyelashCurvature = settings.EyelashCurvature,
		// Eye mode
		.EyelashCount = (uint)settings.EyelashCount,
		.EyelashLength = settings.EyelashLength,
		.ParticleCount = (uint)settings.ParticleCount,
		.ParticleSize = settings.ParticleSize,
		.GratingCount = (uint)settings.GratingCount,
		.GratingStrength = settings.GratingStrength,
		.TearFilmStrength = settings.TearFilmStrength,
		.TearFilmSpeed = settings.TearFilmSpeed,
		.TearFilmComplexity = (uint)settings.TearFilmComplexity,
		.TearFilmTime = tearFilmTimeAccum,
		.SutureBranches = (uint)settings.SutureBranches,
		.SutureStrength = settings.SutureStrength,
		.SutureWidth = settings.SutureWidth,
		.StarburstCount = (uint)settings.StarburstCount,
		.StarburstStrength = settings.StarburstStrength,
		.StarburstIrregularity = settings.StarburstIrregularity,
		// Lens mode
		.DustCount = (uint)settings.DustCount,
		.DustSize = settings.DustSize,
		.BladeRoughnessFreq = (uint)settings.BladeRoughnessFreq,
		.BladeRoughnessAmp = settings.BladeRoughnessAmp,
		.ScratchCount = (uint)settings.ScratchCount,
		.ScratchOpacity = settings.ScratchOpacity,
		.ScratchLength = settings.ScratchLength,
		.ScratchWidth = settings.ScratchWidth,
		.SphericalAberration = settings.SphericalAberration,
		.UseAP1 = (globals::features::linearLighting.settings.enableACEScg && globals::features::linearLighting.settings.enableLinearLighting) ? 1u : 0u,
		.KernelScale = settings.KernelScale,
	};

	glareCB->Update(cbData);
	ID3D11Buffer* cb = glareCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	// ===== Step 1: Cache the static aperture response =====
	if (NeedsApertureRegeneration()) {
		// Tear film is applied separately below; all expensive static geometry
		// and scatter masks remain cached across animated frames.
		GlareCB apertureCBData = cbData;
		apertureCBData.TearFilmStrength = 0.f;
		glareCB->Update(apertureCBData);
		cb = glareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		ID3D11UnorderedAccessView* uav = texApertureBase->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(apertureCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		apertureDirty = false;
	}

	glareCB->Update(cbData);
	cb = glareCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	const uint fftVariant = GetFFTVariant(currentFFTResolution);
	Texture2D* diffraction = nullptr;
	if (settings.ApertureMode == 1 && settings.TearFilmStrength > 0.f) {
		ID3D11ShaderResourceView* srv = texApertureBase->srv.get();
		ID3D11UnorderedAccessView* uav = texFFT[0][0]->uav.get();
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(tearFilmCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srv = nullptr;
		uav = nullptr;
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		DispatchFFT(fftRowCS[fftVariant].get(), texFFT[0][0].get(), texFFT[0][1].get(), currentFFTResolution);
		DispatchFFT(fftColCS[fftVariant].get(), texFFT[0][1].get(), texFFT[0][0].get(), currentFFTResolution);
		diffraction = texFFT[0][0].get();
	} else {
		context->CopyResource(texFFT[0][0]->resource.get(), texApertureBase->resource.get());
		DispatchFFT(fftRowCS[fftVariant].get(), texFFT[0][0].get(), texFFT[0][1].get(), currentFFTResolution);
		DispatchFFT(fftColCS[fftVariant].get(), texFFT[0][1].get(), texFFT[0][0].get(), currentFFTResolution);
		diffraction = texFFT[0][0].get();
	}

	// ===== Step 2: Chromatic blur for all RGB channels =====
	// Reads the cached/current diffraction amplitude and writes texFFT[ch][1].
	// Computes |F|² at wavelength-dependent UV scales with CIE spectral weighting
	{
		ID3D11SamplerState* sampler = wrapSampler.get();
		context->CSSetSamplers(0, 1, &sampler);

		ID3D11ShaderResourceView* srv = diffraction->srv.get();
		std::array<ID3D11UnorderedAccessView*, 3> uavs = {
			texFFT[0][1]->uav.get(),
			texFFT[1][1]->uav.get(),
			texFFT[2][1]->uav.get(),
		};

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(psfCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srv = nullptr;
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		sampler = nullptr;
		context->CSSetSamplers(0, 1, &sampler);
	}

	// ===== Step 3: FFT each channel's PSF for frequency-domain storage =====
	// texFFT[ch][1] -> row FFT -> texFFT[ch][0] -> col FFT -> texPSF_FFT[ch]
	for (int ch = 0; ch < 3; ch++) {
		DispatchFFT(fftRowCS[fftVariant].get(), texFFT[ch][1].get(), texFFT[ch][0].get(), currentFFTResolution);
		DispatchFFT(fftColCS[fftVariant].get(), texFFT[ch][0].get(), texPSF_FFT[ch].get(), currentFFTResolution);
	}

	// Cache parameters
	cachedPSFParams.ApertureMode = settings.ApertureMode;
	cachedPSFParams.ApertureBlades = settings.ApertureBlades;
	cachedPSFParams.ApertureRotation = settings.ApertureRotation;
	cachedPSFParams.ScatterStrength = settings.ScatterStrength;
	cachedPSFParams.FFTResolution = static_cast<int>(currentFFTResolution);
	cachedPSFParams.EnableEyelashes = settings.EnableEyelashes;
	cachedPSFParams.EyelashCount = settings.EyelashCount;
	cachedPSFParams.EyelashLength = settings.EyelashLength;
	cachedPSFParams.EyelashCurvature = settings.EyelashCurvature;
	cachedPSFParams.FresnelExponent = settings.FresnelExponent;
	cachedPSFParams.ChromaticSpread = settings.ChromaticSpread;
	cachedPSFParams.FStop = settings.FStop;
	cachedPSFParams.ParticleCount = settings.ParticleCount;
	cachedPSFParams.ParticleSize = settings.ParticleSize;
	cachedPSFParams.GratingCount = settings.GratingCount;
	cachedPSFParams.GratingStrength = settings.GratingStrength;
	cachedPSFParams.TearFilmStrength = settings.TearFilmStrength;
	cachedPSFParams.TearFilmSpeed = settings.TearFilmSpeed;
	cachedPSFParams.TearFilmComplexity = settings.TearFilmComplexity;
	cachedPSFParams.SutureBranches = settings.SutureBranches;
	cachedPSFParams.SutureStrength = settings.SutureStrength;
	cachedPSFParams.SutureWidth = settings.SutureWidth;
	cachedPSFParams.StarburstCount = settings.StarburstCount;
	cachedPSFParams.StarburstStrength = settings.StarburstStrength;
	cachedPSFParams.StarburstIrregularity = settings.StarburstIrregularity;
	cachedPSFParams.DustCount = settings.DustCount;
	cachedPSFParams.DustSize = settings.DustSize;
	cachedPSFParams.PSFSharpness = settings.PSFSharpness;
	cachedPSFParams.PSFNoiseFloor = settings.PSFNoiseFloor;
	cachedPSFParams.BladeRoughnessAmp = settings.BladeRoughnessAmp;
	cachedPSFParams.BladeRoughnessFreq = settings.BladeRoughnessFreq;
	cachedPSFParams.ScratchCount = settings.ScratchCount;
	cachedPSFParams.ScratchOpacity = settings.ScratchOpacity;
	cachedPSFParams.ScratchLength = settings.ScratchLength;
	cachedPSFParams.ScratchWidth = settings.ScratchWidth;
	cachedPSFParams.SphericalAberration = settings.SphericalAberration;
	cachedPSFParams.KernelScale = settings.KernelScale;
	cachedPSFParams.UseAP1 = globals::features::linearLighting.settings.enableACEScg && globals::features::linearLighting.settings.enableLinearLighting;
	psfDirty = false;
}

void PhysicalGlare::DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution)
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* srv = input->srv.get();
	ID3D11UnorderedAccessView* uav = output->uav.get();

	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(shader, nullptr, 0);
	context->Dispatch(resolution, 1, 1);

	srv = nullptr;
	uav = nullptr;
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
}

void PhysicalGlare::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	if (!AllShadersReady({
			&thresholdCS,
			&apertureCS,
			&tearFilmCS,
			&psfCS,
			&multiplyCS,
			&packCS,
			&compositeCS,
			&fftRowCS[0],
			&fftRowCS[1],
			&fftRowCS[2],
			&fftRowCS[3],
			&fftColCS[0],
			&fftColCS[1],
			&fftColCS[2],
			&fftColCS[3],
			&fftRowInvCS[0],
			&fftRowInvCS[1],
			&fftRowInvCS[2],
			&fftRowInvCS[3],
			&fftColInvCS[0],
			&fftColInvCS[1],
			&fftColInvCS[2],
			&fftColInvCS[3],
		}))
		return;

	state->BeginPerfEvent("Physical Glare");

	// Handle FFT resolution change
	uint targetRes = NormaliseFFTResolution(settings.FFTResolution);
	if (targetRes != currentFFTResolution) {
		CreateFFTTextures(targetRes);
	}

	// Accumulate tear film time
	if (settings.ApertureMode == 1 && settings.TearFilmStrength > 0.f) {
		tearFilmTimeAccum += *globals::game::deltaTime;
	}

	// Update constant buffer
	GlareCB cbData = {
		.Threshold = exp2(settings.ThresholdEV - 3.0f),
		.Intensity = settings.Intensity,
		.ScatterStrength = settings.ScatterStrength,
		.ApertureMode = (uint)settings.ApertureMode,
		.ApertureBlades = settings.ApertureBlades,
		.ApertureRotation = settings.ApertureRotation * 3.14159265f / 180.f,
		.AdaptSpeed = settings.AdaptSpeed,
		.DeltaTime = *globals::game::deltaTime,
		.FFTResolution = currentFFTResolution,
		.PaddingRatio = settings.PaddingRatio,
		.ScreenWidth = (float)texOutput->desc.Width,
		.ScreenHeight = (float)texOutput->desc.Height,
		.ChannelIndex = 0,
		.FresnelExponent = settings.FresnelExponent,
		.ChromaticSpread = settings.ChromaticSpread,
		.ApertureSize = 1.0f / std::max(settings.FStop, 1.0f),
		.PSFSharpness = settings.PSFSharpness,
		.PSFNoiseFloor = settings.PSFNoiseFloor,
		.EnableEyelashes = settings.EnableEyelashes ? 1u : 0u,
		.EyelashCurvature = settings.EyelashCurvature,
		// Eye mode
		.EyelashCount = (uint)settings.EyelashCount,
		.EyelashLength = settings.EyelashLength,
		.ParticleCount = (uint)settings.ParticleCount,
		.ParticleSize = settings.ParticleSize,
		.GratingCount = (uint)settings.GratingCount,
		.GratingStrength = settings.GratingStrength,
		.TearFilmStrength = settings.TearFilmStrength,
		.TearFilmSpeed = settings.TearFilmSpeed,
		.TearFilmComplexity = (uint)settings.TearFilmComplexity,
		.TearFilmTime = tearFilmTimeAccum,
		.SutureBranches = (uint)settings.SutureBranches,
		.SutureStrength = settings.SutureStrength,
		.SutureWidth = settings.SutureWidth,
		.StarburstCount = (uint)settings.StarburstCount,
		.StarburstStrength = settings.StarburstStrength,
		.StarburstIrregularity = settings.StarburstIrregularity,
		// Lens mode
		.DustCount = (uint)settings.DustCount,
		.DustSize = settings.DustSize,
		.BladeRoughnessFreq = (uint)settings.BladeRoughnessFreq,
		.BladeRoughnessAmp = settings.BladeRoughnessAmp,
		.ScratchCount = (uint)settings.ScratchCount,
		.ScratchOpacity = settings.ScratchOpacity,
		.ScratchLength = settings.ScratchLength,
		.ScratchWidth = settings.ScratchWidth,
		.SphericalAberration = settings.SphericalAberration,
		.UseAP1 = (globals::features::linearLighting.settings.enableACEScg && globals::features::linearLighting.settings.enableLinearLighting) ? 1u : 0u,
		.KernelScale = settings.KernelScale,
	};
	glareCB->Update(cbData);

	ID3D11Buffer* cb = glareCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	// ========== Step 1: Regenerate PSF if parameters changed ==========
	if (NeedsPSFRegeneration()) {
		globals::profiler->BeginPass("PostProcessing::PhysicalGlare::PSF");
		GeneratePSF();

		// Re-update CB because GeneratePSF() overwrites it with DeltaTime=0
		glareCB->Update(cbData);
		cb = glareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);
		globals::profiler->EndPass();
	}

	// ========== Step 2: Threshold + downsample scene into FFT textures ==========
	globals::profiler->BeginPass("PostProcessing::PhysicalGlare::FFT");
	{
		// We write R, G, B channels into texFFT[0..2][0]
		ID3D11ShaderResourceView* srv = inout_tex.srv;
		std::array<ID3D11UnorderedAccessView*, 3> uavs = {
			texFFT[0][0]->uav.get(),
			texFFT[1][0]->uav.get(),
			texFFT[2][0]->uav.get()
		};

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(thresholdCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srv = nullptr;
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	}

	// ========== Step 3: Forward FFT on scene (per channel) ==========
	const uint fftVariant = GetFFTVariant(currentFFTResolution);
	for (int ch = 0; ch < 3; ch++) {
		// Row FFT: texFFT[ch][0] -> texFFT[ch][1]
		DispatchFFT(fftRowCS[fftVariant].get(), texFFT[ch][0].get(), texFFT[ch][1].get(), currentFFTResolution);
		// Col FFT: texFFT[ch][1] -> texFFT[ch][0]
		DispatchFFT(fftColCS[fftVariant].get(), texFFT[ch][1].get(), texFFT[ch][0].get(), currentFFTResolution);
	}

	// ========== Step 4: Frequency-domain multiply (scene * PSF) ==========
	{
		std::array<ID3D11ShaderResourceView*, 6> srvs = {
			texFFT[0][0]->srv.get(),
			texPSF_FFT[0]->srv.get(),
			texFFT[1][0]->srv.get(),
			texPSF_FFT[1]->srv.get(),
			texFFT[2][0]->srv.get(),
			texPSF_FFT[2]->srv.get(),
		};
		std::array<ID3D11UnorderedAccessView*, 3> uavs = {
			texFFT[0][1]->uav.get(), texFFT[1][1]->uav.get(), texFFT[2][1]->uav.get()
		};

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(multiplyCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	}

	// ========== Step 5: Inverse FFT (per channel) ==========
	for (int ch = 0; ch < 3; ch++) {
		// Row IFFT: texFFT[ch][1] -> texFFT[ch][0]
		DispatchFFT(fftRowInvCS[fftVariant].get(), texFFT[ch][1].get(), texFFT[ch][0].get(), currentFFTResolution);
		// Col IFFT: texFFT[ch][0] -> texFFT[ch][1]
		DispatchFFT(fftColInvCS[fftVariant].get(), texFFT[ch][0].get(), texFFT[ch][1].get(), currentFFTResolution);
	}
	globals::profiler->EndPass();

	// Pack the three real components into one RGBA32F texture. Filtering this
	// texture is channel-wise identical to filtering the three R32F sources.
	{
		std::array<ID3D11ShaderResourceView*, 3> srvs = {
			texFFT[0][1]->srv.get(),
			texFFT[1][1]->srv.get(),
			texFFT[2][1]->srv.get(),
		};
		ID3D11UnorderedAccessView* uav = texGlarePacked->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(packCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srvs.fill(nullptr);
		uav = nullptr;
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	}

	// ========== Step 6: Composite (upsample + add to scene) ==========
	globals::profiler->BeginPass("PostProcessing::PhysicalGlare::Composite");
	{
		// t0 = scene, t1 = packed RGB IFFT result,
		// u0 = output
		std::array<ID3D11ShaderResourceView*, 2> srvs = {
			inout_tex.srv,
			texGlarePacked->srv.get(),
		};
		std::array<ID3D11UnorderedAccessView*, 1> uavs = {
			texOutput->uav.get(),
		};
		ID3D11SamplerState* sampler = linearSampler.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(compositeCS.get(), nullptr, 0);

		context->Dispatch(((uint)texOutput->desc.Width + 7) >> 3, ((uint)texOutput->desc.Height + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		sampler = nullptr;
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetSamplers(0, 1, &sampler);
	}

	// Cleanup
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();

	state->EndPerfEvent();
}

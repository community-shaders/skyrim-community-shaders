#include "PhysicalLens.h"
#include "PhysicalLensBindings.h"

#include "Features/PostProcessing.h"
#include "RasterPass.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalLens::Settings,
	LensPreset, MaxSources, Intensity, ThresholdEV, FNumber, ApertureBlades, ApertureRotation, Coating, FocusDistanceM, ZoomPosition, ImageEffects, Diffraction, StarburstSize, StarburstIntensity, DirtOpacity, DirtDensity, DirtSize, ScratchLength, ScratchSpread, SurfaceScatter, GhostDiffraction, AlignmentVariation)

using PhysicalLensInternal::OpticalBindings;

void PhysicalLens::SanitizeSettings()
{
	settings.LensPreset = std::clamp(settings.LensPreset, 0, int(LensPresetCount) - 1);
	settings.MaxSources = std::clamp(settings.MaxSources, 1, (int)SourceCount);
	if (settings.ApertureBlades != 0)
		settings.ApertureBlades = std::clamp(settings.ApertureBlades, 3, 16);
	const auto finite = [](float v, float fallback, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
	settings.Intensity = finite(settings.Intensity, 1.f, 0.f, 64.f);
	settings.ThresholdEV = finite(settings.ThresholdEV, 3.f, -7.f, 23.f);
	settings.FNumber = finite(settings.FNumber, 5.6f, (settings.LensPreset == 5 ? 2.91f : PhysicalLensOptics::GetPrescription(settings.LensPreset).minimumFNumber), 22.f);
	settings.ApertureRotation = finite(settings.ApertureRotation, 0.f, -180.f, 180.f);
	settings.Coating = finite(settings.Coating, 1.f, 0.f, 1.f);
	settings.StarburstSize = finite(settings.StarburstSize, 10.f, .1f, 32.f);
	settings.StarburstIntensity = finite(settings.StarburstIntensity, .02f, 0.f, 64.f);
	settings.DirtOpacity = finite(settings.DirtOpacity, 1.f, 0.f, 1.f);
	settings.DirtDensity = finite(settings.DirtDensity, 1.f, 0.f, 2.f);
	settings.DirtSize = finite(settings.DirtSize, 1.f, .25f, 3.f);
	settings.ScratchLength = finite(settings.ScratchLength, 1.f, .25f, 2.f);
	settings.ScratchSpread = finite(settings.ScratchSpread, 180.f, 0.f, 180.f);
	settings.SurfaceScatter = finite(settings.SurfaceScatter, .25f, 0.f, 1.f);
	settings.GhostDiffraction = finite(settings.GhostDiffraction, .6f, 0.f, 1.f);
	settings.AlignmentVariation = finite(settings.AlignmentVariation, .2f, 0.f, 1.f);
	settings.ZoomPosition = finite(settings.ZoomPosition, 0.f, 0.f, 1.f);
	constexpr float minimumFocus[] = { .5f, .4f, .85f, .7f, .5f, .5f };
	settings.FocusDistanceM = settings.FocusDistanceM == 0 ? 0 : finite(settings.FocusDistanceM, 0.f, minimumFocus[settings.LensPreset], 1000.f);
}

void PhysicalLens::LoadSettings(json& value)
{
	settings = value;
	SanitizeSettings();
}

void PhysicalLens::SaveSettings(json& value) { value = settings; }

PhysicalLens::FocalLengthRange PhysicalLens::GetPresetFocalLengthRange(int preset)
{
	switch (preset) {
	case 2:
		return { false, 85.0f, 85.0f };
	case 3:
		return { false, 35.0f, 35.0f };
	case 5:
		return { true, 24.0f, 70.0f };
	case 0:
	case 1:
	case 4:
	default:
		return { false, 50.0f, 50.0f };
	}
}

void PhysicalLens::DrawLensPresetCombo()
{
	const char* presets[] = {
		T("feature.post_processing.physical_lens.master_prime", "ARRI / Zeiss Master Prime 50 mm T1.3"),
		T("feature.post_processing.physical_lens.sigma_art", "Sigma 50 mm F1.4 DG HSM Art"),
		T("feature.post_processing.physical_lens.canon_rf85", "Canon RF 85 mm F1.2 L USM"),
		T("feature.post_processing.physical_lens.summilux35", "Leica Summilux-M 35 mm F1.4 Aspherical"),
		T("feature.post_processing.physical_lens.planar50", "Sony / Zeiss Planar T* FE 50 mm F1.4 ZA"),
		T("feature.post_processing.physical_lens.canon_zoom", "Canon RF 24-70 mm F2.8")
	};
	static_assert(std::size(presets) == LensPresetCount);
	ImGui::Combo(T("feature.post_processing.physical_lens.lens", "Lens Preset"), &settings.LensPreset, presets, (int)LensPresetCount);
}

void PhysicalLens::DrawSettings()
{
	const auto* camera = owner ? owner->GetActivePhysicalCameraState() : nullptr;
	if (camera) {
		ImGui::TextDisabled("%s", T("feature.post_processing.controlled_by_cinematic_camera", "Lens and focus are currently controlled by Cinematic Camera."));
		ImGui::Text(T("feature.post_processing.physical_lens.camera_readout", "%.1f mm | f/%.2f | Gate %.2f mm | %d blades"),
			camera->FocalLengthMM, camera->FNumber, camera->EffectiveSensorWidthMM, camera->ApertureBladeCount);
		ImGui::Text(T("feature.post_processing.physical_lens.camera_focus", "Resolved focus: %.2f m"), resolvedFocusM);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T("feature.post_processing.physical_lens.camera_focus_hint", "Uses the depth-of-field focus result when available, including focus transitions. Otherwise the camera focus point is sampled independently. Lens focus follows by a few frames."));
		ImGui::TextWrapped("%s", T("feature.post_processing.physical_lens.equivalent_zoom", "Focal lengths outside the lens preset's range use equivalent zoom. The camera controls framing, aperture and filmback."));
		if (!configurationValid)
			ImGui::TextWrapped("%s", T("feature.post_processing.physical_lens.focus_unavailable", "This lens cannot focus at the current distance. Increase the focus distance or reduce the focal length."));
	}
	DrawLensPresetCombo();
	ImGui::Checkbox(T("feature.post_processing.physical_lens.image_effects", "Lens Distortion, Vignetting and Color"), &settings.ImageEffects);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.image_effects_hint", "Adds lens distortion, darkened corners and color fringing. Scene framing stays fixed; depth of field is unchanged."));
	ImGui::Checkbox(T("feature.post_processing.physical_lens.diffraction", "Diffraction Starburst"), &settings.Diffraction);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.diffraction_hint", "Adds starbursts around bright lights. Aperture shape controls the rays; higher F-Numbers make the pattern larger and fainter."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.starburst_size", "Starburst Size"), &settings.StarburstSize, .1f, 32.f, "%.2f", ImGuiSliderFlags_Logarithmic);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.starburst_size_hint", "Controls how far the starburst extends. Larger values spread the same light over a wider area."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.starburst_intensity", "Starburst Intensity"), &settings.StarburstIntensity, 0.f, 64.f, "%.3f", ImGuiSliderFlags_Logarithmic);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.starburst_intensity_hint", "Controls starburst brightness independently of ghosts. Set to zero to remove the starburst."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.dirt_opacity", "Dirt Strength"), &settings.DirtOpacity, 0.f, 1.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.dirt_opacity_hint", "Makes dust and scratch diffraction more visible. Zero produces a clean starburst."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.dirt_density", "Dirt Density"), &settings.DirtDensity, 0.f, 2.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.dirt_density_hint", "Higher values add more dust and scratches, creating denser fine rays and colored speckles."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.dirt_size", "Particle Size"), &settings.DirtSize, .25f, 3.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.dirt_size_hint", "Changes dust particle size and scratch thickness, altering the spread and texture of the scattered light."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.scratch_length", "Scratch Length"), &settings.ScratchLength, .25f, 2.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.scratch_length_hint", "Longer scratches produce narrower streaks; shorter scratches spread their light more broadly."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.scratch_spread", "Scratch Orientation Spread"), &settings.ScratchSpread, 0.f, 180.f, "%.0f deg");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.scratch_spread_hint", "Low values group scratch streaks around a shared direction. 180 degrees spreads them in all directions. Aperture Rotation turns the shared direction."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.surface_scatter", "Ghost Texture Strength"), &settings.SurfaceScatter, 0.f, 1.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.surface_scatter_hint", "Adds irregular fine detail inside ghosts. Higher values make the texture more visible."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.ghost_diffraction", "Ghost Edge Diffraction"), &settings.GhostDiffraction, 0.f, 1.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.ghost_diffraction_hint", "Adds colored fringes and fine ripples to ghost edges. Larger light sources produce smoother patterns. Zero gives clean geometric edges."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.alignment", "Ghost Asymmetry"), &settings.AlignmentVariation, 0.f, 1.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.alignment_hint", "Offsets individual ghosts for a less symmetrical arrangement. Zero restores a centered arrangement."));
	if (!camera) {
		bool infiniteFocus = settings.FocusDistanceM == 0;
		if (ImGui::Checkbox(T("feature.post_processing.physical_lens.infinity", "Focus at Infinity"), &infiniteFocus))
			settings.FocusDistanceM = infiniteFocus ? 0.f : 10.f;
		if (!infiniteFocus) {
			constexpr float minimumFocus[] = { .5f, .4f, .85f, .7f, .5f, .5f };
			ImGui::SliderFloat(T("feature.post_processing.physical_lens.focus", "Lens Focus Distance"), &settings.FocusDistanceM, minimumFocus[settings.LensPreset], 1000.f, "%.2f m", ImGuiSliderFlags_Logarithmic);
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.physical_lens.focus_hint", "Changes ghost size and shape for the chosen focus distance. Scene sharpness stays unchanged."));
		}
		if (settings.LensPreset == 5) {
			ImGui::SliderFloat(T("feature.post_processing.physical_lens.zoom", "Zoom Position (Wide to Tele)"), &settings.ZoomPosition, 0.f, 1.f, "%.3f");
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.physical_lens.zoom_hint", "Changes the lens flare appearance from wide-angle to telephoto. Scene framing stays fixed."));
		}
	}
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.intensity", "Ghost Intensity"), &settings.Intensity, 0.f, 64.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.intensity_hint", "Controls ghost brightness independently of the starburst."));
	ImGui::SliderInt(T("feature.post_processing.physical_lens.max_sources", "Maximum Highlight Sources"), &settings.MaxSources, 1, (int)SourceCount);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.sources_hint", "Limits how many highlights can produce lens effects at once. Higher values may increase rendering cost."));
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.threshold", "Threshold (EV above middle gray)"), &settings.ThresholdEV, -7.f, 23.f, "%.2f EV");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.threshold_hint", "Minimum highlight brightness relative to scene exposure. Raise it to restrict the effect to brighter lights; lower it to include more highlights."));
	if (!camera) {
		ImGui::SliderFloat(T("feature.post_processing.physical_lens.f_number", "F-Number"), &settings.FNumber, (settings.LensPreset == 5 ? 2.91f : PhysicalLensOptics::GetPrescription(settings.LensPreset).minimumFNumber), 22.f, "f/%.1f");
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T("feature.post_processing.physical_lens.f_number_hint", "Higher values close the aperture, changing ghost shapes and producing larger, fainter starbursts."));
		bool presetAperture = settings.ApertureBlades == 0;
		if (ImGui::Checkbox(T("feature.post_processing.physical_lens.preset_aperture", "Use Lens Preset Aperture"), &presetAperture))
			settings.ApertureBlades = presetAperture ? 0 : int(ApertureBladeCount());
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T("feature.post_processing.physical_lens.preset_aperture_hint", "Uses the selected lens preset's blade count. Turn off to choose your own blade count."));
		int blades = int(ApertureBladeCount());
		if (ImGui::SliderInt(T("feature.post_processing.physical_lens.blades", "Aperture Blades"), &blades, 3, 16))
			settings.ApertureBlades = blades;
		ImGui::SliderFloat(T("feature.post_processing.physical_lens.rotation", "Aperture Rotation"), &settings.ApertureRotation, -180.f, 180.f, "%.1f deg");
	}
	ImGui::SliderFloat(T("feature.post_processing.physical_lens.coating", "Coating Coverage"), &settings.Coating, 0.f, 1.f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.physical_lens.coating_hint", "Adjusts how strongly lens coatings affect ghost brightness and color."));
}

void PhysicalLens::CompileShaders()
{
	shadersRequested = true;
	const std::array cs{
		ComputeShaderCompileInfo{ &extractCS, "flare.cs.hlsl", {}, "Extract" },
		ComputeShaderCompileInfo{ &prepareCS, "flare.cs.hlsl", {}, "PrepareRegions" },
		ComputeShaderCompileInfo{ &splitCS, "flare.cs.hlsl", {}, "ExtractLayers" },
		ComputeShaderCompileInfo{ &selectCS, "flare.cs.hlsl", {}, "SelectSources" },
		ComputeShaderCompileInfo{ &jobsCS, "flare.cs.hlsl", { { "PHYSICAL_LENS_AREA_LAYERS", "1" } }, "BuildJobs" },
		ComputeShaderCompileInfo{ &expandCS, "planning.cs.hlsl", {}, "ExpandCachedJobs" },
		ComputeShaderCompileInfo{ &requestPlansCS, "planning.cs.hlsl", {}, "RequestPlans" },
		ComputeShaderCompileInfo{ &bakePlansCS, "planning.cs.hlsl", {}, "BakeRequestedPlans" },
		ComputeShaderCompileInfo{ &traceCS, "beam.trace.cs.hlsl", { { "PHYSICAL_LENS_PLANNING", "1" } }, "TraceBeams" },
		ComputeShaderCompileInfo{ &shadeCS, "beam.trace.cs.hlsl", { { "PHYSICAL_LENS_AREA_LAYERS", "1" } }, "ShadeBeams" },
		ComputeShaderCompileInfo{ &ghostWaveShaders[0], "ghost_wave.cs.hlsl", {}, "BuildMaterial" },
		ComputeShaderCompileInfo{ &ghostWaveShaders[1], "ghost_wave.cs.hlsl", {}, "BuildField" },
		ComputeShaderCompileInfo{ &ghostWaveShaders[2], "ghost_wave.cs.hlsl", {}, "Transform" },
		ComputeShaderCompileInfo{ &ghostWaveShaders[3], "ghost_wave.cs.hlsl", {}, "Accumulate" },
		ComputeShaderCompileInfo{ &reduceCS, "flare.cs.hlsl", { { "PHYSICAL_LENS_PLANNING", "1" } }, "ReduceGhosts" },
		ComputeShaderCompileInfo{ &resolveCS, "area.resolve.cs.hlsl", {}, "ResolveArea" }
	};
	const std::array vs{ VertexShaderCompileInfo{ &ghostVS, "beam.hlsl", { { "PHYSICAL_LENS_AREA_LAYERS", "1" } }, "VSMain" } };
	const std::array ps{ PixelShaderCompileInfo{ &ghostPS, "beam.hlsl", {}, "PSMain" }, PixelShaderCompileInfo{ &ghostVioletPS, "beam.hlsl", {}, "PSPackedViolet" }, PixelShaderCompileInfo{ &ghostAreaPS, "beam.hlsl", {}, "PSArea" } };
	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\PhysicalLensFlare", cs);
	const std::array imaging{
		ComputeShaderCompileInfo{ &cameraCS, "trace.cs.hlsl", {}, "BuildCameraMaps" },
		ComputeShaderCompileInfo{ &focusCS, "focus.cs.hlsl", {}, "UpdateFocus" },
		ComputeShaderCompileInfo{ &waveShaders[0], "diffraction_detail.cs.hlsl", {}, "BuildDetailedPupil" },
		ComputeShaderCompileInfo{ &waveShaders[1], "diffraction_detail.cs.hlsl", {}, "TransformDetailed" },
		ComputeShaderCompileInfo{ &waveShaders[2], "diffraction_detail.cs.hlsl", {}, "NormalizeDetailed" },
		ComputeShaderCompileInfo{ &waveShaders[3], "diffraction.cs.hlsl", {}, "PrefixTail" },
		ComputeShaderCompileInfo{ &waveShaders[4], "diffraction_amplitude.cs.hlsl", {}, "FilterAmplitudeSpectrum" },
		ComputeShaderCompileInfo{ &waveShaders[5], "diffraction.cs.hlsl", {}, "BuildCoreKernel" },
		ComputeShaderCompileInfo{ &waveShaders[6], "diffraction.cs.hlsl", {}, "BuildWaveMax" },
		ComputeShaderCompileInfo{ &waveShaders[7], "diffraction.cs.hlsl", {}, "ReduceWaveMax" },
		ComputeShaderCompileInfo{ &waveShaders[8], "diffraction_detail.cs.hlsl", {}, "PrefixDetailed" },
		ComputeShaderCompileInfo{ &waveSourcesCS, "diffraction_sources.cs.hlsl", {}, "BuildWaveSources" },
		ComputeShaderCompileInfo{ &waveTilesCS, "diffraction_sources.cs.hlsl", {}, "BuildWaveTiles" },
		ComputeShaderCompileInfo{ &waveTailCS, "diffraction_tail.cs.hlsl", {}, "BuildWaveTail" }
	};
	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\PhysicalLensImaging", imaging);
	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\PhysicalLensFlare", vs, ps);
}

void PhysicalLens::ClearShaderCache()
{
	BumpShaderGeneration();
	{
		std::lock_guard lock(shaderMutex);
		cameraCS = nullptr;
		for (auto& shader : waveShaders) shader = nullptr;
		waveSourcesCS = waveTilesCS = waveTailCS = focusCS = nullptr;
		extractCS = nullptr;
		prepareCS = nullptr;
		splitCS = nullptr;
		selectCS = nullptr;
		traceCS = shadeCS = nullptr;
		for (auto& shader : ghostWaveShaders) shader = nullptr;
		reduceCS = nullptr;
		resolveCS = nullptr;
		jobsCS = nullptr;
		expandCS = requestPlansCS = bakePlansCS = nullptr;
		ghostVS = nullptr;
		ghostPS = nullptr;
		ghostVioletPS = ghostAreaPS = nullptr;
	}
	outputReady = false;
	cameraReady = cameraValid = false;
	waveReady = waveValid = false;
	planningValid = false;
	ghostWaveValid = false;
	lastGhostFrame = ~0u;
	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/PhysicalLensFlare");
	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/PhysicalLensImaging");
	shadersRequested = false;
	if (IsActive() && resourcesReady)
		CompileShaders();
}

void PhysicalLens::SetupResources()
{
	resourcesReady = false;
	outputReady = cameraReady = waveReady = false;
	if (!IsActive())
		return;
	cachedPreset = -1;
	lastFocusFrame = ~0u;
	focusPending.fill(false);
	D3D11_TEXTURE2D_DESC edgeDesc{};
	edgeDesc.Width = edgeDesc.Height = PhysicalLensGhostWave::Size;
	edgeDesc.ArraySize = edgeDesc.SampleDesc.Count = 1;
	edgeDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	edgeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	edgeDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	ghostDiffraction = std::make_unique<Texture2D>(edgeDesc, "PhysicalLens::GhostWaveAtlas");
	D3D11_SHADER_RESOURCE_VIEW_DESC edgeSRV{};
	edgeSRV.Format = edgeDesc.Format;
	edgeSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	edgeSRV.Texture2D.MipLevels = ~0u;
	ghostDiffraction->CreateSRV(edgeSRV);
	D3D11_UNORDERED_ACCESS_VIEW_DESC edgeUAV{};
	edgeUAV.Format = edgeDesc.Format;
	edgeUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	ghostDiffraction->CreateUAV(edgeUAV);
	edgeDesc.MipLevels = edgeSRV.Texture2D.MipLevels = 1;
	edgeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	edgeDesc.MiscFlags = 0;
	ghostMaterial = std::make_unique<Texture2D>(edgeDesc, "PhysicalLens::GhostPupilMaterial");
	ghostMaterial->CreateSRV(edgeSRV);
	ghostMaterial->CreateUAV(edgeUAV);
	ghostWaveParameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensGhostWave::Parameters>(), "PhysicalLens::GhostWaveParameters");
	cameraReady = cameraValid = false;
	waveReady = waveValid = false;
	planningValid = false;
	ghostWaveValid = false;
	lastGhostFrame = ~0u;
	cameraParameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensImaging::GPUParameters>(), "PhysicalLens::ReverseOptics");
	cameraTableParameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensImaging::CameraTableParameters>(), "PhysicalLens::MappingParameters");
	D3D11_TEXTURE2D_DESC cameraDesc{};
	cameraDesc.Width = cachedCameraTable.width;
	cameraDesc.Height = cachedCameraTable.height;
	cameraDesc.ArraySize = 3;
	cameraDesc.MipLevels = 1;
	cameraDesc.SampleDesc.Count = 1;
	cameraDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	cameraDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	cameraMap = std::make_unique<Texture2D>(cameraDesc, "PhysicalLens::ChromaticMapping");
	D3D11_SHADER_RESOURCE_VIEW_DESC cameraSRV{};
	cameraSRV.Format = cameraDesc.Format;
	cameraSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	cameraSRV.Texture2DArray.MipLevels = 1;
	cameraSRV.Texture2DArray.ArraySize = 3;
	cameraMap->CreateSRV(cameraSRV);
	D3D11_UNORDERED_ACCESS_VIEW_DESC cameraUAV{};
	cameraUAV.Format = cameraDesc.Format;
	cameraUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	cameraUAV.Texture2DArray.ArraySize = 3;
	cameraMap->CreateUAV(cameraUAV);
	waveParameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensImaging::DiffractionParameters>(), "PhysicalLens::DiffractionParameters");
	cameraDesc.Width = cameraDesc.Height = 512;
	for (unsigned index = 0; index < 3; ++index) {
		cameraDesc.Width = cameraDesc.Height = index == 1 ? 512 : 1024;
		waveTextures[index] = std::make_unique<Texture2D>(cameraDesc, ("PhysicalLens::WaveScratch" + std::to_string(index)).c_str());
		waveTextures[index]->CreateSRV(cameraSRV);
		waveTextures[index]->CreateUAV(cameraUAV);
	}
	cameraDesc.Width = cameraDesc.Height = 64;
	cameraDesc.MipLevels = 7;
	cameraDesc.Format = DXGI_FORMAT_R32_FLOAT;
	waveMax = std::make_unique<Texture2D>(cameraDesc, "PhysicalLens::WaveMaxDensity");
	cameraSRV.Format = cameraDesc.Format;
	cameraSRV.Texture2DArray.MipLevels = 7;
	waveMax->CreateSRV(cameraSRV);
	cameraSRV.Texture2DArray.MipLevels = 1;
	cameraUAV.Format = cameraDesc.Format;
	for (unsigned mip = 0; mip < 7; ++mip) {
		cameraSRV.Texture2DArray.MostDetailedMip = mip;
		cameraUAV.Texture2DArray.MipSlice = mip;
		waveMaxMipSRVs[mip] = nullptr;
		waveMaxMipUAVs[mip] = nullptr;
		DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(waveMax->resource.get(), &cameraSRV, waveMaxMipSRVs[mip].put()));
		DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(waveMax->resource.get(), &cameraUAV, waveMaxMipUAVs[mip].put()));
		Util::SetResourceName(waveMaxMipSRVs[mip].get(), ("PhysicalLens::WaveMaxDensity Mip " + std::to_string(mip) + " SRV").c_str());
		Util::SetResourceName(waveMaxMipUAVs[mip].get(), ("PhysicalLens::WaveMaxDensity Mip " + std::to_string(mip) + " UAV").c_str());
	}
	waveKernel = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 4>>(uint64_t(12), true, false), 12, "PhysicalLens::DiffractionCore");
	waveKernel->CreateSRV();
	waveKernel->CreateUAV();
	waveSources = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 4>>(uint64_t(SourceCount * 7), true, false), SourceCount * 7, "PhysicalLens::DiffractionSources");
	waveSources->CreateSRV();
	waveSources->CreateUAV();
	D3D11_TEXTURE2D_DESC sourceDescription{};
	globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture->GetDesc(&sourceDescription);
	unsigned waveTileCount = ((sourceDescription.Width + 15) / 16) * ((sourceDescription.Height + 15) / 16);
	waveTiles = std::make_unique<StructuredBuffer>(StructuredBufferDesc<unsigned>(uint64_t(waveTileCount), true, false), waveTileCount, "PhysicalLens::DiffractionTiles");
	waveTiles->CreateSRV();
	waveTiles->CreateUAV();
	waveWorkTiles = std::make_unique<StructuredBuffer>(StructuredBufferDesc<unsigned>(uint64_t(waveTileCount), true, false), waveTileCount, "PhysicalLens::ActiveDiffractionTiles");
	waveWorkTiles->CreateSRV();
	waveWorkTiles->CreateUAV();
	D3D11_BUFFER_DESC dispatchDesc{};
	dispatchDesc.ByteWidth = 16;
	dispatchDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	dispatchDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	waveDispatch = nullptr;
	waveDispatchUAV = nullptr;
	waveDispatchSRV = nullptr;
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&dispatchDesc, nullptr, waveDispatch.put()));
	Util::SetResourceName(waveDispatch.get(), "PhysicalLens::DiffractionDispatch");
	D3D11_UNORDERED_ACCESS_VIEW_DESC dispatchWrite{};
	dispatchWrite.Format = DXGI_FORMAT_R32_TYPELESS;
	dispatchWrite.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	dispatchWrite.Buffer.NumElements = 4;
	dispatchWrite.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(waveDispatch.get(), &dispatchWrite, waveDispatchUAV.put()));
	Util::SetResourceName(waveDispatchUAV.get(), "PhysicalLens::DiffractionDispatch UAV");
	D3D11_SHADER_RESOURCE_VIEW_DESC dispatchRead{};
	dispatchRead.Format = DXGI_FORMAT_R32_TYPELESS;
	dispatchRead.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	dispatchRead.BufferEx.NumElements = 4;
	dispatchRead.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(waveDispatch.get(), &dispatchRead, waveDispatchSRV.put()));
	Util::SetResourceName(waveDispatchSRV.get(), "PhysicalLens::DiffractionDispatch SRV");
	waveRadiance.reset();

	parameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<Parameters>(), "PhysicalLens::Parameters");
	candidates = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 8>>(uint64_t(CandidateCount), true, false), CandidateCount, "PhysicalLens::Candidates");
	candidates->CreateUAV();
	regions = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 4>>(uint64_t(TileCount), true, false), TileCount, "PhysicalLens::Regions");
	regions->CreateUAV();
	regions->CreateSRV();
	sources = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 8>>(uint64_t(SourceCount), true, false), SourceCount, "PhysicalLens::Sources");
	sources->CreateSRV();
	sources->CreateUAV();
	constexpr unsigned rayCount = 6 * PhysicalLensOptics::PathsPerSource * 3 * PhysicalLensOptics::Vertices;
	paths = std::make_unique<StructuredBuffer>(StructuredBufferDesc<PhysicalLensOptics::GhostPath>(uint64_t(PhysicalLensOptics::MaxPaths), false, true), PhysicalLensOptics::MaxPaths, "PhysicalLens::Paths");
	paths->CreateSRV();
	jobs = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 8>>(uint64_t(SourceCount * PhysicalLensOptics::PathsPerSource), true, false), SourceCount * PhysicalLensOptics::PathsPerSource, "PhysicalLens::VisibleJobs");
	jobs->CreateSRV();
	jobs->CreateUAV();
	const auto indices = PhysicalLensOptics::BeamIndices();
	D3D11_BUFFER_DESC indexDesc{};
	indexDesc.ByteWidth = sizeof(indices);
	indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
	indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA indexData{ indices.data(), 0, 0 };
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&indexDesc, &indexData, splatIndices.put()));
	Util::SetResourceName(splatIndices.get(), "PhysicalLens::BeamTriangles");
	D3D11_BUFFER_DESC args{};
	args.ByteWidth = 128;
	args.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	args.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&args, nullptr, indirect.put()));
	Util::SetResourceName(indirect.get(), "PhysicalLens::IndirectArguments");
	D3D11_UNORDERED_ACCESS_VIEW_DESC argView{};
	argView.Format = DXGI_FORMAT_R32_TYPELESS;
	argView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	argView.Buffer.NumElements = 32;
	argView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(indirect.get(), &argView, indirectUAV.put()));
	Util::SetResourceName(indirectUAV.get(), "PhysicalLens::IndirectArguments UAV");
	D3D11_SHADER_RESOURCE_VIEW_DESC argRead{};
	argRead.Format = DXGI_FORMAT_R32_TYPELESS;
	argRead.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	argRead.BufferEx.NumElements = 32;
	argRead.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(indirect.get(), &argRead, indirectSRV.put()));
	Util::SetResourceName(indirectSRV.get(), "PhysicalLens::IndirectArguments SRV");
	bundles = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 8>>(uint64_t(SourceCount * PhysicalLensOptics::PathsPerSource * 3), true, false), SourceCount * PhysicalLensOptics::PathsPerSource * 3, "PhysicalLens::Bundles");
	bundles->CreateSRV();
	bundles->CreateUAV();
	cachedPreset = -1;
	rays = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 4>>(uint64_t(rayCount), true, false), rayCount, "PhysicalLens::Rays");
	rays->CreateSRV();
	rays->CreateUAV();
	constexpr unsigned pupilCount = SourceCount * PhysicalLensOptics::PathsPerSource * PhysicalLensOptics::Grid;
	pupils = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 2>>(uint64_t(pupilCount), true, false), pupilCount, "PhysicalLens::PupilCoordinates");
	pupils->CreateSRV();
	pupils->CreateUAV();
	constexpr unsigned planCount = 65 * PhysicalLensOptics::MaxPaths;
	planTable = std::make_unique<StructuredBuffer>(StructuredBufferDesc<std::array<float, 4>>(uint64_t(planCount * 6), true, false), planCount * 6, "PhysicalLens::PlanningTable");
	planTable->CreateSRV();
	planTable->CreateUAV();
	planStatus = std::make_unique<StructuredBuffer>(StructuredBufferDesc<unsigned>(uint64_t(planCount), true, false), planCount, "PhysicalLens::PlanningValidity");
	planStatus->CreateUAV();
	constexpr unsigned requests = 4 * PhysicalLensOptics::PathsPerSource;
	planRequests = std::make_unique<StructuredBuffer>(StructuredBufferDesc<unsigned>(uint64_t(requests), true, false), requests, "PhysicalLens::PlanningRequests");
	planRequests->CreateSRV();
	planRequests->CreateUAV();
	args.ByteWidth = 16;
	argView.Buffer.NumElements = 4;
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&args, nullptr, planArguments.put()));
	Util::SetResourceName(planArguments.get(), "PhysicalLens::PlanningDispatch");
	DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(planArguments.get(), &argView, planArgumentsUAV.put()));
	Util::SetResourceName(planArgumentsUAV.get(), "PhysicalLens::PlanningDispatch UAV");
	D3D11_TEXTURE2D_DESC desc{};
	globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture->GetDesc(&desc);
	desc.Width = std::max(1u, (desc.Width + 1) / 2);
	desc.Height = std::max(1u, (desc.Height + 1) / 2);
	desc.MipLevels = 1;
	desc.ArraySize = 3;
	// Floating-point blending must not overflow on concentrated caustics.
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc = { 1, 0 };
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = desc.MiscFlags = 0;
	output = std::make_unique<Texture2D>(desc, "PhysicalLens::Ghosts");
	D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = desc.Format;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srv.Texture2DArray.MipLevels = 1;
	srv.Texture2DArray.ArraySize = 3;
	output->CreateSRV(srv);

	D3D11_UNORDERED_ACCESS_VIEW_DESC resolved{};
	resolved.Format = desc.Format;
	resolved.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	resolved.Texture2DArray.ArraySize = 3;
	output->CreateUAV(resolved);
	D3D11_RENDER_TARGET_VIEW_DESC rtv{};
	rtv.Format = desc.Format;
	rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	rtv.Texture2DArray.ArraySize = 1;
	for (unsigned channel = 0; channel < 3; ++channel) {
		if (channel < 2) {
			rtv.Texture2DArray.FirstArraySlice = channel;
			channelTargets[channel] = nullptr;
			DX::ThrowIfFailed(globals::d3d::device->CreateRenderTargetView(output->resource.get(), &rtv, channelTargets[channel].put()));
			Util::SetResourceName(channelTargets[channel].get(), "PhysicalLens::SpectralTarget");
		}
		channelParameters[channel] = std::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<unsigned, 4>>(), "PhysicalLens::SpectralBand");
		channelParameters[channel]->Update(std::array<unsigned, 4>{ channel, 0, 0, 0 });
	}
	// Keep both short-wave responses in one ROP target. Resolve adds violet
	// to red once, avoiding MRT blending without tracing another wavelength.
	desc.ArraySize = desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32G32_FLOAT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	blueViolet = std::make_unique<Texture2D>(desc, "PhysicalLens::BlueViolet");
	D3D11_SHADER_RESOURCE_VIEW_DESC packedSRV{};
	packedSRV.Format = desc.Format;
	packedSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	packedSRV.Texture2D.MipLevels = 1;
	blueViolet->CreateSRV(packedSRV);
	D3D11_RENDER_TARGET_VIEW_DESC packedRTV{};
	packedRTV.Format = desc.Format;
	packedRTV.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	blueViolet->CreateRTV(packedRTV);
	// Width is driven by the emitter footprint; large sources no longer
	// expand every beam over the half-resolution target. Four guard texels
	// keep reconstruction continuous at the screen boundary.
	const unsigned fineWidth = desc.Width, fineHeight = desc.Height;
	desc.Format = packedSRV.Format = packedRTV.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	for (unsigned level = 0; level < areaLayers.size(); ++level) {
		desc.Width = std::max(1u, fineWidth >> (2 + 2 * level)) + 8;
		desc.Height = std::max(1u, fineHeight >> (2 + 2 * level)) + 8;
		areaLayers[level] = std::make_unique<Texture2D>(desc, ("PhysicalLens::AreaLayer" + std::to_string(level)).c_str());
		areaLayers[level]->CreateSRV(packedSRV);
		areaLayers[level]->CreateRTV(packedRTV);
		for (unsigned band = 0; band < 3; ++band) {
			auto& constants = areaParameters[level * 3 + band];
			constants = std::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<unsigned, 4>>(), "PhysicalLens::AreaBand");
			constants->Update(std::array<unsigned, 4>{ band, level + 1, 0, 0 });
		}
	}
	auto* device = globals::d3d::device;
	D3D11_BLEND_DESC blend{};
	auto& target = blend.RenderTarget[0];
	target.BlendEnable = TRUE;
	target.SrcBlend = target.DestBlend = target.SrcBlendAlpha = target.DestBlendAlpha = D3D11_BLEND_ONE;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&blend, additive.put()));
	Util::SetResourceName(additive.get(), "PhysicalLens::Additive");
	D3D11_RASTERIZER_DESC raster{};
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = D3D11_CULL_NONE;
	raster.DepthClipEnable = TRUE;
	DX::ThrowIfFailed(device->CreateRasterizerState(&raster, twoSided.put()));
	Util::SetResourceName(twoSided.get(), "PhysicalLens::TwoSided");
	D3D11_SAMPLER_DESC sample{};
	sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	sample.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(device->CreateSamplerState(&sample, sampler.put()));
	Util::SetResourceName(sampler.get(), "PhysicalLens::Sampler");
	resourcesReady = true;
	if (!shadersRequested)
		CompileShaders();
}

PhysicalLensImaging::GPUParameters PhysicalLens::OpticalParameters() const
{
	auto data = PhysicalLensImaging::UploadParameters(PhysicalLensImaging::Reverse(cachedImagingModel), 0,
		optical.blades, DirectX::XMConvertToRadians(optical.rotation), settings.Coating);
	data.bladeCurvature = BladeCurvature();
	return data;
}

float PhysicalLens::ResolveCameraFocus(const CinematicCamera::PhysicalCameraState& camera)
{
	auto* ctx = globals::d3d::context;
	const unsigned frame = globals::state->frameCount;
	auto* dof = owner->GetPipelineFeature<DoF>(PostProcessing::FeaturePipelineIndex::DoF);
	auto* source = dof && dof->IsActive() ? dof->GetFocusTexture(frame) : nullptr;
	const bool reset = lastFocusFrame == ~0u || frame != lastFocusFrame + 1 || globals::state->ShouldResetHistory();
	if (reset) {
		focusPending.fill(false);
		completedFocusFrame = 0;
		resolvedFocusM = camera.ManualDistanceM;
	}
	if (!focusTexture) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		focusTexture = std::make_unique<Texture2D>(desc, "PhysicalLens::Focus");
		focusTexture->CreateSRV(srv);
		focusTexture->CreateUAV(uav);
		previousFocus = std::make_unique<Texture2D>(desc, "PhysicalLens::PreviousFocus");
		previousFocus->CreateSRV(srv);
		focusParameters = std::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<float, 8>>(), "PhysicalLens::FocusParameters");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		for (auto& texture : focusReadback) {
			winrt::check_hresult(globals::d3d::device->CreateTexture2D(&desc, nullptr, texture.put()));
			Util::SetResourceName(texture.get(), "PhysicalLens::FocusReadback");
		}
	}
	for (unsigned i = 0; i < focusReadback.size(); ++i) {
		if (!focusPending[i])
			continue;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(ctx->Map(focusReadback[i].get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
			float metres = *static_cast<const float*>(mapped.pData) * 1000.f;
			if (focusFrames[i] >= completedFocusFrame && std::isfinite(metres) && metres > 0) {
				resolvedFocusM = metres;
				completedFocusFrame = focusFrames[i];
			}
			ctx->Unmap(focusReadback[i].get(), 0);
			focusPending[i] = false;
		}
	}
	if (source) {
		// Keep the fallback's history warm when DoF is disabled next frame.
		ctx->CopyResource(previousFocus->resource.get(), source);
	} else {
		winrt::com_ptr<ID3D11ComputeShader> shader;
		{
			std::lock_guard lock(shaderMutex);
			shader = focusCS;
		}
		auto* depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
		if (!shader || !depth)
			return resolvedFocusM;
		struct alignas(16) FocusData
		{
			float2 uv;
			float manualKM, speed;
			unsigned automatic, reset;
			float padding[2]{};
		} data{ camera.ScreenPointUV, camera.ManualDistanceM * .001f, camera.TransitionSpeed,
			camera.Mode == CinematicCamera::FocusMode::ScreenPoint, reset };
		static_assert(sizeof(FocusData) == 32);
		auto& resolver = owner->GetCinematicCamera().focusResolver;
		if (camera.Mode == CinematicCamera::FocusMode::Target) {
			uint ref = 0;
			const auto target = resolver.ResolveTarget(!dof || dof->settings.consoleSelection, ref);
			if (target.hasTarget) {
				data.automatic = target.projected;
				data.uv = target.focusCoord;
				data.manualKM = target.distanceM * .001f;
			}
		}
		if (auto* player = globals::game::player; player && globals::game::ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) {
			data.automatic = resolver.GetReferenceFocusCoord(player, data.uv);
			data.manualKM = Util::Units::GameUnitsToMeters(resolver.GetDistanceToReference(player)) * .001f;
		}
		focusParameters->Update(data);
		{
			OpticalBindings bindings(ctx);
			ID3D11ShaderResourceView* inputs[]{ depth, previousFocus->srv.get() };
			ID3D11UnorderedAccessView* focusUAV = focusTexture->uav.get();
			ID3D11Buffer* cb = focusParameters->CB();
			auto* linear = sampler.get();
			ctx->CSSetConstantBuffers(0, 1, &cb);
			ctx->CSSetShaderResources(0, 2, inputs);
			ctx->CSSetSamplers(0, 1, &linear);
			ctx->CSSetUnorderedAccessViews(0, 1, &focusUAV, nullptr);
			ctx->CSSetShader(shader.get(), nullptr, 0);
			ctx->Dispatch(1, 1, 1);
		}
		source = focusTexture->resource.get();
		ctx->CopyResource(previousFocus->resource.get(), source);
	}
	lastFocusFrame = frame;
	// Never block or overwrite an in-flight readback under GPU contention.
	for (unsigned i = 0; i < focusReadback.size(); ++i) {
		if (focusPending[i])
			continue;
		ctx->CopyResource(focusReadback[i].get(), source);
		focusFrames[i] = frame;
		focusPending[i] = true;
		break;
	}
	return resolvedFocusM;
}

void PhysicalLens::UpdateCameraMapping(float aspect)
{
	if (!settings.ImageEffects)
		return;
	winrt::com_ptr<ID3D11ComputeShader> shader;
	{
		std::lock_guard lock(shaderMutex);
		shader = cameraCS;
	}
	if (!shader)
		return;
	const auto data = OpticalParameters();
	const auto table = PhysicalLensImaging::CameraTable(cachedImagingModel, projectionFocal, aspect, optical.focusM > 0 ? optical.focusM * 1000.0 : PhysicalLensImaging::Infinity);
	if (!cameraValid || std::memcmp(&data, &cachedCameraParameters, sizeof(data)) || std::memcmp(&table, &cachedCameraTable, sizeof(table))) {
		cameraParameters->Update(data);
		cameraTableParameters->Update(table);
		auto* ctx = globals::d3d::context;
		globals::profiler->BeginPass("PostProcessing::PhysicalLensMapping");
		{
			PostProcessingRaster::RasterPass raster(ctx);
			OpticalBindings bindings(ctx);
			ctx->OMSetRenderTargets(0, nullptr, nullptr);
			ID3D11Buffer* buffers[]{ cameraParameters->CB(), cameraTableParameters->CB() };
			ctx->CSSetConstantBuffers(0, 2, buffers);
			ID3D11UnorderedAccessView* outputs[5]{ nullptr, cameraMap->uav.get(), nullptr, nullptr, nullptr };
			ctx->CSSetUnorderedAccessViews(0, 5, outputs, nullptr);
			ctx->CSSetShader(shader.get(), nullptr, 0);
			ctx->Dispatch(table.width, table.height, 3);
		}
		globals::profiler->EndPass();
		cachedCameraParameters = data;
		cachedCameraTable = table;
		cameraValid = true;
	}
	cameraReady = cameraValid;
}

bool PhysicalLens::UpdateDiffraction(unsigned width, unsigned height)
{
	if (!settings.Diffraction || (settings.ImageEffects && !cameraReady))
		return false;
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 9> shaders;
	{
		std::lock_guard lock(shaderMutex);
		shaders = waveShaders;
	}
	if (std::any_of(shaders.begin(), shaders.end(), [](const auto& shader) { return !shader; }))
		return false;
	// Allocate the full-resolution tail only when diffraction is actually used.
	if (!waveRadiance) {
		D3D11_TEXTURE2D_DESC tailDesc{};
		tailDesc.Width = width;
		tailDesc.Height = height;
		tailDesc.MipLevels = tailDesc.SampleDesc.Count = 1;
		tailDesc.ArraySize = 3;
		tailDesc.Format = DXGI_FORMAT_R32_FLOAT;
		tailDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		waveRadiance = std::make_unique<Texture2D>(tailDesc, "PhysicalLens::DiffractionRadiance");
		D3D11_SHADER_RESOURCE_VIEW_DESC tailSRV{};
		tailSRV.Format = tailDesc.Format;
		tailSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		tailSRV.Texture2DArray.MipLevels = 1;
		tailSRV.Texture2DArray.ArraySize = 3;
		waveRadiance->CreateSRV(tailSRV);
		D3D11_UNORDERED_ACCESS_VIEW_DESC tailUAV{};
		tailUAV.Format = tailDesc.Format;
		tailUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		tailUAV.Texture2DArray.ArraySize = 3;
		waveRadiance->CreateUAV(tailUAV);
	}
	const auto optics = OpticalParameters();
	const auto table = PhysicalLensImaging::CameraTable(cachedImagingModel, projectionFocal, float(width) / height, optical.focusM > 0 ? optical.focusM * 1000.0 : PhysicalLensImaging::Infinity);
	// Resolve fine dirt on a 512-sample pupil within the existing 1024 FFT.
	// DiffractionTable adjusts the Fourier step, preserving the optical scale.
	auto sampling = PhysicalLensImaging::DiffractionTable(table, width, height, optical.fNumber, settings.StarburstSize, settings.StarburstIntensity, 512u);
	// The camera aperture is equal-area; FFT coordinates span its outer radius.
	if (optical.linked)
		for (auto& band : sampling.sampling)
			for (auto& value : band) value /= cameraApertureExtent;
	sampling.enabled = settings.ImageEffects ? 2.f : 1.f;
	sampling.surface = { settings.SurfaceScatter, float(settings.LensPreset + 1), float((optical.linked ? optical.focalMM : cachedImagingModel.focalLength) / (2 * optical.fNumber) * cameraApertureExtent), settings.DirtOpacity };
	sampling.pupilDirt = { settings.DirtDensity, settings.DirtSize, settings.ScratchLength, DirectX::XMConvertToRadians(settings.ScratchSpread) };
	auto cacheSampling = sampling;
	// Source gain and unused legacy surface fields do not change the real-pupil FFT.
	cacheSampling.starburst[1] = cachedWaveParameters.starburst[1];
	cacheSampling.surface[0] = cachedWaveParameters.surface[0];
	cacheSampling.surface[2] = cachedWaveParameters.surface[2];
	cacheSampling.enabled = cachedWaveParameters.enabled;
	if (!waveValid || optics.blades != cachedWaveOptics.blades || optics.rotation != cachedWaveOptics.rotation || optics.bladeCurvature != cachedWaveOptics.bladeCurvature || std::memcmp(&cacheSampling, &cachedWaveParameters, sizeof(cacheSampling))) {
		cameraParameters->Update(optics);
		cameraTableParameters->Update(table);
		waveParameters->Update(sampling);
		auto* ctx = globals::d3d::context;
		globals::profiler->BeginPass("PostProcessing::PhysicalLensDiffractionCache");
		{
			PostProcessingRaster::RasterPass raster(ctx);
			OpticalBindings bindings(ctx);
			ctx->OMSetRenderTargets(0, nullptr, nullptr);
			ID3D11UnorderedAccessView* empty[6]{};
			ctx->CSSetUnorderedAccessViews(0, 6, empty, nullptr);
			ID3D11Buffer* buffers[]{ cameraParameters->CB(), cameraTableParameters->CB(), waveParameters->CB() };
			ctx->CSSetConstantBuffers(0, 3, buffers);
			const auto dispatch = [&](unsigned shader, ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* target, unsigned slot, unsigned x, unsigned y, unsigned z) {
				ctx->CSSetShaderResources(1, 1, &source);
				ctx->CSSetUnorderedAccessViews(slot, 1, &target, nullptr);
				ctx->CSSetShader(shaders[shader].get(), nullptr, 0);
				ctx->Dispatch(x, y, z);
				source = nullptr;
				target = nullptr;
				ctx->CSSetShaderResources(1, 1, &source);
				ctx->CSSetUnorderedAccessViews(slot, 1, &target, nullptr);
			};
			// The paper path reuses one real-pupil spectrum for all wavelengths.
			// Higher-density pupils are cache-only. The final atlas and
			// all per-frame source/tile/composite resources remain 512 squared.
			dispatch(0, nullptr, waveTextures[0]->uav.get(), 2, 128, 128, 1);
			dispatch(1, waveTextures[0]->srv.get(), waveTextures[2]->uav.get(), 3, 1024, 1, 1);
			dispatch(1, waveTextures[2]->srv.get(), waveTextures[0]->uav.get(), 3, 1024, 1, 1);
			dispatch(2, waveTextures[0]->srv.get(), waveTextures[2]->uav.get(), 3, 128, 128, 1);
			dispatch(8, waveTextures[2]->srv.get(), waveTextures[0]->uav.get(), 3, 1024, 1, 1);
			dispatch(8, waveTextures[0]->srv.get(), waveTextures[2]->uav.get(), 3, 1024, 1, 1);
			dispatch(4, waveTextures[2]->srv.get(), waveTextures[0]->uav.get(), 4, 64, 64, 3);
			dispatch(3, waveTextures[0]->srv.get(), waveTextures[2]->uav.get(), 3, 512, 3, 1);
			dispatch(3, waveTextures[2]->srv.get(), waveTextures[1]->uav.get(), 3, 512, 3, 1);
			dispatch(5, waveTextures[1]->srv.get(), waveKernel->UAV(), 5, 3, 1, 1);
			dispatch(6, waveTextures[1]->srv.get(), waveMaxMipUAVs[0].get(), 3, 64, 64, 3);
			for (unsigned mip = 1; mip < 7; ++mip) dispatch(7, waveMaxMipSRVs[mip - 1].get(), waveMaxMipUAVs[mip].get(), 3, std::max(1u, (64u >> mip) / 8), std::max(1u, (64u >> mip) / 8), 3);
		}
		globals::profiler->EndPass();
		cachedWaveOptics = optics;
		cachedWaveTable = table;
		cachedWaveParameters = sampling;
		waveValid = true;
	} else if (std::memcmp(&sampling, &cachedWaveParameters, sizeof(sampling))) {
		waveParameters->Update(sampling);
		cachedWaveParameters = sampling;
	}
	return true;
}

bool PhysicalLens::UpdateGhostWave()
{
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 4> shaders;
	{
		std::lock_guard lock(shaderMutex);
		shaders = ghostWaveShaders;
	}
	for (const auto& shader : shaders)
		if (!shader)
			return false;
	PhysicalLensGhostWave::Parameters data;
	data.aperture = { float(optical.blades), BladeCurvature(),
		DirectX::XMConvertToRadians(optical.rotation), optical.fNumber };
	data.surface = { settings.SurfaceScatter, float((optical.linked ? optical.focalMM : cachedImagingModel.focalLength) / (2 * optical.fNumber) * cameraApertureExtent), float(settings.LensPreset + 1), 0 };
	if (ghostWaveValid && data.aperture == cachedGhostWave.aperture && data.surface == cachedGhostWave.surface)
		return true;
	auto* ctx = globals::d3d::context;
	globals::profiler->BeginPass("PostProcessing::PhysicalLensGhostWaveCache");
	{
		PostProcessingRaster::RasterPass raster(ctx);
		OpticalBindings bindings(ctx);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		ID3D11ShaderResourceView* empty[6]{};
		ctx->PSSetShaderResources(0, 6, empty);
		ctx->VSSetShaderResources(0, 6, empty);
		// Texture 1 is the live direct-source PSF. Only 0/2 are scratch.
		PhysicalLensGhostWave::Bake(ctx, { shaders[0].get(), shaders[1].get(), shaders[2].get(), shaders[3].get() },
			ghostWaveParameters->CB(), data, { waveTextures[0]->srv.get(), waveTextures[2]->srv.get() },
			{ waveTextures[0]->uav.get(), waveTextures[2]->uav.get() }, ghostMaterial->srv.get(), ghostMaterial->uav.get(),
			ghostDiffraction->srv.get(), ghostDiffraction->uav.get());
	}
	globals::profiler->EndPass();
	cachedGhostWave = data;
	ghostWaveValid = true;
	return true;
}

void PhysicalLens::Draw(TextureInfo& input)
{
	outputReady = false;
	cameraReady = false;
	waveReady = false;
	if (!resourcesReady)
		SetupResources();
	if (!shadersRequested)
		CompileShaders();
	// Keep local references while a shader-cache clear can replace the members.
	winrt::com_ptr<ID3D11ComputeShader> extract, prepare, split, select, buildJobs, expandJobs, requestPlans, bakePlans, trace, shade, reduce, resolve, buildWaveSources, buildWaveTiles, buildWaveTail;
	winrt::com_ptr<ID3D11VertexShader> vs;
	winrt::com_ptr<ID3D11PixelShader> ps, violetPS, areaPS;
	{
		std::lock_guard lock(shaderMutex);
		extract = extractCS;
		prepare = prepareCS;
		split = splitCS;
		select = selectCS;
		trace = traceCS;
		shade = shadeCS;
		reduce = reduceCS;
		resolve = resolveCS;
		buildJobs = jobsCS;
		expandJobs = expandCS;
		requestPlans = requestPlansCS;
		bakePlans = bakePlansCS;
		vs = ghostVS;
		ps = ghostPS;
		violetPS = ghostVioletPS;
		areaPS = ghostAreaPS;
		buildWaveSources = waveSourcesCS;
		buildWaveTiles = waveTilesCS;
		buildWaveTail = waveTailCS;
	}
	if (!extract || !prepare || !split || !select || !buildJobs || !expandJobs || !requestPlans || !bakePlans || !trace || !shade || !reduce || !resolve || !vs || !ps || !violetPS || !areaPS || !input.tex || !input.srv)
		return;
	SanitizeSettings();
	D3D11_TEXTURE2D_DESC inputDesc;
	input.tex->GetDesc(&inputDesc);
	const float aspect = float(inputDesc.Width) / inputDesc.Height;
	const auto* camera = owner ? owner->GetActivePhysicalCameraState() : nullptr;
	optical = { settings.FNumber, settings.FocusDistanceM, settings.ApertureRotation, 0, 0, 0, ApertureBladeCount(), false };
	if (camera) {
		optical = { camera->FNumber, ResolveCameraFocus(*camera), camera->ApertureBladeRotationDeg,
			camera->ApertureRoundness, camera->FocalLengthMM, camera->EffectiveSensorWidthMM,
			unsigned(camera->ApertureBladeCount), true };
	} else {
		lastFocusFrame = ~0u;
	}
	if (!configurationValid || cachedPreset != settings.LensPreset || cachedFNumber != optical.fNumber || cachedFocusDistance != optical.focusM || cachedZoom != settings.ZoomPosition || cachedCoating != settings.Coating || cachedFocalMM != optical.focalMM || cachedLinked != optical.linked ||
		(optical.linked && (cachedCameraBladeCount != optical.blades || cachedCameraRoundness != optical.roundness))) {
		const double focusMM = optical.focusM > 0 ? optical.focusM * 1000.0 : PhysicalLensImaging::Infinity;
		PhysicalLensImaging::Model imagingModel;
		double nextProjectionFocal = 0;
		float apertureExtent = cameraApertureExtent;
		bool configured;
		if (optical.linked) {
			auto cameraInfinity = cachedCameraInfinity;
			if (!cachedLinked || cachedPreset != settings.LensPreset || cachedFocalMM != optical.focalMM || cachedFNumber != optical.fNumber) {
				if (!PhysicalLensImaging::CameraInfinity(settings.LensPreset, optical.focalMM, optical.fNumber, cameraInfinity)) {
					configurationValid = false;
					return;
				}
			}
			if (!cachedLinked || cachedCameraBladeCount != optical.blades || cachedCameraRoundness != optical.roundness)
				apertureExtent = float(PhysicalLensImaging::CameraApertureExtent(optical.blades, optical.roundness));
			configured = PhysicalLensImaging::FocusCamera(cameraInfinity, optical.fNumber, focusMM,
				apertureExtent, imagingModel, nextProjectionFocal);
			if (configured)
				cachedCameraInfinity = std::move(cameraInfinity);
		} else {
			apertureExtent = 1;
			configured = PhysicalLensImaging::Configure(settings.LensPreset, optical.fNumber, settings.ZoomPosition, focusMM, imagingModel, nextProjectionFocal);
		}
		configurationValid = configured;
		if (!configured)
			return;
		cachedImagingModel = std::move(imagingModel);
		projectionFocal = nextProjectionFocal;
		cameraApertureExtent = apertureExtent;
		lastGhostFrame = ~0u;
		cachedLens = PhysicalLensImaging::GhostModel(cachedImagingModel, projectionFocal);
		std::vector<PhysicalLensOptics::GhostPath> allPaths(PhysicalLensOptics::MaxPaths);
		cachedPathCount = 0;
		for (unsigned back = 1; back < cachedLens.count; ++back)
			for (unsigned front = 0; front < back; ++front)
				if (!cachedLens.interfaces[back].stop && !cachedLens.interfaces[front].stop)
					allPaths[cachedPathCount++] = PhysicalLensOptics::BuildPath(cachedLens, back, front, settings.Coating);
		paths->Update(allPaths.data(), allPaths.size() * sizeof(allPaths[0]));
		cachedPreset = settings.LensPreset;
		cachedFNumber = optical.fNumber;
		cachedFocusDistance = optical.focusM;
		cachedZoom = settings.ZoomPosition;
		cachedCoating = settings.Coating;
		cachedFocalMM = optical.focalMM;
		cachedLinked = optical.linked;
		cachedCameraBladeCount = optical.blades;
		cachedCameraRoundness = optical.roundness;
		planningValid = false;
	}
	if (optical.linked) {
		cachedImagingModel.gateWidth = optical.gateWidthMM;
		cachedImagingModel.gateHeight = optical.gateWidthMM / aspect;
	}
	const auto& lens = cachedLens;
	Parameters data{};
	UpdateCameraMapping(aspect);
	const bool renderWave = buildWaveSources && buildWaveTiles && buildWaveTail && UpdateDiffraction(inputDesc.Width, inputDesc.Height);
	if (settings.Intensity == 0 && !renderWave)
		return;
	data.width = inputDesc.Width;
	data.height = inputDesc.Height;
	data.surfaceCount = lens.count;
	data.surfaces = lens.interfaces;
	data.pathCount = cachedPathCount;
	const auto& prescription = cachedImagingModel;
	// Central crop of the lens's actual format. Screen UV maps onto this virtual
	// cinema/still camera; game FOV no longer invents a different sensor each frame.
	data.sensorWidth = float(std::min(prescription.gateWidth, prescription.gateHeight * data.width / data.height));
	data.sensorHeight = data.sensorWidth * data.height / data.width;
	data.sensorZ = lens.sensorZ;
	data.focalLength = lens.focalLength;
	// The input is an already formed image. Normalize ghost flux to the direct
	// pupil at the selected f-number, not to a permanently wide-open lens.
	data.pupilRadius = lens.pupilRadius;
	data.intensity = settings.Intensity * cameraApertureExtent * cameraApertureExtent;
	data.ghostDiffraction = settings.GhostDiffraction;
	data.fNumber = optical.fNumber;
	data.reserved = 1;
	data.beamState = lastGhostFrame != ~0u && globals::state->frameCount == lastGhostFrame + 1 && !globals::state->ShouldResetHistory() ? 2.f : 1.f;
	data.cutoffRelease = std::exp(-2.44932f * std::clamp(*globals::game::deltaTime, 0.f, .25f));
	data.alignmentVariation = settings.AlignmentVariation;
	data.lensSeed = float(settings.LensPreset + 1);
	if (settings.Intensity > 0 && settings.GhostDiffraction > 0 && !UpdateGhostWave())
		return;
	data.maxSources = settings.MaxSources;
	data.threshold = .18f * std::exp2(settings.ThresholdEV);
	data.coating = settings.Coating;
	data.blades = optical.blades;
	// Fixed blade curvature in millimetres: relative rounding falls as the iris
	// closes. Mechanical blade geometry is an estimate, not patent lens data.
	data.bladeCurvature = BladeCurvature();
	data.rotation = DirectX::XMConvertToRadians(optical.rotation);
	data.outputWidth = (float)output->desc.Width;
	data.outputHeight = (float)output->desc.Height;
	auto* exposure = owner ? owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure) : nullptr;
	auto* local = owner ? owner->GetPipelineFeature<LocalExposure>(PostProcessing::FeaturePipelineIndex::LocalExposure) : nullptr;
	const bool hasExposure = exposure && exposure->IsActive() && exposure->GetAdaptationSRV();
	data.exposureDecode = 1.f;
	data.autoExposure = hasExposure ? 1.f : 0.f;
	data.manualExposure = local && local->enabled && local->GetLogLuminanceSRV() ? local->settings.Exposure : 1.f;
	parameters->Update(data);
	auto* ctx = globals::d3d::context;
	globals::profiler->BeginPass("PostProcessing::PhysicalLensFlare");
	{
		PostProcessingRaster::RasterPass raster(ctx);
		OpticalBindings bindings(ctx);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		ID3D11Buffer* cb = parameters->CB();
		ctx->CSSetConstantBuffers(0, 1, &cb);
		ID3D11Buffer* exposureCB = hasExposure ? exposure->GetConstantBuffer() : nullptr;
		ctx->CSSetConstantBuffers(1, 1, &exposureCB);
		ID3D11ShaderResourceView* srvs[6]{ input.srv, nullptr, nullptr, nullptr };
		srvs[5] = hasExposure ? exposure->GetAdaptationSRV() : nullptr;
		ID3D11UnorderedAccessView* uavs[5]{ candidates->UAV(), nullptr, nullptr, nullptr, regions->UAV() };
		ctx->CSSetShaderResources(0, 6, srvs);
		ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
		ctx->CSSetShader(extract.get(), nullptr, 0);
		ctx->Dispatch(64, 36, 1);
		ctx->CSSetShader(prepare.get(), nullptr, 0);
		ctx->Dispatch(1, 1, 1);
		ctx->CSSetShader(split.get(), nullptr, 0);
		ctx->Dispatch(64, 36, 1);
		// Rank connected components from both energy layers under one budget.
		uavs[1] = sources->UAV();
		ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
		ctx->CSSetShader(select.get(), nullptr, 0);
		ctx->Dispatch(1, 1, 1);
		uavs[1] = nullptr;
		uavs[0] = nullptr;
		uavs[4] = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
		srvs[0] = nullptr;
		srvs[1] = sources->SRV();
		ctx->CSSetShaderResources(0, 6, srvs);

		if (renderWave) {
			ID3D11Buffer* waveCB = waveParameters->CB();
			ctx->CSSetConstantBuffers(2, 1, &waveCB);
			srvs[0] = settings.ImageEffects ? cameraMap->srv.get() : nullptr;
			srvs[3] = waveKernel->SRV();
			srvs[4] = waveMax->srv.get();
			ctx->CSSetShaderResources(0, 6, srvs);
			uavs[0] = waveSources->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			ctx->CSSetShader(buildWaveSources.get(), nullptr, 0);
			ctx->Dispatch(1, 1, 1);
			uavs[0] = nullptr;
			uavs[1] = waveTiles->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			srvs[0] = nullptr;
			srvs[1] = settings.ImageEffects ? cameraMap->srv.get() : nullptr;
			srvs[2] = waveSources->SRV();
			srvs[3] = regions->SRV();
			ctx->CSSetShaderResources(0, 6, srvs);
			const unsigned dispatch[4]{ 0, 12, 0, 0 };
			ctx->UpdateSubresource(waveDispatch.get(), 0, nullptr, dispatch, 0, 0);
			uavs[2] = waveDispatchUAV.get();
			uavs[3] = waveWorkTiles->UAV();
			ctx->CSSetUnorderedAccessViews(2, 2, uavs + 2, nullptr);
			ctx->CSSetShader(buildWaveTiles.get(), nullptr, 0);
			ctx->Dispatch(((data.width + 15) / 16 + 7) / 8, ((data.height + 15) / 16 + 7) / 8, 1);
			uavs[1] = uavs[2] = uavs[3] = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			srvs[0] = waveSources->SRV();
			srvs[1] = waveTiles->SRV();
			srvs[2] = waveTextures[1]->srv.get();
			srvs[3] = waveWorkTiles->SRV();
			srvs[4] = waveDispatchSRV.get();
			ctx->CSSetShaderResources(0, 6, srvs);
			uavs[0] = waveRadiance->uav.get();
			ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			auto* linear = sampler.get();
			ctx->CSSetSamplers(0, 1, &linear);
			ctx->CSSetShader(buildWaveTail.get(), nullptr, 0);
			ctx->DispatchIndirect(waveDispatch.get(), 0);
			uavs[0] = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			srvs[0] = nullptr;
			srvs[1] = sources->SRV();
			srvs[2] = nullptr;
			srvs[3] = nullptr;
			srvs[4] = nullptr;
			ctx->CSSetShaderResources(0, 6, srvs);
			waveReady = true;
		}
		if (settings.Intensity > 0) {
			srvs[2] = paths->SRV();
			ctx->CSSetShaderResources(0, 6, srvs);
			const unsigned args[32]{ (PhysicalLensOptics::Vertices + 63) / 64, 0, 3, PhysicalLensOptics::BeamDrawVertices, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 3, 0,
				PhysicalLensOptics::BeamDrawVertices, 0, 0, 0, 0, PhysicalLensOptics::BeamDrawVertices, 0, 0, 0, 0, PhysicalLensOptics::BeamDrawVertices, 0, 0, 0, 0, 0 };
			ctx->UpdateSubresource(indirect.get(), 0, nullptr, args, 0, 0);
			uavs[2] = jobs->UAV();
			uavs[3] = indirectUAV.get();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			ctx->CSSetShader(buildJobs.get(), nullptr, 0);
			ctx->Dispatch(data.maxSources, 1, 1);
			uavs[3] = nullptr;
			ctx->CSSetUnorderedAccessViews(3, 1, &uavs[3], nullptr);
			// Cache only selected paths and neighbouring incidence anchors. Cold
			// frames use the same basis as warm frames; there is no late LOD switch.
			if (!planningValid || plannedSensorWidth != data.sensorWidth || plannedSensorHeight != data.sensorHeight) {
				const unsigned zero[4]{};
				ctx->ClearUnorderedAccessViewUint(planStatus->UAV(), zero);
				planningValid = true;
				plannedSensorWidth = data.sensorWidth;
				plannedSensorHeight = data.sensorHeight;
			}
			const unsigned bakeArgs[4]{ 0, 1, 1, 0 };
			ctx->UpdateSubresource(planArguments.get(), 0, nullptr, bakeArgs, 0, 0);
			uavs[2] = nullptr;
			uavs[0] = planStatus->UAV();
			uavs[3] = planArgumentsUAV.get();
			uavs[4] = planRequests->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			srvs[3] = jobs->SRV();
			ctx->CSSetShaderResources(3, 1, &srvs[3]);
			ctx->CSSetShader(requestPlans.get(), nullptr, 0);
			ctx->DispatchIndirect(indirect.get(), 32);
			uavs[0] = uavs[3] = uavs[4] = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			ID3D11ShaderResourceView* planView = nullptr;
			ctx->CSSetShaderResources(6, 1, &planView);
			srvs[0] = planRequests->SRV();
			srvs[3] = nullptr;
			ctx->CSSetShaderResources(0, 6, srvs);
			auto* planDestination = planTable->UAV();
			ctx->CSSetUnorderedAccessViews(5, 1, &planDestination, nullptr);
			ctx->CSSetShader(bakePlans.get(), nullptr, 0);
			ctx->DispatchIndirect(planArguments.get(), 0);
			planDestination = nullptr;
			ctx->CSSetUnorderedAccessViews(5, 1, &planDestination, nullptr);
			srvs[0] = nullptr;
			ctx->CSSetShaderResources(0, 1, srvs);
			uavs[2] = jobs->UAV();
			uavs[4] = pupils->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			planView = planTable->SRV();
			ctx->CSSetShaderResources(6, 1, &planView);
			ctx->CSSetShader(expandJobs.get(), nullptr, 0);
			ctx->DispatchIndirect(indirect.get(), 32);
			planView = nullptr;
			ctx->CSSetShaderResources(6, 1, &planView);
			uavs[4] = nullptr;
			uavs[2] = nullptr;
			uavs[3] = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			srvs[3] = jobs->SRV();
			srvs[4] = pupils->SRV();
			ctx->CSSetShaderResources(0, 6, srvs);
			uavs[1] = rays->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			ctx->CSSetShader(trace.get(), nullptr, 0);
			ctx->DispatchIndirect(indirect.get(), 0);
			ctx->CSSetShader(shade.get(), nullptr, 0);
			ctx->DispatchIndirect(indirect.get(), 0);
			lastGhostFrame = globals::state->frameCount;
			uavs[1] = nullptr;
			uavs[4] = bundles->UAV();
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			srvs[0] = rays->SRV();
			ctx->CSSetShaderResources(0, 1, srvs);
			ctx->CSSetShader(reduce.get(), nullptr, 0);
			ctx->DispatchIndirect(indirect.get(), 48);
			uavs[4] = nullptr;
			uavs[1] = nullptr;
			srvs[1] = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
			ctx->CSSetShaderResources(0, 6, srvs);
			const float clear[4]{};
			for (auto& target : channelTargets) ctx->ClearRenderTargetView(target.get(), clear);
			ctx->ClearRenderTargetView(blueViolet->rtv.get(), clear);
			raster.SetShaders(vs.get(), ps.get());
			raster.SetBlendState(additive.get(), clear);
			ctx->RSSetState(twoSided.get());
			ctx->VSSetConstantBuffers(0, 1, &cb);
			ctx->PSSetConstantBuffers(0, 1, &cb);
			auto* edgeView = ghostDiffraction->srv.get();
			auto* edgeSampler = sampler.get();
			ctx->PSSetShaderResources(5, 1, &edgeView);
			ctx->PSSetSamplers(0, 1, &edgeSampler);
			ctx->VSSetSamplers(0, 1, &edgeSampler);
			srvs[4] = bundles->SRV();
			srvs[0] = rays->SRV();
			srvs[1] = sources->SRV();
			srvs[5] = edgeView;
			ctx->VSSetShaderResources(0, 6, srvs);
			ctx->IASetIndexBuffer(splatIndices.get(), DXGI_FORMAT_R16_UINT, 0);
			for (unsigned channel = 0; channel < 3; ++channel) {
				if (channel == 2) {
					ctx->PSSetShader(violetPS.get(), nullptr, 0);
					raster.SetTargets({ blueViolet->rtv.get() }, data.outputWidth, data.outputHeight);
				} else
					raster.SetTargets({ channelTargets[channel].get() }, data.outputWidth, data.outputHeight);
				ID3D11Buffer* band = channelParameters[channel]->CB();
				ctx->VSSetConstantBuffers(5, 1, &band);
				ctx->PSSetConstantBuffers(5, 1, &band);
				ctx->DrawIndexedInstancedIndirect(indirect.get(), 12);
			}
			ctx->PSSetShader(areaPS.get(), nullptr, 0);
			for (unsigned level = 0; level < areaLayers.size(); ++level) {
				auto& target = areaLayers[level];
				ctx->ClearRenderTargetView(target->rtv.get(), clear);
				float width = float(std::max(1u, unsigned(data.outputWidth) >> (2 + 2 * level)) + 8);
				float height = float(std::max(1u, unsigned(data.outputHeight) >> (2 + 2 * level)) + 8);
				raster.SetTargets({ target->rtv.get() }, width, height);
				for (unsigned channel = 0; channel < 3; ++channel) {
					ID3D11Buffer* band = areaParameters[level * 3 + channel]->CB();
					ctx->VSSetConstantBuffers(5, 1, &band);
					ctx->PSSetConstantBuffers(5, 1, &band);
					ctx->DrawIndexedInstancedIndirect(indirect.get(), 64 + 20 * level);
				}
			}
			ctx->OMSetRenderTargets(0, nullptr, nullptr);
			srvs[0] = indirectSRV.get();
			srvs[1] = blueViolet->srv.get();
			for (unsigned level = 0; level < areaLayers.size(); ++level) srvs[2 + level] = areaLayers[level]->srv.get();
			uavs[0] = output->uav.get();
			uavs[1] = waveTiles->UAV();
			ctx->CSSetShaderResources(0, 5, srvs);
			ctx->CSSetSamplers(0, 1, &edgeSampler);
			ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			ctx->CSSetShader(resolve.get(), nullptr, 0);
			ctx->Dispatch((unsigned(data.outputWidth) + 7) / 8, (unsigned(data.outputHeight) + 7) / 8, 1);
		}
	}
	globals::profiler->EndPass();
	outputReady = settings.Intensity > 0;
}

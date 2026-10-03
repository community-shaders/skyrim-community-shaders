#include "DoF.h"

#include "Features/PostProcessing.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include "I18n/I18n.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DoF::Settings,
	VanillaCompatibility,
	AutoFocus,
	TransitionSpeed,
	FocusCoord,
	ManualFocusPlane,
	FocalLength,
	FNumber,
	SensorWidthMM,
	FarPlaneMaxBlur,
	NearPlaneMaxBlur,
	UseAdaptiveGather,
	GatherQuality,
	BokehMode,
	BokehBladeCount,
	BokehBladeRoundness,
	BlurQuality,
	NearFarDistanceCompensation,
	HighlightBoost,
	BokehBusyFactor,
	PostBlurSmoothing,
	PetzvalStrength,
	HighlightShape,
	HighlightShapeRotationAngle,
	MaxNearCoCRadius,
	MaxFarCoCRadius,
	targetFocus,
	targetFocusFocalLength,
	consoleSelection)

void DoF::DrawSettings()
{
	ImGui::Checkbox(T("feature.post_processing.do_f.vanilla_compatibility", "Vanilla Compatibility Mode"), &settings.VanillaCompatibility);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T("feature.post_processing.do_f.vanilla_compatibility_desc", "Matches the game's focus, range, strength and blur modes using Community Shaders DoF. Follows the game's DoF toggle and dynamic DoF settings. Underwater effects remain handled by the game. Overrides lens and bokeh controls without changing their saved values."));
	ImGui::BeginDisabled(settings.VanillaCompatibility);
	const auto* cam = owner && !settings.VanillaCompatibility ? owner->GetActivePhysicalCameraState() : nullptr;
	const bool camActive = cam != nullptr;

	float autoFocusCoord[2] = { settings.FocusCoord.x, settings.FocusCoord.y };
	float manualFocus = settings.ManualFocusPlane;
	float focalLength = settings.FocalLength;
	float fNumber = settings.FNumber;
	float sensorWidth = settings.SensorWidthMM;
	float transitionSpeed = settings.TransitionSpeed;
	int bladeCount = settings.BokehBladeCount;
	float bladeRoundness = settings.BokehBladeRoundness;
	float shapeRotation = settings.HighlightShapeRotationAngle;
	if (camActive) {
		autoFocusCoord[0] = cam->ScreenPointUV.x;
		autoFocusCoord[1] = cam->ScreenPointUV.y;
		manualFocus = cam->ManualDistanceM;
		focalLength = cam->FocalLengthMM;
		fNumber = cam->FNumber;
		sensorWidth = cam->EffectiveSensorWidthMM;
		transitionSpeed = cam->TransitionSpeed;
		bladeCount = cam->ApertureBladeCount;
		bladeRoundness = cam->ApertureRoundness;
		shapeRotation = std::fmod(cam->ApertureBladeRotationDeg, 360.0f) / 360.0f;
		ImGui::TextDisabled("%s", T("feature.post_processing.controlled_by_cinematic_camera", "Lens and focus are currently controlled by Cinematic Camera."));
	}

	ImGui::BeginDisabled(camActive);
	ImGui::Checkbox(T("feature.post_processing.do_f.auto_focus", "Auto Focus"), &settings.AutoFocus);

	if (settings.AutoFocus) {
		ImGui::SliderFloat2(T("feature.post_processing.do_f.focus_point", "Focus Point"), autoFocusCoord, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::SliderFloat(T("feature.post_processing.do_f.transition_speed", "Transition Speed"), &transitionSpeed, 0.1f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.manual_focus", "Manual Focus"), &manualFocus, 0.1f, 150.0f, "%.2f m");
	ImGui::SliderFloat(T("feature.post_processing.do_f.focal_length", "Focal Length"), &focalLength, 1.0f, 300.0f, "%.1f mm");
	ImGui::SliderFloat(T("feature.post_processing.do_f.f_number", "F-Number"), &fNumber, 1.0f, 22.0f, "f/%.1f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.sensor_width", "Sensor Width"), &sensorWidth, 1.0f, 100.0f, "%.1f mm", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.sensor_width_desc", "Horizontal sensor extent the focal length and F-number are expressed for. 36 mm matches a full frame camera."));
	ImGui::EndDisabled();
	if (!camActive) {
		settings.FocusCoord.x = autoFocusCoord[0];
		settings.FocusCoord.y = autoFocusCoord[1];
		settings.ManualFocusPlane = manualFocus;
		settings.TransitionSpeed = transitionSpeed;
	}
	ImGui::SliderFloat(T("feature.post_processing.do_f.far_plane_max_blur", "Far Plane Max Blur"), &settings.FarPlaneMaxBlur, 0.0f, 8.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.near_plane_max_blur", "Near Plane Max Blur"), &settings.NearPlaneMaxBlur, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.max_far_coc_radius", "Max Far Blur Radius"), &settings.MaxFarCoCRadius, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.max_far_coc_radius_desc", "Upper bound of the far field blur disc radius, as a fraction of the screen width. Caps how expensive/undersampled the gather can get."));
	ImGui::SliderFloat(T("feature.post_processing.do_f.max_near_coc_radius", "Max Near Blur Radius"), &settings.MaxNearCoCRadius, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.max_near_coc_radius_desc", "Upper bound of the near field blur disc radius, as a fraction of the screen width."));
	ImGui::EndDisabled();
	ImGui::Checkbox(T("feature.post_processing.do_f.adaptive_gather", "Adaptive Gather"), &settings.UseAdaptiveGather);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.adaptive_gather_desc", "Uses a fixed low-sample kernel and a CoC-aware image pyramid."));
	if (settings.UseAdaptiveGather)
		ImGui::Combo(T("feature.post_processing.do_f.gather_quality", "Gather Quality"), &settings.GatherQuality, "Performance (4 rings)\0Quality (5 rings)\0");
	if (!settings.UseAdaptiveGather)
		ImGui::SliderFloat(T("feature.post_processing.do_f.blur_quality", "Compatibility Blur Quality"), &settings.BlurQuality, 2.0f, 30.0f, "%.1f");
	ImGui::BeginDisabled(settings.VanillaCompatibility);
	ImGui::SliderFloat(T("feature.post_processing.do_f.near_far_plane_distance_compenation", "Near-Far Plane Distance Compenation"), &settings.NearFarDistanceCompensation, 1.0f, 5.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.bokeh_busy_factor", "Bokeh Busy Factor"), &settings.BokehBusyFactor, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.petzval_strength", "Petzval Strength"), &settings.PetzvalStrength, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.highlight_boost", "Highlight Boost"), &settings.HighlightBoost, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.post_blur_smoothing", "Post Blur Smoothing"), &settings.PostBlurSmoothing, 0.0f, 2.0f, "%.2f");
	ImGui::Combo(T("feature.post_processing.do_f.bokeh_mode", "Bokeh Mode"), &settings.BokehMode, "Procedural\0Custom Texture (Higher Cost)\0");
	if (settings.BokehMode == 0) {
		ImGui::BeginDisabled(camActive);
		ImGui::SliderInt(T("feature.post_processing.do_f.bokeh_blade_count", "Aperture Blades"), &bladeCount, 4, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.post_processing.do_f.bokeh_blade_roundness", "Blade Roundness"), &bladeRoundness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		if (!camActive) {
			settings.BokehBladeCount = bladeCount;
			settings.BokehBladeRoundness = bladeRoundness;
		}
		if (!settings.UseAdaptiveGather)
			ImGui::TextDisabled(T("feature.post_processing.do_f.procedural_requires_adaptive", "Procedural blades require Adaptive Gather; the compatibility path uses a circle."));
	} else if (owner) {
		const int shapeCount = owner->bokehResources.GetTotalShapeCount();
		const int shape = std::clamp(settings.HighlightShape, 1, std::max(shapeCount, 1));
		if (!settings.VanillaCompatibility)
			settings.HighlightShape = shape;
		const int selectedShape = shape - 1;
		if (ImGui::BeginCombo(T("feature.post_processing.do_f.highlight_custom_shape", "Custom Aperture Texture"), owner->bokehResources.GetShapeName(selectedShape))) {
			for (int i = 0; i < shapeCount; ++i) {
				const bool selected = i == selectedShape;
				if (ImGui::Selectable(owner->bokehResources.GetShapeName(i), selected))
					settings.HighlightShape = i + 1;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled(T("feature.post_processing.do_f.custom_shape_cost", "Custom textures preserve arbitrary silhouettes but add a texture lookup per gather tap."));
	}
	ImGui::BeginDisabled(camActive);
	ImGui::SliderFloat(T("feature.post_processing.do_f.highlight_shape_rotation", "Highlight Shape Rotation"), &shapeRotation, 0.0f, 1.0f, "%.2f");
	ImGui::EndDisabled();
	if (!camActive)
		settings.HighlightShapeRotationAngle = shapeRotation;
	ImGui::BeginDisabled(camActive);
	ImGui::Checkbox(T("feature.post_processing.do_f.target_focus", "Target Focus"), &settings.targetFocus);
	if (settings.targetFocus) {
		ImGui::SliderFloat(T("feature.post_processing.do_f.target_focus_focal_length", "Target Focus Focal Length"), &settings.targetFocusFocalLength, 1.0f, 300.0f, "%.1f mm");
	}
	ImGui::EndDisabled();
	ImGui::Checkbox(T("feature.post_processing.do_f.console_selection", "Console Selection"), &settings.consoleSelection);
	if (settings.consoleSelection && currentRef != 0) {
		ImGui::Text(T("feature.post_processing.do_f.selected_reference", "Selected Reference: %08X"), currentRef);
	}
	ImGui::EndDisabled();

	if (ImGui::CollapsingHeader(T("feature.post_processing.do_f.debug", "Debug"))) {
		static float debugRescale = .3f;
		ImGui::Text(T("feature.post_processing.do_f.debug_distance", "Debug Distance: %f"), debugDistance);
		ImGui::Text(T("feature.post_processing.do_f.debug_focus_plane", "Debug Focus Plane: %f"), debugFocusPlane);
		ImGui::SliderFloat(T("feature.post_processing.do_f.view_resize", "View Resize"), &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texFocus, 64.0f)
		BUFFER_VIEWER_NODE(texPreFocus, 64.0f)

		BUFFER_VIEWER_NODE(texCoC, debugRescale)
		BUFFER_VIEWER_NODE(texCoCHalf, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTile, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTileTmp, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTileDilated, debugRescale)
		BUFFER_VIEWER_NODE(texPreBlurred, debugRescale)
		BUFFER_VIEWER_NODE(texGatherColor[0], debugRescale)
		BUFFER_VIEWER_NODE(texGatherColor[1], debugRescale)
		BUFFER_VIEWER_NODE(texGatherColor[2], debugRescale)
		BUFFER_VIEWER_NODE(texGatherCoC[0], debugRescale)
		BUFFER_VIEWER_NODE(texGatherCoC[1], debugRescale)
		BUFFER_VIEWER_NODE(texGatherCoC[2], debugRescale)
		BUFFER_VIEWER_NODE(texFarBlurred, debugRescale)
		BUFFER_VIEWER_NODE(texNearBlurred, debugRescale)

		BUFFER_VIEWER_NODE(texBlurredFiltered, debugRescale)
		BUFFER_VIEWER_NODE(texPostSmooth, debugRescale)
		BUFFER_VIEWER_NODE(texPostSmooth2, debugRescale)
	}
}

void DoF::RestoreDefaultSettings()
{
	settings = {};
}

void DoF::LoadSettings(json& o_json)
{
	settings = o_json;
}

void DoF::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DoF::UpdateProceduralBokehSamples(int bladeCount, float bladeRoundness, bool force)
{
	if (!proceduralBokehSamples)
		return;

	bladeCount = std::clamp(bladeCount, 4, 16);
	const float roundness = std::clamp(bladeRoundness, 0.0f, 1.0f);
	if (!force && bladeCount == cachedBokehBladeCount && roundness == cachedBokehBladeRoundness)
		return;

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float tau = 2.0f * pi;
	const float sector = tau / float(bladeCount);
	const float circumRadius = std::sqrt((2.0f * pi) / (float(bladeCount) * std::sin(sector)));
	const float incircleRadius = circumRadius * std::cos(pi / float(bladeCount));
	auto boundaryRadius = [&](float angle) {
		const float edgeNormal = (std::floor(angle / sector) + 0.5f) * sector;
		const float alpha = std::remainder(angle - edgeNormal, sector);
		const float polygonRadius = incircleRadius / std::max(std::cos(alpha), 1e-4f);
		return std::lerp(polygonRadius, 1.0f, roundness);
	};

	// Linear interpolation between a regular polygon and a circle needs a small area correction at
	// intermediate roundness. Integrating the radial function keeps blur energy independent of UI.
	constexpr int integrationSteps = 2048;
	float twiceArea = 0.0f;
	for (int i = 0; i < integrationSteps; ++i) {
		const float angle = (float(i) + 0.5f) * tau / float(integrationSteps);
		const float radius = boundaryRadius(angle);
		twiceArea += radius * radius * tau / float(integrationSteps);
	}
	const float areaScale = std::sqrt((2.0f * pi) / std::max(twiceArea, 1e-4f));
	proceduralBokehAreaScale = areaScale;

	std::array<BokehResources::ShapeSample, BokehResources::GATHER_SAMPLE_COUNT> samples{};
	int sampleIndex = 0;
	float maxRadius = 1.0f;
	for (int ring = 1; ring <= 5; ++ring) {
		const int samplesOnRing = ring * 8;
		// Leave half a sample footprint outside the last ring. This places the ring centers at
		// r/(ringCount+0.5), so bilinear footprints cover the aperture edge instead of piling their
		// centers directly onto it.
		const float ringRadius = float(ring) / 5.5f;
		const float angleOffset = (ring & 1) ? pi / float(samplesOnRing) : 0.0f;
		for (int i = 0; i < samplesOnRing; ++i) {
			const float angle = angleOffset + float(i) * tau / float(samplesOnRing);
			const float radius = ringRadius * boundaryRadius(angle) * areaScale;
			const float fourRingScale = ring <= 4 ? (5.5f / 4.5f) : 1.0f;
			maxRadius = std::max(maxRadius, radius * fourRingScale);
			samples[sampleIndex++] = {
				std::cos(angle) * radius,
				std::sin(angle) * radius,
				float(ring - 1) / 5.0f,
				ringRadius
			};
		}
	}

	proceduralBokehSamples->Update(samples.data(), sizeof(samples));
	const float analyticMaxRadius = std::lerp(circumRadius, 1.0f, roundness) * areaScale;
	proceduralBokehMaxRadius = std::max(maxRadius, analyticMaxRadius);
	cachedBokehBladeCount = bladeCount;
	cachedBokehBladeRoundness = roundness;
}

void DoF::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		dofCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<DoFCB>(), "DoF::Constants");
		proceduralBokehSamples = eastl::make_unique<StructuredBuffer>(
			StructuredBufferDesc<BokehResources::ShapeSample>((uint64_t)BokehResources::GATHER_SAMPLE_COUNT, false, true),
			BokehResources::GATHER_SAMPLE_COUNT,
			"DoF::ProceduralBokehSamples");
		proceduralBokehSamples->CreateSRV();
		UpdateProceduralBokehSamples(settings.BokehBladeCount, settings.BokehBladeRoundness, true);
	}

	logger::debug("Creating 2D textures...");
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

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc, "DoF::Output");
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);

		texPostSmooth = eastl::make_unique<Texture2D>(texDesc, "DoF::PostSmooth");
		texPostSmooth->CreateSRV(srvDesc);
		texPostSmooth->CreateUAV(uavDesc);

		texPostSmooth2 = eastl::make_unique<Texture2D>(texDesc, "DoF::PostSmooth2");
		texPostSmooth2->CreateSRV(srvDesc);
		texPostSmooth2->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC texDescHalf = texDesc;
		texDescHalf.Width = std::max(1u, texDescHalf.Width / 2u);
		texDescHalf.Height = std::max(1u, texDescHalf.Height / 2u);

		texPreBlurred = eastl::make_unique<Texture2D>(texDescHalf, "DoF::SetupColor");
		texPreBlurred->CreateSRV(srvDesc);
		texPreBlurred->CreateUAV(uavDesc);

		texFarBlurred = eastl::make_unique<Texture2D>(texDescHalf, "DoF::FarLayer");
		texFarBlurred->CreateSRV(srvDesc);
		texFarBlurred->CreateUAV(uavDesc);

		texNearBlurred = eastl::make_unique<Texture2D>(texDescHalf, "DoF::NearLayer");
		texNearBlurred->CreateSRV(srvDesc);
		texNearBlurred->CreateUAV(uavDesc);

		texBlurredFiltered = eastl::make_unique<Texture2D>(texDescHalf, "DoF::FarFiltered");
		texBlurredFiltered->CreateSRV(srvDesc);
		texBlurredFiltered->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC texDescGather = texDescHalf;
		for (size_t i = 0; i < texGatherColor.size(); ++i) {
			texDescGather.Width = std::max(1u, (texDescGather.Width + 1u) / 2u);
			texDescGather.Height = std::max(1u, (texDescGather.Height + 1u) / 2u);
			texGatherColor[i] = eastl::make_unique<Texture2D>(texDescGather, std::format("DoF::GatherColor{}", i + 1).c_str());
			texGatherColor[i]->CreateSRV(srvDesc);
			texGatherColor[i]->CreateUAV(uavDesc);
		}

		// CoC buffers. R16_FLOAT is plenty: the CoC is a screen width fraction clamped to ~0.025, so
		// half float resolves it to better than 1/40th of a pixel.
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDescHalf.Format = DXGI_FORMAT_R16_FLOAT;

		texCoC = eastl::make_unique<Texture2D>(texDesc, "DoF::FullCoC");
		texCoC->CreateSRV(srvDesc);
		texCoC->CreateUAV(uavDesc);

		texCoCHalf = eastl::make_unique<Texture2D>(texDescHalf, "DoF::SetupCoC");
		texCoCHalf->CreateSRV(srvDesc);
		texCoCHalf->CreateUAV(uavDesc);

		texDescGather = texDescHalf;
		for (size_t i = 0; i < texGatherCoC.size(); ++i) {
			texDescGather.Width = std::max(1u, (texDescGather.Width + 1u) / 2u);
			texDescGather.Height = std::max(1u, (texDescGather.Height + 1u) / 2u);
			texDescGather.Format = DXGI_FORMAT_R16_FLOAT;
			texGatherCoC[i] = eastl::make_unique<Texture2D>(texDescGather, std::format("DoF::GatherCoC{}", i + 1).c_str());
			texGatherCoC[i]->CreateSRV(srvDesc);
			texGatherCoC[i]->CreateUAV(uavDesc);
		}

		// CoC tile buffers: one texel per 16x16 full res pixels, holding (min, max) signed CoC.
		// RGBA16F rather than RG16F because only the RGBA/R/RG32 families are guaranteed to support
		// typed UAV stores on D3D11 feature level 11_0 hardware.
		D3D11_TEXTURE2D_DESC texDescTile = texDesc;
		texDescTile.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDescTile.Width = std::max(1u, (texDesc.Width / 2 + 7) / 8);
		texDescTile.Height = std::max(1u, (texDesc.Height / 2 + 7) / 8);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDescTile = srvDesc;
		srvDescTile.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescTile = uavDesc;
		uavDescTile.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		texCoCTile = eastl::make_unique<Texture2D>(texDescTile, "DoF::CoCTile");
		texCoCTile->CreateSRV(srvDescTile);
		texCoCTile->CreateUAV(uavDescTile);

		texCoCTileTmp = eastl::make_unique<Texture2D>(texDescTile, "DoF::CoCTileTemporary");
		texCoCTileTmp->CreateSRV(srvDescTile);
		texCoCTileTmp->CreateUAV(uavDescTile);

		texCoCTileDilated = eastl::make_unique<Texture2D>(texDescTile, "DoF::CoCTileDilated");
		texCoCTileDilated->CreateSRV(srvDescTile);
		texCoCTileDilated->CreateUAV(uavDescTile);

		// The 1x1 focus texture stores a distance in km and wants the full float range.
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.Width = 1;
		texDesc.Height = 1;

		texFocus = eastl::make_unique<Texture2D>(texDesc, "DoF::Focus");
		texFocus->CreateSRV(srvDesc);
		texFocus->CreateUAV(uavDesc);

		texPreFocus = eastl::make_unique<Texture2D>(texDesc, "DoF::PreviousFocus");
		texPreFocus->CreateSRV(srvDesc);
		texPreFocus->CreateUAV(uavDesc);
	}

	// Bokeh shapes are loaded by PostProcessing::bokehResources (shared with LensFlare)

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR,
			.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR,
			.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));
		Util::SetResourceName(linearSampler.get(), "DoF::LinearSampler");
	}

	CompileComputeShaders();
}

void DoF::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&UpdateFocusCS,
		&CalculateCoCCS,
		&CoCTileFlattenCS,
		&CoCTileDilateHCS,
		&CoCTileDilateVCS,
		&DownsampleCS,
		&DownsampleLegacyCS,
		&ReduceColorCoCCS,
		&ReduceColorCS,
		&FarBlurCS,
		&NearBlurCS,
		&FarGatherCS[0],
		&FarGatherCS[1],
		&NearGatherCS[0],
		&NearGatherCS[1],
		&GatherPostfilterCS,
		&CombinerCS,
		&PostSmoothing1CS,
		&PostSmoothing2AndFocusingCS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/DoF");
	CompileComputeShaders();
}

void DoF::CompileComputeShaders()
{
	const std::vector<ComputeShaderCompileInfo>
		shaderInfos = {
			{ &UpdateFocusCS, "dof.cs.hlsl", {}, "CS_UpdateFocus" },
			{ &CalculateCoCCS, "dof.cs.hlsl", {}, "CS_CalculateCoC" },
			{ &CoCTileFlattenCS, "dof.cs.hlsl", {}, "CS_CoCTileFlatten" },
			{ &CoCTileDilateHCS, "dof.cs.hlsl", {}, "CS_CoCTileDilateH" },
			{ &CoCTileDilateVCS, "dof.cs.hlsl", {}, "CS_CoCTileDilateV" },
			{ &DownsampleCS, "dof.cs.hlsl", {}, "CS_Downsample" },
			{ &DownsampleLegacyCS, "dof.cs.hlsl", {}, "CS_DownsampleLegacy" },
			{ &ReduceColorCoCCS, "dof.cs.hlsl", {}, "CS_ReduceColorCoC" },
			{ &ReduceColorCS, "dof.cs.hlsl", {}, "CS_ReduceColor" },
			{ &FarBlurCS, "dof.cs.hlsl", {}, "CS_FarBlur" },
			{ &NearBlurCS, "dof.cs.hlsl", {}, "CS_NearBlur" },
			{ &FarGatherCS[0], "dof.cs.hlsl", { { "GATHER_RING_COUNT", "4" } }, "CS_FarGather" },
			{ &FarGatherCS[1], "dof.cs.hlsl", { { "GATHER_RING_COUNT", "5" } }, "CS_FarGather" },
			{ &NearGatherCS[0], "dof.cs.hlsl", { { "GATHER_RING_COUNT", "4" } }, "CS_NearGather" },
			{ &NearGatherCS[1], "dof.cs.hlsl", { { "GATHER_RING_COUNT", "5" } }, "CS_NearGather" },
			{ &GatherPostfilterCS, "dof.cs.hlsl", {}, "CS_GatherPostfilter" },
			{ &CombinerCS, "dof.cs.hlsl", {}, "CS_Combiner" },
			{ &PostSmoothing1CS, "dof.cs.hlsl", {}, "CS_PostSmoothing1" },
			{ &PostSmoothing2AndFocusingCS, "dof.cs.hlsl", {}, "CS_PostSmoothing2AndFocusing" }
		};

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\DoF", shaderInfos);
}

// Focus target resolution (camera position, reference projection, dialogue and
// TDM lookup) lives in CinematicCamera::FocusResolver, shared with the
// Cinematic Camera focus modes. See CinematicCamera.cpp.

namespace
{
	RE::ImageSpaceEffectDepthOfField* GetVanillaDoF()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		const auto index = RE::ImageSpaceManager::GetCurrentIndex(RE::ImageSpaceManager::DepthOfField);
		return manager && index < manager->effects.capacity() ?
		           static_cast<RE::ImageSpaceEffectDepthOfField*>(manager->effects[index]) :
		           nullptr;
	}

	float GetVanillaDoFFloat(const char* name, float fallback)
	{
		const auto* setting = RE::GetINISetting(name);
		const float value = setting ? setting->GetFloat() : fallback;
		return std::isfinite(value) ? value : fallback;
	}

	struct VanillaDoF_UpdateParams
	{
		static bool thunk(RE::ImageSpaceEffectDepthOfField* effect, RE::ImageSpaceEffectParam* param)
		{
			const bool result = func(effect, param);
			auto* dof = globals::features::postProcessing.GetPipelineFeature<DoF>(PostProcessing::FeaturePipelineIndex::DoF);
			if (result && dof && dof->CanUseVanillaCompatibility() && effect->effectParams.capacity() > 2) {
				auto* shaderParam = static_cast<RE::ImageSpaceShaderParam*>(effect->effectParams[2]);
				if (shaderParam && shaderParam->pixelConstantGroup && shaderParam->pixelConstantGroupSize >= 16) {
					// Keep the native depth/focus and blur preparation passes alive, but prevent
					// the final composite from blurring the scene a second time. UpdateParams
					// rebuilds these constants each time; never modify the game's ImageSpace data.
					auto* constants = shaderParam->pixelConstantGroup;
					std::copy_n(constants, dof->vanillaConstants.size(), dof->vanillaConstants.begin());
					dof->vanillaConstantsFrame = globals::state->frameCount;
					constants[8] = 0.0f;  // params2.x: strength
					if (constants[14] > 0.0f)
						constants[11] = 0.0f;  // dynamic far-focus strength (otherwise the sky flag)
				}
			}
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void DoF::InstallHooks()
{
	stl::write_vfunc<0x7, VanillaDoF_UpdateParams>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
}

bool DoF::CanUseVanillaCompatibility() const
{
	if (!settings.VanillaCompatibility || !enabled || !owner || !owner->loaded || owner->bypass ||
		owner->IsTonemapOwnedByEffects11() || globals::state->IsMainOrLoadingMenuOpen() ||
		globals::state->permutationData.RenderToUI || !texOutput || !dofCB)
		return false;
	const auto* renderer = globals::game::renderer;
	if (!renderer || !renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV)
		return false;
	auto* effect = GetVanillaDoF();
	// Fogged and masked underwater composites have their own ImageSpace and depth
	// semantics. Leave those passes entirely native, including waterline transitions.
	if (!effect || !effect->IsActive() || effect->options.capacity() < 5 ||
		effect->options[3] || effect->options[4])
		return false;
	if (effect->effectParams.capacity() <= 2)
		return false;
	const auto* param = static_cast<RE::ImageSpaceShaderParam*>(effect->effectParams[2]);
	if (!param || !param->pixelConstantGroup || param->pixelConstantGroupSize < 16)
		return false;
	const auto& data = RE::ImageSpaceManager::GetSingleton()->GetImageSpaceData().modData.data;
	for (auto index : { RE::ImageSpaceModData::kDOFStrength, RE::ImageSpaceModData::kDOFDistance,
			 RE::ImageSpaceModData::kDOFRange, RE::ImageSpaceModData::kDOFMode }) {
		if (!std::isfinite(data[index]))
			return false;
	}
	if (data[RE::ImageSpaceModData::kDOFMode] < 0.0f || data[RE::ImageSpaceModData::kDOFMode] > 255.0f)
		return false;
	return AllShadersReady({ &UpdateFocusCS, &CalculateCoCCS, &CoCTileFlattenCS,
		&CoCTileDilateHCS, &CoCTileDilateVCS, &DownsampleLegacyCS, &FarBlurCS, &NearBlurCS,
		&GatherPostfilterCS, &CombinerCS });
}

void DoF::Draw(TextureInfo& inout_tex)
{
	const bool vanilla = settings.VanillaCompatibility;
	if (vanilla && !CanUseVanillaCompatibility())
		return;
	Settings renderSettings = settings;
	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
	if (!depthSRV) {
		return;
	}

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	uint vanillaMode = 0;
	float vanillaRange = 0.0f;
	float4 vanillaDynamic{};
	float2 vanillaBlur{};
	float4 vanillaDepthPlanes{};
	ID3D11ShaderResourceView* vanillaFocusSRV = nullptr;
	if (vanilla) {
		// UpdateParams reads these resolved values directly, without multiplying by
		// modAmount or blending currentBaseData again (which would lose IMAD overrides).
		const auto& data = RE::ImageSpaceManager::GetSingleton()->GetImageSpaceData().modData.data;
		vanillaMode = static_cast<uint>(data[RE::ImageSpaceModData::kDOFMode]);
		const bool dynamicFocus = (vanillaMode & 0x80u) != 0;
		const auto toKM = [](float value) { return Util::Units::GameUnitsToMeters(value) * 0.001f; };
		const auto cameraData = Util::GetCameraData();
		vanillaDepthPlanes = { cameraData.y, cameraData.x, GetVanillaDoF()->unk14C, GetVanillaDoF()->unk150 };
		vanillaRange = toKM(std::max(data[RE::ImageSpaceModData::kDOFRange], 0.0f));
		vanillaBlur = float2(std::max(data[RE::ImageSpaceModData::kDOFStrength], 0.0f));
		if (dynamicFocus) {
			vanillaDynamic = {
				toKM(GetVanillaDoFFloat("fDynamicDOFNearDist:Display", 100.0f)),
				toKM(GetVanillaDoFFloat("fDynamicDOFFarDist:Display", 1000.0f)),
				toKM(GetVanillaDoFFloat("fDynamicDOFNearRange:Display", 100.0f)),
				toKM(GetVanillaDoFFloat("fDynamicDOFFarRange:Display", 10000.0f))
			};
			const float multiplier = GetVanillaDoFFloat("fDynamicDOFBlurMultiplier:Display", 0.8f);
			vanillaBlur = {
				GetVanillaDoFFloat("fDynamicDOFNearBlur:Display", 1.0f) * multiplier,
				GetVanillaDoFFloat("fDynamicDOFFarBlur:Display", 0.7f) * multiplier
			};
			// BorrowTextures selects the same average-depth target that the native
			// compositor samples. Its descriptor survives ReturnTextures; no readback
			// or extra focus interpolation is needed. On the first frame use the center.
			const auto target = GetVanillaDoF()->unk0C8[3].renderTarget;
			if (target >= RE::RENDER_TARGETS::kSAO_CAMERAZ && target <= RE::RENDER_TARGETS::kSAO_CAMERAZ_MIP_LEVEL_11)
				vanillaFocusSRV = renderer->GetRuntimeData().renderTargets[target].SRV;
		}
		const uint encodedRadius = (vanillaMode >> 3u) & 0xfu;
		const float radius = float(encodedRadius ? encodedRadius : 3u);
		// Match the native blur's downsampled footprint in viewport units. The
		// Gaussian-to-disc conversion remains approximate; do not make it depend
		// on the user's saved physical lens or maximum CoC settings.
		float blurWidth = res.x * 0.5f;
		const auto borrowedTarget = GetVanillaDoF()->unk0C8[0].renderTarget;
		const auto blurTarget = borrowedTarget == RE::RENDER_TARGETS::kLDR_DOWNSAMPLE0 ? borrowedTarget : RE::RENDER_TARGETS::kHDR_DOWNSAMPLE0;
		auto* nativeBlur = renderer->GetRuntimeData().renderTargets[blurTarget].texture;
		if (nativeBlur) {
			D3D11_TEXTURE2D_DESC desc{};
			nativeBlur->GetDesc(&desc);
			blurWidth = float(std::max(desc.Width, 1u));
		}
		renderSettings.MaxNearCoCRadius = renderSettings.MaxFarCoCRadius = std::clamp(radius / blurWidth, 1e-4f, 0.1f);
		renderSettings.NearPlaneMaxBlur = (vanillaMode & 3u) < 2u ? 1.0f : 0.0f;
		renderSettings.FarPlaneMaxBlur = (vanillaMode & 1u) == 0u ? 1.0f : 0.0f;
		renderSettings.AutoFocus = dynamicFocus;
		renderSettings.FocusCoord = { 0.5f, 0.5f };
		renderSettings.ManualFocusPlane = Util::Units::GameUnitsToMeters(std::max(data[RE::ImageSpaceModData::kDOFDistance], 0.0f));
		renderSettings.TransitionSpeed = 1.0f;
		renderSettings.targetFocus = false;
		renderSettings.BokehMode = 0;
		renderSettings.BokehBladeRoundness = 1.0f;
		renderSettings.NearFarDistanceCompensation = 1.0f;
		renderSettings.BokehBusyFactor = 0.5f;
		renderSettings.HighlightBoost = 0.0f;
		renderSettings.PostBlurSmoothing = 0.0f;
		renderSettings.PetzvalStrength = 0.0f;
		if (vanillaConstantsFrame == state->frameCount) {
			// HDR DoF runs before our tonemap hook: use the actual constants, including
			// runtime ConfigureDDOF changes. LDR DoF runs later and uses the INI fallback.
			const auto& p = vanillaConstants;
			vanillaDepthPlanes = { p[6], p[7], p[12], p[13] };
			if (dynamicFocus && p[14] > 0.0f) {
				vanillaDynamic = { toKM(p[15] / p[14]), toKM((1.0f + p[15]) / p[14]), toKM(p[4]), toKM(p[5]) };
				vanillaBlur = { p[8], p[11] };
			}
		}
	}

	float focusLen = renderSettings.FocalLength;
	float fNumber = renderSettings.FNumber;
	float sensorWidthMM = renderSettings.SensorWidthMM;
	float transitionSpeed = renderSettings.TransitionSpeed;
	float nearBlur = renderSettings.NearPlaneMaxBlur;
	float manualFocus = renderSettings.ManualFocusPlane / 1000.0f;
	float2 focusCoord = renderSettings.FocusCoord;
	debugFocusPlane = manualFocus;
	bool autoFocus = renderSettings.AutoFocus;
	int bladeCount = renderSettings.BokehBladeCount;
	float bladeRoundness = renderSettings.BokehBladeRoundness;

	const auto* cam = owner && !vanilla ? owner->GetActivePhysicalCameraState() : nullptr;
	CinematicCamera::FocusResolver* resolver = owner ? &owner->GetCinematicCamera().focusResolver : nullptr;
	if (cam && resolver) {
		focusLen = cam->FocalLengthMM;
		fNumber = cam->FNumber;
		sensorWidthMM = cam->EffectiveSensorWidthMM;
		transitionSpeed = cam->TransitionSpeed;
		manualFocus = cam->ManualDistanceM / 1000.0f;
		focusCoord = cam->ScreenPointUV;
		bladeCount = cam->ApertureBladeCount;
		bladeRoundness = cam->ApertureRoundness;
		autoFocus = false;

		switch (cam->Mode) {
		case CinematicCamera::FocusMode::Manual:
			break;
		case CinematicCamera::FocusMode::ScreenPoint:
			autoFocus = true;
			break;
		case CinematicCamera::FocusMode::Target:
			{
				// Target focus keeps the DoF console-selection preference. Target mode
				// never swaps the focal length; the unified lens always wins.
				auto result = resolver->ResolveTarget(renderSettings.consoleSelection, currentRef);
				if (result.hasTarget) {
					if (result.projected) {
						autoFocus = true;
						focusCoord = result.focusCoord;
					} else {
						manualFocus = result.distanceM / 1000.0f;
					}
				}
				break;
			}
		default:
			break;
		}
		debugFocusPlane = manualFocus;
	} else if (renderSettings.targetFocus && resolver) {
		focusLen = 1.0f;
		nearBlur = 0.0f;
		float targetFocusDistanceGame = 0;
		autoFocus = false;

		RE::TESObjectREFR* target = resolver->FindTarget(renderSettings.consoleSelection, currentRef);
		if (!target)
			return;

		targetFocusDistanceGame = resolver->GetDistanceToReference(target);
		debugDistance = targetFocusDistanceGame;
		nearBlur = renderSettings.NearPlaneMaxBlur;
		focusLen = renderSettings.targetFocusFocalLength;
		if (resolver->GetReferenceFocusCoord(target, focusCoord)) {
			// Sample the visible surface at the projected head/object position. This matches the
			// view-space depth convention used by the CoC pass and avoids focusing behind a face.
			autoFocus = true;
		} else {
			manualFocus = Util::Units::GameUnitsToMeters(targetFocusDistanceGame) * 0.001f;  // in KM
		}
		debugFocusPlane = manualFocus;
	}
	// No-op the whole frame until the core kernels are ready -- a partial
	// sequential pipeline would write garbage into the scene target.
	const bool needPostSmoothing = renderSettings.PostBlurSmoothing >= 0.01f;
	const bool coreReady = AllShadersReady({ &UpdateFocusCS, &CalculateCoCCS, &CoCTileFlattenCS,
		&CoCTileDilateHCS, &CoCTileDilateVCS, &DownsampleLegacyCS, &FarBlurCS, &NearBlurCS,
		&GatherPostfilterCS, &CombinerCS });
	if (!coreReady || (needPostSmoothing && !AllShadersReady({ &PostSmoothing1CS, &PostSmoothing2AndFocusingCS })))
		return;
	state->BeginPerfEvent("Depth of Field");

	const uint halfResX = std::max(1u, (uint)res.x / 2);
	const uint halfResY = std::max(1u, (uint)res.y / 2);
	const uint tileDimX = std::max(1u, (halfResX + 7) / 8);
	const uint tileDimY = std::max(1u, (halfResY + 7) / 8);
	const size_t gatherQuality = (size_t)std::clamp(renderSettings.GatherQuality, 0, 1);
	UpdateProceduralBokehSamples(bladeCount, bladeRoundness);

	const int requestedBokehMode = std::clamp(renderSettings.BokehMode, 0, 1);
	int customShapeIndex = 0;
	ID3D11ShaderResourceView* customShapeSRV = nullptr;
	ID3D11ShaderResourceView* customShapeSampleSRV = nullptr;
	if (owner && requestedBokehMode == 1) {
		customShapeIndex = std::clamp(renderSettings.HighlightShape - 1, 0, std::max(owner->bokehResources.GetTotalShapeCount() - 1, 0));
		customShapeSRV = owner->bokehResources.GetShapeSRV(customShapeIndex);
		customShapeSampleSRV = owner->bokehResources.GetShapeSampleSRV(customShapeIndex);
	}
	const uint bokehMode = requestedBokehMode == 1 && customShapeSRV && customShapeSampleSRV ? 1u : 0u;
	ID3D11ShaderResourceView* bokehSampleSRV = bokehMode == 1 ? customShapeSampleSRV : proceduralBokehSamples->SRV();
	const float customShapeRadiusScale = bokehMode == 1 ? owner->bokehResources.GetShapeSampleRadiusScale(customShapeIndex) : 1.0f;
	const float bokehMaxRadius = bokehMode == 1 ? owner->bokehResources.GetShapeSampleMaxRadius(customShapeIndex) : proceduralBokehMaxRadius;
	const bool adaptiveGatherReady = bokehSampleSRV &&
	                                 AllShadersReady({ &DownsampleCS, &ReduceColorCoCCS, &ReduceColorCS,
										 &FarGatherCS[gatherQuality], &NearGatherCS[gatherQuality] });
	const bool useAdaptiveGather = renderSettings.UseAdaptiveGather && adaptiveGatherReady;

	// Tile propagation carries the near disc reach itself, so the same low-resolution dilation used
	// for group culling now replaces the two old half-resolution Gaussian passes.
	const float wantNearRadiusPx = std::max(renderSettings.MaxNearCoCRadius, 1e-4f) * res.x * std::max(nearBlur, 0.0f) * bokehMaxRadius;
	constexpr float conservativeTileStepPx = 16.0f * 0.70710678f;
	const uint tileDilateRadius = wantNearRadiusPx > 0.0f ? std::min(48u, (uint)std::ceil(wantNearRadiusPx / conservativeTileStepPx) + 1u) : 0u;
	const float nearMaxReachPx = tileDilateRadius > 0u ? std::min(wantNearRadiusPx, (float)(tileDilateRadius - 1u) * conservativeTileStepPx) : 0.0f;

	DoFCB dofData = {
		.TransitionSpeed = transitionSpeed,
		.FocusCoord = focusCoord,
		.ManualFocusPlane = manualFocus,
		.FocalLength = focusLen,
		.FNumber = fNumber,
		.FarPlaneMaxBlur = renderSettings.FarPlaneMaxBlur,
		.NearPlaneMaxBlur = nearBlur,
		.BlurQuality = renderSettings.BlurQuality,
		.NearFarDistanceCompensation = renderSettings.NearFarDistanceCompensation,
		.BokehBusyFactor = renderSettings.BokehBusyFactor,
		.HighlightBoost = renderSettings.HighlightBoost,
		.PostBlurSmoothing = renderSettings.PostBlurSmoothing,
		.HighlightShape = bokehMode == 1 ? (uint)renderSettings.HighlightShape : 0u,
		.HighlightShapeRotationAngle = cam ? std::fmod(cam->ApertureBladeRotationDeg, 360.0f) / 360.0f : renderSettings.HighlightShapeRotationAngle,
		.PetzvalStrength = renderSettings.PetzvalStrength,
		.AutoFocus = autoFocus,
		.MaxNearCoCRadius = std::max(renderSettings.MaxNearCoCRadius, 1e-4f),
		.MaxFarCoCRadius = std::max(renderSettings.MaxFarCoCRadius, 1e-4f),
		.TileDilateRadius = tileDilateRadius,
		.CoCTileDimX = tileDimX,
		.CoCTileDimY = tileDimY,
		.HalfResDimX = halfResX,
		.HalfResDimY = halfResY,
		.BokehMode = bokehMode,
		.CustomShapeRadiusScale = customShapeRadiusScale,
		.BokehMaxRadius = bokehMaxRadius,
		.NearMaxReachPx = nearMaxReachPx,
		.BokehBladeCount = (uint)std::clamp(bladeCount, 4, 16),
		.BokehBladeRoundness = std::clamp(bladeRoundness, 0.0f, 1.0f),
		.ProceduralBokehAreaScale = proceduralBokehAreaScale,
		.SensorWidthMM = std::max(sensorWidthMM, 1.0f),
		.VanillaCompatibility = vanilla,
		.VanillaMode = vanillaMode,
		.VanillaUseFocusTexture = vanillaFocusSRV != nullptr,
		.VanillaRange = vanillaRange,
		.VanillaDynamic = vanillaDynamic,
		.VanillaBlur = vanillaBlur,
		.VanillaDynamicFocus = vanilla && renderSettings.AutoFocus,
		.pad = 0.0f,
		.VanillaDepthPlanes = vanillaDepthPlanes
	};
	dofCB->Update(dofData);

	std::array<ID3D11ShaderResourceView*, 21> srvs = {};
	std::array<ID3D11UnorderedAccessView*, 4> uavs = {};
	std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };
	auto cb = dofCB->CB();
	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	uint dispatchWidth = ((uint)res.x + 7) >> 3;
	uint dispatchHeight = ((uint)res.y + 7) >> 3;
	uint dispatchWidthBlur = (halfResX + 7) >> 3;
	uint dispatchHeightBlur = (halfResY + 7) >> 3;
	uint dispatchWidthTile = (tileDimX + 7) >> 3;
	uint dispatchHeightTile = (tileDimY + 7) >> 3;

	// Update Focus
	{
		srvs.at(0) = inout_tex.srv;
		srvs.at(1) = texPreFocus->srv.get();
		srvs.at(2) = depthSRV;
		srvs.at(20) = vanillaFocusSRV;
		uavs.at(1) = texFocus->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(UpdateFocusCS.get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
	}

	resetViews();
	context->CopyResource(texPreFocus->resource.get(), texFocus->resource.get());

	// Calculate CoC
	{
		globals::profiler->BeginPass("PostProcessing::DoF::CoC");
		state->BeginPerfEvent("Calculate CoC");
		srvs.at(0) = inout_tex.srv;
		srvs.at(1) = texPreFocus->srv.get();
		srvs.at(2) = depthSRV;
		uavs.at(2) = texCoC->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CalculateCoCCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	resetViews();

	// Half res downsample of colour + CoC (bilateral). Everything the gather kernels read is half res
	// from here on.
	{
		globals::profiler->BeginPass("PostProcessing::DoF::Downsample");
		state->BeginPerfEvent("Downsample");
		srvs.at(0) = inout_tex.srv;
		srvs.at(3) = texCoC->srv.get();
		uavs.at(0) = texPreBlurred->uav.get();
		uavs.at(2) = texCoCHalf->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(useAdaptiveGather ? DownsampleCS.get() : DownsampleLegacyCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// CoC-aware color pyramid for the fixed gather. Each tap can cover a footprint comparable to
	// the spacing between samples instead of always reading a single half-resolution texel.
	if (useAdaptiveGather) {
		globals::profiler->BeginPass("PostProcessing::DoF::GatherReduce");
		state->BeginPerfEvent("Gather Reduce");
		for (size_t i = 0; i < texGatherColor.size(); ++i) {
			auto* sourceColor = i == 0 ? texPreBlurred.get() : texGatherColor[i - 1].get();
			auto* sourceCoC = i == 0 ? texCoCHalf.get() : texGatherCoC[i - 1].get();
			srvs.at(0) = sourceColor->srv.get();
			srvs.at(18) = sourceCoC->srv.get();
			uavs.at(0) = texGatherColor[i]->uav.get();
			uavs.at(2) = texGatherCoC[i]->uav.get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(ReduceColorCoCCS.get(), nullptr, 0);
			context->Dispatch((texGatherColor[i]->desc.Width + 7u) >> 3, (texGatherColor[i]->desc.Height + 7u) >> 3, 1);
			resetViews();
		}
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// CoC tile flatten + separable min/max/reach propagation, at 1/16 of full resolution.
	{
		globals::profiler->BeginPass("PostProcessing::DoF::CoCTile");
		state->BeginPerfEvent("CoC Tile");
		srvs.at(3) = texCoC->srv.get();
		uavs.at(3) = texCoCTile->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTileFlattenCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthTile, dispatchHeightTile, 1);

		resetViews();

		srvs.at(10) = texCoCTile->srv.get();
		uavs.at(3) = texCoCTileTmp->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTileDilateHCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthTile, dispatchHeightTile, 1);

		resetViews();

		srvs.at(10) = texCoCTileTmp->srv.get();
		uavs.at(3) = texCoCTileDilated->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTileDilateVCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthTile, dispatchHeightTile, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// Gather
	{
		globals::profiler->BeginPass("PostProcessing::DoF::FarBlur");
		state->BeginPerfEvent("Far Blur");
		srvs.at(0) = texPreBlurred->srv.get();
		srvs.at(9) = texCoCHalf->srv.get();
		srvs.at(10) = texCoCTile->srv.get();
		if (useAdaptiveGather) {
			for (size_t i = 0; i < texGatherColor.size(); ++i) {
				srvs.at(12 + i) = texGatherColor[i]->srv.get();
				srvs.at(15 + i) = texGatherCoC[i]->srv.get();
			}
			srvs.at(19) = bokehSampleSRV;
			if (bokehMode == 1)
				srvs.at(8) = customShapeSRV;
		} else if (bokehMode == 1) {
			srvs.at(8) = customShapeSRV;
		}
		uavs.at(0) = texFarBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(useAdaptiveGather ? FarGatherCS[gatherQuality].get() : FarBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();

		srvs.at(0) = texFarBlurred->srv.get();
		srvs.at(9) = texCoCHalf->srv.get();
		srvs.at(11) = texCoCTileDilated->srv.get();
		if (useAdaptiveGather) {
			// The near layer gathers the already resolved far result, so give it a matching color-only
			// pyramid while reusing the setup CoC pyramid for footprint selection.
			globals::profiler->BeginPass("PostProcessing::DoF::GatherReduceNear");
			state->BeginPerfEvent("Gather Reduce Near");
			for (size_t i = 0; i < texGatherColor.size(); ++i) {
				auto* sourceColor = i == 0 ? texFarBlurred.get() : texGatherColor[i - 1].get();
				srvs.at(0) = sourceColor->srv.get();
				uavs.at(0) = texGatherColor[i]->uav.get();
				context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
				context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
				context->CSSetShader(ReduceColorCS.get(), nullptr, 0);
				context->Dispatch((texGatherColor[i]->desc.Width + 7u) >> 3, (texGatherColor[i]->desc.Height + 7u) >> 3, 1);
				resetViews();
			}
			state->EndPerfEvent();
			globals::profiler->EndPass();
			srvs.at(0) = texFarBlurred->srv.get();
			srvs.at(9) = texCoCHalf->srv.get();
			srvs.at(11) = texCoCTileDilated->srv.get();
			for (size_t i = 0; i < texGatherColor.size(); ++i) {
				srvs.at(12 + i) = texGatherColor[i]->srv.get();
				srvs.at(15 + i) = texGatherCoC[i]->srv.get();
			}
			srvs.at(19) = bokehSampleSRV;
			if (bokehMode == 1)
				srvs.at(8) = customShapeSRV;
		} else if (bokehMode == 1) {
			srvs.at(8) = customShapeSRV;
		}

		globals::profiler->BeginPass("PostProcessing::DoF::NearBlur");
		state->BeginPerfEvent("Near Blur");
		uavs.at(0) = texNearBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(useAdaptiveGather ? NearGatherCS[gatherQuality].get() : NearBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// Component-wise median removes isolated gather noise without rounding off the aperture as the
	// old tent blur did. Apply it to both convolution layers; setup color is dead after near gather
	// and serves as the near-layer destination without another allocation.
	{
		globals::profiler->BeginPass("PostProcessing::DoF::GatherPostfilter");
		state->BeginPerfEvent("Gather Postfilter");
		srvs.at(0) = texFarBlurred->srv.get();
		srvs.at(6) = texNearBlurred->srv.get();
		uavs.at(0) = texBlurredFiltered->uav.get();
		uavs.at(3) = texPreBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(GatherPostfilterCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// Combiner
	{
		globals::profiler->BeginPass("PostProcessing::DoF::Combiner");
		state->BeginPerfEvent("Combiner");
		srvs.at(0) = inout_tex.srv;
		srvs.at(3) = texCoC->srv.get();
		srvs.at(5) = texBlurredFiltered->srv.get();
		srvs.at(6) = texPreBlurred->srv.get();
		uavs.at(0) = needPostSmoothing ? texPostSmooth->uav.get() : texOutput->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CombinerCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// Post Smoothing only touches out of focus highlights; when it's disabled the combiner
	// already wrote straight into the output above, saving two full res passes.
	if (needPostSmoothing) {
		globals::profiler->BeginPass("PostProcessing::DoF::PostSmooth");
		state->BeginPerfEvent("Post Smooth");
		srvs.at(0) = texPostSmooth->srv.get();
		srvs.at(3) = texCoC->srv.get();
		uavs.at(0) = texPostSmooth2->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(PostSmoothing1CS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();

		srvs.at(0) = texPostSmooth->srv.get();
		srvs.at(3) = texCoC->srv.get();
		srvs.at(7) = texPostSmooth2->srv.get();
		uavs.at(0) = texOutput->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(PostSmoothing2AndFocusingCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
}

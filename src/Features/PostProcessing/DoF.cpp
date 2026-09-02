#include "DoF.h"

#include "Features/PostProcessing.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include "I18n/I18n.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DoF::Settings,
	AutoFocus,
	TransitionSpeed,
	FocusCoord,
	ManualFocusPlane,
	FocalLength,
	FNumber,
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
	ImGui::Checkbox(T("feature.post_processing.do_f.auto_focus", "Auto Focus"), &settings.AutoFocus);

	if (settings.AutoFocus) {
		ImGui::SliderFloat2(T("feature.post_processing.do_f.focus_point", "Focus Point"), &settings.FocusCoord.x, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::SliderFloat(T("feature.post_processing.do_f.transition_speed", "Transition Speed"), &settings.TransitionSpeed, 0.1f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.manual_focus", "Manual Focus"), &settings.ManualFocusPlane, 0.1f, 150.0f, "%.2f m");
	ImGui::SliderFloat(T("feature.post_processing.do_f.focal_length", "Focal Length"), &settings.FocalLength, 1.0f, 300.0f, "%.1f mm");
	ImGui::SliderFloat(T("feature.post_processing.do_f.f_number", "F-Number"), &settings.FNumber, 1.0f, 22.0f, "f/%.1f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.far_plane_max_blur", "Far Plane Max Blur"), &settings.FarPlaneMaxBlur, 0.0f, 8.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.near_plane_max_blur", "Near Plane Max Blur"), &settings.NearPlaneMaxBlur, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.max_far_coc_radius", "Max Far Blur Radius"), &settings.MaxFarCoCRadius, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.max_far_coc_radius_desc", "Upper bound of the far field blur disc radius, as a fraction of the screen width. Caps how expensive/undersampled the gather can get."));
	ImGui::SliderFloat(T("feature.post_processing.do_f.max_near_coc_radius", "Max Near Blur Radius"), &settings.MaxNearCoCRadius, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.max_near_coc_radius_desc", "Upper bound of the near field blur disc radius, as a fraction of the screen width."));
	ImGui::Checkbox(T("feature.post_processing.do_f.adaptive_gather", "Adaptive Gather"), &settings.UseAdaptiveGather);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.do_f.adaptive_gather_desc", "Uses a fixed low-sample kernel and a CoC-aware image pyramid."));
	if (settings.UseAdaptiveGather)
		ImGui::Combo(T("feature.post_processing.do_f.gather_quality", "Gather Quality"), &settings.GatherQuality, "Performance (4 rings)\0Quality (5 rings)\0");
	if (!settings.UseAdaptiveGather)
		ImGui::SliderFloat(T("feature.post_processing.do_f.blur_quality", "Compatibility Blur Quality"), &settings.BlurQuality, 2.0f, 30.0f, "%.1f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.near_far_plane_distance_compenation", "Near-Far Plane Distance Compenation"), &settings.NearFarDistanceCompensation, 1.0f, 5.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.bokeh_busy_factor", "Bokeh Busy Factor"), &settings.BokehBusyFactor, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.petzval_strength", "Petzval Strength"), &settings.PetzvalStrength, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.highlight_boost", "Highlight Boost"), &settings.HighlightBoost, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.do_f.post_blur_smoothing", "Post Blur Smoothing"), &settings.PostBlurSmoothing, 0.0f, 2.0f, "%.2f");
	ImGui::Combo(T("feature.post_processing.do_f.bokeh_mode", "Bokeh Mode"), &settings.BokehMode, "Procedural\0Custom Texture (Higher Cost)\0");
	if (settings.BokehMode == 0) {
		ImGui::SliderInt(T("feature.post_processing.do_f.bokeh_blade_count", "Aperture Blades"), &settings.BokehBladeCount, 4, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.post_processing.do_f.bokeh_blade_roundness", "Blade Roundness"), &settings.BokehBladeRoundness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (!settings.UseAdaptiveGather)
			ImGui::TextDisabled(T("feature.post_processing.do_f.procedural_requires_adaptive", "Procedural blades require Adaptive Gather; the compatibility path uses a circle."));
	} else if (owner) {
		const int shapeCount = owner->bokehResources.GetTotalShapeCount();
		settings.HighlightShape = std::clamp(settings.HighlightShape, 1, std::max(shapeCount, 1));
		const int selectedShape = settings.HighlightShape - 1;
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
	ImGui::SliderFloat(T("feature.post_processing.do_f.highlight_shape_rotation", "Highlight Shape Rotation"), &settings.HighlightShapeRotationAngle, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox(T("feature.post_processing.do_f.target_focus", "Target Focus"), &settings.targetFocus);
	ImGui::SliderFloat(T("feature.post_processing.do_f.target_focus_focal_length", "Target Focus Focal Length"), &settings.targetFocusFocalLength, 1.0f, 300.0f, "%.1f mm");
	ImGui::Checkbox(T("feature.post_processing.do_f.console_selection", "Console Selection"), &settings.consoleSelection);
	if (settings.consoleSelection && currentRef != 0) {
		ImGui::Text(T("feature.post_processing.do_f.selected_reference", "Selected Reference: %08X"), currentRef);
	}

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

void DoF::UpdateProceduralBokehSamples(bool force)
{
	if (!proceduralBokehSamples)
		return;

	const int bladeCount = std::clamp(settings.BokehBladeCount, 4, 16);
	const float roundness = std::clamp(settings.BokehBladeRoundness, 0.0f, 1.0f);
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
		UpdateProceduralBokehSamples(true);
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

		g_TDM = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V2));
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

// Thanks Ershin!
RE::NiPoint3 DoF::GetCameraPos()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	auto playerCamera = RE::PlayerCamera::GetSingleton();
	RE::NiPoint3 ret;

	if (playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kFirstPerson] ||
		playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kThirdPerson] ||
		playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kMount]) {
		RE::NiNode* root = playerCamera->cameraRoot.get();
		if (root) {
			ret.x = root->world.translate.x;
			ret.y = root->world.translate.y;
			ret.z = root->world.translate.z;
		}
	} else if (playerCamera->IsInFreeCameraMode()) {
		auto freeCameraState = static_cast<RE::FreeCameraState*>(playerCamera->currentState.get());
		ret = freeCameraState->translation;
	} else {
		RE::NiPoint3 playerPos = player->GetLookingAtLocation();

		ret.z = playerPos.z;
		ret.x = player->GetPositionX();
		ret.y = player->GetPositionY();
	}

	return ret;
}

bool DoF::GetTargetLockEnabled()
{
	return g_TDM && g_TDM->GetCurrentTarget();
}

bool DoF::GetInDialogue()
{
	return RE::MenuTopicManager::GetSingleton()->speaker || RE::MenuTopicManager::GetSingleton()->lastSpeaker;
}

float DoF::GetDistanceToReference(RE::TESObjectREFR* a_ref)
{
	auto* camera = RE::Main::WorldRootCamera();
	RE::NiPoint3 cameraPosition = camera ? camera->world.translate : GetCameraPos();
	return cameraPosition.GetDistance(GetReferenceFocusPosition(a_ref));
}

RE::NiPoint3 DoF::GetReferenceFocusPosition(RE::TESObjectREFR* a_ref)
{
	RE::NiPoint3 targetPosition = a_ref->GetPosition();
	if (a_ref->GetFormType() == RE::FormType::ActorCharacter) {
		auto head = a_ref->GetNodeByName("NPC Head [Head]");
		if (head)
			targetPosition = head->world.translate;
	}
	return targetPosition;
}

bool DoF::GetReferenceFocusCoord(RE::TESObjectREFR* a_ref, float2& a_focusCoord)
{
	auto* camera = RE::Main::WorldRootCamera();
	if (!camera)
		return false;

	float screenX = 0.0f;
	float screenY = 0.0f;
	float screenZ = 0.0f;
	if (!camera->WorldPtToScreenPt3(GetReferenceFocusPosition(a_ref), screenX, screenY, screenZ, 1e-5f) ||
		!std::isfinite(screenX) || !std::isfinite(screenY) || screenZ <= 0.0f ||
		screenX < 0.0f || screenX > 1.0f || screenY < 0.0f || screenY > 1.0f) {
		return false;
	}

	// WorldPtToScreenPt3 uses a bottom-left origin; texture UVs use a top-left origin.
	a_focusCoord = float2(screenX, 1.0f - screenY);
	return true;
}

void DoF::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
	if (!depthSRV) {
		return;
	}

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };

	float focusLen = settings.FocalLength;
	float nearBlur = settings.NearPlaneMaxBlur;
	float manualFocus = settings.ManualFocusPlane / 1000.0f;
	float2 focusCoord = settings.FocusCoord;
	debugFocusPlane = manualFocus;
	bool autoFocus = settings.AutoFocus;

	if (settings.targetFocus) {
		focusLen = 1.0f;
		nearBlur = 0.0f;
		float targetFocusDistanceGame = 0;
		autoFocus = false;

		RE::TESObjectREFR* target = nullptr;
		const auto consoleRef = RE::Console::GetSelectedRef();
		if (settings.consoleSelection)
			if (consoleRef && !consoleRef->IsDisabled() && !consoleRef->IsDeleted() && consoleRef->Is3DLoaded()) {
				currentRef = consoleRef->formID;
				target = consoleRef.get();
			} else {
				currentRef = 0;
			}

		if (GetTargetLockEnabled()) {
			target = g_TDM->GetCurrentTarget().get().get();
		}

		if (GetInDialogue()) {
			if (RE::MenuTopicManager::GetSingleton()->speaker) {
				target = RE::MenuTopicManager::GetSingleton()->speaker.get().get();
			} else {
				target = RE::MenuTopicManager::GetSingleton()->lastSpeaker.get().get();
			}
		}
		if (!target)
			return;

		targetFocusDistanceGame = GetDistanceToReference(target);
		debugDistance = targetFocusDistanceGame;
		nearBlur = settings.NearPlaneMaxBlur;
		focusLen = settings.targetFocusFocalLength;
		if (GetReferenceFocusCoord(target, focusCoord)) {
			// Sample the visible surface at the projected head/object position. This matches the
			// view-space depth convention used by the CoC pass and avoids focusing behind a face.
			autoFocus = true;
		} else {
			manualFocus = Util::Units::GameUnitsToMeters(targetFocusDistanceGame) * 0.001f;  // in KM
		}
	}
	debugFocusPlane = manualFocus;
	// No-op the whole frame until the core kernels are ready -- a partial
	// sequential pipeline would write garbage into the scene target.
	const bool needPostSmoothing = settings.PostBlurSmoothing >= 0.01f;
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
	const size_t gatherQuality = (size_t)std::clamp(settings.GatherQuality, 0, 1);
	UpdateProceduralBokehSamples();

	const int requestedBokehMode = std::clamp(settings.BokehMode, 0, 1);
	int customShapeIndex = 0;
	ID3D11ShaderResourceView* customShapeSRV = nullptr;
	ID3D11ShaderResourceView* customShapeSampleSRV = nullptr;
	if (owner && requestedBokehMode == 1) {
		customShapeIndex = std::clamp(settings.HighlightShape - 1, 0, std::max(owner->bokehResources.GetTotalShapeCount() - 1, 0));
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
	const bool useAdaptiveGather = settings.UseAdaptiveGather && adaptiveGatherReady;

	// Tile propagation carries the near disc reach itself, so the same low-resolution dilation used
	// for group culling now replaces the two old half-resolution Gaussian passes.
	const float wantNearRadiusPx = std::max(settings.MaxNearCoCRadius, 1e-4f) * res.x * std::max(nearBlur, 0.0f) * bokehMaxRadius;
	constexpr float conservativeTileStepPx = 16.0f * 0.70710678f;
	const uint tileDilateRadius = wantNearRadiusPx > 0.0f ? std::min(48u, (uint)std::ceil(wantNearRadiusPx / conservativeTileStepPx) + 1u) : 0u;
	const float nearMaxReachPx = tileDilateRadius > 0u ? std::min(wantNearRadiusPx, (float)(tileDilateRadius - 1u) * conservativeTileStepPx) : 0.0f;

	DoFCB dofData = {
		.TransitionSpeed = settings.TransitionSpeed,
		.FocusCoord = focusCoord,
		.ManualFocusPlane = manualFocus,
		.FocalLength = focusLen,
		.FNumber = settings.FNumber,
		.FarPlaneMaxBlur = settings.FarPlaneMaxBlur,
		.NearPlaneMaxBlur = nearBlur,
		.BlurQuality = settings.BlurQuality,
		.NearFarDistanceCompensation = settings.NearFarDistanceCompensation,
		.BokehBusyFactor = settings.BokehBusyFactor,
		.HighlightBoost = settings.HighlightBoost,
		.PostBlurSmoothing = settings.PostBlurSmoothing,
		.HighlightShape = bokehMode == 1 ? (uint)settings.HighlightShape : 0u,
		.HighlightShapeRotationAngle = settings.HighlightShapeRotationAngle,
		.PetzvalStrength = settings.PetzvalStrength,
		.AutoFocus = autoFocus,
		.MaxNearCoCRadius = std::max(settings.MaxNearCoCRadius, 1e-4f),
		.MaxFarCoCRadius = std::max(settings.MaxFarCoCRadius, 1e-4f),
		.TileDilateRadius = tileDilateRadius,
		.CoCTileDimX = tileDimX,
		.CoCTileDimY = tileDimY,
		.HalfResDimX = halfResX,
		.HalfResDimY = halfResY,
		.BokehMode = bokehMode,
		.CustomShapeRadiusScale = customShapeRadiusScale,
		.BokehMaxRadius = bokehMaxRadius,
		.NearMaxReachPx = nearMaxReachPx,
		.BokehBladeCount = (uint)std::clamp(settings.BokehBladeCount, 4, 16),
		.BokehBladeRoundness = std::clamp(settings.BokehBladeRoundness, 0.0f, 1.0f),
		.ProceduralBokehAreaScale = proceduralBokehAreaScale,
		.pad = 0
	};
	dofCB->Update(dofData);

	std::array<ID3D11ShaderResourceView*, 20> srvs = {};
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

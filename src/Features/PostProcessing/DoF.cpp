#include "DoF.h"

#include "Menu.h"
#include "State.h"
#include "Util.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

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
	BlurQuality,
	NearFarDistanceCompensation,
	HighlightBoost,
	BokehBusyFactor,
	PostBlurSmoothing,
	HighlightShape,
	HighlightShapeRotationAngle,
	targetFocus,
	targetFocusFocalLength,
	consoleSelection)

void DoF::DrawSettings()
{
	ImGui::Checkbox("Auto Focus", &settings.AutoFocus);

	if (settings.AutoFocus) {
		ImGui::SliderFloat2("Focus Point", &settings.FocusCoord.x, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::SliderFloat("Transition Speed", &settings.TransitionSpeed, 0.1f, 1.0f, "%.2f");
	ImGui::SliderFloat("Manual Focus", &settings.ManualFocusPlane, 0.1f, 150.0f, "%.2f m");
	ImGui::SliderFloat("Focal Length", &settings.FocalLength, 1.0f, 300.0f, "%.1f mm");
	ImGui::SliderFloat("F-Number", &settings.FNumber, 1.0f, 22.0f, "f/%.1f");
	ImGui::SliderFloat("Far Plane Max Blur", &settings.FarPlaneMaxBlur, 0.0f, 8.0f, "%.2f");
	ImGui::SliderFloat("Near Plane Max Blur", &settings.NearPlaneMaxBlur, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat("Blur Quality", &settings.BlurQuality, 2.0f, 30.0f, "%.1f");
	ImGui::SliderFloat("Near-Far Plane Distance Compenation", &settings.NearFarDistanceCompensation, 1.0f, 5.0f, "%.2f");
	ImGui::SliderFloat("Bokeh Busy Factor", &settings.BokehBusyFactor, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Highlight Boost", &settings.HighlightBoost, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Post Blur Smoothing", &settings.PostBlurSmoothing, 0.0f, 2.0f, "%.2f");
	ImGui::Combo("Highlight Custom Shape", &settings.HighlightShape, "Circle (No custom shape)\0Heart\0Hexagon\0Circle with fringe\0Hexagon with fringe\0Star\0Square\0");
	ImGui::SliderFloat("Highlight Shape Rotation", &settings.HighlightShapeRotationAngle, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Target Focus", &settings.targetFocus);
	ImGui::SliderFloat("Target Focus Focal Length", &settings.targetFocusFocalLength, 1.0f, 300.0f, "%.1f mm");
	ImGui::Checkbox("Console Selection", &settings.consoleSelection);
	if (settings.consoleSelection && currentRef != 0) {
		ImGui::Text("Selected Reference: %08X", currentRef);
	}

	if (ImGui::CollapsingHeader("Debug")) {
		static float debugRescale = .3f;
		ImGui::Text("Debug Distance: %f", debugDistance);
		ImGui::Text("Debug Focus Plane: %f", debugFocusPlane);
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texFocus, 64.0f)
		BUFFER_VIEWER_NODE(texPreFocus, 64.0f)
		BUFFER_VIEWER_NODE(texBokehShapes[std::clamp((settings.HighlightShape - 1), 0, (int)texBokehShapes.size() - 1)], 1.f)

		BUFFER_VIEWER_NODE(texCoC, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTileTmp, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTileTmp2, debugRescale)
		BUFFER_VIEWER_NODE(texCoCTileNeighbor, debugRescale)
		BUFFER_VIEWER_NODE(texCoCBlur1, debugRescale)
		BUFFER_VIEWER_NODE(texCoCBlur2, debugRescale)

		BUFFER_VIEWER_NODE(texPreBlurred, debugRescale)
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

void DoF::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		dofCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<DoFCB>());
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

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);

		texBlurredFull = eastl::make_unique<Texture2D>(texDesc);
		texBlurredFull->CreateSRV(srvDesc);
		texBlurredFull->CreateUAV(uavDesc);

		texPostSmooth = eastl::make_unique<Texture2D>(texDesc);
		texPostSmooth->CreateSRV(srvDesc);
		texPostSmooth->CreateUAV(uavDesc);

		texPostSmooth2 = eastl::make_unique<Texture2D>(texDesc);
		texPostSmooth2->CreateSRV(srvDesc);
		texPostSmooth2->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC texDescHalf = texDesc;
		texDescHalf.Width /= 2;
		texDescHalf.Height /= 2;

		texPreBlurred = eastl::make_unique<Texture2D>(texDescHalf);
		texPreBlurred->CreateSRV(srvDesc);
		texPreBlurred->CreateUAV(uavDesc);

		texFarBlurred = eastl::make_unique<Texture2D>(texDescHalf);
		texFarBlurred->CreateSRV(srvDesc);
		texFarBlurred->CreateUAV(uavDesc);

		texNearBlurred = eastl::make_unique<Texture2D>(texDescHalf);
		texNearBlurred->CreateSRV(srvDesc);
		texNearBlurred->CreateUAV(uavDesc);

		texBlurredFiltered = eastl::make_unique<Texture2D>(texDescHalf);
		texBlurredFiltered->CreateSRV(srvDesc);
		texBlurredFiltered->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDescHalf.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;

		texCoC = eastl::make_unique<Texture2D>(texDesc);
		texCoC->CreateSRV(srvDesc);
		texCoC->CreateUAV(uavDesc);

		texCoCTileTmp = eastl::make_unique<Texture2D>(texDesc);
		texCoCTileTmp->CreateSRV(srvDesc);
		texCoCTileTmp->CreateUAV(uavDesc);

		texCoCTileTmp2 = eastl::make_unique<Texture2D>(texDesc);
		texCoCTileTmp2->CreateSRV(srvDesc);
		texCoCTileTmp2->CreateUAV(uavDesc);

		texCoCTileNeighbor = eastl::make_unique<Texture2D>(texDesc);
		texCoCTileNeighbor->CreateSRV(srvDesc);
		texCoCTileNeighbor->CreateUAV(uavDesc);

		texCoCBlur1 = eastl::make_unique<Texture2D>(texDescHalf);
		texCoCBlur1->CreateSRV(srvDesc);
		texCoCBlur1->CreateUAV(uavDesc);

		texCoCBlur2 = eastl::make_unique<Texture2D>(texDescHalf);
		texCoCBlur2->CreateSRV(srvDesc);
		texCoCBlur2->CreateUAV(uavDesc);

		texDesc.Width = 1;
		texDesc.Height = 1;

		texFocus = eastl::make_unique<Texture2D>(texDesc);
		texFocus->CreateSRV(srvDesc);
		texFocus->CreateUAV(uavDesc);

		texPreFocus = eastl::make_unique<Texture2D>(texDesc);
		texPreFocus->CreateSRV(srvDesc);
		texPreFocus->CreateUAV(uavDesc);

		g_TDM = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V2));
	}

	logger::debug("Loading Bokeh Shapes...");
	{
		for (int i = 0; i < bokehShapeFiles.size(); i++) {
			auto& shapeFile = bokehShapeFiles[i];
			auto shapePath = bokehShapesPath / shapeFile;

			DirectX::ScratchImage image;
			try {
				DX::ThrowIfFailed(DirectX::LoadFromWICFile(shapePath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image));
			} catch (std::runtime_error& e) {
				logger::warn("Error loading bokeh shape {}: {}", shapePath.string(), e.what());
			}

			ID3D11Resource* pRsrc = nullptr;
			try {
				DX::ThrowIfFailed(CreateTexture(device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), &pRsrc));
			} catch (std::runtime_error& e) {
				logger::warn("Error creating texture for bokeh shape {}: {}", shapePath.string(), e.what());
			}

			texBokehShapes[i] = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pRsrc));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
				.Format = texBokehShapes[i]->desc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};
			texBokehShapes[i]->CreateSRV(srvDesc);
		}
	}

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
	}

	CompileComputeShaders();
}

void DoF::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&UpdateFocusCS,
		&CalculateCoCCS,
		&CoCTile1CS,
		&CoCTile2CS,
		&CoCTileNeighbor,
		&CoCGaussian1CS,
		&CoCGaussian2CS,
		&BlurCS,
		&FarBlurCS,
		&NearBlurCS,
		&TentFilterCS,
		&CombinerCS,
		&PostSmoothing1CS,
		&PostSmoothing2AndFocusingCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void DoF::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &UpdateFocusCS, "dof.cs.hlsl", {}, "CS_UpdateFocus" },
			{ &CalculateCoCCS, "dof.cs.hlsl", {}, "CS_CalculateCoC" },
			{ &CoCTile1CS, "dof.cs.hlsl", {}, "CS_CoCTile1" },
			{ &CoCTile2CS, "dof.cs.hlsl", {}, "CS_CoCTile2" },
			{ &CoCTileNeighbor, "dof.cs.hlsl", {}, "CS_CoCTileNeighbor" },
			{ &CoCGaussian1CS, "dof.cs.hlsl", {}, "CS_CoCGaussian1" },
			{ &CoCGaussian2CS, "dof.cs.hlsl", {}, "CS_CoCGaussian2" },
			{ &BlurCS, "dof.cs.hlsl", {}, "CS_Blur" },
			{ &FarBlurCS, "dof.cs.hlsl", {}, "CS_FarBlur" },
			{ &NearBlurCS, "dof.cs.hlsl", {}, "CS_NearBlur" },
			{ &TentFilterCS, "dof.cs.hlsl", {}, "CS_TentFilter" },
			{ &CombinerCS, "dof.cs.hlsl", {}, "CS_Combiner" },
			{ &PostSmoothing1CS, "dof.cs.hlsl", {}, "CS_PostSmoothing1" },
			{ &PostSmoothing2AndFocusingCS, "dof.cs.hlsl", {}, "CS_PostSmoothing2AndFocusing" }
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\DoF") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
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
	RE::NiPoint3 cameraPosition = GetCameraPos();
	RE::NiPoint3 targetPosition = a_ref->GetPosition();
	if (a_ref->GetFormType() == RE::FormType::ActorCharacter && !a_ref->IsPlayer()) {
		auto head = a_ref->GetNodeByName("NPC Head [Head]");
		if (head) {
			targetPosition = head->world.translate;
		}
	}
	return cameraPosition.GetDistance(targetPosition);
}

void DoF::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };

	float focusLen = settings.FocalLength;
	float nearBlur = settings.NearPlaneMaxBlur;
	float manualFocus = settings.ManualFocusPlane / 1000.0f;
	debugFocusPlane = manualFocus;
	bool autoFocus = settings.AutoFocus;

	if (settings.targetFocus) {
		focusLen = 1.0f;
		nearBlur = 0.0f;
		float targetFocusDistanceGame = 0;
		auto targetFocusEnabled = false;
		autoFocus = false;

		RE::TESObjectREFR* target = nullptr;
		const auto consoleRef = RE::Console::GetSelectedRef();
		if (settings.consoleSelection)
			if (consoleRef && !consoleRef->IsDisabled() && !consoleRef->IsDeleted() && consoleRef->Is3DLoaded()) {
				currentRef = consoleRef->formID;
				target = consoleRef.get();
				targetFocusEnabled = true;
			} else {
				currentRef = 0;
			}

		if (GetTargetLockEnabled()) {
			target = g_TDM->GetCurrentTarget().get().get();
			targetFocusEnabled = true;
		}

		if (GetInDialogue()) {
			if (RE::MenuTopicManager::GetSingleton()->speaker) {
				target = RE::MenuTopicManager::GetSingleton()->speaker.get().get();
			} else {
				target = RE::MenuTopicManager::GetSingleton()->lastSpeaker.get().get();
			}
			targetFocusEnabled = true;
		}
		if (target)
			targetFocusDistanceGame = GetDistanceToReference(target);
		debugDistance = targetFocusDistanceGame;
		if (targetFocusEnabled) {
			nearBlur = settings.NearPlaneMaxBlur;
			focusLen = settings.targetFocusFocalLength;
			manualFocus = Util::Units::GameUnitsToMeters(targetFocusDistanceGame) * 0.001f; // in KM
		} else {
			return;
		}
	}
	debugFocusPlane = manualFocus;
	state->BeginPerfEvent("Depth of Field");
	DoFCB dofData = {
		.TransitionSpeed = settings.TransitionSpeed,
		.FocusCoord = settings.FocusCoord,
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
		.HighlightShape = (uint)settings.HighlightShape,
		.HighlightShapeRotationAngle = settings.HighlightShapeRotationAngle,
		.AutoFocus = autoFocus
	};
	dofCB->Update(dofData);

	std::array<ID3D11ShaderResourceView*, 9> srvs = { inout_tex.srv, texPreFocus->srv.get(), depth.depthSRV, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	std::array<ID3D11UnorderedAccessView*, 3> uavs = { texOutput->uav.get(), texFocus->uav.get(), texCoC->uav.get() };
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
	uint dispatchWidthBlur = ((uint)(res.x / 2) + 7) >> 3;
	uint dispatchHeightBlur = ((uint)(res.y / 2) + 7) >> 3;

	// Update Focus
	{
		srvs.at(0) = inout_tex.srv;
		srvs.at(1) = texPreFocus->srv.get();
		srvs.at(2) = depth.depthSRV;
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
		srvs.at(0) = inout_tex.srv;
		srvs.at(1) = texPreFocus->srv.get();
		srvs.at(2) = depth.depthSRV;
		uavs.at(2) = texCoC->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CalculateCoCCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);
	}

	resetViews();

	// CoC Tile
	{
		srvs.at(3) = texCoC->srv.get();
		uavs.at(2) = texCoCTileTmp->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTile1CS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();

		srvs.at(3) = texCoCTileTmp->srv.get();
		uavs.at(2) = texCoCTileTmp2->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTile2CS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();

		srvs.at(3) = texCoCTileTmp2->srv.get();
		uavs.at(2) = texCoCTileNeighbor->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCTileNeighbor.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();
	}

	// CoC Gaussian Blur (coc uses srv3 and uav2)
	{
		srvs.at(3) = texCoCTileNeighbor->srv.get();
		uavs.at(2) = texCoCBlur1->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCGaussian1CS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();

		srvs.at(3) = texCoCBlur1->srv.get();
		uavs.at(2) = texCoCBlur2->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CoCGaussian2CS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
	}

	// Blur
	{
		srvs.at(0) = inout_tex.srv;
		srvs.at(3) = texCoC->srv.get();
		srvs.at(4) = texCoCBlur2->srv.get();
		uavs.at(0) = texPreBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(BlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();

		srvs.at(0) = texPreBlurred->srv.get();
		srvs.at(3) = texCoC->srv.get();
		srvs.at(4) = texCoCBlur2->srv.get();
		srvs.at(8) = texBokehShapes[std::clamp((settings.HighlightShape - 1), 0, (int)texBokehShapes.size() - 1)]->srv.get();
		uavs.at(0) = texFarBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(FarBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();

		srvs.at(0) = texFarBlurred->srv.get();
		srvs.at(3) = texCoCTileNeighbor->srv.get();
		srvs.at(4) = texCoCBlur2->srv.get();
		srvs.at(8) = texBokehShapes[std::clamp((settings.HighlightShape - 1), 0, (int)texBokehShapes.size() - 1)]->srv.get();
		uavs.at(0) = texNearBlurred->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(NearBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
	}

	// Tent Filter
	{
		srvs.at(0) = texFarBlurred->srv.get();
		uavs.at(0) = texBlurredFiltered->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(TentFilterCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidthBlur, dispatchHeightBlur, 1);

		resetViews();
	}

	// Combiner
	{
		srvs.at(0) = inout_tex.srv;
		srvs.at(3) = texCoC->srv.get();
		srvs.at(5) = texBlurredFiltered->srv.get();
		srvs.at(6) = texNearBlurred->srv.get();
		uavs.at(0) = texPostSmooth->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		context->CSSetShader(CombinerCS.get(), nullptr, 0);
		context->Dispatch(dispatchWidth, dispatchHeight, 1);

		resetViews();
	}

	// Post Smooth
	{
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
	}

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
}
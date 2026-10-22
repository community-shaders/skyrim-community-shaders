#include "NRD.h"

#include "Deferred.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NRD::Settings,
	Enabled)

////////////////////////////////////////////////////////////////////////////////////

void NRD::RestoreDefaultSettings()
{
	settings = {};
}

void NRD::DrawSettings()
{
	ImGui::TextWrapped(
		"NRD provides shared denoising infrastructure for screen-space "
		"radiance signals. It is consumed by features such as Screen Space "
		"GI and Screen Space Reflections; toggle those features to control "
		"whether denoising actually runs.");

	ImGui::Separator();
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"When disabled, NRD skips the guide preparation pass and consumer "
			"features that depend on denoising will fall back to undenoised input.");
	}

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		if (texNRDViewZ)
			BUFFER_VIEWER_NODE(texNRDViewZ, debugRescale)
		if (texNRDNormalRoughness)
			BUFFER_VIEWER_NODE(texNRDNormalRoughness, debugRescale)
		if (texNRDMV)
			BUFFER_VIEWER_NODE(texNRDMV, debugRescale)

		ImGui::TreePop();
	}
}

void NRD::LoadSettings(json& o_json)
{
	settings = o_json;
}

void NRD::SaveSettings(json& o_json)
{
	o_json = settings;
}

void NRD::SetupResources()
{
	commonSettingsValidThisFrame = false;
	guidesReadyThisFrame = false;
	hasCommonFrameHistory = false;
	lastCommonGameFrame = 0;
	prevResourceSize[0] = prevResourceSize[1] = 0;
	prevRectSize[0] = prevRectSize[1] = 0;

	auto renderer = globals::game::renderer;
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);

	D3D11_TEXTURE2D_DESC texDesc{
		.Width = mainDesc.Width,
		.Height = mainDesc.Height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R16G16_FLOAT,
		.SampleDesc = { 1, 0 },
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

	texNRDMV = eastl::make_unique<Texture2D>(texDesc, "NRD::MV");
	texNRDMV->CreateSRV(srvDesc);
	texNRDMV->CreateUAV(uavDesc);

	srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texNRDViewZ = eastl::make_unique<Texture2D>(texDesc, "NRD::ViewZ");
	texNRDViewZ->CreateSRV(srvDesc);
	texNRDViewZ->CreateUAV(uavDesc);

	srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	texNRDNormalRoughness = eastl::make_unique<Texture2D>(texDesc, "NRD::NormalRoughness");
	texNRDNormalRoughness->CreateSRV(srvDesc);
	texNRDNormalRoughness->CreateUAV(uavDesc);

	CompileComputeShaders();
}

void NRD::ClearShaderCache()
{
	prepareNRDGuidesCompute = nullptr;
	CompileComputeShaders();
}

void NRD::CompileComputeShaders()
{
	std::vector<std::pair<const char*, const char*>> defines;

	auto path = std::filesystem::path("Data\\Shaders\\NRD") / "prepareNRDGuides.cs.hlsl";
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), defines, "cs_5_0")))
		prepareNRDGuidesCompute.attach(rawPtr);
}

void NRD::PrepareGuides()
{
	commonSettingsValidThisFrame = false;
	guidesReadyThisFrame = false;

	if (!settings.Enabled || !prepareNRDGuidesCompute || !texNRDViewZ || !texNRDNormalRoughness || !texNRDMV)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "NRD - Prepare Guides");

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto normal = rts[NORMALROUGHNESS];
	auto motion = rts[RE::RENDER_TARGETS::kMOTION_VECTOR];

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	float2 dynres = Util::ConvertToDynamic(screenSize);
	dynres = { floor(dynres.x), floor(dynres.y) };

	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);

	std::array<ID3D11ShaderResourceView*, 2> guideSRVs = {
		depth.depthSRV,
		normal.SRV
	};
	std::array<ID3D11UnorderedAccessView*, 2> guideUAVs = {
		texNRDViewZ->uav.get(),
		texNRDNormalRoughness->uav.get()
	};

	context->CSSetShaderResources(0, (uint)guideSRVs.size(), guideSRVs.data());
	context->CSSetUnorderedAccessViews(0, (uint)guideUAVs.size(), guideUAVs.data(), nullptr);
	context->CSSetShader(prepareNRDGuidesCompute.get(), nullptr, 0);
	context->Dispatch(((uint)dynres.x + 7) / 8, ((uint)dynres.y + 7) / 8, 1);

	std::array<ID3D11ShaderResourceView*, 2> nullSRVs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> nullUAVs = { nullptr };
	context->CSSetShaderResources(0, (uint)nullSRVs.size(), nullSRVs.data());
	context->CSSetUnorderedAccessViews(0, (uint)nullUAVs.size(), nullUAVs.data(), nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	// Motion Vector is used as both SRV and UAV by ReBLUR; snapshot the game's
	// MV target so we can rebind it through NRD's UAV slot without aliasing.
	context->CopyResource(texNRDMV->resource.get(), motion.texture);

	guidesReadyThisFrame = true;
}

const nrd::CommonSettings& NRD::GetCommonSettings()
{
	if (commonSettingsValidThisFrame)
		return commonSettings;

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	float2 dynres = Util::ConvertToDynamic(screenSize);
	dynres = { floor(dynres.x), floor(dynres.y) };

	auto fullW = texNRDViewZ ? texNRDViewZ->desc.Width : (uint32_t)screenSize.x;
	auto fullH = texNRDViewZ ? texNRDViewZ->desc.Height : (uint32_t)screenSize.y;

	commonSettings = nrd::CommonSettings{};
	const uint16_t resourceSize[2] = { (uint16_t)fullW, (uint16_t)fullH };
	const uint16_t rectSize[2] = { (uint16_t)dynres.x, (uint16_t)dynres.y };
	const bool resourceSizeChanged = hasCommonFrameHistory &&
	                                 (resourceSize[0] != prevResourceSize[0] || resourceSize[1] != prevResourceSize[1]);

	commonSettings.resourceSize[0] = resourceSize[0];
	commonSettings.resourceSize[1] = resourceSize[1];
	commonSettings.resourceSizePrev[0] = hasCommonFrameHistory ? prevResourceSize[0] : resourceSize[0];
	commonSettings.resourceSizePrev[1] = hasCommonFrameHistory ? prevResourceSize[1] : resourceSize[1];
	commonSettings.rectSize[0] = rectSize[0];
	commonSettings.rectSize[1] = rectSize[1];
	commonSettings.rectSizePrev[0] = hasCommonFrameHistory ? prevRectSize[0] : rectSize[0];
	commonSettings.rectSizePrev[1] = hasCommonFrameHistory ? prevRectSize[1] : rectSize[1];

	auto viewMat = globals::game::frameBufferCached.GetCameraView().Transpose();
	auto projMat = globals::game::frameBufferCached.GetCameraProj().Transpose();

	float3 cameraWorldPos = float3(globals::game::frameBufferCached.GetCameraPosAdjust());
	DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslation(-cameraWorldPos.x, -cameraWorldPos.y, -cameraWorldPos.z);
	worldToViewMat = DirectX::XMMatrixMultiply(translationMat, viewMat);

	memcpy(commonSettings.viewToClipMatrix, &projMat, sizeof(float) * 16);
	memcpy(commonSettings.viewToClipMatrixPrev, &prevProjMatrix, sizeof(float) * 16);
	memcpy(commonSettings.worldToViewMatrix, &worldToViewMat, sizeof(float) * 16);
	memcpy(commonSettings.worldToViewMatrixPrev, &prevWorldToViewMat, sizeof(float) * 16);

	commonSettings.motionVectorScale[0] = 1.0f;
	commonSettings.motionVectorScale[1] = 1.0f;
	commonSettings.motionVectorScale[2] = 0.0f;
	commonSettings.isMotionVectorInWorldSpace = false;

	auto jitter = globals::features::upscaling.jitter;
	commonSettings.cameraJitter[0] = jitter.x;
	commonSettings.cameraJitter[1] = jitter.y;
	commonSettings.cameraJitterPrev[0] = prevJitter.x;
	commonSettings.cameraJitterPrev[1] = prevJitter.y;

	const uint32_t gameFrame = globals::state->frameCount;
	commonSettings.frameIndex = gameFrame;
	commonSettings.denoisingRange = 1e6f;

	// A changing rect is normal dynamic-resolution operation; NRD consumes both
	// rectSize and rectSizePrev and should retain history across that transition.
	if (!hasCommonFrameHistory || gameFrame != lastCommonGameFrame + 1 || resourceSizeChanged)
		commonSettings.accumulationMode = nrd::AccumulationMode::CLEAR_AND_RESTART;
	lastCommonGameFrame = gameFrame;
	hasCommonFrameHistory = true;

	prevWorldToViewMat = worldToViewMat;
	prevProjMatrix = projMat;
	prevJitter = jitter;
	prevResourceSize[0] = resourceSize[0];
	prevResourceSize[1] = resourceSize[1];
	prevRectSize[0] = rectSize[0];
	prevRectSize[1] = rectSize[1];

	commonSettingsValidThisFrame = true;
	return commonSettings;
}

void NRD::ApplyReblurSettings(nrd::ReblurSettings& out, const REBLURSettings& in, nrd::CheckerboardMode checkerboard) const
{
	out.maxAccumulatedFrameNum = std::min((uint32_t)in.MaxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
	out.maxFastAccumulatedFrameNum = std::min((uint32_t)in.MaxFastAccumulatedFrameNum, out.maxAccumulatedFrameNum);
	out.maxStabilizedFrameNum = std::min((uint32_t)in.MaxStabilizedFrameNum, out.maxAccumulatedFrameNum);
	out.historyFixFrameNum = out.maxFastAccumulatedFrameNum > 0 ? std::min((uint32_t)in.HistoryFixFrameNum, out.maxFastAccumulatedFrameNum - 1) : 0;
	out.historyFixBasePixelStride = std::max(in.HistoryFixBasePixelStride, 1u);
	out.historyFixAlternatePixelStride = std::max(in.HistoryFixAlternatePixelStride, 1u);
	out.fastHistoryClampingSigmaScale = std::clamp(in.FastHistoryClampingSigmaScale, 1.0f, 3.0f);
	out.diffusePrepassBlurRadius = 0.0f;
	out.minHitDistanceWeight = std::clamp(in.MinHitDistanceWeight, 0.0001f, 0.2f);
	out.minBlurRadius = std::max(in.MinBlurRadius, 0.0f);
	out.maxBlurRadius = std::max(in.MaxBlurRadius, out.minBlurRadius);
	out.lobeAngleFraction = std::clamp(in.LobeAngleFraction, 0.0f, 1.0f);
	out.roughnessFraction = std::clamp(in.RoughnessFraction, 0.0f, 1.0f);
	out.planeDistanceSensitivity = std::max(in.PlaneDistanceSensitivity, 0.0f);
	out.enableAntiFirefly = true;
	out.hitDistanceReconstructionMode = static_cast<nrd::HitDistanceReconstructionMode>(std::min(in.HitDistanceReconstructionMode, 2u));
	out.checkerboardMode = checkerboard;
	out.returnHistoryLengthInsteadOfOcclusion = in.ReturnHistoryLength;
}

bool NRD::DrawReblurSettings(REBLURSettings& s, bool showAdvanced, const char* tag)
{
	bool changed = false;
	ImGui::PushID(tag);

	if (showAdvanced) {
		ImGui::SeparatorText("Accumulation");
		{
			int v = (int)s.MaxAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Accumulated Frames", &v, 1, (int)nrd::REBLUR_MAX_HISTORY_FRAME_NUM)) {
				s.MaxAccumulatedFrameNum = (uint32_t)v;
				changed = true;
			}

			v = (int)s.MaxFastAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Fast Accumulated Frames", &v, 1, (int)s.MaxAccumulatedFrameNum)) {
				s.MaxFastAccumulatedFrameNum = (uint32_t)v;
				changed = true;
			}

			v = (int)s.MaxStabilizedFrameNum;
			if (ImGui::SliderInt("Max Stabilized Frames", &v, 0, (int)s.MaxAccumulatedFrameNum)) {
				s.MaxStabilizedFrameNum = (uint32_t)v;
				changed = true;
			}
		}

		ImGui::SeparatorText("Spatial Filter");
		{
			changed |= ImGui::SliderFloat("Min Blur Radius", &s.MinBlurRadius, 0.0f, 10.0f, "%.1f px");
			changed |= ImGui::SliderFloat("Max Blur Radius", &s.MaxBlurRadius, 0.0f, 60.0f, "%.1f px");
			changed |= ImGui::SliderFloat("Lobe Angle Fraction", &s.LobeAngleFraction, 0.0f, 1.0f, "%.2f");
			changed |= ImGui::SliderFloat("Roughness Fraction", &s.RoughnessFraction, 0.0f, 1.0f, "%.2f");
			changed |= ImGui::SliderFloat("Plane Distance Sensitivity", &s.PlaneDistanceSensitivity, 0.0f, 0.1f, "%.4f");
		}

		ImGui::SeparatorText("Quality");
		{
			changed |= ImGui::SliderFloat("Fast History Clamping Sigma", &s.FastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
			changed |= ImGui::SliderFloat("Min Hit Distance Weight", &s.MinHitDistanceWeight, 0.0001f, 0.2f, "%.4f");

			int v = (int)s.HistoryFixFrameNum;
			if (ImGui::SliderInt("History Fix Frame Num", &v, 0, 4)) {
				s.HistoryFixFrameNum = (uint32_t)v;
				changed = true;
			}

			v = (int)s.HistoryFixBasePixelStride;
			if (ImGui::SliderInt("History Fix Pixel Stride", &v, 1, 20)) {
				s.HistoryFixBasePixelStride = (uint32_t)v;
				changed = true;
			}
		}

		ImGui::SeparatorText("Debug");
		{
			changed |= ImGui::SliderFloat("Split Screen", &s.SplitScreen, 0.0f, 1.0f, "%.2f");
			changed |= ImGui::Checkbox("NRD Validation Overlay", &s.EnableValidation);
			changed |= ImGui::Checkbox("Output History Length", &s.ReturnHistoryLength);

			static const char* hitDistReconModes[] = { "OFF", "AREA_3X3", "AREA_5X5" };
			int hdMode = (int)s.HitDistanceReconstructionMode;
			if (ImGui::Combo("Hit Distance Reconstruction", &hdMode, hitDistReconModes, 3)) {
				s.HitDistanceReconstructionMode = (uint32_t)hdMode;
				changed = true;
			}
		}
	}

	ImGui::PopID();
	return changed;
}

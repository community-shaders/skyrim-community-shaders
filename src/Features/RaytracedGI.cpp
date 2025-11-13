#include "RaytracedGI.h"
#include "InverseSquareLighting.h"

#include "Globals.h"
#include "State.h"
#include "RaytracedGI/ShaderUtils.h"
#include "ShaderCache.h"

#include <filesystem>
#include <shlobj.h>
#include <windows.h>
#include "Deferred.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RaytracedGI::Settings,
	Enabled,
	Denoiser,
	Bounces,
	SamplesPerPixel,
	Roughness,
	Specularity,
	Diffuse,
	Specular,
	Directional,
	Point,
	PointFade,
	DebugOutput,
	EnablePIXCapture,
	EnableDebugDevice)

////////////////////////////////////////////////////////////////////////////////////

void RaytracedGI::RestoreDefaultSettings()
{
	settings = {};
}

void RaytracedGI::LoadSettings(json& o_json)
{
	settings = o_json;
}

void RaytracedGI::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DrawFloat2(const char* label, float2& v, float min = 0.0f, float max = 1.0f)
{
	float floats[2] = { v.x, v.y };
	if (ImGui::SliderFloat2(label, floats, min, max)) {
		v = { floats[0], floats[1] };
		v.Clamp({ min, min }, { max, max });
	}
}

void RaytracedGI::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Ray-Traced Global Illumination.");
	}

	// Denoiser
	{
		int denoiser = static_cast<int32_t>(settings.Denoiser);
		ImGui::TextUnformatted("Denoiser");

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(25, 0));

		for (auto& [value, name] : magic_enum::enum_entries<Denoiser>()) {
			ImGui::SameLine();
			ImGui::RadioButton(name.data(), &denoiser, static_cast<int32_t>(value));
		}

		settings.Denoiser = static_cast<Denoiser>(denoiser);
	}

	if (ImGui::SliderInt("Bounces", &settings.Bounces, 1, 32))
		settings.Bounces = std::clamp(settings.Bounces, 1, 32);

	if (ImGui::SliderInt("Samples Per Pixel", &settings.SamplesPerPixel, 1, 32))
		settings.SamplesPerPixel = std::clamp(settings.SamplesPerPixel, 1, 32);

	/*if (ImGui::SliderInt("Bounces", &settings.Bounces, 1, 32))
		settings.Bounces = std::clamp(settings.Bounces, 1, 32);*/

	DrawFloat2("Roughness", settings.Roughness);
	DrawFloat2("Specularity", settings.Specularity);

	if (ImGui::DragFloat("Diffuse Strength", &settings.Diffuse, 0.001f))
		settings.Diffuse = std::max(0.0f, settings.Diffuse);

	if (ImGui::DragFloat("Specular Strength", &settings.Specular, 0.001f))
		settings.Specular = std::max(0.0f, settings.Specular);

	if (ImGui::DragFloat("Emissive Strength", &settings.Emissive, 0.001f))
		settings.Emissive = std::max(0.0f, settings.Emissive);

	if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNodeEx("Direct Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			if(ImGui::DragFloat("Directional Strength", &settings.Directional, 0.001f))
				settings.Directional = std::max(0.0f, settings.Directional);

			if (ImGui::DragFloat("Point Strength", &settings.Point, 0.001f))
				settings.Point = std::max(0.0f, settings.Point);

			ImGui::Checkbox("Point Fade", &settings.PointFade);
		}


	}
	/*if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Number of clipmaps to use\n");
	}*/

	// Debug display mode
	if (ImGui::BeginCombo("Debug Output", magic_enum::enum_name(settings.DebugOutput).data())) {
		for (auto& value : magic_enum::enum_values<DebugOutput>())
		{
			bool isSelected = (settings.DebugOutput == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				settings.DebugOutput = value;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
	}


#ifdef SHARC
	ImGui::DragFloat("SHARC Scale", &settings.SHARCScale, 1.0f, 0.1f, 100.0f);
	settings.SHARCScale = std::clamp(settings.SHARCScale, 0.1f, 100.0f);
#endif

#ifdef DLSS_RR
	ImGui::Checkbox("Enable DLSS Ray Reconstruction", &settings.EnableRR);
#endif

	
	ImGui::Checkbox("Enabled PIX Capture", &settings.EnablePIXCapture);
	ImGui::Checkbox("Enabled Debug Device", &settings.EnableDebugDevice);

	if (settings.EnablePIXCapture)
	{
		if (ImGui::Button("Create PIX Capture")) {
			capture = true;
			captureStarted = false;
		}
	}

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Meshes (vertex, index and BLAS buffers): {}", meshes.size()).c_str());
		ImGui::Text(std::format("Shared Textures: {}", sharedTextures.size()).c_str());

		ImGui::Text(std::format("Instances: {}", instances.size()).c_str());
		ImGui::Text(std::format("Lights: {}", lights.size()).c_str());

		ImGui::TreePop();
	}


	/*if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {		 
		if (ImGui::BeginTable("TriShapes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("TriShape");
			ImGui::TableSetupColumn("Vertex Buffer");
			ImGui::TableHeadersRow();

			for (const auto& [key, value] : debugMultimap) {
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(key.c_str());

				for (const auto& item : value) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(item.c_str());
				}
			}

			debugMultimap.clear();

			ImGui::EndTable();
		}

		ImGui::TreePop();
	}*/

	D3D11_TEXTURE2D_DESC desc;
	finalTexture->resource11->GetDesc(&desc);
	ImGui::Image(finalTexture->srv, { desc.Width * 0.5f, desc.Height * 0.5f });
}

void RaytracedGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	commonHeap = eastl::make_unique<DX12::DescriptorHeap<HeapSlot::Slot, HeapType::Type>>(
		d3d12Device.get(), 
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, HeapSlot::NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	computeHeap = eastl::make_unique<DX12::DescriptorHeap<ComputeHeapSlot::Slot, ComputeHeapType::Type>>(
		d3d12Device.get(),
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ComputeHeapSlot::NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	// UAVs
	{	
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC mainDesc;
		mainTex.texture->GetDesc(&mainDesc);

		// u0 - Final texture
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			finalTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(finalTexture->resource->SetName(L"Final Texture"));

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Format = texDesc.Format;

			d3d12Device->CreateUnorderedAccessView(finalTexture->resource.get(), nullptr, &uavDesc, computeHeap->CPUHandle(ComputeHeapSlot::Final));
		}

		// Motion vector
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			motionVectorsTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(motionVectorsTexture->resource->SetName(L"Motion Vectors Texture"));
		}

		// u1 - Diffuse GI texture
		{
			diffuseGITexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			diffuseGITexture->SetName(L"Diffuse GI Texture");

			diffuseGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			diffuseGITexture->CreateUAV(commonHeap->CPUHandle(HeapSlot::DiffuseGI));
			diffuseGITexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::DiffuseGI));
		}

		// u2 - Specular GI texture
		{				
			specularGITexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			specularGITexture->SetName(L"Specular GI Texture");

			specularGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			specularGITexture->CreateUAV(commonHeap->CPUHandle(HeapSlot::SpecularGI));
			specularGITexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::SpecularGI));	
		}

		// u3 - Specular Hit Distance texture
		{
			specularHitDistanceTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			specularHitDistanceTexture->SetName(L"Specular Hit Distance Texture");

			specularHitDistanceTexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::SpecHitDist));
		}

		// u4 - Depth (Compute only)
		{
			depthTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			depthTexture->SetName(L"Depth Texture");

			depthTexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::Depth));
		}
	}

	// t3 - Light buffer
	{
		lightBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Light>>(d3d12Device.get(), MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->resource->SetName(L"Light Buffer"));

		lightBuffer->CreateSRV(commonHeap->CPUHandle(HeapSlot::Lights));
	}

	// t4 - Instance buffer
	{
		instanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Instance>>(d3d12Device.get(), MAX_INSTANCES);
		DX::ThrowIfFailed(instanceBuffer->resource->SetName(L"Instance Buffer"));

		instanceBuffer->CreateSRV(commonHeap->CPUHandle(HeapSlot::Instances));
	}

	// Create instance buffer for BLAS
	{
		auto instancesDesc = BASIC_BUFFER_DESC;
		instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * MAX_INSTANCES;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&blasInstanceBuffer)));
		DX::ThrowIfFailed(blasInstanceBuffer->SetName(L"BLAS Instance Buffer"));

		blasInstances = new D3D12_RAYTRACING_INSTANCE_DESC[MAX_INSTANCES];
		blasInstanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&blasInstances));
	}

	logger::debug("Creating framebuffer...");
	{
		auto frameBufferDesc = BASIC_BUFFER_DESC;
		frameBufferDesc.Width = (sizeof(FrameBuffer) + 255) & ~255;

		d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &frameBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frameBuffer));
		DX::ThrowIfFailed(frameBuffer->SetName(L"Frame Buffer"));
		frameBuffer->Map(0, nullptr, reinterpret_cast<void**>(&frameBufferData));
	}

	logger::debug("Creating samplers...");
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, cheeseSampler.put()));
	}

	logger::debug("Creating irradiance cache buffer...");
	{
		irradianceCacheBuffer = eastl::make_unique<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>>(d3d12Device.get(), MAX_IRRADIANCE_ENTRIES);
	}

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (fenceEvent == nullptr) {
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

#if defined(DLSS_RR)
	InitRR();
#endif

	CompileShaders();
}

#ifdef DLSS_RR
void RaytracedGI::InitRR()
{
	std::wstring interposerPath = L"Data\\Shaders\\Upscaling\\Streamline\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_RR };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D12;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;
	//sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		logger::info("[Streamline] Successfully initialized Streamline");
	}

	slSetD3DDevice((void*)d3d12Device.get());

	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetOptimalSettings", (void*&)slDLSSDGetOptimalSettings);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetState", (void*&)slDLSSDGetState);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", (void*&)slDLSSDSetOptions);
}

int32_t RaytracedGI::GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
float RaytracedGI::Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void RaytracedGI::GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

float2 RaytracedGI::GetInputResolutionScaleRR(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityMode)
{
	logger::debug("[DLSS RR] Getting input resolution scale for output {}x{} and quality mode {}", outputWidth, outputHeight, qualityMode);
	sl::DLSSMode dlssMode;
	switch (qualityMode) {
	case 1:
		dlssMode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssMode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssMode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssMode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssMode = sl::DLSSMode::eDLAA;
		break;
	}

	sl::DLSSDOptions dlssdOptions{};
	dlssdOptions.mode = dlssMode;
	dlssdOptions.outputWidth = outputWidth;
	dlssdOptions.outputHeight = outputHeight;

	sl::DLSSDOptimalSettings optimalSettings{};
	sl::Result result = slDLSSDGetOptimalSettings(dlssdOptions, optimalSettings);
	if (result != sl::Result::eOk) {
		logger::critical("[Streamline] Failed to get DLSS RR optimal settings, error code: {}", (int)result);
		return { 1.0f, 1.0f };
	}

	float scaleX;
	float scaleY;

	if (globals::game::ui->GameIsPaused()) {
		// Calculate scale as ratio of minimum render resolution to output resolution
		scaleX = (float)optimalSettings.renderWidthMin / (float)outputWidth;
		scaleY = (float)optimalSettings.renderHeightMin / (float)outputHeight;
	} else {
		// Calculate scale as ratio of optimal render resolution to output resolution
		scaleX = (float)optimalSettings.optimalRenderWidth / (float)outputWidth;
		scaleY = (float)optimalSettings.optimalRenderHeight / (float)outputHeight;
	}

	// Return separate X and Y scales for more precision
	return { scaleX, scaleY };
}

void RaytracedGI::SetDLSSRROptions()
{
	sl::DLSSDOptions dlssdOptions{};

	// Map quality mode to DLSS mode
	switch (settings.DLSSRRQualityMode) {
	case 1:
		dlssdOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssdOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssdOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssdOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssdOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	auto worldToCameraView = globals::game::frameBufferCached.GetCameraView().Transpose();
	auto cameraViewToWorld = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	auto state = globals::state;

	dlssdOptions.outputWidth = (uint)state->screenSize.x;
	dlssdOptions.outputHeight = (uint)state->screenSize.y;
	dlssdOptions.colorBuffersHDR = sl::Boolean::eTrue;
	dlssdOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	dlssdOptions.alphaUpscalingEnabled = sl::Boolean::eFalse;

	dlssdOptions.worldToCameraView = sl::float4x4{
		sl::float4{ worldToCameraView._11, worldToCameraView._12, worldToCameraView._13, worldToCameraView._14 },
		sl::float4{ worldToCameraView._21, worldToCameraView._22, worldToCameraView._23, worldToCameraView._24 },
		sl::float4{ worldToCameraView._31, worldToCameraView._32, worldToCameraView._33, worldToCameraView._34 },
		sl::float4{ worldToCameraView._41, worldToCameraView._42, worldToCameraView._43, worldToCameraView._44 }
	};
	dlssdOptions.cameraViewToWorld = sl::float4x4{
		sl::float4{ cameraViewToWorld._11, cameraViewToWorld._12, cameraViewToWorld._13, cameraViewToWorld._14 },
		sl::float4{ cameraViewToWorld._21, cameraViewToWorld._22, cameraViewToWorld._23, cameraViewToWorld._24 },
		sl::float4{ cameraViewToWorld._31, cameraViewToWorld._32, cameraViewToWorld._33, cameraViewToWorld._34 },
		sl::float4{ cameraViewToWorld._41, cameraViewToWorld._42, cameraViewToWorld._43, cameraViewToWorld._44 }
	};
	dlssdOptions.dlaaPreset = sl::DLSSDPreset::ePresetD;
	dlssdOptions.qualityPreset = sl::DLSSDPreset::ePresetD;
	dlssdOptions.balancedPreset = sl::DLSSDPreset::ePresetD;
	dlssdOptions.performancePreset = sl::DLSSDPreset::ePresetD;
	dlssdOptions.ultraPerformancePreset = sl::DLSSDPreset::ePresetD;

	if (SL_FAILED(result, slDLSSDSetOptions(slViewportHandle, dlssdOptions))) {
		logger::critical("[DLSS RR] Could not set DLSS RR options");
		return;
	}
}

void RaytracedGI::CheckFrameConstants()
{
	if (frameChecker.IsNewFrame()) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);

		auto state = globals::state;

		sl::Constants slConstants = {};

		if (globals::game::isVR) {
			slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
		} else {
			slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
		}

		slConstants.cameraFOV = Util::GetVerticalFOVRad();
		slConstants.cameraNear = *globals::game::cameraNear;
		slConstants.cameraFar = *globals::game::cameraFar;

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
		slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
		slConstants.depthInverted = sl::Boolean::eFalse;

		recalculateCameraMatrices(slConstants);

		auto screenSize = state->screenSize;

		auto screenWidth = static_cast<int>(screenSize.x);
		auto screenHeight = static_cast<int>(screenSize.y);

		float2 resolutionScaleBase = GetInputResolutionScaleRR((uint32_t)screenSize.x, (uint32_t)screenSize.y, settings.DLSSRRQualityMode);
		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase.x);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase.y);

		float2 resolutionScale = { 1.0f, 1.0f };

		// Use precise scale if the integer conversion doesn't change the dimensions
		if (renderWidth == screenWidth && renderHeight == screenHeight) {
			// For DLAA and other 1:1 modes, ensure exactly 1.0
			resolutionScale.x = 1.0f;
			resolutionScale.y = 1.0f;
		} else {
			resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
			resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);
		}

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { (globals::game::isVR ? 0.5f : 1.0f), 1 };
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, slViewportHandle))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}
#endif

LPCWSTR StringViewToLPCWSTR(std::string_view sv)
{
	std::string str(sv);

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);

	std::wstring wstr(size_needed, 0);

	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);

	return wstr.c_str();
}

void RaytracedGI::ShareRT(ID3D11Texture2D* pTexture2D, const HeapSlot::Slot& target, const ComputeHeapSlot::Slot& cTarget, ID3D12Resource** ppResource)
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture2D->GetDesc(&desc);

	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(pTexture2D->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle)); // DXGI_SHARED_RESOURCE_WRITE

	DX::ThrowIfFailed(d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(ppResource)));
	CloseHandle(sharedHandle);

	/*const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(*ppResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
	commandList->ResourceBarrier(1, &barrier);*/

	// Create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, commonHeap->CPUHandle(target));

	if (cTarget != ComputeHeapSlot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, computeHeap->CPUHandle(cTarget));
}

void RaytracedGI::SetupSharedRT()
{
	const auto& rendererRD = globals::game::renderer->GetRuntimeData();

	ShareRT(rendererRD.renderTargets[ALBEDO].texture, HeapSlot::Albedo, ComputeHeapSlot::Albedo, albedoTexture.put());
	ShareRT(rendererRD.renderTargets[REFLECTANCE].texture, HeapSlot::Reflectance, ComputeHeapSlot::Reflectance, reflectanceTexture.put());
	ShareRT(rendererRD.renderTargets[NORMALROUGHNESS].texture, HeapSlot::NormalRoughness, ComputeHeapSlot::None, normalRoughnessTexture.put());
	ShareRT(rendererRD.renderTargets[MASKS2].texture, HeapSlot::GeometryNormalDepth, ComputeHeapSlot::GeometryNormalDepth, goemetryNormalDepthTexture.put());
	
	DX::ThrowIfFailed(albedoTexture->SetName(L"Shared Albedo Texture"));
	DX::ThrowIfFailed(reflectanceTexture->SetName(L"Shared Reflectance Texture"));
	DX::ThrowIfFailed(normalRoughnessTexture->SetName(L"Shared NormalRoughness Texture"));
	DX::ThrowIfFailed(goemetryNormalDepthTexture->SetName(L"Shared GeometryNormalDepth Texture"));
}

bool IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

eastl::vector<LightLimitFix::LightData> RaytracedGI::GetPointLights()
{
	eastl::vector<LightLimitFix::LightData> lightsData{};

	auto accumulator = *globals::game::currentAccumulator.get();
	const auto activeShadowSceneNode = accumulator->GetRuntimeData().activeShadowSceneNode;

	auto& isl = globals::features::inverseSquareLighting;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) 
	{
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					// pow(abs(color), 1.6)

					LightLimitFix::LightData light{};
					light.color = { pow(abs(runtimeData.diffuse.red), 1.6f), pow(abs(runtimeData.diffuse.green), 1.6f), pow(abs(runtimeData.diffuse.blue), 1.6f) };
					light.lightFlags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;

						if (settings.PointFade)
							light.color *= runtimeData.fade;
					}

					//light.color *= bsLight->lodDimmer;

					if (!IsGlobalLight(bsLight)) {
						light.lightFlags.set(LightLimitFix::LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(shadowLightIndex, shadowLight);
						light.shadowMaskIndex = shadowLightIndex;
						light.lightFlags.set(LightLimitFix::LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						auto worldPos = niLight->world.translate;

						light.positionWS[0].data = float3(worldPos.x, worldPos.y, worldPos.z);

						if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	const auto& activeLights = activeShadowSceneNode->GetRuntimeData().activeLights;
	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = activeShadowSceneNode->GetRuntimeData().activeShadowLights;
	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	return lightsData;
}

void RaytracedGI::UpdateLights() 
{
	if (!renderingWorld || lightsUpdated)
		return;

	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& directionNi = dirLight->GetWorldDirection();
		auto direction = float3(directionNi.x, directionNi.y, directionNi.z);
		direction.Normalize();

		auto diffuse = dirLight->GetLightRuntimeData().diffuse;

		frameBufferData->Directional.Vector = -direction;
		frameBufferData->Directional.Color = float3(pow(abs(diffuse.red), 1.6f), pow(abs(diffuse.green), 1.6f), pow(abs(diffuse.blue), 1.6f)) * settings.Directional;  // * ( Util::IsInterior() ? 0.0f : 1.0f );
	}

	// Point lights
	{
		lights.clear();
		lights.reserve(MAX_LIGHTS);

		for (auto data : GetPointLights()) {
			if (lights.size() >= MAX_LIGHTS)
				break;

			lights.emplace_back(data.positionWS[0].data, data.radius, data.color * settings.Point, 0);
		}

		lightBuffer->UpdateList(lights.data(), lights.size());
	}

	lightsUpdated = true;
}

DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
{
	DirectX::XMMATRIX temp;

	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][0],
										   m.entry[1][0],
										   m.entry[2][0],
										   0.0f),
		scale);

	temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][1],
										   m.entry[1][1],
										   m.entry[2][1],
										   0.0f),
		scale);

	temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][2],
										   m.entry[1][2],
										   m.entry[2][2],
										   0.0f),
		scale);

	temp.r[3] = DirectX::XMVectorSet(
		Transform.translate.x,
		Transform.translate.y,
		Transform.translate.z,
		1.0f);

	return temp;
}

void RaytracedGI::CopyDepth()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->CSSetShader(copyDepthCS.get(), nullptr, 0);

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	context->CSSetShaderResources(0, 1, &depth.depthSRV);

	auto masks2 = renderer->GetRuntimeData().renderTargets[MASKS2];
	context->CSSetUnorderedAccessViews(0, 1, &masks2.UAV, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
}

void RaytracedGI::Main_RenderWorld(bool a1)
{
	if (Active()) {
		renderingWorld = true;
		lightsUpdated = false;
		//CreateBuffers();
		//CopyDepth();
		if (!addedAllInstances)
		{
			//AddUpdateAllInstances();
			addedAllInstances = true;
		}
	}

	Hooks::Main_RenderWorld::func(a1);

	if (Active()) {
		renderingWorld = false;
		//logger::info("RenderWorld_After");
		/*for (const auto& [key, value] : debugMultimap) {
			logger::info(fmt::runtime("Name [0x{:x}], TriShape: [0x{:x}], Vertex: [0x{:x}]"), key.c_str(), value[0].c_str(), value[1].c_str());
		}
		debugMultimap.clear();*/
	}
}

template <typename T>
auto GetFlags(uint32_t value) {
	const auto& entries = magic_enum::enum_entries<T>();

	std::string flags;

	for (const auto& [flag, name] : entries) {
		if ((value & static_cast<uint32_t>(flag)) != 0) {
			flags += fmt::format("{} ", name);
		}
	}

	return flags;
};

RE::BSFadeNode* FindBSFadeNode(RE::NiNode* a_niNode)
{
	if (auto fadeNode = a_niNode->AsFadeNode()) {
		return fadeNode;
	}
	return a_niNode->parent ? FindBSFadeNode(a_niNode->parent) : nullptr;
}

template <typename T>
void RaytracedGI::MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res)
 {
	auto desc = BASIC_BUFFER_DESC;
	desc.Width = sizeof(T) * data.size();

	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));

	void* ptr;
	DX::ThrowIfFailed(res->Map(0, nullptr, &ptr));
	memcpy(ptr, data.data(), desc.Width);
	res->Unmap(0, nullptr);
}

inline std::wstring ToWide(const std::string& str)
{
	if (str.empty())
		return std::wstring();

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), &wstr[0], size_needed);
	return wstr;
}

void RaytracedGI::AddInstance(RE::BSTriShape* pTriShape)
{
	//std::lock_guard lock{ renderMutex };

	RE::BSGeometry* pGeometry = pTriShape->AsGeometry();

	if (pGeometry->worldBound.radius == 0.0f)
		return;

	// Ensure its Lighting shader, for now
	//if (!pTriShape->lightingShaderProp_cast())
	//	return;

	/*if (pTriShape->GetFlags().all(RE::NiAVObject::Flag::kRenderUse)) {
		if (auto fadeNode = FindBSFadeNode((RE::NiNode*)pTriShape)) {
			if (auto extraData = fadeNode->GetExtraData("BSX")) {
				auto bsxFlags = (RE::BSXFlags*)extraData;

				if (static_cast<uint32_t>(bsxFlags->value) & (uint32_t)RE::BSXFlags::Flag::kEditorMarker)
					return;
			}
		} else {  // Else it crashes on Block stuff when reading indexes
			return;
		}
	} else {
		return;
	}*/

	RE::BSGraphics::TriShape* rendererData = pTriShape->GetGeometryRuntimeData().rendererData;

	if (!rendererData)
		return;

	// Our beloved key, this could mess things up if the engine uses the same vertexBuffer for another, but based on Brixelizer it doesn't...
	auto vertexBufferDX11 = (ID3D11Buffer*)rendererData->vertexBuffer;

	auto meshIt = meshes.find(vertexBufferDX11);

	// Mesh doesn't exist yet
	if (meshIt == meshes.end()) 
	{
		const auto triShapeRuntime = pTriShape->GetTrishapeRuntimeData();
		uint vertexCount = triShapeRuntime.vertexCount;
		uint triangleCount = triShapeRuntime.triangleCount;

		//logger::info("[RTGI] AddInstance - {}, Vertex Count: {}, Triangle Count: {}", pTriShape->name, vertexCount, triangleCount);
		const auto& flagsUnd = pTriShape->GetFlags().underlying();
		const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flagsUnd);

		//logger::info("[RTGI] AddInstance - {}, Flags: [ {} ]", pTriShape->name, flagsStr);

		std::wstring geometryNameW = ToWide(pGeometry->name.c_str());

		eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Vertex>>(d3d12Device.get(), vertexCount);
		eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> triangleBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Triangle>>(d3d12Device.get(), triangleCount);
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;

		// Create vertex buffer
		{
			auto vertexDesc = rendererData->vertexDesc;

			auto vertexFlags = vertexDesc.GetFlags();
			uint32_t stride = vertexDesc.GetSize();

			uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
			uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
			uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
			uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);

			eastl::vector<Vertex> vertices(vertexCount);

			for (uint32_t i = 0; i < vertexCount; i++) {
				uint8_t* vtx = rendererData->rawVertexData + i * stride;

				Vertex vertexData{};

				if (vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX) {
					const float* pos = reinterpret_cast<const float*>(vtx + posOffset);
					vertexData.Position.x = pos[0];
					vertexData.Position.y = pos[1];
					vertexData.Position.z = pos[2];
					//vertexData.Position.w = pos[3];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
					const uint16_t* texcoord = reinterpret_cast<const uint16_t*>(vtx + uvOffset);

					vertexData.Texcoord0[0] = texcoord[0];
					vertexData.Texcoord0[1] = texcoord[1];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL) {
					const uint8_t* norm = reinterpret_cast<const uint8_t*>(vtx + normOffset);

					vertexData.Normal[0] = norm[0];
					vertexData.Normal[1] = norm[1];
					vertexData.Normal[2] = norm[2];
					vertexData.Normal[3] = norm[3];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
					const uint8_t* col = reinterpret_cast<const uint8_t*>(vtx + colorOffset);

					vertexData.Color[0] = col[0];
					vertexData.Color[1] = col[1];
					vertexData.Color[2] = col[2];
					vertexData.Color[3] = col[3];
				}

				vertices[i] = eastl::move(vertexData);
			}

			vertexBuffer->UpdateList(vertices.data(), vertices.size());
			DX::ThrowIfFailed(vertexBuffer->resource->SetName(std::format(L"Vertex Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));

			vertexBuffer->Upload(commandList.get());
		}

		// Create indices buffer
		{
			const uint16_t* indexes = rendererData->rawIndexData;

			eastl::vector<Triangle> triangles(triangleCount);

			for (uint32_t t = 0; t < triangleCount; ++t) {
				uint32_t i = t * 3;

				triangles[t] = Triangle(
					static_cast<uint32_t>(indexes[i]),
					static_cast<uint32_t>(indexes[i + 1]),
					static_cast<uint32_t>(indexes[i + 2]));
			}

			triangleBuffer->UpdateList(triangles.data(), triangles.size());
			DX::ThrowIfFailed(triangleBuffer->resource->SetName(std::format(L"Index Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));

			triangleBuffer->Upload(commandList.get());
		}

		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* glowTexture = nullptr;

		// Register textures buffer
		{
			using State = RE::BSGeometry::States;
			using Feature = RE::BSShaderMaterial::Feature;

			auto geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

			auto effect = geometryRuntimeData.properties[State::kEffect].get();

			if (effect) {
				auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);

				if (lightingShader) {
					auto material = lightingShader->material;

					if (material) {
						auto lightingBaseMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(material);

						auto niDiffuseTexture = lightingBaseMaterial->diffuseTexture;
						auto bsDiffuseTexture = niDiffuseTexture->rendererTexture;

						if (auto it = sharedTextures.find(bsDiffuseTexture->texture); it != sharedTextures.end()) {
							diffuseTexture = it->second.get();
						} else {
							logger::warn(fmt::runtime("[RT] Diffuse texture not found for {}"), pGeometry->name.c_str());
						}

						if (material->GetFeature() == Feature::kGlowMap) {
							const auto& lightingGlowMaterial = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material);

							if (lightingGlowMaterial) {
								if (const auto& niGlowTexture = lightingGlowMaterial->glowTexture; niGlowTexture) {
									if (const auto& bsGlowTexture = niGlowTexture->rendererTexture; bsGlowTexture) {
										if (auto it = sharedTextures.find(bsGlowTexture->texture); it != sharedTextures.end()) {
											glowTexture = it->second.get();
										}
									}
								}
							}

							if (!glowTexture)
								logger::warn(fmt::runtime("[RT] Glow texture not found for {}"), pGeometry->name.c_str());
						}
					}
				}
			}
		}

		// Create BLAS
		{
			blasBuffer.attach(MakeBLAS(vertexBuffer->resource.get(), vertexCount, triangleBuffer->resource.get(), triangleCount * 3));
		}

		uint registerIndex = registers.allocate();

		//logger::info("[RT] Register {} for mesh {}", registerIndex, meshes.size());

		// SRVs
		{
			// Vertex structured buffer
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = static_cast<int32_t>(vertexCount);
			vbDesc.Buffer.StructureByteStride = sizeof(Vertex);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(vertexBuffer->resource.get(), &vbDesc, commonHeap->CPUHandle(HeapSlot::Vertices, registerIndex));

			// Index/Triangle (Structured buffer)
			D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
			ibDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			ibDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ibDesc.Format = DXGI_FORMAT_UNKNOWN;
			ibDesc.Buffer.FirstElement = 0;
			ibDesc.Buffer.NumElements = static_cast<int32_t>(triangleCount);
			ibDesc.Buffer.StructureByteStride = sizeof(Triangle);
			ibDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(triangleBuffer->resource.get(), &ibDesc, commonHeap->CPUHandle(HeapSlot::Triangles, registerIndex));

			// Diffuse Texture
			if (diffuseTexture) {
				D3D12_RESOURCE_DESC texResDesc = diffuseTexture->GetDesc();

				D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
				texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				texSrvDesc.Format = texResDesc.Format;
				texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				texSrvDesc.Texture2D.MostDetailedMip = 0;
				texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
				texSrvDesc.Texture2D.PlaneSlice = 0;
				texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				d3d12Device->CreateShaderResourceView(diffuseTexture, &texSrvDesc, commonHeap->CPUHandle(HeapSlot::DiffuseTextures, registerIndex));
			}

			// Glow Texture
			if (glowTexture) {
				D3D12_RESOURCE_DESC texResDesc = glowTexture->GetDesc();

				D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
				texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				texSrvDesc.Format = texResDesc.Format;
				texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				texSrvDesc.Texture2D.MostDetailedMip = 0;
				texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
				texSrvDesc.Texture2D.PlaneSlice = 0;
				texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				d3d12Device->CreateShaderResourceView(glowTexture, &texSrvDesc, commonHeap->CPUHandle(HeapSlot::GlowTextures, registerIndex));
			}		
		}

		// Emplace mesh
		meshes.emplace(
			vertexBufferDX11,
			MeshData(
				registerIndex,
				vertexCount,
				triangleCount,
				eastl::move(vertexBuffer),
				eastl::move(triangleBuffer),
				std::move(blasBuffer),
				MaterialData(diffuseTexture, glowTexture),
				{ pTriShape }));

	} else {
		meshIt->second.instances.push_back(pTriShape);
	}

	instances.emplace(
		pTriShape,
		InstanceData(
			vertexBufferDX11,
			GetXMFromNiTransform(pTriShape->world),
			GatherInstanceLights(pTriShape)));
}

eastl::vector<size_t> RaytracedGI::GatherInstanceLights(RE::BSTriShape* pBSTriShape)
{
	eastl::vector<size_t> instanceLights;

	float3 center = Float3(pBSTriShape->worldBound.center);
	float radius = pBSTriShape->worldBound.radius;

	for (size_t i = 0; i < lights.size(); i++) {
		const Light& light = lights[i];

		if ((center - light.Vector).Length() <= radius + light.Range)
			instanceLights.push_back(i);
	}

	return instanceLights;
}

void RaytracedGI::AddUpdateInstance(RE::BSTriShape* pTriShape)
{
	std::lock_guard lock{ meshMutex };

	//logger::info("[RT] {} [0x{:x}] [0x{:x}]", pTriShape->name.c_str(), reinterpret_cast<uintptr_t>(pTriShape), reinterpret_cast<uintptr_t>(pTriShape->GetGeometryRuntimeData().rendererData->vertexBuffer));

	if (const auto it = instances.find(pTriShape); it != instances.end()) {
		InstanceData& instance = it->second;

		instance.transform = GetXMFromNiTransform(pTriShape->world);
		instance.lights = GatherInstanceLights(pTriShape);		
	} else {
		AddInstance(pTriShape);	
	}
}

void RaytracedGI::VertexBufferReleased(ID3D11Buffer* pBuffer)
{
	std::lock_guard lock{ meshMutex };

	if (const auto it = meshes.find(pBuffer); it != meshes.end()) {
		MeshData& meshData = it->second;

		for (auto& instance : meshData.instances)
			instances.erase(instance);

		registers.free(meshData.registerIndex);

		meshes.erase(pBuffer);
	}
}

void RaytracedGI::AddUpdateAllInstances()
{
	static auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];

	RE::BSVisit::TraverseScenegraphGeometries(shadowSceneNode, [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
		if (RE::BSTriShape* pTriShape = geometry->AsTriShape()) {

			if (pTriShape->GetFlags().all(RE::NiAVObject::Flag::kRenderUse)) {
				if (auto fadeNode = FindBSFadeNode((RE::NiNode*)pTriShape)) {
					if (auto extraData = fadeNode->GetExtraData("BSX")) {
						auto bsxFlags = (RE::BSXFlags*)extraData;

						if (static_cast<uint32_t>(bsxFlags->value) & (uint32_t)RE::BSXFlags::Flag::kEditorMarker)
							return RE::BSVisit::BSVisitControl::kContinue;
					}
				} else {  // Else it crashes on Block stuff when reading indexes
					return RE::BSVisit::BSVisitControl::kContinue;
				}
			} else {
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			AddUpdateInstance(pTriShape);
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});
}

void RaytracedGI::UpdateInstances()
{
	std::lock_guard lock{ meshMutex };

	instanceData.clear();
	instanceData.resize(instances.size());

	size_t instanceID = 0;
	for (auto& [triShape, data] : instances) {

		MeshData& meshData = meshes[data.meshKey];

		blasInstances[instanceID] = {
			.InstanceID = static_cast<uint>(instanceID),
			.InstanceMask = 1,
			.AccelerationStructure = meshData.blasBuffer->GetGPUVirtualAddress()
		};

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&blasInstances[instanceID].Transform);
		XMStoreFloat3x4(ptr, data.transform);

		instanceData[instanceID] = Instance(static_cast<uint>(meshData.registerIndex), LightData(data.lights));

		instanceID++;
	}
	
	instanceBuffer->UpdateList(instanceData.data(), instanceData.size());
}


void RaytracedGI::BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
{
	const auto shader = pass->shader;
	const auto shaderType = shader->shaderType.get();

	if (shaderType != RE::BSShader::Type::Lighting)
		return;

	const auto geometry = pass->geometry;

	const auto triShape = geometry->AsTriShape();

	if (triShape == nullptr)
		return;

	const auto& type = geometry->GetType().get();
	const auto typeName = magic_enum::enum_name(type);

	const auto& flagsUnd = geometry->GetFlags().underlying();
	const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flagsUnd);

	// NiAVObject::Flag
	logger::info(fmt::runtime("Geometry [0x{:x}]: {}"), reinterpret_cast<uintptr_t>(geometry), geometry->name.c_str());
	logger::info(fmt::runtime("Flags: {}"), flagsStr);

	logger::info(
		fmt::runtime("Pass: [0x{:x}], Alpha Test: {}, Render Flags: [0x{:x}], TriShape: [0x{:x}], Type: {}"),
		reinterpret_cast<uintptr_t>(pass),
		alphaTest,
		renderFlags,
		reinterpret_cast<uintptr_t>(triShape),
		typeName);

	if (technique)
		return;

	//int32_t technique = 0x3F & (vertexDescriptor >> 24);
	//logger::info(fmt::runtime("LightingShaderTechniques: {}"), GetFlags<SIE::ShaderCache::LightingShaderTechniques>(technique));
}

void RaytracedGI::BSTriShape_UpdateWorldData(RE::BSTriShape* oThis, RE::NiUpdateData* pData)
{
	if (Active() && pData->flags.any(RE::NiUpdateData::Flag::kDirty)) {
		RE::NiPoint3 pointA = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);

		RE::NiPoint3 pointB = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		if (pointA.GetDistance(pointB) > 0.1f) {
			//AddUpdateInstance(oThis);
		}		
	} else {
		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);
	}
}

void RaytracedGI::BSShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active() || !renderingWorld)
		return;

	UpdateLights();

	if (auto triShape = pPass->geometry->AsTriShape())
		AddUpdateInstance(triShape);
	/*else
		logger::warn("TriShape not available for {}, type: {}", pPass->geometry->name, magic_enum::enum_name(pPass->geometry->GetType().get()));*/
}

void RaytracedGI::DrawRTGI()
{
	//std::lock_guard lock{ renderMutex };

	if (!d3d11Context) {
		logger::error("d3d11Context is nullptr");
	}

	if (!d3d11Fence) {
		logger::error("d3d11Fence is nullptr");
	}

	auto rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto main = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kMAIN];

	d3d11Context->CopyResource(finalTexture->resource11, main.texture);
	d3d11Context->CopyResource(motionVectorsTexture->resource11, rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].texture);

	// Copy depth and normal/roughness to wrapped resources
	{
		//auto renderer = globals::game::renderer;
		//d3d11Context->CopyResource(depthTexture->resource11, renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture);
		//d3d11Context->CopyResource(normalRoughnessTexture->resource11, renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS].texture);

		//goemetryNormalDepthTexture
	}

	CopyDepth();

	// Wait for D3D11 to finish
	{
		d3d11Context->Flush();
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;
	}

	// New frame, reset
	/*{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	// Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
	}*/

	/*if (capture) {
		captureStarted = true;
		ga->BeginCapture();
	}*/

	UpdateInstances();

	// Upload buffers
	{
		instanceBuffer->Upload(commandList.get());
		lightBuffer->Upload(commandList.get());
	}

#ifdef DLSS_RR
	if (settings.EnableRR) {
		SetDLSSRROptions();
		CheckFrameConstants();
	}
#endif

	// Update framebuffer
	{
		frameBufferData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameBufferData->ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();

		float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameBufferData->Position = float3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
		frameBufferData->FrameCount = globals::state->frameCount;

		frameBufferData->CameraData = Util::GetCameraData();

		auto eye = Util::GetCameraData(0);
		float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
		float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

		frameBufferData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

		frameBufferData->Diffuse = settings.Diffuse;
		frameBufferData->Specular = settings.Specular;
		frameBufferData->Emissive = settings.Emissive;
#ifdef SHARC
		frameBufferData->SHARCScale = settings.SHARCScale / Util::Units::GAME_UNIT_TO_M;
#endif
	}

	// Create TLAS and its update scratch or update TLAS
	if (tlas == nullptr || tlasUpdateScratch == nullptr) {
		UINT64 updateScratchSize;
		tlas.attach(MakeTLAS(blasInstanceBuffer.get(), MAX_INSTANCES, &updateScratchSize));
		DX::ThrowIfFailed(tlas->SetName(L"TLAS"));

		auto desc = BASIC_BUFFER_DESC;
		desc.Width = std::max(updateScratchSize, 8ULL);  // WARP bug workaround: use 8 if the required size was reported as less
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
		DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch"));

		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, commonHeap->CPUHandle(HeapSlot::TLAS));
	} else {
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {
			.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.Inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
				.NumDescs = static_cast<uint>(instances.size()),
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = blasInstanceBuffer->GetGPUVirtualAddress() },
			.SourceAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.ScratchAccelerationStructureData = tlasUpdateScratch->GetGPUVirtualAddress(),
		};

		commandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

		const auto& tlasBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
		commandList->ResourceBarrier(1, &tlasBarrier);
	}

	{
		// Raytracing
		{
			commandList->SetPipelineState1(pipelineRT.get());
			commandList->SetComputeRootSignature(rootSignature.get());

			auto commonHeapPtr = commonHeap->Heap();
			commandList->SetDescriptorHeaps(1, &commonHeapPtr);

			// Parameter 0: UAV table
			commandList->SetComputeRootDescriptorTable(0, commonHeap->TableGPUHandle(HeapType::UAV));

			// Parameter 1: Fixed SRVs
			commandList->SetComputeRootDescriptorTable(1, commonHeap->TableGPUHandle(HeapType::SRV));

			// Parameter 2: Vertex buffers
			commandList->SetComputeRootDescriptorTable(2, commonHeap->TableGPUHandle(HeapType::VertexBuffer));

			// Parameter 3: Triangle buffers
			commandList->SetComputeRootDescriptorTable(3, commonHeap->TableGPUHandle(HeapType::TriangleBuffer));

			// Parameter 4: Diffuse Textures
			commandList->SetComputeRootDescriptorTable(4, commonHeap->TableGPUHandle(HeapType::DiffuseTextures));

			// Parameter 5: Glow Textures
			commandList->SetComputeRootDescriptorTable(5, commonHeap->TableGPUHandle(HeapType::GlowTextures));

			// Parameter 6: Constant buffer
			commandList->SetComputeRootConstantBufferView(6, frameBuffer->GetGPUVirtualAddress());

			auto finalTexDesc = finalTexture->resource->GetDesc();

			auto shaderTableGPUAddr = shaderIDs->GetGPUVirtualAddress();
			const auto& shaderTable = commonHeap->GetShaderTable();

			D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
				.RayGenerationShaderRecord = {
					.StartAddress = shaderTableGPUAddr + shaderTable.RayGenerationShaderRecord.StartAddress,
					.SizeInBytes = shaderTable.RayGenerationShaderRecord.SizeInBytes },
				.MissShaderTable = { .StartAddress = shaderTableGPUAddr + shaderTable.MissShaderTable.StartAddress, .SizeInBytes = shaderTable.MissShaderTable.SizeInBytes, .StrideInBytes = shaderTable.MissShaderTable.StrideInBytes },
				.HitGroupTable = { .StartAddress = shaderTableGPUAddr + shaderTable.HitGroupTable.StartAddress, .SizeInBytes = shaderTable.HitGroupTable.SizeInBytes, .StrideInBytes = shaderTable.HitGroupTable.StrideInBytes },
				.Width = static_cast<uint>(finalTexDesc.Width),
				.Height = finalTexDesc.Height,
				.Depth = 1
			};

			commandList->DispatchRays(&dispatchDesc);

			CD3DX12_RESOURCE_BARRIER rtUAVBarrier[2] = {
				CD3DX12_RESOURCE_BARRIER::UAV(diffuseGITexture->resource.get()),
				CD3DX12_RESOURCE_BARRIER::UAV(specularGITexture->resource.get())
			};
			commandList->ResourceBarrier(_countof(rtUAVBarrier), rtUAVBarrier);
		}

		// Compute
		if (settings.DebugOutput == DebugOutput::None) {
			commandList->SetPipelineState(pipelineCS.get());
			commandList->SetComputeRootSignature(rootSignatureCS.get());

			auto computeHeapPtr = computeHeap->Heap();
			commandList->SetDescriptorHeaps(1, &computeHeapPtr);

			// UAV table
			commandList->SetComputeRootDescriptorTable(0, computeHeap->TableGPUHandle(ComputeHeapType::UAV));

			// SRV table
			commandList->SetComputeRootDescriptorTable(1, computeHeap->TableGPUHandle(ComputeHeapType::SRV));

			// Constant buffer
			//commandList->SetComputeRootConstantBufferView(5, frameBuffer->GetGPUVirtualAddress());

			CD3DX12_RESOURCE_BARRIER commonToUAVBarrier[2] = {
				specularHitDistanceTexture->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				depthTexture->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};
			commandList->ResourceBarrier(_countof(commonToUAVBarrier), commonToUAVBarrier);

			auto dispatchCount = Util::GetScreenDispatchCount();
			commandList->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			CD3DX12_RESOURCE_BARRIER uavToCommonBarrier[2] = {
				specularHitDistanceTexture->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_COMMON),
				depthTexture->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_COMMON)
			};
			commandList->ResourceBarrier(_countof(uavToCommonBarrier), uavToCommonBarrier);

			//const auto& specHitDistBarrier = CD3DX12_RESOURCE_BARRIER::UAV(specularHitDistanceTexture.get());
			//commandList->ResourceBarrier(1, &specHitDistBarrier);

#ifdef DLSS_RR
			if (settings.EnableRR) {
				CD3DX12_RESOURCE_BARRIER rtBarrier[1] = { CD3DX12_RESOURCE_BARRIER::UAV(finalTexture->resource.get()) };
				commandList->ResourceBarrier(1, rtBarrier);

				{
					auto screenSize = globals::state->screenSize;
					auto renderSize = Util::ConvertToDynamic(screenSize);

					sl::Extent inputExtent{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
					sl::Extent outputExtent{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

					sl::Resource colorIn = { sl::ResourceType::eTex2d, finalTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
					sl::Resource colorOut = { sl::ResourceType::eTex2d, finalTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };  // This probably will break, we'll see...
					sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON };
					sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture->resource.get(), 0 };
					sl::Resource diffuseAlbedo = { sl::ResourceType::eTex2d, albedoTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource specularAlbedo = { sl::ResourceType::eTex2d, reflectanceTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource normalRoughness = { sl::ResourceType::eTex2d, normalRoughnessTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource specHitDistance = { sl::ResourceType::eTex2d, specularHitDistanceTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON };

					sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent };
					sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };
					sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag diffuseAlbedoTag = sl::ResourceTag{ &diffuseAlbedo, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specularAlbedoTag = sl::ResourceTag{ &specularAlbedo, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag normalRoughnessTag = sl::ResourceTag{ &normalRoughness, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specHitDistanceTag = sl::ResourceTag{ &specHitDistance, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };

					sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, diffuseAlbedoTag, specularAlbedoTag, normalRoughnessTag, specHitDistanceTag };
					if (SL_FAILED(result, slSetTag(slViewportHandle, resourceTags, _countof(resourceTags), commandList.get()))) {
						logger::error("[DLSS RR] Failed to set DLSS RR tags, error: {}", magic_enum::enum_name(result));
						return;
					}
				}

				const sl::BaseStructure* inputs[] = { &slViewportHandle };

				if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, inputs, _countof(inputs), commandList.get()))) {
					logger::error("[DLSS RR] Failed to evaluate DLSS RR feature, error: {}", magic_enum::enum_name(result));
				}
			}
#endif
		} else {
			if (settings.DebugOutput == DebugOutput::Indirect) {
				diffuseGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(finalTexture->resource.get(), diffuseGITexture->resource.get());
				diffuseGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			} else if (settings.DebugOutput == DebugOutput::Specular) {
				specularGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(finalTexture->resource.get(), specularGITexture->resource.get());
				specularGITexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}
		}

		DX::ThrowIfFailed(commandList->Close());

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish
	{
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		// Wait until GPU is done with previous frame
		if (d3d12Fence->GetCompletedValue() < fenceValue) {
			DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
		}

		if (capture && captureStarted) {
			ga->EndCapture();
			capture = false;
			captureStarted = false;
		}

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	//New frame, reset
	{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	if (capture) {
		captureStarted = true;
		ga->BeginCapture();
	}

	//Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
	}

	d3d11Context->CopyResource(main.texture, finalTexture->resource11);

	//auto reflectance = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kRAWINDIRECT];

	// Clear specular for now, just so I can see the results better 
	{
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		d3d11Context->ClearRenderTargetView(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
	}
}

ID3D12Resource* RaytracedGI::MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize)
{
	auto makeBuffer = [&](UINT64 size, auto initialState) {
		auto desc = BASIC_BUFFER_DESC;
		desc.Width = size;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ID3D12Resource* buffer;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer)));
		return buffer;
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
	if (updateScratchSize)
		*updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

	winrt::com_ptr<ID3D12Resource> scratch = nullptr;
	scratch.attach(makeBuffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	auto buildDesc = eastl::make_unique<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>(
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC{
			.DestAccelerationStructureData = as->GetGPUVirtualAddress(),
			.Inputs = inputs,
			.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() 
		}
	);

	commandList->BuildRaytracingAccelerationStructure(buildDesc.get(), 0, nullptr);

	rtASDescs.push_back(eastl::move(buildDesc));

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(as);
	commandList->ResourceBarrier(1, &asBarrier);	

	asScratchBuffers.push_back(std::move(scratch));

	return as;
}

ID3D12Resource* RaytracedGI::MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices)
{
	eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs({ D3D12_RAYTRACING_GEOMETRY_DESC{
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {
			.Transform3x4 = 0,
			.IndexFormat = DXGI_FORMAT_R32_UINT,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = indices,
			.VertexCount = vertices,
			.IndexBuffer = indexBuffer->GetGPUVirtualAddress(),
			.VertexBuffer = {
				.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
				.StrideInBytes = sizeof(Vertex) 
			} 
		}	  
	 }});

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	rtGeometryDescs.push_back(eastl::move(geometryDescs));

	return MakeAccelerationStructure(inputs);
}

ID3D12Resource* RaytracedGI::MakeTLAS(ID3D12Resource* localInstances, UINT numInstances, UINT64* updateScratchSize)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE, // | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = numInstances,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = localInstances->GetGPUVirtualAddress()
	};

	return MakeAccelerationStructure(inputs, updateScratchSize);
}

void RaytracedGI::Flush()
{
	static UINT64 value = 1;
	commandQueue->Signal(d3d12Fence.get(), value);
	d3d12Fence->SetEventOnCompletion(value++, nullptr);
}


void RaytracedGI::PostPostLoad()
{
	Hooks::Install();
	Initialize();

	//MenuOpenCloseEventHandler::Register();
	//TESLoadGameEventHandler::Register();
}

static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath)) {
		if (directory_entry.is_directory()) {
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str()) {
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty()) {
		// TODO: Error, no PIX installation found
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

void DumpDredBreadcrumbs(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1& breadcrumbsOutput)
{
	const D3D12_AUTO_BREADCRUMB_NODE1* pNode = breadcrumbsOutput.pHeadAutoBreadcrumbNode;

	while (pNode) {
		const UINT32 completedOps = *pNode->pLastBreadcrumbValue;
		const UINT32 totalOps = pNode->BreadcrumbCount;

		logger::error("[RT] Command List: {}", pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "<unnamed>");
		logger::error("[RT] Queue: {}", pNode->pCommandQueueDebugNameA ? pNode->pCommandQueueDebugNameA : "<unnamed>");
		logger::error("[RT] Completed Ops: {} / {}", completedOps, totalOps);

		if (pNode->pCommandHistory && totalOps > 0) {
			// Last executed command
			UINT32 lastIndex = (completedOps > 0) ? completedOps - 1 : 0;
			auto lastOp = pNode->pCommandHistory[lastIndex];
			logger::error("[RT] Last Executed Command: {}", magic_enum::enum_name(lastOp));

			// Next (likely faulting) command
			if (completedOps < totalOps) {
				auto nextOp = pNode->pCommandHistory[completedOps];
				logger::error("[RT] Next (Likely Faulting) Command: {}", magic_enum::enum_name(nextOp));
			}
		}

		logger::error("");  // empty line for readability
		pNode = pNode->pNext;
	}
}

void RaytracedGI::DeviceRemovedHandler()
{
	// 1. Device removed reason
	HRESULT reason = d3d12Device->GetDeviceRemovedReason();
	logger::error("[RT] ============================================================");
	logger::error("[RT] DEVICE REMOVED! HRESULT = 0x{:08X}", reason);

	winrt::com_ptr<ID3D12DeviceRemovedExtendedData1> dred;
	if (FAILED(d3d12Device->QueryInterface(IID_PPV_ARGS(&dred)))) {
		logger::error("[RT] DRED not available on this device.");
		return;
	}

	// ---------------------------------------------------------------------
	// 2. Auto Breadcrumbs
	// ---------------------------------------------------------------------
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bcOutput = {};
	if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&bcOutput)) && bcOutput.pHeadAutoBreadcrumbNode) {
		DumpDredBreadcrumbs(bcOutput);
	} else {
		logger::error("[RT] No breadcrumbs available.");
	}
}

void RaytracedGI::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			LoadLibrary(GetLatestWinPixGpuCapturerPath().c_str());
		}
	}

	logger::info("[RTGI] Creating D3D12 device");

	// Set Device
	DX::ThrowIfFailed(ppDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(pImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create debug device
	if (!settings.EnablePIXCapture && settings.EnableDebugDevice)
	{
		winrt::com_ptr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(TRUE);
		}

		winrt::com_ptr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}

	if (settings.EnablePIXCapture) {
		DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
	}

	// Create Device
	{
		DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device)));

		// Check hardware raytracing tier
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
			if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
				if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
					logger::info("Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
				else
					logger::warn("Hardware ray tracing not supported.");
			}		
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));

		DX::ThrowIfFailed(commandQueue->SetName(L"Command Queue"));
		DX::ThrowIfFailed(commandAllocator->SetName(L"Command Allocator"));
		DX::ThrowIfFailed(commandList->SetName(L"Command List"));

		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
		//DX::ThrowIfFailed(commandList->Close());
	}

	// Create Interop
	{
		HANDLE sharedFenceHandle;
		DX::ThrowIfFailed(d3d12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
		DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
		DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
		CloseHandle(sharedFenceHandle);
	}

	if (settings.EnableDebugDevice)
	{
		HANDLE disconnectEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(UINT64_MAX, disconnectEvent));

		std::thread([this, disconnectEvent]() {
			WaitForSingleObject(disconnectEvent, INFINITE);
			DeviceRemovedHandler();
		}).detach();
	}
}

void RaytracedGI::CreateRootSignature()
{
	if (rootSignature)
		return;

	// UAV range
	commonHeap->CreateTable(
		HeapType::UAV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 
		{  
			{ HeapSlot::DiffuseGI, 1 }, 
			{ HeapSlot::SpecularGI, 1 }, 
		});

	// Fixed SRV ranges (NormalRoughness + GeometryNormalDepth + Scene + Lights + Index map)
	commonHeap->CreateTable(
		HeapType::SRV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::Albedo, 1 },
			{ HeapSlot::Reflectance, 1 },
			{ HeapSlot::NormalRoughness, 1 },
			{ HeapSlot::GeometryNormalDepth, 1 },
			{ HeapSlot::TLAS, 1 },
			{ HeapSlot::Lights, 1 },
			{ HeapSlot::Instances, 1 }		
		});


	// Vertex buffers (unbounded)
	commonHeap->CreateTable(
		HeapType::VertexBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::Vertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE }
		});
	

	// Index buffers (unbounded)
	commonHeap->CreateTable(
		HeapType::TriangleBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
		{ 
			{ HeapSlot::Triangles, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});
	

	// Diffuse Textures (unbounded)
	commonHeap->CreateTable(
		HeapType::DiffuseTextures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::DiffuseTextures, UINT_MAX, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE }	
		});

	// Glow Textures (unbounded)
	commonHeap->CreateTable(
		HeapType::GlowTextures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ HeapSlot::GlowTextures, UINT_MAX, 4, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});


	auto rootParameters = commonHeap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0); 
	rootParameters.push_back(constantRootParam);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);  // register s0

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			logger::error("[RT] D3DX12SerializeVersionedRootSignature {}", (char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	DX::ThrowIfFailed(rootSignature->SetName(L"RT Root Signature"));
}

void RaytracedGI::CreateComputeRootSignature()
{
	if (rootSignatureCS)
		return;

	// UAV range
	computeHeap->CreateTable(
		ComputeHeapType::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ 
			{ ComputeHeapSlot::Final, 1 },
			{ ComputeHeapSlot::DiffuseGI, 1 },
			{ ComputeHeapSlot::SpecularGI, 1 },
			{ ComputeHeapSlot::SpecHitDist, 1 },
			{ ComputeHeapSlot::Depth, 1 }
		});

	// SRV range
	computeHeap->CreateTable(
		ComputeHeapType::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ ComputeHeapSlot::GeometryNormalDepth, 1 },
			{ ComputeHeapSlot::Albedo, 1 },
			{ ComputeHeapSlot::Reflectance, 1 }
		});

	auto rootParameters = computeHeap->GetRootParameters();

	/*CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0);
	rootParameters.push_back(constantRootParam);*/

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig;
	winrt::com_ptr<ID3DBlob> errorBlob;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignatureCS.put())));
	DX::ThrowIfFailed(rootSignatureCS->SetName(L"Compute Root Signature"));
}

void RaytracedGI::Initialize()
{
	
}


void RaytracedGI::ClearShaderCache()
{
	copyDepthCS = nullptr;  // This is actually optional
	CompileShaders();
}

void RaytracedGI::CompileShaders()
{
	CreateRootSignature();
	CreateComputeRootSignature();

	CompileRaytracingShaders();
	CompileDX12ComputeShaders();

	CompileComputeShaders();
}

void RaytracedGI::CompileRaytracingShaders()
{
	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"Data/Shaders/RaytracedGI/Raytracing/RayGeneration.hlsl");

	winrt::com_ptr<IDxcBlob> diffuseMissBlob, diffuseClosestHitBlob;
	ShaderUtils::CompileShader(diffuseMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl");
	ShaderUtils::CompileShader(diffuseClosestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl");

	winrt::com_ptr<IDxcBlob> shadowMissBlob;
	ShaderUtils::CompileShader(shadowMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/ShadowMiss.hlsl");

	winrt::com_ptr<IDxcBlob> specularMissBlob, specularClosestHitBlob;
	ShaderUtils::CompileShader(specularMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl", { { L"SPECULAR", nullptr } });
	ShaderUtils::CompileShader(specularClosestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl", { { L"SPECULAR", nullptr } });
	
	DX12::RTPipelineBuilder pipelineBuilder;

	// Init pipeline
	{
		// Libraries
		pipelineBuilder.AddRayGenLib(rayGenBlob.get(), L"RayGeneration");

		pipelineBuilder.AddMissLib(diffuseMissBlob.get(), L"DiffuseMiss");
		pipelineBuilder.AddMissLib(shadowMissBlob.get(), L"ShadowMiss");
		pipelineBuilder.AddMissLib(specularMissBlob.get(), L"SpecularMiss");

		pipelineBuilder.AddHitLib(diffuseClosestHitBlob.get(), L"DiffuseClosestHit");
		pipelineBuilder.AddHitLib(specularClosestHitBlob.get(), L"SpecularClosestHit");

		// Hit groups
		pipelineBuilder.AddHitGroup(L"DiffuseHitGroup", L"DiffuseClosestHit");
		pipelineBuilder.AddHitGroup(L"ShadowHitGroup");
		pipelineBuilder.AddHitGroup(L"SpecularHitGroup", L"SpecularClosestHit");

		// Shader + pipeline config
		pipelineBuilder.AddShaderConfig(20, 8);
		pipelineBuilder.AddGlobalRootSignature(rootSignature.get());
		pipelineBuilder.AddPipelineConfig(2);

		auto desc = pipelineBuilder.MakeStateObjectDesc();
		HRESULT hr = d3d12Device->CreateStateObject(desc, IID_PPV_ARGS(&pipelineRT));

		if (FAILED(hr)) {
			logger::error("CreateStateObject failed: {}", hr);
		}

		DX::ThrowIfFailed(hr);

		DX::ThrowIfFailed(pipelineRT->SetName(L"RT Pipeline"));
	}

	// Init shader tables
	{
		ID3D12StateObjectProperties* props;
		pipelineRT->QueryInterface(&props);

		auto shaderTablesSize = pipelineBuilder.ShaderTablesSize();

		logger::info("[RT] ShaderTablesSize: {}", shaderTablesSize);

		auto idDesc = BASIC_BUFFER_DESC;
		idDesc.Width = shaderTablesSize;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &idDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shaderIDs)));
		DX::ThrowIfFailed(shaderIDs->SetName(L"Shader IDs"));

		void* data;
		DX::ThrowIfFailed(shaderIDs->Map(0, nullptr, &data));
		const auto shaderTable = pipelineBuilder.WriteShaderIdentifiers(props, data);
		shaderIDs->Unmap(0, nullptr);

		commonHeap->SetShaderTable(shaderTable);

		logger::info("[RT] ShaderTable size: {}", shaderTable.HitGroupTable.StartAddress + shaderTable.HitGroupTable.SizeInBytes);

		props->Release();	
	}
}

void RaytracedGI::CompileDX12ComputeShaders()
{
	winrt::com_ptr<IDxcBlob> compositeBlob;
	ShaderUtils::CompileShader(compositeBlob, L"Data/Shaders/RaytracedGI/CompositeCS.hlsl", {}, L"cs_6_3");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignatureCS.get();
	computeDesc.CS = { compositeBlob->GetBufferPointer(), compositeBlob->GetBufferSize() };

	DX::ThrowIfFailed(d3d12Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineCS.put())));
	DX::ThrowIfFailed(pipelineCS->SetName(L"Compute Pipeline"));
}

void RaytracedGI::CompileComputeShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\RaytracedGI\\CopyDepthCS.hlsl", { { "SOME_MACRO", "0" } }, "cs_5_0")); rawPtr)
		copyDepthCS.attach(rawPtr);
}

RE::BSEventNotifyControl RaytracedGI::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a loadscreen
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		//auto& rtgi = globals::features::raytracedGI;

		logger::info("MenuOpenCloseEventHandler::ProcessEvent - Opening: {}", a_event->opening);

		if (a_event->opening) {			
			//rtgi.meshesCreated = false;		
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl RaytracedGI::TESLoadGameEventHandler::ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*)
{
	logger::info("TESLoadGameEventHandler::ProcessEvent {}", reinterpret_cast<intptr_t>(a_event));

	return RE::BSEventNotifyControl::kContinue;
}
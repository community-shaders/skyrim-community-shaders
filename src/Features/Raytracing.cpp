#include "Raytracing.h"

#include "Globals.h"
#include "State.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	Enabled,
	PathTracing,
	EnablePIXCapture)

////////////////////////////////////////////////////////////////////////////////////

void Raytracing::RestoreDefaultSettings()
{
	settings = {};
}

void Raytracing::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Raytracing::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Raytracing::DrawSettings()
{
	bool forcedDisabledReason = disableReason != DisableReason::None;

	if (forcedDisabledReason)
		ImGui::BeginDisabled();

	ImGui::Checkbox("Enabled", &settings.Enabled);

	ImGui::Checkbox("Path Tracing", &settings.PathTracing);

	if (forcedDisabledReason)
		ImGui::EndDisabled();

	if (forcedDisabledReason) {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Ray tracing is disabled: %s", [&]() {
			switch (disableReason) {
			case DisableReason::UnsupportedGPU:
				return "Unsupported GPU.";
			case DisableReason::OutdatedDrivers:
				return "Outdated Drivers.";
			case DisableReason::MissingPlugin:
				return "Missing 'CreationEngineRaytracing.dll', check your mod manager.";
			case DisableReason::InitFailed:
				return "Initialization Failed, check CreationEngineRaytracing.txt log";
			default:
				return "Unknown Reason";
			}
		}());
	}

	ImGui::Checkbox("Enable PIX Capture", &settings.EnablePIXCapture);

	if (settings.EnablePIXCapture) {
		if (ImGui::Button("Capture")) {
			pixCapture = true;
			pixCaptureStarted = false;
		}

	}

	if (mainTexture)
		ImGui::Image(mainTexture->srv, { 1280, 720 });
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

void Raytracing::CreateD3D12Device(ID3D11Device* d3d11Device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter)
{
	if (forcedDisabled)
		return;

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			auto pixGPUCapturerPath = GetLatestWinPixGpuCapturerPath();

			if (pixGPUCapturerPath.empty()) {
				logger::warn("[RT] PIX capture is enabled but binaries where not found.");
			} else {
				LoadLibrary(pixGPUCapturerPath.c_str());
			}
		}
	}

	// Set Device
	DX::ThrowIfFailed(d3d11Device->QueryInterface(IID_PPV_ARGS(&m_D3D11Device)));

	// Set Context Device
	DX::ThrowIfFailed(immediateContext->QueryInterface(IID_PPV_ARGS(&m_D3D11Context)));

	if (settings.EnablePIXCapture) {
		DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
	}

	// Create device
	DX::ThrowIfFailed(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_D3D12Device)));

	// Check hardware raytracing tier
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	if (SUCCEEDED(m_D3D12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
		if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
			logger::info("[Raytracing] Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
		else
			logger::warn("[Raytracing] Hardware ray tracing not supported.");
	}

	auto createCommandQueue = [&](D3D12_COMMAND_LIST_TYPE type, LPCWSTR name, winrt::com_ptr<ID3D12CommandQueue>& queue) {
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = type;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;
		DX::ThrowIfFailed(m_D3D12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
		DX::ThrowIfFailed(queue->SetName(name));
	};

	// Command Queues
	createCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Command Queue", m_CommandQueue);
	createCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, L"Compute Command Queue", m_ComputeCommandQueue);
	createCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY, L"Copy Command Queue", m_CopyCommandQueue);

	/*DX::ThrowIfFailed(m_D3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	DX::ThrowIfFailed(commandAllocator->SetName(L"Command Allocator"));

	DX::ThrowIfFailed(m_D3D12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));
	DX::ThrowIfFailed(commandList->SetName(L"Command List"));

	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));*/

	// Create Interop
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(m_D3D12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(m_D3D12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(m_D3D11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	InitializeCERaytracing(m_D3D11Device.get(), m_D3D12Device.get(), m_CommandQueue.get(), m_ComputeCommandQueue.get(), m_CopyCommandQueue.get());
}

void Raytracing::SetDevices(ID3D11Device* d3d11Device, ID3D12Device5* d3d12Device, ID3D11DeviceContext* immediateContext)
{
	DX::ThrowIfFailed(d3d11Device->QueryInterface(IID_PPV_ARGS(&m_D3D11Device)));
	DX::ThrowIfFailed(immediateContext->QueryInterface(IID_PPV_ARGS(&m_D3D11Context)));
	DX::ThrowIfFailed(d3d12Device->QueryInterface(IID_PPV_ARGS(&m_D3D12Device)));
}

void Raytracing::Load()
{
	Hooks::Install();
}

void Raytracing::PostPostLoad()
{
	creationEngineRaytracing = eastl::make_unique<CreationEngineRaytracing>();

	if (!creationEngineRaytracing->handle) {
		settings.Enabled = false;
		forcedDisabled = true;
		disableReason = DisableReason::MissingPlugin;
		return;
	}

	RE::GetINISetting("bReflectLODLand:Water")->data.b = false;
	RE::GetINISetting("bReflectLODObjects:Water")->data.b = false;
	RE::GetINISetting("bReflectLODTrees:Water")->data.b = false;
	RE::GetINISetting("bReflectSky:Water")->data.b = true;
}

void Raytracing::InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	if (initialized)
		return;

	bool result = creationEngineRaytracing->Initialize(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue);

	if (!result) {
		settings.Enabled = false;
		forcedDisabled = true;
		disableReason = DisableReason::InitFailed;

		logger::error("[Raytracing] Failed to initialize Creation Engine ray tracing.");
		return;
	}

	initialized = true;

	UpdateResolution();

	logger::info("[Raytracing] Successfully initialized Creation Engine ray tracing.");
}

bool Raytracing::UpdateResolution()
{
	uint2 resolution = { static_cast<uint32_t>(globals::state->screenSize.x), static_cast<uint32_t>(globals::state->screenSize.y) };

	if (resolution == m_Resolution)
		return false;

	m_Resolution = resolution;

	creationEngineRaytracing->SetResolution(m_Resolution.x, m_Resolution.y);

	return true;
}

void Raytracing::SetupResources()
{
	auto renderer = globals::game::renderer;

	D3D11_TEXTURE2D_DESC mainDesc;
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	mainTex.texture->GetDesc(&mainDesc);

	if (initialized) {
		creationEngineRaytracing->SetResolution(mainDesc.Width, mainDesc.Height);
	}

	featureData = eastl::make_unique<FeatureData>();
}

void Raytracing::UpdateFeatureData()
{
	auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();
	auto linearLighting = globals::features::linearLighting.GetCommonBufferData();

	std::memcpy(&featureData->ExtendedMaterials, &globals::features::extendedMaterials.settings, sizeof(ExtendedMaterials::Settings));
	std::memcpy(&featureData->WetnessEffects, &wetnessEffect, sizeof(WetnessEffects::PerFrame));
	std::memcpy(&featureData->CloudShadows, &globals::features::cloudShadows.settings, sizeof(CloudShadows::Settings));
	std::memcpy(&featureData->HairSpecular, &globals::features::hairSpecular.settings, sizeof(HairSpecular::Settings));
	std::memcpy(&featureData->ExtendedTranslucency, &globals::features::extendedTranslucency.GetCommonBufferData(), sizeof(ExtendedTranslucency::PerFrame));
	std::memcpy(&featureData->LinearLighting, &linearLighting, sizeof(LinearLighting::PerFrameData));

	static_assert(sizeof(FeatureData::ExtendedMaterials) == sizeof(ExtendedMaterials::Settings));
	static_assert(sizeof(FeatureData::WetnessEffects) == sizeof(WetnessEffects::PerFrame));
	static_assert(sizeof(FeatureData::CloudShadows) == sizeof(CloudShadows::Settings));
	static_assert(sizeof(FeatureData::HairSpecular) == sizeof(HairSpecular::Settings));
	static_assert(sizeof(FeatureData::ExtendedTranslucency) == sizeof(ExtendedTranslucency::PerFrame));
	static_assert(sizeof(FeatureData::LinearLighting) == sizeof(LinearLighting::PerFrameData));

	creationEngineRaytracing->UpdateFeatureData(featureData.get(), sizeof(FeatureData));
}

void Raytracing::DeferredPasses()
{
	if (!settings.Enabled)
		return;

	bool resolutionChanged = UpdateResolution();

	if (!mainTexture || resolutionChanged) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = m_Resolution.x;
		desc.Height = m_Resolution.y;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		mainTexture = eastl::make_unique<WrappedResource>(desc, m_D3D11Device.get(), m_D3D12Device.get());

		creationEngineRaytracing->SetCopyTarget(mainTexture->resource.get());
	}

	creationEngineRaytracing->WaitExecution();

	auto renderer = globals::game::renderer;

	auto* context = globals::d3d::context;

	context->CopyResource(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture, mainTexture->resource11);

	if (settings.PathTracing) {
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearRenderTargetView(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
	}

	if (pixCapture && pixCaptureStarted) {
		ga->EndCapture();

		pixCapture = false;
		pixCaptureStarted = false;
	}

	if (pixCapture && !pixCaptureStarted) {
		pixCaptureStarted = true;

		ga->BeginCapture();
	}
}
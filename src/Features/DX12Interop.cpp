#include "DX12Interop.h"

#include <dxgi1_6.h>

#include "Features/Upscaling.h"
#include "Features/Raytracing.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DX12Interop::Settings,
	EnablePIXCapture,
	EnableDebugDevice,
	DebugBreakCorruption,
	DebugBreakError,
	DebugBreakWarning)

bool DX12Interop::Active() const
{
	return active;
}

void DX12Interop::RestoreDefaultSettings()
{
	settings = {};
}

void DX12Interop::LoadSettings(json& o_json)
{
	settings = o_json;
}

void DX12Interop::SaveSettings(json& o_json)
{
	o_json = settings;
}


void DX12Interop::DrawSettings()
{
	ImGui::Checkbox("Enable PIX Capture", &settings.EnablePIXCapture);

	if (settings.EnablePIXCapture)
		if (ImGui::Button("Capture")) {
			pixCapture = true;
			pixCaptureStarted = false;
		}

	ImGui::Checkbox("Enable Debug Device", &settings.EnableDebugDevice);

	if (settings.EnableDebugDevice) {
		ImGui::Checkbox("Break on corruption", &settings.DebugBreakCorruption);
		ImGui::Checkbox("Break on error", &settings.DebugBreakError);
		ImGui::Checkbox("Break on warning", &settings.DebugBreakWarning);

	}
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

void DX12Interop::InitializePIX()
{
	if (!settings.EnablePIXCapture)
		return;

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

	DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
}

bool DX12Interop::D3D12Mode()
{
	auto& rt = globals::features::raytracing;

	if (rt.loaded)
		return true;

	auto& up = globals::features::upscaling;

	if (up.loaded && up.settings.frameGenerationMode)
		return true;

	return false;
}

void DX12Interop::Init(ID3D11Device* a_d3d11Device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter)
{
	auto& rt = globals::features::raytracing;

	if (!D3D12Mode())
		return;

	active = true;

	SetD3D11Device(a_d3d11Device);
	SetD3D11DeviceContext(immediateContext);

	InitializePIX();

	CreateD3D12Device(adapter);

	CreateInterop();

	if (rt.loaded)
		rt.InitializeCERaytracing(d3d11Device.get(), d3d12Device.get(), commandQueue.get(), computeCommandQueue.get(), copyCommandQueue.get());
}

void DX12Interop::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	if (settings.EnableDebugDevice) {
		winrt::com_ptr<ID3D12Debug3> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(TRUE);
		} else {
			logger::critical("[DX12Interop] Debug layer creation failed.");
		}		
	}

	auto& rt = globals::features::raytracing;

	auto featureLevel = rt.loaded ? D3D_FEATURE_LEVEL_12_1 : D3D_FEATURE_LEVEL_12_0;

	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, featureLevel, IID_PPV_ARGS(&d3d12Device)));

	if (settings.EnableDebugDevice) {
		winrt::com_ptr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, settings.DebugBreakCorruption ? TRUE : FALSE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, settings.DebugBreakError ? TRUE : FALSE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, settings.DebugBreakWarning ? TRUE : FALSE);
		} else {
			logger::critical("[DX12Interop] Debug break creation failed.");
		}
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&computeCommandQueue)));

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));

	DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(), nullptr, IID_PPV_ARGS(&commandList)));
	commandList->Close();
}

void DX12Interop::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);
}

void DX12Interop::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12Interop::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

void DX12Interop::SetupResources()
{
	auto renderer = globals::game::renderer;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);

	// Create depth buffer
	auto texDesc = mainDesc;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	sharedResources.depth = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());

	// Create motion vector buffer
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	mainDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.motionVector = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());

	// 
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.main = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());

	// Upscaler reactive mask
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	mainDesc.Format = DXGI_FORMAT_R8_UNORM;
	sharedResources.reactiveMask = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());
}
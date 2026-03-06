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
	EnableDebugDevice)

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

	ImGui::Checkbox("Enable Debug Device", &settings.EnableDebugDevice);
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

void DX12Interop::Init(ID3D11Device* a_d3d11Device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter)
{
	auto& rt = globals::features::raytracing;
	auto& up = globals::features::upscaling;

	if (!rt.loaded && !(up.loaded && up.IsFrameGenerationActive()))
		return;

	active = true;

	SetD3D11Device(a_d3d11Device);
	SetD3D11DeviceContext(immediateContext);

	InitializePIX();

	CreateD3D12Device(adapter);

	if (rt.loaded)
		rt.InitializeCERaytracing(d3d11Device.get(), d3d12Device.get(), commandQueue.get(), nullptr, nullptr);
}

void DX12Interop::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}
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

void DX12Interop::CreateSharedResources()
{
	auto renderer = globals::game::renderer;

	// Create depth buffer
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());

	// Create motion vector buffer
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	motionVectorBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
}
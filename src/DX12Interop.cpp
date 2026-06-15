#include "DX12Interop.h"

#include <dxgi1_6.h>

#include "Features/Raytracing.h"
#include "Features/Upscaling.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <KnownFolders.h>

DX12Interop::~DX12Interop()
{
	if (fenceEvent) {
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}

void DX12Interop::InitializePIX()
{
	if (!globals::state->interopLoadPIX)
		return;

	auto getLatestWinPixGpuCapturerPath = [] {
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
			logger::warn("[DX12Interop] PIX installation not found");
		}

		return std::wstring{ pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll" };
	};

	// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
	// This may happen if the application is launched through the PIX UI.
	if (GetModuleHandleW(L"WinPixGpuCapturer.dll") == 0) {
		auto pixGPUCapturerPath = getLatestWinPixGpuCapturerPath();

		if (pixGPUCapturerPath.empty()) {
			logger::warn("[DX12Interop] PIX capture is enabled but binaries where not found.");
		} else {
			LoadLibraryW(pixGPUCapturerPath.c_str());
		}
	}

	DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
}

void DX12Interop::Init(ID3D11Device* a_d3d11Device, ID3D11DeviceContext* a_immediateContext, IDXGIAdapter* a_adapter)
{
	if (!D3D12Mode())
		return;

	active = true;

	SetD3D11Device(a_d3d11Device);
	SetD3D11DeviceContext(a_immediateContext);

	InitializePIX();

	CreateD3D12Device(a_adapter);

	CreateInterop();

	auto& rt = globals::features::raytracing;
	if (rt.loaded)
		rt.InitializeCERaytracing(d3d11Device.get(), d3d12Device.get(), commandQueue.get(), computeCommandQueue.get(), copyCommandQueue.get());
}

bool DX12Interop::Active() const
{
	return active;
}

bool DX12Interop::D3D12Mode()
{
	auto& upscaling = globals::features::upscaling;
	if (upscaling.loaded && upscaling.HasFrameGenModule())
		return true;

	auto& rt = globals::features::raytracing;
	if (rt.loaded)
		return true;

	return false;
}

void DX12Interop::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	const bool enableDebug = !globals::state->interopLoadPIX && globals::state->interopDebugDevice;

	if (enableDebug) {
		winrt::com_ptr<ID3D12Debug3> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(TRUE);
		} else {
			logger::critical("[DX12Interop] Debug layer creation failed");
		}

		winrt::com_ptr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}

	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));

	if (enableDebug) {
		winrt::com_ptr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
		} else {
			logger::critical("[DX12Interop] Debug break creation failed");
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
}

void DX12Interop::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!fenceEvent)
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
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
	if (!active)
		return;

	auto renderer = globals::game::renderer;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);

	// Main render target
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.main = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());
	sharedResources.upscaleOutput = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());

	// Depth
	mainDesc.Format = DXGI_FORMAT_R32_FLOAT;
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.depth = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());

	// Motion vector
	mainDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.motionVector = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());

	// Upscaler reactive mask
	mainDesc.Format = DXGI_FORMAT_R8_UNORM;
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	sharedResources.reactiveMask = new WrappedResource(mainDesc, d3d11Device.get(), d3d12Device.get());
}

UINT DX12Interop::GetFrameContextIndex() const
{
	return globals::state->frameCount % kMaxFramesInFlight;
}

#include "DX12SwapChain.h"
#include <dxgi1_6.h>

#include "FidelityFX.h"
#include "Streamline.h"
#include "Upscaling.h"

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
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

	if (globals::streamline->initialized)
		globals::streamline->CheckFeatures(a_adapter);
}

void DX12SwapChain::CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	IDXGIFactory4* dxgiFactory;
	DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

	swapChainDesc = {};
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = a_swapChainDesc.Flags;

	winrt::com_ptr<IDXGISwapChain4> swapChainCOM;

	DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.get(),
		a_swapChainDesc.OutputWindow,
		&swapChainDesc,
		nullptr,
		nullptr,
		(IDXGISwapChain1**)swapChainCOM.put()));

	swapChain = swapChainCOM.detach();

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	FidelityFX::GetSingleton()->SetupFrameGeneration();

	swapChain->SetMaximumFrameLatency(1);
	frameLatencyWaitableObject = swapChain->GetFrameLatencyWaitableObject();

	QueryPerformanceFrequency(&qpf);

	refreshRate = GetRefreshRate(a_swapChainDesc.OutputWindow);
}

void DX12SwapChain::CreateInterop()
{
	for (int i = 0; i < 2; i++) {
		HANDLE sharedFenceHandle;
		DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fences[i])));
		DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fences[i].get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
		DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fences[i])));
		CloseHandle(sharedFenceHandle);
	}

	swapChainProxy = new DXGISwapChainProxy(swapChain);

	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = swapChainDesc.Width;
	texDesc11.Height = swapChainDesc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = swapChainDesc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.Usage = D3D11_USAGE_DEFAULT;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	texDesc11.CPUAccessFlags = 0;
	texDesc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

	swapChainBufferWrapped = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());

	if (reshade::create_effect_runtime(reshade::api::device_api::d3d11, d3d11Device.get(), d3d11Context.get(), swapChainProxy, "ReShade", &reShadeRuntime)) {
		auto device = reShadeRuntime->get_device();

		reshade::api::resource reshadeSwapChainResource = device->get_resource_from_view(reshade::api::resource_view{ reinterpret_cast<uintptr_t>(swapChainBufferWrapped->rtv) });
		reshade::api::resource_desc reshadeSwapChainDesc = device->get_resource_desc(reshadeSwapChainResource);

		device->create_resource_view(reshadeSwapChainResource, reshade::api::resource_usage::render_target, reshade::api::resource_view_desc(reshade::api::format_to_default_typed(reshadeSwapChainDesc.texture.format, 0), 0, 1, 0, 1), &reshadeSwapChainRTV);
		device->create_resource_view(reshadeSwapChainResource, reshade::api::resource_usage::render_target, reshade::api::resource_view_desc(reshade::api::format_to_default_typed(reshadeSwapChainDesc.texture.format, 1), 0, 1, 0, 1), &reshadeSwapChainRTVsRGB);
	}
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	return swapChainProxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	if (globals::streamline->initialized) {
		globals::streamline->slSetD3DDevice(d3d11Device.get());
		globals::streamline->PostDevice();
	}
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(void** ppSurface)
{
	*ppSurface = swapChainBufferWrapped->resource11;
	return S_OK;
}

void DX12SwapChain::RenderReShadeEffects()
{
	if (reShadeRuntime) {
		reShadeRuntime->render_effects(reShadeRuntime->get_command_queue()->get_immediate_command_list(), reshadeSwapChainRTV, reshadeSwapChainRTVsRGB);
	}
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT)
{
	// If ReShade is loaded, call it now
	reshade::update_and_present_effect_runtime(reShadeRuntime);

	// Wait for D3D11 work to finish
	d3d11Context->Flush();

	// New frame, reset
	DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
	DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

	// Update the fence value for the current frame
	fenceValues[frameIndex]++;

	// Signal fence from D3D11
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fences[frameIndex].get(), fenceValues[frameIndex]));

	// Wait for D3D11 to finish on D3D12 side
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fences[frameIndex].get(), fenceValues[frameIndex]));

	winrt::com_ptr<ID3D12Resource> swapChainBuffer;
	DX::ThrowIfFailed(swapChain->GetBuffer(frameIndex, IID_PPV_ARGS(&swapChainBuffer)));

	// Copy shared texture to swap chain buffer
	{
		auto fakeSwapChain = swapChainBufferWrapped->resource.get();
		auto realSwapChain = swapChainBuffer.get();
		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}

		commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
	}

	auto upscaling = globals::upscaling;

	bool useFrameGeneration = upscaling->settings.frameGenerationMode && !RE::UI::GetSingleton()->GameIsPaused();

	FidelityFX::GetSingleton()->Present(useFrameGeneration);

	DX::ThrowIfFailed(commandLists[frameIndex]->Close());

	ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
	commandQueue->ExecuteCommandLists(1, commandListsToExecute);

	// Present the frame
	DX::ThrowIfFailed(swapChain->Present(upscaling->settings.vsyncMode ? std::max(1u, SyncInterval) : 0, 0));

	// Wait for the frame to finish to minimise latency
	WaitForSingleObject(frameLatencyWaitableObject, 500);

	// Frame limiter for V-Sync and VRR
	if (!upscaling->settings.vsyncMode && upscaling->settings.frameLimitMode)
		FrameLimiter(useFrameGeneration);

	// Update the frame index.
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER].RTV = swapChainBufferWrapped->rtv;

	return S_OK;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		*ppDevice = d3d11Device.get();
		return S_OK;
	}

	return swapChain->GetDevice(uuid, ppDevice);
}

static void TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void DX12SwapChain::FrameLimiter(bool a_useFrameGeneration)
{
	double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;

	int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * (a_useFrameGeneration ? 0.5 : 1.0)));

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (delta < targetFrameTicks) {
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	}
	QueryPerformanceCounter(&lastFrame);
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double DX12SwapChain::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	if (a_texDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
		flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
		flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (!(a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
		flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	if (!(a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET))
		flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_RESOURCE_DESC desc12{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, a_texDesc.Width, a_texDesc.Height, (UINT16)a_texDesc.ArraySize, (UINT16)a_texDesc.MipLevels, a_texDesc.Format, { a_texDesc.SampleDesc.Count, a_texDesc.SampleDesc.Quality }, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags };
	D3D12_HEAP_PROPERTIES heapProp = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };

	DX::ThrowIfFailed(a_d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_SHARED, &desc12, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));

	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(a_d3d12Device->CreateSharedHandle(resource.get(), nullptr, GENERIC_ALL, nullptr, &sharedHandle));

	DX::ThrowIfFailed(a_d3d11Device->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&resource11)));

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		} else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
	}
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	auto ret = swapChain->QueryInterface(riid, ppvObj);
	if (*ppvObj)
		*ppvObj = this;
	return ret;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return swapChain->AddRef();
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	return swapChain->Release();
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return globals::dx12SwapChain->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return globals::dx12SwapChain->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID, _COM_Outptr_ void** ppSurface)
{
	return globals::dx12SwapChain->GetBuffer(ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	return swapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	return swapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}
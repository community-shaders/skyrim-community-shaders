#include "DX12SwapChain.h"
#include <dxgi1_6.h>

#include "FidelityFX.h"
#include "Streamline.h"

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(), nullptr, IID_PPV_ARGS(&commandList)));

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

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
	fullscreenDesc.Windowed = TRUE;

	winrt::com_ptr<IDXGISwapChain4> swapChainCOM;

	DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.get(),
		a_swapChainDesc.OutputWindow,
		&swapChainDesc,
		&fullscreenDesc,
		nullptr,
		(IDXGISwapChain1**)swapChainCOM.put()));

	swapChain = swapChainCOM.detach();

	FidelityFX::GetSingleton()->WrapSwapChain();
}

void DX12SwapChain::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d3d12OnlyFence)));
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

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
	swapChainBufferWrappedDummy[0] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
	swapChainBufferWrappedDummy[1] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
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

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	// Wait for D3D11 work to finish
	auto index = swapChain->GetCurrentBackBufferIndex();

	d3d11Context->CopyResource(swapChainBufferWrappedDummy[index]->resource11, swapChainBufferWrapped->resource11);

	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), currentSharedFenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), currentSharedFenceValue));
	currentSharedFenceValue++;

	winrt::com_ptr<ID3D12Resource> swapChainBuffer;
	DX::ThrowIfFailed(swapChain->GetBuffer(index, IID_PPV_ARGS(&swapChainBuffer)));

	// Copy D3D11 result to D3D12
	{
		auto fakeSwapChain = swapChainBufferWrappedDummy[index]->resource.get();
		auto realSwapchain = swapChainBuffer.get();
		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapchain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
			commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}

		commandList->CopyResource(realSwapchain, fakeSwapChain);

		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapchain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
			commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
	}

	FidelityFX::GetSingleton()->Present();

	DX::ThrowIfFailed(commandList->Close());

	ID3D12CommandList* commandLists[] = { commandList.get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	auto hr = swapChain->Present(SyncInterval, Flags);
	
	swapChain->SetMaximumFrameLatency(1);
	auto waitable = swapChain->GetFrameLatencyWaitableObject();
	WaitForSingleObject(waitable, 1000);

	// New frame, reset
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	return hr;
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
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	return swapChain->QueryInterface(riid, ppvObj);
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
	return swapChain->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return DX12SwapChain::GetSingleton()->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID, _COM_Outptr_ void** ppSurface)
{
	return DX12SwapChain::GetSingleton()->GetBuffer(ppSurface);
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

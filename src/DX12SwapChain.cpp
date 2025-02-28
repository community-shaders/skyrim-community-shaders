#include "DX12SwapChain.h"
#include <dxgi1_6.h>

#include "Features/TerrainBlending.h"
#include <FidelityFX.h>

void DX12SwapChain::CacheFramebuffer()
{
	auto frameBuffer = (FrameBuffer*)mappedFrameBuffer->pData;
	frameBufferCached = *frameBuffer;
	mappedFrameBuffer = nullptr;
}

void DX12SwapChain::Main_RenderDepth::thunk(bool a1, bool a2)
{
	DX12SwapChain::GetSingleton()->UpdateFrameBuffer();
	func(a1, a2);
}

void DX12SwapChain::UpdateFrameBuffer()
{
	//auto cameraViewInverseAdjusted = frameBufferCached.CameraViewInverse.Transpose();
	//cameraViewInverseAdjusted._41 += frameBufferCached.CameraPosAdjust.x;
	//cameraViewInverseAdjusted._42 += frameBufferCached.CameraPosAdjust.y;
	//cameraViewInverseAdjusted._43 += frameBufferCached.CameraPosAdjust.z;

	//auto worldToViewMatrix = cameraViewInverseAdjusted.Invert();
	//auto viewToClipMatrix = frameBufferCached.CameraProjUnjittered.Transpose();

	//static auto prevRenderedWorldToViewMatrix = worldToViewMatrix;
	//static auto prevRenderedViewToClipMatrix = viewToClipMatrix;

	//sl::ReflexCameraData inCameraData{};
	//inCameraData.worldToViewMatrix = *(sl::float4x4*)&worldToViewMatrix;
	//inCameraData.prevRenderedWorldToViewMatrix = *(sl::float4x4*)&prevRenderedWorldToViewMatrix;
	//inCameraData.viewToClipMatrix = *(sl::float4x4*)&viewToClipMatrix;
	//inCameraData.prevRenderedViewToClipMatrix = *(sl::float4x4*)&prevRenderedViewToClipMatrix;

	//auto streamline = Streamline::GetSingleton();
	//auto frameToken = streamline->frameTokenPrevious;
	//static uint32_t lastFrameID = 0;

	//if (frameToken && streamline->frameID > lastFrameID) {
	//	prevRenderedWorldToViewMatrix = worldToViewMatrix;
	//	prevRenderedViewToClipMatrix = viewToClipMatrix;
	//	if (sl::Result::eOk != streamline->slReflexSetCameraData(streamline->viewport, *frameToken, inCameraData)) {
	//		logger::error("error");
	//	}
	//	lastFrameID = streamline->frameID;
	//}
}

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* adapter)
{
	//winrt::com_ptr<ID3D12Debug> debugController;
	//if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
	//	debugController->EnableDebugLayer();
	//}

	//auto streamline = Streamline::GetSingleton();
	//auto slD3D12CreateDevice = reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(streamline->interposer, "D3D12CreateDevice"));

	//streamline->featureDLSSG = true;
	//streamline->featureReflex = true;

	DX::ThrowIfFailed(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(), nullptr, IID_PPV_ARGS(&commandList)));
	DX::ThrowIfFailed(commandList->Close());

	//streamline->slSetD3DDevice(d3d12Device.get());

	//DXGI_ADAPTER_DESC adapterDesc;
	//adapter->GetDesc(&adapterDesc);

	//sl::AdapterInfo adapterInfo;
	//adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	//adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	//streamline->slIsFeatureLoaded(sl::kFeatureDLSS_G, streamline->featureDLSSG);
	//if (streamline->featureDLSSG && !REL::Module::IsVR()) {
	//	logger::info("[Streamline] DLSSG feature is loaded");
	//	streamline->featureDLSSG = streamline->slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;
	//} else {
	//	logger::info("[Streamline] DLSSG feature is not loaded");
	//	sl::FeatureRequirements featureRequirements;
	//	sl::Result result = streamline->slGetFeatureRequirements(sl::kFeatureDLSS_G, featureRequirements);
	//	if (result != sl::Result::eOk) {
	//		logger::info("[Streamline] DLSSG feature failed to load due to: {}", magic_enum::enum_name(result));
	//	}
	//}

	//streamline->slIsFeatureLoaded(sl::kFeatureReflex, streamline->featureReflex);
	//if (streamline->featureReflex) {
	//	logger::info("[Streamline] Reflex feature is loaded");
	//	streamline->featureReflex = streamline->slIsFeatureSupported(sl::kFeatureReflex, adapterInfo) == sl::Result::eOk;
	//} else {
	//	logger::info("[Streamline] Reflex feature is not loaded");
	//	sl::FeatureRequirements featureRequirements;
	//	sl::Result result = streamline->slGetFeatureRequirements(sl::kFeatureReflex, featureRequirements);
	//	if (result != sl::Result::eOk) {
	//		logger::info("[Streamline] Reflex feature failed to load due to: {}", magic_enum::enum_name(result));
	//	}
	//}
	//streamline->PostDevice();
}

winrt::com_ptr<IDXGISwapChain4> swapChainy;

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
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
	fullscreenDesc.Windowed = TRUE;

	DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.get(),
		a_swapChainDesc.OutputWindow,
		&swapChainDesc,
		&fullscreenDesc,
		nullptr,
		(IDXGISwapChain1**)swapChainy.put()));

	swapChain = swapChainy.get();

	FidelityFX::GetSingleton()->WrapSwapChain();
}

void DX12SwapChain::CreateInterop()
{
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));

	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));

	HANDLE sharedFenceHandle2;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence2)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence2.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle2));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle2, IID_PPV_ARGS(&d3d11Fence2)));

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
	swapChainBufferWrappedDummy = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	return swapChainProxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	//stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_d3d11Context);
	//stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_d3d11Context);
}

HRESULT DX12SwapChain::GetBuffer(void** ppSurface)
{
	*ppSurface = swapChainBufferWrapped->resource11;
	return S_OK;
}

void DX12SwapChain::BeginFrame()
{
}

HRESULT DX12SwapChain::Present(UINT, UINT)
{
	DX::ThrowIfFailed(commandAllocator->Reset());

	bool needsReset2 = true;
	if (needsReset2) {
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
		needsReset2 = false;
		OpenSharedHandles();
	}

	// Wait for D3D11 work to finish
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), currentSharedFenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), currentSharedFenceValue));

	//Streamline::GetSingleton()->Present();

	currentSharedFenceValue++;

	auto index = swapChain->GetCurrentBackBufferIndex();

	winrt::com_ptr<ID3D12Resource> swapChainBuffer;
	DX::ThrowIfFailed(swapChain->GetBuffer(index, IID_PPV_ARGS(&swapChainBuffer)));

	// Copy D3D11 result to D3D12
	{
		auto fakeSwapChain = swapChainBufferWrapped->resource.get();
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

	// Because D3D12 was accessing D3D11 resources we need to sync
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence2.get(), currentSharedFenceValue));
	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence2.get(), currentSharedFenceValue));

	currentSharedFenceValue++;

	DX::ThrowIfFailed(commandList->Close());

	ID3D12CommandList* commandLists[] = { commandList.get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	//auto streamline = Streamline::GetSingleton();

	//auto frameToken = streamline->GetFrameToken(streamline->frameID);

	//streamline->slPCLSetMarker2(sl::PCLMarker::eRenderSubmitEnd, *frameToken);
	//streamline->slPCLSetMarker2(sl::PCLMarker::ePresentStart, *frameToken);

	//    float4x4 worldToViewMatrix;
	//float4x4 viewToClipMatrix;
	//float4x4 prevRenderedWorldToViewMatrix;
	//float4x4 prevRenderedViewToClipMatrix;

	//auto cameraViewInverseAdjusted = frameBufferCached.CameraViewInverse.Transpose();
	//cameraViewInverseAdjusted._41 += frameBufferCached.CameraPosAdjust.x;
	//cameraViewInverseAdjusted._42 += frameBufferCached.CameraPosAdjust.y;
	//cameraViewInverseAdjusted._43 += frameBufferCached.CameraPosAdjust.z;

	//auto worldToViewMatrix = cameraViewInverseAdjusted.Invert();
	//auto viewToClipMatrix = frameBufferCached.CameraProjUnjittered.Transpose();

	//static auto prevRenderedWorldToViewMatrix = worldToViewMatrix;
	//static auto prevRenderedViewToClipMatrix = viewToClipMatrix;

	////! Sets Reflex camera data
	////!
	////! Call this method to inform Reflex of upcoming camera data
	////!
	////! @param viewport The viewport the camera corresponds to
	////! @param frame The frame to set camera data for
	////! @param inCameraData Camera data for an upcoming render frame
	////! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
	////!
	////! This method is thread safe.
	////using PFun_slReflexSetCameraData = sl::Result(const sl::ViewportHandle& viewport, const sl::FrameToken& frame, const sl::ReflexCameraData& inCameraData);

	//sl::ReflexCameraData inCameraData{};
	//inCameraData.worldToViewMatrix = *(sl::float4x4*)&worldToViewMatrix;
	//inCameraData.prevRenderedWorldToViewMatrix = *(sl::float4x4*)&prevRenderedWorldToViewMatrix;
	//inCameraData.viewToClipMatrix = *(sl::float4x4*)&viewToClipMatrix;
	//inCameraData.prevRenderedViewToClipMatrix = *(sl::float4x4*)&prevRenderedViewToClipMatrix;

	//if (sl::Result::eOk != streamline->slReflexSetCameraData(streamline->viewport, *frameToken, inCameraData))
	//{
	//	logger::error("ererer");
	//}

	//prevRenderedWorldToViewMatrix = worldToViewMatrix;
	//prevRenderedViewToClipMatrix = viewToClipMatrix;

	//! Gets predicted Reflex camera data
	//!
	//! Call this method to get a prediction of upcoming camera data
	//!
	//! @param viewport The viewport the camera corresponds to
	//! @param frame The frame to get camera data for (if available)
	//! @param outCameraData Predicted Camera data for an upcoming render frame
	//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
	//!
	//!
	//! This method is thread safe.
	FidelityFX::GetSingleton()->Present();

	auto hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

	//streamline->slPCLSetMarker2(sl::PCLMarker::ePresentEnd, *frameToken);

	DX::ThrowIfFailed(commandQueue->Signal(d3d12OnlyFence.get(), currentSharedFenceValue));

	// Wait until the fence has been processed.
	DX::ThrowIfFailed(d3d12OnlyFence->SetEventOnCompletion(currentSharedFenceValue, fenceEvent));
	WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE);

	//frameToken = streamline->GetFrameToken(++streamline->frameID);

	//sl::ReflexPredictedCameraData outCameraData;
	//if (sl::Result::eOk == streamline->slReflexGetPredictedCameraData(streamline->viewport, *frameToken, outCameraData))
	//{
	//	inCameraData.worldToViewMatrix = outCameraData.predictedWorldToViewMatrix;
	//	inCameraData.prevRenderedWorldToViewMatrix = *(sl::float4x4*)&prevRenderedWorldToViewMatrix;
	//	inCameraData.viewToClipMatrix = outCameraData.predictedViewToClipMatrix;
	//	inCameraData.prevRenderedViewToClipMatrix = *(sl::float4x4*)&prevRenderedViewToClipMatrix;
	//
	//	streamline->slReflexSetCameraData(streamline->viewport, *frameToken, inCameraData);
	//}

	//streamline->slReflexSleep(*frameToken);

	//streamline->slPCLSetMarker2(sl::PCLMarker::eSimulationStart, *frameToken);
	//streamline->slPCLSetMarker2(sl::PCLMarker::eSimulationEnd, *frameToken);
	//streamline->slPCLSetMarker2(sl::PCLMarker::eRenderSubmitStart, *frameToken);

	return hr;
}

void DX12SwapChain::OpenSharedHandles()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();

	for (int i = 0; i < RE::RENDER_TARGET::kTOTAL; i++) {
		renderTargetsD3D12[i] = RenderTargetDataD3D12::ConvertD3D11TextureToD3D12(&renderer->GetRuntimeData().renderTargets[i], d3d12Device.get());
	}
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

RenderTargetDataD3D12 RenderTargetDataD3D12::ConvertD3D11TextureToD3D12(RE::BSGraphics::RenderTargetData* rtData, ID3D12Device* a_d3d12Device)
{
	RenderTargetDataD3D12 renderTargetData{};

	if (!rtData->texture)
		return renderTargetData;

	D3D11_TEXTURE2D_DESC texDesc{};
	rtData->texture->GetDesc(&texDesc);

	if (!(texDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
		return renderTargetData;

	// Query the DXGIResource1 interface to access shared NT handle
	winrt::com_ptr<IDXGIResource1> dxgiResource1;
	DX::ThrowIfFailed(rtData->texture->QueryInterface(IID_PPV_ARGS(&dxgiResource1)));

	// Create the shared NT handle
	HANDLE sharedNtHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedNtHandle));

	// Open the shared handle in D3D12
	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedNtHandle, IID_PPV_ARGS(&renderTargetData.d3d12Resource)));

	return renderTargetData;
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

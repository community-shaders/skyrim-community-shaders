#include "D3D12Loader.h"

#include <vector>

namespace D3D12Loader
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		bool g_attempted = false;
		bool g_loaded = false;

		ComPtr<ID3D12Device> g_device;
		ComPtr<ID3D12CommandQueue> g_queue;
		ComPtr<ID3D11On12Device> g_on12;
		ComPtr<IDXGIFactory2> g_factory;

		/// The engine asks for a D3D11 feature level; D3D12 wants one to create the device
		/// with. Take the highest the caller will accept, defaulting to 11_0, which is what
		/// Skyrim actually uses.
		D3D_FEATURE_LEVEL PickFeatureLevel(const D3D_FEATURE_LEVEL* levels, UINT count)
		{
			D3D_FEATURE_LEVEL best = D3D_FEATURE_LEVEL_11_0;

			for (UINT i = 0; i < count; i++) {
				if (levels[i] > best && levels[i] <= D3D_FEATURE_LEVEL_12_1)
					best = levels[i];
			}
			return best;
		}
	}

	namespace
	{
		/**
		 * @brief Presents a blit-model D3D11 back buffer over a flip-model D3D12 swap chain.
		 *
		 * Two mismatches have to be bridged here, and they are the reason this proxy exists
		 * rather than handing the engine the real swap chain.
		 *
		 * A swap chain created on a D3D12 queue hands out ID3D12Resource back buffers, but the
		 * engine asks GetBuffer for an ID3D11Texture2D. Wrapping the D3D12 resources through
		 * ID3D11On12Device::CreateWrappedResource solves that much.
		 *
		 * It does not solve the second mismatch. D3D12 requires the flip model, where the
		 * current back buffer rotates and callers are expected to follow it with
		 * GetCurrentBackBufferIndex. The engine is a blit-model D3D11 renderer: it calls
		 * GetBuffer(0) once, builds a render target view from it, and renders to that same
		 * view every frame. Handing it buffer 0 would be correct one frame in three.
		 *
		 * So the engine gets a stable render target of its own, and Present copies that into
		 * whichever back buffer is current before presenting. The copy runs on the same queue
		 * D3D11On12 flushes to, so it is ordered against the engine's translated work without
		 * extra synchronisation.
		 */
		class BlitModelSwapChain : public IDXGISwapChain
		{
		public:
			BlitModelSwapChain(
				ComPtr<IDXGISwapChain3> real,
				ComPtr<ID3D11On12Device> on12,
				ComPtr<ID3D11Device> device11,
				const DXGI_SWAP_CHAIN_DESC& desc)
			: m_real(std::move(real)), m_on12(std::move(on12)),
			  m_device11(std::move(device11)), m_desc(desc)
			{
				m_device11->GetImmediateContext(&m_context11);
			}

			HRESULT Initialise()
			{
				auto* device = D3D12Loader::GetDevice();

				D3D12_HEAP_PROPERTIES heap = {};
				heap.Type = D3D12_HEAP_TYPE_DEFAULT;

				D3D12_RESOURCE_DESC rt = {};
				rt.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				rt.Width = m_desc.BufferDesc.Width;
				rt.Height = m_desc.BufferDesc.Height;
				rt.DepthOrArraySize = 1;
				rt.MipLevels = 1;
				rt.Format = m_desc.BufferDesc.Format;
				rt.SampleDesc.Count = 1;
				rt.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				rt.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rt,
					D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&m_gameTarget));
				if (FAILED(hr))
					return hr;

				m_gameTarget->SetName(L"CS engine back buffer");

				// The wrapped resource's in/out states tell D3D11On12 what to transition to on
				// acquire and release. The engine renders to it, and Present copies out of it.
				D3D11_RESOURCE_FLAGS flags = {};
				flags.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

				hr = m_on12->CreateWrappedResource(m_gameTarget.Get(), &flags,
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE,
					IID_PPV_ARGS(&m_wrapped));
				if (FAILED(hr))
					return hr;

				hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
					IID_PPV_ARGS(&m_allocator));
				if (FAILED(hr))
					return hr;

				hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
					m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_list));
				if (FAILED(hr))
					return hr;
				m_list->Close();

				hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
				if (FAILED(hr))
					return hr;

				m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (!m_fenceEvent)
					return HRESULT_FROM_WIN32(GetLastError());

				// Optional: only present when the debug layer is on.
				D3D12Loader::GetDevice()->QueryInterface(IID_PPV_ARGS(&m_infoQueue));

				// The engine renders into the wrapped resource from the first frame, so it has
				// to start out owned by D3D11.
				ID3D11Resource* acquire[] = { m_wrapped.Get() };
				m_on12->AcquireWrappedResources(acquire, 1);
				return S_OK;
			}

			// IUnknown
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
			{
				if (!ppv)
					return E_POINTER;

				if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
					riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain)) {
					*ppv = static_cast<IDXGISwapChain*>(this);
					AddRef();
					return S_OK;
				}
				// Anything newer than IDXGISwapChain goes to the real chain, which is where the
				// colour-space and HDR metadata calls need to land anyway.
				return m_real->QueryInterface(riid, ppv);
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG n = --m_refs;
				if (!n)
					delete this;
				return n;
			}

			// IDXGIObject
			HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT n, const void* p) override { return m_real->SetPrivateData(g, n, p); }
			HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return m_real->SetPrivateDataInterface(g, p); }
			HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* n, void* p) override { return m_real->GetPrivateData(g, n, p); }
			HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return m_real->GetParent(riid, pp); }

			// IDXGIDeviceSubObject: the engine expects its D3D11 device back, not the D3D12 one.
			HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** pp) override
			{
				return m_device11->QueryInterface(riid, pp);
			}

			// IDXGISwapChain
			HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override
			{
				CopyToBackBufferAndPresent(SyncInterval, Flags);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override
			{
				if (!ppSurface)
					return E_POINTER;

				// The engine only ever asks for buffer 0, and it always means "the thing I
				// render to". Give it the stable target rather than a rotating back buffer.
				if (Buffer == 0)
					return m_wrapped->QueryInterface(riid, ppSurface);

				return m_real->GetBuffer(Buffer, riid, ppSurface);
			}

			HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override { return m_real->SetFullscreenState(Fullscreen, pTarget); }
			HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override { return m_real->GetFullscreenState(pFullscreen, ppTarget); }

			HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override
			{
				if (!pDesc)
					return E_POINTER;
				// Report what the engine asked for. It branches on BufferCount and SwapEffect,
				// and the flip-model values we actually created are not what it expects.
				*pDesc = m_desc;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override
			{
				// Deliberately unimplemented while the path is being brought up: a resize has to
				// rebuild the wrapped target and every view the engine holds on it. Reporting
				// success without doing that would corrupt rendering silently.
				logger::warn("[D3D12] ResizeBuffers is not implemented on the D3D12 path");
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* p) override { return m_real->ResizeTarget(p); }
			HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** pp) override { return m_real->GetContainingOutput(pp); }
			HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { return m_real->GetFrameStatistics(p); }
			HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* p) override { return m_real->GetLastPresentCount(p); }

			ID3D11Texture2D* GetEngineTarget() const { return m_wrapped.Get(); }

		private:
			~BlitModelSwapChain()
			{
				if (m_fenceEvent)
					CloseHandle(m_fenceEvent);
			}

			/// Drains the debug layer into our own log. Without this its messages only reach a
			/// attached debugger, which is exactly what is unavailable when the game is driven
			/// from a script.
			void DrainDebugMessages()
			{
				if (!m_infoQueue)
					return;

				const UINT64 count = m_infoQueue->GetNumStoredMessages();

				for (UINT64 i = 0; i < count; i++) {
					SIZE_T length = 0;
					if (FAILED(m_infoQueue->GetMessage(i, nullptr, &length)) || !length)
						continue;

					std::vector<uint8_t> storage(length);
					auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());

					if (FAILED(m_infoQueue->GetMessage(i, message, &length)))
						continue;

					if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
						logger::warn("[D3D12/debug] {}",
							std::string_view(message->pDescription, message->DescriptionByteLength));
					}
				}

				m_infoQueue->ClearStoredMessages();
			}

			void CopyToBackBufferAndPresent(UINT syncInterval, UINT flags)
			{
				DrainDebugMessages();

				// Hand the target back to D3D12 and make sure every translated draw that wrote
				// it has been submitted before the copy is recorded behind them.
				ID3D11Resource* wrapped[] = { m_wrapped.Get() };
				m_on12->ReleaseWrappedResources(wrapped, 1);
				m_context11->Flush();

				const UINT index = m_real->GetCurrentBackBufferIndex();

				ComPtr<ID3D12Resource> backBuffer;
				if (FAILED(m_real->GetBuffer(index, IID_PPV_ARGS(&backBuffer)))) {
					m_on12->AcquireWrappedResources(wrapped, 1);
					return;
				}

				m_allocator->Reset();
				m_list->Reset(m_allocator.Get(), nullptr);

				D3D12_RESOURCE_BARRIER toCopy = {};
				toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				toCopy.Transition.pResource = backBuffer.Get();
				toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
				m_list->ResourceBarrier(1, &toCopy);

				m_list->CopyResource(backBuffer.Get(), m_gameTarget.Get());

				std::swap(toCopy.Transition.StateBefore, toCopy.Transition.StateAfter);
				m_list->ResourceBarrier(1, &toCopy);

				m_list->Close();

				ID3D12CommandList* lists[] = { m_list.Get() };
				D3D12Loader::GetCommandQueue()->ExecuteCommandLists(1, lists);

				m_real->Present(syncInterval, flags);

				// One frame of overlap: the engine starts writing the target again as soon as
				// this returns, so the previous frame's copy has to have read it by then.
				D3D12Loader::GetCommandQueue()->Signal(m_fence.Get(), ++m_fenceValue);
				if (m_fence->GetCompletedValue() < m_fenceValue) {
					m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
					WaitForSingleObject(m_fenceEvent, INFINITE);
				}

				m_on12->AcquireWrappedResources(wrapped, 1);
			}

			std::atomic<ULONG> m_refs{ 1 };

			ComPtr<IDXGISwapChain3> m_real;
			ComPtr<ID3D11On12Device> m_on12;
			ComPtr<ID3D11Device> m_device11;
			ComPtr<ID3D11DeviceContext> m_context11;

			ComPtr<ID3D12Resource> m_gameTarget;
			ComPtr<ID3D11Texture2D> m_wrapped;

			ComPtr<ID3D12CommandAllocator> m_allocator;
			ComPtr<ID3D12GraphicsCommandList> m_list;
			ComPtr<ID3D12InfoQueue> m_infoQueue;
			ComPtr<ID3D12Fence> m_fence;
			HANDLE m_fenceEvent = nullptr;
			UINT64 m_fenceValue = 0;

			DXGI_SWAP_CHAIN_DESC m_desc = {};
		};
	}

	bool Requested()
	{
		static const bool s_requested = [] {
			char buf[8] = {};
			return GetEnvironmentVariableA("CS_D3D12", buf, sizeof(buf)) && buf[0] == '1';
		}();
		return s_requested;
	}

	bool Load()
	{
		if (g_attempted)
			return g_loaded;
		g_attempted = true;

		if (!Requested())
			return false;

		UINT factoryFlags = 0;

		// A debug D3D12 device costs far too much to leave on, but the validation it gives
		// is the only practical way to see translation-layer misuse, so make it opt-in.
		char dbg[8] = {};
		if (GetEnvironmentVariableA("CS_D3D12_DEBUG", dbg, sizeof(dbg)) && dbg[0] == '1') {
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
				debugController->EnableDebugLayer();
				factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
				logger::info("[D3D12] Debug layer enabled");
			}
		}

		HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&g_factory));
		if (FAILED(hr)) {
			logger::error("[D3D12] CreateDXGIFactory2 failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
			return false;
		}

		// Take the adapter DXGI prefers for performance rather than enumeration order, so a
		// hybrid machine does not quietly land on the integrated GPU.
		ComPtr<IDXGIAdapter1> adapter;
		ComPtr<IDXGIFactory6> factory6;

		if (SUCCEEDED(g_factory.As(&factory6))) {
			factory6->EnumAdapterByGpuPreference(0,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
		}
		if (!adapter)
			g_factory->EnumAdapters1(0, &adapter);

		hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device));
		if (FAILED(hr)) {
			logger::error("[D3D12] D3D12CreateDevice failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

		hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_queue));
		if (FAILED(hr)) {
			logger::error("[D3D12] CreateCommandQueue failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
			g_device.Reset();
			return false;
		}

		g_device->SetName(L"CommunityShaders D3D12 device");
		g_queue->SetName(L"CommunityShaders direct queue");

		DXGI_ADAPTER_DESC1 desc = {};
		if (adapter && SUCCEEDED(adapter->GetDesc1(&desc)))
			logger::info("[D3D12] Device created on '{}'", winrt::to_string(desc.Description));

		g_loaded = true;
		return true;
	}

	bool IsLoaded() { return g_loaded; }

	ID3D12Device* GetDevice() { return g_device.Get(); }
	ID3D12CommandQueue* GetCommandQueue() { return g_queue.Get(); }
	ID3D11On12Device* GetD3D11On12Device() { return g_on12.Get(); }

	HRESULT WINAPI CreateFactory(REFIID riid, void** ppFactory)
	{
		return ::CreateDXGIFactory1(riid, ppFactory);
	}

	HRESULT WINAPI CreateDeviceAndSwapChain(
		IDXGIAdapter* /*pAdapter*/,
		D3D_DRIVER_TYPE /*DriverType*/,
		HMODULE /*Software*/,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT /*SDKVersion*/,
		const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext)
	{
		if (!g_loaded && !Load())
			return E_FAIL;

		const D3D_FEATURE_LEVEL requested = PickFeatureLevel(pFeatureLevels, FeatureLevels);

		// D3D11On12 wants the queues it is allowed to submit through. One direct queue is
		// enough: the translation layer serialises the engine's work onto it, and the swap
		// chain below presents from the same queue, so present ordering needs no extra
		// synchronisation on our side.
		IUnknown* queues[] = { g_queue.Get() };

		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;

		// BGRA support is not optional here: the engine creates BGRA swap-chain formats and
		// D3D11On12 refuses those views without it.
		const UINT flags = Flags | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		HRESULT hr = D3D11On12CreateDevice(
			g_device.Get(), flags,
			pFeatureLevels, FeatureLevels,
			queues, 1u, 0u,
			&device, &context, nullptr);

		if (FAILED(hr)) {
			logger::error("[D3D12] D3D11On12CreateDevice failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
			return hr;
		}

		hr = device.As(&g_on12);
		if (FAILED(hr)) {
			logger::error("[D3D12] ID3D11On12Device unavailable (hr 0x{:08X})", static_cast<uint32_t>(hr));
			return hr;
		}

		if (pSwapChainDesc) {
			if (!ppSwapChain)
				return E_INVALIDARG;

			// Created on the command queue, not the device: this is what makes presentation
			// itself D3D12 rather than a D3D11 swap chain sitting over translated work.
			DXGI_SWAP_CHAIN_DESC1 desc = {};
			desc.Width = pSwapChainDesc->BufferDesc.Width;
			desc.Height = pSwapChainDesc->BufferDesc.Height;
			desc.Format = pSwapChainDesc->BufferDesc.Format;
			desc.SampleDesc = pSwapChainDesc->SampleDesc;
			desc.BufferUsage = pSwapChainDesc->BufferUsage;
			desc.Scaling = DXGI_SCALING_STRETCH;
			desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

			// Flip model is mandatory for a D3D12 queue swap chain, and it needs at least two
			// buffers. The engine asks for one with the old blit model.
			desc.BufferCount = std::max<UINT>(2u, pSwapChainDesc->BufferCount);
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc = {};
			fsDesc.RefreshRate = pSwapChainDesc->BufferDesc.RefreshRate;
			fsDesc.ScanlineOrdering = pSwapChainDesc->BufferDesc.ScanlineOrdering;
			fsDesc.Scaling = pSwapChainDesc->BufferDesc.Scaling;
			fsDesc.Windowed = pSwapChainDesc->Windowed;

			ComPtr<IDXGISwapChain1> swapChain1;
			hr = g_factory->CreateSwapChainForHwnd(
				g_queue.Get(), pSwapChainDesc->OutputWindow,
				&desc, &fsDesc, nullptr, &swapChain1);

			if (FAILED(hr)) {
				logger::error("[D3D12] CreateSwapChainForHwnd failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
				return hr;
			}

			ComPtr<IDXGISwapChain3> swapChain3;
			hr = swapChain1.As(&swapChain3);
			if (FAILED(hr)) {
				logger::error("[D3D12] IDXGISwapChain3 unavailable (hr 0x{:08X})", static_cast<uint32_t>(hr));
				return hr;
			}

			auto* proxy = new BlitModelSwapChain(swapChain3, g_on12, device, *pSwapChainDesc);
			hr = proxy->Initialise();
			if (FAILED(hr)) {
				logger::error("[D3D12] Back-buffer bridge failed (hr 0x{:08X})", static_cast<uint32_t>(hr));
				proxy->Release();
				return hr;
			}

			*ppSwapChain = proxy;

			logger::info("[D3D12] Swap chain created on the D3D12 queue ({}x{}, {} buffers), "
						 "engine sees a stable blit-model back buffer",
				desc.Width, desc.Height, desc.BufferCount);
		}

		if (pFeatureLevel)
			*pFeatureLevel = device->GetFeatureLevel();

		if (ppDevice)
			device.CopyTo(ppDevice);
		if (ppImmediateContext)
			context.CopyTo(ppImmediateContext);

		logger::info("[D3D12] D3D11On12 device ready (requested FL {:X}, got {:X})",
			static_cast<uint32_t>(requested), static_cast<uint32_t>(device->GetFeatureLevel()));
		return S_OK;
	}
}

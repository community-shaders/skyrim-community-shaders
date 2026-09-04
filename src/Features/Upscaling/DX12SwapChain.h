#pragma once

#include <atomic>

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <directx/d3dx12.h>

class WrappedResource
{
public:
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device);
	~WrappedResource();

	ID3D11Texture2D* resource11 = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	winrt::com_ptr<ID3D12Resource> resource;
};

struct DXGISwapChainProxy : IDXGISwapChain4
{
public:
	explicit DXGISwapChainProxy(IDXGISwapChain4* a_swapChain);

	winrt::com_ptr<IDXGISwapChain4> swapChain;

	/****IUnknown****/
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
	virtual ULONG STDMETHODCALLTYPE AddRef() override;
	virtual ULONG STDMETHODCALLTYPE Release() override;

	/****IDXGIObject****/
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown) override;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent) override;

	/****IDXGIDeviceSubObject****/
	virtual HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice) override;

	/****IDXGISwapChain****/
	virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
	virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface) override;
	virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget) override;
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget) override;
	virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters) override;
	virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput) override;
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats) override;
	virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount) override;

	/****IDXGISwapChain1****/
	virtual HRESULT STDMETHODCALLTYPE GetDesc1(_Out_ DXGI_SWAP_CHAIN_DESC1* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc(_Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetHwnd(_Out_ HWND* pHwnd) override;
	virtual HRESULT STDMETHODCALLTYPE GetCoreWindow(_In_ REFIID refiid, _COM_Outptr_ void** ppUnk) override;
	virtual HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
	virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
	virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput(_Out_ IDXGIOutput** ppRestrictToOutput) override;
	virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(_In_ const DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(_Out_ DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE SetRotation(_In_ DXGI_MODE_ROTATION Rotation) override;
	virtual HRESULT STDMETHODCALLTYPE GetRotation(_Out_ DXGI_MODE_ROTATION* pRotation) override;

	/****IDXGISwapChain2****/
	virtual HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
	virtual HRESULT STDMETHODCALLTYPE GetSourceSize(_Out_ UINT* pWidth, _Out_ UINT* pHeight) override;
	virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
	virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(_Out_ UINT* pMaxLatency) override;
	virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
	virtual HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
	virtual HRESULT STDMETHODCALLTYPE GetMatrixTransform(_Out_ DXGI_MATRIX_3X2_F* pMatrix) override;

	/****IDXGISwapChain3****/
	virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
	virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(_In_ DXGI_COLOR_SPACE_TYPE ColorSpace, _Out_ UINT* pColorSpaceSupport) override;
	virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(_In_ DXGI_COLOR_SPACE_TYPE ColorSpace) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(
		_In_ UINT BufferCount,
		_In_ UINT Width,
		_In_ UINT Height,
		_In_ DXGI_FORMAT Format,
		_In_ UINT SwapChainFlags,
		_In_reads_(BufferCount) const UINT* pCreationNodeMask,
		_In_reads_(BufferCount) IUnknown* const* ppPresentQueue) override;

	/****IDXGISwapChain4****/
	virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(
		_In_ DXGI_HDR_METADATA_TYPE Type,
		_In_ UINT Size,
		_In_reads_opt_(Size) void* pMetaData) override;

private:
	std::atomic<ULONG> referenceCount{ 1 };
};

class DX12SwapChain
{
public:
	enum class SwapChainBackend
	{
		Native,
		FidelityFX,
		XeSS
	};

	SwapChainBackend swapChainBackend = SwapChainBackend::Native;
	winrt::com_ptr<ID3D12Device> d3d12Device;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[2];
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[2];

	IDXGISwapChain4* swapChain;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;

	WrappedResource* swapChainBufferWrapped;
	WrappedResource* uiBufferWrapped;

	/**
	 * Holds the HUD-less back buffer with the UI blended back in, and is the Present source
	 * whenever the backend composites UI onto generated frames only (XeSS-FG). Null otherwise.
	 */
	WrappedResource* presentBufferWrapped = nullptr;
	/**
	 * Set by whoever already wrote this frame's composited image into presentBufferWrapped
	 * (HDROutputCS in SDR) so Present skips its own composite pass. Cleared after each Present.
	 */
	bool presentBufferValid = false;

	// D3D12 interop resources for frame generation
	WrappedResource* depthBufferShared12 = nullptr;
	WrappedResource* motionVectorBufferShared12 = nullptr;
	WrappedResource* xessColorBufferShared12 = nullptr;
	WrappedResource* xessResponsiveMaskShared12 = nullptr;
	WrappedResource* xessOutputBufferShared12 = nullptr;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<ID3D12Resource> swapChainBuffers[2];

	UINT frameIndex = 0;
	UINT64 fenceValue = 0;

	UINT64 frameFenceValues[2] = { 0, 0 };
	winrt::com_ptr<ID3D12CommandAllocator> xessCommandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> xessCommandList;
	UINT64 xessFenceValue = 0;

	LARGE_INTEGER qpf;

	double refreshRate = 0;

	winrt::com_ptr<DXGISwapChainProxy> swapChainProxy;

	// Returns the current frame time (in seconds) for accurate FPS calculation when frame generation is active
	float GetFrameTime() const;

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
	bool CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc, bool enableFrameGenerationProvider);

	void CreateInterop();
	void RecreateWrappedResources(const DXGI_SWAP_CHAIN_DESC1& desc);

	DXGISwapChainProxy* GetSwapChainProxy();
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);

	HRESULT GetBuffer(UINT buffer, REFIID riid, void** ppSurface);
	HRESULT ResizeBuffers(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
	HRESULT ResizeBuffers1(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* creationNodeMask, IUnknown* const* presentQueue);
	HRESULT Present(UINT SyncInterval, UINT Flags);
	HRESULT Present1(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters);
	HRESULT GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice);
	HANDLE GetFrameLatencyWaitableObject();

	void SetColorSpace(bool enableHDR);

	// Resources needed by BackgroundBlur when D3D12 swap chain is active
	struct BlurResources
	{
		ID3D11Texture2D* backbufferTex = nullptr;
		ID3D11RenderTargetView* backbufferRTV = nullptr;
		ID3D11ShaderResourceView* backbufferSRV = nullptr;
		ID3D11ShaderResourceView* uiBufferSRV = nullptr;
		ID3D11RenderTargetView* uiBufferRTV = nullptr;
	};

	// Get all resources needed for background blur in one call
	BlurResources GetBlurResources() const;

	// D3D12 interop resource management
	void CreateSharedResources();
	bool ExecuteXeSS(
		ID3D11Resource* color,
		ID3D11Resource* depth,
		ID3D11Resource* motionVectors,
		ID3D11Resource* responsiveMask,
		ID3D11Resource* output,
		uint32_t inputWidth,
		uint32_t inputHeight,
		float jitterOffsetX,
		float jitterOffsetY,
		bool resetHistory);
	bool WaitForIdle();
	[[nodiscard]] bool UsesFrameGenerationProvider() const { return swapChainBackend != SwapChainBackend::Native; }

private:
	HRESULT ResizeBuffersInternal(
		UINT bufferCount,
		UINT width,
		UINT height,
		DXGI_FORMAT format,
		UINT flags,
		const UINT* creationNodeMask,
		IUnknown* const* presentQueue,
		bool useResizeBuffers1);
	HRESULT PresentInternal(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters);
};

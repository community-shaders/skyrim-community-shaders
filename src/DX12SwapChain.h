#pragma once

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <d3d12.h>

#include <FidelityFX/host/backends/dx12/d3dx12.h>

class WrappedResource
{
public:
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device);

	ID3D11Texture2D* resource11;
	ID3D11ShaderResourceView* srv;
	ID3D11UnorderedAccessView* uav;
	winrt::com_ptr<ID3D12Resource> resource;
};

struct RenderTargetDataD3D12
{
public:
	static RenderTargetDataD3D12 ConvertD3D11TextureToD3D12(RE::BSGraphics::RenderTargetData* rtData, ID3D12Device* a_d3d12Device);

	winrt::com_ptr<ID3D12Resource> d3d12Resource;
};

struct DXGISwapChainProxy : IDXGISwapChain
{
public:
	DXGISwapChainProxy(IDXGISwapChain4* a_swapChain);

	IDXGISwapChain4* swapChain;

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
	virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags);
	virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface);
	virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget);
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget);
	virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc);
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters);
	virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput);
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats);
	virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount);
};

class DX12SwapChain
{
public:
	static DX12SwapChain* GetSingleton()
	{
		static DX12SwapChain singleton;
		return &singleton;
	}

	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer = nullptr;
	FrameBuffer frameBufferCached{};

	void CacheFramebuffer();

	void UpdateFrameBuffer();

	winrt::com_ptr<IDXGIFactory4> factory;

	winrt::com_ptr<ID3D12Device> d3d12Device;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> commandList;

	winrt::com_ptr<IDXGISwapChain4> swapChain;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
	WrappedResource* swapChainBufferWrapped = nullptr;
	WrappedResource* swapChainBufferWrappedDummy = nullptr;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<ID3D11Fence> d3d11Fence2;
	winrt::com_ptr<ID3D12Fence> d3d12Fence2;

	UINT64 currentSharedFenceValue = 0;

	DXGISwapChainProxy* swapChainProxy = nullptr;

	winrt::com_ptr<ID3D12Fence> d3d12OnlyFence;
	UINT64 d3d12FenceValue = 0;
	HANDLE fenceEvent = nullptr;

	RenderTargetDataD3D12 renderTargetsD3D12[RE::RENDER_TARGET::kTOTAL];

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
	void CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);

	void CreateInterop();

	DXGISwapChainProxy* GetSwapChainProxy();
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);

	HRESULT GetBuffer(void** ppSurface);

	bool needsReset = true;

	void BeginFrame();

	HRESULT Present(UINT SyncInterval, UINT Flags);

	void OpenSharedHandles();

	struct Main_Update_Start
	{
		static void thunk(INT64 a_unk)
		{
			GetSingleton()->BeginFrame();
			func(a_unk);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void SetMiscFlags(RE::RENDER_TARGET a_targetIndex, D3D11_TEXTURE2D_DESC* a_pDesc)
	{
		a_pDesc->MiscFlags = 0;  // Original code we wrote over

		if (a_targetIndex == RE::RENDER_TARGET::kMOTION_VECTOR ||
			a_targetIndex == RE::RENDER_TARGET::kNORMAL_TAAMASK_SSRMASK ||
			a_targetIndex == RE::RENDER_TARGET::kNORMAL_TAAMASK_SSRMASK_SWAP ||
			a_targetIndex == RE::RENDER_TARGET::kMAIN) {
			a_pDesc->MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		}
	}

	static void PatchCreateRenderTarget()
	{
		static REL::Relocation<uintptr_t> func{ REL::VariantID(75467, 77253, 0xDBC440) };  // D6A870, DA6200, DBC440

		uint8_t patchNop7[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

		auto& trampoline = SKSE::GetTrampoline();

		struct Patch : Xbyak::CodeGenerator
		{
			explicit Patch(uintptr_t a_funcAddr)
			{
				Xbyak::Label originalLabel;

				// original code we wrote over
				if (REL::Module::IsAE()) {
					mov(ptr[rbp - 0xC], esi);
				} else {
					mov(ptr[rbp - 0x10], eax);
				}

				// push all volatile to be safe
				push(rax);
				push(rcx);
				push(rdx);
				push(r8);
				push(r9);
				push(r10);
				push(r11);

				// scratch space
				sub(rsp, 0x20);

				// call our function
				if (REL::Module::IsAE()) {
					mov(rcx, edx);  // target index
				} else {
					mov(rcx, r12d);  // target index
				}
				lea(rdx, ptr[rbp - 0x30]);  // D3D11_TEXTURE2D_DESC*
				mov(rax, a_funcAddr);
				call(rax);

				add(rsp, 0x20);

				pop(r11);
				pop(r10);
				pop(r9);
				pop(r8);
				pop(rdx);
				pop(rcx);
				pop(rax);

				jmp(ptr[rip + originalLabel]);

				L(originalLabel);
				if (REL::Module::IsAE()) {
					dq(func.address() + 0x8B);
				} else if (REL::Module::IsVR()) {
					dq(func.address() + 0x9E);
				} else {
					dq(func.address() + 0x9F);
				}
			}
		};

		Patch patch(reinterpret_cast<uintptr_t>(SetMiscFlags));
		patch.ready();
		SKSE::AllocTrampoline(8 + patch.getSize());
		if (REL::Module::IsAE()) {  // AE code is 6 bytes anyway
			REL::safe_write<uint8_t>(func.address() + REL::VariantOffset(0x98, 0x85, 0x97).offset(), patchNop7);
		}
		trampoline.write_branch<6>(func.address() + REL::VariantOffset(0x98, 0x85, 0x97).offset(), trampoline.allocate(patch));
	}

	struct ID3D11DeviceContext_Map
	{
		static HRESULT thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
		{
			HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);

			static REL::Relocation<ID3D11Buffer**> perFrame{ REL::RelocationID(524768, 411384) };

			if (*perFrame.get() == pResource)
				GetSingleton()->mappedFrameBuffer = pMappedResource;

			return hr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
		{
			static REL::Relocation<ID3D11Buffer**> perFrame{ REL::RelocationID(524768, 411384) };

			if (*perFrame.get() == pResource && GetSingleton()->mappedFrameBuffer)
				GetSingleton()->CacheFramebuffer();

			func(This, pResource, Subresource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderDepth
	{
		static void thunk(bool a1, bool a2);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		PatchCreateRenderTarget();
		stl::write_thunk_call<Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395, 0x2EE));

		//stl::write_thunk_call<Main_Update_Start>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x1E, 0x3E, 0x33));
	}
};

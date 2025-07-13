#include "Hooks.h"

#include "ShaderTools/BSShaderHooks.h"

#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"

#include "Features/InteriorSunShadows.h"
#include "Features/LightLimitFix.h"
#include "Features/TerrainHelper.h"
#include "Features/VolumetricLighting.h"

#include "ShaderTools/BSShaderHooks.h"

#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include "Upscaling.h"

std::unordered_map<void*, std::pair<std::unique_ptr<uint8_t[]>, size_t>> ShaderBytecodeMap;

void RegisterShaderBytecode(void* Shader, const void* Bytecode, size_t BytecodeLength)
{
	// Grab a copy since the pointer isn't going to be valid forever
	auto codeCopy = std::make_unique<uint8_t[]>(BytecodeLength);
	memcpy(codeCopy.get(), Bytecode, BytecodeLength);
	logger::debug(fmt::runtime("Saving shader at index {:x} with {} bytes:\t{:x}"), (std::uintptr_t)Shader, BytecodeLength, (std::uintptr_t)Bytecode);
	ShaderBytecodeMap.emplace(Shader, std::make_pair(std::move(codeCopy), BytecodeLength));
}

const std::pair<std::unique_ptr<uint8_t[]>, size_t>& GetShaderBytecode(void* Shader)
{
	logger::debug(fmt::runtime("Loading shader at index {:x}"), (std::uintptr_t)Shader);
	return ShaderBytecodeMap.at(Shader);
}

template <class ShaderType>
void DumpShader(const REX::BSShader* thisClass, const ShaderType* shader, const std::pair<std::unique_ptr<uint8_t[]>, size_t>& bytecode)
{
	static_assert(std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> || std::is_same_v<ShaderType, RE::BSGraphics::PixelShader>);

	uint8_t* dxbcData = new uint8_t[bytecode.second];
	size_t dxbcLen = bytecode.second;
	memcpy(dxbcData, bytecode.first.get(), bytecode.second);

	constexpr auto shaderExtStr = std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> ? "vs" : "ps";
	constexpr auto shaderTypeStr = std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> ? "vertex" : "pixel";

	std::string dumpDir = std::format("Data\\ShaderDump\\{}\\{:X}.{}.bin", thisClass->m_LoaderType, shader->id, shaderExtStr);
	auto directoryPath = std::format("Data\\ShaderDump\\{}", thisClass->m_LoaderType);
	logger::debug(fmt::runtime("Dumping {} shader {} with id {:x} at {}"), shaderTypeStr, thisClass->m_LoaderType, shader->id, dumpDir);

	if (!std::filesystem::is_directory(directoryPath)) {
		try {
			std::filesystem::create_directories(directoryPath);
		} catch (std::filesystem::filesystem_error const& ex) {
			logger::error("Failed to create folder: {}", ex.what());
		}
	}

	if (FILE* file; fopen_s(&file, dumpDir.c_str(), "wb") == 0) {
		fwrite(dxbcData, 1, dxbcLen, file);
		fclose(file);
	}

	delete[] dxbcData;
}

struct BSShader_LoadShaders
{
	static void thunk(RE::BSShader* shader, std::uintptr_t stream)
	{
		func(shader, stream);

		auto state = globals::state;
		auto shaderCache = globals::shaderCache;
		auto truePBR = globals::truePBR;

		if (shaderCache->IsDiskCache() || shaderCache->IsDump()) {
			if (shaderCache->IsDiskCache()) {
				truePBR->GenerateShaderPermutations(shader);
			}

			for (const auto& entry : shader->vertexShaders) {
				if (entry->shader && shaderCache->IsDump()) {
					const auto& bytecode = GetShaderBytecode(entry->shader);
					DumpShader((REX::BSShader*)shader, entry, bytecode);
				}
				auto vertexShaderDesriptor = entry->id;
				auto pixelShaderDescriptor = entry->id;
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				shaderCache->GetVertexShader(*shader, vertexShaderDesriptor);
			}
			for (const auto& entry : shader->pixelShaders) {
				if (entry->shader && shaderCache->IsDump()) {
					const auto& bytecode = GetShaderBytecode(entry->shader);
					DumpShader((REX::BSShader*)shader, entry, bytecode);
				}
				auto vertexShaderDesriptor = entry->id;
				auto pixelShaderDescriptor = entry->id;
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor, true);
				shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
			}
		}
		BSShaderHooks::hk_LoadShaders((REX::BSShader*)shader, stream);
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

bool Hooks::BSShader_BeginTechnique::thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader)
{
	auto state = globals::state;
	auto shaderCache = globals::shaderCache;

	state->updateShader = true;
	state->currentShader = shader;

	state->currentVertexDescriptor = vertexDescriptor;
	state->currentPixelDescriptor = pixelDescriptor;

	state->modifiedVertexDescriptor = vertexDescriptor;
	state->modifiedPixelDescriptor = pixelDescriptor;

	state->ModifyShaderLookup(*shader, state->modifiedVertexDescriptor, state->modifiedPixelDescriptor);

	bool shaderFound = func(shader, vertexDescriptor, pixelDescriptor, skipPixelShader);

	if (!shaderFound && shader->shaderType.get() != RE::BSShader::Type::Effect) {
		RE::BSGraphics::VertexShader* vertexShader = shaderCache->GetVertexShader(*shader, state->modifiedVertexDescriptor);
		RE::BSGraphics::PixelShader* pixelShader = shaderCache->GetPixelShader(*shader, state->modifiedPixelDescriptor);
		if (vertexShader == nullptr || (!skipPixelShader && pixelShader == nullptr)) {
			shaderFound = false;
		} else {
			state->settingCustomShader = true;
			globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader), NULL, NULL);
			*globals::game::currentVertexShader = vertexShader;
			globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
			if (skipPixelShader) {
				pixelShader = nullptr;
			}
			*globals::game::currentPixelShader = pixelShader;
			if (pixelShader)
				globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader), NULL, NULL);
			state->settingCustomShader = false;
			shaderFound = true;
		}
	}

	state->lastModifiedVertexDescriptor = state->modifiedVertexDescriptor;
	state->lastModifiedPixelDescriptor = state->modifiedPixelDescriptor;

	return shaderFound;
}

namespace EffectExtensions
{
	struct BSEffectShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			func(shader, pass, renderFlags);
			if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(pass->geometry->GetGeometryRuntimeData().properties[1].get())) {
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale)) {
					auto state = globals::state;
					state->currentExtraDescriptor |= (uint)State::ExtraShaderDescriptors::EffectShadows;
				}
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace LightingExtensions
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			func(shader, pass, renderFlags);

			globals::state->isTree = false;

			if (auto userData = pass->geometry->GetUserData())
				if (auto baseObject = userData->GetBaseObject())
					if (baseObject->As<RE::TESObjectTREE>())
						globals::state->isTree = true;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

struct IDXGISwapChain_Present
{
	static HRESULT WINAPI /**
	 * @brief Presents the swap chain with additional upscaling, overlay, and Reflex marker integration.
	 *
	 * Copies frame buffers for upscaling, processes overlays, manages tearing flags, and integrates Streamline Reflex markers for frame timing if enabled. Also collects profiling data and applies frame limiting after presentation.
	 *
	 * @param This The swap chain instance to present.
	 * @param SyncInterval The vertical sync interval.
	 * @param Flags Presentation flags, possibly modified for tearing support.
	 * @return HRESULT Result of the present operation.
	 */
	thunk(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
	{
		auto state = globals::state;
		auto streamline = globals::streamline;
		auto upscaling = globals::upscaling;
		auto menu = globals::menu;

		upscaling->CopyBuffersToSharedResources();
		state->PresentReShade();
		streamline->Present();
		state->Reset();
		menu->DrawOverlay();

		if (upscaling->d3d12Interop)
			SyncInterval = 0;

		if (!globals::game::isVR) {
			BOOL fullscreen = FALSE;
			((IDXGISwapChain*)This)->GetFullscreenState(&fullscreen, nullptr);
			if (fullscreen || SyncInterval) {
				Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
			} else if (SyncInterval == 0) {
				Flags |= DXGI_PRESENT_ALLOW_TEARING;
			}
		}

		HRESULT retval = S_OK;

		if (globals::streamline->featureReflex) {
			sl::FrameToken* frameToken;
			globals::streamline->slGetNewFrameToken(frameToken, &globals::state->frameCount);

			globals::streamline->slReflexSetMarker(sl::ReflexMarker::eRenderSubmitEnd, *frameToken);
			globals::streamline->slReflexSetMarker(sl::ReflexMarker::ePresentStart, *frameToken);

			retval = func(This, SyncInterval, Flags);

			globals::streamline->slReflexSetMarker(sl::ReflexMarker::ePresentEnd, *frameToken);
		} else {
			retval = func(This, SyncInterval, Flags);
		}

		TracyD3D11Collect(state->tracyCtx);

		upscaling->FrameLimiter();

		return retval;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Hooks::BSGraphics_SetDirtyStates::thunk(bool isCompute)
{
	func(isCompute);
	globals::state->Draw();
}

struct ID3D11Device_CreateVertexShader
{
	static HRESULT thunk(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader)
	{
		HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);

		if (SUCCEEDED(hr))
			RegisterShaderBytecode(*ppVertexShader, pShaderBytecode, BytecodeLength);

		return hr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct ID3D11Device_CreatePixelShader
{
	static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader)
	{
		HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);

		if (SUCCEEDED(hr))
			RegisterShaderBytecode(*ppPixelShader, pShaderBytecode, BytecodeLength);

		return hr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct ID3D11Device_CreateSamplerState
{
	static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* This, D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState)
	{
		// Limit Anisotropy to 8x for performance
		D3D11_SAMPLER_DESC descCopy = *pSamplerDesc;  // make a copy, pSamplerDesc is supposed to be immutable
		descCopy.MaxAnisotropy = std::min(descCopy.MaxAnisotropy, 8u);
		return func(This, &descCopy, ppSamplerState);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

decltype(&CreateDXGIFactory) ptrCreateDXGIFactory;

HRESULT WINAPI hk_CreateDXGIFactory(REFIID, void** ppFactory)
{
	auto streamline = globals::streamline;
	if (!streamline->triedInitialization)
		globals::streamline->LoadInterposer();
	if (streamline->initialized)
		return streamline->slCreateDXGIFactory1(__uuidof(IDXGIFactory4), ppFactory);
	return ptrCreateDXGIFactory(__uuidof(IDXGIFactory4), ppFactory);
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	if (!REL::Module::IsVR()) {
		pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		IDXGIFactory5* dxgiFactory;
		DX::ThrowIfFailed(pAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

		BOOL allowTearing = FALSE;
		DX::ThrowIfFailed(dxgiFactory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&allowTearing,
			sizeof(allowTearing)));

		if (allowTearing) {
			pSwapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		} else {
			pSwapChainDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}
	}

	auto streamline = globals::streamline;
	auto fidelityFX = globals::fidelityFX;
	auto upscaling = globals::upscaling;

	if (streamline->initialized)
		streamline->CheckFeatures(pAdapter);
	else
		upscaling->streamlineMissing = true;

	auto proxy = globals::dx12SwapChain;

	bool shouldProxy = !REL::Module::IsVR();
	if (shouldProxy)
		if (!pSwapChainDesc->Windowed)
			shouldProxy = false;

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling->refreshRate = refreshRate;

	if (shouldProxy) {
		if (upscaling->settings.frameGenerationMode)
			if (refreshRate >= 120)
				shouldProxy = true;
			else if (upscaling->settings.frameGenerationForceEnable)
				shouldProxy = true;
			else
				shouldProxy = false;
		else
			shouldProxy = false;
	}

	upscaling->lowRefreshRate = refreshRate < 119;
	upscaling->isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		if (streamline->featureDLSSG) {
			logger::info("[Frame Generation] Using D3D12 proxy via Streamline");

			auto ret = streamline->slD3D11CreateDeviceAndSwapChain(pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				pSwapChainDesc,
				ppSwapChain,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext);

			upscaling->d3d12Interop = true;

			streamline->PostDevice();
			upscaling->InstallD3DHooks(*ppImmediateContext);

			IDXGIFactory* factory = nullptr;
			if (SUCCEEDED((*ppSwapChain)->GetParent(IID_PPV_ARGS(&factory)))) {
				factory->MakeWindowAssociation(pSwapChainDesc->OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES);
				factory->Release();
			}

			return ret;

		} else {
			logger::info("[Frame Generation] Using manual D3D12 proxy");

			if (fidelityFX->module) {
				proxy->CreateD3D12Device(pAdapter);

				D3D11CreateDevice(
					pAdapter,
					DriverType,
					Software,
					Flags,
					&featureLevel,
					1,
					SDKVersion,
					ppDevice,
					pFeatureLevel,
					ppImmediateContext);

				proxy->SetD3D11Device(*ppDevice);
				proxy->SetD3D11DeviceContext(*ppImmediateContext);

				proxy->CreateSwapChain(pAdapter, *pSwapChainDesc);

				proxy->CreateInterop();

				*ppSwapChain = proxy->GetSwapChainProxy();

				upscaling->d3d12Interop = true;

				if (streamline->initialized) {
					streamline->slSetD3DDevice(*ppDevice);
					streamline->PostDevice();
				}

				IDXGIFactory* factory = nullptr;
				if (SUCCEEDED((*ppSwapChain)->GetParent(IID_PPV_ARGS(&factory)))) {
					factory->MakeWindowAssociation(pSwapChainDesc->OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES);
					factory->Release();
				}

				return S_OK;
			} else {
				logger::warn("[Frame Generation] amd_fidelityfx_dx12.dll is not loaded, skipping proxy");
				upscaling->fidelityFXMissing = true;
			}
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChain(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (streamline->initialized) {
		streamline->slSetD3DDevice(*ppDevice);
		streamline->PostDevice();
	}

	return ret;
}

struct Main_Update_Begin
{
	/**
	 * @brief Wraps the player character update with Reflex frame timing markers.
	 *
	 * If the Reflex feature is enabled, obtains a new frame token and sets markers for input sampling and simulation start before invoking the original update function.
	 *
	 * @param a_player Pointer to the player character being updated.
	 */
	static void thunk(RE::PlayerCharacter* a_player)
	{
		if (globals::streamline->featureReflex) {
			sl::FrameToken* frameToken;
			globals::streamline->slGetNewFrameToken(frameToken, &globals::state->frameCount);
			globals::streamline->slReflexSetMarker(sl::ReflexMarker::eInputSample, *frameToken);
			globals::streamline->slReflexSetMarker(sl::ReflexMarker::eSimulationStart, *frameToken);
		}
		func(a_player);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct Main_Update_Swap
{
	/**
	 * @brief Wraps a function call with Streamline Reflex simulation and render submit markers.
	 *
	 * If the Reflex feature is enabled, obtains a new frame token and sets markers for simulation end and render submit start before invoking the original function.
	 */
	static void thunk(void* This)
	{
		if (globals::streamline->featureReflex) {
			sl::FrameToken* frameToken;
			globals::streamline->slGetNewFrameToken(frameToken, &globals::state->frameCount);
			globals::streamline->slReflexSetMarker(sl::ReflexMarker::eSimulationEnd, *frameToken);
			globals::streamline->slReflexSetMarker(sl::ReflexMarker::eRenderSubmitStart, *frameToken);
		}
		func(This);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSShaderRenderTargets_Create
{
	/**
	 * @brief Calls the original render target creation function and reinitializes global rendering state.
	 *
	 * Invokes the original function, then reinitializes global state and performs necessary setup for rendering targets.
	 */
	static void thunk()
	{
		func();
		globals::ReInit();
		globals::state->Setup();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSInputDeviceManager_PollInputDevices
{
	static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
	{
		bool blockedDevice = true;

		auto menu = globals::menu;

		if (a_events) {
			menu->ProcessInputEvents(a_events);

			if (*a_events) {
				if (auto device = (*a_events)->GetDevice()) {
					// Check that the device is not a Gamepad or VR controller. If it is, unblock input.
					bool vrDevice = false;
#ifdef ENABLE_SKYRIM_VR
					vrDevice = (globals::game::isVR && ((device == RE::INPUT_DEVICES::INPUT_DEVICE::kVivePrimary) ||
														   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kViveSecondary) ||
														   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusPrimary) ||
														   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusSecondary) ||
														   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRPrimary) ||
														   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRSecondary)));
#endif
					blockedDevice = !((device == RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad) || vrDevice);
				}
			}
		}

		if (blockedDevice && menu->ShouldSwallowInput()) {  //the menu is open, eat all keypresses
			constexpr RE::InputEvent* const dummy[] = { nullptr };
			func(a_dispatcher, dummy);
			return;
		}

		func(a_dispatcher, a_events);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

namespace Hooks
{
	struct BSGraphics_Renderer_Init_InitD3D
	{
		static void thunk()
		{
			logger::info("Calling original Init3D");

			func();

			logger::info("Accessing render device information");
			globals::ReInit();

			logger::info("Detouring virtual function tables");
			stl::detour_vfunc<8, IDXGISwapChain_Present>(globals::d3d::swapChain);

			auto shaderCache = globals::shaderCache;
			if (shaderCache->IsDump()) {
				stl::detour_vfunc<12, ID3D11Device_CreateVertexShader>(globals::d3d::device);
				stl::detour_vfunc<15, ID3D11Device_CreatePixelShader>(globals::d3d::device);
			}

			stl::detour_vfunc<23, ID3D11Device_CreateSamplerState>(globals::d3d::device);

			globals::menu->Init();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct WndProcHandler_Hook
	{
		static LRESULT thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			auto menu = globals::menu;
			if ((a_msg == WM_KILLFOCUS || a_msg == WM_SETFOCUS) && menu->initialized) {
				menu->focusChanged = true;
			}
			return func(a_hwnd, a_msg, a_wParam, a_lParam);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RegisterClassA_Hook
	{
		static ATOM thunk(WNDCLASSA* a_wndClass)
		{
			WndProcHandler_Hook::func = reinterpret_cast<uintptr_t>(a_wndClass->lpfnWndProc);
			a_wndClass->lpfnWndProc = &WndProcHandler_Hook::thunk;

			return func(a_wndClass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Main
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Normals
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_NormalsSwap
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Snow
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_MotionVectors
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShader__BeginTechnique_SetVertexShader
	{
		static void thunk(RE::BSGraphics::Renderer*, RE::BSGraphics::VertexShader* a_vertexShader)
		{
			auto state = globals::state;
			auto shaderCache = globals::shaderCache;

			if (!state->settingCustomShader) {
				if (shaderCache->IsEnabled()) {
					auto currentShader = state->currentShader;
					auto type = currentShader->shaderType.get();
					if (type > 0 && type < RE::BSShader::Type::Total) {
						if (state->enabledClasses[type - 1]) {
							RE::BSGraphics::VertexShader* vertexShader = shaderCache->GetVertexShader(*currentShader, state->modifiedVertexDescriptor);
							if (vertexShader) {
								globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader), NULL, NULL);
								*globals::game::currentVertexShader = a_vertexShader;
								globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
								return;
							}
						}
					}
				}
			}

			globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);

			*globals::game::currentVertexShader = a_vertexShader;
			globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(a_vertexShader->shader), NULL, NULL);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShader__BeginTechnique_SetPixelShader
	{
		static void thunk(RE::BSGraphics::Renderer*, RE::BSGraphics::PixelShader* a_pixelShader)
		{
			auto state = globals::state;
			auto shaderCache = globals::shaderCache;

			if (!state->settingCustomShader) {
				if (shaderCache->IsEnabled()) {
					auto currentShader = state->currentShader;
					auto type = currentShader->shaderType.get();
					if (type > 0 && type < RE::BSShader::Type::Total) {
						if (state->enabledClasses[type - 1]) {
							RE::BSGraphics::PixelShader* pixelShader = shaderCache->GetPixelShader(*currentShader, state->modifiedPixelDescriptor);
							if (pixelShader) {
								globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader), NULL, NULL);
								*globals::game::currentPixelShader = a_pixelShader;
								return;
							}
						}
					}
				}
			}

			*globals::game::currentPixelShader = a_pixelShader;

			if (a_pixelShader)
				globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(a_pixelShader->shader), NULL, NULL);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil_PrecipitationMask
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
		{
			a_properties->use16BitsDepth = true;
			a_properties->stencil = false;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateCubemapRenderTarget_Reflections
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::CubeMapRenderTargetProperties* a_properties)
		{
			a_properties->height = 128;
			a_properties->width = 128;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil_Reflections
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
		{
			a_properties->height = 128;
			a_properties->width = 128;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Sky Reflection Fix
	struct TESWaterReflections_Update_Actor_GetLOSPosition
	{
		static RE::NiPoint3* thunk(RE::PlayerCharacter* a_player, RE::NiPoint3* a_target, int unk1, float unk2)
		{
			auto ret = func(a_player, a_target, unk1, unk2);

			auto camera = RE::PlayerCamera::GetSingleton();
			ret->x = camera->cameraRoot->world.translate.x;
			ret->y = camera->cameraRoot->world.translate.y;
			ret->z = camera->cameraRoot->world.translate.z;

			return ret;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESObjectLAND_SetupMaterial
	{
		static bool thunk(RE::TESObjectLAND* land)
		{
			bool vanillaResult = func(land);

			// setup material for PBR
			auto TruePBRSingleton = globals::truePBR;
			if (TruePBRSingleton->TESObjectLAND_SetupMaterial(land)) {
				// if PBR, we are done
				return true;
			}

			// setup material for terrain helper
			auto terrainHelper = globals::features::terrainHelper;
			if (vanillaResult && terrainHelper->loaded) {
				terrainHelper->TESObjectLAND_SetupMaterial(land);
			}

			return vanillaResult;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_SetupMaterial
	{
		static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
		{
			// setup material for PBR
			auto TruePBRSingleton = globals::truePBR;
			if (TruePBRSingleton->BSLightingShader_SetupMaterial(shader, material)) {
				// if PBR, we are done
				return;
			}

			// vanilla
			func(shader, material);

			// terrain helper
			auto terrainHelper = globals::features::terrainHelper;
			if (terrainHelper->loaded) {
				terrainHelper->BSLightingShader_SetupMaterial(material);
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void BSBatchRenderer_RenderPassImmediately1::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
	{
		if (globals::features::lightLimitFix->loaded && !globals::features::lightLimitFix->CheckParticleLights(a_pass, a_technique))
			return;

		// Separate deferred and forward blended decals
		if (globals::state->inWorld && a_pass->accumulationHint == 3 && !a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) {
			RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
			globals::state->blendedDecalRenderPasses.push_back(call);
			return;
		}

		func(a_pass, a_technique, a_alphaTest, a_renderFlags);
	}

	struct BSBatchRenderer_RenderPassImmediately2
	{
		static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
		{
			if (globals::features::lightLimitFix->loaded && !globals::features::lightLimitFix->CheckParticleLights(a_pass, a_technique))
				return;

			if (globals::features::interiorSunShadows->loaded)
				globals::features::interiorSunShadows->UpdateRasterStateCullMode(a_pass, a_technique);

			// Separate deferred and forward blended decals
			if (globals::state->inWorld && a_pass->accumulationHint == 3 && !a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) {
				RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
				globals::state->blendedDecalRenderPasses.push_back(call);
				return;
			}

			func(a_pass, a_technique, a_alphaTest, a_renderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately3
	{
		static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
		{
			if (globals::features::lightLimitFix->loaded && !globals::features::lightLimitFix->CheckParticleLights(a_pass, a_technique))
				return;

			// Separate deferred and forward blended decals
			if (globals::state->inWorld && a_pass->accumulationHint == 3 && !a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) {
				RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
				globals::state->blendedDecalRenderPasses.push_back(call);
				return;
			}

			func(a_pass, a_technique, a_alphaTest, a_renderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

#ifdef TRACY_ENABLE
	struct Main_Update
	{
		static void thunk(RE::Main* a_this, float a2)
		{
			func(a_this, a2);
			FrameMark;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
#endif

	namespace CSShadersSupport
	{
		RE::BSImagespaceShader* CurrentlyDispatchedShader = nullptr;
		RE::BSComputeShader* CurrentlyDispatchedComputeShader = nullptr;
		uint32_t CurrentComputeShaderTechniqueId = 0;

		struct BSImagespaceShader_DispatchComputeShader
		{
			static void thunk(RE::BSImagespaceShader* shader, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				CurrentlyDispatchedShader = shader;
				func(shader, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				CurrentlyDispatchedShader = nullptr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSComputeShader_Dispatch
		{
			static void thunk(RE::BSComputeShader* shader, uint32_t techniqueId, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				CurrentlyDispatchedComputeShader = shader;
				CurrentComputeShaderTechniqueId = techniqueId;
				func(shader, techniqueId, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				CurrentlyDispatchedComputeShader = nullptr;
				CurrentComputeShaderTechniqueId = 0;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_DispatchCSShader
		{
			static void thunk(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::ComputeShader* shader, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				auto state = globals::state;
				auto shaderCache = globals::shaderCache;
				auto vl = globals::features::volumetricLighting;

				if (state->enabledClasses[RE::BSShader::Type::ImageSpace]) {
					RE::BSImagespaceShader* isShader = CurrentlyDispatchedShader;
					uint32_t techniqueId = CurrentComputeShaderTechniqueId;
					if (CurrentlyDispatchedShader == nullptr) {
						techniqueId = 0;
						if (CurrentlyDispatchedComputeShader->name == std::string_view("ISVolumetricLightingGenerateCS")) {
							isShader = globals::features::volumetricLighting->GetOrCreateGenerateCS(CurrentlyDispatchedComputeShader);
						} else if (CurrentlyDispatchedComputeShader->name == std::string_view("ISVolumetricLightingRaymarchCS")) {
							isShader = globals::features::volumetricLighting->GetOrCreateRaymarchCS(CurrentlyDispatchedComputeShader);
						}
					} else if (CurrentlyDispatchedComputeShader->name == std::string_view("ISVolumetricLightingBlurHCS")) {
						techniqueId = 0;
						isShader = vl->GetOrCreateBlurHCS(CurrentlyDispatchedComputeShader);
						vl->SetDimensionsCB();
						vl->SetGroupCountsHCS(threadGroupCountX);
					} else if (CurrentlyDispatchedComputeShader->name == std::string_view("ISVolumetricLightingBlurVCS")) {
						techniqueId = 0;
						isShader = vl->GetOrCreateBlurVCS(CurrentlyDispatchedComputeShader);
						vl->SetDimensionsCB();
						vl->SetGroupCountsVCS(threadGroupCountY);
					}
					if (isShader != nullptr) {
						if (auto* computeShader = shaderCache->GetComputeShader(*isShader, techniqueId)) {
							shader = computeShader;
						}
					}
				}
				func(renderer, shader, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void PatchMemory(uintptr_t Address, const uint8_t* Data, size_t Size)
	{
		DWORD d = 0;
		VirtualProtect(reinterpret_cast<LPVOID>(Address), Size, PAGE_EXECUTE_READWRITE, &d);

		for (uintptr_t i = Address; i < (Address + Size); i++) {
			*reinterpret_cast<volatile uint8_t*>(i) = *Data++;
		}

		VirtualProtect(reinterpret_cast<LPVOID>(Address), Size, d, &d);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(Address), Size);
	}

	void PatchMemory(uintptr_t Address, std::initializer_list<uint8_t> Data)
	{
		PatchMemory(Address, Data.begin(), Data.size());
	}

	/**
	 * @brief Installs hooks, detours, and memory patches for graphics, input, and rendering subsystems.
	 *
	 * Sets up function hooks and virtual method overrides for shader management, input polling, rendering pipeline stages, compute shader dispatch, material setup, batch rendering, and window procedure handling. Applies memory patches to adjust render pass cache sizes and offsets. Installs additional update hooks for frame timing and Reflex marker integration when not in VR mode.
	 */
	void Install()
	{
		logger::info("Hooking BSInputDeviceManager::PollInputDevices");
		stl::write_thunk_call<BSInputDeviceManager_PollInputDevices>(REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B, 0x81));

		logger::info("Hooking BSShader::LoadShaders");
		stl::detour_thunk<BSShader_LoadShaders>(REL::RelocationID(101339, 108326));

		logger::info("Hooking BSShader::BeginTechnique");
		stl::detour_thunk<BSShader_BeginTechnique>(REL::RelocationID(101341, 108328));

		stl::write_thunk_call<BSShader__BeginTechnique_SetVertexShader>(REL::RelocationID(101341, 108328).address() + REL::Relocate(0xC3, 0xD5));
		stl::write_thunk_call<BSShader__BeginTechnique_SetPixelShader>(REL::RelocationID(101341, 108328).address() + REL::Relocate(0xD7, 0xEB));

		logger::info("Hooking BSGraphics::SetDirtyStates");
		stl::detour_thunk<BSGraphics_SetDirtyStates>(REL::RelocationID(75580, 77386));

		logger::info("Hooking BSGraphics::Renderer::InitD3D");
		stl::write_thunk_call<BSGraphics_Renderer_Init_InitD3D>(REL::RelocationID(75595, 77226).address() + REL::Relocate(0x50, 0x2BC));

		logger::info("Hooking WndProcHandler");
		stl::write_thunk_call<RegisterClassA_Hook, 6>(REL::VariantID(75591, 77226, 0xDC4B90).address() + REL::VariantOffset(0x8E, 0x15C, 0x99).offset());

		logger::info("Hooking BSShaderRenderTargets::Create");
		stl::detour_thunk<BSShaderRenderTargets_Create>(REL::RelocationID(100458, 107175));

		logger::info("Hooking BSShaderRenderTargets::Create::CreateRenderTarget(s)");
		stl::write_thunk_call<CreateRenderTarget_Main>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x3F0, 0x3F3, 0x548));
		stl::write_thunk_call<CreateRenderTarget_Normals>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x458, 0x45B, 0x5B0));
		stl::write_thunk_call<CreateRenderTarget_NormalsSwap>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x46B, 0x46E, 0x5C3));
		stl::write_thunk_call<CreateRenderTarget_Snow>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x406, 0x409, 0x55e));
		stl::write_thunk_call<CreateRenderTarget_MotionVectors>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x4F0, 0x4EF, 0x64E));
		stl::write_thunk_call<CreateDepthStencil_PrecipitationMask>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x1245, 0x123B, 0x1917));
		stl::write_thunk_call<CreateCubemapRenderTarget_Reflections>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xA25, 0xA25, 0xCD2));
		stl::write_thunk_call<CreateDepthStencil_Reflections>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xA59, 0xA59, 0xD13));

#ifdef TRACY_ENABLE
		stl::write_thunk_call<Main_Update>(REL::RelocationID(35551, 36544).address() + REL::Relocate(0x11F, 0x160));
#endif

		logger::info("Hooking BSImagespaceShader");
		stl::detour_thunk<CSShadersSupport::BSImagespaceShader_DispatchComputeShader>(REL::RelocationID(100952, 107734));

		logger::info("Hooking BSComputeShader");
		stl::write_vfunc<0x02, CSShadersSupport::BSComputeShader_Dispatch>(RE::VTABLE_BSComputeShader[0]);

		logger::info("Hooking Renderer::DispatchCSShader");
		stl::detour_thunk<CSShadersSupport::Renderer_DispatchCSShader>(REL::RelocationID(75532, 77329));

		logger::info("Hooking TESWaterReflections::Update_Actor::GetLOSPosition for Sky Reflection Fix");
		stl::write_thunk_call<TESWaterReflections_Update_Actor_GetLOSPosition>(REL::RelocationID(31373, 32160).address() + REL::Relocate(0x1AD, 0x1CA, 0x1ed));

		logger::info("Hooking BSEffectShader");
		stl::write_vfunc<0x6, EffectExtensions::BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x6, LightingExtensions::BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);

		logger::info("Hooking TESObjectLAND");
		stl::detour_thunk<TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

		logger::info("Hooking BSLightingShader");
		stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);

		logger::info("Hooking BSBatchRenderer::RenderPassImmediately");
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately1>(REL::RelocationID(100877, 107673).address() + REL::Relocate(0x1E5, 0x1EE));
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately2>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately3>(REL::RelocationID(100871, 107667).address() + REL::Relocate(0xEE, 0xED));

		const auto renderPassCacheCtor = REL::VariantID(100720, 107500, 0x1340330);
		const int32_t passCount = 4194240;
		const int32_t passCountSE = 4194240 * 16;

		const int32_t passSize = 4194240 * sizeof(RE::BSRenderPass);
		const int32_t lightsCount = passCount * 16;
		const int32_t lightsSize = lightsCount * sizeof(void*);
		const int32_t lastPassIndex = passCount - 1;
		const int32_t lastPassOffset =
			(passCount - 1) * sizeof(RE::BSRenderPass);
		const int32_t lastPassNextOffset =
			(passCount - 1) * sizeof(RE::BSRenderPass) + offsetof(RE::BSRenderPass, next);
		PatchMemory(
			REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0x76).address(),
			reinterpret_cast<const uint8_t*>(&lightsSize), 4);
		PatchMemory(
			REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0xAD).address(),
			reinterpret_cast<const uint8_t*>(&passSize), 4);
		PatchMemory(
			REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0xCB).address(),
			reinterpret_cast<const uint8_t*>(&lastPassIndex), 4);
		PatchMemory(
			REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0xF0).address(),
			reinterpret_cast<const uint8_t*>(&lastPassNextOffset), 4);
		PatchMemory(
			REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0xFD).address(),
			reinterpret_cast<const uint8_t*>(&lastPassOffset), 4);
		if (REL::Module::IsAE()) {
			PatchMemory(
				REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0x191).address(),
				reinterpret_cast<const uint8_t*>(&passCount), 4);
		} else {
			PatchMemory(
				REL::Relocation<std::uintptr_t>(renderPassCacheCtor, 0x191 - 2).address(),
				reinterpret_cast<const uint8_t*>(&passCountSE), 4);
		}
		if (!REL::Module::IsVR()) {
			stl::write_thunk_call<Main_Update_Begin>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x53, 0x6E));
			stl::write_thunk_call<Main_Update_Swap>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x5D2, 0xA97));
		}

		// Patch eye position in BSLightingShader::SetupGeometry to always update due to additional effects which may require it
		// The variable updateEyePosition is set to false by default. By patching to be true it will always update the eye position
		// SE and AE use a 6-byte instruction whereas VR uses a 7-byte instruction
		// We offset from the base address of the containing function to the instruction itself and then to the value the instruction sets
		{
			uintptr_t setupGeometryUpdateEyePosition = REL::RelocationID(100565, 107300).address() + REL::Relocate(0x50, 0x75, 0x78);
			REL::safe_write(setupGeometryUpdateEyePosition + REL::Relocate(6, 6, 7), bool{ true });
		}
	}

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	 */
	void InstallD3DHooks()
	{
		globals::fidelityFX->LoadFFX();

		*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		*(uintptr_t*)&ptrCreateDXGIFactory = SKSE::PatchIAT(hk_CreateDXGIFactory, "dxgi.dll", !REL::Module::IsVR() ? "CreateDXGIFactory" : "CreateDXGIFactory1");
	}
}
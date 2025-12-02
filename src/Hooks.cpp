#include "Hooks.h"

#include "ShaderTools/BSShaderHooks.h"

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"

#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/TerrainHelper.h"
#include "Features/UnifiedWater.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"

#include "ShaderTools/BSShaderHooks.h"

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

	state->permutationData.VertexShaderDescriptor = vertexDescriptor;
	state->permutationData.PixelShaderDescriptor = pixelDescriptor;

	state->modifiedVertexDescriptor = vertexDescriptor;
	state->modifiedPixelDescriptor = pixelDescriptor;

	state->ModifyShaderLookup(*shader, state->modifiedVertexDescriptor, state->modifiedPixelDescriptor);

	// Only check against non-shader bits
	state->permutationData.PixelShaderDescriptor &= ~state->modifiedPixelDescriptor;

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
			// Check for vanilla water foam and skip if UnifiedWater is active
			auto& unifiedWater = globals::features::unifiedWater;
			if (unifiedWater.loaded && unifiedWater.settings.foam.EnableFoam) {
				if (auto* shaderProperty = static_cast<RE::BSEffectShaderProperty*>(pass->geometry->GetGeometryRuntimeData().properties[1].get())) {
					if (auto* material = shaderProperty->GetMaterial()) {
						const char* texPath = material->sourceTexturePath.c_str();
						if (texPath && texPath[0] != '\0') {
							// Case-insensitive check for water foam textures
							std::string path(texPath);
							for (auto& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							// Check for vanilla foam (fxwhitewater*.dds) and any modded foam textures
							if (path.find("whitewater") != std::string::npos ||
								path.find("waterfoam") != std::string::npos) {
								return;  // Skip rendering this foam decal
							}
						}
					}
				}
			}

			func(shader, pass, renderFlags);

			auto state = globals::state;

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::EffectShadows);

			if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(pass->geometry->GetGeometryRuntimeData().properties[1].get())) {
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale)) {
					state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::EffectShadows);
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

			auto state = globals::state;

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsTree);

			if (auto userData = pass->geometry->GetUserData())
				if (auto baseObject = userData->GetBaseObject())
					if (baseObject->As<RE::TESObjectTREE>())
						state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsTree);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace GrassExtensions
{
	struct BSGrassShaderProperty_ctor
	{
		static RE::BSLightingShaderProperty* thunk(RE::BSLightingShaderProperty* property)
		{
			const uint64_t stackPointer = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
			const uint64_t lightingPropertyAddress = stackPointer + (REL::Module::IsAE() ? 0x68 : 0x70);
			auto* lightingProperty = *reinterpret_cast<RE::BSLightingShaderProperty**>(lightingPropertyAddress);

			RE::BSLightingShaderProperty* grassProperty = func(property);

			if (lightingProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting)) {
				grassProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kEffectLighting, true);
			}

			return grassProperty;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			func(shader, pass, renderFlags);

			auto state = globals::state;

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::GrassSphereNormal);

			if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(pass->geometry->GetGeometryRuntimeData().properties[1].get())) {
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting)) {
					state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::GrassSphereNormal);
				}
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

struct IDXGISwapChain_Present
{
	static HRESULT WINAPI thunk(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
	{
		auto state = globals::state;
		auto menu = globals::menu;
		state->Reset();
		menu->DrawOverlay();

		HRESULT retval = func(This, SyncInterval, Flags);

		TracyD3D11Collect(state->tracyCtx);

		return retval;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

decltype(&CreateDXGIFactory) ptrCreateDXGIFactory;

HRESULT WINAPI hk_CreateDXGIFactory(REFIID, void** ppFactory)
{
	return ptrCreateDXGIFactory(__uuidof(IDXGIFactory4), ppFactory);
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

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

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

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

	return ret;
}

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
					if (globals::game::isVR) {
						// In VR, block mouse/keyboard input when menu is open (like Flatrim)
						// Allow gamepad input to pass through
						// Also handle VR controller devices based on OpenVR compatibility
						bool isVRController = ((device == RE::INPUT_DEVICES::INPUT_DEVICE::kVivePrimary) ||
											   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kViveSecondary) ||
											   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusPrimary) ||
											   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusSecondary) ||
											   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRPrimary) ||
											   (device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRSecondary));

						// Allow gamepad input to pass through always
						if (device == RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad) {
							blockedDevice = false;
						}
						// For VR controllers, only block if OpenVR is compatible
						else if (isVRController) {
							blockedDevice = globals::features::vr.IsOpenVRCompatible();
						}
						// For mouse/keyboard and other devices, block them (like Flatrim)
						else {
							blockedDevice = true;
						}
					} else {
						// Block all devices except gamepad when menu is open
						blockedDevice = (device != RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad);
					}
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

			globals::InstallD3DHooks(globals::d3d::context);

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

	struct CreateRenderTarget_MotionVectors
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_RefractionNormals
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			auto properties = *a_properties;
			properties.copyable = true;
			func(This, a_target, &properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_UnderwaterMask
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			auto properties = *a_properties;
			properties.copyable = true;
			func(This, a_target, &properties);
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
			auto& terrainHelper = globals::features::terrainHelper;
			if (vanillaResult && terrainHelper.loaded) {
				terrainHelper.TESObjectLAND_SetupMaterial(land);
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
			auto& terrainHelper = globals::features::terrainHelper;
			if (terrainHelper.loaded) {
				terrainHelper.BSLightingShader_SetupMaterial(material);
			}
		};
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
				auto& vl = globals::features::volumetricLighting;

				if (state->enabledClasses[RE::BSShader::Type::ImageSpace]) {
					RE::BSImagespaceShader* isShader = CurrentlyDispatchedShader;
					uint32_t techniqueId = CurrentComputeShaderTechniqueId;
					if (vl.loaded) {
						if (CurrentlyDispatchedShader == nullptr) {
							techniqueId = 0;
							if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingGenerateCS"sv) {
								isShader = vl.GetOrCreateGenerateCS(CurrentlyDispatchedComputeShader);
							} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingRaymarchCS"sv) {
								isShader = vl.GetOrCreateRaymarchCS(CurrentlyDispatchedComputeShader);
							}
						} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingBlurHCS"sv) {
							techniqueId = 0;
							isShader = vl.GetOrCreateBlurHCS(CurrentlyDispatchedComputeShader);
							vl.SetDimensionsCB();
							vl.SetGroupCountsHCS(threadGroupCountX);
						} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingBlurVCS"sv) {
							techniqueId = 0;
							isShader = vl.GetOrCreateBlurVCS(CurrentlyDispatchedComputeShader);
							vl.SetDimensionsCB();
							vl.SetGroupCountsVCS(threadGroupCountY);
						}
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

	struct BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights
	{
		static void thunk(RE::BSGraphics::PixelShader* PixelShader, RE::BSRenderPass* Pass, DirectX::XMMATRIX& Transform, uint32_t LightCount, uint32_t ShadowLightCount, float WorldScale, uint32_t)
		{
			if (globals::features::lightLimitFix.loaded)
				globals::features::lightLimitFix.BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(Pass);
			else
				func(PixelShader, Pass, Transform, LightCount, ShadowLightCount, WorldScale, 0);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImageSpace_Init_IBLF
	{
		static void thunk(char* a1,
			void* a2,
			void* a3,
			void* a4,
			void* a5,
			void* a6,
			void* a7)
		{
			auto enableIBLF = (float*)(REL::RelocationID(513510, 391362).address());
			*enableIBLF = false;

			func(a1, a2, a3, a4, a5, a6, a7);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
	 * @brief Installs hooks, detours, and memory patches for graphics, input, and rendering subsystems.
	 *
	 * Sets up function hooks and virtual method overrides for shader management, input polling, rendering pipeline stages, compute shader dispatch, material setup, batch rendering, and window procedure handling. Applies memory patches to adjust render pass cache sizes and offsets. Installs additional update hooks for frame timing and Reflex marker integration when not in VR mode.
	 */
	void Install()
	{
		if (!REL::Module::IsVR()) {
			logger::info("Hooking BSImageSpace::Init::IBLF");
			stl::detour_thunk<BSImageSpace_Init_IBLF>(REL::RelocationID(100480, 107198));
		}

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
		stl::write_thunk_call<CreateRenderTarget_MotionVectors>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x4F0, 0x4EF, 0x64E));

		stl::write_thunk_call<CreateRenderTarget_RefractionNormals>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x503, 0x502, 0x661));
		stl::write_thunk_call<CreateRenderTarget_UnderwaterMask>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xB19, 0xB19, 0xE06));

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

		logger::info("Installing SetupGeometry hooks");
		stl::write_vfunc<0x6, EffectExtensions::BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x6, LightingExtensions::BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		stl::write_thunk_call<GrassExtensions::BSGrassShaderProperty_ctor>(REL::RelocationID(15214, 15383).address() + REL::Relocate(0x45B, 0x4F5));
		stl::write_vfunc<0x6, GrassExtensions::BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);

		logger::info("Hooking TESObjectLAND");
		stl::detour_thunk<TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

		logger::info("Hooking BSLightingShader");
		stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);

		// Patch render space in BSLightingShader::SetupGeometry to always use world space
		// The variable updateEyePosition is set to 1 when not skinned. By patching to be 0 it will always use world space
		// We offset from the base address of the containing function to the start of the patch
		{
			logger::info("Patching BSLightingShader::SetupGeometry::updateEyePosition");
			auto setupGeometryUpdateRenderSpace = REL::RelocationID(100565, 107300).address();

			if (REL::Module::IsAE()) {
				std::uint8_t patch[] = { 0x41, 0x83, 0xE7, 0x00 };  // and r15d, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x71, patch, sizeof(patch));
			} else if (REL::Module::IsVR()) {
				std::uint8_t patch[] = { 0x41, 0x83, 0xE4, 0x00 };  // and r12d, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x65, patch, sizeof(patch));
			} else {
				std::uint8_t patch1[] = { 0xB8, 0x00, 0x00 };  // mov eax, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x73, patch1, sizeof(patch1));

				std::uint8_t patch2[] = { 0x45, 0x31, 0xC9 };  // xor r9d, r9d (zeros r9d)
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x36D, patch2, sizeof(patch2));

				std::uint8_t patch3[] = { 0x45, 0x31, 0xC0 };  // xor r8d, r8d (zeros r8d)
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x378, patch3, sizeof(patch3));
			}
		}

		stl::write_thunk_call<BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights>(REL::RelocationID(100565, 107300).address() + REL::Relocate(0x523, 0xB0E, 0x5FE));
	}

	void InstallEarlyHooks()
	{
		if (!globals::features::upscaling.loaded) {
			logger::info("Hooking D3D11CreateDeviceAndSwapChain");
			*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		}

		logger::info("Hooking CreateDXGIFactory");
		*(uintptr_t*)&ptrCreateDXGIFactory = SKSE::PatchIAT(hk_CreateDXGIFactory, "dxgi.dll", !REL::Module::IsVR() ? "CreateDXGIFactory" : "CreateDXGIFactory1");
	}
}
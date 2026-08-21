#include "FidelityFX.h"
#include "FSRHostLifecyclePolicy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <directx/d3dx12.h>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "../../ShaderCache.h"
#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

ffxFunctions ffxModule;

std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

namespace
{
	constexpr wchar_t kFrameGenerationDllName[] = L"amd_fidelityfx_framegeneration_dx12.dll";
	constexpr wchar_t kLoaderDllName[] = L"amd_fidelityfx_loader_dx12.dll";
	constexpr uint32_t kAmdVendorId = 0x1002u;
	constexpr uint32_t kNvidiaVendorId = 0x10DEu;

	void* s_fidelityFxDllDirectoryCookie = nullptr;

	bool ShouldEmitFidelityFXDiagLogs()
	{
		auto* state = globals::state;
		return state && state->IsDeveloperMode();
	}

	void ReleaseD3D11IdleFence(ID3D11Query*& a_query)
	{
		if (!a_query)
			return;

		a_query->Release();
		a_query = nullptr;
	}

	struct FSRSharpeningSettings
	{
		bool enabled = false;
		float sharpness = 0.0f;
	};

	FSRSharpeningSettings ResolveFSRSharpeningSettings(float a_sharpness)
	{
		const float sharpness =
			std::isfinite(a_sharpness) ?
				std::clamp(a_sharpness, 0.0f, 1.0f) :
				0.0f;
		return {
			.enabled = sharpness > 0.0f,
			.sharpness = sharpness
		};
	}

	void LogFSRSharpeningDispatch(const FSRSharpeningSettings& a_settings, const char* a_path)
	{
		const char* path = a_path && *a_path ? a_path : "unknown";
		struct DispatchLogState
		{
			bool initialized = false;
			bool enabled = false;
			float sharpness = -1.0f;
		};
		static DispatchLogState runtimeState{};
		static DispatchLogState hostState{};
		auto& state = std::strcmp(path, "runtime") == 0 ? runtimeState : hostState;
		if (state.initialized &&
			state.enabled == a_settings.enabled &&
			state.sharpness == a_settings.sharpness) {
			return;
		}

		state.initialized = true;
		state.enabled = a_settings.enabled;
		state.sharpness = a_settings.sharpness;
		logger::info(
			"[FidelityFX] FSR sharpening dispatch path={} enabled={} sharpness={:.2f}.",
			path,
			a_settings.enabled ? "yes" : "no",
			a_settings.sharpness);
	}

	FidelityFX::LifecycleResult BeginOrPollD3D11IdleFence(ID3D11DeviceContext* a_context, ID3D11Query*& a_query, const char* a_reason)
	{
		if (!a_context) {
			ReleaseD3D11IdleFence(a_query);
			return FidelityFX::LifecycleResult::Failed;
		}

		const auto pollFence = [&]() {
			BOOL completed = FALSE;
			const HRESULT dataResult = a_context->GetData(a_query, &completed, sizeof(completed), 0);
			if (dataResult == S_OK && completed) {
				ReleaseD3D11IdleFence(a_query);
				return FidelityFX::LifecycleResult::Ready;
			}

			if (dataResult == S_FALSE || dataResult == S_OK)
				return FidelityFX::LifecycleResult::Pending;

			logger::debug("[FidelityFX] D3D11 idle fence poll failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(dataResult));
			ReleaseD3D11IdleFence(a_query);
			return FidelityFX::LifecycleResult::Failed;
		};

		if (a_query)
			return pollFence();

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device) {
			logger::debug("[FidelityFX] D3D11 idle fence cannot be created before {} because the device is unavailable.", a_reason);
			return FidelityFX::LifecycleResult::Failed;
		}

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;

		const HRESULT createResult = device->CreateQuery(&queryDesc, &a_query);
		device->Release();

		if (FAILED(createResult) || !a_query) {
			a_context->Flush();
			logger::debug("[FidelityFX] D3D11 idle fence creation failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(createResult));
			return FidelityFX::LifecycleResult::Failed;
		}

		a_context->End(a_query);
		a_context->Flush();
		return pollFence();
	}

	bool UseSplitPerEyeFSRContexts()
	{
		return globals::game::isVR;
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_outDesc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_outDesc);
		return true;
	}

	void GetRuntimeUpscaleSizes(float2& a_displaySize, float2& a_renderSize)
	{
		auto state = globals::state;
		if (!state) {
			a_displaySize = { 0.0f, 0.0f };
			a_renderSize = { 0.0f, 0.0f };
			return;
		}

		auto& upscaling = globals::features::upscaling;
		const auto& resolutionPlan = upscaling.GetRuntimeResolutionPlan();
		a_displaySize = resolutionPlan.finalOutputSize;
		a_renderSize = VRGeometry::ToFloat2(resolutionPlan.engineRenderSize);

		if (a_displaySize.x <= 0.0f || a_displaySize.y <= 0.0f)
			a_displaySize = state->screenSize;
		if (a_renderSize.x <= 0.0f || a_renderSize.y <= 0.0f)
			a_renderSize = Util::ConvertToDynamic(a_displaySize);
	}

	bool TryGetCurrentAdapterDesc(DXGI_ADAPTER_DESC& a_outDesc)
	{
		if (!globals::d3d::device)
			return false;

		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))))
			return false;

		winrt::com_ptr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(adapter.put())))
			return false;

		a_outDesc = {};
		if (FAILED(adapter->GetDesc(&a_outDesc)))
			return false;

		return true;
	}

	std::string ToUpperAscii(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return a_value;
	}

	FidelityFX::Fsr4AdapterSupport ClassifyFsr4AdapterSupport(const DXGI_ADAPTER_DESC& a_desc)
	{
		if (a_desc.VendorId != kAmdVendorId)
			return FidelityFX::Fsr4AdapterSupport::Unsupported;

		std::wstring wideDescription(a_desc.Description);
		const std::string description = ToUpperAscii(stl::utf16_to_utf8(wideDescription).value_or(""));

		// FSR 4.1.1 supports AMD's RDNA 3/RX 7000 and RDNA 4/RX 9000
		// discrete GPUs. Do not treat a generic RDNA 3 marker as sufficient:
		// it can also describe an unsupported integrated adapter.
		const bool isKnownRdna3DiscreteDie =
			description.find("NAVI31") != std::string::npos ||
			description.find("NAVI 31") != std::string::npos ||
			description.find("NAVI32") != std::string::npos ||
			description.find("NAVI 32") != std::string::npos ||
			description.find("NAVI33") != std::string::npos ||
			description.find("NAVI 33") != std::string::npos;
		if (isKnownRdna3DiscreteDie)
			return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;

		if (description.find("RDNA4") != std::string::npos ||
			description.find("RDNA 4") != std::string::npos ||
			description.find("NAVI4") != std::string::npos ||
			description.find("NAVI 4") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
		}

		size_t searchPosition = 0;
		while (searchPosition < description.length()) {
			while (searchPosition < description.length() && !std::isdigit(static_cast<unsigned char>(description[searchPosition])))
				searchPosition++;

			const size_t modelStart = searchPosition;
			while (searchPosition < description.length() && std::isdigit(static_cast<unsigned char>(description[searchPosition])))
				searchPosition++;

			if (searchPosition > modelStart) {
				const std::string modelText = description.substr(modelStart, searchPosition - modelStart);
				char* parseEnd = nullptr;
				const unsigned long modelNumber = std::strtoul(modelText.c_str(), &parseEnd, 10);
				const bool isRadeon7000Series = modelNumber >= 7000ul && modelNumber < 8000ul;
				const bool isRadeon9000Series = modelNumber >= 9000ul && modelNumber < 10000ul;
				if (parseEnd != modelText.c_str() && isRadeon7000Series)
					return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;
				if (parseEnd != modelText.c_str() && isRadeon9000Series)
					return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
			}
		}

		// Keep fallbacks for abbreviated naming variants that don't include full numeric model text.
		if (description.find("RX 70") != std::string::npos ||
			description.find("RX70") != std::string::npos ||
			description.find("RADEON 70") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;
		}
		if (description.find("RX 90") != std::string::npos ||
			description.find("RX90") != std::string::npos ||
			description.find("RADEON 90") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
		}

		return FidelityFX::Fsr4AdapterSupport::Unsupported;
	}

	std::string UpscalerVersionToString(uint32_t a_version)
	{
		const uint32_t major = (a_version >> 22) & 0x3FFu;
		const uint32_t minor = (a_version >> 12) & 0x3FFu;
		const uint32_t patch = a_version & 0xFFFu;
		return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
	}

	const std::string& PendingFsrDispatchLabel()
	{
		static const std::string label = "Pending FSR dispatch";
		return label;
	}

	const std::string& HostFsrFallbackLabel()
	{
		static const std::string label = std::format("{} fallback", FidelityFX::GetHostFsrSdkLabel());
		return label;
	}

	bool RuntimeProviderNameMatchesVersion(const std::string& a_providerName, uint32_t a_version)
	{
		return !a_providerName.empty() &&
		       a_providerName.find(UpscalerVersionToString(a_version)) != std::string::npos;
	}

	bool RuntimeProviderIdMatchesVersion(uint64_t a_providerId, uint32_t a_version)
	{
		return a_providerId != 0 &&
		       static_cast<uint32_t>(a_providerId & 0xFFFFFFFFull) == a_version;
	}

	bool RuntimeProviderMatchesVersion(uint64_t a_providerId, const std::string& a_providerName, uint32_t a_version)
	{
		return RuntimeProviderIdMatchesVersion(a_providerId, a_version) ||
		       RuntimeProviderNameMatchesVersion(a_providerName, a_version);
	}

	std::string RuntimeProviderDisplayName(uint64_t a_providerId, const std::string& a_providerName)
	{
		if (!a_providerName.empty())
			return a_providerName;
		if (a_providerId != 0)
			return std::format("id 0x{:X}", a_providerId);

		return {};
	}

	void RuntimeFfxMessage(uint32_t a_type, const wchar_t* a_message)
	{
		const std::string message = stl::utf16_to_utf8(a_message ? a_message : L"").value_or("unknown FidelityFX runtime message");
		if (a_type == FFX_API_MESSAGE_TYPE_ERROR) {
			logger::error("[FidelityFX] {}", message);
		} else {
			logger::warn("[FidelityFX] {}", message);
		}
	}

	void EnsureFidelityFxDllDirectory(const std::filesystem::path& a_pluginDir)
	{
		if (s_fidelityFxDllDirectoryCookie) {
			return;
		}

		auto kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (!kernel32) {
			logger::warn("[FidelityFX] Could not get kernel32 module while preparing FidelityFX DLL search path");
			return;
		}

		using AddDllDirectoryFn = void*(WINAPI*)(PCWSTR);
		auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel32, "AddDllDirectory"));
		if (!addDllDirectory) {
			logger::warn("[FidelityFX] AddDllDirectory is unavailable; FidelityFX provider discovery will rely on explicit DLL loads");
			return;
		}

		s_fidelityFxDllDirectoryCookie = addDllDirectory(a_pluginDir.c_str());
		if (!s_fidelityFxDllDirectoryCookie) {
			logger::warn(
				"[FidelityFX] Failed to add FidelityFX DLL directory {} (error {})",
				stl::utf16_to_utf8(a_pluginDir.wstring()).value_or("<unknown>"),
				GetLastError());
		}
	}

	HMODULE LoadFidelityFxDll(const std::filesystem::path& a_path, DWORD& a_error)
	{
		a_error = ERROR_SUCCESS;

		constexpr DWORD kLoadFlags =
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS;

		auto module = LoadLibraryExW(a_path.c_str(), nullptr, kLoadFlags);
		if (module) {
			return module;
		}

		a_error = GetLastError();
		logger::warn(
			"[FidelityFX] LoadLibraryEx failed for {} with error {}; retrying legacy LoadLibrary",
			stl::utf16_to_utf8(a_path.wstring()).value_or("<unknown>"),
			a_error);

		module = LoadLibraryW(a_path.c_str());
		if (module) {
			a_error = ERROR_SUCCESS;
			return module;
		}

		a_error = GetLastError();
		return nullptr;
	}

	bool QueryRuntimeUpscalerVersionId(ID3D12Device* a_device, uint32_t a_requestedVersion, uint64_t& a_versionId, std::string& a_versionName)
	{
		a_versionId = 0;
		a_versionName.clear();

		if (!a_device || !ffxModule.Query) {
			return false;
		}

		uint64_t versionCount = 0;
		ffxQueryDescGetVersions countQuery{};
		countQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		countQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		countQuery.device = a_device;
		countQuery.outputCount = &versionCount;

		auto countResult = ffxModule.Query(nullptr, &countQuery.header);
		if (countResult != FFX_API_RETURN_OK || versionCount == 0) {
			logger::warn(
				"[FidelityFX] Runtime upscaler version query failed or returned no versions (code {}, count {})",
				static_cast<uint32_t>(countResult),
				versionCount);
			return false;
		}

		std::vector<uint64_t> versionIds(versionCount, 0);
		std::vector<const char*> versionNames(versionCount, nullptr);

		uint64_t returnedVersionCount = versionCount;
		ffxQueryDescGetVersions versionsQuery{};
		versionsQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		versionsQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		versionsQuery.device = a_device;
		versionsQuery.outputCount = &returnedVersionCount;
		versionsQuery.versionIds = versionIds.data();
		versionsQuery.versionNames = versionNames.data();

		auto versionsResult = ffxModule.Query(nullptr, &versionsQuery.header);
		if (versionsResult != FFX_API_RETURN_OK) {
			logger::warn("[FidelityFX] Runtime upscaler version enumeration failed with code {}", static_cast<uint32_t>(versionsResult));
			return false;
		}

		const auto requestedVersion = UpscalerVersionToString(a_requestedVersion);
		const auto resultCount = std::min<size_t>(versionIds.size(), returnedVersionCount);

		logger::debug(
			"[FidelityFX] Runtime upscaler reported {} version provider(s); requested FSR version {}",
			resultCount,
			requestedVersion);

		for (size_t i = 0; i < resultCount; ++i) {
			const std::string versionName = versionNames[i] ? versionNames[i] : "";
			logger::debug(
				"[FidelityFX] Runtime upscaler version provider {}: '{}' (id 0x{:X})",
				i,
				versionName.empty() ? "(unnamed)" : versionName,
				versionIds[i]);

			if (versionIds[i] == a_requestedVersion ||
				RuntimeProviderIdMatchesVersion(versionIds[i], a_requestedVersion) ||
				RuntimeProviderNameMatchesVersion(versionName, a_requestedVersion)) {
				a_versionId = versionIds[i];
				a_versionName = versionName;
				return true;
			}
		}

		logger::warn(
			"[FidelityFX] Runtime upscaler did not report requested FSR version {}; falling back to upscaler version descriptor",
			requestedVersion);
		return false;
	}

	std::string FfxCreateResultText(bool a_attempted, ffxReturnCode_t a_result)
	{
		if (!a_attempted) {
			return "not attempted";
		}

		return std::format("code {}", static_cast<uint32_t>(a_result));
	}

	enum class RuntimeUpscalerCreateAttempt : uint8_t
	{
		kGenericOverrideOnly,
		kGenericOverrideWithUpscalerVersion,
		kUpscalerVersionDescriptor,
		kDefaultProvider
	};

	struct RuntimeUpscalerCreateAttemptResult
	{
		RuntimeUpscalerCreateAttempt attempt;
		bool enabled;
		bool attempted = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
	};

	ffxReturnCode_t CreateRuntimeUpscalerContextProtected(
		ffx::Context* a_context,
		ffxCreateContextDescHeader* a_desc,
		bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.CreateContext)
				result = ffxModule.CreateContext(a_context, a_desc, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	ffxReturnCode_t DestroyRuntimeUpscalerContextProtected(
		ffx::Context* a_context,
		bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.DestroyContext)
				result = ffxModule.DestroyContext(a_context, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	ffxReturnCode_t DispatchRuntimeUpscalerProtected(
		ffx::Context* a_context,
		const ffxDispatchDescHeader* a_desc,
		bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.Dispatch)
				result = ffxModule.Dispatch(a_context, a_desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	ffxReturnCode_t TryCreateRuntimeUpscalerContext(
		ffx::Context& a_context,
		RuntimeUpscalerCreateAttempt a_attempt,
		ffx::CreateContextDescUpscale& a_createDesc,
		ffx::CreateBackendDX12Desc& a_backendDesc,
		ffx::CreateContextDescUpscaleVersion& a_versionDesc,
		ffx::CreateContextDescOverrideVersion& a_overrideVersionDesc,
		bool& a_callCrashed,
		bool& a_ownershipIndeterminate)
	{
		a_callCrashed = false;
		a_ownershipIndeterminate = false;
		if (a_context)
			return FFX_API_RETURN_ERROR;
		a_createDesc.header.pNext = nullptr;
		a_backendDesc.header.pNext = nullptr;
		a_versionDesc.header.pNext = nullptr;
		a_overrideVersionDesc.header.pNext = nullptr;

		switch (a_attempt) {
		case RuntimeUpscalerCreateAttempt::kGenericOverrideOnly:
			a_createDesc.header.pNext = &a_backendDesc.header;
			a_backendDesc.header.pNext = &a_overrideVersionDesc.header;
			break;
		case RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion:
			a_createDesc.header.pNext = &a_backendDesc.header;
			a_backendDesc.header.pNext = &a_versionDesc.header;
			a_versionDesc.header.pNext = &a_overrideVersionDesc.header;
			break;
		case RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor:
			a_createDesc.header.pNext = &a_versionDesc.header;
			a_versionDesc.header.pNext = &a_backendDesc.header;
			break;
		case RuntimeUpscalerCreateAttempt::kDefaultProvider:
			a_createDesc.header.pNext = &a_backendDesc.header;
			break;
		}

		ffx::Context createdContext = nullptr;
		const auto result = CreateRuntimeUpscalerContextProtected(
			&createdContext,
			&a_createDesc.header,
			a_callCrashed);
		if (a_callCrashed) {
			// A provider fault gives no ownership proof even if it did not publish a
			// handle. Quarantine the provider rather than probing it again.
			a_context = createdContext;
			a_ownershipIndeterminate = true;
			return FFX_API_RETURN_ERROR;
		}
		if (result == FFX_API_RETURN_OK) {
			if (createdContext) {
				a_context = createdContext;
				return result;
			}

			// A provider that reports success without publishing its handle has
			// violated the ownership protocol. Hidden allocations cannot be ruled
			// out, so do not issue another create probe this session.
			a_ownershipIndeterminate = true;
			return FFX_API_RETURN_ERROR;
		} else if (createdContext) {
			// Some providers return an error with a partially created handle. Make
			// exactly one protected release attempt. If release is not proven, retain
			// the original handle and never call into it again this session.
			const auto retainedContext = createdContext;
			bool destroyCrashed = false;
			const auto destroyResult = DestroyRuntimeUpscalerContextProtected(
				&createdContext,
				destroyCrashed);
			if (destroyCrashed || destroyResult != FFX_API_RETURN_OK) {
				a_context = retainedContext;
				a_ownershipIndeterminate = true;
			}
		}

		return result;
	}

	D3D11_TEXTURE2D_DESC MakeSharedTextureDesc(const D3D11_TEXTURE2D_DESC& a_sourceDesc, uint32_t a_width, uint32_t a_height, UINT a_bindFlags)
	{
		D3D11_TEXTURE2D_DESC desc = a_sourceDesc;
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = a_bindFlags;
		desc.MiscFlags = 0;
		return desc;
	}

	bool SameTextureDesc(const D3D11_TEXTURE2D_DESC& a_left, const D3D11_TEXTURE2D_DESC& a_right)
	{
		return a_left.Width == a_right.Width &&
		       a_left.Height == a_right.Height &&
		       a_left.MipLevels == a_right.MipLevels &&
		       a_left.ArraySize == a_right.ArraySize &&
		       a_left.Format == a_right.Format &&
		       a_left.SampleDesc.Count == a_right.SampleDesc.Count &&
		       a_left.SampleDesc.Quality == a_right.SampleDesc.Quality &&
		       a_left.Usage == a_right.Usage &&
		       a_left.BindFlags == a_right.BindFlags &&
		       a_left.CPUAccessFlags == a_right.CPUAccessFlags &&
		       a_left.MiscFlags == a_right.MiscFlags;
	}

	template <class T, size_t N>
	void ResetOwnedResourceArray(std::array<std::unique_ptr<T>, N>& a_resources)
	{
		for (auto& resource : a_resources)
			resource.reset();
	}

	bool DispatchHostFsr3UpscaleProtected(FfxFsr3Context& a_context, FfxFsr3DispatchUpscaleDescription& a_dispatchParameters, bool& a_crashed)
	{
		a_crashed = false;
		bool dispatchOk = true;

		__try {
			dispatchOk = ffxFsr3ContextDispatchUpscale(&a_context, &a_dispatchParameters) == FFX_OK;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
			dispatchOk = false;
		}

		return dispatchOk;
	}

	FfxErrorCode GetHostFsr3InterfaceProtected(
		FfxInterface* a_interface,
		FfxDevice a_device,
		void* a_scratchBuffer,
		size_t a_scratchBufferSize,
		uint32_t a_contextCount,
		bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxGetInterfaceDX11(
				a_interface,
				a_device,
				a_scratchBuffer,
				a_scratchBufferSize,
				a_contextCount);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	FfxErrorCode CreateHostFsr3ContextProtected(
		FfxFsr3Context* a_context,
		FfxFsr3ContextDescription* a_description,
		bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxFsr3ContextCreate(a_context, a_description);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	FfxErrorCode DestroyHostFsr3ContextProtected(
		FfxFsr3Context* a_context,
		bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxFsr3ContextDestroy(a_context);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	bool IsD3DDeviceLossReason(HRESULT a_result)
	{
		return a_result == DXGI_ERROR_DEVICE_REMOVED ||
		       a_result == DXGI_ERROR_DEVICE_RESET ||
		       a_result == DXGI_ERROR_DEVICE_HUNG ||
		       a_result == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
		       a_result == DXGI_ERROR_INVALID_CALL;
	}

	constexpr bool IsTerminalRuntimeQuarantineResult(
		FidelityFX::LifecycleResult a_result) noexcept
	{
		return a_result == FidelityFX::LifecycleResult::Failed ||
		       a_result == FidelityFX::LifecycleResult::DeviceLost ||
		       a_result == FidelityFX::LifecycleResult::RuntimeDeviceLost;
	}

	constexpr FidelityFX::LifecycleResult NormalizeRuntimeQuarantineResult(
		FidelityFX::LifecycleResult a_result) noexcept
	{
		return a_result == FidelityFX::LifecycleResult::DeviceLost ||
		               a_result == FidelityFX::LifecycleResult::RuntimeDeviceLost ?
		           a_result :
		           FidelityFX::LifecycleResult::Failed;
	}
}

FidelityFX::~FidelityFX()
{
	ResetFSRIdleFence();
}

FidelityFX::LifecycleResult FidelityFX::RecordFSRDeviceStatus() noexcept
{
	auto* d3d11Device = globals::d3d::device;
	if (!d3d11Device) {
		return IsD3DDeviceLossReason(fsrLastDeviceRemovedReason) ?
			LifecycleResult::DeviceLost : LifecycleResult::Failed;
	}

	const HRESULT d3d11Reason = d3d11Device ? d3d11Device->GetDeviceRemovedReason() : S_OK;
	if (IsD3DDeviceLossReason(d3d11Reason)) {
		fsrLastDeviceRemovedReason = d3d11Reason;
	} else if (!IsD3DDeviceLossReason(fsrLastDeviceRemovedReason)) {
		fsrLastDeviceRemovedReason = d3d11Reason;
	}
	return IsD3DDeviceLossReason(fsrLastDeviceRemovedReason) ?
		LifecycleResult::DeviceLost : LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::RecordRuntimeUpscalerDeviceStatus() noexcept
{
	const auto primaryResult = RecordFSRDeviceStatus();
	if (primaryResult != LifecycleResult::Ready)
		return primaryResult;

	auto* d3d12Device = globals::features::upscaling.dx12SwapChain.d3d12Device.get();
	if (!d3d12Device) {
		return IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason) ?
			LifecycleResult::RuntimeDeviceLost : LifecycleResult::Failed;
	}

	const HRESULT d3d12Reason = d3d12Device->GetDeviceRemovedReason();
	if (IsD3DDeviceLossReason(d3d12Reason)) {
		runtimeUpscalerLastDeviceRemovedReason = d3d12Reason;
	} else if (!IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason)) {
		runtimeUpscalerLastDeviceRemovedReason = d3d12Reason;
	}
	return IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason) ?
		LifecycleResult::RuntimeDeviceLost : LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ResolveFSRLifecycleFailure(const char* a_operation)
{
	const auto deviceResult = RecordFSRDeviceStatus();
	if (deviceResult == LifecycleResult::DeviceLost) {
		logger::error(
			"[FidelityFX] D3D11 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "FSR lifecycle work",
			static_cast<std::uint32_t>(fsrLastDeviceRemovedReason));
		return LifecycleResult::DeviceLost;
	}

	return LifecycleResult::Failed;
}

FidelityFX::LifecycleResult FidelityFX::ResolveRuntimeUpscalerLifecycleFailure(const char* a_operation)
{
	const auto deviceResult = RecordRuntimeUpscalerDeviceStatus();
	if (deviceResult == LifecycleResult::DeviceLost) {
		logger::error(
			"[FidelityFX] Primary D3D11 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "runtime upscaler lifecycle work",
			static_cast<std::uint32_t>(fsrLastDeviceRemovedReason));
		return deviceResult;
	}
	if (deviceResult == LifecycleResult::RuntimeDeviceLost) {
		logger::error(
			"[FidelityFX] Runtime-provider D3D12 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "runtime upscaler lifecycle work",
			static_cast<std::uint32_t>(runtimeUpscalerLastDeviceRemovedReason));
		QuarantineRuntimeUpscalerForSession("runtime-provider D3D12 device removal");
		for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i) {
			if (runtimeUpscalerContexts[i])
				runtimeUpscalerContextIndeterminate[i] = true;
		}
		// Quarantine initializes ordinary asynchronous retirement. A removed
		// optional D3D12 device has no trustworthy retirement path, so detach
		// that ownership domain instead and retain every object for the session.
		runtimeUpscalerQuarantineRetirement = LifecycleResult::RuntimeDeviceLost;
		return deviceResult;
	}

	return LifecycleResult::Failed;
}

bool FidelityFX::IsRuntimeUpscalerOwnershipDetached() const noexcept
{
	return runtimeUpscalerSessionQuarantined &&
	       runtimeUpscalerQuarantineRetirement == LifecycleResult::RuntimeDeviceLost;
}

void FidelityFX::QuarantineHostFSRState(const char* a_reason)
{
	if (fsrHostStateQuarantined)
		return;

	fsrHostStateQuarantined = true;
	logger::critical(
		"[FidelityFX] Quarantined indeterminate host FSR state after {}; retained contexts and backend scratch will not be reused or destroyed this session.",
		a_reason && *a_reason ? a_reason : "an unknown lifecycle exception");
}

void FidelityFX::QuarantineHostFSRContext(uint32_t a_contextIndex, const char* a_reason)
{
	if (a_contextIndex < std::size(fsrContext)) {
		fsrContextValid[a_contextIndex] = false;
		fsrContextIndeterminate[a_contextIndex] = true;
	}
	QuarantineHostFSRState(a_reason);
}

FidelityFX::LifecycleResult FidelityFX::GetQuarantinedHostFSRResult(const char* a_operation)
{
	if (!fsrHostStateQuarantined)
		return LifecycleResult::Failed;

	return ResolveFSRLifecycleFailure(a_operation);
}

FidelityFX::LifecycleResult FidelityFX::RetireRuntimeUpscalerWhileHostFSRQuarantined(
	const char* a_operation)
{
	const auto hostResult = GetQuarantinedHostFSRResult(a_operation);
	if (hostResult == LifecycleResult::DeviceLost)
		return hostResult;
	if (IsRuntimeUpscalerOwnershipDetached())
		return hostResult;

	// Host FSR3 contexts and their scratch buffer share indeterminate SDK
	// ownership and must remain quarantined. Runtime/DX12 provider objects are
	// independently tracked, so retire those normally instead of leaking both
	// ownership domains for the rest of the process.
	const auto idleResult = PollRuntimeUpscalerTeardownReady(a_operation);
	if (idleResult != LifecycleResult::Ready)
		return idleResult;

	const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
	if (contextDestroyResult != LifecycleResult::Ready)
		return contextDestroyResult;
	const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
	if (resourceDestroyResult != LifecycleResult::Ready)
		return resourceDestroyResult;

	ReleaseIdleRuntimeUpscalerInterop();
	ResetRuntimeUpscalerTracking(true);
	return hostResult;
}

FidelityFX::LifecycleResult FidelityFX::DestroyTrackedHostFSRContexts(const char* a_operation)
{
	if (fsrHostStateQuarantined)
		return GetQuarantinedHostFSRResult(a_operation);

	for (uint32_t i = 0; i < std::size(fsrContextValid); ++i) {
		if (!fsrContextValid[i])
			continue;

		bool destroyCrashed = false;
		const FfxErrorCode destroyResult =
			DestroyHostFsr3ContextProtected(&fsrContext[i], destroyCrashed);
		const auto disposition = FSRHostLifecyclePolicy::ClassifyCallDisposition(
			destroyCrashed,
			destroyResult == FFX_OK);
		if (disposition == FSRHostLifecyclePolicy::CallDisposition::Faulted) {
			logger::error(
				"[FidelityFX] FSR3 context destruction for eye {} faulted during {}.",
				i,
				a_operation && *a_operation ? a_operation : "host context cleanup");
			QuarantineHostFSRContext(i, "an FSR3 context destruction fault");
			return GetQuarantinedHostFSRResult(a_operation);
		}

		if (!FSRHostLifecyclePolicy::RequiresOwnershipQuarantine(disposition)) {
			fsrContext[i] = {};
			fsrContextValid[i] = false;
			fsrContextIndeterminate[i] = false;
			continue;
		}

		const auto failureResult = ResolveFSRLifecycleFailure(a_operation);
		logger::critical(
			"[FidelityFX] FSR3 context destruction for eye {} returned an error during {}; quarantining partially destroyed state and its backend scratch buffer.",
			i,
			a_operation && *a_operation ? a_operation : "host context cleanup");
		QuarantineHostFSRContext(i, "an FSR3 context destruction error");
		return failureResult;
	}

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ReleaseHostFSRResources()
{
	const bool anyValidContext =
		std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; });
	const bool anyIndeterminateContext =
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; });
	if (!FSRHostLifecyclePolicy::CanReleaseHostOwnership(
			fsrHostStateQuarantined,
			anyValidContext,
			anyIndeterminateContext)) {
		logger::critical("[FidelityFX] Refusing to release quarantined host FSR resources.");
		return GetQuarantinedHostFSRResult("host FSR ownership release");
	}

	for (uint32_t i = 0; i < std::size(fsrContext); ++i) {
		fsrContext[i] = {};
		fsrContextValid[i] = false;
		fsrContextIndeterminate[i] = false;
	}
	fsrContextCount = 0;
	fsrContextMaxRenderWidth = 0;
	fsrContextMaxRenderHeight = 0;
	fsrContextDisplayWidth = 0;
	fsrContextDisplayHeight = 0;
	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}
	return LifecycleResult::Ready;
}

void FidelityFX::LoadFFX()
{
	const std::filesystem::path pluginDir = std::filesystem::absolute(std::filesystem::path(FidelityFX::PluginDir));

	ResetRuntimeUpscalerTracking(true);
	EnsureFidelityFxDllDirectory(pluginDir);

	const std::filesystem::path framegenPath = pluginDir / kFrameGenerationDllName;
	const std::filesystem::path loaderPath = pluginDir / kLoaderDllName;
	const std::filesystem::path upscalerPath = pluginDir / RuntimeUpscalerDllName.data();

	const bool framegenDllExists = std::filesystem::exists(framegenPath);
	const bool upscalerDllExists = std::filesystem::exists(upscalerPath);
	DWORD framegenLoadError = ERROR_SUCCESS;
	DWORD upscalerLoadError = ERROR_SUCCESS;
	DWORD loaderLoadError = ERROR_SUCCESS;

	if (!module) {
		module = LoadFidelityFxDll(loaderPath, loaderLoadError);
	}
	if (!frameGenerationModule && framegenDllExists) {
		frameGenerationModule = LoadFidelityFxDll(framegenPath, framegenLoadError);
	}
	if (!runtimeUpscalerModule && upscalerDllExists) {
		runtimeUpscalerModule = LoadFidelityFxDll(upscalerPath, upscalerLoadError);
	}

	featureFSR3FG = frameGenerationModule != nullptr;
	featureRuntimeUpscaler = runtimeUpscalerModule != nullptr;

	FidelityFX::dllVersions = Util::EnumerateDllVersions(pluginDir);
	for (const auto& [name, versionStr] : FidelityFX::dllVersions)
		logger::info("[FidelityFX] {} version: {}", name, versionStr);

	if (module) {
		ffxLoadFunctions(&ffxModule, module);
		logger::info("[FidelityFX] Loader DLL loaded successfully from plugin directory");
	} else {
		logger::error("[FidelityFX] Failed to load {} from plugin directory (Win32 error {})",
			stl::utf16_to_utf8(kLoaderDllName).value_or("loader DLL"),
			loaderLoadError);
	}

	if (featureFSR3FG) {
		logger::info("[FidelityFX] Frame generation DLL loaded and available");
	} else if (framegenDllExists) {
		logger::warn("[FidelityFX] Frame generation DLL found but failed to load (Win32 error {}) - FSR3 frame generation disabled",
			framegenLoadError);
	} else {
		logger::warn("[FidelityFX] Frame generation DLL not found - FSR3 frame generation disabled");
	}

	if (featureRuntimeUpscaler) {
		logger::info("[FidelityFX] Runtime upscaler DLL loaded; runtime availability will be verified during context creation");
	} else if (upscalerDllExists) {
		logger::warn("[FidelityFX] Runtime upscaler DLL found but failed to load (Win32 error {}) - runtime FSR path disabled",
			upscalerLoadError);
	} else {
		logger::warn("[FidelityFX] Runtime upscaler DLL not found - runtime FSR path disabled");
	}
}

bool FidelityFX::HasRuntimeUpscalerSupportCheckResult() const
{
	return runtimeUpscalerSupportCheckKnown;
}

bool FidelityFX::IsRuntimeUpscalerSupportConfirmed() const
{
	return runtimeUpscalerSupportCheckKnown && runtimeUpscalerSupportConfirmed;
}

bool FidelityFX::IsRuntimeUpscalerProviderMatchingRequestedVersion() const
{
	if (!runtimeUpscalerSupportCheckKnown || !runtimeUpscalerSupportConfirmed)
		return false;

	if (runtimeUpscalerProviderMatchedVersionName.empty() && runtimeUpscalerProviderMatchedVersionId == 0)
		return true;

	const uint32_t requestedVersion = runtimeUpscalerRequestedVersion ? runtimeUpscalerRequestedVersion : GetPreferredRuntimeUpscalerVersion();
	return RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, requestedVersion);
}

bool FidelityFX::IsRuntimeUpscalerFailureLatched() const
{
	return runtimeUpscalerFailureLatched || runtimeUpscalerSessionQuarantined;
}

bool FidelityFX::IsRuntimeFsr4FailureLatched() const
{
	return runtimeFsr4FailureLatched;
}

const std::string& FidelityFX::GetHostFsrSdkLabel()
{
	static const std::string label = std::format("Host FSR3 SDK {}", UpscalerVersionToString(Fsr3Version));
	return label;
}

const std::string& FidelityFX::GetRuntimeUpscalerLabel(uint32_t a_version)
{
	static const std::string runtimeFsr3Label = std::format("Runtime FSR3 {} ({})", UpscalerVersionToString(Fsr3Version), RuntimeUpscalerDllNameUtf8);
	static const std::string runtimeFsr4Label = std::format("Runtime FSR4 ({})", RuntimeUpscalerDllNameUtf8);

	if (a_version == Fsr3Version)
		return runtimeFsr3Label;
	if (a_version == FFX_UPSCALER_VERSION)
		return runtimeFsr4Label;

	thread_local std::string fallbackLabel;
	fallbackLabel = std::format("Runtime FSR {} ({})", UpscalerVersionToString(a_version), RuntimeUpscalerDllNameUtf8);
	return fallbackLabel;
}

const std::string& FidelityFX::GetRuntimeUpscalerLastFramePathLabel() const
{
	if (!runtimeUpscalerLastFramePathValid)
		return PendingFsrDispatchLabel();

	switch (runtimeUpscalerLastFramePath) {
	case RuntimeUpscalerFramePath::kHostFsr31:
		return GetHostFsrSdkLabel();
	case RuntimeUpscalerFramePath::kRuntimeFsr31:
		return GetRuntimeUpscalerLabel(Fsr3Version);
	case RuntimeUpscalerFramePath::kRuntimeFsr4:
		return GetRuntimeUpscalerLabel(FFX_UPSCALER_VERSION);
	case RuntimeUpscalerFramePath::kHostFsr31Fallback:
		return HostFsrFallbackLabel();
	case RuntimeUpscalerFramePath::kInactive:
	default:
		return PendingFsrDispatchLabel();
	}
}

const std::string& FidelityFX::GetConfiguredFsrPathLabel() const
{
	if (runtimeUpscalerSessionQuarantined || runtimeUpscalerFailureLatched)
		return HostFsrFallbackLabel();

	if (runtimeFsr4FailureLatched) {
		if (ShouldUseRuntimeUpscalerForFSR())
			return GetRuntimeUpscalerLabel(Fsr3Version);

		return HostFsrFallbackLabel();
	}

	if (runtimeUpscalerSupportCheckKnown && !runtimeUpscalerSupportConfirmed)
		return HostFsrFallbackLabel();

	if (ShouldRequestRuntimeFsr4())
		return GetRuntimeUpscalerLabel(FFX_UPSCALER_VERSION);

	if (ShouldUseRuntimeUpscalerForFSR())
		return GetRuntimeUpscalerLabel(Fsr3Version);

	return GetHostFsrSdkLabel();
}

const std::string& FidelityFX::GetDisplayedFsrPathLabel() const
{
	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	if (runtimeUpscalerLastFramePathValid && runtimeUpscalerLastFrameIndex == currentFrame)
		return GetRuntimeUpscalerLastFramePathLabel();

	return GetConfiguredFsrPathLabel();
}

std::string FidelityFX::GetRuntimeUpscalerProviderName() const
{
	return RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName);
}

std::string FidelityFX::GetRuntimeUpscalerRequestedVersionString() const
{
	const uint32_t requestedVersion = runtimeUpscalerRequestedVersion ? runtimeUpscalerRequestedVersion : GetPreferredRuntimeUpscalerVersion();
	return UpscalerVersionToString(requestedVersion);
}

#ifdef DEVBENCH_BRIDGE_ENABLED
FidelityFX::RuntimeUpscalerDispatchSnapshot FidelityFX::GetRuntimeUpscalerDispatchSnapshotForRenderThread() const
{
	return {
		devBenchSuccessfulDispatch.valid,
		devBenchSuccessfulDispatch.frame,
		devBenchSuccessfulDispatch.path,
		devBenchSuccessfulDispatch.serial,
	};
}
#endif

void FidelityFX::ResetRuntimeUpscalerTracking(bool a_invalidateProviderCache)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return;

	runtimeUpscalerFailureLatched = false;
	runtimeFsr4FailureLatched = false;
	runtimeFallbackResetDispatchesRemaining = 0;
	runtimeResumeResetDispatchesRemaining = 0;
	runtimeHostFallbackActive = false;
	runtimeUpscalerLastFramePathValid = false;
	runtimeUpscalerLastFrameIndex = 0;
	runtimeUpscalerLastFramePath = RuntimeUpscalerFramePath::kInactive;
#ifdef DEVBENCH_BRIDGE_ENABLED
	devBenchSuccessfulDispatch = {};
#endif

	if (!a_invalidateProviderCache)
		return;

	runtimeUpscalerSupportCheckKnown = false;
	runtimeUpscalerSupportConfirmed = false;
	runtimeUpscalerProviderMatchedVersionId = 0;
	runtimeUpscalerProviderMatchedVersionName.clear();
}

void FidelityFX::LatchRuntimeFsr4Failure()
{
	if (runtimeFsr4FailureLatched)
		return;

	runtimeFsr4FailureLatched = true;
	logger::warn("[FidelityFX] Runtime FSR4 path failed; selecting the host fallback without creating another DX12 runtime provider.");
}

void FidelityFX::QuarantineRuntimeUpscalerForSession(const char* a_reason)
{
	runtimeUpscalerFailureLatched = true;
	if (runtimeUpscalerSessionQuarantined)
		return;

	runtimeUpscalerSessionQuarantined = true;
	runtimeUpscalerQuarantineRetirement = LifecycleResult::Pending;
	runtimeUpscalerQuarantineFrameValid = globals::state != nullptr;
	runtimeUpscalerQuarantineFrame = globals::state ? globals::state->frameCount : 0;
	logger::warn(
		"[FidelityFX] Quarantined the DX12 runtime upscaler for this game session after {}; using {}. Restart the game to retry the runtime provider.",
		a_reason ? a_reason : "a provider failure",
		GetHostFsrSdkLabel());
}

FidelityFX::RuntimeUpscalerFramePath FidelityFX::GetRuntimeUpscalerProviderFramePath(uint32_t a_requestedVersion) const
{
	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, FFX_UPSCALER_VERSION))
		return RuntimeUpscalerFramePath::kRuntimeFsr4;

	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, Fsr3Version))
		return RuntimeUpscalerFramePath::kRuntimeFsr31;

	return a_requestedVersion == FFX_UPSCALER_VERSION ? RuntimeUpscalerFramePath::kRuntimeFsr4 : RuntimeUpscalerFramePath::kRuntimeFsr31;
}

void FidelityFX::RecordRuntimeUpscalerFramePath(RuntimeUpscalerFramePath a_path)
{
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (!runtimeUpscalerLastFramePathValid || runtimeUpscalerLastFrameIndex != frame) {
		runtimeUpscalerLastFramePathValid = true;
		runtimeUpscalerLastFrameIndex = frame;
		runtimeUpscalerLastFramePath = a_path;
		return;
	}

	if (static_cast<uint8_t>(a_path) > static_cast<uint8_t>(runtimeUpscalerLastFramePath))
		runtimeUpscalerLastFramePath = a_path;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
void FidelityFX::RecordDevBenchSuccessfulDispatch(RuntimeUpscalerFramePath a_path)
{
	uint64_t serial = ++devBenchSuccessfulDispatchSerial;
	if (serial == 0)
		serial = ++devBenchSuccessfulDispatchSerial;
	devBenchSuccessfulDispatch = {
		true,
		globals::state ? std::max(globals::state->frameCount, 1u) : 0u,
		a_path,
		serial,
	};
}
#endif

void FidelityFX::SetupFrameGeneration()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain.swapChainDesc.Width, swapChain.swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain.swapChainDesc.Format);

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, backendDesc) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to create frame generation context!");
}

void FidelityFX::Present(bool a_useFrameGeneration)
{
	auto& upscaling = globals::features::upscaling;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};

		configParameters.frameGenerationCallbackUserContext = &frameGenContext;
	} else {
		configParameters.frameGenerationEnabled = false;
		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;
	}

	configParameters.HUDLessColor = FfxApiResource({});
	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain.swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.flags = 0;
	configParameters.allowAsyncWorkloads = true;

	auto state = globals::state;
	auto renderSize = state->screenSize * upscaling.resolutionScale;

	configParameters.generationRect.left = 0;
	configParameters.generationRect.top = 0;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to configure frame generation!");

	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	uiConfig.uiResource = ffxApiGetResourceDX12(swapChain.uiBufferWrapped->resource.get());
	uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA | FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;

	if (ffx::Configure(swapChainContext, uiConfig) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to configure UI composition!");

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = swapChain.commandLists[swapChain.frameIndex].get();
		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint32_t>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint32_t>(renderSize.y);
		dispatchParameters.jitterOffset.x = -upscaling.jitter.x;
		dispatchParameters.jitterOffset.y = -upscaling.jitter.y;
		dispatchParameters.frameTimeDelta = RE::GetSecondsSinceLastFrame() * 1000.f;
		dispatchParameters.cameraFar = *globals::game::cameraFar;
		dispatchParameters.cameraNear = *globals::game::cameraNear;
		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.frameID = frameID;
		dispatchParameters.depth = ffxApiGetResourceDX12(swapChain.depthBufferShared12->resource.get());
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(swapChain.motionVectorBufferShared12->resource.get());

		ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraConfig{};

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

		cameraConfig.cameraRight[0] = viewMatrix._11;
		cameraConfig.cameraRight[1] = viewMatrix._12;
		cameraConfig.cameraRight[2] = viewMatrix._13;

		cameraConfig.cameraUp[0] = viewMatrix._21;
		cameraConfig.cameraUp[1] = viewMatrix._22;
		cameraConfig.cameraUp[2] = viewMatrix._23;

		cameraConfig.cameraForward[0] = viewMatrix._31;
		cameraConfig.cameraForward[1] = viewMatrix._32;
		cameraConfig.cameraForward[2] = viewMatrix._33;

		cameraConfig.cameraPosition[0] = globals::game::frameBufferCached.GetCameraPosAdjust().x;
		cameraConfig.cameraPosition[1] = globals::game::frameBufferCached.GetCameraPosAdjust().y;
		cameraConfig.cameraPosition[2] = globals::game::frameBufferCached.GetCameraPosAdjust().z;

		if (ffx::Dispatch(frameGenContext, dispatchParameters, cameraConfig) != ffx::ReturnCode::Ok)
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
	}

	frameID++;
	isFrameGenActive = a_useFrameGeneration;
}

FidelityFX::LifecycleResult FidelityFX::CreateFSRResources()
{
	if (fsrHostStateQuarantined)
		return GetQuarantinedHostFSRResult("host FSR resource creation");
	if (std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		QuarantineHostFSRState("indeterminate FSR3 context tracking before creation");
		return GetQuarantinedHostFSRResult("host FSR resource creation");
	}

	auto state = globals::state;
	if (!state) {
		logger::critical("[FidelityFX] Missing global state when creating FSR resources.");
		return LifecycleResult::Failed;
	}

	const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();
	const uint32_t numContexts = splitPerEyeContexts ? 2u : 1u;
	float2 screenSize{};
	float2 renderSize{};
	GetRuntimeUpscaleSizes(screenSize, renderSize);

	const uint32_t displayWidth = static_cast<uint32_t>(splitPerEyeContexts ? screenSize.x / 2 : screenSize.x);
	const uint32_t displayHeight = static_cast<uint32_t>(screenSize.y);
	const uint32_t requestedRenderWidth = static_cast<uint32_t>(splitPerEyeContexts ? renderSize.x / 2 : renderSize.x);
	const uint32_t requestedRenderHeight = static_cast<uint32_t>(renderSize.y);
	const uint32_t renderWidth = splitPerEyeContexts ? displayWidth : requestedRenderWidth;
	const uint32_t renderHeight = splitPerEyeContexts ? displayHeight : requestedRenderHeight;
	if (!displayWidth || !displayHeight || !renderWidth || !renderHeight) {
		logger::critical("[FidelityFX] Cannot create FSR resources with zero-sized render or display bounds.");
		return LifecycleResult::Failed;
	}
	if (!globals::d3d::device) {
		logger::error("[FidelityFX] Cannot create FSR resources without a D3D11 device.");
		return LifecycleResult::Failed;
	}
	const auto deviceResult = RecordFSRDeviceStatus();
	if (deviceResult == LifecycleResult::DeviceLost) {
		if (fsrScratchBuffer || fsrContextCount != 0 ||
			std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; })) {
			QuarantineHostFSRState("D3D11 device removal before FSR resource creation");
		}
		logger::error(
			"[FidelityFX] Refusing FSR resource creation after D3D11 device removal. reason=0x{:08X}",
			static_cast<std::uint32_t>(fsrLastDeviceRemovedReason));
		return LifecycleResult::DeviceLost;
	}

	if (fsrScratchBuffer || fsrContextCount != 0 ||
		std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		if (AreFSRResourcesCompatible(renderWidth, renderHeight, displayWidth, displayHeight, numContexts))
			return LifecycleResult::Ready;

		if (HasFSRResources()) {
			const auto teardownResult = PollFSRResourceTeardownReady("incompatible host FSR replacement");
			if (teardownResult != LifecycleResult::Ready)
				return teardownResult;

			const auto destroyResult = DestroyFSRResources(false);
			if (destroyResult != LifecycleResult::Ready)
				return destroyResult;
		} else {
			logger::error("[FidelityFX] Cannot create FSR resources while malformed or partially destroyed host resources remain tracked.");
			QuarantineHostFSRState("malformed host FSR ownership before replacement");
			return GetQuarantinedHostFSRResult("host FSR resource replacement");
		}
	}

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto runtimeIdleResult = PollRuntimeUpscalerTeardownReady("host FSR resource creation");
		if (runtimeIdleResult != LifecycleResult::Ready)
			return runtimeIdleResult;
		const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
		if (contextDestroyResult != LifecycleResult::Ready)
			return contextDestroyResult;
		const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
		if (resourceDestroyResult != LifecycleResult::Ready)
			return resourceDestroyResult;

		ResetRuntimeUpscalerTracking(true);
	}

	auto fsrDevice = ffxGetDeviceDX11_Fsr31(globals::d3d::device);

	const size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		return LifecycleResult::Failed;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface{};
	bool interfaceCrashed = false;
	const FfxErrorCode interfaceResult = GetHostFsr3InterfaceProtected(
		&fsrInterface,
		fsrDevice,
		fsrScratchBuffer,
		scratchBufferSize,
		numContexts,
		interfaceCrashed);
	const auto interfaceDisposition = FSRHostLifecyclePolicy::ClassifyCallDisposition(
		interfaceCrashed,
		interfaceResult == FFX_OK);
	if (interfaceDisposition == FSRHostLifecyclePolicy::CallDisposition::Faulted) {
		logger::error("[FidelityFX] FSR3 backend interface initialization faulted.");
		QuarantineHostFSRState("an FSR3 backend interface initialization fault");
		return GetQuarantinedHostFSRResult("FSR3 backend interface initialization");
	}
	if (FSRHostLifecyclePolicy::CanReleaseFailedInterfaceScratch(interfaceDisposition)) {
		logger::critical("[FidelityFX] FSR3 backend interface initialization returned an error; releasing its unowned scratch state.");
		const auto failureResult = ResolveFSRLifecycleFailure("FSR3 backend interface initialization");
		const auto releaseResult = ReleaseHostFSRResources();
		if (releaseResult != LifecycleResult::Ready)
			return releaseResult;
		return failureResult;
	}

	const bool emitDiagLogs = ShouldEmitFidelityFXDiagLogs();
	if (emitDiagLogs) {
		const bool amdAdapter = IsAmdAdapterDetected();
		const bool runtimeUpscalerPresent = IsRuntimeUpscalerPresent();
		const bool runtimeFsr4AutoEligible = IsRuntimeFsr4AutoEligible();
		logger::debug(
			"[FidelityFX][Diag] CreateFSRResources plan amd={} fsr4Eligible={} runtimeUpscaler={} runtimeFsr4={} contexts={} display={}x{} requestedRender={}x{} maxRender={}x{} splitPerEye={}",
			amdAdapter ? "yes" : "no",
			runtimeFsr4AutoEligible ? "yes" : "no",
			(runtimeUpscalerPresent && amdAdapter) ? "yes" : "no",
			(runtimeUpscalerPresent && runtimeFsr4AutoEligible) ? "yes" : "no",
			numContexts,
			displayWidth,
			displayHeight,
			requestedRenderWidth,
			requestedRenderHeight,
			renderWidth,
			renderHeight,
			splitPerEyeContexts ? "yes" : "no");
	}

	for (uint32_t i = 0; i < numContexts; ++i) {
		FfxFsr3ContextDescription contextDescription{};
		contextDescription.maxRenderSize.width = renderWidth;
		contextDescription.maxRenderSize.height = renderHeight;
		contextDescription.maxUpscaleSize.width = displayWidth;
		contextDescription.maxUpscaleSize.height = displayHeight;
		contextDescription.displaySize.width = displayWidth;
		contextDescription.displaySize.height = displayHeight;
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backendInterfaceUpscaling = fsrInterface;

		fsrContext[i] = {};
		fsrContextValid[i] = false;
		fsrContextIndeterminate[i] = false;
		bool createCrashed = false;
		const FfxErrorCode createResult =
			CreateHostFsr3ContextProtected(&fsrContext[i], &contextDescription, createCrashed);
		const auto createDisposition = FSRHostLifecyclePolicy::ClassifyCallDisposition(
			createCrashed,
			createResult == FFX_OK);
		if (createDisposition == FSRHostLifecyclePolicy::CallDisposition::Faulted) {
			logger::error("[FidelityFX] FSR3 context creation for eye {} faulted.", i);
			QuarantineHostFSRContext(i, "an FSR3 context creation fault");
			return GetQuarantinedHostFSRResult("FSR3 context creation");
		}
		if (FSRHostLifecyclePolicy::RequiresOwnershipQuarantine(createDisposition)) {
			logger::critical(
				"[FidelityFX] FSR3 context creation for eye {} returned an error; quarantining potentially partial SDK ownership.",
				i);
			const auto failureResult = ResolveFSRLifecycleFailure("FSR3 context creation");
			QuarantineHostFSRContext(i, "an FSR3 context creation error");
			return failureResult;
		}
		fsrContextValid[i] = true;
	}

	fsrContextCount = numContexts;
	fsrContextMaxRenderWidth = renderWidth;
	fsrContextMaxRenderHeight = renderHeight;
	fsrContextDisplayWidth = displayWidth;
	fsrContextDisplayHeight = displayHeight;
	if (emitDiagLogs) {
		logger::debug("[FidelityFX] Created {} FSR3 contexts (Display: {}x{}, MaxRender: {}x{}, RequestedRender: {}x{}, SplitPerEye={})",
			numContexts, displayWidth, displayHeight, renderWidth, renderHeight, requestedRenderWidth, requestedRenderHeight, splitPerEyeContexts);
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyRuntimeUpscalerContexts(bool a_waitForIdle)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;

	if (std::ranges::any_of(
			runtimeUpscalerContextIndeterminate,
			[](bool a_indeterminate) { return a_indeterminate; })) {
		// A failed destroy does not prove whether the provider still owns the
		// handle. Never retry that call or release resources it may reference.
		if (IsTerminalRuntimeQuarantineResult(runtimeUpscalerQuarantineRetirement)) {
			return runtimeUpscalerQuarantineRetirement;
		}
		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(
			"indeterminate runtime upscaler context destruction");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(failureResult);
		return runtimeUpscalerQuarantineRetirement;
	}
	if (a_waitForIdle) {
		const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler context destruction");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}

	for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i) {
		if (!runtimeUpscalerContexts[i])
			continue;

		const auto retainedContext = runtimeUpscalerContexts[i];
		bool destroyCrashed = false;
		const auto destroyResult = DestroyRuntimeUpscalerContextProtected(
			&runtimeUpscalerContexts[i],
			destroyCrashed);
		if (destroyCrashed || destroyResult != FFX_API_RETURN_OK) {
			runtimeUpscalerContexts[i] = retainedContext;
			runtimeUpscalerContextIndeterminate[i] = true;
			logger::critical(
				"[FidelityFX] Runtime upscaler context {} destruction {} without proving release; retaining its handle and referenced resources for this session.",
				i,
				destroyCrashed ? "faulted" : "failed");
			QuarantineRuntimeUpscalerForSession("an indeterminate runtime context destruction");
			const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(
				"runtime upscaler context destruction");
			runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(failureResult);
			return failureResult;
		}

		runtimeUpscalerContexts[i] = nullptr;
		runtimeUpscalerContextIndeterminate[i] = false;
	}

	runtimeUpscalerContextCount = 0;
	runtimeUpscalerMaxRenderWidth = 0;
	runtimeUpscalerMaxRenderHeight = 0;
	runtimeUpscalerMaxDisplayWidth = 0;
	runtimeUpscalerMaxDisplayHeight = 0;
	runtimeUpscalerRequestedVersion = 0;
	return LifecycleResult::Ready;
}

void FidelityFX::ResetRuntimeCommandContexts()
{
	for (auto& commandContext : runtimeCommandContexts) {
		commandContext.commandList = nullptr;
		commandContext.commandAllocator = nullptr;
		commandContext.fenceValue = 0;
	}
	runtimeCommandContextCursor = 0;
}

void FidelityFX::ReleaseIdleRuntimeUpscalerInterop()
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return;

	ResetRuntimeCommandContexts();
	pendingRuntimeTeardownD3D11FenceValue = 0;
	pendingRuntimeTeardownD3D12FenceValue = 0;
	runtimeD3D11Fence = nullptr;
	runtimeD3D12Fence = nullptr;
	runtimeFenceValue = 1;
	runtimeUpscalerQuarantineFrameValid = false;
	runtimeUpscalerQuarantineFrame = 0;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeCommandContexts()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!swapChain.d3d12Device)
		return LifecycleResult::Pending;

	try {
		for (auto& commandContext : runtimeCommandContexts) {
			if (commandContext.commandAllocator && commandContext.commandList)
				continue;

			winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
			winrt::com_ptr<ID3D12GraphicsCommandList4> commandList;
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(commandAllocator.put())));
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				commandAllocator.get(),
				nullptr,
				IID_PPV_ARGS(commandList.put())));
			DX::ThrowIfFailed(commandList->Close());

			commandContext.commandAllocator = std::move(commandAllocator);
			commandContext.commandList = std::move(commandList);
			commandContext.fenceValue = 0;
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to create runtime upscaler command contexts: {}", e.what());
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context creation");
	} catch (...) {
		logger::error("[FidelityFX] Failed to create runtime upscaler command contexts.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context creation");
	}

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::AcquireRuntimeCommandContext(RuntimeCommandContext*& a_commandContext, uint32_t a_requiredFreeContexts)
{
	a_commandContext = nullptr;
	if (!runtimeD3D12Fence)
		return LifecycleResult::Pending;
	const auto ensureResult = EnsureRuntimeCommandContexts();
	if (ensureResult != LifecycleResult::Ready)
		return ensureResult;

	const uint64_t completedValue = runtimeD3D12Fence->GetCompletedValue();
	if (completedValue == std::numeric_limits<uint64_t>::max()) {
		logger::error("[FidelityFX] Runtime upscaler fence reported device removal while acquiring a command context.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context acquisition");
	}

	const uint32_t commandContextCount = static_cast<uint32_t>(runtimeCommandContexts.size());
	std::array<uint32_t, kRuntimeCommandContextCount> availableIndices{};
	uint32_t availableCount = 0;
	for (uint32_t i = 0; i < commandContextCount; ++i) {
		const uint32_t index = (runtimeCommandContextCursor + i) % commandContextCount;
		auto& commandContext = runtimeCommandContexts[index];
		if (!commandContext.commandAllocator || !commandContext.commandList)
			continue;
		if (commandContext.fenceValue != 0 && completedValue < commandContext.fenceValue)
			continue;
		availableIndices[availableCount++] = index;
	}

	const uint32_t requiredFreeContexts = std::clamp(a_requiredFreeContexts, 1u, commandContextCount);
	if (availableCount < requiredFreeContexts) {
		static bool loggedCommandPoolExhausted = false;
		if (!loggedCommandPoolExhausted) {
			logger::warn("[FidelityFX] Deferring runtime upscaler dispatch because the command context pool is still in flight.");
			loggedCommandPoolExhausted = true;
		}
		return LifecycleResult::Pending;
	}

	const uint32_t selectedIndex = availableIndices[0];
	auto& commandContext = runtimeCommandContexts[selectedIndex];
	commandContext.fenceValue = 0;
	runtimeCommandContextCursor = (selectedIndex + 1) % commandContextCount;
	a_commandContext = &commandContext;
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyRuntimeUpscalerResources(bool a_waitForIdle)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;

	if (std::ranges::any_of(
			runtimeUpscalerContextIndeterminate,
			[](bool a_indeterminate) { return a_indeterminate; })) {
		return ResolveRuntimeUpscalerLifecycleFailure(
			"runtime upscaler shared-resource destruction with indeterminate context ownership");
	}
	if (a_waitForIdle) {
		const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler shared-resource destruction");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}

	ResetOwnedResourceArray(runtimeColorShared);
	ResetOwnedResourceArray(runtimeDepthShared);
	ResetOwnedResourceArray(runtimeMotionShared);
	ResetOwnedResourceArray(runtimeReactiveShared);
	ResetOwnedResourceArray(runtimeTransparencyShared);
	ResetOwnedResourceArray(runtimeOutputShared);

	runtimeColorSharedDesc = {};
	runtimeDepthSharedDesc = {};
	runtimeMotionSharedDesc = {};
	runtimeReactiveSharedDesc = {};
	runtimeTransparencySharedDesc = {};
	runtimeOutputSharedDesc = {};
	return LifecycleResult::Ready;
}

void FidelityFX::ResetFSRIdleFence()
{
	ReleaseD3D11IdleFence(pendingFSRResourceFreeIdleFence);
}

bool FidelityFX::HasFSRResources() const
{
	if (fsrHostStateQuarantined ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }) ||
		fsrContextCount == 0 || fsrContextCount > std::size(fsrContext) || !fsrScratchBuffer) {
		return false;
	}

	for (uint32_t i = 0; i < fsrContextCount; ++i) {
		if (!fsrContextValid[i])
			return false;
	}
	for (uint32_t i = fsrContextCount; i < std::size(fsrContextValid); ++i) {
		if (fsrContextValid[i])
			return false;
	}
	return true;
}

bool FidelityFX::AreFSRResourcesCompatible(uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_contextCount) const
{
	return HasFSRResources() &&
	       fsrContextCount == a_contextCount &&
	       a_renderWidth != 0 &&
	       a_renderHeight != 0 &&
	       a_displayWidth != 0 &&
	       a_displayHeight != 0 &&
	       a_renderWidth <= fsrContextMaxRenderWidth &&
	       a_renderHeight <= fsrContextMaxRenderHeight &&
	       a_displayWidth == fsrContextDisplayWidth &&
	       a_displayHeight == fsrContextDisplayHeight;
}

bool FidelityFX::HasRuntimeUpscalerResources() const
{
	bool hasRuntimeResources =
		runtimeUpscalerContextCount != 0 ||
		pendingRuntimeTeardownD3D11FenceValue != 0 ||
		pendingRuntimeTeardownD3D12FenceValue != 0 ||
		runtimeD3D11Fence.get() != nullptr ||
		runtimeD3D12Fence.get() != nullptr;
	for (const bool indeterminate : runtimeUpscalerContextIndeterminate)
		hasRuntimeResources = hasRuntimeResources || indeterminate;
	for (const auto& context : runtimeUpscalerContexts)
		hasRuntimeResources = hasRuntimeResources || context != nullptr;
	for (const auto& commandContext : runtimeCommandContexts) {
		hasRuntimeResources = hasRuntimeResources ||
			commandContext.commandAllocator.get() != nullptr ||
			commandContext.commandList.get() != nullptr ||
			commandContext.fenceValue != 0;
	}
	for (const auto& resource : runtimeColorShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	for (const auto& resource : runtimeDepthShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	for (const auto& resource : runtimeMotionShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	for (const auto& resource : runtimeReactiveShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	for (const auto& resource : runtimeTransparencyShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	for (const auto& resource : runtimeOutputShared)
		hasRuntimeResources = hasRuntimeResources || resource != nullptr;
	return hasRuntimeResources;
}

bool FidelityFX::AreRuntimeUpscalerContextsCompatible(
	uint32_t a_fullRenderWidth,
	uint32_t a_fullRenderHeight,
	uint32_t a_fullDisplayWidth,
	uint32_t a_fullDisplayHeight,
	uint32_t a_contextCount,
	uint32_t a_requestedVersion) const
{
	if (!a_fullRenderWidth || !a_fullRenderHeight || !a_fullDisplayWidth || !a_fullDisplayHeight ||
		a_contextCount == 0 || a_contextCount > std::size(runtimeUpscalerContexts)) {
		return false;
	}
	if (std::ranges::any_of(
			runtimeUpscalerContextIndeterminate,
			[](bool a_indeterminate) { return a_indeterminate; })) {
		return false;
	}

	for (uint32_t i = 0; i < a_contextCount; ++i) {
		if (!runtimeUpscalerContexts[i])
			return false;
	}

	return runtimeUpscalerContextCount == a_contextCount &&
	       runtimeUpscalerMaxRenderWidth == a_fullRenderWidth &&
	       runtimeUpscalerMaxRenderHeight == a_fullRenderHeight &&
	       runtimeUpscalerMaxDisplayWidth == a_fullDisplayWidth &&
	       runtimeUpscalerMaxDisplayHeight == a_fullDisplayHeight &&
	       runtimeUpscalerRequestedVersion == a_requestedVersion;
}

FidelityFX::LifecycleResult FidelityFX::PollRuntimeUpscalerTeardownIdle(const char* a_reason)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;

	const char* reason = a_reason && *a_reason ? a_reason : "runtime upscaler teardown";
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!HasRuntimeUpscalerResources()) {
		pendingRuntimeTeardownD3D11FenceValue = 0;
		pendingRuntimeTeardownD3D12FenceValue = 0;
		return LifecycleResult::Ready;
	}

	if (!swapChain.d3d11Context || !swapChain.commandQueue || !runtimeD3D11Fence || !runtimeD3D12Fence) {
		logger::debug("[FidelityFX] Deferring runtime idle proof before {} because the interop fence topology is incomplete.", reason);
		return LifecycleResult::Pending;
	}

	try {
		if (pendingRuntimeTeardownD3D12FenceValue == 0) {
			if (pendingRuntimeTeardownD3D11FenceValue == 0) {
				pendingRuntimeTeardownD3D11FenceValue = runtimeFenceValue++;
				DX::ThrowIfFailed(swapChain.d3d11Context->Signal(runtimeD3D11Fence.get(), pendingRuntimeTeardownD3D11FenceValue));
				swapChain.d3d11Context->Flush();
			}

			const uint64_t d3d11CompletedValue = runtimeD3D12Fence->GetCompletedValue();
			if (d3d11CompletedValue == std::numeric_limits<uint64_t>::max()) {
				logger::warn("[FidelityFX] Runtime upscaler fence reported device removal before {}.", reason);
				return ResolveRuntimeUpscalerLifecycleFailure(reason);
			}
			if (d3d11CompletedValue < pendingRuntimeTeardownD3D11FenceValue)
				return LifecycleResult::Pending;
			pendingRuntimeTeardownD3D11FenceValue = 0;

			pendingRuntimeTeardownD3D12FenceValue = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), pendingRuntimeTeardownD3D12FenceValue));
		}

		const uint64_t completedValue = runtimeD3D12Fence->GetCompletedValue();
		if (completedValue == std::numeric_limits<uint64_t>::max()) {
			logger::warn("[FidelityFX] Runtime upscaler fence reported device removal before {}.", reason);
			return ResolveRuntimeUpscalerLifecycleFailure(reason);
		}
		if (completedValue < pendingRuntimeTeardownD3D12FenceValue)
			return LifecycleResult::Pending;
		pendingRuntimeTeardownD3D12FenceValue = 0;

		for (auto& commandContext : runtimeCommandContexts) {
			if (commandContext.fenceValue != 0 && completedValue >= commandContext.fenceValue)
				commandContext.fenceValue = 0;
		}
	} catch (const std::exception& e) {
		logger::warn("[FidelityFX] Failed to poll runtime upscaler idle before {}: {}", reason, e.what());
		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(reason);
		if (failureResult != LifecycleResult::RuntimeDeviceLost) {
			pendingRuntimeTeardownD3D11FenceValue = 0;
			pendingRuntimeTeardownD3D12FenceValue = 0;
		}
		return failureResult;
	} catch (...) {
		logger::warn("[FidelityFX] Failed to poll runtime upscaler idle before {}.", reason);
		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(reason);
		if (failureResult != LifecycleResult::RuntimeDeviceLost) {
			pendingRuntimeTeardownD3D11FenceValue = 0;
			pendingRuntimeTeardownD3D12FenceValue = 0;
		}
		return failureResult;
	}

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::PollRuntimeUpscalerTeardownReady(const char* a_reason)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;

	if (std::ranges::any_of(
			runtimeUpscalerContextIndeterminate,
			[](bool a_indeterminate) { return a_indeterminate; })) {
		if (IsTerminalRuntimeQuarantineResult(runtimeUpscalerQuarantineRetirement)) {
			return runtimeUpscalerQuarantineRetirement;
		}

		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(
			a_reason && *a_reason ? a_reason : "indeterminate runtime upscaler teardown");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(failureResult);
		return runtimeUpscalerQuarantineRetirement;
	}

	if (!HasRuntimeUpscalerResources()) {
		pendingRuntimeTeardownD3D11FenceValue = 0;
		pendingRuntimeTeardownD3D12FenceValue = 0;
		return LifecycleResult::Ready;
	}

	return PollRuntimeUpscalerTeardownIdle(a_reason);
}

bool FidelityFX::HasFSRResourcesPendingTeardown() const
{
	return fsrHostStateQuarantined ||
	       fsrContextCount != 0 ||
	       fsrScratchBuffer ||
	       std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
	       std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }) ||
	       (!IsRuntimeUpscalerOwnershipDetached() && HasRuntimeUpscalerResources());
}

FidelityFX::LifecycleResult FidelityFX::PollFSRResourceTeardownReady(const char* a_reason)
{
	const char* reason = a_reason && *a_reason ? a_reason : "FSR resource teardown";
	if (fsrHostStateQuarantined)
		return RetireRuntimeUpscalerWhileHostFSRQuarantined(reason);

	if (!HasFSRResourcesPendingTeardown()) {
		ResetFSRIdleFence();
		return LifecycleResult::Ready;
	}
	const bool hasHostResources = fsrContextCount != 0 || fsrScratchBuffer ||
		std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; });
	if (hasHostResources) {
		auto result = BeginOrPollD3D11IdleFence(globals::d3d::context, pendingFSRResourceFreeIdleFence, reason);
		if (result == LifecycleResult::Failed)
			result = ResolveFSRLifecycleFailure(reason);
		if (result != LifecycleResult::Ready) {
			static bool loggedFSRResourceFreePending = false;
			if (!loggedFSRResourceFreePending) {
				if (result == LifecycleResult::Pending) {
					logger::warn("[FidelityFX] Deferring FSR resource teardown because the D3D11 queue did not become idle.");
				} else {
					logger::warn("[FidelityFX] FSR resource teardown cannot continue because D3D11 idle synchronization failed.");
				}
				loggedFSRResourceFreePending = true;
			}
			return result;
		}
	} else {
		ReleaseD3D11IdleFence(pendingFSRResourceFreeIdleFence);
	}

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto runtimeResult = PollRuntimeUpscalerTeardownReady(reason);
		if (runtimeResult != LifecycleResult::Ready) {
			static bool loggedRuntimeUpscalerTeardownPending = false;
			if (!loggedRuntimeUpscalerTeardownPending) {
				if (runtimeResult == LifecycleResult::Pending)
					logger::warn("[FidelityFX] Deferring FSR resource teardown because runtime upscaler GPU work is still in flight.");
				else
					logger::warn("[FidelityFX] FSR resource teardown cannot continue because runtime idle synchronization failed.");
				loggedRuntimeUpscalerTeardownPending = true;
			}
			return runtimeResult;
		}
	}

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ResetRuntimeUpscalerResources(bool a_invalidateProviderCache)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;

	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler reset");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;

	const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
	if (contextDestroyResult != LifecycleResult::Ready)
		return contextDestroyResult;
	const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
	if (resourceDestroyResult != LifecycleResult::Ready)
		return resourceDestroyResult;
	ResetRuntimeUpscalerTracking(a_invalidateProviderCache);
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::RetireQuarantinedRuntimeUpscalerResources()
{
	if (!runtimeUpscalerSessionQuarantined)
		return LifecycleResult::Ready;
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;
	if (!HasRuntimeUpscalerResources()) {
		ReleaseIdleRuntimeUpscalerInterop();
		runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
		return LifecycleResult::Ready;
	}
	if (runtimeUpscalerQuarantineRetirement != LifecycleResult::Pending)
		return runtimeUpscalerQuarantineRetirement;

	if (runtimeUpscalerQuarantineFrameValid && globals::state &&
		globals::state->frameCount == runtimeUpscalerQuarantineFrame) {
		return LifecycleResult::Pending;
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("quarantined runtime upscaler retirement");
	if (idleResult != LifecycleResult::Ready) {
		if (idleResult != LifecycleResult::Pending)
			runtimeUpscalerQuarantineRetirement = idleResult;
		return idleResult;
	}

	const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
	if (contextDestroyResult != LifecycleResult::Ready) {
		runtimeUpscalerQuarantineRetirement = contextDestroyResult;
		return contextDestroyResult;
	}
	const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
	if (resourceDestroyResult != LifecycleResult::Ready) {
		runtimeUpscalerQuarantineRetirement = resourceDestroyResult;
		return resourceDestroyResult;
	}

	ReleaseIdleRuntimeUpscalerInterop();
	runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyFSRResources(bool a_waitForIdle)
{
	const bool emitDiagLogs = ShouldEmitFidelityFXDiagLogs();
	if (emitDiagLogs) {
		logger::debug(
			"[FidelityFX][Diag] DestroyFSRResources waitForIdle={} contexts={} maxRender={}x{} display={}x{} scratch={} pendingTeardown={} runtimeContexts={} runtimeMaxRender={}x{} runtimeMaxDisplay={}x{}",
			a_waitForIdle ? "yes" : "no",
			fsrContextCount,
			fsrContextMaxRenderWidth,
			fsrContextMaxRenderHeight,
			fsrContextDisplayWidth,
			fsrContextDisplayHeight,
			fsrScratchBuffer ? "yes" : "no",
			HasFSRResourcesPendingTeardown() ? "yes" : "no",
			runtimeUpscalerContextCount,
			runtimeUpscalerMaxRenderWidth,
			runtimeUpscalerMaxRenderHeight,
			runtimeUpscalerMaxDisplayWidth,
			runtimeUpscalerMaxDisplayHeight);
	}
	if (fsrHostStateQuarantined)
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	if (std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		QuarantineHostFSRState("indeterminate FSR3 context tracking during teardown");
		return GetQuarantinedHostFSRResult("FSR resource teardown");
	}

	if (a_waitForIdle) {
		const auto idleResult = PollFSRResourceTeardownReady("FSR resource teardown");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}

	ResetFSRIdleFence();

	if (fsrContextCount > std::size(fsrContext)) {
		logger::critical("[FidelityFX] Refusing FSR teardown because the tracked host context count is invalid.");
		QuarantineHostFSRState("invalid tracked host FSR context count");
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	}
	if (!fsrScratchBuffer && std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; })) {
		logger::critical("[FidelityFX] Refusing FSR teardown because live host contexts have no retained backend scratch buffer.");
		for (uint32_t i = 0; i < std::size(fsrContextValid); ++i) {
			if (fsrContextValid[i])
				fsrContextIndeterminate[i] = true;
		}
		QuarantineHostFSRState("live host FSR contexts without backend scratch ownership");
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	}

	const auto hostDestroyResult = DestroyTrackedHostFSRContexts("FSR resource teardown");
	if (hostDestroyResult != LifecycleResult::Ready)
		return hostDestroyResult;
	const auto hostReleaseResult = ReleaseHostFSRResources();
	if (hostReleaseResult != LifecycleResult::Ready)
		return hostReleaseResult;

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
		if (contextDestroyResult != LifecycleResult::Ready)
			return contextDestroyResult;
		const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
		if (resourceDestroyResult != LifecycleResult::Ready)
			return resourceDestroyResult;

		ReleaseIdleRuntimeUpscalerInterop();
		ResetRuntimeUpscalerTracking(true);
	}
	fsrDispatchCrashLogged = false;
	return LifecycleResult::Ready;
}

bool FidelityFX::IsAmdAdapterDetected() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (TryGetCurrentAdapterDesc(adapterDesc))
		return adapterDesc.VendorId == kAmdVendorId;

	return false;
}

bool FidelityFX::IsNvidiaAdapterDetected() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (TryGetCurrentAdapterDesc(adapterDesc))
		return adapterDesc.VendorId == kNvidiaVendorId;

	return false;
}

bool FidelityFX::IsRuntimeUpscalerPresent() const
{
	if (!featureRuntimeUpscaler || !runtimeUpscalerModule || !module)
		return false;
	if (!ffxModule.CreateContext || !ffxModule.DestroyContext || !ffxModule.Dispatch || !ffxModule.Query)
		return false;

	return true;
}

FidelityFX::Fsr4AdapterSupport FidelityFX::GetFsr4AdapterSupport(const DXGI_ADAPTER_DESC& a_adapterDesc)
{
	return ClassifyFsr4AdapterSupport(a_adapterDesc);
}

bool FidelityFX::IsRuntimeFsr4AutoEligible() const
{
	return GetFsr4AdapterSupport() != Fsr4AdapterSupport::Unsupported;
}

FidelityFX::Fsr4AdapterSupport FidelityFX::GetFsr4AdapterSupport() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (!TryGetCurrentAdapterDesc(adapterDesc))
		return Fsr4AdapterSupport::Unsupported;

	return GetFsr4AdapterSupport(adapterDesc);
}

bool FidelityFX::IsRuntimeFsr4Available() const
{
	if (!IsRuntimeUpscalerPresent())
		return false;

	return IsRuntimeFsr4AutoEligible();
}

bool FidelityFX::ShouldUseRuntimeUpscalerForFSR() const
{
	return IsRuntimeUpscalerPresent() && IsAmdAdapterDetected();
}

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
	resource.state = state;
	resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
	if (ffxResName) {
		wcscpy_s(resource.name, ffxResName);
	}
#endif

	return resource;
}

bool FidelityFX::CanUseRuntimeUpscalerPath()
{
	if (runtimeUpscalerSessionQuarantined || runtimeUpscalerFailureLatched)
		return false;
	return true;
}

bool FidelityFX::ShouldRequestRuntimeFsr4() const
{
	return globals::features::upscaling.GetRuntimeFSR4Enabled() &&
	       !runtimeFsr4FailureLatched &&
	       IsRuntimeFsr4Available();
}

uint32_t FidelityFX::GetPreferredRuntimeUpscalerVersion() const
{
	return ShouldRequestRuntimeFsr4() ? FFX_UPSCALER_VERSION : Fsr3Version;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerInterop()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	if (!globals::d3d::device || !globals::d3d::context)
		return LifecycleResult::Pending;

	try {
		if (!swapChain.d3d11Device)
			swapChain.SetD3D11Device(globals::d3d::device);
		if (!swapChain.d3d11Context)
			swapChain.SetD3D11DeviceContext(globals::d3d::context);

		if (!swapChain.d3d12Device) {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			DX::ThrowIfFailed(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));

			winrt::com_ptr<IDXGIAdapter> adapter;
			DX::ThrowIfFailed(dxgiDevice->GetAdapter(adapter.put()));
			swapChain.CreateD3D12Device(adapter.get());
		}

		if (!runtimeD3D12Fence || !runtimeD3D11Fence) {
			winrt::handle sharedFenceHandle;
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&runtimeD3D12Fence)));
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateSharedHandle(runtimeD3D12Fence.get(), nullptr, GENERIC_ALL, nullptr, sharedFenceHandle.put()));
			DX::ThrowIfFailed(swapChain.d3d11Device->OpenSharedFence(sharedFenceHandle.get(), IID_PPV_ARGS(&runtimeD3D11Fence)));
			runtimeFenceValue = 1;
			for (auto& commandContext : runtimeCommandContexts)
				commandContext.fenceValue = 0;
			runtimeCommandContextCursor = 0;
		}

		const auto commandContextResult = EnsureRuntimeCommandContexts();
		if (commandContextResult != LifecycleResult::Ready)
			return commandContextResult;
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to initialize DX11->DX12 runtime interop: {}", e.what());
		return ResolveRuntimeUpscalerLifecycleFailure("DX11-to-DX12 runtime interop initialization");
	} catch (...) {
		logger::error("[FidelityFX] Failed to initialize DX11->DX12 runtime interop.");
		return ResolveRuntimeUpscalerLifecycleFailure("DX11-to-DX12 runtime interop initialization");
	}

	const bool complete =
		swapChain.d3d11Device.get() &&
		swapChain.d3d11Context.get() &&
		swapChain.d3d12Device.get() &&
		swapChain.commandQueue.get() &&
		runtimeD3D11Fence.get() &&
		runtimeD3D12Fence.get();
	return complete ? LifecycleResult::Ready : LifecycleResult::Pending;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerContexts(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion)
{
	auto recordRuntimeProviderResult = [&](bool a_supported) {
		runtimeUpscalerSupportCheckKnown = true;
		runtimeUpscalerSupportConfirmed = a_supported;
		runtimeUpscalerProviderMatchedVersionId = 0;
		runtimeUpscalerProviderMatchedVersionName.clear();

		if (!a_supported || !runtimeUpscalerContexts[0] || !ffxModule.Query)
			return;

		ffxQueryGetProviderVersion providerQuery{};
		providerQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
		providerQuery.header.pNext = nullptr;
		providerQuery.versionId = 0;
		providerQuery.versionName = nullptr;

		if (ffxModule.Query(&runtimeUpscalerContexts[0], &providerQuery.header) == FFX_API_RETURN_OK) {
			runtimeUpscalerProviderMatchedVersionId = providerQuery.versionId;
			if (providerQuery.versionName)
				runtimeUpscalerProviderMatchedVersionName = providerQuery.versionName;
		}
	};

	if (!a_fullRenderWidth || !a_fullRenderHeight || !a_fullDisplayWidth || !a_fullDisplayHeight) {
		return LifecycleResult::Pending;
	}
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeUpscalerContexts)) {
		recordRuntimeProviderResult(false);
		return LifecycleResult::Failed;
	}
	const auto interopResult = EnsureRuntimeUpscalerInterop();
	if (interopResult != LifecycleResult::Ready) {
		if (interopResult == LifecycleResult::Failed)
			recordRuntimeProviderResult(false);
		return interopResult;
	}
	if (!ffxModule.CreateContext || !ffxModule.DestroyContext) {
		recordRuntimeProviderResult(false);
		return LifecycleResult::Failed;
	}

	if (AreRuntimeUpscalerContextsCompatible(
			a_fullRenderWidth,
			a_fullRenderHeight,
			a_fullDisplayWidth,
			a_fullDisplayHeight,
			a_contextCount,
			a_requestedVersion)) {
		return LifecycleResult::Ready;
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler context recreation");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
	if (contextDestroyResult != LifecycleResult::Ready)
		return contextDestroyResult;

	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	uint64_t runtimeVersionId = 0;
	std::string runtimeVersionName;
	const bool hasRuntimeVersionOverride = QueryRuntimeUpscalerVersionId(swapChain.d3d12Device.get(), a_requestedVersion, runtimeVersionId, runtimeVersionName);
	const bool emitDiagLogs = ShouldEmitFidelityFXDiagLogs();
	if (emitDiagLogs && hasRuntimeVersionOverride) {
		logger::debug(
			"[FidelityFX] Runtime upscaler will request FSR version {} through generic override '{}' (id 0x{:X})",
			UpscalerVersionToString(a_requestedVersion),
			runtimeVersionName.empty() ? "(unnamed)" : runtimeVersionName,
			runtimeVersionId);
	}

	bool createdContextWithGenericVersionOverride = false;
	bool createdContextWithGenericVersionAndUpscalerDescriptor = false;
	bool createdContextWithUpscalerVersionDescriptor = false;
	bool createdContextWithDefaultProvider = false;

	for (uint32_t i = 0; i < a_contextCount; ++i) {
		ffx::CreateContextDescUpscale createDesc{};
		createDesc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
		createDesc.maxRenderSize = { a_fullRenderWidth, a_fullRenderHeight };
		createDesc.maxUpscaleSize = { a_fullDisplayWidth, a_fullDisplayHeight };
		createDesc.fpMessage = RuntimeFfxMessage;

		ffx::CreateContextDescUpscaleVersion versionDesc{};
		versionDesc.version = a_requestedVersion;

		ffx::CreateContextDescOverrideVersion overrideVersionDesc{};
		overrideVersionDesc.versionId = runtimeVersionId;

		std::array<RuntimeUpscalerCreateAttemptResult, 4> attempts{ {
			// The upscaler runtime requires its effect-version descriptor when
			// selecting a provider. Do not issue the older override-only probe:
			// current AMD providers reject it, and failed context probes add
			// avoidable driver/compiler churn during VR relatches.
			{ RuntimeUpscalerCreateAttempt::kGenericOverrideOnly, false },
			{ RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion, hasRuntimeVersionOverride },
			{ RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor, true },
			{ RuntimeUpscalerCreateAttempt::kDefaultProvider, a_requestedVersion == FFX_UPSCALER_VERSION },
		} };

		bool contextCreated = false;
		for (auto& attempt : attempts) {
			if (!attempt.enabled)
				continue;

			attempt.attempted = true;
			bool createCrashed = false;
			bool ownershipIndeterminate = false;
			attempt.result = TryCreateRuntimeUpscalerContext(
				runtimeUpscalerContexts[i],
				attempt.attempt,
				createDesc,
				backendDesc,
				versionDesc,
				overrideVersionDesc,
				createCrashed,
				ownershipIndeterminate);

			if (ownershipIndeterminate) {
				runtimeUpscalerContextIndeterminate[i] = true;
				logger::critical(
					"[FidelityFX] Runtime upscaler context {} creation {} without proving provider ownership; quarantining the runtime provider for this session.",
					i,
					createCrashed ? "faulted" : "left a partial handle");
				QuarantineRuntimeUpscalerForSession(
					createCrashed ? "a runtime context creation fault" : "an indeterminate partial runtime context");
				const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(
					"runtime upscaler context creation");
				runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(failureResult);
				recordRuntimeProviderResult(false);
				return failureResult;
			}

			if (attempt.result != FFX_API_RETURN_OK) {
				if (runtimeUpscalerContexts[i])
					break;
				continue;
			}

			contextCreated = true;
			if (attempt.attempt == RuntimeUpscalerCreateAttempt::kGenericOverrideOnly) {
				createdContextWithGenericVersionOverride = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion) {
				createdContextWithGenericVersionAndUpscalerDescriptor = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor) {
				createdContextWithUpscalerVersionDescriptor = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kDefaultProvider) {
				createdContextWithDefaultProvider = true;
			}
			break;
		}

		if (!contextCreated) {
			const auto getAttemptResult = [&](RuntimeUpscalerCreateAttempt a_attempt) {
				const auto iter = std::find_if(attempts.begin(), attempts.end(), [&](const RuntimeUpscalerCreateAttemptResult& a_result) {
					return a_result.attempt == a_attempt;
				});

				return iter != attempts.end() ? FfxCreateResultText(iter->attempted, iter->result) : std::string("not attempted");
			};

			logger::error("[FidelityFX] Failed to create runtime upscaler context {} for FSR version {}. Generic override: {}, generic override + upscaler descriptor: {}, upscaler version descriptor: {}, default provider: {} (Render: {}x{}, Display: {}x{}).",
				i,
				UpscalerVersionToString(a_requestedVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kGenericOverrideOnly),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kDefaultProvider),
				a_fullRenderWidth,
				a_fullRenderHeight,
				a_fullDisplayWidth,
				a_fullDisplayHeight);
			const auto cleanupResult = DestroyRuntimeUpscalerContexts(false);
			recordRuntimeProviderResult(false);
			return cleanupResult == LifecycleResult::Ready ?
				LifecycleResult::Failed : cleanupResult;
		}
		runtimeUpscalerContextIndeterminate[i] = false;
	}

	runtimeUpscalerContextCount = a_contextCount;
	runtimeUpscalerMaxRenderWidth = a_fullRenderWidth;
	runtimeUpscalerMaxRenderHeight = a_fullRenderHeight;
	runtimeUpscalerMaxDisplayWidth = a_fullDisplayWidth;
	runtimeUpscalerMaxDisplayHeight = a_fullDisplayHeight;
	runtimeUpscalerRequestedVersion = a_requestedVersion;
	recordRuntimeProviderResult(true);

	if ((runtimeUpscalerProviderMatchedVersionId != 0 || !runtimeUpscalerProviderMatchedVersionName.empty()) &&
		!RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, a_requestedVersion)) {
		logger::warn(
			"[FidelityFX] Runtime upscaler provider '{}' does not match requested FSR version {}; reporting actual provider path.",
			RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName),
			UpscalerVersionToString(a_requestedVersion));
	}

	if (emitDiagLogs && createdContextWithGenericVersionOverride) {
		logger::debug("[FidelityFX] Runtime upscaler context creation used the generic FSR version override path.");
	}
	if (emitDiagLogs && createdContextWithGenericVersionAndUpscalerDescriptor) {
		logger::debug("[FidelityFX] Runtime upscaler context creation used the generic FSR version override path with the upscaler version descriptor.");
	}
	if (emitDiagLogs && createdContextWithUpscalerVersionDescriptor) {
		logger::debug("[FidelityFX] Runtime upscaler context creation used the upscaler FSR version descriptor.");
	}
	if (createdContextWithDefaultProvider) {
		logger::warn("[FidelityFX] Runtime upscaler context creation succeeded only through the default provider path after explicit FSR version requests failed; reporting the actual provider path.");
	}

	if (emitDiagLogs && runtimeUpscalerProviderMatchedVersionName.empty()) {
		logger::debug("[FidelityFX] Created {} runtime upscaler context(s) for FSR version {} (Render: {}x{}, Display: {}x{}).",
			a_contextCount,
			UpscalerVersionToString(a_requestedVersion),
			a_fullRenderWidth,
			a_fullRenderHeight,
			a_fullDisplayWidth,
			a_fullDisplayHeight);
	} else if (emitDiagLogs) {
		logger::debug("[FidelityFX] Created {} runtime upscaler context(s) using provider '{}' (id 0x{:X}) for FSR version {} (Render: {}x{}, Display: {}x{}).",
			a_contextCount,
			RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName),
			runtimeUpscalerProviderMatchedVersionId,
			UpscalerVersionToString(a_requestedVersion),
			a_fullRenderWidth,
			a_fullRenderHeight,
			a_fullDisplayWidth,
			a_fullDisplayHeight);
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerSharedResources(uint32_t a_contextCount, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight,
	const D3D11_TEXTURE2D_DESC& a_colorDesc,
	const D3D11_TEXTURE2D_DESC& a_depthDesc,
	const D3D11_TEXTURE2D_DESC& a_motionDesc,
	const D3D11_TEXTURE2D_DESC& a_reactiveDesc,
	const D3D11_TEXTURE2D_DESC& a_transparencyDesc,
	const D3D11_TEXTURE2D_DESC& a_outputDesc)
{
	const auto interopResult = EnsureRuntimeUpscalerInterop();
	if (interopResult != LifecycleResult::Ready)
		return interopResult;
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeColorShared))
		return LifecycleResult::Failed;

	const D3D11_TEXTURE2D_DESC desiredColorDesc = MakeSharedTextureDesc(a_colorDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredDepthDesc = MakeSharedTextureDesc(a_depthDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredMotionDesc = MakeSharedTextureDesc(a_motionDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredReactiveDesc = MakeSharedTextureDesc(a_reactiveDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredTransparencyDesc = MakeSharedTextureDesc(a_transparencyDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredOutputDesc = MakeSharedTextureDesc(a_outputDesc, a_fullDisplayWidth, a_fullDisplayHeight, D3D11_BIND_UNORDERED_ACCESS);

	bool missingRequiredResource = false;
	for (uint32_t i = 0; i < a_contextCount; ++i) {
		if (!runtimeColorShared[i] ||
			!runtimeDepthShared[i] ||
			!runtimeMotionShared[i] ||
			!runtimeReactiveShared[i] ||
			!runtimeTransparencyShared[i] ||
			!runtimeOutputShared[i] ||
			!runtimeColorShared[i]->resource11 ||
			!runtimeDepthShared[i]->resource11 ||
			!runtimeMotionShared[i]->resource11 ||
			!runtimeReactiveShared[i]->resource11 ||
			!runtimeTransparencyShared[i]->resource11 ||
			!runtimeOutputShared[i]->resource11 ||
			!runtimeColorShared[i]->resource.get() ||
			!runtimeDepthShared[i]->resource.get() ||
			!runtimeMotionShared[i]->resource.get() ||
			!runtimeReactiveShared[i]->resource.get() ||
			!runtimeTransparencyShared[i]->resource.get() ||
			!runtimeOutputShared[i]->resource.get()) {
			missingRequiredResource = true;
			break;
		}
	}

	const bool needsRecreate =
		missingRequiredResource ||
		!SameTextureDesc(runtimeColorSharedDesc, desiredColorDesc) ||
		!SameTextureDesc(runtimeDepthSharedDesc, desiredDepthDesc) ||
		!SameTextureDesc(runtimeMotionSharedDesc, desiredMotionDesc) ||
		!SameTextureDesc(runtimeReactiveSharedDesc, desiredReactiveDesc) ||
		!SameTextureDesc(runtimeTransparencySharedDesc, desiredTransparencyDesc) ||
		!SameTextureDesc(runtimeOutputSharedDesc, desiredOutputDesc);

	if (!needsRecreate) {
		// Keep any unused array slots until the next proven-idle teardown. They
		// are bounded and retaining them avoids deleting a wrapped resource that
		// may still be referenced by an in-flight cross-API dispatch.
		return LifecycleResult::Ready;
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime shared-resource recreation");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	// The old descriptors do not satisfy this request and the generation is now
	// proven idle. Release it before allocating the replacement to avoid holding
	// two complete six-surface-per-eye interop sets at peak memory. Candidate
	// ownership remains transactional below, so a failure still cannot publish a
	// partially populated new set.
	const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
	if (resourceDestroyResult != LifecycleResult::Ready)
		return resourceDestroyResult;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	RuntimeWrappedResources newColorShared{};
	RuntimeWrappedResources newDepthShared{};
	RuntimeWrappedResources newMotionShared{};
	RuntimeWrappedResources newReactiveShared{};
	RuntimeWrappedResources newTransparencyShared{};
	RuntimeWrappedResources newOutputShared{};
	try {
		for (uint32_t i = 0; i < a_contextCount; ++i) {
			newColorShared[i] = std::make_unique<WrappedResource>(desiredColorDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			newDepthShared[i] = std::make_unique<WrappedResource>(desiredDepthDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			newMotionShared[i] = std::make_unique<WrappedResource>(desiredMotionDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			newReactiveShared[i] = std::make_unique<WrappedResource>(desiredReactiveDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			newTransparencyShared[i] = std::make_unique<WrappedResource>(desiredTransparencyDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			newOutputShared[i] = std::make_unique<WrappedResource>(desiredOutputDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to create runtime shared resources: {}", e.what());
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler shared-resource creation");
	} catch (...) {
		logger::error("[FidelityFX] Failed to create runtime shared resources.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler shared-resource creation");
	}

	// Publish the complete new generation in one ownership swap. Local RAII
	// owners release every partial candidate if any later allocation fails.
	runtimeColorShared = std::move(newColorShared);
	runtimeDepthShared = std::move(newDepthShared);
	runtimeMotionShared = std::move(newMotionShared);
	runtimeReactiveShared = std::move(newReactiveShared);
	runtimeTransparencyShared = std::move(newTransparencyShared);
	runtimeOutputShared = std::move(newOutputShared);

	runtimeColorSharedDesc = desiredColorDesc;
	runtimeDepthSharedDesc = desiredDepthDesc;
	runtimeMotionSharedDesc = desiredMotionDesc;
	runtimeReactiveSharedDesc = desiredReactiveDesc;
	runtimeTransparencySharedDesc = desiredTransparencyDesc;
	runtimeOutputSharedDesc = desiredOutputDesc;

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DispatchRuntimeUpscalerSingle(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
	float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness)
{
	if (a_contextIndex >= runtimeUpscalerContextCount || !runtimeUpscalerContexts[a_contextIndex])
		return LifecycleResult::Failed;
	if (!a_color || !a_depth || !a_motionVectors || !a_reactiveMask || !a_transparencyCompositionMask || !a_output)
		return LifecycleResult::Failed;
	if (!a_renderWidth || !a_renderHeight || !a_displayWidth || !a_displayHeight)
		return LifecycleResult::Failed;

	D3D11_TEXTURE2D_DESC colorDesc{};
	D3D11_TEXTURE2D_DESC depthDesc{};
	D3D11_TEXTURE2D_DESC motionDesc{};
	D3D11_TEXTURE2D_DESC reactiveDesc{};
	D3D11_TEXTURE2D_DESC transparencyDesc{};
	D3D11_TEXTURE2D_DESC outputDesc{};
	if (!TryGetTexture2DDesc(a_color, colorDesc) ||
		!TryGetTexture2DDesc(a_depth, depthDesc) ||
		!TryGetTexture2DDesc(a_motionVectors, motionDesc) ||
		!TryGetTexture2DDesc(a_reactiveMask, reactiveDesc) ||
		!TryGetTexture2DDesc(a_transparencyCompositionMask, transparencyDesc) ||
		!TryGetTexture2DDesc(a_output, outputDesc)) {
		return LifecycleResult::Pending;
	}

	const auto sharedResourceResult = EnsureRuntimeUpscalerSharedResources(
			runtimeUpscalerContextCount,
			runtimeUpscalerMaxRenderWidth,
			runtimeUpscalerMaxRenderHeight,
			runtimeUpscalerMaxDisplayWidth,
			runtimeUpscalerMaxDisplayHeight,
			colorDesc,
			depthDesc,
			motionDesc,
			reactiveDesc,
			transparencyDesc,
			outputDesc);
	if (sharedResourceResult != LifecycleResult::Ready)
		return sharedResourceResult;

	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;

	if (!swapChain.d3d11Context || !swapChain.commandQueue || !runtimeD3D11Fence || !runtimeD3D12Fence)
		return LifecycleResult::Pending;

	auto isValidShared = [](const std::unique_ptr<WrappedResource>& a_resource) {
		return a_resource && a_resource->resource11 && a_resource->resource.get();
	};
	if (!isValidShared(runtimeColorShared[a_contextIndex]) ||
		!isValidShared(runtimeDepthShared[a_contextIndex]) ||
		!isValidShared(runtimeMotionShared[a_contextIndex]) ||
		!isValidShared(runtimeReactiveShared[a_contextIndex]) ||
		!isValidShared(runtimeTransparencyShared[a_contextIndex]) ||
		!isValidShared(runtimeOutputShared[a_contextIndex])) {
		return LifecycleResult::Failed;
	}

	RuntimeCommandContext* commandContext = nullptr;
	const uint32_t requiredFreeContexts =
		globals::game::isVR && a_contextIndex == 0 ? runtimeUpscalerContextCount : 1u;
	const auto acquireResult = AcquireRuntimeCommandContext(commandContext, requiredFreeContexts);
	if (acquireResult != LifecycleResult::Ready)
		return acquireResult;

	auto* commandAllocator = commandContext->commandAllocator.get();
	auto* commandList = commandContext->commandList.get();
	if (!commandAllocator || !commandList)
		return LifecycleResult::Failed;

	const bool annotateDispatch = state && state->frameAnnotations;
	if (annotateDispatch) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "FSR Runtime Eye %u", a_contextIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("FSR Runtime Dispatch");
		}
	}

	bool dispatchOk = false;
	bool commandListSubmitted = false;
	bool commandFenceTracked = false;
	try {
		auto copyIntoShared = [&](ID3D11Resource* a_source, const std::unique_ptr<WrappedResource>& a_destination, uint32_t a_width, uint32_t a_height, uint32_t a_maxWidth, uint32_t a_maxHeight) {
			if (!a_source || !a_destination || !a_destination->resource11)
				return false;

			const uint32_t copyWidth = std::min(a_width, a_maxWidth);
			const uint32_t copyHeight = std::min(a_height, a_maxHeight);
			if (!copyWidth || !copyHeight)
				return false;

			D3D11_BOX sourceBox{};
			sourceBox.left = 0;
			sourceBox.top = 0;
			sourceBox.front = 0;
			sourceBox.right = copyWidth;
			sourceBox.bottom = copyHeight;
			sourceBox.back = 1;
			swapChain.d3d11Context->CopySubresourceRegion(a_destination->resource11.get(), 0, 0, 0, 0, a_source, 0, &sourceBox);
			return true;
		};

		if (!copyIntoShared(a_color, runtimeColorShared[a_contextIndex], colorDesc.Width, colorDesc.Height, runtimeColorSharedDesc.Width, runtimeColorSharedDesc.Height) ||
			!copyIntoShared(a_depth, runtimeDepthShared[a_contextIndex], depthDesc.Width, depthDesc.Height, runtimeDepthSharedDesc.Width, runtimeDepthSharedDesc.Height) ||
			!copyIntoShared(a_motionVectors, runtimeMotionShared[a_contextIndex], motionDesc.Width, motionDesc.Height, runtimeMotionSharedDesc.Width, runtimeMotionSharedDesc.Height) ||
			!copyIntoShared(a_reactiveMask, runtimeReactiveShared[a_contextIndex], reactiveDesc.Width, reactiveDesc.Height, runtimeReactiveSharedDesc.Width, runtimeReactiveSharedDesc.Height) ||
			!copyIntoShared(a_transparencyCompositionMask, runtimeTransparencyShared[a_contextIndex], transparencyDesc.Width, transparencyDesc.Height, runtimeTransparencySharedDesc.Width, runtimeTransparencySharedDesc.Height)) {
			logger::error("[FidelityFX] Runtime upscaler shared-resource copy failed for eye {}.", a_contextIndex);
			dispatchOk = false;
		} else {
			const uint64_t d3d11SubmitFence = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.d3d11Context->Signal(runtimeD3D11Fence.get(), d3d11SubmitFence));
			DX::ThrowIfFailed(swapChain.commandQueue->Wait(runtimeD3D12Fence.get(), d3d11SubmitFence));

			DX::ThrowIfFailed(commandAllocator->Reset());
			DX::ThrowIfFailed(commandList->Reset(commandAllocator, nullptr));

			std::array<D3D12_RESOURCE_BARRIER, 6> beginBarriers = {
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeColorShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeDepthShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeMotionShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeReactiveShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeTransparencyShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeOutputShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			commandList->ResourceBarrier(static_cast<UINT>(beginBarriers.size()), beginBarriers.data());

			ffx::DispatchDescUpscale dispatchParameters{};
			dispatchParameters.commandList = commandList;
			dispatchParameters.color = ffxApiGetResourceDX12(runtimeColorShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.depth = ffxApiGetResourceDX12(runtimeDepthShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.motionVectors = ffxApiGetResourceDX12(runtimeMotionShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.exposure = FfxApiResource({});
			dispatchParameters.reactive = ffxApiGetResourceDX12(runtimeReactiveShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.transparencyAndComposition = ffxApiGetResourceDX12(runtimeTransparencyShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.output = ffxApiGetResourceDX12(runtimeOutputShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
			dispatchParameters.jitterOffset = { -upscaling.jitter.x, -upscaling.jitter.y };
			dispatchParameters.motionVectorScale = { a_motionVectorScaleX, a_motionVectorScaleY };
			dispatchParameters.renderSize = { a_renderWidth, a_renderHeight };
			dispatchParameters.upscaleSize = { a_displayWidth, a_displayHeight };
			const auto sharpening = ResolveFSRSharpeningSettings(a_sharpness);
			dispatchParameters.enableSharpening = sharpening.enabled;
			dispatchParameters.sharpness = sharpening.sharpness;
			LogFSRSharpeningDispatch(sharpening, "runtime");
			dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
			dispatchParameters.preExposure = 1.0f;
			dispatchParameters.reset = upscaling.ShouldResetHistoryThisFrame();
			dispatchParameters.cameraNear = *globals::game::cameraNear;
			dispatchParameters.cameraFar = *globals::game::cameraFar;
			dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
			dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
			dispatchParameters.flags = 0;
			const bool runtimeResumeReset = runtimeResumeResetDispatchesRemaining > 0;
			dispatchParameters.reset = dispatchParameters.reset || runtimeResumeReset;

			bool dispatchCrashed = false;
			const auto dispatchResult = DispatchRuntimeUpscalerProtected(
				&runtimeUpscalerContexts[a_contextIndex],
				&dispatchParameters.header,
				dispatchCrashed);
			dispatchOk = !dispatchCrashed && dispatchResult == FFX_API_RETURN_OK;
			if (dispatchCrashed) {
				runtimeUpscalerContextIndeterminate[a_contextIndex] = true;
				QuarantineRuntimeUpscalerForSession("a runtime upscaler dispatch fault");
				const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure(
					"runtime upscaler dispatch fault");
				runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(failureResult);
				logger::critical(
					"[FidelityFX] Runtime upscaler dispatch faulted for eye {}; retaining its indeterminate context and resources for this session.",
					a_contextIndex);
			}
			if (dispatchOk && runtimeResumeReset) {
				runtimeResumeResetDispatchesRemaining--;
				if (runtimeResumeResetDispatchesRemaining == 0)
					runtimeHostFallbackActive = false;
			}
			if (!dispatchOk) {
				logger::error("[FidelityFX] Runtime upscaler dispatch failed for eye {}.", a_contextIndex);
			}

			if (dispatchOk) {
				std::array<D3D12_RESOURCE_BARRIER, 6> endBarriers = {
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeColorShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeDepthShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeMotionShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeReactiveShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeTransparencyShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(runtimeOutputShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
				};
				commandList->ResourceBarrier(static_cast<UINT>(endBarriers.size()), endBarriers.data());

				DX::ThrowIfFailed(commandList->Close());

				ID3D12CommandList* commandListsToExecute[] = { commandList };
				const uint64_t d3d12SubmitFence = runtimeFenceValue++;
				swapChain.commandQueue->ExecuteCommandLists(1, commandListsToExecute);
				commandListSubmitted = true;
				DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), d3d12SubmitFence));
				commandContext->fenceValue = d3d12SubmitFence;
				commandFenceTracked = true;
				DX::ThrowIfFailed(swapChain.d3d11Context->Wait(runtimeD3D11Fence.get(), d3d12SubmitFence));

				const uint32_t copyWidth = std::min({ a_displayWidth, outputDesc.Width, runtimeOutputSharedDesc.Width });
				const uint32_t copyHeight = std::min({ a_displayHeight, outputDesc.Height, runtimeOutputSharedDesc.Height });
				if (!copyWidth || !copyHeight) {
					dispatchOk = false;
				} else {
					D3D11_BOX outputBox{};
					outputBox.left = 0;
					outputBox.top = 0;
					outputBox.front = 0;
					outputBox.right = copyWidth;
					outputBox.bottom = copyHeight;
					outputBox.back = 1;
					swapChain.d3d11Context->CopySubresourceRegion(a_output, 0, 0, 0, 0, runtimeOutputShared[a_contextIndex]->resource11.get(), 0, &outputBox);
				}
			}
		}
	} catch (const std::exception& e) {
		if (!commandListSubmitted) {
			commandContext->fenceValue = 0;
		} else if (!commandFenceTracked) {
			try {
				const uint64_t rescueFence = runtimeFenceValue++;
				DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), rescueFence));
				commandContext->fenceValue = rescueFence;
			} catch (...) {
				commandContext->fenceValue = 0;
			}
		}
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for eye {}: {}", a_contextIndex, e.what());
		dispatchOk = false;
	} catch (...) {
		if (!commandListSubmitted) {
			commandContext->fenceValue = 0;
		} else if (!commandFenceTracked) {
			try {
				const uint64_t rescueFence = runtimeFenceValue++;
				DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), rescueFence));
				commandContext->fenceValue = rescueFence;
			} catch (...) {
				commandContext->fenceValue = 0;
			}
		}
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for eye {}.", a_contextIndex);
		dispatchOk = false;
	}

	if (annotateDispatch)
		state->EndPerfEvent();

	if (dispatchOk)
		return LifecycleResult::Ready;
	if (runtimeUpscalerContextIndeterminate[a_contextIndex])
		return runtimeUpscalerQuarantineRetirement;
	return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler dispatch");
}

bool FidelityFX::UpscaleRegion(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
	float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness, bool* a_usedRuntimeUpscaler)
{
	if (a_usedRuntimeUpscaler)
		*a_usedRuntimeUpscaler = false;
	if (fsrHostStateQuarantined ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		return false;
	}
	if (!a_color || !a_depth || !a_motionVectors || !a_reactiveMask || !a_transparencyCompositionMask || !a_output ||
		!a_renderWidth || !a_renderHeight || !a_displayWidth || !a_displayHeight) {
		return false;
	}
	auto state = globals::state;
	if (!state)
		return false;
	auto& upscaling = globals::features::upscaling;
	const bool vendorLifecycleMutationDeferred =
		globals::game::isVR &&
		upscaling.ShouldDeferVRVendorLifecycleMutation();

	const uint32_t currentFrame = state->frameCount;
	if (!runtimeHostFallbackFrameValid || runtimeHostFallbackFrame != currentFrame) {
		runtimeHostFallbackFrameValid = true;
		runtimeHostFallbackFrame = currentFrame;
		runtimeHostFallbackForFrame = false;
		runtimeUpscalerUsedForFrame = false;
	}
	if (runtimeUpscalerSessionQuarantined && !vendorLifecycleMutationDeferred) {
		const auto retirementResult = RetireQuarantinedRuntimeUpscalerResources();
		if (retirementResult == LifecycleResult::Failed) {
			static bool loggedQuarantineRetirementFailure = false;
			if (!loggedQuarantineRetirementFailure) {
				logger::error("[FidelityFX] Quarantined runtime upscaler resources could not be retired safely; retained allocations will remain owned for this game session.");
				loggedQuarantineRetirementFailure = true;
			}
		}
	}

	const bool runtimeFsr4Requested = ShouldRequestRuntimeFsr4();
	const bool runtimeRequested = runtimeFsr4Requested || ShouldUseRuntimeUpscalerForFSR();
	const uint32_t requestedRuntimeVersion = runtimeFsr4Requested ? FFX_UPSCALER_VERSION : Fsr3Version;
	const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();
	const uint32_t runtimeContextCount = splitPerEyeContexts ? 2u : 1u;
	const auto armRuntimeHostFallback = [&]() {
		runtimeResumeResetDispatchesRemaining = std::max(runtimeResumeResetDispatchesRemaining, runtimeContextCount);
		if (runtimeHostFallbackActive)
			return;
		runtimeHostFallbackActive = true;
		runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeContextCount);
	};
	const bool shaderCompilationActive =
		globals::shaderCache &&
		globals::shaderCache->IsCompiling();
	const bool awaitingInitialVRRenderScaleLatch =
		globals::game::isVR &&
		upscaling.IsRenderScaleModeRequested() &&
		!upscaling.IsVRRenderScaleModeLatched();
	const bool runtimePathEligible =
		runtimeRequested &&
		CanUseRuntimeUpscalerPath() &&
		!awaitingInitialVRRenderScaleLatch &&
		!vendorLifecycleMutationDeferred;

	float2 screenSize{};
	float2 renderSize{};
	uint32_t fullDisplayWidth = 0;
	uint32_t fullDisplayHeight = 0;
	uint32_t requestedFullRenderWidth = 0;
	uint32_t requestedFullRenderHeight = 0;
	uint32_t fullRenderWidth = 0;
	uint32_t fullRenderHeight = 0;
	bool runtimeContextsCompatible = false;
	if (runtimePathEligible) {
		GetRuntimeUpscaleSizes(screenSize, renderSize);
		fullDisplayWidth = static_cast<uint32_t>(splitPerEyeContexts ? screenSize.x / 2.0f : screenSize.x);
		fullDisplayHeight = static_cast<uint32_t>(screenSize.y);
		requestedFullRenderWidth = static_cast<uint32_t>(splitPerEyeContexts ? renderSize.x / 2.0f : renderSize.x);
		requestedFullRenderHeight = static_cast<uint32_t>(renderSize.y);
		// Mirror the host VR path: keep runtime contexts/resources sized for the
		// largest per-eye display bounds so render-scale quality changes do not
		// force DX11<->DX12 runtime context/shared-resource churn.
		const bool useFullRenderBounds = splitPerEyeContexts || runtimeFsr4Requested;
		fullRenderWidth = useFullRenderBounds ? fullDisplayWidth : requestedFullRenderWidth;
		fullRenderHeight = useFullRenderBounds ? fullDisplayHeight : requestedFullRenderHeight;
		runtimeContextsCompatible = AreRuntimeUpscalerContextsCompatible(
			fullRenderWidth,
			fullRenderHeight,
			fullDisplayWidth,
			fullDisplayHeight,
			runtimeContextCount,
			requestedRuntimeVersion);
	}

	// Shader compilation and runtime-provider context creation both exercise the
	// driver compiler. Keep an already-compatible provider dispatching, but do
	// not create or recreate a provider context until CSX compilation is idle.
	const bool runtimeDeferredByGate =
		runtimeRequested &&
		!runtimeUpscalerSessionQuarantined &&
		(vendorLifecycleMutationDeferred ||
			awaitingInitialVRRenderScaleLatch ||
			(runtimePathEligible && shaderCompilationActive && !runtimeContextsCompatible));
	if (runtimeDeferredByGate)
		runtimeHostFallbackForFrame = true;
	const bool runtimeSelected =
		runtimePathEligible &&
		(!shaderCompilationActive || runtimeContextsCompatible) &&
		!runtimeHostFallbackForFrame;
	static bool loggedRuntimeDeferredForShaderCompilation = false;
	if (ShouldEmitFidelityFXDiagLogs() && runtimePathEligible && shaderCompilationActive && !runtimeContextsCompatible) {
		if (!loggedRuntimeDeferredForShaderCompilation) {
			logger::debug(
				"[FidelityFX] Deferring required DX12 runtime upscaler context creation/recreation while CSX shader compilation is active; actual dispatch is {}.",
				GetHostFsrSdkLabel());
			loggedRuntimeDeferredForShaderCompilation = true;
		}
	} else {
		loggedRuntimeDeferredForShaderCompilation = false;
	}
	static bool loggedRuntimeContinuedDuringShaderCompilation = false;
	if (ShouldEmitFidelityFXDiagLogs() && runtimeSelected && shaderCompilationActive && runtimeContextsCompatible) {
		if (!loggedRuntimeContinuedDuringShaderCompilation) {
			logger::debug(
				"[FidelityFX] CSX shader compilation is active; continuing dispatch through the already-compatible DX12 runtime upscaler context (requested FSR version {}).",
				UpscalerVersionToString(requestedRuntimeVersion));
			loggedRuntimeContinuedDuringShaderCompilation = true;
		}
	} else {
		loggedRuntimeContinuedDuringShaderCompilation = false;
	}
	static bool loggedRuntimeDeferredForRenderScaleLatch = false;
	if (ShouldEmitFidelityFXDiagLogs() && runtimeRequested && !runtimeUpscalerSessionQuarantined && awaitingInitialVRRenderScaleLatch) {
		if (!loggedRuntimeDeferredForRenderScaleLatch) {
			logger::debug(
				"[FidelityFX] Deferring DX12 runtime upscaler context creation until the requested VR Render Scale contract is latched; using {}.",
				GetHostFsrSdkLabel());
			loggedRuntimeDeferredForRenderScaleLatch = true;
		}
	} else {
		loggedRuntimeDeferredForRenderScaleLatch = false;
	}

	if (runtimeSelected) {
		auto tryRuntimeUpscaler = [&](uint32_t a_requestedVersion, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight) {
			try {
				const auto contextResult = EnsureRuntimeUpscalerContexts(
					a_fullRenderWidth,
					a_fullRenderHeight,
					fullDisplayWidth,
					fullDisplayHeight,
					runtimeContextCount,
					a_requestedVersion);
				if (contextResult != LifecycleResult::Ready)
					return contextResult;

				const auto dispatchResult = DispatchRuntimeUpscalerSingle(
						a_contextIndex,
						a_color,
						a_depth,
						a_motionVectors,
						a_reactiveMask,
						a_transparencyCompositionMask,
						a_output,
						a_renderWidth,
						a_renderHeight,
						a_displayWidth,
						a_displayHeight,
						a_motionVectorScaleX,
						a_motionVectorScaleY,
						a_sharpness);
				if (dispatchResult == LifecycleResult::Ready) {
					const auto dispatchPath = GetRuntimeUpscalerProviderFramePath(a_requestedVersion);
					RecordRuntimeUpscalerFramePath(dispatchPath);
#ifdef DEVBENCH_BRIDGE_ENABLED
					RecordDevBenchSuccessfulDispatch(dispatchPath);
#endif
				}
				return dispatchResult;
			} catch (const std::exception& e) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an exception: {}",
					UpscalerVersionToString(a_requestedVersion),
					e.what());
			} catch (...) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an unknown exception.",
					UpscalerVersionToString(a_requestedVersion));
			}

			return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler setup/dispatch");
		};

		const auto runtimeResult = tryRuntimeUpscaler(requestedRuntimeVersion, fullRenderWidth, fullRenderHeight);
		if (runtimeResult == LifecycleResult::Ready) {
			runtimeUpscalerUsedForFrame = true;
			if (a_usedRuntimeUpscaler)
				*a_usedRuntimeUpscaler = true;
			return true;
		}
		runtimeHostFallbackForFrame = true;
		armRuntimeHostFallback();

		if (runtimeResult == LifecycleResult::DeviceLost) {
			QuarantineRuntimeUpscalerForSession(
				"runtime-provider device loss");
			return false;
		}
		if (runtimeResult == LifecycleResult::RuntimeDeviceLost) {
			// The optional D3D12 provider may have consumed or submitted this eye.
			// Keep this stereo cycle on the established presentation fallback; the
			// next frame can select host D3D11 FSR coherently for both eyes.
			return false;
		}
		if (runtimeResult == LifecycleResult::Failed) {
			// A terminal runtime-provider failure can leave AMD's DX12 provider state
			// unsafe for immediate reuse. Quarantine it and retire it asynchronously.
			if (runtimeFsr4Requested)
				LatchRuntimeFsr4Failure();
			QuarantineRuntimeUpscalerForSession(
				runtimeFsr4Requested ? "an FSR4 setup/dispatch failure" : "an FSR3 runtime setup/dispatch failure");
		}
		if (std::ranges::any_of(
				runtimeUpscalerContextIndeterminate,
				[](bool a_indeterminate) { return a_indeterminate; })) {
			// A faulting provider may have consumed or submitted this eye. Do not
			// mix an immediate host dispatch into the same stereo cycle.
			return false;
		}
		if (runtimeUpscalerUsedForFrame)
			return false;
	}

	// OpenVR accepts each eye independently. Once this frame has published a
	// runtime-provider eye, falling through to host FSR for a later eye would
	// create a mixed temporal-provider stereo pair that cannot be retracted.
	// Fail this eye so the existing presentation fallback owns the remainder of
	// the cycle; the next frame can select host FSR coherently for both eyes.
	if (runtimeUpscalerUsedForFrame && !runtimeSelected)
		return false;

	if (runtimeRequested && !runtimeSelected)
		armRuntimeHostFallback();
	if (!runtimeRequested) {
		runtimeFallbackResetDispatchesRemaining = 0;
		runtimeResumeResetDispatchesRemaining = 0;
		runtimeHostFallbackActive = false;
	}

	if (vendorLifecycleMutationDeferred &&
		!AreFSRResourcesCompatible(
			a_renderWidth,
			a_renderHeight,
			a_displayWidth,
			a_displayHeight,
			runtimeContextCount)) {
		return false;
	}

	if (!HasFSRResources() || a_contextIndex >= fsrContextCount || !fsrContextValid[a_contextIndex])
		return false;

	auto context = globals::d3d::context;
	if (!context)
		return false;

	auto jitter = upscaling.jitter;
	const auto fallbackFramePath =
		runtimeRequested ? RuntimeUpscalerFramePath::kHostFsr31Fallback : RuntimeUpscalerFramePath::kHostFsr31;
	RecordRuntimeUpscalerFramePath(fallbackFramePath);

	if (state->frameAnnotations) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "FSR Dispatch Eye %u", a_contextIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("FSR Dispatch");
		}
	}

	FfxFsr3DispatchUpscaleDescription dispatchParameters{};
	dispatchParameters.commandList = ffxGetCommandListDX11(context);
	dispatchParameters.color = ffxGetResource(a_color, L"FSR3_Input_OutputColor");
	dispatchParameters.depth = ffxGetResource(a_depth, L"FSR3_InputDepth");
	dispatchParameters.motionVectors = ffxGetResource(a_motionVectors, L"FSR3_InputMotionVectors");
	dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure");
	dispatchParameters.upscaleOutput = ffxGetResource(a_output, L"FSR3_OutputColor");
	dispatchParameters.reactive = ffxGetResource(a_reactiveMask, L"FSR3_InputReactiveMap");
	dispatchParameters.transparencyAndComposition = ffxGetResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");
	dispatchParameters.motionVectorScale.x = a_motionVectorScaleX;
	dispatchParameters.motionVectorScale.y = a_motionVectorScaleY;
	dispatchParameters.renderSize.width = a_renderWidth;
	dispatchParameters.renderSize.height = a_renderHeight;
	dispatchParameters.upscaleSize.width = a_displayWidth;
	dispatchParameters.upscaleSize.height = a_displayHeight;
	dispatchParameters.jitterOffset.x = -jitter.x;
	dispatchParameters.jitterOffset.y = -jitter.y;
	dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
	dispatchParameters.cameraFar = *globals::game::cameraFar;
	dispatchParameters.cameraNear = *globals::game::cameraNear;
	const auto sharpening = ResolveFSRSharpeningSettings(a_sharpness);
	dispatchParameters.enableSharpening = sharpening.enabled;
	dispatchParameters.sharpness = sharpening.sharpness;
	LogFSRSharpeningDispatch(sharpening, "host");
	dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
	dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
	const bool runtimeFallbackReset = runtimeRequested && runtimeFallbackResetDispatchesRemaining > 0;
	if (runtimeFallbackReset)
		runtimeFallbackResetDispatchesRemaining--;
	dispatchParameters.reset = globals::features::upscaling.ShouldResetHistoryThisFrame() || runtimeFallbackReset;
	dispatchParameters.preExposure = 1.0f;
	dispatchParameters.flags = 0;

	bool hostDispatchCrashed = false;
	const bool dispatchOK = DispatchHostFsr3UpscaleProtected(fsrContext[a_contextIndex], dispatchParameters, hostDispatchCrashed);
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (dispatchOK)
		RecordDevBenchSuccessfulDispatch(fallbackFramePath);
#endif
	if (!dispatchOK && !hostDispatchCrashed) {
		logger::critical("[FidelityFX] Failed to dispatch region upscaling for eye {}!", a_contextIndex);
	}
	if (hostDispatchCrashed) {
		(void)ResolveFSRLifecycleFailure("FSR3 host dispatch fault");
		QuarantineHostFSRContext(
			a_contextIndex,
			"an FSR3 host dispatch fault");
		if (!fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] Region FSR3 dispatch faulted for eye {}; its indeterminate context and shared scratch ownership have been quarantined for this session.", a_contextIndex);
			fsrDispatchCrashLogged = true;
		}
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return dispatchOK;
}

bool FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_depth, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto state = globals::state;
	if (!state || !a_depth)
		return false;

	float2 screenSize{};
	float2 renderSize{};
	GetRuntimeUpscaleSizes(screenSize, renderSize);

	auto& upscaling = globals::features::upscaling;
	if (globals::game::isVR && upscaling.IsPresentationUpscalingActive())
		return false;

	const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();

	if (splitPerEyeContexts) {
		if (!upscaling.PreparePerEyeInputs(a_upscalingTexture, a_depth, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask, false, false)) {
			static bool loggedPrepareFailure = false;
			if (!loggedPrepareFailure) {
				logger::warn("[FidelityFX] VR FSR skipped because per-eye input preparation failed.");
				loggedPrepareFailure = true;
			}
			return false;
		}

		const bool perEyeResourcesReady = upscaling.AreVRPerEyeUpscalingResourcesReady(false, true);
		if (!perEyeResourcesReady) {
			static bool loggedMissingResource = false;
			if (!loggedMissingResource) {
				logger::warn("[FidelityFX] VR FSR skipped because prepared per-eye resources are incomplete.");
				loggedMissingResource = true;
			}
			return false;
		}

		const uint32_t eyeDisplayWidth = static_cast<uint32_t>(screenSize.x / 2.0f);
		const uint32_t eyeDisplayHeight = static_cast<uint32_t>(screenSize.y);
		const uint32_t eyeRenderWidth = static_cast<uint32_t>(renderSize.x / 2.0f);
		const uint32_t eyeRenderHeight = static_cast<uint32_t>(renderSize.y);

		bool allEvaluated = true;
		std::array<bool, 2> usedRuntimeUpscaler{};
		for (uint32_t i = 0; i < 2; ++i) {
			if (!UpscaleRegion(
					i,
					upscaling.vrIntermediateColorIn[i]->resource.get(),
					upscaling.vrIntermediateLinearDepth[i]->resource.get(),
					upscaling.vrIntermediateMotionVectors[i]->resource.get(),
					upscaling.vrIntermediateReactiveMask[i]->resource.get(),
					upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
					upscaling.vrIntermediateColorOut[i]->resource.get(),
					eyeRenderWidth,
					eyeRenderHeight,
					eyeDisplayWidth,
					eyeDisplayHeight,
					renderSize.x / 2.0f,
					renderSize.y,
					a_sharpness,
					std::addressof(usedRuntimeUpscaler[i]))) {
				logger::error("[FidelityFX] Upscale dispatch failed for VR eye {}.", i);
				allEvaluated = false;
			}
		}
		if (allEvaluated && usedRuntimeUpscaler[0] != usedRuntimeUpscaler[1]) {
			static bool loggedStereoProviderMismatch = false;
			if (!loggedStereoProviderMismatch) {
				logger::warn("[FidelityFX] VR FSR provider changed between eyes; retaining the current scene instead of publishing a mixed-provider stereo pair.");
				loggedStereoProviderMismatch = true;
			}
			allEvaluated = false;
		}

		if (allEvaluated) {
			upscaling.FinalizePerEyeOutputs(a_upscalingTexture);
		} else {
			static bool loggedVREvaluateFailure = false;
			if (!loggedVREvaluateFailure) {
				logger::warn("[FidelityFX] VR FSR evaluate did not complete for both eyes; keeping the current scene texture instead of copying stale output.");
				loggedVREvaluateFailure = true;
			}
		}
		return allEvaluated;
	}

	const bool evaluated = UpscaleRegion(
			0,
			a_upscalingTexture,
			a_depth,
			a_motionVectors,
			a_reactiveMask,
			a_transparencyCompositionMask,
			a_upscalingTexture,
			static_cast<uint32_t>(renderSize.x),
			static_cast<uint32_t>(renderSize.y),
			static_cast<uint32_t>(screenSize.x),
			static_cast<uint32_t>(screenSize.y),
			renderSize.x,
			renderSize.y,
			a_sharpness,
			nullptr);
	if (!evaluated) {
		logger::error("[FidelityFX] Upscale dispatch failed.");
	}
	return evaluated;
}

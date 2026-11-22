#include "D3D.h"

#include "Deferred.h"
#include "Feature.h"
#include "Features/TerrainBlending.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/CompileDedupe.h"
#include "Utils/Format.h"
#include "Utils/Logging.h"
#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <d3dcompiler.h>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace Util
{
	namespace
	{
		std::mutex multithreadScopeMutex;
		int multithreadScopeHolders = 0;
		BOOL multithreadScopePriorProtection = FALSE;
	}

	MultithreadScope::MultithreadScope(ID3D11DeviceContext* context)
	{
		if (!context || FAILED(context->QueryInterface(multithread.put()))) {
			multithread = nullptr;
			return;
		}
		{
			std::lock_guard lock(multithreadScopeMutex);
			const BOOL prior = multithread->SetMultithreadProtected(TRUE);
			if (multithreadScopeHolders++ == 0)
				multithreadScopePriorProtection = prior;
		}
		multithread->Enter();
	}

	MultithreadScope::~MultithreadScope()
	{
		if (!multithread)
			return;
		multithread->Leave();
		std::lock_guard lock(multithreadScopeMutex);
		if (--multithreadScopeHolders == 0)
			multithread->SetMultithreadProtected(multithreadScopePriorProtection);
	}

	ID3D11ShaderResourceView* GetCurrentSceneDepthSRV(bool prefer16bit)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return nullptr;
		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		if (globals::deferred && globals::deferred->sceneDepthFinal)
			return zPrepassCopy.depthSRV;

		auto& tb = globals::features::terrainBlending;
		if (tb.loaded && tb.settings.Enabled) {
			auto* srv = prefer16bit ? (tb.blendedDepthTexture16 ? tb.blendedDepthTexture16->srv.get() : nullptr) : (tb.blendedDepthTexture ? tb.blendedDepthTexture->srv.get() : nullptr);
			if (srv)
				return srv;
		}
		return zPrepassCopy.depthSRV;
	}

	ID3D11ShaderResourceView* GetSRVFromRTV(const ID3D11RenderTargetView* a_rtv)
	{
		if (a_rtv) {
			if (auto r = globals::game::renderer) {
				for (int i = 0; i < RE::RENDER_TARGETS::kTOTAL; i++) {
					auto rt = r->GetRuntimeData().renderTargets[i];
					if (a_rtv == rt.RTV) {
						return rt.SRV;
					}
				}
			}
		}
		return nullptr;
	}

	ID3D11RenderTargetView* GetRTVFromSRV(const ID3D11ShaderResourceView* a_srv)
	{
		if (a_srv) {
			if (auto r = globals::game::renderer) {
				for (int i = 0; i < RE::RENDER_TARGETS::kTOTAL; i++) {
					auto rt = r->GetRuntimeData().renderTargets[i];
					if (a_srv == rt.SRV || a_srv == rt.SRVCopy) {
						return rt.RTV;
					}
				}
			}
		}
		return nullptr;
	}

	std::string GetNameFromSRV(const ID3D11ShaderResourceView* a_srv)
	{
		using RENDER_TARGET = RE::RENDER_TARGETS::RENDER_TARGET;

		if (a_srv) {
			if (auto r = globals::game::renderer) {
				for (int i = 0; i < RE::RENDER_TARGETS::kTOTAL; i++) {
					auto rt = r->GetRuntimeData().renderTargets[i];
					if (a_srv == rt.SRV || a_srv == rt.SRVCopy) {
						return std::string(magic_enum::enum_name(static_cast<RENDER_TARGET>(i)));
					}
				}
			}
		}
		return "NONE";
	}

	std::string GetNameFromRTV(const ID3D11RenderTargetView* a_rtv)
	{
		using RENDER_TARGET = RE::RENDER_TARGETS::RENDER_TARGET;
		if (a_rtv) {
			if (auto r = globals::game::renderer) {
				for (int i = 0; i < RE::RENDER_TARGETS::kTOTAL; i++) {
					auto rt = r->GetRuntimeData().renderTargets[i];
					if (a_rtv == rt.RTV) {
						return std::string(magic_enum::enum_name(static_cast<RENDER_TARGET>(i)));
					}
				}
			}
		}
		return "NONE";
	}

	GUID WKPDID_D3DDebugObjectNameT = { 0x429b8c22, 0x9188, 0x4b0c, 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 };

	namespace
	{
		uint64_t MipChainBytes(DXGI_FORMAT a_format, uint32_t a_width, uint32_t a_height, uint32_t a_depth, uint32_t a_mips)
		{
			uint64_t bytes = 0;
			for (uint32_t mip = 0; mip < std::max(a_mips, 1u); mip++) {
				size_t rowPitch = 0, slicePitch = 0;
				if (FAILED(DirectX::ComputePitch(a_format, std::max(a_width >> mip, 1u), std::max(a_height >> mip, 1u), rowPitch, slicePitch)))
					return 0;
				bytes += uint64_t(slicePitch) * std::max(a_depth >> mip, 1u);
			}
			return bytes;
		}
	}

	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...)
	{
		if (!Resource)
			return;

		char buffer[1024];
		va_list va;

		va_start(va, Format);
		int len = _vsnprintf_s(buffer, _TRUNCATE, Format, va);
		va_end(va);

		if (len < 0)
			len = static_cast<int>(strnlen(buffer, sizeof(buffer)));
		Resource->SetPrivateData(WKPDID_D3DDebugObjectNameT, static_cast<UINT>(len), buffer);
	}

	uint64_t ResourceBytes(ID3D11Resource* a_resource)
	{
		if (!a_resource)
			return 0;
		D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		a_resource->GetType(&dimension);
		switch (dimension) {
		case D3D11_RESOURCE_DIMENSION_BUFFER:
			{
				D3D11_BUFFER_DESC desc;
				static_cast<ID3D11Buffer*>(a_resource)->GetDesc(&desc);
				return desc.ByteWidth;
			}
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
			{
				D3D11_TEXTURE1D_DESC desc;
				static_cast<ID3D11Texture1D*>(a_resource)->GetDesc(&desc);
				return MipChainBytes(desc.Format, desc.Width, 1, 1, desc.MipLevels) * std::max(desc.ArraySize, 1u);
			}
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
			{
				D3D11_TEXTURE2D_DESC desc;
				static_cast<ID3D11Texture2D*>(a_resource)->GetDesc(&desc);
				return MipChainBytes(desc.Format, desc.Width, desc.Height, 1, desc.MipLevels) * std::max(desc.ArraySize, 1u) * std::max(desc.SampleDesc.Count, 1u);
			}
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
			{
				D3D11_TEXTURE3D_DESC desc;
				static_cast<ID3D11Texture3D*>(a_resource)->GetDesc(&desc);
				return MipChainBytes(desc.Format, desc.Width, desc.Height, desc.Depth, desc.MipLevels);
			}
		default:
			return 0;
		}
	}

	// Per-frame getters would otherwise retry a failed compile every frame.
	namespace
	{
		std::mutex shaderCompileFailuresMutex;
		std::unordered_set<std::string> shaderCompileFailures;
	}

	void ClearShaderCompileFailures()
	{
		std::lock_guard lock(shaderCompileFailuresMutex);
		shaderCompileFailures.clear();
	}

	// CompileShader's shaders share the main cache's persistent store, which survives cache invalidation.
	namespace
	{
		/**
		 * @brief CompileDedupe key for one compile, built like the main shader cache's: the preprocessed source
		 *        (without #line unless compiling with debug info), the entry point, the target and the flags.
		 * @return The key, or nullopt when the source cannot be read or preprocessed.
		 */
		std::optional<Util::ContentHash::Hash128> GetStoreKey(const wchar_t* filePath, const D3D_SHADER_MACRO* macros, ID3DInclude* include, const char* program, const char* programType, uint32_t flags)
		{
			std::ifstream file(filePath, std::ios::binary);
			if (!file.is_open())
				return std::nullopt;
			const std::string source{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

			winrt::com_ptr<ID3DBlob> preprocessed;
			winrt::com_ptr<ID3DBlob> errors;
			const auto sourceName = Util::WStringToString(filePath);
			if (FAILED(D3DPreprocess(source.data(), source.size(), sourceName.c_str(), macros, include, preprocessed.put(), errors.put())) || !preprocessed)
				return std::nullopt;

			std::string_view text(static_cast<const char*>(preprocessed->GetBufferPointer()), preprocessed->GetBufferSize());
			if (text.ends_with('\0'))
				text.remove_suffix(1);
			const auto keyText = (flags & D3DCOMPILE_DEBUG) ? std::string(text) : Util::CompileDedupe::StripLineDirectives(text);
			return Util::CompileDedupe::MakeKey({ keyText, program, programType, flags });
		}

		/** @brief True for the targets CreateShaderObject can turn into a shader: only those are read from or written to the cache. */
		bool IsCacheableTarget(const char* programType)
		{
			for (const auto* target : { "ps_5_0", "vs_5_0", "hs_5_0", "ds_5_0", "cs_5_0", "cs_4_0" })
				if (!_stricmp(programType, target))
					return true;
			return false;
		}

		/**
		 * @brief Creates the shader object for a CompileShader target.
		 * @return The device's result; a target without a creation path returns S_OK and no shader.
		 */
		HRESULT CreateShaderObject(ID3D11Device* device, const char* programType, ID3DBlob* blob, ID3D11DeviceChild** shader)
		{
			*shader = nullptr;
			const void* code = blob->GetBufferPointer();
			const SIZE_T size = blob->GetBufferSize();
			HRESULT result = S_OK;
			if (!_stricmp(programType, "ps_5_0")) {
				ID3D11PixelShader* regShader = nullptr;
				result = device->CreatePixelShader(code, size, nullptr, &regShader);
				*shader = regShader;
			} else if (!_stricmp(programType, "vs_5_0")) {
				ID3D11VertexShader* regShader = nullptr;
				result = device->CreateVertexShader(code, size, nullptr, &regShader);
				*shader = regShader;
			} else if (!_stricmp(programType, "hs_5_0")) {
				ID3D11HullShader* regShader = nullptr;
				result = device->CreateHullShader(code, size, nullptr, &regShader);
				*shader = regShader;
			} else if (!_stricmp(programType, "ds_5_0")) {
				ID3D11DomainShader* regShader = nullptr;
				result = device->CreateDomainShader(code, size, nullptr, &regShader);
				*shader = regShader;
			} else if (!_stricmp(programType, "cs_5_0") || !_stricmp(programType, "cs_4_0")) {
				ID3D11ComputeShader* regShader = nullptr;
				result = device->CreateComputeShader(code, size, nullptr, &regShader);
				*shader = regShader;
			}
			return result;
		}
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program, ID3DBlob** OutBlob)
	{
		if (OutBlob)
			*OutBlob = nullptr;
		auto device = globals::d3d::device;

		CustomInclude include;

		// Build defines (aka convert vector->D3DCONSTANT array)
		std::vector<D3D_SHADER_MACRO> macros;
		std::string str = Util::WStringToString(FilePath);

		for (auto& i : Defines) {
			if (i.first && _stricmp(i.first, "") != 0) {
				macros.push_back({ i.first, i.second });
			} else {
				logger::error("Failed to process shader defines for {}", str);
			}
		}

		const bool debugCompile = SIE::ShaderCache::IsDebugCompile();
		if (debugCompile) {
			macros.push_back({ "D3DCOMPILE_SKIP_OPTIMIZATION", "" });
			macros.push_back({ "D3DCOMPILE_DEBUG", "" });
		}
		auto shaderDefines = globals::state->GetDefines();
		if (!shaderDefines->empty()) {
			for (unsigned int i = 0; i < shaderDefines->size(); i++)
				macros.push_back({ shaderDefines->at(i).first.c_str(), shaderDefines->at(i).second.c_str() });
		}

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;
			for (const auto& [name, value] : feature->GetCommonShaderDefines()) {
				if (std::ranges::none_of(macros, [&](const auto& macro) { return name == macro.Name; }))
					macros.push_back({ name.data(), value.empty() ? nullptr : value.data() });
			}
		}
		if (!_stricmp(ProgramType, "ps_5_0"))
			macros.push_back({ "PSHADER", "" });
		else if (!_stricmp(ProgramType, "vs_5_0"))
			macros.push_back({ "VSHADER", "" });
		else if (!_stricmp(ProgramType, "hs_5_0"))
			macros.push_back({ "HULLSHADER", "" });
		else if (!_stricmp(ProgramType, "ds_5_0"))
			macros.push_back({ "DOMAINSHADER", "" });
		else if (!_stricmp(ProgramType, "cs_5_0"))
			macros.push_back({ "COMPUTESHADER", "" });
		else if (!_stricmp(ProgramType, "cs_4_0"))
			macros.push_back({ "COMPUTESHADER", "" });
		else if (!_stricmp(ProgramType, "cs_5_1"))
			macros.push_back({ "COMPUTESHADER", "" });
		else
			return nullptr;

		// Add null terminating entry
		macros.push_back({ "WINPC", "" });
		macros.push_back({ "DX11", "" });
		macros.push_back({ nullptr, nullptr });

		// Compiler setup
		uint32_t flags = !debugCompile ? (D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3) : D3DCOMPILE_DEBUG;
		if (globals::state->enablePartialPrecision.load(std::memory_order_relaxed))
			flags |= D3DCOMPILE_PARTIAL_PRECISION;
		if (globals::state->enableAvoidFlowControl.load(std::memory_order_relaxed))
			flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
		// Disk cache on = user is running shipped, known-good shaders — skip the fxc
		// validation pass to trim compile time. Disk cache off = dev workflow, keep
		// validation so malformed source produces a clean error instead of UB.
		if (globals::shaderCache->IsDiskCache())
			flags |= D3DCOMPILE_SKIP_VALIDATION;

		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> shaderErrors;

		const auto failureKey = std::format("{}|{}|{}|{}|{}", str, ProgramType, Program, flags, DefinesToString(macros));
		{
			std::lock_guard lock(shaderCompileFailuresMutex);
			if (shaderCompileFailures.contains(failureKey))
				return nullptr;
		}
		const auto recordFailure = [&]() {
			std::lock_guard lock(shaderCompileFailuresMutex);
			shaderCompileFailures.insert(failureKey);
		};

		if (!std::filesystem::exists(FilePath)) {
			logger::error("Failed to compile shader; {} does not exist", str);
			recordFailure();
			return nullptr;
		}
		std::optional<Util::ContentHash::Hash128> storeKey;
		if (globals::state->enableContentStore.load(std::memory_order_relaxed) && IsCacheableTarget(ProgramType))
			storeKey = GetStoreKey(FilePath, macros.data(), &include, Program, ProgramType, flags);

		if (const auto cachedBlob = storeKey ? SIE::SShaderCache::ReadContentStore(*storeKey) : nullptr) {
			ID3D11DeviceChild* shader = nullptr;
			if (SUCCEEDED(CreateShaderObject(device, ProgramType, cachedBlob.get(), &shader))) {
				Util::SetResourceName(shader, "%s:%s", str.c_str(), Program);
				logger::debug("Loaded {}:{} from the shader store", str, Program);
				if (OutBlob)
					cachedBlob.copy_to(OutBlob);
				return shader;
			}
			// An intact container can still hold bytecode the runtime rejects; compile from source, which overwrites the entry.
			logger::warn("Recompiling {}:{}: stored shader rejected by the device", str, Program);
		}

		logger::debug("Compiling {} with {}", str, DefinesToString(macros));
		if (FAILED(D3DCompileFromFile(FilePath, macros.data(), &include, Program, ProgramType, flags, 0, shaderBlob.put(), shaderErrors.put()))) {
			logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			recordFailure();
			return nullptr;
		}
		if (shaderErrors)
			logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));
		if (storeKey)
			SIE::SShaderCache::WriteContentStore(*storeKey, shaderBlob.get());

		ID3D11DeviceChild* shader = nullptr;
		DX::ThrowIfFailed(CreateShaderObject(device, ProgramType, shaderBlob.get(), &shader));
		Util::SetResourceName(shader, "%s:%s", str.c_str(), Program);
		if (OutBlob && shader)
			shaderBlob.copy_to(OutBlob);
		return shader;
	}

	bool SupportsMaterialAlpha(DXGI_FORMAT format)
	{
		static constexpr std::array formats{
			DXGI_FORMAT_BC2_UNORM,
			DXGI_FORMAT_BC3_UNORM,
			DXGI_FORMAT_BC7_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_SNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_B4G4R4A4_UNORM,
			DXGI_FORMAT_A8_UNORM,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_R16G16B16A16_UNORM,
			DXGI_FORMAT_R16G16B16A16_SNORM,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT
		};

		return std::ranges::contains(formats, DirectX::MakeLinear(format));
	}

	// RAII wrapper for D3D mapped resources
	class ScopedD3DMap
	{
	public:
		ScopedD3DMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP mapType, UINT mapFlags) :
			context_(context), resource_(resource), subresource_(subresource), mapped_(false)
		{
			if (context && resource) {
				HRESULT hr = context->Map(resource, subresource, mapType, mapFlags, &mappedResource_);
				mapped_ = SUCCEEDED(hr);
			}
		}

		~ScopedD3DMap()
		{
			if (mapped_ && context_ && resource_) {
				context_->Unmap(resource_, subresource_);
			}
		}

		// Non-copyable, non-movable for simplicity
		ScopedD3DMap(const ScopedD3DMap&) = delete;
		ScopedD3DMap& operator=(const ScopedD3DMap&) = delete;

		bool IsValid() const { return mapped_; }
		D3D11_MAPPED_SUBRESOURCE* GetMappedResource() { return mapped_ ? &mappedResource_ : nullptr; }

	private:
		ID3D11DeviceContext* context_;
		ID3D11Resource* resource_;
		UINT subresource_;
		bool mapped_;
		D3D11_MAPPED_SUBRESOURCE mappedResource_{};
	};

	// Helper function to validate highlight color components
	bool ValidateHighlightColor(const std::array<float, 4>& color)
	{
		for (size_t i = 0; i < 4; ++i) {
			if (color[i] < 0.0f || color[i] > 1.0f || !std::isfinite(color[i])) {
				return false;
			}
		}
		return true;
	}

	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor)
	{
		if (!isHighlighted || !texture)
			return;

		// Validate input color values
		if (!ValidateHighlightColor(highlightColor)) {
			logger::error("ApplyHighlightTintToTexture: Invalid highlight color values. All components must be finite and in range [0.0, 1.0]");
			return;
		}

		// Get texture description and validate dimensions
		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);

		// Performance consideration: warn about large textures (only once)
		const UINT pixelCount = desc.Width * desc.Height;
		const UINT largeTextureThreshold = 1024 * 1024;  // 1 megapixel
		if (pixelCount > largeTextureThreshold) {
			static Util::OnceFlag largeTextureWarning;
			if (largeTextureWarning.First())
				logger::warn("ApplyHighlightTintToTexture: Processing large texture ({}x{} = {} pixels). Consider using compute shader for better performance. This warning will only be shown once.",
					desc.Width, desc.Height, pixelCount);
		}  // Create a temporary staging texture to read from
		ID3D11Texture2D* stagingTexture = nullptr;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = globals::d3d::device->CreateTexture2D(&desc, nullptr, &stagingTexture);
		if (FAILED(hr)) {
			logger::error("ApplyHighlightTintToTexture: Failed to create staging texture (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			return;
		}

		// RAII wrapper ensures staging texture is always released
		std::unique_ptr<ID3D11Texture2D, void (*)(ID3D11Texture2D*)> stagingGuard(
			stagingTexture, [](ID3D11Texture2D* tex) { if (tex) tex->Release(); });

		// Copy the original texture to staging
		globals::d3d::context->CopyResource(stagingTexture, texture);

		// Use RAII wrapper for mapping/unmapping
		ScopedD3DMap scopedMap(globals::d3d::context, stagingTexture, 0, D3D11_MAP_READ_WRITE, 0);
		if (!scopedMap.IsValid()) {
			logger::error("ApplyHighlightTintToTexture: Failed to map staging texture");
			return;
		}

		D3D11_MAPPED_SUBRESOURCE* mapped = scopedMap.GetMappedResource();
		if (!mapped || !mapped->pData) {
			logger::error("ApplyHighlightTintToTexture: Invalid mapped resource data");
			return;
		}

		// Apply highlight tint to each pixel
		uint8_t* pixels = static_cast<uint8_t*>(mapped->pData);
		const float blendAlpha = highlightColor[3];

		for (UINT y = 0; y < desc.Height; ++y) {
			for (UINT x = 0; x < desc.Width; ++x) {
				uint8_t* pixel = pixels + (y * mapped->RowPitch + x * 4);

				// Only tint non-transparent pixels (alpha > 0)
				if (pixel[3] > 0) {
					// Apply configurable highlight tint
					// Blend the original color with the highlight color
					float originalR = pixel[0] / 255.0f;
					float originalG = pixel[1] / 255.0f;
					float originalB = pixel[2] / 255.0f;

					// Blend: original * (1 - alpha) + highlight * alpha
					pixel[0] = static_cast<uint8_t>((originalR * (1.0f - blendAlpha) + highlightColor[0] * blendAlpha) * 255.0f);
					pixel[1] = static_cast<uint8_t>((originalG * (1.0f - blendAlpha) + highlightColor[1] * blendAlpha) * 255.0f);
					pixel[2] = static_cast<uint8_t>((originalB * (1.0f - blendAlpha) + highlightColor[2] * blendAlpha) * 255.0f);
					// Alpha stays the same
				}
			}
		}

		// ScopedD3DMap destructor will automatically unmap the resource
		// Copy the modified texture back
		globals::d3d::context->CopyResource(texture, stagingTexture);

		// stagingGuard destructor will automatically release the staging texture
	}

	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV)
	{
		// Input validation
		if (!device) {
			logger::error("CreateOverlayTextureAndRTV: device parameter is null");
			return E_INVALIDARG;
		}

		if (!outTex || !outRTV) {
			logger::error("CreateOverlayTextureAndRTV: output parameters cannot be null");
			return E_INVALIDARG;
		}

		if (width <= 0 || height <= 0) {
			logger::error("CreateOverlayTextureAndRTV: invalid dimensions ({}x{})", width, height);
			return E_INVALIDARG;
		}

		// Initialize output parameters
		*outTex = nullptr;
		*outRTV = nullptr;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		HRESULT hr = device->CreateTexture2D(&desc, nullptr, outTex);
		if (FAILED(hr)) {
			logger::error("CreateOverlayTextureAndRTV: Failed to create texture (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			return hr;
		}

		hr = device->CreateRenderTargetView(*outTex, nullptr, outRTV);
		if (FAILED(hr)) {
			logger::error("CreateOverlayTextureAndRTV: Failed to create render target view (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			// Clean up the texture if RTV creation failed
			if (*outTex) {
				(*outTex)->Release();
				*outTex = nullptr;
			}
			return hr;
		}

		return S_OK;
	}

	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex)
	{
		// Input validation
		if (!device) {
			logger::error("SaveTextureToFile: device parameter is null");
			return E_INVALIDARG;
		}

		if (!context) {
			logger::error("SaveTextureToFile: context parameter is null");
			return E_INVALIDARG;
		}

		if (!tex) {
			logger::error("SaveTextureToFile: texture paramater is null");
			return E_INVALIDARG;
		}

		if (path.empty()) {
			logger::error("SaveTextureToFile: path parameter is empty");
			return E_INVALIDARG;
		}

		namespace fs = std::filesystem;

		DirectX::ScratchImage cpuImage;
		if (const auto hr = CaptureTexture(device, context, tex, cpuImage); FAILED(hr))
			return hr;

		const auto parent = path.parent_path();
		std::error_code ec;
		if (!parent.empty() && !fs::exists(parent, ec)) {
			if (!fs::create_directories(parent, ec)) {
				logger::error("SaveTextureToFile: failed to create directories");
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
		}

		const std::wstring tmp = path.wstring().append(L".tmp");
		if (const auto hr = SaveToDDSFile(cpuImage.GetImages(), cpuImage.GetImageCount(), cpuImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, tmp.c_str()); FAILED(hr)) {
			logger::error("SaveTextureToFile: failed to save to file");
			fs::remove(tmp);
			return hr;
		}

		fs::rename(tmp, path, ec);
		if (ec) {
			fs::remove(path, ec);
			if (ec) {
				logger::error("SaveTextureToFile: failed to remove existing file");
				fs::remove(tmp);
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
			fs::rename(tmp, path, ec);
			if (ec) {
				logger::error("SaveTextureToFile: failed to rename file");
				fs::remove(tmp);
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
		}

		return S_OK;
	}

	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV)
	{
		// Input validation
		if (!device) {
			logger::error("LoadTextureFromFile: device parameter is null");
			return E_INVALIDARG;
		}

		if (path.empty()) {
			logger::error("LoadTextureFromFile: path parameter is empty");
			return E_INVALIDARG;
		}

		*outTex = nullptr;
		*outSRV = nullptr;

		ID3D11Resource* resource;
		if (const auto hr = DirectX::CreateDDSTextureFromFile(device, path.wstring().c_str(), &resource, outSRV); FAILED(hr)) {
			logger::error("LoadTextureFromFile: failed to load resource from file");
			return hr;
		}

		const auto hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(outTex));
		resource->Release();
		resource = nullptr;

		if (FAILED(hr)) {
			logger::error("LoadTextureFromFile: failed to query texture interface");
			(*outSRV)->Release();
			*outSRV = nullptr;
			*outTex = nullptr;
			return hr;
		}

		return S_OK;
	}
}  // namespace Util

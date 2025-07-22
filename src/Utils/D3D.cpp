#include "D3D.h"

#include "State.h"
#include "Utils/Format.h"

#include <d3dcompiler.h>

namespace Util
{
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
				for (int i = 0; i < RENDER_TARGET::kTOTAL; i++) {
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
				for (int i = 0; i < RENDER_TARGET::kTOTAL; i++) {
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

	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...)
	{
		if (!Resource)
			return;

		char buffer[1024];
		va_list va;

		va_start(va, Format);
		int len = _vsnprintf_s(buffer, _TRUNCATE, Format, va);
		va_end(va);

		Resource->SetPrivateData(WKPDID_D3DDebugObjectNameT, len, buffer);
	}

	struct CustomInclude : public ID3DInclude
	{
		HRESULT Open([[maybe_unused]] D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, [[maybe_unused]] LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
		{
			std::filesystem::path filePath = pFileName;
			filePath = L"Data\\Shaders" / filePath;

			std::ifstream file(filePath, std::ios::binary);
			if (!file.is_open()) {
				*ppData = NULL;
				*pBytes = 0;
				return E_FAIL;
			}

			// Get filesize
			file.seekg(0, std::ios::end);
			UINT size = static_cast<UINT>(file.tellg());
			file.seekg(0, std::ios::beg);

			// Create buffer and read file
			char* data = new char[size];
			file.read(data, size);
			*ppData = data;
			*pBytes = size;
			return S_OK;
		}

		HRESULT Close(LPCVOID pData) override
		{
			if (pData)
				delete[] pData;
			return S_OK;
		}
	};

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program)
	{
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

		if (REL::Module::IsVR())
			macros.push_back({ "VR", "" });
		if (globals::state->IsDeveloperMode()) {
			macros.push_back({ "D3DCOMPILE_SKIP_OPTIMIZATION", "" });
			macros.push_back({ "D3DCOMPILE_DEBUG", "" });
		}
		auto shaderDefines = globals::state->GetDefines();
		if (!shaderDefines->empty()) {
			for (unsigned int i = 0; i < shaderDefines->size(); i++)
				macros.push_back({ shaderDefines->at(i).first.c_str(), shaderDefines->at(i).second.c_str() });
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
		uint32_t flags = !globals::state->IsDeveloperMode() ? (D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3) : D3DCOMPILE_DEBUG;

		ID3DBlob* shaderBlob;
		ID3DBlob* shaderErrors;

		if (!std::filesystem::exists(FilePath)) {
			logger::error("Failed to compile shader; {} does not exist", str);
			return nullptr;
		}
		logger::debug("Compiling {} with {}", str, DefinesToString(macros));
		if (FAILED(D3DCompileFromFile(FilePath, macros.data(), &include, Program, ProgramType, flags, 0, &shaderBlob, &shaderErrors))) {
			logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}
		if (shaderErrors)
			logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));
		if (!_stricmp(ProgramType, "ps_5_0")) {
			ID3D11PixelShader* regShader;
			device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader);
			return regShader;
		} else if (!_stricmp(ProgramType, "vs_5_0")) {
			ID3D11VertexShader* regShader;
			device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader);
			return regShader;
		} else if (!_stricmp(ProgramType, "hs_5_0")) {
			ID3D11HullShader* regShader;
			device->CreateHullShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader);
			return regShader;
		} else if (!_stricmp(ProgramType, "ds_5_0")) {
			ID3D11DomainShader* regShader;
			device->CreateDomainShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader);
			return regShader;
		} else if (!_stricmp(ProgramType, "cs_5_0")) {
			ID3D11ComputeShader* regShader;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		} else if (!_stricmp(ProgramType, "cs_4_0")) {
			ID3D11ComputeShader* regShader;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}

		return nullptr;
	}

	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor)
	{
		if (!isHighlighted || !texture)
			return;

		// Create a temporary staging texture to read from
		ID3D11Texture2D* stagingTexture = nullptr;
		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

		if (FAILED(globals::d3d::device->CreateTexture2D(&desc, nullptr, &stagingTexture)))
			return;

		// Copy the original texture to staging
		globals::d3d::context->CopyResource(stagingTexture, texture);

		// Map the staging texture to read/write pixels
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(globals::d3d::context->Map(stagingTexture, 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
			// Apply highlight tint to each pixel
			uint8_t* pixels = static_cast<uint8_t*>(mapped.pData);
			for (UINT y = 0; y < desc.Height; ++y) {
				for (UINT x = 0; x < desc.Width; ++x) {
					uint8_t* pixel = pixels + (y * mapped.RowPitch + x * 4);

					// Only tint non-transparent pixels (alpha > 0)
					if (pixel[3] > 0) {
						// Apply configurable highlight tint
						// Blend the original color with the highlight color
						float originalR = pixel[0] / 255.0f;
						float originalG = pixel[1] / 255.0f;
						float originalB = pixel[2] / 255.0f;

						// Blend: original * (1 - alpha) + highlight * alpha
						float blendAlpha = highlightColor[3];
						pixel[0] = static_cast<uint8_t>((originalR * (1.0f - blendAlpha) + highlightColor[0] * blendAlpha) * 255.0f);
						pixel[1] = static_cast<uint8_t>((originalG * (1.0f - blendAlpha) + highlightColor[1] * blendAlpha) * 255.0f);
						pixel[2] = static_cast<uint8_t>((originalB * (1.0f - blendAlpha) + highlightColor[2] * blendAlpha) * 255.0f);
						// Alpha stays the same
					}
				}
			}

			globals::d3d::context->Unmap(stagingTexture, 0);

			// Copy the modified texture back
			globals::d3d::context->CopyResource(texture, stagingTexture);
		}

		stagingTexture->Release();
	}

	void CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		device->CreateTexture2D(&desc, nullptr, outTex);
		if (*outTex) {
			device->CreateRenderTargetView(*outTex, nullptr, outRTV);
		}
	}
}  // namespace Util

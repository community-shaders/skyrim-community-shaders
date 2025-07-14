#include "ShaderCompiler.h"

#include <d3dcompiler.h>

namespace ShaderCompiler
{
	ID3D11PixelShader* RegisterPixelShader(const std::wstring& a_filePath)
	{
		static REL::Relocation<ID3D11Device**> g_ID3D11Device{ RELOCATION_ID(524729, 411348) };

		ID3DBlob* shaderBlob = nullptr;

		if (FAILED(D3DReadFileToBlob(a_filePath.c_str(), &shaderBlob))) {
			logger::error("Pixel shader load failed:\n{}", "File does not exist or is invalid");

			if (shaderBlob)
				shaderBlob->Release();

			return nullptr;
		}

		logger::debug("shader load succeeded");

		logger::debug("registering shader");

		ID3D11PixelShader* regShader;

		if (FAILED((*g_ID3D11Device)->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader))) {
			logger::error("pixel shader registration failed");

			shaderBlob->Release();

			return nullptr;
		}

		logger::debug("shader registration succeeded");

		return regShader;
	}

	ID3D11PixelShader* CompileAndRegisterPixelShader(const std::wstring& a_filePath)
	{
		return static_cast<ID3D11PixelShader*>(Util::CompileShader(a_filePath.data(), {}, "ps_5_0"));
	}
}
#pragma once

#include "ShaderUtils.h"
#include <dxcapi.h>
#include <shlwapi.h>

namespace ShaderUtils 
{
	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, const wchar_t* Target, const wchar_t* EntryPoint)
	{
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
			logger::error("Failed to initialize COM");
			return;
		}

		std::string str = Util::WStringToString(FilePath);

		if (!std::filesystem::exists(FilePath)) {
			logger::error("Failed to compile shader; {} does not exist", str);
			return;
		}

		winrt::com_ptr<IDxcUtils> utils;
		if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
			logger::error("Failed to create DxcUtils");
			return;
		}

		winrt::com_ptr<IDxcCompiler3> compiler;
		if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
			logger::error("Failed to create DxcCompiler");
			return;
		}

		winrt::com_ptr<IDxcBlobEncoding> source;
		if (FAILED(utils->LoadFile(FilePath, nullptr, source.put()))) {
			logger::error("Failed to load shader file");
			return;
		}

		winrt::com_ptr<IDxcIncludeHandler> baseHandler;
		if (FAILED(utils->CreateDefaultIncludeHandler(baseHandler.put()))) {
			logger::error("Failed to create Include Handler");
			return;
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = source->GetBufferPointer();
		sourceBuffer.Size = source->GetBufferSize();
		sourceBuffer.Encoding = DXC_CP_ACP;

		LPCWSTR args[] = {
			FilePath,
			L"-E", EntryPoint,
			L"-T", Target,
			L"-I", L"Data\\Shaders",
			L"-Zi",
			L"-Qstrip_reflect",
			L"-O3",
		};

		winrt::com_ptr<IDxcResult> result;
		if (FAILED(compiler->Compile(&sourceBuffer, args, _countof(args), baseHandler.get(), IID_PPV_ARGS(&result)))) {
			logger::error("Compile call failed");
			return;
		}

		winrt::com_ptr<IDxcBlobUtf8> errors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
		if (errors && errors->GetStringLength() > 0) {
			logger::error("Shader compilation errors: {}", errors->GetStringPointer());
		}

		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr))) {
			logger::error("Failed to get compiled shader");
			return;
		}
	}
};
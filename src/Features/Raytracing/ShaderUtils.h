#pragma once

#include <dxcapi.h>

namespace ShaderUtils
{
	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, const wchar_t* Target = L"lib_6_5", const wchar_t* EntryPoint = L"main");
};
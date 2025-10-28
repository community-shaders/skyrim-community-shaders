#include <dxcapi.h>

namespace ShaderUtils 
{
	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, const wchar_t* Target, const wchar_t* EntryPoint = L"main");
};
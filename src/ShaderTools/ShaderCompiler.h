#pragma once

namespace ShaderCompiler
{
	ID3D11PixelShader* RegisterPixelShader(const std::wstring& a_filePath);
	ID3D11PixelShader* CompileAndRegisterPixelShader(const std::wstring& a_filePath);
}

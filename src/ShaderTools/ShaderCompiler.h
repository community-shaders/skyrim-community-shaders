#pragma once

namespace ShaderCompiler
{
	/**
	 * @brief Loads a precompiled pixel shader from disk and registers it with the D3D11 device.
	 * @param a_filePath Path to the compiled shader blob (.cso) file.
	 * @return Registered pixel shader, or nullptr on failure.
	 */
	ID3D11PixelShader* RegisterPixelShader(const std::wstring& a_filePath);

	/**
	 * @brief Compiles an HLSL source file and registers the resulting pixel shader.
	 * @param a_filePath Path to the HLSL source file.
	 * @return Compiled and registered pixel shader, or nullptr on failure.
	 */
	ID3D11PixelShader* CompileAndRegisterPixelShader(const std::wstring& a_filePath);
}

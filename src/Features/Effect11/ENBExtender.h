#pragma once

#include <d3dcompiler.h>
#include <filesystem>
#include <string>

/**
 * Preprocesses HLSL source for ENB Extender compatibility.
 *
 * @param content HLSL source text to preprocess.
 * @returns Preprocessed HLSL source text.
 */

/**
 * Custom ID3DInclude implementation that resolves and supplies included shader files
 * using a configurable base directory for ENB Extender compatibility.
 */

/**
 * Create an IncludeHandler that resolves include paths relative to the provided base directory.
 *
 * @param basePath Base directory used to resolve include file paths.
 */

/**
 * Load an included file's contents for the shader compiler.
 *
 * On success, sets *ppData to point to the file contents buffer and *pBytes to its size.
 *
 * @param IncludeType Type of include request (local or system).
 * @param pFileName Name of the file to include.
 * @param pParentData Data pointer of the parent include (if provided by the caller).
 * @param ppData Receives pointer to the loaded file data on success.
 * @param pBytes Receives size of the loaded file data on success.
 * @returns S_OK on success, or an HRESULT error code on failure.
 */

/**
 * Release resources associated with a previously opened include data pointer.
 *
 * @param pData Pointer previously returned via Open.
 * @returns S_OK on success, or an HRESULT error code on failure.
 */
namespace ENBExtender
{
	// Preprocess HLSL source code for ENB Extender compatibility
	std::string PreprocessSource(const std::string& content);

	// Custom ID3DInclude implementation for ENB Extender compatibility
	class IncludeHandler : public ID3DInclude
	{
	public:
		IncludeHandler(const std::filesystem::path& basePath);

		HRESULT __stdcall Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override;
		HRESULT __stdcall Close(LPCVOID pData) override;

	private:
		std::filesystem::path basePath;
	};
}

#pragma once

#include <d3dcompiler.h>
#include <filesystem>
#include <string>

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

#include "WinApi.h"

namespace Util
{
	std::optional<REL::Version> GetDllVersion(const std::wstring& dllPath)
	{
		DWORD handle = 0;
		DWORD size = GetFileVersionInfoSize(dllPath.c_str(), &handle);
		if (size == 0) {
			return std::nullopt;
		}

		std::vector<BYTE> buffer(size);
		if (!GetFileVersionInfo(dllPath.c_str(), handle, size, buffer.data())) {
			return std::nullopt;
		}

		VS_FIXEDFILEINFO* fileInfo = nullptr;
		UINT fileInfoSize = 0;
		if (!VerQueryValue(buffer.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize)) {
			return std::nullopt;
		}

		if (fileInfoSize == sizeof(VS_FIXEDFILEINFO)) {
			return REL::Version(HIWORD(fileInfo->dwFileVersionMS), LOWORD(fileInfo->dwFileVersionMS), HIWORD(fileInfo->dwFileVersionLS), LOWORD(fileInfo->dwFileVersionLS));
		}

		return std::nullopt;
	}
}  // namespace Util

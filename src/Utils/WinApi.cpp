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

	uint32_t GetPerformanceCoreCount()
	{
		// Cache the result — CPU topology never changes at runtime.
		// C++11 guarantees thread-safe initialisation of static locals.
		static const uint32_t cached = []() -> uint32_t {
			const uint32_t fallback = std::max(1u, std::thread::hardware_concurrency());

			DWORD size = 0;
			GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
				return fallback;

			std::vector<uint8_t> buf(size);
			auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
			if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size))
				return fallback;

			// First pass: find the highest efficiency class present.
			BYTE maxClass = 0;
			for (DWORD offset = 0; offset < size;) {
				auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
				if (entry->Processor.EfficiencyClass > maxClass)
					maxClass = entry->Processor.EfficiencyClass;
				offset += entry->Size;
			}

			// Second pass: count logical processors on those (P-)cores.
			uint32_t count = 0;
			for (DWORD offset = 0; offset < size;) {
				auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
				if (entry->Processor.EfficiencyClass == maxClass) {
					for (WORD g = 0; g < entry->Processor.GroupCount; ++g)
						count += static_cast<uint32_t>(std::popcount(entry->Processor.GroupMask[g].Mask));
				}
				offset += entry->Size;
			}

			return count > 0 ? count : fallback;
		}();
		return cached;
	}
}  // namespace Util

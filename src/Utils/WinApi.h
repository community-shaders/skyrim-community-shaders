#pragma once

namespace Util
{
	/**
	 * @brief Retrieve the file version of a DLL using the Win32 version-info API.
	 * @param dllPath Absolute path to the DLL file.
	 * @return The parsed version, or std::nullopt if version info is unavailable.
	 */
	std::optional<REL::Version> GetDllVersion(const std::wstring& dllPath);

	/**
	 * @brief Get the number of logical processors on the highest-efficiency cores.
	 *
	 * On Intel hybrid CPUs this returns only P-core logical processors.
	 * On non-hybrid CPUs all cores share the same efficiency class, so this
	 * returns std::thread::hardware_concurrency(). Falls back to
	 * hardware_concurrency() on any API failure. The result is cached.
	 *
	 * @return The logical processor count for performance cores.
	 */
	uint32_t GetPerformanceCoreCount();
}  // namespace Util

// string and printing related helpers

#pragma once

namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version);

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines);
	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines);

	/**
	 * @brief Normalizes a file path by replacing backslashes with forward slashes, 
	 *        removing redundant slashes, and converting all characters to lowercase.
	 * 
	 * This function ensures that the file path uses consistent forward slashes
	 * (`/`), eliminates consecutive slashes (`//`), and converts all characters 
	 * in the path to lowercase for case-insensitive comparisons.
	 * 
	 * @param a_path The original file path to be normalized.
	 * @return A normalized file path as a lowercase string with single forward slashes.
	 */
	std::string FixFilePath(const std::string& a_path);
	std::string WStringToString(const std::wstring& wideString);
}  // namespace Util
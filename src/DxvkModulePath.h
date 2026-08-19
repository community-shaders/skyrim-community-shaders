#pragma once

#include <Windows.h>

#include <filesystem>
#include <functional>

namespace DxvkLoader::Detail
{
	using ModuleFileNameQuery = std::function<DWORD(HMODULE, wchar_t*, DWORD)>;

	/** Resolves a module path with a growing buffer. Empty means the query failed. */
	std::filesystem::path ResolveModulePath(HMODULE a_module, const ModuleFileNameQuery& a_query);
}

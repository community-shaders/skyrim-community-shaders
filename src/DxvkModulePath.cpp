#include "DxvkModulePath.h"

#include <limits>
#include <vector>

namespace DxvkLoader::Detail
{
	std::filesystem::path ResolveModulePath(HMODULE a_module, const ModuleFileNameQuery& a_query)
	{
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;) {
			const DWORD capacity = static_cast<DWORD>(buffer.size());
			const DWORD length = a_query(a_module, buffer.data(), capacity);
			if (length == 0)
				return {};
			if (length < capacity - 1u)
				return std::filesystem::path(buffer.data(), buffer.data() + length);

			if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) / 2u)
				return {};
			buffer.resize(buffer.size() * 2u);
		}
	}
}

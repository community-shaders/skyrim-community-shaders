#include <catch2/catch_test_macros.hpp>

#include "DxvkModulePath.h"

#include <string>

TEST_CASE("DXVK module path grows beyond MAX_PATH")
{
	const std::wstring expected = L"C:\\" + std::wstring(MAX_PATH * 2u, L'x') + L"\\CommunityShaders.dll";
	uint32_t calls = 0;
	const auto result = DxvkLoader::Detail::ResolveModulePath(nullptr,
		[&](HMODULE, wchar_t* a_buffer, DWORD a_capacity) -> DWORD {
			++calls;
			if (a_capacity <= expected.size()) {
				if (a_capacity)
					a_buffer[a_capacity - 1u] = L'\0';
				return a_capacity;
			}
			std::copy(expected.begin(), expected.end(), a_buffer);
			a_buffer[expected.size()] = L'\0';
			return static_cast<DWORD>(expected.size());
		});

	CHECK(result == std::filesystem::path(expected));
	CHECK(calls >= 2u);
}

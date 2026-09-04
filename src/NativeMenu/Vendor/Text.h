#pragma once

// Adapted from NativeSystemMenuFramework (https://github.com/RoseEden30/NativeSystemMenuFramework)
// License: GPL-3.0-or-later

#include <unordered_set>

namespace NativeMenu::Vendor::Text
{
	inline RE::GFxValue MakeGFxString(const std::string& a_utf8)
	{
		static std::unordered_set<std::wstring> interned;

		std::wstring wide;
		const int    wideLen = MultiByteToWideChar(CP_UTF8, 0, a_utf8.c_str(), -1, nullptr, 0);
		if (wideLen > 1) {
			wide.resize(static_cast<std::size_t>(wideLen) - 1);
			MultiByteToWideChar(CP_UTF8, 0, a_utf8.c_str(), -1, wide.data(), wideLen);
		}

		return RE::GFxValue(interned.insert(std::move(wide)).first->c_str());
	}
}

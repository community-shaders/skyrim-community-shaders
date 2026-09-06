#pragma once

// Adapted from NativeSystemMenuFramework (https://github.com/RoseEden30/NativeSystemMenuFramework)
// License: GPL-3.0-or-later

#include <functional>

namespace NativeMenu::Vendor::ListRows
{
	void Ensure(RE::GFxValue& a_list, std::uint32_t a_needed, const char* a_tag,
		const std::function<void(RE::GFxValue&)>& a_setup = {});
}

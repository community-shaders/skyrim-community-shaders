#pragma once

#include <REL/REL.h>
#include <SKSE/Version.h>

namespace Util::VersionedRelocation
{
	/** True on Skyrim AE 1.7.99 or newer. */
	[[nodiscard]] inline bool IsAtLeastAE1799() noexcept
	{
		return REL::Module::IsAE() && REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99);
	}

	/**
	 * Select a value for SE, pre-1.7 AE, or 1.7.99-or-newer AE.
	 */
	template <class T>
	[[nodiscard]] T Select(T a_se, T a_ae, T a_ae1799) noexcept
	{
		if (IsAtLeastAE1799()) {
			return a_ae1799;
		}
		return REL::Module::IsAE() ? a_ae : a_se;
	}

	/** Resolve an Address Library ID whose numeric ID changed in Skyrim 1.7.99. */
	[[nodiscard]] inline std::uintptr_t ResolveID(
		std::uint64_t a_se,
		std::uint64_t a_ae,
		std::uint64_t a_ae1799)
	{
		if (IsAtLeastAE1799()) {
			return REL::ID(a_ae1799).address();
		}
		return REL::RelocationID(a_se, a_ae).address();
	}

	/** @brief True if a_callSite holds a rel32 CALL to a_target, i.e. no other plugin or runtime change owns it. */
	[[nodiscard]] inline bool IsCallTo(std::uintptr_t a_callSite, std::uintptr_t a_target) noexcept
	{
		if (!REL::verify_code(a_callSite, REL::make_pattern<"E8 ?? ?? ?? ??">()))
			return false;
		std::int32_t displacement{};
		std::memcpy(std::addressof(displacement), reinterpret_cast<const void*>(a_callSite + 1), sizeof(displacement));
		return a_callSite + 5 + displacement == a_target;
	}
}

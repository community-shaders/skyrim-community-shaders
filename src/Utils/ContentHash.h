#pragma once

#include <xxhash.h>

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

/// Fast non-cryptographic XXH3-128 hashing for shader cache keys.
namespace Util::ContentHash
{
	struct Hash128
	{
		uint64_t high = 0;
		uint64_t low = 0;

		bool operator==(const Hash128&) const = default;

		/** @brief 32 lowercase hex digits, high word first. */
		std::string ToHex() const
		{
			return std::format("{:016x}{:016x}", high, low);
		}
	};

	inline Hash128 HashBytes(const void* a_data, size_t a_size)
	{
		const XXH128_hash_t hash = XXH3_128bits(a_data, a_size);
		return { hash.high64, hash.low64 };
	}

	inline Hash128 HashString(std::string_view a_text)
	{
		return HashBytes(a_text.data(), a_text.size());
	}

	/// Order-sensitive combination of two hashes.
	inline Hash128 CombineHashes(const Hash128& a_first, const Hash128& a_second)
	{
		const std::array<uint64_t, 4> buffer{ a_first.high, a_first.low, a_second.high, a_second.low };
		return HashBytes(buffer.data(), buffer.size() * sizeof(uint64_t));
	}
}

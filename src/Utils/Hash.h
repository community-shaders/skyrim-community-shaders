#pragma once

#include <cstddef>

namespace Util
{
	/** @brief Mixes value into signature, boost::hash_combine style. */
	inline void CombineHash(std::size_t& signature, std::size_t value) noexcept
	{
		signature ^= value + 0x9E3779B9u + (signature << 6) + (signature >> 2);
	}
}

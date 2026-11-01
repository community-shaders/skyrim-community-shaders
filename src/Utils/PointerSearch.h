#pragma once

#include <algorithm>
#include <cstdint>
#include <immintrin.h>
#include <intrin.h>

#if defined(__clang__)
#	define CS_TARGET_AVX2 __attribute__((target("avx2")))
#else
#	define CS_TARGET_AVX2
#endif

namespace Util
{
	namespace detail
	{
		/** @brief True when the CPU and the operating system both support AVX2. */
		[[nodiscard]] inline bool HasAvx2()
		{
			static const bool supported = [] {
				int regs[4]{};
				__cpuidex(regs, 0, 0);
				if (regs[0] < 7)
					return false;
				__cpuidex(regs, 1, 0);
				constexpr int kOsXsave = 1 << 27;
				constexpr int kAvx = 1 << 28;
				if ((regs[2] & kOsXsave) == 0 || (regs[2] & kAvx) == 0)
					return false;
				constexpr unsigned long long kXmmYmmState = 0x6;
				if ((_xgetbv(0) & kXmmYmmState) != kXmmYmmState)
					return false;
				__cpuidex(regs, 7, 0);
				constexpr int kAvx2 = 1 << 5;
				return (regs[1] & kAvx2) != 0;
			}();
			return supported;
		}

		/** @brief One element at a time form of ContainsPointer. */
		[[nodiscard]] inline bool ContainsPointerScalar(const void* const* a_data, std::uint32_t a_size, const void* a_needle)
		{
			return std::find(a_data, a_data + a_size, a_needle) != a_data + a_size;
		}

#if defined(_MSC_VER) && !defined(__clang__)
#	pragma warning(push)
#	pragma warning(disable: 4752)  // AVX2 is gated at runtime by HasAvx2, not by /arch
#endif
		/** @brief AVX2 form of ContainsPointer. Callers must check HasAvx2 first. */
		[[nodiscard]] CS_TARGET_AVX2 inline bool ContainsPointerAvx2(const void* const* a_data, std::uint32_t a_size, const void* a_needle)
		{
			constexpr std::uint32_t kLanes = 4;
			const auto needle = _mm256_set1_epi64x(static_cast<long long>(reinterpret_cast<std::uintptr_t>(a_needle)));
			std::uint32_t i = 0;
			for (; i + kLanes <= a_size; i += kLanes) {
				const auto equal = _mm256_cmpeq_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a_data + i)), needle);
				if (_mm256_movemask_pd(_mm256_castsi256_pd(equal)))
					return true;
			}
			return ContainsPointerScalar(a_data + i, a_size - i, a_needle);
		}
#if defined(_MSC_VER) && !defined(__clang__)
#	pragma warning(pop)
#endif
	}

	/** @brief True when a_needle is one of the a_size pointers in a_data. */
	[[nodiscard]] inline bool ContainsPointer(const void* const* a_data, std::uint32_t a_size, const void* a_needle)
	{
		return detail::HasAvx2() ? detail::ContainsPointerAvx2(a_data, a_size, a_needle) : detail::ContainsPointerScalar(a_data, a_size, a_needle);
	}
}

#undef CS_TARGET_AVX2

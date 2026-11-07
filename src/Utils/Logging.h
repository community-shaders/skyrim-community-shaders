#pragma once

#include "Utils/Hash.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace Util
{
	/** @brief Thread-safe first-use flag that can be re-armed. */
	class OnceFlag
	{
	public:
		/** @brief True the first time after construction or Reset(). */
		bool First() noexcept
		{
			return !fired.exchange(true, std::memory_order_relaxed);
		}

		/** @brief Re-arms the flag. */
		void Reset() noexcept
		{
			fired.store(false, std::memory_order_relaxed);
		}

	private:
		std::atomic<bool> fired{ false };
	};

	/** @brief Thread-safe first-use tracking for hashed key combinations. */
	class OnceSet
	{
	public:
		/** @brief True the first time a key combination is seen. */
		template <class... Keys>
		bool First(const Keys&... keys)
		{
			std::size_t signature = 0;
			(CombineHash(signature, std::hash<Keys>{}(keys)), ...);
			std::scoped_lock lock(mutex);
			return seen.insert(signature).second;
		}

	private:
		std::mutex mutex;
		std::unordered_set<std::size_t> seen;
	};

	/** @brief Thread-safe gate: first call, then once per interval frames. */
	class FrameThrottle
	{
	public:
		explicit constexpr FrameThrottle(std::uint32_t intervalFrames) noexcept :
			interval(intervalFrames) {}

		/** @brief True on the first call or past the interval since last true. */
		bool Ready(std::uint32_t frame) noexcept
		{
			auto state = last.load(std::memory_order_relaxed);
			if ((state & kFired) && frame - static_cast<std::uint32_t>(state) <= interval)
				return false;
			return last.compare_exchange_strong(state, kFired | frame, std::memory_order_relaxed);
		}

	private:
		static constexpr std::uint64_t kFired = 1ull << 32;

		std::uint32_t interval;
		std::atomic<std::uint64_t> last{ 0 };
	};
}

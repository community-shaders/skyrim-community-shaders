#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

// Pure decision logic for skipping redundant per-frame light attach walks, kept free of engine types.
namespace LightAttachGate
{
	/** @brief Everything that decides what an attach walk produces for one light. */
	using Signature = std::array<std::uint32_t, 20>;

	/** @brief Remembers each light's state at its last attach walk and decides when the next walk can be skipped. */
	class Tracker
	{
	public:
		/** @brief Lights tracked at once; reaching it empties the table, which only costs every light one extra walk. */
		static constexpr std::size_t kMaxTrackedLights = 8192;

		/**
		 * @brief True when a_light needs an attach walk on a_frame: it is new, its signature changed, or its staggered refresh is due.
		 * @param a_refreshFrames Longest run of frames an unchanged light is skipped; 0 never skips.
		 */
		bool ShouldWalk(const void* a_light, const Signature& a_signature, std::uint64_t a_frame, std::uint32_t a_refreshFrames)
		{
			if (a_refreshFrames == 0)
				return true;
			if (entries.size() >= kMaxTrackedLights)
				entries.clear();
			const auto [it, inserted] = entries.try_emplace(a_light);
			Entry& entry = it->second;
			if (inserted) {
				entry.phase = nextPhase++;
				return true;
			}
			if (entry.signature != a_signature)
				return true;
			return (a_frame + entry.phase) % a_refreshFrames == 0;
		}

		/** @brief Stores a_light's state after a walk, so the next frame compares against what the walk left behind. */
		void RecordWalk(const void* a_light, const Signature& a_signature)
		{
			entries[a_light].signature = a_signature;
		}

		/** @brief Drops a_light, so its next visit is walked. For a light the gate stops handling, such as one that becomes a shadow light. */
		void Forget(const void* a_light) { entries.erase(a_light); }

		void Clear() noexcept { entries.clear(); }
		[[nodiscard]] std::size_t Size() const noexcept { return entries.size(); }

	private:
		struct Entry
		{
			Signature signature{};
			std::uint32_t phase = 0;
		};

		std::unordered_map<const void*, Entry> entries;
		std::uint32_t nextPhase = 0;
	};
}

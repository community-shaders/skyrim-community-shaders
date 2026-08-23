#pragma once

#include <bit>
#include <cstdint>

// CDO4-001 phase 2, item 5: the minimal common recorder.
//
// This is the COMPARATOR, not the instrument. It is present in every arm of the
// non-interference matrix and is identical in all of them; the telemetry under
// test is the intervention. Recording only in the instrumented arm would compare
// a stream against nothing.
//
// Two constraints follow from that, and they shape the whole design:
//
// 1. It may only observe things that exist in EVERY arm. In particular it cannot
//    record pass IDs or writer events, because arm A has no ImageSpace wrappers
//    at all - FrameAnnotations::OnPostPostLoad returns before installing them
//    when frame annotations are off. A recorder that needed them would be
//    comparing two different populations.
//
// 2. It must be cheap enough that its own cost is negligible AND constant across
//    arms. It therefore emits CANONICAL HASHES rather than payloads: comparing
//    two runs is comparing hash sequences, not diffing megabytes.
//
// The hashes are split by category so a mismatch localizes. "Frame 812 differs"
// is nearly useless; "frame 812 differs in raster, everything else identical" is
// a lead.
namespace CDO4CommonRecorder
{
	inline constexpr const char* kPrefix = "[CDO4-CR]";
	inline constexpr std::uint32_t kSchemaVersion = 1u;

	/**
	 * @brief FNV-1a accumulator over canonical values.
	 *
	 * Values, not object bytes. Hashing a struct's memory would change with
	 * padding or field order and would NOT change when two numerically identical
	 * states came from different producers - the opposite of what a comparator
	 * needs.
	 */
	class Digest
	{
	public:
		constexpr void Add(std::uint64_t a_value) noexcept
		{
			for (int i = 0; i < 8; ++i) {
				hash_ ^= static_cast<std::uint64_t>((a_value >> (i * 8)) & 0xFFu);
				hash_ *= 1099511628211ull;
			}
		}

		constexpr void Add(std::uint32_t a_value) noexcept { Add(static_cast<std::uint64_t>(a_value)); }
		constexpr void Add(bool a_value) noexcept { Add(static_cast<std::uint64_t>(a_value ? 1u : 0u)); }

		/**
		 * @brief Floats enter through their exact bit pattern.
		 *
		 * Quantising would hide a real difference, and comparing as floats would
		 * make the comparator's own rounding part of the result. A bit pattern
		 * differs exactly when the value differs.
		 */
		constexpr void Add(float a_value) noexcept
		{
			static_assert(sizeof(std::uint32_t) == sizeof(float));
			Add(std::bit_cast<std::uint32_t>(a_value));
		}

		[[nodiscard]] constexpr std::uint64_t Value() const noexcept { return hash_; }
		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return hash_ == kSeed; }

	private:
		static constexpr std::uint64_t kSeed = 1469598103934665603ull;
		std::uint64_t hash_{ kSeed };
	};

	/**
	 * @brief One frame's comparison record, split so a mismatch names a category.
	 *
	 * A category that was not observable this frame stays 0 and is emitted as
	 * null rather than as an empty hash, so "absent" and "empty" never collapse -
	 * the same reason the protocol refuses to let an absent log line mean success.
	 */
	struct FrameRecord
	{
		std::uint32_t frame{};
		std::uint64_t planHash{};
		std::uint32_t contractGeneration{};

		std::uint64_t plan{};
		std::uint64_t resources{};
		std::uint64_t raster{};
		std::uint64_t camera{};
		std::uint64_t provider{};
		std::uint64_t history{};
		std::uint64_t submit{};

		// Flags that change interpretation rather than geometry, kept out of the
		// hashes so a frame can be excluded rather than silently compared.
		bool menuContext{};
		bool loadingContext{};
		bool relatchPending{};
		bool vendorResetPending{};
		bool deviceLost{};
		bool fallbackTaken{};
	};

	[[nodiscard]] inline bool Comparable(const FrameRecord& a_record) noexcept
	{
		return !a_record.menuContext && !a_record.loadingContext &&
		       !a_record.relatchPending && !a_record.vendorResetPending &&
		       !a_record.deviceLost;
	}
}

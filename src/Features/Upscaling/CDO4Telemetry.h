#pragma once

#include <atomic>
#include <cstdint>

// CDO4-001 phase 2: the common event envelope.
//
// Every record carries identity and ancestry, because the protocol's own
// conclusion about the existing telemetry was blunt: a deduplicated line without
// an occurrence stream cannot form an event DAG, no matter how much it contains.
// The earlier loggers deduplicated to a fixed cap and emitted nothing per
// occurrence, so they can scope work but can never be confirmatory.
//
// What this adds over those:
//
//   monotonic event IDs      so a gap is a dropped record, not a reordering
//   parent event IDs         so ancestry is recorded rather than reconstructed
//   plan hash                so one frame can be proven to use one geometry
//   stable resource IDs      because a COM pointer is not an identity
//   window begin/end         with reserved/emitted/dropped/saturated totals
//   fail-closed admission    so a lost tail cannot read as "it never happened"
//
// Records are emitted as one-line JSON with a fixed prefix, so a run's stream can
// be extracted from the CS log into the .jsonl files the evidence layout expects.
// That is a deliberate trade: it avoids introducing separate file handling and
// its own flush semantics, at the cost of interleaving with other log output.
// Info-level logging is not presumed non-perturbing - phase 2 measures off/on
// equivalence before any of this counts as evidence.
namespace CDO4Telemetry
{
	inline constexpr const char* kPrefix = "[CDO4]";
	inline constexpr std::uint32_t kSchemaVersion = 1u;

	/** @brief Payload kinds. Reserved window records are part of the schema. */
	enum class Payload : std::uint8_t
	{
		WindowBegin,
		WindowEnd,
		PlanPublished,          // A0
		RenderTargetProperties, // A1  pre-create
		RenderTargetIdentity,   // A2  post-create, the only thing that confirms a resource
		IntermediateCapacity,   // Ks* capacity vs command footprint
		VendorDeclaredRegion,   // Ks* -> V0
		EffectiveConfiguration  // C0  what was actually installed, not what was asked for
	};

	[[nodiscard]] inline constexpr const char* PayloadName(Payload a_payload) noexcept
	{
		switch (a_payload) {
		case Payload::WindowBegin:            return "WINDOW_BEGIN";
		case Payload::WindowEnd:              return "WINDOW_END";
		case Payload::PlanPublished:          return "PLAN_PUBLISHED";
		case Payload::RenderTargetProperties: return "RENDER_TARGET_PROPERTIES";
		case Payload::RenderTargetIdentity:   return "RENDER_TARGET_IDENTITY";
		case Payload::IntermediateCapacity:   return "INTERMEDIATE_CAPACITY";
		case Payload::VendorDeclaredRegion:   return "VENDOR_DECLARED_REGION";
		case Payload::EffectiveConfiguration: return "EFFECTIVE_CONFIGURATION";
		default:                              return "UNKNOWN";
		}
	}

	// --- counters ----------------------------------------------------------------
	//
	// Deliberately global and monotonic for the process. A window is a slice of
	// this sequence, not a private counter, so an event ID is comparable across
	// windows and a gap is meaningful.

	inline std::atomic<std::uint64_t> g_nextEventId{ 1u };
	inline std::atomic<std::uint64_t> g_emitted{ 0u };
	inline std::atomic<std::uint64_t> g_dropped{ 0u };
	inline std::atomic<std::uint64_t> g_reserved{ 0u };
	inline std::atomic<bool> g_saturated{ false };
	inline std::atomic<bool> g_windowOpen{ false };

	/** @brief Reserves the next event ID. Reserving is counted even if emission fails. */
	[[nodiscard]] inline std::uint64_t ReserveEventId() noexcept
	{
		g_reserved.fetch_add(1u, std::memory_order_relaxed);
		return g_nextEventId.fetch_add(1u, std::memory_order_relaxed);
	}

	inline void NoteEmitted() noexcept { g_emitted.fetch_add(1u, std::memory_order_relaxed); }
	inline void NoteDropped() noexcept { g_dropped.fetch_add(1u, std::memory_order_relaxed); }
	inline void NoteSaturated() noexcept { g_saturated.store(true, std::memory_order_relaxed); }

	/**
	 * @brief Admission for a window's records.
	 *
	 * Fails CLOSED. A window with no end record, or whose accounting does not
	 * balance, yields nothing usable - which is the point. Silence must never be
	 * indistinguishable from "the thing did not happen".
	 */
	[[nodiscard]] inline bool WindowAccountingBalances(
		std::uint64_t a_reserved,
		std::uint64_t a_emitted,
		std::uint64_t a_dropped) noexcept
	{
		return a_reserved == a_emitted + a_dropped;
	}

	// --- stable resource identity ------------------------------------------------

	/**
	 * @brief A COM pointer is not a stable identity - it can be recycled.
	 *
	 * Identity is the pointer PLUS a generation that increments whenever the
	 * pointer is seen with a different descriptor. Two records agree on identity
	 * only when both agree, so a recycled address cannot silently join two
	 * different resources.
	 */
	struct ResourceIdentity
	{
		std::uint64_t address{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t format{};
		std::uint32_t generation{};

		[[nodiscard]] constexpr bool DescribesSame(const ResourceIdentity& a_other) const noexcept
		{
			return address == a_other.address && width == a_other.width &&
			       height == a_other.height && format == a_other.format;
		}
	};

	// --- plan hash ---------------------------------------------------------------

	/**
	 * @brief FNV-1a over the canonical geometry, so one frame can be proven to
	 *        have used one plan.
	 *
	 * Hashing the CANONICAL VALUES rather than the bytes of the plan structure is
	 * deliberate: a memcmp of wrappers would change when padding or field order
	 * changed, and would not change when two numerically identical plans came from
	 * different producers.
	 */
	[[nodiscard]] inline constexpr std::uint64_t HashPlan(
		std::uint32_t a_allocWidth, std::uint32_t a_allocHeight,
		std::uint32_t a_renderWidth, std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth, std::uint32_t a_outputHeight,
		std::uint32_t a_bootQuality, std::uint32_t a_activeQuality,
		std::uint32_t a_owner, std::uint32_t a_contractGeneration) noexcept
	{
		std::uint64_t hash = 1469598103934665603ull;
		const auto mix = [&hash](std::uint32_t a_value) {
			for (int i = 0; i < 4; ++i) {
				hash ^= static_cast<std::uint64_t>((a_value >> (i * 8)) & 0xFFu);
				hash *= 1099511628211ull;
			}
		};
		mix(a_allocWidth);
		mix(a_allocHeight);
		mix(a_renderWidth);
		mix(a_renderHeight);
		mix(a_outputWidth);
		mix(a_outputHeight);
		mix(a_bootQuality);
		mix(a_activeQuality);
		mix(a_owner);
		mix(a_contractGeneration);
		return hash;
	}

	// --- the envelope ------------------------------------------------------------

	struct Envelope
	{
		std::uint64_t eventId{};
		std::uint64_t parentEventId{};   // 0 = no parent, stated rather than omitted
		std::uint32_t frame{};
		std::uint64_t compositorCycleToken{};  // 0 = not part of a cycle
		std::uint32_t eye{ 0xFFFFFFFFu };      // sentinel = not eye-specific
		std::uint64_t planHash{};
		std::uint32_t contractGeneration{};
		Payload payload{ Payload::WindowBegin };
	};

	/** @brief The eye sentinel, so "no eye" is a value rather than a guess. */
	inline constexpr std::uint32_t kNoEye = 0xFFFFFFFFu;
}

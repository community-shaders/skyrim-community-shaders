#pragma once

#include <cstdint>

// CDO4-001 phase 2, item 5: the frame-matching predicate.
//
// The comparison harness needs to know which frame in run B corresponds to a
// given frame in run A. Without that, "the runs differ" is not a finding - two
// live sessions never see the same frames, so a raw difference is expected and
// carries no information.
//
// WHAT SESSION 2 FORCED ABOUT THIS DESIGN
//
// The naive predicate matches on frame number. Sessions 1 and 2 falsify it: all
// seven category transitions were exactly 437 frames apart, because session 1
// simply spent 437 more frames in the pre-game menu. Frame numbers are offset by
// however long the player took to press a key. Matching on them would have
// declared two identical sequences completely different.
//
// The same measurement showed the opposite problem for play. Across ~22,000
// frames of steady play there was not a single transition in any category: the
// recorded state is CONSTANT during play. So for play frames the recorded state
// cannot identify a frame at all - every frame looks alike, and a match on state
// would pair arbitrary frames and call it success.
//
// Those two facts give the two modes below, and the boundary between them.

namespace CDO4FrameMatch
{
	// Why a pair of frames is or is not comparable.
	//
	// Refusals are values, not exceptions and not `false`. A caller that gets
	// `NotComparable` must be able to say WHY, because "no match" and "cannot
	// tell" are different results and the protocol forbids collapsing them.
	enum class Verdict : std::uint8_t
	{
		Matched,

		// Structural refusals - the pairing is wrong.
		ContextFlagMismatch,
		GenerationMismatch,
		OrdinalMismatch,
		OffsetNotConstant,

		// Capability refusals - the pairing may be right, but this build cannot
		// establish it. Never report these as a mismatch.
		StateIdentityNotRecorded,
		AnchorNotEstablished,
	};

	[[nodiscard]] constexpr bool IsRefusal(Verdict a_verdict) noexcept
	{
		return a_verdict != Verdict::Matched;
	}

	// A refusal that says "this build cannot answer", as opposed to one that says
	// "these frames do not correspond". Conflating them is how an absent
	// instrument becomes a false negative.
	[[nodiscard]] constexpr bool IsCapabilityRefusal(Verdict a_verdict) noexcept
	{
		return a_verdict == Verdict::StateIdentityNotRecorded ||
		       a_verdict == Verdict::AnchorNotEstablished;
	}

	// Whether a recorded category can serve as a state coordinate at all.
	//
	// Added after the harness was first run on sessions 1 and 2 and refused to
	// conclude anything. The cause was `submit`: all-null in session 1, and
	// unique-per-frame in session 2. Feeding either into the frame signature
	// destroys it - the all-null one contributes nothing, and the unique one
	// makes every frame its own block, so no two frames in different runs can
	// ever share an ordinal.
	//
	// The rule lives here rather than in the analyzer because it decides what
	// evidence is admissible, and the protocol requires that to be pre-registered
	// rather than chosen once the data is in view.
	enum class CategoryUsability : std::uint8_t
	{
		Usable,
		DegenerateAbsent,          // never recorded - carries nothing
		DegeneratePerFrameUnique,  // a per-frame fingerprint, not state
	};

	// A category whose distinct-value count reaches this share of frames is a
	// fingerprint rather than a state. Deliberately far from both observed cases
	// so it is a principled boundary and not a threshold fitted to this data:
	// session 2's `submit` sat at ~100% and every working category under 0.02%.
	// Expressed as a reciprocal to keep the comparison in integers.
	inline constexpr std::uint32_t kPerFrameUniqueDivisor = 2u;  // 50%

	[[nodiscard]] constexpr CategoryUsability ClassifyCategory(
		std::uint32_t a_frames,
		std::uint32_t a_distinctValues,
		std::uint32_t a_absentFrames) noexcept
	{
		if (a_frames == 0u)
			return CategoryUsability::DegenerateAbsent;

		if (a_absentFrames >= a_frames)
			return CategoryUsability::DegenerateAbsent;

		if (a_distinctValues >= a_frames / kPerFrameUniqueDivisor)
			return CategoryUsability::DegeneratePerFrameUnique;

		return CategoryUsability::Usable;
	}

	// A constant category is USABLE, not degenerate. It contributes nothing to
	// the ordinal but it is still a real observation, and excluding it would
	// discard the evidence that it stayed constant.
	[[nodiscard]] constexpr bool MayContributeToSignature(CategoryUsability a_usability) noexcept
	{
		return a_usability == CategoryUsability::Usable;
	}

	// A degenerate category must not be compared either: an all-null one would
	// compare equal for the wrong reason, and a per-frame-unique one would report
	// a difference on every matched pair. Both are false results, in opposite
	// directions.
	[[nodiscard]] constexpr bool MayBeComparedAcrossRuns(CategoryUsability a_usability) noexcept
	{
		return a_usability == CategoryUsability::Usable;
	}

	// Usability is a property of the PAIR of runs, not of one run. A category
	// usable in A and degenerate in B is unusable for the comparison.
	[[nodiscard]] constexpr CategoryUsability CombineUsability(
		CategoryUsability a_lhs,
		CategoryUsability a_rhs) noexcept
	{
		if (a_lhs == CategoryUsability::Usable && a_rhs == CategoryUsability::Usable)
			return CategoryUsability::Usable;

		// Report the more specific failure so the operator can act on it.
		if (a_lhs == CategoryUsability::DegeneratePerFrameUnique ||
			a_rhs == CategoryUsability::DegeneratePerFrameUnique)
			return CategoryUsability::DegeneratePerFrameUnique;

		return CategoryUsability::DegenerateAbsent;
	}

	enum class Mode : std::uint8_t
	{
		// Startup: align by transition ordinal, then by offset within the block.
		// Tolerates the constant frame offset between runs; the offset itself is
		// checked rather than assumed.
		SequenceAnchored,

		// Play: requires a state identity strong enough to name one frame.
		StrictState,
	};

	// Which quantities a build actually emits.
	//
	// Every field defaults to false, so a build that records nothing refuses
	// everything. This is the direction the protocol requires: a new capability
	// has to be declared before it can be relied on, and forgetting to declare
	// one produces a refusal rather than a silent pass.
	struct RecordedQuantities
	{
		bool pose{ false };
		bool projection{ false };
		bool jitter{ false };
		bool exposure{ false };
		bool worldAndAnimationState{ false };
		bool weatherAndTime{ false };
		bool submissionCohort{ false };

		[[nodiscard]] constexpr bool SufficientForStateIdentity() const noexcept
		{
			// All seven, deliberately. A subset does not identify a frame: two
			// frames with the same pose and projection are still different frames
			// if the animation, weather or exposure differ, and any of those can
			// move the pixels the oracle is comparing.
			return pose && projection && jitter && exposure &&
			       worldAndAnimationState && weatherAndTime && submissionCohort;
		}
	};

	// One frame, as the harness sees it.
	struct Frame
	{
		std::uint32_t frame{ 0u };
		std::uint32_t contractGeneration{ 0u };

		// How many category transitions have completed at or before this frame.
		// This is the coordinate that survives a differing menu time.
		std::uint32_t transitionOrdinal{ 0u };

		// Frame of the transition that opened the current block. Offset within
		// the block is `frame - blockStartFrame`, which is what makes two frames
		// inside the same block distinguishable.
		std::uint32_t blockStartFrame{ 0u };

		bool menuContext{ false };
		bool loadingContext{ false };
		bool relatchPending{ false };
		bool vendorResetPending{ false };
		bool deviceLost{ false };
		bool fallbackTaken{ false };

		// Opaque digest of the seven quantities above. Meaningful only when the
		// build declares `SufficientForStateIdentity()`.
		std::uint64_t stateIdentity{ 0u };
	};

	[[nodiscard]] constexpr bool ContextFlagsAgree(const Frame& a_lhs, const Frame& a_rhs) noexcept
	{
		return a_lhs.menuContext == a_rhs.menuContext &&
		       a_lhs.loadingContext == a_rhs.loadingContext &&
		       a_lhs.relatchPending == a_rhs.relatchPending &&
		       a_lhs.vendorResetPending == a_rhs.vendorResetPending &&
		       a_lhs.deviceLost == a_rhs.deviceLost &&
		       a_lhs.fallbackTaken == a_rhs.fallbackTaken;
	}

	// The anchor: the constant frame offset between two runs.
	//
	// Established from a transition pair, then verified against every later
	// transition. Sessions 1 and 2 gave 437 across all seven - but the point is
	// that it was CHECKED. A drifting offset means the two runs are not the same
	// sequence, and the harness must fail rather than rescale.
	struct Anchor
	{
		bool established{ false };
		std::int64_t offset{ 0 };  // lhs.frame - rhs.frame
	};

	// Ordinal 0 is not a transition.
	//
	// The first block does not begin because something changed; it begins
	// because logging began. Its start frame is therefore an artifact of when the
	// recorder was switched on, and in practice it is frame 0 in every run, which
	// yields a degenerate offset of 0 that then rejects every real frame.
	//
	// This is not hypothetical. The harness's first run against sessions 1 and 2
	// anchored on ordinal 0, took the offset as 0, and refused 21,816 frames as
	// OffsetNotConstant while printing 437 for every genuine transition beside it.
	[[nodiscard]] constexpr bool IsAnchorableOrdinal(std::uint32_t a_ordinal) noexcept
	{
		return a_ordinal != 0u;
	}

	[[nodiscard]] constexpr Anchor EstablishAnchor(const Frame& a_lhs, const Frame& a_rhs) noexcept
	{
		if (a_lhs.transitionOrdinal != a_rhs.transitionOrdinal)
			return Anchor{};

		if (!IsAnchorableOrdinal(a_lhs.transitionOrdinal))
			return Anchor{};

		return Anchor{ true,
			static_cast<std::int64_t>(a_lhs.blockStartFrame) -
				static_cast<std::int64_t>(a_rhs.blockStartFrame) };
	}

	[[nodiscard]] constexpr bool AnchorHolds(const Anchor& a_anchor, const Frame& a_lhs, const Frame& a_rhs) noexcept
	{
		if (!a_anchor.established)
			return false;

		const auto observed = static_cast<std::int64_t>(a_lhs.blockStartFrame) -
		                      static_cast<std::int64_t>(a_rhs.blockStartFrame);
		return observed == a_anchor.offset;
	}

	[[nodiscard]] constexpr Verdict Match(
		Mode a_mode,
		const RecordedQuantities& a_recorded,
		const Anchor& a_anchor,
		const Frame& a_lhs,
		const Frame& a_rhs) noexcept
	{
		// Context first, in both modes. A menu frame never corresponds to a play
		// frame however well the other coordinates line up, and a frame with a
		// relatch pending is in a different regime from one without.
		if (!ContextFlagsAgree(a_lhs, a_rhs))
			return Verdict::ContextFlagMismatch;

		if (a_lhs.contractGeneration != a_rhs.contractGeneration)
			return Verdict::GenerationMismatch;

		if (a_mode == Mode::SequenceAnchored) {
			if (!a_anchor.established)
				return Verdict::AnchorNotEstablished;

			if (a_lhs.transitionOrdinal != a_rhs.transitionOrdinal)
				return Verdict::OrdinalMismatch;

			if (!AnchorHolds(a_anchor, a_lhs, a_rhs))
				return Verdict::OffsetNotConstant;

			// Same block, same distance into it.
			const auto lhsOffset = a_lhs.frame - a_lhs.blockStartFrame;
			const auto rhsOffset = a_rhs.frame - a_rhs.blockStartFrame;
			return lhsOffset == rhsOffset ? Verdict::Matched : Verdict::OrdinalMismatch;
		}

		// StrictState.
		//
		// This is where the honest answer is currently "cannot tell". None of the
		// seven quantities is recorded yet, so `SufficientForStateIdentity()` is
		// false and every play-frame comparison refuses. That refusal is the
		// correct output, not a gap to be worked around: matching play frames on
		// the state we DO record would pair arbitrary frames, because session 2
		// showed that state is constant across all ~22,000 play frames.
		if (!a_recorded.SufficientForStateIdentity())
			return Verdict::StateIdentityNotRecorded;

		return a_lhs.stateIdentity == a_rhs.stateIdentity ?
		           Verdict::Matched :
		           Verdict::OrdinalMismatch;
	}

	// What the harness may conclude from a set of comparisons.
	//
	// Separated from `Match` because the dangerous inference is not about one
	// pair, it is about a run: "nothing differed" is only a result if enough
	// pairs were actually comparable to have shown a difference.
	struct RunTally
	{
		std::uint32_t matched{ 0u };
		std::uint32_t structuralRefusals{ 0u };
		std::uint32_t capabilityRefusals{ 0u };
		std::uint32_t differencesFound{ 0u };
	};

	// The minimum matched-pair count below which "no difference" means nothing.
	// Set to the frozen settle window so the harness cannot claim equivalence
	// from a handful of startup frames.
	inline constexpr std::uint32_t kMinimumMatchedPairsForEquivalence = 120u;

	[[nodiscard]] constexpr bool MayConcludeEquivalent(const RunTally& a_tally) noexcept
	{
		return a_tally.differencesFound == 0u &&
		       a_tally.capabilityRefusals == 0u &&
		       a_tally.matched >= kMinimumMatchedPairsForEquivalence;
	}

	// A difference is a difference regardless of how few pairs produced it -
	// asymmetric with equivalence on purpose. One proven difference falsifies
	// non-interference; a thousand matches do not prove it.
	[[nodiscard]] constexpr bool MayConcludeDifferent(const RunTally& a_tally) noexcept
	{
		return a_tally.differencesFound > 0u;
	}

	// Neither conclusion available: the run answered nothing and must be
	// archived as inconclusive rather than counted as a pass.
	[[nodiscard]] constexpr bool IsInconclusive(const RunTally& a_tally) noexcept
	{
		return !MayConcludeEquivalent(a_tally) && !MayConcludeDifferent(a_tally);
	}
}

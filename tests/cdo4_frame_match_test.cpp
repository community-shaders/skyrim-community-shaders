// CDO4-001 phase 2, item 5: tests for the frame-matching predicate.
//
// As with cdo4_policy_test.cpp, a large share of these assert what the predicate
// REFUSES. A matcher that is willing to match is worse than useless: it turns
// "we could not tell" into "they agreed", which is the exact failure the oracle
// exists to prevent.

#include "Features/Upscaling/CDO4FrameMatch.h"

namespace
{
	using namespace CDO4FrameMatch;

	constexpr Frame MakeFrame(
		std::uint32_t a_frame,
		std::uint32_t a_ordinal,
		std::uint32_t a_blockStart,
		std::uint64_t a_identity = 0u) noexcept
	{
		Frame f{};
		f.frame = a_frame;
		f.transitionOrdinal = a_ordinal;
		f.blockStartFrame = a_blockStart;
		f.stateIdentity = a_identity;
		return f;
	}

	constexpr RecordedQuantities kNothingRecorded{};

	constexpr RecordedQuantities kEverythingRecorded{
		true, true, true, true, true, true, true
	};

	// ---------------------------------------------------------------- capability

	static_assert(!kNothingRecorded.SufficientForStateIdentity());
	static_assert(kEverythingRecorded.SufficientForStateIdentity());

	// Six of seven is not six-sevenths of an answer. Each of these omits exactly
	// one quantity and must still be insufficient.
	static_assert(!RecordedQuantities{ false, true, true, true, true, true, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, false, true, true, true, true, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, true, false, true, true, true, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, true, true, false, true, true, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, true, true, true, false, true, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, true, true, true, true, false, true }.SufficientForStateIdentity());
	static_assert(!RecordedQuantities{ true, true, true, true, true, true, false }.SufficientForStateIdentity());

	// The default must be the refusing one. If this ever flips, a build that
	// forgets to declare a capability starts silently passing comparisons.
	static_assert(!RecordedQuantities{}.SufficientForStateIdentity());

	// ------------------------------------------------------- category usability

	// The two measured degenerate cases, which is why this policy exists.
	// Session 1: submit null in all 23,972 comparable frames.
	static_assert(ClassifyCategory(23972u, 0u, 23972u) == CategoryUsability::DegenerateAbsent);
	// Session 2: submit distinct in 21,816 of 21,817.
	static_assert(ClassifyCategory(21817u, 21816u, 0u) == CategoryUsability::DegeneratePerFrameUnique);

	// The working categories from the same two sessions, at the other extreme.
	static_assert(ClassifyCategory(23972u, 3u, 0u) == CategoryUsability::Usable);
	static_assert(ClassifyCategory(21817u, 2u, 0u) == CategoryUsability::Usable);

	// A constant category is usable, not degenerate. It contributes nothing to
	// the ordinal but "it stayed constant" is a real observation.
	static_assert(ClassifyCategory(1000u, 1u, 0u) == CategoryUsability::Usable);

	// Boundary, stated explicitly so a later edit cannot move it silently.
	static_assert(ClassifyCategory(1000u, 499u, 0u) == CategoryUsability::Usable);
	static_assert(ClassifyCategory(1000u, 500u, 0u) == CategoryUsability::DegeneratePerFrameUnique);

	// No frames at all is absent, not usable - an empty run must not read as
	// agreement.
	static_assert(ClassifyCategory(0u, 0u, 0u) == CategoryUsability::DegenerateAbsent);

	// Absent wins over the unique test: a category that is missing everywhere
	// cannot also be a fingerprint.
	static_assert(ClassifyCategory(100u, 100u, 100u) == CategoryUsability::DegenerateAbsent);

	// Partial absence is not absence. A category recorded on most frames is
	// still evidence, and discarding it would lose real observations.
	static_assert(ClassifyCategory(100u, 3u, 99u) == CategoryUsability::Usable);

	// Neither degenerate form may be used, for either purpose.
	static_assert(MayContributeToSignature(CategoryUsability::Usable));
	static_assert(!MayContributeToSignature(CategoryUsability::DegenerateAbsent));
	static_assert(!MayContributeToSignature(CategoryUsability::DegeneratePerFrameUnique));
	static_assert(MayBeComparedAcrossRuns(CategoryUsability::Usable));
	static_assert(!MayBeComparedAcrossRuns(CategoryUsability::DegenerateAbsent));
	static_assert(!MayBeComparedAcrossRuns(CategoryUsability::DegeneratePerFrameUnique));

	// Usability belongs to the PAIR. This is the exact session 1 / session 2
	// case: absent in one run, per-frame-unique in the other, and the pair is
	// unusable however each run looks alone.
	static_assert(CombineUsability(CategoryUsability::DegenerateAbsent,
					  CategoryUsability::DegeneratePerFrameUnique) ==
				  CategoryUsability::DegeneratePerFrameUnique);
	static_assert(CombineUsability(CategoryUsability::Usable,
					  CategoryUsability::DegenerateAbsent) == CategoryUsability::DegenerateAbsent);
	static_assert(CombineUsability(CategoryUsability::Usable,
					  CategoryUsability::DegeneratePerFrameUnique) ==
				  CategoryUsability::DegeneratePerFrameUnique);
	static_assert(CombineUsability(CategoryUsability::Usable, CategoryUsability::Usable) ==
				  CategoryUsability::Usable);

	// Symmetric: which run is called reference must not change the verdict.
	static_assert(CombineUsability(CategoryUsability::DegenerateAbsent, CategoryUsability::Usable) ==
				  CombineUsability(CategoryUsability::Usable, CategoryUsability::DegenerateAbsent));
	static_assert(CombineUsability(CategoryUsability::DegeneratePerFrameUnique, CategoryUsability::Usable) ==
				  CombineUsability(CategoryUsability::Usable, CategoryUsability::DegeneratePerFrameUnique));

	// ------------------------------------------------------------------ arm pair

	constexpr ArmIdentity kArmA{ true, false, false, 0u };
	constexpr ArmIdentity kArmB{ true, true, true, 7u };
	constexpr ArmIdentity kUnrecorded{};

	static_assert(ClassifyArmPairing(kArmA, kArmB) == ArmPairing::Differential);
	static_assert(ClassifyArmPairing(kArmB, kArmA) == ArmPairing::Differential);
	static_assert(ClassifyArmPairing(kArmA, kArmA) == ArmPairing::SameArm);
	static_assert(ClassifyArmPairing(kArmB, kArmB) == ArmPairing::SameArm);

	// Sessions 1 and 2: neither could state its arm, because the field did not
	// exist in those builds. That must read as Unrecorded, never as SameArm -
	// "both were arm A" was an assumption, however well-founded.
	static_assert(ClassifyArmPairing(kUnrecorded, kUnrecorded) == ArmPairing::Unrecorded);
	static_assert(ClassifyArmPairing(kArmA, kUnrecorded) == ArmPairing::Unrecorded);
	static_assert(ClassifyArmPairing(kUnrecorded, kArmB) == ArmPairing::Unrecorded);

	// The installed state is the arm, not the setting that requests it. Item 4
	// established the setting can be on while the wrappers are absent.
	constexpr ArmIdentity kRequestedButAbsent{ true, true, false, 0u };
	static_assert(ClassifyArmPairing(kArmA, kRequestedButAbsent) == ArmPairing::SameArm);
	static_assert(ClassifyArmPairing(kArmB, kRequestedButAbsent) == ArmPairing::Differential);

	// A same-arm pair is a repeatability check and not a non-interference test.
	// Reporting one as the other turns "we changed nothing and nothing changed"
	// into "the instrument is harmless".
	static_assert(MayScoreAsNonInterference(ArmPairing::Differential));
	static_assert(!MayScoreAsNonInterference(ArmPairing::SameArm));
	static_assert(!MayScoreAsNonInterference(ArmPairing::Unrecorded));
	static_assert(MayScoreAsRepeatability(ArmPairing::SameArm));
	static_assert(!MayScoreAsRepeatability(ArmPairing::Differential));
	static_assert(!MayScoreAsRepeatability(ArmPairing::Unrecorded));

	// Unrecorded supports NEITHER claim. A run that cannot state its arm cannot
	// be scored as any kind of comparison.
	static_assert(!MayScoreAsNonInterference(ArmPairing::Unrecorded) &&
				  !MayScoreAsRepeatability(ArmPairing::Unrecorded));

	// ------------------------------------------------------------------- anchor

	// The measured case: session 1 and session 2, offset 437 at every transition.
	constexpr auto kS1First = MakeFrame(8205u, 1u, 8205u);
	constexpr auto kS2First = MakeFrame(7768u, 1u, 7768u);
	constexpr auto kAnchor = EstablishAnchor(kS1First, kS2First);

	static_assert(kAnchor.established);
	static_assert(kAnchor.offset == 437);

	// Verified, not assumed: every later transition must reproduce the offset.
	static_assert(AnchorHolds(kAnchor, MakeFrame(8210u, 2u, 8210u), MakeFrame(7773u, 2u, 7773u)));
	static_assert(AnchorHolds(kAnchor, MakeFrame(8235u, 3u, 8235u), MakeFrame(7798u, 3u, 7798u)));
	static_assert(AnchorHolds(kAnchor, MakeFrame(8337u, 4u, 8337u), MakeFrame(7900u, 4u, 7900u)));
	static_assert(AnchorHolds(kAnchor, MakeFrame(8339u, 5u, 8339u), MakeFrame(7902u, 5u, 7902u)));
	static_assert(AnchorHolds(kAnchor, MakeFrame(8343u, 6u, 8343u), MakeFrame(7906u, 6u, 7906u)));
	static_assert(AnchorHolds(kAnchor, MakeFrame(8344u, 7u, 8344u), MakeFrame(7907u, 7u, 7907u)));

	// A drifting offset is a different sequence, not a rescaling problem.
	static_assert(!AnchorHolds(kAnchor, MakeFrame(8345u, 7u, 8345u), MakeFrame(7907u, 7u, 7907u)));

	// An anchor cannot be established across differing ordinals - that pairing is
	// exactly the mistake the anchor exists to catch.
	static_assert(!EstablishAnchor(MakeFrame(8205u, 1u, 8205u), MakeFrame(7768u, 2u, 7768u)).established);

	// Ordinal 0 is not anchorable. Both runs start their first block at frame 0
	// because that is when logging began, so an anchor taken there is 0 by
	// construction and rejects every real frame afterwards.
	static_assert(!IsAnchorableOrdinal(0u));
	static_assert(IsAnchorableOrdinal(1u));
	static_assert(!EstablishAnchor(MakeFrame(0u, 0u, 0u), MakeFrame(0u, 0u, 0u)).established);

	// Even when the two first blocks genuinely differ, ordinal 0 is refused -
	// the difference is a logging-start artifact, not a sequence offset.
	static_assert(!EstablishAnchor(MakeFrame(0u, 0u, 0u), MakeFrame(0u, 0u, 12u)).established);

	// An unestablished anchor never holds, whatever is fed to it.
	static_assert(!AnchorHolds(Anchor{}, kS1First, kS2First));

	// A zero offset is a legitimate established anchor, not "no anchor". These
	// are distinct states and the struct must keep them distinct.
	static_assert(EstablishAnchor(MakeFrame(100u, 1u, 100u), MakeFrame(100u, 1u, 100u)).established);
	static_assert(EstablishAnchor(MakeFrame(100u, 1u, 100u), MakeFrame(100u, 1u, 100u)).offset == 0);

	// Negative offsets: run B may be the later one.
	static_assert(EstablishAnchor(MakeFrame(7768u, 1u, 7768u), MakeFrame(8205u, 1u, 8205u)).offset == -437);

	// --------------------------------------------------------- sequence matching

	// Same ordinal, same distance into the block, offset holds.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  MakeFrame(8250u, 3u, 8235u), MakeFrame(7813u, 3u, 7798u)) == Verdict::Matched);

	// Sequence matching works with NOTHING recorded. That is the point of it:
	// the startup sequence is identifiable from its own shape.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  kS1First, kS2First) == Verdict::Matched);

	// Different distance into the same block is not the same frame.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  MakeFrame(8250u, 3u, 8235u), MakeFrame(7814u, 3u, 7798u)) == Verdict::OrdinalMismatch);

	// Different ordinal.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  MakeFrame(8250u, 3u, 8235u), MakeFrame(7813u, 4u, 7798u)) == Verdict::OrdinalMismatch);

	// Right ordinal, wrong offset - the sequences have drifted apart.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  MakeFrame(8250u, 3u, 8235u), MakeFrame(7812u, 3u, 7797u)) == Verdict::OffsetNotConstant);

	// Without an anchor the mode cannot run at all, and says so specifically
	// rather than reporting a mismatch.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, Anchor{},
					  kS1First, kS2First) == Verdict::AnchorNotEstablished);
	static_assert(IsCapabilityRefusal(Verdict::AnchorNotEstablished));

	// ------------------------------------------------------------ strict matching

	// THE CURRENT STATE OF THE WORLD. No build records the seven quantities, so
	// every play-frame comparison refuses. This is the assertion that keeps the
	// harness honest today.
	static_assert(Match(Mode::StrictState, kNothingRecorded, kAnchor,
					  MakeFrame(9000u, 7u, 8344u, 0xABCDu), MakeFrame(8563u, 7u, 7907u, 0xABCDu)) ==
				  Verdict::StateIdentityNotRecorded);
	static_assert(IsCapabilityRefusal(Verdict::StateIdentityNotRecorded));

	// Equal identities do NOT rescue an under-recorded build. Two frames whose
	// digests agree because both are zero is exactly the false pass to prevent.
	static_assert(Match(Mode::StrictState, kNothingRecorded, kAnchor,
					  MakeFrame(9000u, 7u, 8344u, 0u), MakeFrame(8563u, 7u, 7907u, 0u)) ==
				  Verdict::StateIdentityNotRecorded);

	// With everything recorded, identity decides.
	static_assert(Match(Mode::StrictState, kEverythingRecorded, kAnchor,
					  MakeFrame(9000u, 7u, 8344u, 0xABCDu), MakeFrame(8563u, 7u, 7907u, 0xABCDu)) ==
				  Verdict::Matched);
	static_assert(Match(Mode::StrictState, kEverythingRecorded, kAnchor,
					  MakeFrame(9000u, 7u, 8344u, 0xABCDu), MakeFrame(8563u, 7u, 7907u, 0xABCEu)) !=
				  Verdict::Matched);

	// Strict matching does not need the anchor - it identifies frames directly.
	static_assert(Match(Mode::StrictState, kEverythingRecorded, Anchor{},
					  MakeFrame(9000u, 7u, 8344u, 0xABCDu), MakeFrame(8563u, 7u, 7907u, 0xABCDu)) ==
				  Verdict::Matched);

	// ------------------------------------------------------------------- context

	constexpr Frame WithMenu(Frame a_frame) noexcept
	{
		a_frame.menuContext = true;
		return a_frame;
	}

	constexpr Frame WithRelatch(Frame a_frame) noexcept
	{
		a_frame.relatchPending = true;
		return a_frame;
	}

	// Context is checked before everything else, in both modes, even when every
	// other coordinate lines up perfectly.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  WithMenu(MakeFrame(8250u, 3u, 8235u)), MakeFrame(7813u, 3u, 7798u)) ==
				  Verdict::ContextFlagMismatch);
	static_assert(Match(Mode::StrictState, kEverythingRecorded, kAnchor,
					  WithMenu(MakeFrame(9000u, 7u, 8344u, 0xABCDu)), MakeFrame(8563u, 7u, 7907u, 0xABCDu)) ==
				  Verdict::ContextFlagMismatch);

	// The relatch band matters specifically: session 2 had 18 frames with a
	// relatch pending inside the handover, and they are a different regime.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  WithRelatch(MakeFrame(8250u, 3u, 8235u)), MakeFrame(7813u, 3u, 7798u)) ==
				  Verdict::ContextFlagMismatch);

	// Agreeing flags, even when both are set, do not block a match.
	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  WithRelatch(MakeFrame(8250u, 3u, 8235u)), WithRelatch(MakeFrame(7813u, 3u, 7798u))) ==
				  Verdict::Matched);

	// Generation is checked before any coordinate work.
	constexpr Frame WithGeneration(Frame a_frame, std::uint32_t a_generation) noexcept
	{
		a_frame.contractGeneration = a_generation;
		return a_frame;
	}

	static_assert(Match(Mode::SequenceAnchored, kNothingRecorded, kAnchor,
					  WithGeneration(MakeFrame(8250u, 3u, 8235u), 1u), MakeFrame(7813u, 3u, 7798u)) ==
				  Verdict::GenerationMismatch);

	// ----------------------------------------------------------------- run tally

	// One difference falsifies non-interference outright.
	static_assert(MayConcludeDifferent(RunTally{ 0u, 0u, 0u, 1u }));
	static_assert(MayConcludeDifferent(RunTally{ 100000u, 0u, 0u, 1u }));

	// Equivalence needs enough matched pairs to have been able to show one.
	static_assert(MayConcludeEquivalent(RunTally{ 120u, 0u, 0u, 0u }));
	static_assert(!MayConcludeEquivalent(RunTally{ 119u, 0u, 0u, 0u }));

	// A capability refusal poisons an equivalence claim even at high match counts.
	// "We could not look" is not "we looked and saw nothing".
	static_assert(!MayConcludeEquivalent(RunTally{ 100000u, 0u, 1u, 0u }));

	// Structural refusals do not poison it - unmatched frames are expected and
	// simply do not contribute.
	static_assert(MayConcludeEquivalent(RunTally{ 120u, 50000u, 0u, 0u }));

	// The two conclusions are mutually exclusive and jointly non-exhaustive.
	static_assert(!(MayConcludeEquivalent(RunTally{ 120u, 0u, 0u, 1u }) &&
					MayConcludeDifferent(RunTally{ 120u, 0u, 0u, 1u })));
	static_assert(IsInconclusive(RunTally{}));
	static_assert(IsInconclusive(RunTally{ 119u, 0u, 0u, 0u }));
	static_assert(IsInconclusive(RunTally{ 100000u, 0u, 1u, 0u }));
	static_assert(!IsInconclusive(RunTally{ 120u, 0u, 0u, 0u }));
	static_assert(!IsInconclusive(RunTally{ 0u, 0u, 0u, 1u }));

	// The threshold is the frozen settle window, not an arbitrary number.
	static_assert(kMinimumMatchedPairsForEquivalence == 120u);

	// -------------------------------------------------------------- refusal shape

	static_assert(!IsRefusal(Verdict::Matched));
	static_assert(IsRefusal(Verdict::ContextFlagMismatch));
	static_assert(IsRefusal(Verdict::OffsetNotConstant));

	// Structural refusals must NOT be reported as capability refusals: doing so
	// would let a genuine mismatch be excused as an instrument limitation.
	static_assert(!IsCapabilityRefusal(Verdict::ContextFlagMismatch));
	static_assert(!IsCapabilityRefusal(Verdict::GenerationMismatch));
	static_assert(!IsCapabilityRefusal(Verdict::OrdinalMismatch));
	static_assert(!IsCapabilityRefusal(Verdict::OffsetNotConstant));
	static_assert(!IsCapabilityRefusal(Verdict::Matched));
}

int main()
{
	return 0;
}

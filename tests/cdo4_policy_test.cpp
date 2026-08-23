// CDO4-001 phase 1. Every assertion is a static_assert, so a wrong policy is a
// build error rather than a red test.
//
// Half of this file asserts what the policies DO. The other half asserts what
// they REFUSE to conclude, because that is where this project has actually gone
// wrong: gem 2's rejected contradiction, gem 5's demoted allocation claim and
// gem 1's misread vanillaRuns were all over-readings of a real signal, not
// arithmetic errors.

#include "Features/Upscaling/CDO4Policy.h"
#include "Features/Upscaling/VRGeometryPolicy.h"

#include <cstdint>

namespace
{
	namespace P = CDO4Policy;
	namespace GP = VRGeometryPolicy;

	// --- the worked case, taken from the planner rather than retyped -------------

	inline constexpr GP::Extent kPimaxPerEye{ 3494u, 3558u };

	[[nodiscard]] constexpr GP::Decision Envelope(std::uint32_t a_boot, std::uint32_t a_active) noexcept
	{
		return GP::Derive(GP::Inputs{ GP::Flow::HotEnvelope, GP::Phase::Stable, kPimaxPerEye, a_boot, a_active });
	}

	[[nodiscard]] constexpr GP::Decision RenderScaleOff(std::uint32_t a_quality) noexcept
	{
		return GP::Derive(GP::Inputs{ GP::Flow::RenderScaleOff, GP::Phase::Stable, kPimaxPerEye, a_quality, a_quality });
	}

	[[nodiscard]] constexpr P::Extent ToPolicy(GP::Extent a_extent) noexcept
	{
		return P::Extent{ a_extent.width, a_extent.height };
	}

	inline constexpr std::uint32_t kQuality = 3u;
	inline constexpr std::uint32_t kBalanced = 4u;

	inline constexpr P::Extent kAllocCombined = ToPolicy(Envelope(kQuality, kBalanced).plan.allocationCombined);
	inline constexpr P::Extent kRenderCombined = ToPolicy(Envelope(kQuality, kBalanced).plan.renderCombined);
	inline constexpr P::Extent kDisplayCombined{ 6988u, 3558u };

	static_assert(kAllocCombined == P::Extent{ 4656u, 2372u });
	static_assert(kRenderCombined == P::Extent{ 4108u, 2092u });

	// --- A0, and the characterized RS-off render lag (carried-in item P-1) -------

	[[nodiscard]] constexpr P::A0Observation MakeA0(
		P::Flow a_flow,
		P::Extent a_observedRender,
		P::Extent a_expectedRender,
		P::Extent a_previousExpectedRender,
		std::uint32_t a_framesSinceChange,
		P::Extent a_alloc = kAllocCombined) noexcept
	{
		P::A0Observation obs{};
		obs.flow = a_flow;
		obs.observedAllocation = a_alloc;
		obs.expectedAllocation = a_alloc;
		obs.observedOutput = kDisplayCombined;
		obs.expectedOutput = kDisplayCombined;
		obs.observedRender = a_observedRender;
		obs.expectedRender = a_expectedRender;
		obs.previousExpectedRender = a_previousExpectedRender;
		obs.framesSinceQualityChange = a_framesSinceChange;
		return obs;
	}

	// RS-off render extents for the two qualities either side of a change.
	inline constexpr P::Extent kOffQuality = ToPolicy(RenderScaleOff(kQuality).plan.renderCombined);
	inline constexpr P::Extent kOffBalanced = ToPolicy(RenderScaleOff(kBalanced).plan.renderCombined);

	// RS-off scales the COMBINED width and truncates, so 4110 - not 4108. The
	// two-pixel difference from the per-eye path is known arithmetic and is not
	// forced to one value anywhere.
	static_assert(kOffBalanced == P::Extent{ 4110u, 2092u });
	static_assert(kOffQuality == P::Extent{ 4658u, 2372u });
	static_assert(!(kOffBalanced == kRenderCombined));

	// The exact matching case passes in every flow.
	static_assert(P::ScoreA0(MakeA0(P::Flow::HotEnvelope, kRenderCombined, kRenderCombined, kRenderCombined, 0u)) ==
				  P::A0Outcome::Match);
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, kOffBalanced, kOffBalanced, kOffQuality, 0u, kDisplayCombined)) ==
				  P::A0Outcome::Match);

	// The lag itself: RS-off, one frame after a change, carrying the PREVIOUS
	// quality's render extent. Admissible, and labelled as what it is.
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, kOffQuality, kOffBalanced, kOffQuality, 0u, kDisplayCombined)) ==
				  P::A0Outcome::CharacterizedRSOffRenderLag);
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, kOffQuality, kOffBalanced, kOffQuality, 1u, kDisplayCombined)) ==
				  P::A0Outcome::CharacterizedRSOffRenderLag);
	static_assert(P::A0Admits(P::A0Outcome::CharacterizedRSOffRenderLag));

	// The window is one frame wide and does not become an open-ended excuse.
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, kOffQuality, kOffBalanced, kOffQuality, 2u, kDisplayCombined)) ==
				  P::A0Outcome::Mismatch);
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, kOffQuality, kOffBalanced, kOffQuality, 60u, kDisplayCombined)) ==
				  P::A0Outcome::Mismatch);

	// The lag is RS-off specific. The other two flows DO take their render extent
	// from the planner, so the same stale value there is a real mismatch.
	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOn, kOffQuality, kRenderCombined, kOffQuality, 0u)) ==
				  P::A0Outcome::Mismatch);
	static_assert(P::ScoreA0(MakeA0(P::Flow::HotEnvelope, kOffQuality, kRenderCombined, kOffQuality, 0u)) ==
				  P::A0Outcome::Mismatch);

	// Allocation and output never lag - only the render extent is derived from
	// resolutionScale - so a wrong allocation fails even inside the window.
	static_assert(P::ScoreA0(
					  P::A0Observation{ P::Flow::RenderScaleOff, P::Extent{ 1u, 1u }, kOffQuality, kDisplayCombined,
						  kDisplayCombined, kOffBalanced, kDisplayCombined, kOffQuality, 0u }) ==
				  P::A0Outcome::Mismatch);

	static_assert(P::ScoreA0(MakeA0(P::Flow::RenderScaleOff, P::Extent{}, kOffBalanced, kOffQuality, 0u, kDisplayCombined)) ==
				  P::A0Outcome::Invalid);

	// --- gem 1: what a pass reason cannot say ------------------------------------

	static_assert(!P::PassReasonEstablishesWriterBehaviour(P::PassOutcome::OriginalCalled));
	static_assert(!P::PassReasonEstablishesWriterBehaviour(P::PassOutcome::Replaced));
	static_assert(!P::PassReasonEstablishesWriterBehaviour(P::PassOutcome::Rejected));
	static_assert(!P::PassReasonEstablishesWriterBehaviour(P::PassOutcome::NoMatchingEventObserved));

	// Absence never concludes "not invoked" at this level, whatever the reason.
	static_assert(!P::MayConcludeNotInvoked(P::PassOutcome::NoMatchingEventObserved, false, false, false));
	static_assert(!P::MayConcludeNotInvoked(P::PassOutcome::TraceNotInstalled, true, false, true));
	static_assert(!P::MayConcludeNotInvoked(P::PassOutcome::NoMatchingEventObserved, true, true, false));
	static_assert(!P::MayConcludeNotInvoked(P::PassOutcome::NoMatchingEventObserved, false, true, true));
	// Only the full L4 conjunction may.
	static_assert(P::MayConcludeNotInvoked(P::PassOutcome::NoMatchingEventObserved, true, true, true));

	// Identical branches under RS-on and Hot do not make the difference
	// insignificant: the ratios are 1 and R/A.
	static_assert(!P::ReasonParityFalsifiesSignificance(P::PassOutcome::OriginalCalled, P::PassOutcome::OriginalCalled));
	static_assert(!P::ReasonParityFalsifiesSignificance(P::PassOutcome::Replaced, P::PassOutcome::Replaced));

	// --- evidence labels ---------------------------------------------------------

	static_assert(!P::EstablishesFrameContent(P::Label::SourceProven));
	static_assert(!P::EstablishesFrameContent(P::Label::SourceConditional));
	static_assert(!P::EstablishesFrameContent(P::Label::ArtifactSupported));
	static_assert(P::EstablishesFrameContent(P::Label::ObservedForFrame));
	static_assert(P::EstablishesFrameContent(P::Label::FalsifiedForFrame));

	static_assert(!P::EstablishesCausality(P::Label::ObservedForFrame));
	static_assert(!P::EstablishesCausality(P::Label::SourceProven));
	static_assert(P::EstablishesCausality(P::Label::LocalCausalEffectSupported));
	static_assert(P::EstablishesCausality(P::Label::EndpointCausalEffectSupported));

	// --- gem 3: capacity, with the comparator always named -----------------------

	inline constexpr P::Extent kInputEyeBoot{ 2328u, 2372u };   // A_eye
	inline constexpr P::Extent kInputEyeActive{ 2054u, 2092u }; // R_eye
	inline constexpr P::Extent kOutputEye{ 3494u, 3558u };      // O_eye

	[[nodiscard]] constexpr P::CapacityObservation MakeCapacity(
		P::UpscaleMethodClass a_method,
		P::Extent a_capacity,
		P::Extent a_comparand,
		P::Comparator a_comparator,
		bool a_proven) noexcept
	{
		P::CapacityObservation obs{};
		obs.method = a_method;
		obs.physicalCapacity = a_capacity;
		obs.comparandExtent = a_comparand;
		obs.comparator = a_comparator;
		obs.comparandProven = a_proven;
		return obs;
	}

	// The Hot case gem 3 describes: a boot-sized input still holding an
	// active-sized field. Named against the comparator, never as bare "oversized".
	static_assert(P::ClassifyCapacity(MakeCapacity(P::UpscaleMethodClass::DLSS, kInputEyeBoot,
					  kInputEyeActive, P::Comparator::CommandWriteFootprint, true)) ==
				  P::CapacityOutcome::CapacityExceedsCommandFootprint);
	static_assert(P::ClassifyCapacity(MakeCapacity(P::UpscaleMethodClass::DLSS, kInputEyeBoot,
					  kInputEyeActive, P::Comparator::DeclaredRegion, true)) ==
				  P::CapacityOutcome::CapacityExceedsDeclaredRegion);
	static_assert(P::ClassifyCapacity(MakeCapacity(P::UpscaleMethodClass::DLSS, kInputEyeActive,
					  kInputEyeActive, P::Comparator::CommandWriteFootprint, true)) ==
				  P::CapacityOutcome::CapacityEqualsCommandFootprint);

	// An unproven comparand yields no comparison. DEFINED_CURRENT_FRAME_COVERAGE
	// needs L4 provenance and cannot be inferred from a descriptor.
	static_assert(P::ClassifyCapacity(MakeCapacity(P::UpscaleMethodClass::DLSS, kInputEyeBoot,
					  kInputEyeActive, P::Comparator::ProvenDefinedCoverage, false)) ==
				  P::CapacityOutcome::Inconclusive);

	// FSR allocates max(input, output) by design. That is its contract, not slack,
	// and it must not be reported as an oversized-capacity finding.
	[[nodiscard]] constexpr P::CapacityObservation MakeFsr(P::Extent a_capacity) noexcept
	{
		P::CapacityObservation obs{};
		obs.method = P::UpscaleMethodClass::FSR;
		obs.physicalCapacity = a_capacity;
		obs.comparandExtent = kInputEyeActive;
		obs.comparator = P::Comparator::CommandWriteFootprint;
		obs.comparandProven = true;
		obs.fsrInputExtent = kInputEyeActive;
		obs.fsrOutputExtent = kOutputEye;
		return obs;
	}
	static_assert(P::MaxExtent(kInputEyeActive, kOutputEye) == kOutputEye);
	static_assert(P::ClassifyCapacity(MakeFsr(kOutputEye)) ==
				  P::CapacityOutcome::FsrAllocationContractMaxInputOutput);
	// An FSR resource that is NOT at the contract size still gets a named comparison.
	static_assert(P::ClassifyCapacity(MakeFsr(kInputEyeBoot)) ==
				  P::CapacityOutcome::CapacityExceedsCommandFootprint);

	// A provider tag cannot prove the provider's own sampling behaviour.
	static_assert(P::ScoreConsumerContainment(false, true, kInputEyeBoot, kInputEyeActive) ==
				  P::CapacityOutcome::ConsumerFootprintOpaque);
	static_assert(P::ScoreConsumerContainment(true, false, kInputEyeBoot, kInputEyeActive) ==
				  P::CapacityOutcome::Inconclusive);
	static_assert(P::ScoreConsumerContainment(true, true, kInputEyeBoot, kInputEyeActive) ==
				  P::CapacityOutcome::ConsumerOutsideValidField);
	static_assert(P::ScoreConsumerContainment(true, true, kInputEyeActive, kInputEyeActive) ==
				  P::CapacityOutcome::CapacityEqualsProvenDefinedCoverage);

	// Pixels outside proven coverage are never assumed to hold the previous frame.
	static_assert(P::DefaultOutsideCoverageProvenance() == P::OutsideCoverageProvenance::Unknown);

	// --- gem 4: one bound, two extents -------------------------------------------

	// The stock submit path resolves the SAME OpenVR bound against the physical
	// resource for colour and the logical field for mask depth. Combined widths.
	inline constexpr std::uint32_t kResourceCombined = 4656u;  // A
	inline constexpr std::uint32_t kFieldCombined = 4108u;     // R

	inline constexpr auto kEyeOneBound = P::ResolveBothSpaces(0.5f, kResourceCombined, kFieldCombined);
	static_assert(kEyeOneBound.colourOrigin == 2328u);
	static_assert(kEyeOneBound.maskOrigin == 2054u);
	static_assert(!kEyeOneBound.Agree());
	static_assert(kEyeOneBound.Delta() == 274u);
	// Vertical. Height is NOT split between eyes - v runs [0,1] - so the
	// meaningful bound is the bottom edge at v = 1.0, not a half. Scoring 0.5
	// vertically would be arithmetically valid and semantically meaningless.
	inline constexpr auto kEyeBottomBound = P::ResolveBothSpaces(1.0f, 2372u, 2092u);
	static_assert(kEyeBottomBound.colourOrigin == 2372u);
	static_assert(kEyeBottomBound.maskOrigin == 2092u);
	static_assert(kEyeBottomBound.Delta() == 280u);

	// In BOTH shipped flows the field fills the resource, so the two agree and
	// nothing ever had to distinguish them. That is why this is invisible upstream.
	static_assert(P::ResolveBothSpaces(0.5f, 4108u, 4108u).Agree());   // RS-on: A = R
	static_assert(P::ResolveBothSpaces(0.5f, 6988u, 6988u).Agree());   // RS-off: source at full output
	static_assert(P::ResolveBothSpaces(0.0f, kResourceCombined, kFieldCombined).Agree());  // eye 0 is 0 either way

	// The edge passes on LOGICAL correspondence, not raw origin equality.
	static_assert(P::ScoreMaskEdge(true, true, true, false, true) == P::MaskEdgeOutcome::ApplicableExecuted);
	static_assert(P::ScoreMaskEdge(true, true, false, true, true) == P::MaskEdgeOutcome::ApplicableReused);
	static_assert(P::ScoreMaskEdge(true, true, true, false, false) == P::MaskEdgeOutcome::CorrespondenceFail);
	static_assert(P::ScoreMaskEdge(false, true, true, false, true) == P::MaskEdgeOutcome::NotApplicable);
	static_assert(P::ScoreMaskEdge(true, false, true, false, true) == P::MaskEdgeOutcome::MissingTelemetry);
	static_assert(P::ScoreMaskEdge(true, true, false, false, true) == P::MaskEdgeOutcome::SkippedWithReason);

	// A skipped or inapplicable branch never proves correct clearing.
	static_assert(!P::MaskOutcomeProvesCorrectClearing(P::MaskEdgeOutcome::NotApplicable));
	static_assert(!P::MaskOutcomeProvesCorrectClearing(P::MaskEdgeOutcome::SkippedWithReason));
	static_assert(!P::MaskOutcomeProvesCorrectClearing(P::MaskEdgeOutcome::MissingTelemetry));
	static_assert(!P::MaskOutcomeProvesCorrectClearing(P::MaskEdgeOutcome::CorrespondenceFail));
	static_assert(P::MaskOutcomeProvesCorrectClearing(P::MaskEdgeOutcome::ApplicableExecuted));

	// --- gem 5: pre-create property versus created resource ----------------------

	[[nodiscard]] constexpr P::ResourceObservation MakeResource(
		bool a_invoked, bool a_changed, P::Extent a_plan, P::Extent a_requested,
		bool a_created, bool a_descRead, P::Extent a_desc) noexcept
	{
		P::ResourceObservation obs{};
		obs.hookInvoked = a_invoked;
		obs.propertiesChanged = a_changed;
		obs.planExtent = a_plan;
		obs.requestedProperties = a_requested;
		obs.creationObserved = a_created;
		obs.postCreateDescriptorRead = a_descRead;
		obs.postCreateDescriptor = a_desc;
		return obs;
	}

	// The demotion, encoded: a property record alone can never confirm the
	// physical resource, however well it matches the plan.
	static_assert(P::ScoreResource(MakeResource(true, true, kAllocCombined, kAllocCombined, false, false, P::Extent{})) ==
				  P::ResourceOutcome::NoCreationEvent);
	static_assert(P::ScoreResource(MakeResource(true, true, kAllocCombined, kAllocCombined, true, false, P::Extent{})) ==
				  P::ResourceOutcome::Inconclusive);
	static_assert(P::ScoreResource(MakeResource(true, true, kAllocCombined, kAllocCombined, true, true, kAllocCombined)) ==
				  P::ResourceOutcome::PostCreateMatchesProperties);
	static_assert(P::ScoreResource(MakeResource(true, true, kAllocCombined, kAllocCombined, true, true, kRenderCombined)) ==
				  P::ResourceOutcome::PropertyResourceDrift);
	static_assert(P::ScoreResource(MakeResource(true, true, kAllocCombined, kRenderCombined, true, true, kRenderCombined)) ==
				  P::ResourceOutcome::PlanPropertyDrift);

	// No adjustment means "already equal", and needs a POSITIVE invocation record.
	static_assert(P::ScoreResource(MakeResource(true, false, kAllocCombined, kAllocCombined, false, false, P::Extent{})) ==
				  P::ResourceOutcome::NoPropertyChangeRequired);
	static_assert(P::ScoreResource(MakeResource(false, false, kAllocCombined, kAllocCombined, false, false, P::Extent{})) ==
				  P::ResourceOutcome::Inconclusive);

	// Only one outcome confirms the physical resource.
	static_assert(P::ConfirmsPhysicalResource(P::ResourceOutcome::PostCreateMatchesProperties));
	static_assert(!P::ConfirmsPhysicalResource(P::ResourceOutcome::PropertyPathMatchesPlan));
	static_assert(!P::ConfirmsPhysicalResource(P::ResourceOutcome::NoPropertyChangeRequired));
	static_assert(!P::ConfirmsPhysicalResource(P::ResourceOutcome::NoCreationEvent));
}

int main()
{
	// Everything is compile-time. Reaching main means every policy and every
	// refusal held.
	return 0;
}

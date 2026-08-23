#pragma once

#include <cstdint>

// CDO4-001 phase 1: the pure policies, and the invalid inferences they refuse.
//
// Every rule the protocol states in prose is encoded here as something a
// compiler can check, including the ones that say what a piece of evidence
// CANNOT establish. Those are the load-bearing half: this project's failures
// have come from over-reading a signal, not from arithmetic.
//
// Dependency-free and constexpr. No globals, no settings, no D3D, no production
// consumer - phase 1 produces policies and tests, nothing that runs in a frame.
//
// Reference: PLAN_COMPOSITIONAL_DIFFERENTIAL_ORACLE_final-v1.md sections 1, 10,
// 15 and 16, and CDO4-001/gem-interpretation-table.json.
namespace CDO4Policy
{
	// --- evidence labels (final-v1 section 1) -----------------------------------

	enum class Label : std::uint8_t
	{
		SourceProven,
		SourceConditional,
		ArtifactSupported,
		ObservedForFrame,
		LocalCausalEffectSupported,
		EndpointCausalEffectSupported,
		FalsifiedForFrame,
		Inconclusive,
		RuntimeUnresolved
	};

	/**
	 * @brief Can this label alone close a coordinate or content question?
	 *
	 * Only measurement of the frame in question can. Source facts constrain the
	 * candidate set; they never establish what a given frame's pixels were.
	 */
	[[nodiscard]] constexpr bool EstablishesFrameContent(Label a_label) noexcept
	{
		return a_label == Label::ObservedForFrame ||
		       a_label == Label::FalsifiedForFrame;
	}

	/** @brief Can this label support a causal claim? */
	[[nodiscard]] constexpr bool EstablishesCausality(Label a_label) noexcept
	{
		return a_label == Label::LocalCausalEffectSupported ||
		       a_label == Label::EndpointCausalEffectSupported;
	}

	// --- A0: plan publication, and the characterized RS-off lag -----------------

	enum class Flow : std::uint8_t
	{
		RenderScaleOff,
		RenderScaleOn,
		HotEnvelope
	};

	enum class A0Outcome : std::uint8_t
	{
		Match,
		// P-1. Under Render Scale off, production does not take its render extent
		// from the planner: resolveVendorDynamicRenderSize() reads resolutionScale,
		// and ConfigureUpscaling writes resolutionScale later in the SAME frame. So
		// for exactly one frame after a quality change the plan carries the
		// PREVIOUS quality's render extent, then catches up.
		//
		// This is characterized, not repaired. A control should be described
		// rather than corrected, and adopting the planner there would silently
		// remove a shipped behaviour in the middle of a protocol that uses that
		// flow as a reference.
		CharacterizedRSOffRenderLag,
		Mismatch,
		Invalid
	};

	/** @brief The lag is exactly one frame wide. It is not an open-ended excuse. */
	inline constexpr std::uint32_t kRSOffRenderExtentLagFrames = 1u;

	struct Extent
	{
		std::uint32_t width{};
		std::uint32_t height{};

		[[nodiscard]] constexpr bool operator==(const Extent&) const noexcept = default;
	};

	struct A0Observation
	{
		Flow flow{ Flow::RenderScaleOn };
		Extent observedAllocation{};
		Extent observedRender{};
		Extent observedOutput{};
		Extent expectedAllocation{};
		Extent expectedRender{};
		Extent expectedOutput{};
		// The render extent the PREVIOUS active quality would have produced. Equal
		// to expectedRender when no quality change is in flight.
		Extent previousExpectedRender{};
		std::uint32_t framesSinceQualityChange{};
	};

	/**
	 * @brief Scores the A0 edge, allowing the characterized RS-off render lag.
	 *
	 * Allocation and output are checked at zero tolerance in every flow. Only the
	 * RENDER extent may lag, only under Render Scale off, and only inside the
	 * one-frame window - because only the render extent is derived from
	 * resolutionScale.
	 */
	[[nodiscard]] constexpr A0Outcome ScoreA0(const A0Observation& a_obs) noexcept
	{
		const auto positive = [](Extent a_extent) {
			return a_extent.width != 0u && a_extent.height != 0u;
		};
		if (!positive(a_obs.observedAllocation) || !positive(a_obs.observedRender) ||
			!positive(a_obs.observedOutput) || !positive(a_obs.expectedAllocation) ||
			!positive(a_obs.expectedRender) || !positive(a_obs.expectedOutput)) {
			return A0Outcome::Invalid;
		}

		// These never lag. The lag mechanism is specific to the render extent.
		if (a_obs.observedAllocation != a_obs.expectedAllocation ||
			a_obs.observedOutput != a_obs.expectedOutput) {
			return A0Outcome::Mismatch;
		}

		if (a_obs.observedRender == a_obs.expectedRender)
			return A0Outcome::Match;

		const bool lagIsAvailable =
			a_obs.flow == Flow::RenderScaleOff &&
			a_obs.framesSinceQualityChange < kRSOffRenderExtentLagFrames + 1u &&
			positive(a_obs.previousExpectedRender) &&
			a_obs.observedRender == a_obs.previousExpectedRender;

		return lagIsAvailable ? A0Outcome::CharacterizedRSOffRenderLag : A0Outcome::Mismatch;
	}

	/** @brief A characterized lag is an admissible control frame, not a defect. */
	[[nodiscard]] constexpr bool A0Admits(A0Outcome a_outcome) noexcept
	{
		return a_outcome == A0Outcome::Match ||
		       a_outcome == A0Outcome::CharacterizedRSOffRenderLag;
	}

	// --- gem 1: pass decision ----------------------------------------------------

	enum class PassOutcome : std::uint8_t
	{
		OriginalCalled,
		Replaced,
		Rejected,
		NoMatchingEventObserved,
		TraceNotInstalled,
		TraceSaturated,
		Inconclusive
	};

	/**
	 * @brief What a pass reason alone can establish about the writer. Nothing.
	 *
	 * `vanillaRuns=YES` means PREDECESSOR_CALLED_BY_WRAPPER. It does not mean the
	 * predecessor wrote anything, and it certainly does not mean it expanded
	 * anything. The D0->W0 edge needs the writer event as well.
	 */
	[[nodiscard]] constexpr bool PassReasonEstablishesWriterBehaviour(PassOutcome) noexcept
	{
		return false;
	}

	/**
	 * @brief Whether a run may conclude the scaling path was never invoked.
	 *
	 * Never at this level. The wrappers sit behind the Frame Annotations startup
	 * path, which the public MGO presets set false, so an absent trace may mean
	 * they were never installed. H1_NOT_INVOKED is an L4 verdict requiring
	 * complete expected-call coverage, writer ancestry and no relevant opaque cut.
	 */
	[[nodiscard]] constexpr bool MayConcludeNotInvoked(
		PassOutcome,
		bool a_haveCompleteExpectedCallCoverage,
		bool a_haveCompleteWriterAncestry,
		bool a_noRelevantOpaqueCuts) noexcept
	{
		return a_haveCompleteExpectedCallCoverage &&
		       a_haveCompleteWriterAncestry &&
		       a_noRelevantOpaqueCuts;
	}

	/** @brief Identical reasons across flows do not make the difference insignificant. */
	[[nodiscard]] constexpr bool ReasonParityFalsifiesSignificance(
		PassOutcome a_rsOn,
		PassOutcome a_hot) noexcept
	{
		// Deliberately always false: RS-on runs at ratio 1 while Hot runs at R/A,
		// so the same branch can mean different things in the two flows.
		(void)a_rsOn;
		(void)a_hot;
		return false;
	}

	// --- gem 3: capacity versus valid field --------------------------------------

	enum class UpscaleMethodClass : std::uint8_t
	{
		DLSS,
		FSR,
		Other
	};

	/** @brief The five quantities, kept apart because collapsing any two is the defect class. */
	enum class Comparator : std::uint8_t
	{
		CommandWriteFootprint,
		DeclaredRegion,
		ProvenDefinedCoverage
	};

	enum class CapacityOutcome : std::uint8_t
	{
		CapacityExceedsCommandFootprint,
		CapacityExceedsDeclaredRegion,
		CapacityExceedsProvenDefinedCoverage,
		CapacityEqualsCommandFootprint,
		CapacityEqualsDeclaredRegion,
		CapacityEqualsProvenDefinedCoverage,
		FsrAllocationContractMaxInputOutput,
		ConsumerOutsideValidField,
		ConsumerFootprintOpaque,
		Inconclusive
	};

	struct CapacityObservation
	{
		UpscaleMethodClass method{ UpscaleMethodClass::DLSS };
		Extent physicalCapacity{};
		Extent comparandExtent{};
		Comparator comparator{ Comparator::CommandWriteFootprint };
		bool comparandProven{ false };
		// FSR allocates inputs at max(input, output) by design and tolerates
		// generation mismatch, so a large capacity there is its contract rather
		// than slack.
		Extent fsrInputExtent{};
		Extent fsrOutputExtent{};
	};

	[[nodiscard]] constexpr Extent MaxExtent(Extent a_lhs, Extent a_rhs) noexcept
	{
		return Extent{
			a_lhs.width > a_rhs.width ? a_lhs.width : a_rhs.width,
			a_lhs.height > a_rhs.height ? a_lhs.height : a_rhs.height
		};
	}

	/**
	 * @brief Classifies capacity. Never emits a verdict without naming what it
	 *        compared against, and never calls the FSR allocation policy slack.
	 */
	[[nodiscard]] constexpr CapacityOutcome ClassifyCapacity(const CapacityObservation& a_obs) noexcept
	{
		const auto positive = [](Extent a_extent) {
			return a_extent.width != 0u && a_extent.height != 0u;
		};
		if (!positive(a_obs.physicalCapacity))
			return CapacityOutcome::Inconclusive;

		if (a_obs.method == UpscaleMethodClass::FSR &&
			positive(a_obs.fsrInputExtent) && positive(a_obs.fsrOutputExtent) &&
			a_obs.physicalCapacity == MaxExtent(a_obs.fsrInputExtent, a_obs.fsrOutputExtent)) {
			return CapacityOutcome::FsrAllocationContractMaxInputOutput;
		}

		// An unproven comparand cannot produce a comparison verdict. In particular
		// DEFINED_CURRENT_FRAME_COVERAGE needs L4 provenance and is not inferable
		// from a descriptor.
		if (!a_obs.comparandProven || !positive(a_obs.comparandExtent))
			return CapacityOutcome::Inconclusive;

		const bool exceeds =
			a_obs.physicalCapacity.width > a_obs.comparandExtent.width ||
			a_obs.physicalCapacity.height > a_obs.comparandExtent.height;

		switch (a_obs.comparator) {
		case Comparator::CommandWriteFootprint:
			return exceeds ? CapacityOutcome::CapacityExceedsCommandFootprint :
			                 CapacityOutcome::CapacityEqualsCommandFootprint;
		case Comparator::DeclaredRegion:
			return exceeds ? CapacityOutcome::CapacityExceedsDeclaredRegion :
			                 CapacityOutcome::CapacityEqualsDeclaredRegion;
		case Comparator::ProvenDefinedCoverage:
		default:
			return exceeds ? CapacityOutcome::CapacityExceedsProvenDefinedCoverage :
			                 CapacityOutcome::CapacityEqualsProvenDefinedCoverage;
		}
	}

	/**
	 * @brief Out-of-coverage failure needs BOTH footprints independently proven.
	 *
	 * A provider tag cannot prove the provider's own sampling behaviour, so an
	 * unproven consumer footprint is opaque rather than passing or failing.
	 */
	[[nodiscard]] constexpr CapacityOutcome ScoreConsumerContainment(
		bool a_consumerFootprintProven,
		bool a_definedCoverageProven,
		Extent a_consumerFootprint,
		Extent a_definedCoverage) noexcept
	{
		if (!a_consumerFootprintProven)
			return CapacityOutcome::ConsumerFootprintOpaque;
		if (!a_definedCoverageProven)
			return CapacityOutcome::Inconclusive;
		const bool outside =
			a_consumerFootprint.width > a_definedCoverage.width ||
			a_consumerFootprint.height > a_definedCoverage.height;
		return outside ? CapacityOutcome::ConsumerOutsideValidField :
		                 CapacityOutcome::CapacityEqualsProvenDefinedCoverage;
	}

	/** @brief Pixels outside proven coverage have unknown provenance, never assumed stale. */
	enum class OutsideCoverageProvenance : std::uint8_t
	{
		Unknown,
		ProvenPreviousFrame,
		ProvenCleared,
		ProvenUninitialized
	};

	[[nodiscard]] constexpr OutsideCoverageProvenance DefaultOutsideCoverageProvenance() noexcept
	{
		return OutsideCoverageProvenance::Unknown;
	}

	// --- gem 4: one normalized bound, two extents --------------------------------

	enum class MaskEdgeOutcome : std::uint8_t
	{
		ApplicableExecuted,
		ApplicableReused,
		NotApplicable,
		SkippedWithReason,
		MissingTelemetry,
		CorrespondenceFail
	};

	/** @brief round(clamp(u,0,1) * extent), matching Util::NormalizedCoordinates. */
	[[nodiscard]] constexpr std::uint32_t ResolveBoundary(float a_normalized, std::uint32_t a_extent) noexcept
	{
		if (a_extent == 0u)
			return 0u;
		const double clamped = a_normalized < 0.0f ? 0.0 :
		                       (a_normalized > 1.0f ? 1.0 : static_cast<double>(a_normalized));
		// llround semantics: nearest, halfway away from zero. Values here are
		// non-negative, so a manual +0.5 truncation is equivalent and constexpr.
		return static_cast<std::uint32_t>(clamped * static_cast<double>(a_extent) + 0.5);
	}

	/**
	 * @brief The two origins the stock submit path produces for one bound.
	 *
	 * Colour resolves against the physical resource; mask depth resolves against
	 * the logical field. They agree only when the field fills the resource -
	 * true in both shipped flows, false under Hot.
	 */
	struct BoundResolution
	{
		std::uint32_t colourOrigin{};
		std::uint32_t maskOrigin{};

		[[nodiscard]] constexpr bool Agree() const noexcept { return colourOrigin == maskOrigin; }
		[[nodiscard]] constexpr std::uint32_t Delta() const noexcept
		{
			return colourOrigin > maskOrigin ? colourOrigin - maskOrigin : maskOrigin - colourOrigin;
		}
	};

	[[nodiscard]] constexpr BoundResolution ResolveBothSpaces(
		float a_normalized,
		std::uint32_t a_resourceExtent,
		std::uint32_t a_logicalFieldExtent) noexcept
	{
		return BoundResolution{
			ResolveBoundary(a_normalized, a_resourceExtent),
			ResolveBoundary(a_normalized, a_logicalFieldExtent)
		};
	}

	/**
	 * @brief The mask edge passes on LOGICAL correspondence, not raw origin equality.
	 *
	 * Different resources may legitimately have different physical layouts. What
	 * must hold is that both transforms land on the same logical hidden-area
	 * boundary. Raw origins differing is not by itself a failure.
	 */
	[[nodiscard]] constexpr MaskEdgeOutcome ScoreMaskEdge(
		bool a_branchApplicable,
		bool a_telemetryComplete,
		bool a_executed,
		bool a_reusedWithProvenance,
		bool a_logicalBoundariesAgree) noexcept
	{
		if (!a_branchApplicable)
			return MaskEdgeOutcome::NotApplicable;
		if (!a_telemetryComplete)
			return MaskEdgeOutcome::MissingTelemetry;
		if (!a_executed && !a_reusedWithProvenance)
			return MaskEdgeOutcome::SkippedWithReason;
		if (!a_logicalBoundariesAgree)
			return MaskEdgeOutcome::CorrespondenceFail;
		return a_executed ? MaskEdgeOutcome::ApplicableExecuted : MaskEdgeOutcome::ApplicableReused;
	}

	/** @brief A skipped branch is applicability, never proof of correct clearing. */
	[[nodiscard]] constexpr bool MaskOutcomeProvesCorrectClearing(MaskEdgeOutcome a_outcome) noexcept
	{
		return a_outcome == MaskEdgeOutcome::ApplicableExecuted ||
		       a_outcome == MaskEdgeOutcome::ApplicableReused;
	}

	// --- gem 5: pre-create property versus created resource ----------------------

	enum class ResourceOutcome : std::uint8_t
	{
		PropertyPathMatchesPlan,
		PostCreateMatchesProperties,
		PlanPropertyDrift,
		PropertyResourceDrift,
		NoPropertyChangeRequired,
		NoCreationEvent,
		Inconclusive
	};

	struct ResourceObservation
	{
		bool hookInvoked{ false };
		bool propertiesChanged{ false };
		Extent planExtent{};
		Extent requestedProperties{};
		bool creationObserved{ false };
		bool postCreateDescriptorRead{ false };
		Extent postCreateDescriptor{};
	};

	/**
	 * @brief A pre-create property is not a created texture, and this refuses to
	 *        pretend otherwise.
	 *
	 * Only a post-create descriptor read can produce PostCreateMatchesProperties.
	 * An absent adjustment means "already equal or no recreation expected" and is
	 * never scored as failure - but it still requires a POSITIVE invocation record.
	 */
	[[nodiscard]] constexpr ResourceOutcome ScoreResource(const ResourceObservation& a_obs) noexcept
	{
		if (!a_obs.hookInvoked)
			return ResourceOutcome::Inconclusive;

		if (!a_obs.propertiesChanged) {
			return a_obs.requestedProperties == a_obs.planExtent ?
			           ResourceOutcome::NoPropertyChangeRequired :
			           ResourceOutcome::PlanPropertyDrift;
		}

		if (a_obs.requestedProperties != a_obs.planExtent)
			return ResourceOutcome::PlanPropertyDrift;

		if (!a_obs.creationObserved)
			return ResourceOutcome::NoCreationEvent;
		if (!a_obs.postCreateDescriptorRead)
			return ResourceOutcome::Inconclusive;

		return a_obs.postCreateDescriptor == a_obs.requestedProperties ?
		           ResourceOutcome::PostCreateMatchesProperties :
		           ResourceOutcome::PropertyResourceDrift;
	}

	/** @brief Which outcomes actually confirm the physical resource. */
	[[nodiscard]] constexpr bool ConfirmsPhysicalResource(ResourceOutcome a_outcome) noexcept
	{
		return a_outcome == ResourceOutcome::PostCreateMatchesProperties;
	}
}

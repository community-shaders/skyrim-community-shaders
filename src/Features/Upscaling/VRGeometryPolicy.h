#pragma once

#include <algorithm>
#include <cstdint>

// One pure derivation of VR render geometry, shared by production and tests.
//
// CSX has three logical geometries - allocation, render extent and output - and
// in every shipped configuration two of the three coincide, so a single name
// like "the render size" was unambiguous. Hot-Envelope is the first
// configuration in which all three differ at once, and the ambiguity became a
// defect class rather than a naming preference.
//
// This header is the single source of truth for how large each of the three is.
// It answers "how big", never "where in a resource" - eye origins and copy boxes
// depend on the layout of a concrete D3D resource and belong in a separate
// binding stage, because the same logical extent can be laid out packed,
// allocation-separated, per-eye or array-sliced.
//
// Deliberately dependency-free and constexpr: no globals, no settings, no
// perfMode, no frame state, no D3D. Everything it needs arrives in Inputs, so
// production and tests cannot drift apart, and the tests are compile-time.
namespace VRGeometryPolicy
{
	enum class Flow : std::uint8_t
	{
		RenderScaleOff,
		RenderScaleOn,
		HotEnvelope
	};

	enum class Phase : std::uint8_t
	{
		Stable,
		// While a physical mutation is in flight the allocation is being rebuilt,
		// so rendering into a sub-rect of it would race the recreation. The
		// render extent collapses to the allocation until the relatch publishes.
		PhysicalRecovery
	};

	enum class Action : std::uint8_t
	{
		Use,
		// The requested render extent does not fit the physical allocation. This
		// is a state decision, not a dimension to clamp silently: a boot
		// allocation sized for a low quality cannot contain a higher one.
		RelatchRequired,
		Invalid
	};

	struct Extent
	{
		std::uint32_t width{};
		std::uint32_t height{};

		[[nodiscard]] constexpr bool operator==(const Extent&) const noexcept = default;
	};

	struct Inputs
	{
		Flow flow{ Flow::RenderScaleOff };
		Phase phase{ Phase::Stable };
		// Per eye, as the headset reports it. Combined stereo is two of these
		// side by side; the distinction is explicit everywhere below because
		// conflating them is one of the errors this header exists to prevent.
		Extent displayPerEye{};
		std::uint32_t bootQuality{};
		std::uint32_t activeQuality{};
	};

	struct Plan
	{
		Extent allocationCombined{};
		Extent renderCombined{};
		Extent outputCombined{};
		Extent renderPerEye{};
		Extent outputPerEye{};
	};

	struct Decision
	{
		Plan plan{};
		Action action{ Action::Invalid };
	};

	inline constexpr std::uint32_t kQualityModeMaxIndex = 6u;

	// Mirrors Upscaling::GetQualityModeResolutionScale exactly, including the
	// float32 division expressions - 1.0f/1.3f is 0.76923078298..., not the
	// decimal 0.769230769, and the difference reaches the truncated integers.
	[[nodiscard]] constexpr float QualityScale(std::uint32_t a_qualityMode) noexcept
	{
		switch (a_qualityMode) {
		case 1:
			return 0.85f;
		case 2:
			return 1.0f / 1.3f;
		case 3:
			return 1.0f / 1.5f;
		case 4:
			return 1.0f / 1.7f;
		case 5:
			return 0.5f;
		case 6:
			return 1.0f / 3.0f;
		default:
			return 1.0f;
		}
	}

	// Mirrors Upscaling::ScaleVRRenderDimension. Applies to a PER EYE dimension.
	//
	// The `& ~1u` even-forcing is reproduced here because phase 0A is
	// behaviour-null, not because it is believed correct. It is a phase 2
	// candidate: it drives the Performance per-eye extent one pixel below the
	// vendor's reported accepted minimum in both axes. See VendorMinimum below.
	[[nodiscard]] constexpr std::uint32_t ScaleVRRenderDimension(
		std::uint32_t a_dimension,
		float a_scale) noexcept
	{
		if (a_dimension < 2u)
			return a_dimension;

		const float scaled = static_cast<float>(a_dimension) * std::clamp(a_scale, 0.1f, 1.0f);
		// The shipped code applies std::floor before the cast. For the strictly
		// positive values this sees, truncation and floor are the same, and the
		// cast is constexpr where std::floor is not guaranteed to be.
		const std::uint32_t truncated = static_cast<std::uint32_t>(scaled);
		const std::uint32_t bounded = std::clamp<std::uint32_t>(truncated, 2u, a_dimension);
		return bounded & ~1u;
	}

	// Mirrors the non-render-scale branch of Upscaling::ConfigureUpscaling,
	// which scales the COMBINED width and does not force even. That difference
	// of operand and rounding is why the two flows disagree by one pixel per eye
	// at qualities 1 through 5.
	[[nodiscard]] constexpr std::uint32_t ScaleCombinedDimension(
		std::uint32_t a_dimension,
		float a_scale) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<float>(a_dimension) * a_scale);
	}

	[[nodiscard]] constexpr Extent CombineStereo(Extent a_perEye) noexcept
	{
		return Extent{ a_perEye.width * 2u, a_perEye.height };
	}

	[[nodiscard]] constexpr Extent ScalePerEye(Extent a_perEye, float a_scale) noexcept
	{
		return Extent{
			ScaleVRRenderDimension(a_perEye.width, a_scale),
			ScaleVRRenderDimension(a_perEye.height, a_scale)
		};
	}

	[[nodiscard]] constexpr bool FitsWithin(Extent a_inner, Extent a_outer) noexcept
	{
		return a_inner.width <= a_outer.width && a_inner.height <= a_outer.height;
	}

	[[nodiscard]] constexpr Decision Derive(const Inputs& a_inputs) noexcept
	{
		Decision decision{};

		if (a_inputs.displayPerEye.width < 2u || a_inputs.displayPerEye.height < 2u ||
			a_inputs.bootQuality > kQualityModeMaxIndex ||
			a_inputs.activeQuality > kQualityModeMaxIndex) {
			decision.action = Action::Invalid;
			return decision;
		}

		const Extent displayCombined = CombineStereo(a_inputs.displayPerEye);
		decision.plan.outputPerEye = a_inputs.displayPerEye;
		decision.plan.outputCombined = displayCombined;

		switch (a_inputs.flow) {
		case Flow::RenderScaleOff: {
			// Targets stay at the display size; the sub-rect comes from the
			// engine's dynamic-resolution ratio.
			const float scale = QualityScale(a_inputs.activeQuality);
			decision.plan.allocationCombined = displayCombined;
			decision.plan.renderCombined = Extent{
				ScaleCombinedDimension(displayCombined.width, scale),
				ScaleCombinedDimension(displayCombined.height, scale)
			};
			// Integer division, so an odd combined width leaves its last column
			// uncovered by the two eye regions. Reproduced, not corrected.
			decision.plan.renderPerEye = Extent{
				decision.plan.renderCombined.width / 2u,
				decision.plan.renderCombined.height
			};
			decision.action = Action::Use;
			break;
		}

		case Flow::RenderScaleOn: {
			// Targets are allocated at the boot quality and fully rendered into.
			const Extent perEye = ScalePerEye(a_inputs.displayPerEye, QualityScale(a_inputs.bootQuality));
			decision.plan.allocationCombined = CombineStereo(perEye);
			decision.plan.renderCombined = decision.plan.allocationCombined;
			decision.plan.renderPerEye = perEye;
			decision.action = Action::Use;
			break;
		}

		case Flow::HotEnvelope: {
			const Extent allocationPerEye = ScalePerEye(a_inputs.displayPerEye, QualityScale(a_inputs.bootQuality));
			const Extent renderPerEye = ScalePerEye(a_inputs.displayPerEye, QualityScale(a_inputs.activeQuality));
			decision.plan.allocationCombined = CombineStereo(allocationPerEye);

			if (a_inputs.phase == Phase::PhysicalRecovery) {
				decision.plan.renderPerEye = allocationPerEye;
				decision.plan.renderCombined = decision.plan.allocationCombined;
				decision.action = Action::Use;
				break;
			}

			decision.plan.renderPerEye = renderPerEye;
			decision.plan.renderCombined = CombineStereo(renderPerEye);
			decision.action = FitsWithin(renderPerEye, allocationPerEye) ?
			                      Action::Use :
			                      Action::RelatchRequired;
			break;
		}
		}

		return decision;
	}

	// Vendor input constraints, measured with slDLSSGetOptimalSettings at
	// 3494x3558 per eye and recorded in CSX_HOT_ENVELOPE_POC.md.
	//
	// These are CHARACTERIZATION helpers in phase 0A: they describe the contract
	// so the current violations are visible and testable, and they are
	// deliberately not enforced yet. Phase 2 decides whether to reconcile the
	// sizing against them. Enforcing them now would make a behaviour-null
	// extraction impossible to land.
	namespace VendorMinimum
	{
		inline constexpr Extent kRangedModeMinimum{ 1747u, 1779u };
		inline constexpr Extent kUltraPerformanceFixed{ 1165u, 1186u };

		[[nodiscard]] constexpr bool IsUltraPerformance(std::uint32_t a_qualityMode) noexcept
		{
			return a_qualityMode == 6u;
		}

		[[nodiscard]] constexpr bool SatisfiesReportedContract(
			std::uint32_t a_qualityMode,
			Extent a_renderPerEye) noexcept
		{
			if (IsUltraPerformance(a_qualityMode))
				return a_renderPerEye == kUltraPerformanceFixed;

			return a_renderPerEye.width >= kRangedModeMinimum.width &&
			       a_renderPerEye.height >= kRangedModeMinimum.height;
		}
	}
}

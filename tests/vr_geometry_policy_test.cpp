#include "Features/Upscaling/VRGeometryPolicy.h"

#include <cstdint>

namespace
{
	using namespace VRGeometryPolicy;

	// The Pimax Crystal Super fixture every archived measurement was taken on.
	// Retained as a golden case; the sweeps below are what make the policy
	// agnostic to headset, refresh mode and Image Quality changes.
	inline constexpr Extent kPimaxPerEye{ 3494u, 3558u };

	constexpr Decision Off(std::uint32_t a_quality, Extent a_display = kPimaxPerEye)
	{
		return Derive(Inputs{ Flow::RenderScaleOff, Phase::Stable, a_display, a_quality, a_quality });
	}

	constexpr Decision On(std::uint32_t a_quality, Extent a_display = kPimaxPerEye)
	{
		return Derive(Inputs{ Flow::RenderScaleOn, Phase::Stable, a_display, a_quality, a_quality });
	}

	constexpr Decision Env(std::uint32_t a_boot, std::uint32_t a_active, Extent a_display = kPimaxPerEye)
	{
		return Derive(Inputs{ Flow::HotEnvelope, Phase::Stable, a_display, a_boot, a_active });
	}

	constexpr Decision EnvRecovering(std::uint32_t a_boot, std::uint32_t a_active)
	{
		return Derive(Inputs{ Flow::HotEnvelope, Phase::PhysicalRecovery, kPimaxPerEye, a_boot, a_active });
	}

	// ---------------------------------------------------------------------
	// Golden values. Every render-scale row below reproduces an observed boot
	// latch from CommunityShaders.log, and the RS-off Quality row reproduces
	// StereoTrace's captured 4658x2372 viewport with eyes at 0-2329, 2329-4658.
	// ---------------------------------------------------------------------

	constexpr bool CoversRenderScaleOnGoldenValues()
	{
		return On(0).plan.allocationCombined == Extent{ 6988u, 3558u } &&
		       On(1).plan.allocationCombined == Extent{ 5936u, 3024u } &&
		       On(2).plan.allocationCombined == Extent{ 5372u, 2736u } &&
		       On(3).plan.allocationCombined == Extent{ 4656u, 2372u } &&
		       On(4).plan.allocationCombined == Extent{ 4108u, 2092u } &&
		       On(5).plan.allocationCombined == Extent{ 3492u, 1778u } &&
		       On(6).plan.allocationCombined == Extent{ 2328u, 1186u };
	}

	constexpr bool CoversRenderScaleOffGoldenValues()
	{
		return Off(0).plan.renderCombined == Extent{ 6988u, 3558u } &&
		       Off(1).plan.renderCombined == Extent{ 5939u, 3024u } &&
		       Off(2).plan.renderCombined == Extent{ 5375u, 2736u } &&
		       Off(3).plan.renderCombined == Extent{ 4658u, 2372u } &&
		       Off(4).plan.renderCombined == Extent{ 4110u, 2092u } &&
		       Off(5).plan.renderCombined == Extent{ 3494u, 1779u } &&
		       Off(6).plan.renderCombined == Extent{ 2329u, 1186u };
	}

	// RS-off scales the combined width and does not force even; the render-scale
	// path scales per eye and does. They therefore disagree by one pixel per eye
	// at qualities 1 through 5, and agree at 0 and 6.
	constexpr bool CoversFlowArithmeticDivergence()
	{
		for (std::uint32_t q = 1u; q <= 5u; ++q) {
			if (Off(q).plan.renderPerEye.width == On(q).plan.renderPerEye.width)
				return false;
		}
		return Off(0).plan.renderPerEye.width == On(0).plan.renderPerEye.width &&
		       Off(6).plan.renderPerEye.width == On(6).plan.renderPerEye.width;
	}

	// An odd combined width leaves its final column uncovered by the two eye
	// regions. Characterized, not corrected: it is current RS-off behaviour.
	constexpr bool CoversRenderScaleOffOrphanedColumn()
	{
		constexpr std::uint32_t kOddWidthQualities[] = { 1u, 2u, 6u };
		for (const std::uint32_t q : kOddWidthQualities) {
			const Decision d = Off(q);
			if ((d.plan.renderCombined.width % 2u) == 0u)
				return false;
			if (d.plan.renderPerEye.width * 2u == d.plan.renderCombined.width)
				return false;
		}
		// The even-width qualities lose nothing.
		constexpr std::uint32_t kEvenWidthQualities[] = { 0u, 3u, 4u, 5u };
		for (const std::uint32_t q : kEvenWidthQualities) {
			const Decision d = Off(q);
			if (d.plan.renderPerEye.width * 2u != d.plan.renderCombined.width)
				return false;
		}
		return true;
	}

	// ---------------------------------------------------------------------
	// Metamorphic invariants across the full 7 x 7 envelope matrix.
	// ---------------------------------------------------------------------

	constexpr bool CoversEnvelopeMatrix()
	{
		for (std::uint32_t boot = 0u; boot <= kQualityModeMaxIndex; ++boot) {
			for (std::uint32_t active = 0u; active <= kQualityModeMaxIndex; ++active) {
				const Decision env = Env(boot, active);
				const Decision onBoot = On(boot);
				const Decision onActive = On(active);

				// Allocation follows the boot quality, render follows the active
				// quality, output is always the display contract.
				if (env.plan.allocationCombined != onBoot.plan.allocationCombined)
					return false;
				if (env.plan.renderCombined != onActive.plan.renderCombined)
					return false;
				if (env.plan.outputCombined != CombineStereo(kPimaxPerEye))
					return false;

				// A higher active quality than the boot allocation can hold is a
				// state decision, never a silent clamp.
				const bool fits = FitsWithin(env.plan.renderPerEye, onBoot.plan.renderPerEye);
				if (fits && env.action != Action::Use)
					return false;
				if (!fits && env.action != Action::RelatchRequired)
					return false;
				if (env.action == Action::Use &&
					!FitsWithin(env.plan.renderCombined, env.plan.allocationCombined)) {
					return false;
				}
			}
		}
		return true;
	}

	// The diagonal of the matrix is exactly render-scale mode.
	constexpr bool CoversEnvelopeDiagonalEqualsRenderScaleOn()
	{
		for (std::uint32_t q = 0u; q <= kQualityModeMaxIndex; ++q) {
			const Decision env = Env(q, q);
			const Decision on = On(q);
			if (env.plan.allocationCombined != on.plan.allocationCombined ||
				env.plan.renderCombined != on.plan.renderCombined ||
				env.plan.renderPerEye != on.plan.renderPerEye ||
				env.action != Action::Use) {
				return false;
			}
		}
		return true;
	}

	// While a physical mutation is in flight the render extent collapses to the
	// allocation, whatever was requested.
	constexpr bool CoversPhysicalRecoveryCollapsesToAllocation()
	{
		for (std::uint32_t boot = 0u; boot <= kQualityModeMaxIndex; ++boot) {
			for (std::uint32_t active = 0u; active <= kQualityModeMaxIndex; ++active) {
				const Decision d = EnvRecovering(boot, active);
				if (d.action != Action::Use)
					return false;
				if (d.plan.renderCombined != d.plan.allocationCombined)
					return false;
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------
	// Properties that must hold for any headset, not just the Pimax fixture.
	// ---------------------------------------------------------------------

	constexpr bool CoversDisplayDimensionSweep()
	{
		constexpr Extent kDisplays[] = {
			{ 3494u, 3558u },  // Pimax Crystal Super, the golden fixture
			{ 3495u, 3559u },  // odd in both axes
			{ 2064u, 2208u },  // Quest-class
			{ 1440u, 1600u },  // Index-class
			{ 2560u, 2560u },  // square
			{ 4096u, 2160u },  // wide
			{ 960u, 1080u }    // small, to catch scale-dependent assumptions
		};

		for (const Extent display : kDisplays) {
			for (std::uint32_t q = 0u; q <= kQualityModeMaxIndex; ++q) {
				const Decision on = On(q, display);
				const Decision off = Off(q, display);

				if (on.action != Action::Use || off.action != Action::Use)
					return false;
				// Positive, and never larger than the display.
				if (on.plan.renderPerEye.width < 2u || on.plan.renderPerEye.height < 2u)
					return false;
				if (!FitsWithin(on.plan.renderPerEye, display))
					return false;
				// Combined width is exactly the sum of its eye regions.
				if (on.plan.renderCombined.width != on.plan.renderPerEye.width * 2u)
					return false;
				// Output is always the captured display contract.
				if (off.plan.outputPerEye != display || on.plan.outputPerEye != display)
					return false;
				// Render-scale mode renders the whole allocation.
				if (on.plan.renderCombined != on.plan.allocationCombined)
					return false;
			}
		}
		return true;
	}

	// Render extent never increases as quality index rises down the ladder.
	constexpr bool CoversMonotonicLadder()
	{
		for (std::uint32_t q = 1u; q <= kQualityModeMaxIndex; ++q) {
			if (On(q).plan.renderPerEye.width > On(q - 1u).plan.renderPerEye.width)
				return false;
			if (On(q).plan.renderPerEye.height > On(q - 1u).plan.renderPerEye.height)
				return false;
		}
		return true;
	}

	constexpr bool CoversDeterminismAndValidation()
	{
		if (!(Env(3u, 5u).plan.renderCombined == Env(3u, 5u).plan.renderCombined))
			return false;
		// Out-of-range quality and degenerate displays are rejected, not clamped.
		if (Derive(Inputs{ Flow::HotEnvelope, Phase::Stable, kPimaxPerEye, 7u, 3u }).action != Action::Invalid)
			return false;
		if (Derive(Inputs{ Flow::RenderScaleOn, Phase::Stable, Extent{ 1u, 1u }, 3u, 3u }).action != Action::Invalid)
			return false;
		return true;
	}

	// ---------------------------------------------------------------------
	// Characterization of the vendor-contract violations.
	//
	// These assert what the code does TODAY, including where it is outside the
	// range Streamline reports. They are not a claim that the current values are
	// correct - they exist so that phase 2 changes the behaviour deliberately
	// and visibly, rather than a refactor changing it by accident.
	// ---------------------------------------------------------------------

	constexpr bool CharacterizesPerformanceBelowVendorMinimum()
	{
		// Working RS-off lands exactly on the reported minimum.
		if (Off(5).plan.renderPerEye != Extent{ 1747u, 1779u })
			return false;
		if (!VendorMinimum::SatisfiesReportedContract(5u, Off(5).plan.renderPerEye))
			return false;

		// Render-scale mode is one pixel below it in BOTH axes.
		if (On(5).plan.renderPerEye != Extent{ 1746u, 1778u })
			return false;
		if (VendorMinimum::SatisfiesReportedContract(5u, On(5).plan.renderPerEye))
			return false;

		return true;
	}

	constexpr bool CharacterizesUltraPerformanceBelowFixedExtent()
	{
		// Both flows produce 1164 against a reported fixed 1165, so this one is
		// not a difference between the flows - it is a shared violation.
		return Off(6).plan.renderPerEye == Extent{ 1164u, 1186u } &&
		       On(6).plan.renderPerEye == Extent{ 1164u, 1186u } &&
		       !VendorMinimum::SatisfiesReportedContract(6u, On(6).plan.renderPerEye) &&
		       !VendorMinimum::SatisfiesReportedContract(6u, Off(6).plan.renderPerEye);
	}

	constexpr bool CharacterizesQualityWithinRange()
	{
		// Quality is inside the range in both flows, so any visual difference
		// there is not explained by the vendor contract.
		return VendorMinimum::SatisfiesReportedContract(3u, Off(3).plan.renderPerEye) &&
		       VendorMinimum::SatisfiesReportedContract(3u, On(3).plan.renderPerEye) &&
		       Off(3).plan.renderPerEye == Extent{ 2329u, 2372u } &&
		       On(3).plan.renderPerEye == Extent{ 2328u, 2372u };
	}

	static_assert(CoversRenderScaleOnGoldenValues());
	static_assert(CoversRenderScaleOffGoldenValues());
	static_assert(CoversFlowArithmeticDivergence());
	static_assert(CoversRenderScaleOffOrphanedColumn());
	static_assert(CoversEnvelopeMatrix());
	static_assert(CoversEnvelopeDiagonalEqualsRenderScaleOn());
	static_assert(CoversPhysicalRecoveryCollapsesToAllocation());
	static_assert(CoversDisplayDimensionSweep());
	static_assert(CoversMonotonicLadder());
	static_assert(CoversDeterminismAndValidation());
	static_assert(CharacterizesPerformanceBelowVendorMinimum());
	static_assert(CharacterizesUltraPerformanceBelowFixedExtent());
	static_assert(CharacterizesQualityWithinRange());
}

int main() {}

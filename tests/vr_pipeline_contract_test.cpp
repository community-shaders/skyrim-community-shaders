// Phase 1 of CDO-001: the finite candidate transforms, fixed before any runtime
// data is seen.
//
// Every assertion is a static_assert, so a wrong candidate is a build error
// rather than a red test. That matters more here than usual: these values are
// the oracle a later capture is scored against, and an oracle written after
// seeing the data is not an oracle.
//
// All extents come from VRGeometryPolicy rather than being retyped. This test
// therefore cannot disagree with production about a size, and no quality-scale
// constant is duplicated - which is how this project previously produced float
// labels that were wrong in the fourth decimal.

#include "Features/Upscaling/VRGeometryPolicy.h"
#include "Features/Upscaling/VRPipelineContract.h"

#include <cstdint>
#include <initializer_list>

namespace
{
	namespace VP = VRPipelineContract;
	namespace GP = VRGeometryPolicy;

	inline constexpr GP::Extent kPimaxPerEye{ 3494u, 3558u };

	[[nodiscard]] constexpr VP::Extent ToContract(GP::Extent a_extent) noexcept
	{
		return VP::Extent{ a_extent.width, a_extent.height };
	}

	[[nodiscard]] constexpr GP::Decision Envelope(
		std::uint32_t a_boot,
		std::uint32_t a_active,
		GP::Extent a_display = kPimaxPerEye) noexcept
	{
		return GP::Derive(GP::Inputs{ GP::Flow::HotEnvelope, GP::Phase::Stable, a_display, a_boot, a_active });
	}

	[[nodiscard]] constexpr GP::Decision RenderScaleOn(
		std::uint32_t a_quality,
		GP::Extent a_display = kPimaxPerEye) noexcept
	{
		return GP::Derive(GP::Inputs{ GP::Flow::RenderScaleOn, GP::Phase::Stable, a_display, a_quality, a_quality });
	}

	// The allocation is published combined. Halving it is only sound because
	// both flows build it as CombineStereo(perEye), so the width is even by
	// construction - asserted rather than assumed, immediately below.
	[[nodiscard]] constexpr VP::Extent AllocationPerEye(const GP::Plan& a_plan) noexcept
	{
		return VP::Extent{ a_plan.allocationCombined.width / 2u, a_plan.allocationCombined.height };
	}

	[[nodiscard]] constexpr bool AllocationHalvesExactly() noexcept
	{
		for (std::uint32_t boot = 0u; boot <= 6u; ++boot) {
			if (RenderScaleOn(boot).plan.allocationCombined.width % 2u != 0u)
				return false;
			for (std::uint32_t active = 0u; active <= 6u; ++active) {
				if (Envelope(boot, active).plan.allocationCombined.width % 2u != 0u)
					return false;
			}
		}
		return true;
	}
	static_assert(AllocationHalvesExactly(),
		"AllocationPerEye divides by two; an odd combined allocation would make it lossy.");

	[[nodiscard]] constexpr VP::Inputs InputsFor(
		std::uint32_t a_boot,
		std::uint32_t a_active,
		std::uint8_t a_eye,
		GP::Extent a_display = kPimaxPerEye) noexcept
	{
		const auto decision = Envelope(a_boot, a_active, a_display);
		return VP::Inputs{
			AllocationPerEye(decision.plan),
			ToContract(decision.plan.renderPerEye),
			ToContract(a_display),
			a_eye,
			1u,
			1u
		};
	}

	// The worked case throughout the protocol: boot Quality, active Balanced.
	inline constexpr std::uint32_t kQuality = 3u;
	inline constexpr std::uint32_t kBalanced = 4u;
	inline constexpr std::uint32_t kPerformance = 5u;
	inline constexpr std::uint32_t kUltraPerformance = 6u;

	inline constexpr VP::Inputs kHE_Q3_Q4 = InputsFor(kQuality, kBalanced, 0u);

	// --- the golden geometry, cross-checked against the policy -------------------

	static_assert(kHE_Q3_Q4.allocationPerEye == VP::Extent{ 2328u, 2372u });
	static_assert(kHE_Q3_Q4.renderPerEye == VP::Extent{ 2054u, 2092u });
	static_assert(kHE_Q3_Q4.outputPerEye == VP::Extent{ 3494u, 3558u });
	static_assert(InputsFor(kQuality, kPerformance, 0u).renderPerEye == VP::Extent{ 1746u, 1778u });
	static_assert(InputsFor(kQuality, kUltraPerformance, 0u).renderPerEye == VP::Extent{ 1164u, 1186u });
	static_assert(InputsFor(kQuality, kQuality, 0u).renderPerEye == VP::Extent{ 2328u, 2372u });

	// --- the two valid paths reach the output complete ---------------------------

	[[nodiscard]] constexpr bool PathReachesOutputComplete(VP::Path a_path, VP::Layout a_layout) noexcept
	{
		for (std::uint8_t eye = 0u; eye < 2u; ++eye) {
			const auto inputs = InputsFor(kQuality, kBalanced, eye);
			const auto submit = VP::Expect(VP::Boundary::SubmitB7, a_path, inputs, a_layout);
			if (!VP::SatisfiesSubmitContract(submit, inputs.outputPerEye))
				return false;
		}
		return true;
	}

	static_assert(PathReachesOutputComplete(VP::Path::Raw, VP::Layout::PackedStereo));
	static_assert(PathReachesOutputComplete(VP::Path::Raw, VP::Layout::AllocationSeparatedStereo));
	static_assert(PathReachesOutputComplete(VP::Path::Raw, VP::Layout::PerEyeTexture));
	static_assert(PathReachesOutputComplete(VP::Path::Raw, VP::Layout::ArraySliced));
	static_assert(PathReachesOutputComplete(VP::Path::Expanded, VP::Layout::PackedStereo));
	static_assert(PathReachesOutputComplete(VP::Path::Expanded, VP::Layout::AllocationSeparatedStereo));
	static_assert(PathReachesOutputComplete(VP::Path::Expanded, VP::Layout::PerEyeTexture));
	static_assert(PathReachesOutputComplete(VP::Path::Expanded, VP::Layout::ArraySliced));

	// The point of section 7.1: Raw and Expanded disagree about every
	// intermediate extent and still both deliver a complete field. The defect is
	// not choosing A over R, it is mixing one path's coordinate state with the
	// other path's dimensions.
	static_assert(
		VP::Expect(VP::Boundary::VendorInputB4, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PerEyeTexture).resourceExtent !=
		VP::Expect(VP::Boundary::VendorInputB4, VP::Path::Expanded, kHE_Q3_Q4, VP::Layout::PerEyeTexture).resourceExtent);

	// --- the predicted duplicate-scale matrices ----------------------------------

	// H1 fails the submit contract, and fails it by exactly A_eye / R_eye.
	static_assert(!PathReachesOutputComplete(VP::Path::DuplicateExpansion, VP::Layout::PackedStereo));
	static_assert(!PathReachesOutputComplete(VP::Path::DuplicateExpansion, VP::Layout::AllocationSeparatedStereo));
	static_assert(!PathReachesOutputComplete(VP::Path::DuplicateExpansion, VP::Layout::PerEyeTexture));
	static_assert(!PathReachesOutputComplete(VP::Path::DuplicateExpansion, VP::Layout::ArraySliced));

	[[nodiscard]] constexpr VP::FieldCoverage DuplicateSubmitCoverage(std::uint32_t a_active) noexcept
	{
		return VP::Expect(
			VP::Boundary::SubmitB7,
			VP::Path::DuplicateExpansion,
			InputsFor(kQuality, a_active, 0u),
			VP::Layout::PerEyeTexture)
			.coverage;
	}

	// Exact rationals, compared by cross-multiplication. Nothing is divided, so
	// there is no decimal to get wrong.
	static_assert(VP::CoverageZoomX(DuplicateSubmitCoverage(kBalanced)) == VP::Ratio{ 2328u, 2054u });
	static_assert(VP::CoverageZoomY(DuplicateSubmitCoverage(kBalanced)) == VP::Ratio{ 2372u, 2092u });
	static_assert(VP::CoverageZoomX(DuplicateSubmitCoverage(kPerformance)) == VP::Ratio{ 2328u, 1746u });
	static_assert(VP::CoverageZoomY(DuplicateSubmitCoverage(kPerformance)) == VP::Ratio{ 2372u, 1778u });
	static_assert(VP::CoverageZoomX(DuplicateSubmitCoverage(kUltraPerformance)) == VP::Ratio{ 2u, 1u });
	static_assert(VP::CoverageZoomY(DuplicateSubmitCoverage(kUltraPerformance)) == VP::Ratio{ 2u, 1u });

	// The prediction is anisotropic at Balanced and Performance and isotropic at
	// UltraPerformance, because the even-forcing in the render arithmetic lands
	// differently on the two axes. That asymmetry is a discriminator, so it is
	// pinned rather than left to be noticed later.
	static_assert(!(VP::CoverageZoomX(DuplicateSubmitCoverage(kBalanced)) ==
					 VP::CoverageZoomY(DuplicateSubmitCoverage(kBalanced))));
	static_assert(!(VP::CoverageZoomX(DuplicateSubmitCoverage(kPerformance)) ==
					 VP::CoverageZoomY(DuplicateSubmitCoverage(kPerformance))));
	static_assert(VP::CoverageZoomX(DuplicateSubmitCoverage(kUltraPerformance)) ==
				  VP::CoverageZoomY(DuplicateSubmitCoverage(kUltraPerformance)));

	// On the envelope diagonal the defect is unobservable: A_eye == R_eye, so the
	// duplicate expansion is the identity and all three paths agree. This is the
	// one preset where the image is known to be correct, and the model has to
	// reproduce that rather than merely be consistent with it.
	static_assert(VP::CoverageZoomX(DuplicateSubmitCoverage(kQuality)).IsUnity());
	static_assert(VP::CoverageZoomY(DuplicateSubmitCoverage(kQuality)).IsUnity());

	[[nodiscard]] constexpr bool DiagonalIsIndistinguishable() noexcept
	{
		const auto inputs = InputsFor(kQuality, kQuality, 0u);
		for (const auto boundary : { VP::Boundary::SceneB2, VP::Boundary::DynamicResolutionB3,
				 VP::Boundary::VendorInputB4, VP::Boundary::VendorOutputB6, VP::Boundary::SubmitB7 }) {
			const auto expanded = VP::Expect(boundary, VP::Path::Expanded, inputs, VP::Layout::PerEyeTexture);
			const auto duplicate = VP::Expect(boundary, VP::Path::DuplicateExpansion, inputs, VP::Layout::PerEyeTexture);
			if (!(expanded == duplicate))
				return false;
		}
		return true;
	}
	static_assert(DiagonalIsIndistinguishable(),
		"On the diagonal the defect must vanish - that is why the image is correct there.");

	// --- the full 7 x 7 boot x active matrix -------------------------------------

	// Two properties, over every boot and active pair and both eyes:
	//   a fitting pair must let both valid paths deliver a complete field;
	//   the duplicate path must fail exactly off the diagonal and only there.
	[[nodiscard]] constexpr bool MatrixHolds(VP::Layout a_layout) noexcept
	{
		for (std::uint32_t boot = 0u; boot <= 6u; ++boot) {
			for (std::uint32_t active = 0u; active <= 6u; ++active) {
				for (std::uint8_t eye = 0u; eye < 2u; ++eye) {
					const auto inputs = InputsFor(boot, active, eye);
					const bool fits =
						inputs.renderPerEye.width <= inputs.allocationPerEye.width &&
						inputs.renderPerEye.height <= inputs.allocationPerEye.height;
					if (!fits)
						continue;  // covered by NonFittingPairsAreRejected below

					const auto raw = VP::Expect(VP::Boundary::SubmitB7, VP::Path::Raw, inputs, a_layout);
					const auto expanded = VP::Expect(VP::Boundary::SubmitB7, VP::Path::Expanded, inputs, a_layout);
					const auto duplicate = VP::Expect(VP::Boundary::SubmitB7, VP::Path::DuplicateExpansion, inputs, a_layout);

					if (!VP::SatisfiesSubmitContract(raw, inputs.outputPerEye))
						return false;
					if (!VP::SatisfiesSubmitContract(expanded, inputs.outputPerEye))
						return false;

					const bool onDiagonal = inputs.renderPerEye == inputs.allocationPerEye;
					if (VP::SatisfiesSubmitContract(duplicate, inputs.outputPerEye) != onDiagonal)
						return false;
					if (VP::CoverageZoomX(duplicate.coverage).IsUnity() != onDiagonal)
						return false;
				}
			}
		}
		return true;
	}

	static_assert(MatrixHolds(VP::Layout::PackedStereo));
	static_assert(MatrixHolds(VP::Layout::AllocationSeparatedStereo));
	static_assert(MatrixHolds(VP::Layout::PerEyeTexture));
	static_assert(MatrixHolds(VP::Layout::ArraySliced));

	// --- invalid containment -----------------------------------------------------

	// An active quality above the boot quality does not fit its allocation. The
	// model must say so rather than clamp: whether a non-fitting request should
	// return a size at all is a state decision, not a dimension to repair.
	[[nodiscard]] constexpr bool NonFittingPairsAreRejected() noexcept
	{
		bool sawOne = false;
		for (std::uint32_t boot = 0u; boot <= 6u; ++boot) {
			for (std::uint32_t active = 0u; active <= 6u; ++active) {
				const auto inputs = InputsFor(boot, active, 0u);
				const bool fits =
					inputs.renderPerEye.width <= inputs.allocationPerEye.width &&
					inputs.renderPerEye.height <= inputs.allocationPerEye.height;
				if (fits)
					continue;
				sawOne = true;
				const auto state = VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, inputs, VP::Layout::PerEyeTexture);
				if (state.validity != VP::Validity::RenderExceedsAllocation)
					return false;
				if (VP::SatisfiesSubmitContract(
						VP::Expect(VP::Boundary::SubmitB7, VP::Path::Raw, inputs, VP::Layout::PerEyeTexture),
						inputs.outputPerEye))
					return false;
			}
		}
		return sawOne;  // a matrix in which nothing failed to fit would prove nothing
	}
	static_assert(NonFittingPairsAreRejected());

	// A covered region larger than the field it claims to be part of.
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw,
			VP::Inputs{ VP::Extent{ 2328u, 2372u }, VP::Extent{ 0u, 2092u }, VP::Extent{ 3494u, 3558u }, 0u, 1u, 1u },
			VP::Layout::PerEyeTexture)
			.validity == VP::Validity::EmptyExtent);

	// An eye index no layout can place.
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw,
			VP::Inputs{ VP::Extent{ 2328u, 2372u }, VP::Extent{ 2054u, 2092u }, VP::Extent{ 3494u, 3558u }, 2u, 1u, 1u },
			VP::Layout::PerEyeTexture)
			.validity == VP::Validity::LayoutUnsupported);

	// --- invalid generation combinations -----------------------------------------

	static_assert(VP::JoinGenerations(kHE_Q3_Q4, kHE_Q3_Q4) == VP::Validity::Valid);
	static_assert(
		VP::JoinGenerations(
			kHE_Q3_Q4,
			VP::Inputs{ kHE_Q3_Q4.allocationPerEye, kHE_Q3_Q4.renderPerEye, kHE_Q3_Q4.outputPerEye, 0u, 2u, 1u }) ==
		VP::Validity::GenerationMismatch);
	static_assert(
		VP::JoinGenerations(
			kHE_Q3_Q4,
			VP::Inputs{ kHE_Q3_Q4.allocationPerEye, kHE_Q3_Q4.renderPerEye, kHE_Q3_Q4.outputPerEye, 0u, 1u, 7u }) ==
		VP::Validity::GenerationMismatch);

	// --- layouts place the eyes differently, and both look legal -----------------

	// This is the pair of conventions already built and already falsified by
	// experiment. Neither is wrong on its face; they simply differ, which is why
	// the protocol records the layout instead of choosing one.
	[[nodiscard]] constexpr VP::Offset SceneOrigin(VP::Layout a_layout, std::uint8_t a_eye) noexcept
	{
		return VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, InputsFor(kQuality, kBalanced, a_eye), a_layout)
			.activeRegion.origin;
	}

	static_assert(SceneOrigin(VP::Layout::PackedStereo, 0u) == VP::Offset{ 0u, 0u });
	static_assert(SceneOrigin(VP::Layout::PackedStereo, 1u) == VP::Offset{ 2054u, 0u });
	static_assert(SceneOrigin(VP::Layout::AllocationSeparatedStereo, 0u) == VP::Offset{ 0u, 0u });
	static_assert(SceneOrigin(VP::Layout::AllocationSeparatedStereo, 1u) == VP::Offset{ 2328u, 0u });
	static_assert(SceneOrigin(VP::Layout::PerEyeTexture, 1u) == VP::Offset{ 0u, 0u });
	static_assert(SceneOrigin(VP::Layout::ArraySliced, 1u) == VP::Offset{ 0u, 0u });

	// Both stereo layouts occupy the same combined resource; only the origin moves.
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PackedStereo).resourceExtent ==
		VP::Extent{ 4656u, 2372u });
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::AllocationSeparatedStereo).resourceExtent ==
		VP::Extent{ 4656u, 2372u });
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PerEyeTexture).resourceExtent ==
		VP::Extent{ 2328u, 2372u });

	// Per-eye and array-slice resources are tagged differently even though their
	// extents match, because a combined number is not a claim about layout.
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::ArraySliced).tag ==
		VP::ExtentTag{ VP::Stereo::ArraySlice, 0u, 0u });
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, InputsFor(kQuality, kBalanced, 1u), VP::Layout::ArraySliced).tag ==
		VP::ExtentTag{ VP::Stereo::ArraySlice, 1u, 1u });
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PerEyeTexture).tag ==
		VP::ExtentTag{ VP::Stereo::PerEye, 0u, 0u });
	static_assert(
		VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PackedStereo).tag ==
		VP::ExtentTag{ VP::Stereo::CombinedStereo, 0u, 0u });

	// --- transform composition ---------------------------------------------------

	inline constexpr VP::Affine kIdentity{ 1.0f, 1.0f, 0.0f, 0.0f };

	[[nodiscard]] constexpr bool AffineEq(const VP::Affine& a_lhs, const VP::Affine& a_rhs) noexcept
	{
		return a_lhs.sx == a_rhs.sx && a_lhs.sy == a_rhs.sy &&
		       a_lhs.tx == a_rhs.tx && a_lhs.ty == a_rhs.ty;
	}

	static_assert(AffineEq(VP::Compose(kIdentity, kIdentity), kIdentity));
	static_assert(AffineEq(VP::Compose(kIdentity, VP::Affine{ 2.0f, 4.0f, 8.0f, 16.0f }),
		VP::Affine{ 2.0f, 4.0f, 8.0f, 16.0f }));
	// M_out = T * M_in: the outer scale multiplies the inner translation.
	static_assert(AffineEq(
		VP::Compose(VP::Affine{ 2.0f, 2.0f, 1.0f, 1.0f }, VP::Affine{ 3.0f, 3.0f, 5.0f, 5.0f }),
		VP::Affine{ 6.0f, 6.0f, 11.0f, 11.0f }));

	// Every float below is an exact float32 value: the products stay under 2^24
	// and every division is by a factor of its numerator. Chosen deliberately -
	// a candidate transform that needed a tolerance to assert would not be a
	// candidate the analyzer could be scored against.
	[[nodiscard]] constexpr VP::Affine SubmitAffine(VP::Path a_path, std::uint32_t a_active) noexcept
	{
		return VP::ToAffine(
			VP::Expect(VP::Boundary::SubmitB7, a_path, InputsFor(kQuality, a_active, 0u), VP::Layout::PerEyeTexture));
	}

	// Correct: the complete logical eye field spans exactly the output eye, once.
	static_assert(AffineEq(SubmitAffine(VP::Path::Raw, kUltraPerformance), VP::Affine{ 3494.0f, 3558.0f, 0.0f, 0.0f }));
	static_assert(AffineEq(SubmitAffine(VP::Path::Expanded, kUltraPerformance), VP::Affine{ 3494.0f, 3558.0f, 0.0f, 0.0f }));
	static_assert(AffineEq(SubmitAffine(VP::Path::Raw, kQuality), VP::Affine{ 3494.0f, 3558.0f, 0.0f, 0.0f }));

	// H1 at UltraPerformance inside a Quality envelope: A_eye / R_eye is exactly
	// 2 in both axes, so the complete field would need twice the output to fit -
	// half of it is off the edge. This is the 2.00x row of the prediction table,
	// expressed as a matrix rather than as a number to be eyeballed.
	static_assert(AffineEq(SubmitAffine(VP::Path::DuplicateExpansion, kUltraPerformance),
		VP::Affine{ 6988.0f, 7116.0f, 0.0f, 0.0f }));

	// And on the diagonal it collapses back to the correct transform.
	static_assert(AffineEq(SubmitAffine(VP::Path::DuplicateExpansion, kQuality),
		VP::Affine{ 3494.0f, 3558.0f, 0.0f, 0.0f }));

	// --- no production consumer reads this yet -----------------------------------
	//
	// Phase 1 exit requires the candidate transforms to compile with their golden
	// values fixed, and nothing else. VRPipelineContract.h is included by this
	// test only; the plugin build must not change behaviour because of it.

	// The separated stride is the per-eye half of *that* resource. At B7 that is
	// the output half, not the engine allocation - striding an output-space
	// resource by 2328 would be meaningless, and would still have produced a
	// contained, legal-looking region.
	[[nodiscard]] constexpr VP::Offset SubmitOrigin(VP::Layout a_layout, std::uint8_t a_eye) noexcept
	{
		return VP::Expect(VP::Boundary::SubmitB7, VP::Path::Raw, InputsFor(kQuality, kBalanced, a_eye), a_layout)
			.activeRegion.origin;
	}

	static_assert(SubmitOrigin(VP::Layout::AllocationSeparatedStereo, 1u) == VP::Offset{ 3494u, 0u });
	static_assert(SubmitOrigin(VP::Layout::PackedStereo, 1u) == VP::Offset{ 3494u, 0u });

	// Where a resource is exactly field-sized the two stereo conventions
	// coincide; where it is not, they differ. Both are true statements about the
	// same pair of layouts, which is why neither can be assumed.
	static_assert(
		VP::Expect(VP::Boundary::VendorInputB4, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::PackedStereo).activeRegion ==
		VP::Expect(VP::Boundary::VendorInputB4, VP::Path::Raw, kHE_Q3_Q4, VP::Layout::AllocationSeparatedStereo).activeRegion);
	static_assert(
		!(VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, InputsFor(kQuality, kBalanced, 1u), VP::Layout::PackedStereo).activeRegion ==
			VP::Expect(VP::Boundary::SceneB2, VP::Path::Raw, InputsFor(kQuality, kBalanced, 1u), VP::Layout::AllocationSeparatedStereo).activeRegion));

	// Nothing the model produces may claim pixels its resource does not have.
	[[nodiscard]] constexpr bool EveryStateIsContained() noexcept
	{
		for (std::uint32_t boot = 0u; boot <= 6u; ++boot) {
			for (std::uint32_t active = 0u; active <= 6u; ++active) {
				const auto inputs = InputsFor(boot, active, 1u);
				if (inputs.renderPerEye.width > inputs.allocationPerEye.width ||
					inputs.renderPerEye.height > inputs.allocationPerEye.height)
					continue;
				for (const auto layout : { VP::Layout::PackedStereo, VP::Layout::AllocationSeparatedStereo,
						 VP::Layout::PerEyeTexture, VP::Layout::ArraySliced }) {
					for (const auto path : { VP::Path::Raw, VP::Path::Expanded, VP::Path::DuplicateExpansion }) {
						for (const auto boundary : { VP::Boundary::SceneB2, VP::Boundary::DynamicResolutionB3,
								 VP::Boundary::VendorInputB4, VP::Boundary::VendorOutputB6, VP::Boundary::SubmitB7 }) {
							const auto state = VP::Expect(boundary, path, inputs, layout);
							if (state.validity != VP::Validity::Valid)
								return false;
							if (!VP::ContainedIn(state.activeRegion, state.resourceExtent))
								return false;
						}
					}
				}
			}
		}
		return true;
	}
	static_assert(EveryStateIsContained());

	[[nodiscard]] constexpr bool AllSevenQualitiesModelled() noexcept
	{
		for (std::uint32_t quality = 0u; quality <= 6u; ++quality) {
			const auto inputs = InputsFor(quality, quality, 0u);
			if (inputs.allocationPerEye != inputs.renderPerEye)
				return false;  // the diagonal must be A == R for every quality
			if (!VP::SatisfiesSubmitContract(
					VP::Expect(VP::Boundary::SubmitB7, VP::Path::Raw, inputs, VP::Layout::PerEyeTexture),
					inputs.outputPerEye))
				return false;
		}
		return true;
	}
	static_assert(AllSevenQualitiesModelled());
	// --------------------------------------- vanilla upsample fallback safety

	// The three configurations, which is the whole point of the predicate.
	// Render Scale ON: allocation == render, vanilla is correct.
	static_assert(VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4108u, 2092u, 4108u, 2092u));
	// Render Scale OFF: allocation == render at full size, likewise.
	static_assert(VRPipelineContract::VanillaUpsampleIsSafeFallback(
		6988u, 3558u, 6988u, 3558u));
	// Hot-Envelope below the envelope: allocation exceeds render, vanilla reads
	// past the rendered region. THE MEASURED CASE.
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4108u, 2092u, 4656u, 2372u));

	// A single differing axis is enough to make the fallback unsafe. Both must
	// agree, or vanilla's assumption is broken on that axis.
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4108u, 2092u, 4656u, 2092u));
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4108u, 2092u, 4108u, 2372u));

	// An allocation SMALLER than the render extent is not "safe" either. It
	// should never happen, and treating an impossible state as safe is how a
	// guard becomes a formality.
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4656u, 2372u, 4108u, 2092u));

	// Unknown extents refuse. An absent allocation must not read as agreement -
	// refusing keeps the replacement running, which is correct wherever the
	// allocation is genuinely absent, because A equals R there by definition.
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		4108u, 2092u, 0u, 0u));
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		0u, 0u, 4656u, 2372u));
	static_assert(!VRPipelineContract::VanillaUpsampleIsSafeFallback(
		0u, 0u, 0u, 0u));

	// ------------------------------------------- menu composite destination size

	// THE MEASURED CASE, 2026-08-25. Boot quality 3, active 4, envelope open.
	// The destination is the ALLOCATION and the old check had no case for it, so
	// every menu frame was poisoned with destination-size-mismatch and the menu
	// was never composited - audible, interactive, invisible.
	static_assert(VRPipelineContract::IsMenuDestinationSizeValid(
		4656u, 2372u,   // destination = allocation
		4108u, 2092u,   // render
		4656u, 2372u,   // allocation
		6988u, 3558u)); // output

	// The two the original check already accepted must keep working.
	static_assert(VRPipelineContract::IsMenuDestinationSizeValid(
		4108u, 2092u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));
	static_assert(VRPipelineContract::IsMenuDestinationSizeValid(
		6988u, 3558u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));

	// A destination matching NONE of the three is still a mismatch. Widening the
	// predicate must not turn it into a rubber stamp.
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		1234u, 5678u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));

	// Width right, height wrong - both must match, and vice versa.
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		4656u, 2092u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		4108u, 2372u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));

	// The pre-Hot-Envelope configurations, which is why this never fired before.
	// Render Scale ON: allocation == render.
	static_assert(VRPipelineContract::IsMenuDestinationSizeValid(
		4108u, 2092u, 4108u, 2092u, 4108u, 2092u, 6988u, 3558u));
	// Render Scale OFF: allocation == output.
	static_assert(VRPipelineContract::IsMenuDestinationSizeValid(
		6988u, 3558u, 6988u, 3558u, 6988u, 3558u, 6988u, 3558u));

	// A zero destination is never valid, even against a zero candidate. Nothing
	// composites into a zero-sized target, and letting 0 == 0 pass would turn an
	// unset plan into a silent acceptance.
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u));
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		0u, 2372u, 4108u, 2092u, 4656u, 2372u, 6988u, 3558u));

	// An unset candidate must not match anything either - the same rule from the
	// other side.
	static_assert(!VRPipelineContract::IsMenuDestinationSizeValid(
		4656u, 2372u, 4108u, 2092u, 0u, 0u, 6988u, 3558u));
}

int main()
{
	// Everything is asserted at compile time. Reaching main means the candidate
	// transform set compiled with its golden values intact.
	return 0;
}

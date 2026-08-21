#pragma once

#include <cstdint>

// The pipeline contract model: what each boundary is supposed to contain, and
// which part of the logical eye image its pixels represent.
//
// VRGeometryPolicy answers "how large". This header answers the second question
// the six failed attempts kept getting wrong:
//
//     A texture's dimensions do not describe the coordinate state of the image
//     inside it.
//
// An A-sized texture may hold a complete field still occupying an R-sized
// region, or the same field already resampled to fill A. Both have the same
// D3D description and can show activity across the same columns. Cropping R
// pixels from the second and expanding that crop again produces a deterministic
// zoom of A/R, with every individual dimension numerically legal.
//
// So a boundary is described here by three things, never by a size alone:
//
//     the physical resource extent          - how big
//     the active region inside it           - where
//     the field coverage                    - which part of the complete
//                                             logical eye image those pixels are
//
// Deliberately dependency-free and constexpr: no float2, no D3D, no settings,
// no VRGeometryPolicy. Per-eye extents arrive as inputs, so this header never
// re-derives a quality scale and cannot disagree with the policy about a size.
// That is a correctness decision, not a layering preference: duplicating the
// float32 scale table is exactly how this project previously produced labels
// that were wrong in the fourth decimal.
//
// Coverage is carried as exact integers and compared as exact rationals. No
// tolerance, no float equality, nothing to round. Floats appear only in the
// affine form the offline analyzer fits against, and that form is derived from
// the integer model rather than maintained beside it.
namespace VRPipelineContract
{
	// --- basic geometry ---------------------------------------------------------

	struct Extent
	{
		std::uint32_t width{};
		std::uint32_t height{};

		[[nodiscard]] constexpr bool operator==(const Extent&) const noexcept = default;
	};

	struct Offset
	{
		std::uint32_t x{};
		std::uint32_t y{};

		[[nodiscard]] constexpr bool operator==(const Offset&) const noexcept = default;
	};

	/** @brief Half-open pixel-edge region: [x, x+width) x [y, y+height). */
	struct Region
	{
		Offset origin{};
		Extent extent{};

		[[nodiscard]] constexpr bool operator==(const Region&) const noexcept = default;

		[[nodiscard]] constexpr std::uint32_t Right() const noexcept { return origin.x + extent.width; }
		[[nodiscard]] constexpr std::uint32_t Bottom() const noexcept { return origin.y + extent.height; }
	};

	/** @brief An exact ratio. Compared by cross-multiplication, never by dividing. */
	struct Ratio
	{
		std::uint32_t num{};
		std::uint32_t den{};

		[[nodiscard]] constexpr bool operator==(const Ratio& a_other) const noexcept
		{
			return static_cast<std::uint64_t>(num) * a_other.den ==
			       static_cast<std::uint64_t>(a_other.num) * den;
		}

		[[nodiscard]] constexpr bool IsUnity() const noexcept { return num == den && den != 0u; }

		/** @brief For reporting only. Never use the result to decide a contract. */
		[[nodiscard]] constexpr float Approx() const noexcept
		{
			return den == 0u ? 0.0f : static_cast<float>(num) / static_cast<float>(den);
		}
	};

	// --- tagging ----------------------------------------------------------------

	// Protocol section 2: no geometry value is unqualified.
	enum class Stereo : std::uint8_t
	{
		CombinedStereo,
		PerEye,
		ArraySlice
	};

	struct ExtentTag
	{
		Stereo stereo{ Stereo::PerEye };
		std::uint8_t eye{};
		std::uint8_t slice{};

		[[nodiscard]] constexpr bool operator==(const ExtentTag&) const noexcept = default;
	};

	/** @brief How the two eyes are arranged in a concrete resource. */
	enum class Layout : std::uint8_t
	{
		// Both active fields adjacent: eye 1 starts where eye 0's active field ends.
		PackedStereo,
		// Each eye owns an allocation-sized half; the active field sits at the
		// start of its half and the remainder is unused.
		AllocationSeparatedStereo,
		// One resource per eye.
		PerEyeTexture,
		// One resource, one array slice per eye.
		ArraySliced
	};

	[[nodiscard]] constexpr bool IsStereoPacked(Layout a_layout) noexcept
	{
		return a_layout == Layout::PackedStereo || a_layout == Layout::AllocationSeparatedStereo;
	}

	// --- field coverage ---------------------------------------------------------

	/**
	 * @brief Which part of the complete logical eye image a region holds.
	 *
	 * `fieldExtent` is the complete logical eye image expressed in the pixel
	 * units this boundary works in. `covered` is the part of it actually
	 * present. Both are exact integers, so "is the whole field here" is an
	 * equality test rather than a comparison against a tolerance.
	 *
	 * This is what distinguishes the two textures in the header comment. A raw
	 * field in an A-sized resource has `fieldExtent == R_eye` and covers all of
	 * it. A field already resampled to A has `fieldExtent == A_eye` and covers
	 * all of it. A crop of R pixels taken from the resampled one has
	 * `fieldExtent == A_eye` but covers only `R_eye` - and that is the defect,
	 * visible here and invisible in any resource description.
	 */
	struct FieldCoverage
	{
		Extent fieldExtent{};
		Region covered{};

		[[nodiscard]] constexpr bool operator==(const FieldCoverage&) const noexcept = default;
	};

	[[nodiscard]] constexpr FieldCoverage CompleteField(Extent a_field) noexcept
	{
		return FieldCoverage{ a_field, Region{ Offset{ 0u, 0u }, a_field } };
	}

	[[nodiscard]] constexpr bool CoversCompleteField(const FieldCoverage& a_coverage) noexcept
	{
		return a_coverage.fieldExtent.width != 0u &&
		       a_coverage.fieldExtent.height != 0u &&
		       a_coverage.covered.origin.x == 0u &&
		       a_coverage.covered.origin.y == 0u &&
		       a_coverage.covered.extent == a_coverage.fieldExtent;
	}

	/**
	 * @brief How much the covered part must be magnified to fill the output.
	 *
	 * Unity when the complete field is present, which is the only correct value
	 * at the submit boundary. `fieldExtent / covered` is exactly H1's predicted
	 * `A_eye / R_eye` when a crop of R has been taken from a field already
	 * resampled to A.
	 *
	 * Returned per axis on purpose. The two axes do not agree at Balanced or
	 * Performance, because the even-forcing in the render-extent arithmetic
	 * lands differently on width and height, and an observed zoom that is
	 * isotropic where the prediction is anisotropic is evidence against a pure
	 * duplicate expansion.
	 */
	[[nodiscard]] constexpr Ratio CoverageZoomX(const FieldCoverage& a_coverage) noexcept
	{
		return Ratio{ a_coverage.fieldExtent.width, a_coverage.covered.extent.width };
	}

	[[nodiscard]] constexpr Ratio CoverageZoomY(const FieldCoverage& a_coverage) noexcept
	{
		return Ratio{ a_coverage.fieldExtent.height, a_coverage.covered.extent.height };
	}

	// --- affine form, for the analyzer ------------------------------------------

	/**
	 * @brief Normalized logical eye coordinates to physical resource pixels.
	 *
	 * `x = sx * u + tx`, `y = sy * v + ty`, with `u` and `v` spanning the
	 * complete logical eye image over [0,1]. This is the form the offline
	 * analyzer fits from image correspondence, so the model has to be able to
	 * emit it - but it is derived from the integer model below, never authored
	 * beside it.
	 */
	struct Affine
	{
		float sx{ 0.0f };
		float sy{ 0.0f };
		float tx{ 0.0f };
		float ty{ 0.0f };
	};

	/** @brief `M_out = T * M_in`, protocol section 2.1. */
	[[nodiscard]] constexpr Affine Compose(const Affine& a_outer, const Affine& a_inner) noexcept
	{
		return Affine{
			a_outer.sx * a_inner.sx,
			a_outer.sy * a_inner.sy,
			a_outer.sx * a_inner.tx + a_outer.tx,
			a_outer.sy * a_inner.ty + a_outer.ty
		};
	}

	// --- validity ---------------------------------------------------------------

	enum class Validity : std::uint8_t
	{
		Valid,
		EmptyExtent,
		RenderExceedsAllocation,
		RegionExceedsResource,
		CoverageExceedsField,
		GenerationMismatch,
		LayoutUnsupported
	};

	[[nodiscard]] constexpr bool ContainedIn(const Region& a_region, Extent a_resource) noexcept
	{
		return a_region.Right() <= a_resource.width && a_region.Bottom() <= a_resource.height;
	}

	[[nodiscard]] constexpr bool FitsWithin(Extent a_inner, Extent a_outer) noexcept
	{
		return a_inner.width <= a_outer.width && a_inner.height <= a_outer.height;
	}

	// --- the two internally valid paths, and the predicted defect ---------------

	// Protocol section 7.1. Both of the first two are internally consistent; the
	// defect is not that one uses A and the other R, it is mixing the coordinate
	// state of one with the dimensions of the other.
	enum class Path : std::uint8_t
	{
		// The engine expansion is suppressed. The vendor consumes the complete
		// field at R_eye and reconstructs it to O_eye.
		Raw,
		// An earlier pass resamples the complete field to A_eye. Everything
		// downstream consumes that complete A_eye field.
		Expanded,
		// H1: the field is resampled to A_eye, then only R_eye of it is cropped
		// and expanded again. Every resource description stays legal; the
		// complete field never reaches the output.
		DuplicateExpansion
	};

	// The image-carrying boundaries of the protocol graph. B0 and B1 are plan
	// and camera state, and B5 auxiliaries have their own correspondence
	// contracts; neither is a colour-field coverage question.
	enum class Boundary : std::uint8_t
	{
		SceneB2,
		DynamicResolutionB3,
		VendorInputB4,
		VendorOutputB6,
		SubmitB7
	};

	struct Inputs
	{
		Extent allocationPerEye{};
		Extent renderPerEye{};
		Extent outputPerEye{};
		std::uint8_t eye{};
		std::uint32_t contractGeneration{};
		std::uint32_t historyGeneration{};
	};

	/**
	 * @brief What one boundary should contain, under one path hypothesis.
	 *
	 * `resourceExtent` and `activeRegion` are the physical facts a capture can
	 * confirm directly. `coverage` is the part a resource description cannot
	 * answer, and is why this header exists.
	 *
	 * Coverage stays in **pre-vendor field units** through B6 and B7. The
	 * vendor's magnification to the output extent changes `resourceExtent`, not
	 * which part of the logical field is present, so "an O_eye resource holding
	 * R_eye of an A_eye field" is expressible exactly, in integers, with no
	 * rounding anywhere.
	 */
	struct BoundaryState
	{
		Extent resourceExtent{};
		Region activeRegion{};
		FieldCoverage coverage{};
		ExtentTag tag{};
		Layout layout{ Layout::PerEyeTexture };
		Validity validity{ Validity::Valid };

		[[nodiscard]] constexpr bool operator==(const BoundaryState&) const noexcept = default;
	};

	/**
	 * @brief Where eye `i` starts, for a given layout.
	 *
	 * Packed stereo strides by the **active field width at that boundary**.
	 * Separated stereo strides by the **per-eye half of that resource** - not by
	 * the engine allocation, which would be meaningless once the resource is in
	 * output space. Where a resource happens to be exactly field-sized the two
	 * conventions coincide; where it does not, they differ, and both origins look
	 * equally legal in a resource description.
	 *
	 * These are the two conventions already built and already falsified by
	 * experiment. That is why the layout is a recorded input here rather than a
	 * constant: the capture is supposed to answer this, not the model.
	 */
	[[nodiscard]] constexpr std::uint32_t EyeOriginX(
		Layout a_layout,
		std::uint8_t a_eye,
		std::uint32_t a_activeFieldWidth,
		std::uint32_t a_spacePerEyeWidth) noexcept
	{
		switch (a_layout) {
		case Layout::PackedStereo:
			return a_eye * a_activeFieldWidth;
		case Layout::AllocationSeparatedStereo:
			return a_eye * a_spacePerEyeWidth;
		case Layout::PerEyeTexture:
		case Layout::ArraySliced:
		default:
			return 0u;
		}
	}

	namespace detail
	{
		[[nodiscard]] constexpr bool IsPositive(Extent a_extent) noexcept
		{
			return a_extent.width != 0u && a_extent.height != 0u;
		}

		/** @brief Assembles one boundary state. Active region and field coverage are separate
		 * parameters on purpose: at the vendor output they differ - the whole output
		 * resource is active while only part of the logical field is present. */
		[[nodiscard]] constexpr BoundaryState Make(
			const Inputs& a_inputs,
			Layout a_layout,
			Extent a_spacePerEye,
			Extent a_activeExtent,
			Extent a_fieldExtent,
			Extent a_coveredExtent) noexcept
		{
			BoundaryState state{};
			state.layout = a_layout;
			state.tag = ExtentTag{
				IsStereoPacked(a_layout) ? Stereo::CombinedStereo :
				                           (a_layout == Layout::ArraySliced ? Stereo::ArraySlice : Stereo::PerEye),
				a_inputs.eye,
				static_cast<std::uint8_t>(a_layout == Layout::ArraySliced ? a_inputs.eye : 0u)
			};

			state.resourceExtent = IsStereoPacked(a_layout) ?
			                           Extent{ a_spacePerEye.width * 2u, a_spacePerEye.height } :
			                           a_spacePerEye;

			state.activeRegion = Region{
				Offset{
					EyeOriginX(a_layout, a_inputs.eye, a_activeExtent.width, a_spacePerEye.width),
					0u
				},
				a_activeExtent
			};

			state.coverage = FieldCoverage{
				a_fieldExtent,
				Region{ Offset{ 0u, 0u }, a_coveredExtent }
			};

			if (a_inputs.eye > 1u)
				state.validity = Validity::LayoutUnsupported;
			else if (!IsPositive(a_inputs.allocationPerEye) || !IsPositive(a_inputs.renderPerEye) ||
					 !IsPositive(a_inputs.outputPerEye) ||
					 !IsPositive(a_activeExtent) || !IsPositive(a_coveredExtent))
				state.validity = Validity::EmptyExtent;
			else if (!FitsWithin(a_inputs.renderPerEye, a_inputs.allocationPerEye))
				state.validity = Validity::RenderExceedsAllocation;
			else if (!FitsWithin(a_coveredExtent, a_fieldExtent))
				state.validity = Validity::CoverageExceedsField;
			else if (!ContainedIn(state.activeRegion, state.resourceExtent))
				state.validity = Validity::RegionExceedsResource;

			return state;
		}
	}

	/**
	 * @brief The expected state at one boundary, under one path hypothesis.
	 *
	 * The layout is a parameter rather than a constant because protocol section
	 * 5.1 requires layout to be **recorded** at each boundary. A model that
	 * assumed one would be answering a question the capture is supposed to ask.
	 */
	[[nodiscard]] constexpr BoundaryState Expect(
		Boundary a_boundary,
		Path a_path,
		const Inputs& a_inputs,
		Layout a_layout) noexcept
	{
		const Extent alloc = a_inputs.allocationPerEye;
		const Extent render = a_inputs.renderPerEye;
		const Extent output = a_inputs.outputPerEye;

		// Every path starts the same way: the scene produced the complete field,
		// and it occupies R_eye pixels inside an allocation-space resource.
		if (a_boundary == Boundary::SceneB2)
			return detail::Make(a_inputs, a_layout, alloc, render, render, render);

		const bool expansionRan = a_path != Path::Raw;

		// After B3 the field is either untouched at R_eye, or resampled to A_eye.
		const Extent fieldAfterB3 = expansionRan ? alloc : render;
		// The resource stays allocation-space either way: Raw leaves the field at
		// R_eye inside the A-sized target rather than moving it.
		const Extent spaceAfterB3 = alloc;

		if (a_boundary == Boundary::DynamicResolutionB3)
			return detail::Make(a_inputs, a_layout, spaceAfterB3, fieldAfterB3, fieldAfterB3, fieldAfterB3);

		// B4 is where the three paths separate. Raw and Expanded hand the vendor
		// the complete field. DuplicateExpansion hands it an R_eye crop of an
		// A_eye field - a resource that is entirely legal and entirely active,
		// holding R/A of the picture.
		const Extent coveredAtB4 = (a_path == Path::DuplicateExpansion) ? render : fieldAfterB3;
		const Extent spaceAtB4 = coveredAtB4;

		if (a_boundary == Boundary::VendorInputB4)
			return detail::Make(a_inputs, a_layout, spaceAtB4, coveredAtB4, fieldAfterB3, coveredAtB4);

		// B6 and B7: the vendor reconstructs to the output extent. That changes
		// the resource, not which part of the field is present, so coverage is
		// carried forward unchanged and stays exact.
		return detail::Make(a_inputs, a_layout, output, output, fieldAfterB3, coveredAtB4);
	}

	// --- scoring ----------------------------------------------------------------

	/**
	 * @brief The submit contract: the complete logical eye field reaches the
	 *        complete output eye exactly once.
	 *
	 * Two separate conditions, deliberately not merged. A resource of the right
	 * size proves nothing about coverage, and complete coverage in a
	 * wrong-sized resource is still wrong.
	 */
	[[nodiscard]] constexpr bool SatisfiesSubmitContract(
		const BoundaryState& a_state,
		Extent a_outputPerEye) noexcept
	{
		const Extent expectedResource = IsStereoPacked(a_state.layout) ?
		                                    Extent{ a_outputPerEye.width * 2u, a_outputPerEye.height } :
		                                    a_outputPerEye;

		return a_state.validity == Validity::Valid &&
		       CoversCompleteField(a_state.coverage) &&
		       a_state.resourceExtent == expectedResource &&
		       a_state.activeRegion.extent == a_outputPerEye;
	}

	/** @brief Two records can only be joined if they describe the same contract. */
	[[nodiscard]] constexpr Validity JoinGenerations(const Inputs& a_lhs, const Inputs& a_rhs) noexcept
	{
		return (a_lhs.contractGeneration == a_rhs.contractGeneration &&
				   a_lhs.historyGeneration == a_rhs.historyGeneration) ?
		           Validity::Valid :
		           Validity::GenerationMismatch;
	}

	/**
	 * @brief The analyzer-facing form: complete logical eye field to physical pixels.
	 *
	 * Derived from the integer model rather than authored beside it, so the two
	 * cannot drift. The complete field would span
	 * `activeRegion.width * fieldExtent.width / covered.width` physical pixels;
	 * when the whole field is present that is just the active width.
	 */
	[[nodiscard]] constexpr Affine ToAffine(const BoundaryState& a_state) noexcept
	{
		const auto& covered = a_state.coverage.covered;
		if (covered.extent.width == 0u || covered.extent.height == 0u)
			return Affine{};

		const float activeWidth = static_cast<float>(a_state.activeRegion.extent.width);
		const float activeHeight = static_cast<float>(a_state.activeRegion.extent.height);
		const float fieldSpanX = activeWidth *
		                         static_cast<float>(a_state.coverage.fieldExtent.width) /
		                         static_cast<float>(covered.extent.width);
		const float fieldSpanY = activeHeight *
		                         static_cast<float>(a_state.coverage.fieldExtent.height) /
		                         static_cast<float>(covered.extent.height);

		// Where the complete field's u=0 edge would fall, given that the covered
		// part starts at covered.origin within the field.
		const float originShiftX = fieldSpanX *
		                           static_cast<float>(covered.origin.x) /
		                           static_cast<float>(a_state.coverage.fieldExtent.width);
		const float originShiftY = fieldSpanY *
		                           static_cast<float>(covered.origin.y) /
		                           static_cast<float>(a_state.coverage.fieldExtent.height);

		return Affine{
			fieldSpanX,
			fieldSpanY,
			static_cast<float>(a_state.activeRegion.origin.x) - originShiftX,
			static_cast<float>(a_state.activeRegion.origin.y) - originShiftY
		};
	}
}

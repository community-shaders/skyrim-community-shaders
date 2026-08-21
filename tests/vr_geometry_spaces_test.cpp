// Phase 1a guard rail. VRGeometrySpaces.h carries its own static_asserts, so
// including it here is already most of the test - a wrong property is a build
// error. What this file adds is the part that is easy to lose in a later
// refactor: that the tags are still doing work, and that the retype did not
// change the shape of the data underneath.
//
// "It compiled" is not evidence that a mechanical retype is behaviour-null, so
// the checks below are about layout and substitution resistance rather than
// about arithmetic. The arithmetic lives in vr_geometry_policy_test.cpp.

#include "Features/Upscaling/VRGeometrySpaces.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace
{
	using VRGeometry::AllocationExtent;
	using VRGeometry::DisplayExtent;
	using VRGeometry::OutputExtent;
	using VRGeometry::RenderExtent;

	// --- the tags are distinct types, not aliases -------------------------------
	// If someone "simplifies" the template away, or makes two spaces the same
	// struct, every guarantee in phase 1a evaporates silently. This catches that.

	static_assert(!std::is_same_v<RenderExtent, AllocationExtent>);
	static_assert(!std::is_same_v<RenderExtent, OutputExtent>);
	static_assert(!std::is_same_v<AllocationExtent, OutputExtent>);
	static_assert(!std::is_same_v<DisplayExtent, RenderExtent>);

	// --- substitution resistance, stated as overload resolution -----------------
	// The header asserts non-convertibility. This asserts the consequence that
	// actually matters at a call site: a function that wants one geometry cannot
	// be fed another.

	constexpr int WantsRender(const RenderExtent&) noexcept { return 1; }
	constexpr int WantsAllocation(const AllocationExtent&) noexcept { return 2; }

	static_assert(WantsRender(RenderExtent{ 4656.0f, 4744.0f }) == 1);
	static_assert(WantsAllocation(AllocationExtent{ 4656.0f, 4744.0f }) == 2);

	// Stated with is_invocable over a function pointer: a plain, unambiguous way
	// to ask "can this consumer be handed that geometry?".
	static_assert(std::is_invocable_v<decltype(&WantsRender), RenderExtent>);
	static_assert(!std::is_invocable_v<decltype(&WantsRender), AllocationExtent>);
	static_assert(!std::is_invocable_v<decltype(&WantsRender), OutputExtent>);
	static_assert(!std::is_invocable_v<decltype(&WantsAllocation), RenderExtent>);
	// Equal numbers in different geometries are still different things.
	static_assert(!std::is_invocable_v<decltype(&WantsAllocation), DisplayExtent>);

	// --- the retype did not move any bytes --------------------------------------
	// RuntimeResolutionPlan is not memcpy'd today, but a later change that starts
	// doing so must not silently reinterpret a different layout, and the fields
	// used to be a two-float pair.

	static_assert(sizeof(RenderExtent) == sizeof(AllocationExtent));
	static_assert(sizeof(RenderExtent) == 8);
	static_assert(offsetof(RenderExtent, width) == offsetof(AllocationExtent, width));
	static_assert(offsetof(RenderExtent, height) == offsetof(AllocationExtent, height));

	// --- values survive unchanged ------------------------------------------------
	// Phase 1a must preserve float semantics exactly: no promotion to double, no
	// rounding, no normalisation on the way in or out.

	static_assert(RenderExtent{ 2328.0f, 2372.0f }.width == 2328.0f);
	static_assert(RenderExtent{ 2328.0f, 2372.0f }.height == 2372.0f);
	static_assert(RenderExtent{}.width == 0.0f);
	static_assert(RenderExtent{}.height == 0.0f);
	static_assert(std::is_same_v<decltype(RenderExtent{}.width), float>);

	// The odd-width display that produces CSX's one-column asymmetry, carried
	// through the type without change.
	static_assert(VRGeometry::MakeExtent<VRGeometry::RenderSpace>(5939.0f, 3558.0f).width == 5939.0f);
	static_assert(std::is_same_v<
		decltype(VRGeometry::MakeExtent<VRGeometry::RenderSpace>(1.0f, 1.0f)),
		RenderExtent>);

	// --- the layout still matches a bare pair of floats ---------------------------
	// Checked at runtime rather than by static_assert because it is a
	// representation question, and a reinterpreting read is not constexpr.
	[[nodiscard]] bool LayoutMatchesBarePair() noexcept
	{
		const RenderExtent tagged{ 1746.0f, 1778.0f };
		float raw[2]{};
		std::memcpy(raw, &tagged, sizeof(raw));
		return raw[0] == 1746.0f && raw[1] == 1778.0f;
	}
}

int main()
{
	return LayoutMatchesBarePair() ? 0 : 1;
}

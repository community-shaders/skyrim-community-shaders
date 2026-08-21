#pragma once

#include <cstddef>
#include <type_traits>

// Distinct C++ types for CSX's three VR geometries, so the compiler - not a
// play session - enumerates every consumer.
//
// The defect this exists to catch: allocation, render extent and output are
// three different quantities, but in every shipped configuration two of the
// three coincide (Render Scale off: allocation == output; Render Scale on:
// allocation == render), so `float2` was an honest type for all of them.
// Hot-Envelope is the first configuration in which all three differ at once,
// and a consumer that asks the old question now gets an answer that used to be
// right. Reading the code cannot find those consumers reliably, because the
// wrong ones look exactly like the right ones. Giving each geometry its own
// type makes the compiler produce the list.
//
// What this guarantees, precisely: a whole RenderExtent cannot reach a consumer
// that wants an AllocationExtent, and every field access has to be rewritten,
// so the inventory is exhaustive. What it does not guarantee: `.width` is a
// bare float on both, so a component can still cross. Component hardening is a
// later, narrower step aimed at the flows this inventory shows to be risky.
//
// Deliberately dependency-free: no float2, no D3D, no CS headers. Conversion to
// and from float2 lives at the call site through the named adapters below, so
// every loss of type information is one grep away.
namespace VRGeometry
{
	// Space tags. Empty by design - they exist only to make two otherwise
	// identical structures non-interchangeable.

	/** @brief The physical render targets Skyrim allocated. Does not move while the envelope holds. */
	struct AllocationSpace
	{};

	/** @brief The sub-rect actually drawn this frame. At or below the allocation. */
	struct RenderSpace
	{};

	/** @brief What the compositor is handed. */
	struct OutputSpace
	{};

	/** @brief What the headset reports, before any scaling. */
	struct DisplaySpace
	{};

	/**
	 * @brief A width and height in one named geometry.
	 *
	 * Field names are `width`/`height` rather than `x`/`y` on purpose: renaming
	 * them is what forces every existing access to be rewritten, which is how
	 * the consumer inventory becomes exhaustive rather than best-effort.
	 */
	template <class Space>
	struct Extent2F
	{
		float width{ 0.0f };
		float height{ 0.0f };

		[[nodiscard]] constexpr bool operator==(const Extent2F&) const noexcept = default;
	};

	using AllocationExtent = Extent2F<AllocationSpace>;
	using RenderExtent = Extent2F<RenderSpace>;
	using OutputExtent = Extent2F<OutputSpace>;
	using DisplayExtent = Extent2F<DisplaySpace>;

	// The properties the retype must not have changed. Phase 1a is behaviour-
	// null, and "it compiled" is not evidence of that on its own.
	static_assert(std::is_aggregate_v<RenderExtent>);
	static_assert(std::is_standard_layout_v<RenderExtent>);
	static_assert(std::is_trivially_copyable_v<RenderExtent>);
	static_assert(std::is_trivially_destructible_v<RenderExtent>);
	static_assert(sizeof(RenderExtent) == 2 * sizeof(float));
	static_assert(alignof(RenderExtent) == alignof(float));
	static_assert(offsetof(RenderExtent, width) == 0);
	static_assert(offsetof(RenderExtent, height) == sizeof(float));

	// The whole point: the three geometries do not convert into one another, in
	// either direction, by any implicit route.
	static_assert(!std::is_convertible_v<RenderExtent, AllocationExtent>);
	static_assert(!std::is_convertible_v<AllocationExtent, RenderExtent>);
	static_assert(!std::is_convertible_v<RenderExtent, OutputExtent>);
	static_assert(!std::is_convertible_v<OutputExtent, RenderExtent>);
	static_assert(!std::is_convertible_v<AllocationExtent, OutputExtent>);
	static_assert(!std::is_convertible_v<DisplayExtent, RenderExtent>);
	static_assert(!std::is_constructible_v<AllocationExtent, RenderExtent>);
	static_assert(!std::is_constructible_v<RenderExtent, AllocationExtent>);
	static_assert(!std::is_assignable_v<AllocationExtent&, RenderExtent>);
	static_assert(!std::is_assignable_v<RenderExtent&, AllocationExtent>);

	// ...while each still behaves normally with itself.
	static_assert(std::is_copy_assignable_v<RenderExtent>);
	static_assert(std::is_nothrow_copy_constructible_v<RenderExtent>);
	static_assert(RenderExtent{} == RenderExtent{});
	static_assert(!(RenderExtent{ 1.0f, 2.0f } == RenderExtent{ 1.0f, 3.0f }));

	// A tagged extent must not silently decay to a scalar either.
	static_assert(!std::is_convertible_v<RenderExtent, float>);
	static_assert(!std::is_convertible_v<float, RenderExtent>);

	/**
	 * @brief Attaches a space tag to a raw pair.
	 *
	 * Named rather than implicit so every point where geometry gains a claimed
	 * meaning is greppable. `Vector2` is not usable here without dragging in
	 * DirectXMath, so the components are taken separately; the float2 overloads
	 * live next to the call sites that need them.
	 */
	template <class Space>
	[[nodiscard]] constexpr Extent2F<Space> MakeExtent(float a_width, float a_height) noexcept
	{
		return Extent2F<Space>{ a_width, a_height };
	}
}

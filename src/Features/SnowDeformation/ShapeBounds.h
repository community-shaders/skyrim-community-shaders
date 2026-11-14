#pragma once

namespace SnowShapes
{
	/**
	 * @brief Util::GetShapeBound with the shape's radius remembered per collision object.
	 *
	 * The two RTTI casts and the shape's six projections are the cost, and the radius is a
	 * property of the shape. Validated by the body, the referenced Havok body and the shape
	 * pointers, so a swapped body or shape recomputes; only the centre of mass is read live.
	 * Render thread only.
	 */
	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius);

	/**
	 * @brief Half-extents of a hkpShape along its own local axes, in world units.
	 * @return True if the shape could be measured, false otherwise.
	 */
	bool ExtractShapeHalfExtents(const RE::hkpShape* shape, float& hx, float& hy, float& hz);

	/**
	 * @brief The shape's silhouette on the ground: a segment along its longest horizontal extent, and the extent across it.
	 *
	 * The three local half-extents are rotated by the owning scene node and projected onto XY;
	 * the longest projection is the segment, the longer of the other two the width. A sphere
	 * reports equal length and width, so a caller subtracting one from the other gets a point.
	 * @param halfHeight Output: half of the shape's vertical extent in this orientation, so centre - halfHeight is its underside.
	 * @return True if the footprint could be measured, false otherwise.
	 */
	bool GetShapeFootprint(RE::bhkNiCollisionObject* collisionObj, float& axisX, float& axisY, float& halfLength, float& halfWidth, float& halfHeight);
}

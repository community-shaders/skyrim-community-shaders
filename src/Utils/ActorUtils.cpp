#include "ActorUtils.h"
#include <algorithm>
#include <cmath>

namespace Util
{
	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius)
	{
		if (!collisionObj)
			return false;

		RE::bhkRigidBody* bhkRigid = collisionObj->body.get() ? collisionObj->body.get()->AsBhkRigidBody() : nullptr;
		RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
		if (bhkRigid && hkpRigid) {
			RE::hkVector4 massCenter;
			bhkRigid->GetCenterOfMassWorld(massCenter);
			float massTrans[4];
			_mm_store_ps(massTrans, massCenter.quad);
			centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();
			return Util::ExtractShapeBound(hkpRigid->collidable.GetShape(), radius);
		}
		return false;
	}

	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius)
	{
		using ShapeType = RE::hkpShapeType;
		if (!shape)
			return false;
		if (shape->type == ShapeType::kCapsule) {
			float upExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float downExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, -1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto z_extent = (upExtent + downExtent) / 2.0f;
			float forwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float backwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, -1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto y_extent = (forwardExtent + backwardExtent) / 2.0f;
			float leftExtent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float rightExtent = shape->GetMaximumProjection(RE::hkVector4{ -1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto x_extent = (leftExtent + rightExtent) / 2.0f;
			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent);
			return true;
		} else if (shape->type == ShapeType::kSphere) {
			float sphereRadius = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			radius = sphereRadius;
			return true;
		} else if (shape->type == ShapeType::kBox) {
			float x_extent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float y_extent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float z_extent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent) * 0.5f;
			return true;
		} else if (shape->type == ShapeType::kCylinder) {
			float x_extent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float y_extent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float z_extent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			radius = sqrtf(std::max(x_extent, y_extent) * std::max(x_extent, y_extent) + z_extent * z_extent) * 0.5f;
			return true;
		} else if (shape->type == ShapeType::kConvexVertices || shape->type == ShapeType::kTriangle) {
			float max_extent = 0.0f;
			for (const auto& dir : { RE::hkVector4{ 1, 0, 0, 0 }, RE::hkVector4{ 0, 1, 0, 0 }, RE::hkVector4{ 0, 0, 1, 0 }, RE::hkVector4{ -1, 0, 0, 0 }, RE::hkVector4{ 0, -1, 0, 0 }, RE::hkVector4{ 0, 0, -1, 0 } }) {
				float extent = shape->GetMaximumProjection(dir) * RE::bhkWorld::GetWorldScaleInverse();
				max_extent = std::max(max_extent, extent);
			}
			radius = max_extent;
			return true;
		} else {
			float max_extent = 0.0f;
			for (const auto& dir : { RE::hkVector4{ 1, 0, 0, 0 }, RE::hkVector4{ 0, 1, 0, 0 }, RE::hkVector4{ 0, 0, 1, 0 }, RE::hkVector4{ -1, 0, 0, 0 }, RE::hkVector4{ 0, -1, 0, 0 }, RE::hkVector4{ 0, 0, -1, 0 } }) {
				float extent = shape->GetMaximumProjection(dir) * RE::bhkWorld::GetWorldScaleInverse();
				max_extent = std::max(max_extent, extent);
			}
			radius = max_extent;
			return true;
		}
	}
}

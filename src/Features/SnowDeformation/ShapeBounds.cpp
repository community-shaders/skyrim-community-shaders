#include "Features/SnowDeformation/ShapeBounds.h"

#include "Utils/ActorUtils.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace SnowShapes
{
	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius)
	{
		if (!collisionObj)
			return false;

		struct Cached
		{
			const void* body = nullptr;
			RE::bhkRigidBody* bhkRigid = nullptr;
			RE::hkpRigidBody* hkpRigid = nullptr;
			const RE::hkpShape* shape = nullptr;
			float radius = 0.0f;
			bool valid = false;
		};
		static std::unordered_map<const void*, Cached> memo;
		if (memo.size() > 8192)
			memo.clear();
		auto* body = collisionObj->body.get();
		auto [it, inserted] = memo.try_emplace(collisionObj);
		auto& c = it->second;
		bool fresh = inserted || c.body != body;
		if (!fresh && c.bhkRigid) {
			if (c.bhkRigid->referencedObject.get() != c.hkpRigid)
				fresh = true;
			else if (c.hkpRigid && c.hkpRigid->collidable.GetShape() != c.shape)
				fresh = true;
		}
		if (fresh) {
			c = {};
			c.body = body;
			c.bhkRigid = body ? body->AsBhkRigidBody() : nullptr;
			c.hkpRigid = c.bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(c.bhkRigid->referencedObject.get()) : nullptr;
			if (c.bhkRigid && c.hkpRigid && !skyrim_cast<RE::hkpListShape*>(c.hkpRigid)) {
				c.shape = c.hkpRigid->collidable.GetShape();
				c.valid = Util::ExtractShapeBound(c.shape, c.radius);
			}
		}
		if (!c.valid)
			return false;
		RE::hkVector4 massCenter;
		c.bhkRigid->GetCenterOfMassWorld(massCenter);
		float massTrans[4];
		_mm_storeu_ps(massTrans, massCenter.quad);
		centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();
		radius = c.radius;
		return true;
	}

	bool ExtractShapeHalfExtents(const RE::hkpShape* shape, float& hx, float& hy, float& hz)
	{
		if (!shape)
			return false;
		auto project = [shape](float x, float y, float z) {
			return shape->GetMaximumProjection(RE::hkVector4{ x, y, z, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
		};
		hx = 0.5f * (project(1.0f, 0.0f, 0.0f) - project(-1.0f, 0.0f, 0.0f));
		hy = 0.5f * (project(0.0f, 1.0f, 0.0f) - project(0.0f, -1.0f, 0.0f));
		hz = 0.5f * (project(0.0f, 0.0f, 1.0f) - project(0.0f, 0.0f, -1.0f));
		return true;
	}

	bool GetShapeFootprint(RE::bhkNiCollisionObject* collisionObj, float& axisX, float& axisY, float& halfLength, float& halfWidth, float& halfHeight)
	{
		if (!collisionObj || !collisionObj->sceneObject)
			return false;
		RE::bhkRigidBody* bhkRigid = collisionObj->body.get() ? collisionObj->body.get()->AsBhkRigidBody() : nullptr;
		RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
		if (!bhkRigid || !hkpRigid || skyrim_cast<RE::hkpListShape*>(hkpRigid))
			return false;
		float h[3];
		if (!ExtractShapeHalfExtents(hkpRigid->collidable.GetShape(), h[0], h[1], h[2]))
			return false;

		const auto& rot = collisionObj->sceneObject->world.rotate;
		const RE::NiPoint3 axes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		float px[3], py[3], len[3];
		int longest = 0;
		halfHeight = 0.0f;
		for (int i = 0; i < 3; ++i) {
			const RE::NiPoint3 world = rot * axes[i];
			px[i] = world.x * h[i];
			py[i] = world.y * h[i];
			len[i] = sqrtf(px[i] * px[i] + py[i] * py[i]);
			halfHeight += fabsf(world.z) * h[i];
			if (len[i] > len[longest])
				longest = i;
		}
		halfLength = len[longest];
		halfWidth = 0.0f;
		for (int i = 0; i < 3; ++i)
			if (i != longest)
				halfWidth = std::max(halfWidth, len[i]);
		if (halfLength > 1e-3f) {
			axisX = px[longest] / halfLength;
			axisY = py[longest] / halfLength;
		} else {
			axisX = 1.0f;
			axisY = 0.0f;
		}
		return true;
	}
}

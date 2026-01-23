#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Core/Model.h"
#include "Features/Raytracing/Core/Shape.h"

#include "Features/Raytracing/Pipelines/SkinningPipeline.h"

struct Instance
{
	enum State : uint8_t
	{
		Hidden = 1 << 0,
		Detached = 1 << 1
	};

	Instance(eastl::string filename) :
		filename(filename) {};

	void SetDetached(bool detach)
	{
		detached = detach;
	}

	bool IsDetached() const
	{
		return detached;
	}

	// Checks for skinned and dynamic trishapes update
	void Update(RE::NiAVObject* pNiNode, const eastl::pair<eastl::string, Model*>& modelPair, SkinningPipeline* skinningPipeline)
	{
		// Instance was not changed by the game, so there is no need to update it
		// This doesn't work at all for actors
		/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
				return true;*/

		// Is this working?
		if (pNiNode->GetAppCulled())
			return;

		//logger::info("Render Use: {}", pNiNode->GetFlags().any(RE::NiAVObject::Flag::kRenderUse));

		// Instance has already been updated this frame
		if (!frameChecker.IsNewFrame())
			return;

		// Sets the BLAS instance transform
		XMStoreFloat3x4(&transform, GetXMFromNiTransform(pNiNode->world));

		auto& [path, model] = modelPair;

		if ((model->GetFlags() & Shape::Flags::Dynamic) || (model->GetFlags() & Shape::Flags::Skinned)) {
			for (auto& shape : model->shapes) {
				Shape::Flags updateFlags = Shape::Flags::None;

				if (shape->UpdateDynamicPosition()) {
					updateFlags |= Shape::Flags::Dynamic;
				}

				if (shape->UpdateSkinning()) {
					updateFlags |= Shape::Flags::Skinned;
				}

				if (updateFlags & Shape::Flags::Skinned) {
					auto& skinInstance = shape->geometry->GetGeometryRuntimeData().skinInstance;

					if (shape->boneMatrices.empty())
						shape->boneMatrices.resize(skinInstance->numMatrices);

					float3x4* boneMatricesArray = reinterpret_cast<float3x4*>(skinInstance->boneMatrices);

					auto rootParent = skinInstance->rootParent;
					auto skinRootInverse = GetXMFromNiTransform(rootParent->world.Invert());

					shape->boundRadius = rootParent->worldBound.radius + (rootParent->world.translate + rootParent->worldBound.center).GetDistance(shape->geometry->world.translate);

					for (uint i = 0; i < skinInstance->numMatrices; i++) {
						XMStoreFloat3x4(&shape->boneMatrices[i], XMMatrixMultiply(XMLoadFloat3x4(&boneMatricesArray[i]), skinRootInverse));
					}
				}

				if ((updateFlags & Shape::Flags::Dynamic) || (updateFlags & Shape::Flags::Skinned)) {
					skinningPipeline->QueueUpdate(updateFlags, path, shape.get());
				}
			}
		}
	}

	// What model this instance references
	eastl::string filename;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	Util::FrameChecker frameChecker;

private:
	bool detached = false;
};
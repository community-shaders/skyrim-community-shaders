#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Core/Model.h"
#include "Features/Raytracing/Core/Shape.h"

#include "Features/Raytracing/Pipelines/SkinningPipeline.h"

struct Instance
{
	// What model this instance references
	eastl::string filename;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	Util::FrameChecker frameChecker;

	// Checks for skinned and dynamic trishapes update
	void Update(RE::NiNode* pNiNode, const eastl::pair<eastl::string, Model*>& modelPair, SkinningPipeline* skinningPipeline)
	{
		// Instance was not changed by the game, so there is no need to update it
		// This doesn't work at all for actors
		/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
				return true;*/

		// Is this working?
		if (pNiNode->GetAppCulled())
			return;

		// Instance has already been updated this frame
		if (!frameChecker.IsNewFrame())
			return;

		// Sets the BLAS instance transform
		XMStoreFloat3x4(&transform, GetXMFromNiTransform(pNiNode->world));

		auto& [path, model] = modelPair;

		if ((model->GetFlags() & Flags::Dynamic) || (model->GetFlags() & Flags::Skinned)) {
			auto worldInverse = pNiNode->world.Invert();

			for (auto& shape : model->shapes) {
				Flags updateFlags = Flags::None;

				if (shape->UpdateDynamicPosition()) {
					updateFlags |= Flags::Dynamic;
				}

				if (shape->UpdateSkinning()) {
					updateFlags |= Flags::Skinned;
				}

				if (updateFlags & Flags::Skinned) {
					auto& skinInstance = shape->geometry->GetGeometryRuntimeData().skinInstance;

					shape->boneMatrices.clear();
					shape->boneMatrices.resize(skinInstance->numMatrices);

					float3x4* boneMatricesArray = reinterpret_cast<float3x4*>(skinInstance->boneMatrices);

					auto skinRootInverse = GetXMFromNiTransform(skinInstance->rootParent->world.Invert());

					for (uint i = 0; i < skinInstance->numMatrices; i++) {
						XMStoreFloat3x4(&shape->boneMatrices[i], XMMatrixMultiply(XMLoadFloat3x4(&boneMatricesArray[i]), skinRootInverse));
					}
				}

				if ((updateFlags & Flags::Dynamic) || (updateFlags & Flags::Skinned)) {
					skinningPipeline->QueueUpdate(updateFlags, path, shape.get());
				}
			}
		}
	}
};
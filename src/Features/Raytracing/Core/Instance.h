#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Core/Shape.h"
#include "Features/Raytracing/Core/Model.h"

#include "Features/Raytracing/Pipelines/SkinningPipeline.h"

struct Instance
{
	eastl::string filename;
	float3x4 transform;
	Util::FrameChecker frameChecker;

	// Checks for skinned and dynamic trishapes update
	void Update(RE::NiNode* pNiNode, const eastl::pair<eastl::string, Model*>& modelPair, SkinningPipeline* skinningPipeline)
	{
		// Instance was not changed by the game, so there is no need to update it
		// This doesn't work at all for actors
		/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
				return true;*/

		if (pNiNode->GetAppCulled())
			return;

		// Instance has already been updated this frame
		if (!frameChecker.IsNewFrame())
			return;

		auto world = GetXMFromNiTransform(pNiNode->world);
		XMStoreFloat3x4(&transform, world);

		auto worldInverse = pNiNode->world.Invert();

		auto& [path, model] = modelPair;

		if ((model->GetFlags() & Flags::Dynamic) || (model->GetFlags() & Flags::Skinned)) {
			for (auto& shape : model->shapes) {
				Flags updateFlags = Flags::None;

				if (shape->UpdateDynamicPosition()) {
					updateFlags |= Flags::Dynamic;
				}

				if (shape->UpdateSkinning()) {
					updateFlags |= Flags::Skinned;
				}

				if ((updateFlags & Flags::Dynamic) || (updateFlags & Flags::Skinned)) {

					DirectX::XMMATRIX localToRootXM;
					if (shape->geometry->parent == pNiNode) {
						localToRootXM = GetXMFromNiTransform(shape->geometry->local);
					} else {
						localToRootXM = GetXMFromNiTransform(worldInverse * shape->geometry->world);
					}

					float3x4 localToRoot;
					XMStoreFloat3x4(&localToRoot, localToRootXM);
	
					skinningPipeline->QueueUpdate(updateFlags, path, shape.get(), localToRoot);
				}
			}
		}
	}
};
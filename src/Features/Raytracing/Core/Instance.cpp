#include "Instance.h"

#include "Features/Raytracing.h"

void Instance::SetDetached(bool detach)
{
	detached = detach;
}

bool Instance::IsDetached() const
{
	return detached;
}

bool Instance::ShouldUpdate(RE::NiAVObject* node, RE::NiPoint3 cameraPosition)
{
	bool vur = globals::features::raytracing.settings.AdvancedSettings.VariableUpdateRate;

	uint delta = globals::state->frameCount - lastUpdate;

	if (!vur && delta > 0) {
		lastUpdate = globals::state->frameCount;
		return true;
	}

	float distance = Util::Units::GameUnitsToMeters(node->world.translate.GetDistance(cameraPosition));

	if (delta >= UpdateRate(distance)) {
		lastUpdate = globals::state->frameCount;
		return true;
	}

	return false;
}

// Checks for skinned and dynamic trishapes update
void Instance::Update(RE::NiAVObject* node, RE::NiPoint3 cameraPosition, const eastl::pair<eastl::string, Model*>& modelPair, [[maybe_unused]] SkinningPipeline* skinningPipeline)
{
	// Instance was not changed by the game, so there is no need to update it
	// This doesn't work at all for actors
	/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
			return true;*/

	// Instance has already been updated this frame
	if (!ShouldUpdate(node, cameraPosition))
		return;

	// Sets the BLAS instance transform
	XMStoreFloat3x4(&transform, GetXMFromNiTransform(node->world));

	/*if (node->GetAppCulled())
		return;*/

	auto& [path, model] = modelPair;

	bool isRenderUseValid = model->IsRenderUseValid();

	//bool changed = false;

	for (auto& shape : model->shapes) {
		const bool prevHidden = (shape->state & Shape::State::Hidden) != Shape::State::None;

		auto updateFlags = shape->Update(isRenderUseValid);

		const bool hidden = (shape->state & Shape::State::Hidden) != Shape::State::None;

		if (hidden != prevHidden) {
			model->flags |= Model::Flags::BLASRebuild;

			//changed = true;
			logger::trace("Instance::Update {} - {} 0x{:08X} - Valid: {}, Hidden: {}, Flags: {}", 
				path, shape->geometry->name, reinterpret_cast<uintptr_t>(this), isRenderUseValid,
				hidden, GetFlagsString<RE::NiAVObject::Flag>(shape->geometry->GetFlags().underlying()));
		}

		if ((updateFlags & Shape::Flags::Dynamic) || (updateFlags & Shape::Flags::Skinned)) {
			model->flags |= Model::Flags::BLASUpdate;

			skinningPipeline->QueueUpdate(updateFlags, path, shape.get());
		}
	}

	//RE::BSDismemberSkinInstance

	/*if (changed) {
		RE::BSVisit::TraverseScenegraphObjects(node, [&](RE::NiAVObject* pObject) -> RE::BSVisit::BSVisitControl {
			logger::info("Instance::Update {} - {}, Flags: {}",
				path, pObject->name, GetFlagsString<RE::NiAVObject::Flag>(pObject->GetFlags().underlying()));

			return RE::BSVisit::BSVisitControl::kContinue;
		});	
	}*/
}
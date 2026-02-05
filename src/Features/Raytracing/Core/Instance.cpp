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

bool Instance::SkipUpdate(RE::NiAVObject* node, const RE::NiPoint3& cameraPosition)
{
	auto& rt = globals::features::raytracing;

	if (!rt.settings.AdvancedSettings.VariableUpdateRate)
		return false;

	const uint64_t delta = rt.frameIndex - lastUpdate;

	const float distance = Util::Units::GameUnitsToMeters(node->worldBound.center.GetDistance(cameraPosition));

	const uint64_t interval = UpdateInterval(distance);

	if (delta < interval)
		return true;

	lastUpdate = rt.frameIndex;

	return false;
}

// Checks for skinned and dynamic trishapes update
void Instance::Update(RE::NiAVObject* node, const RE::NiPoint3& cameraPosition, const eastl::pair<eastl::string, Model*>& modelPair, [[maybe_unused]] SkinningPipeline* skinningPipeline)
{
	// Instance was not changed by the game, so there is no need to update it
	// This doesn't work at all for actors
	/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
			return true;*/

	auto& [path, model] = modelPair;

	// Instance has already been updated this frame
	if (SkipUpdate(node, cameraPosition))
		return;

	// Sets the BLAS instance transform
	XMStoreFloat3x4(&transform, GetXMFromNiTransform(node->world));

	/*if (node->GetAppCulled())
		return;*/

	for (auto& shape : model->shapes) {
		auto updateFlags = shape->Update();

		if (shape->IsDirtyState()) {
			model->flags.set(Model::Flags::BLASRebuild);

			/*logger::info("Instance::Update {} 0x{:08X} - {} 0x{:08X} - Hidden: {}", 
				path, reinterpret_cast<uintptr_t>(model),
				shape->geometry->name, reinterpret_cast<uintptr_t>(shape.get()), 
				shape->IsPendingHidden());*/
		}

		if ((updateFlags & Shape::Flags::Dynamic) || (updateFlags & Shape::Flags::Skinned)) {
			model->flags.set(Model::Flags::BLASUpdate);
			skinningPipeline->QueueUpdate(updateFlags, path, shape.get());
		}
	}
}
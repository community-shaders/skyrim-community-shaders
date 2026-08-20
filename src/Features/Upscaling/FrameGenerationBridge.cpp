#include "FrameGenerationBridge.h"

#include "../../DxvkLoader.h"

bool FrameGenerationBridge::Register(const Callbacks& a_callbacks) const
{
	if (!DxvkLoader::IsLoaded() || !DxvkLoader::HasFrameGenerationOwnershipCallback())
		return false;
	return DxvkLoader::RegisterFrameGenerationCallbacks(a_callbacks.ownership,
		a_callbacks.presentBegin, a_callbacks.presentCompleted, a_callbacks.swapchainTornDown);
}

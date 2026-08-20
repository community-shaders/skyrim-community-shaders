#pragma once

#include <cs_dxvk_api.h>

/** Registers the Streamline/DXVK frame-generation callback boundary. */
class FrameGenerationBridge
{
public:
	struct Callbacks
	{
		PFN_csDxvkFrameGenOwnershipQuery ownership = nullptr;
		PFN_csDxvkPresentCallback presentBegin = nullptr;
		PFN_csDxvkPresentCallback presentCompleted = nullptr;
		PFN_csDxvkSwapchainTornDownCallback swapchainTornDown = nullptr;
	};

	[[nodiscard]] bool Register(const Callbacks& a_callbacks) const;
};

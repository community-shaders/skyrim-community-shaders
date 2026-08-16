#pragma once

namespace VRPerfModeRestartState
{
	struct ActiveBootContractInputs
	{
		bool bootActive = false;
		bool requestedNow = false;
		bool displaySizeChanged = false;
		bool eligibleNow = false;
		bool methodMatches = false;
		bool qualityModeMatches = false;

		// Hot-Envelope (experimental): the active quality renders into a region
		// that FITS the targets already allocated at boot.
		//
		// The latched contract exists because Render Scale Mode sizes Skyrim's
		// physical targets to the boot quality's input resolution, so a quality
		// change changes texture dimensions. That is exactly true when the new
		// quality is LARGER than the latched one. When it is smaller the existing
		// targets are already big enough, and only the logical extent rendered
		// into them needs to move - which the dynamic-resolution path in
		// ConfigureUpscaling already knows how to do.
		//
		// Defaults false, so a caller that never sets it gets byte-identical
		// behaviour to before this field existed.
		bool renderSizeFitsAllocation = false;
	};

	[[nodiscard]] constexpr bool RequiresRestart(const ActiveBootContractInputs& a_inputs) noexcept
	{
		return a_inputs.bootActive &&
		       (!a_inputs.requestedNow ||
				   a_inputs.displaySizeChanged ||
				   !a_inputs.eligibleNow ||
				   !a_inputs.methodMatches ||
				   !(a_inputs.qualityModeMatches || a_inputs.renderSizeFitsAllocation));
	}

	constexpr void Refresh(bool& a_restartRequired, const ActiveBootContractInputs& a_inputs) noexcept
	{
		a_restartRequired = RequiresRestart(a_inputs);
	}
}

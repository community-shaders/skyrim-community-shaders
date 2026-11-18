#pragma once

/**
 * Skips the engine's per-frame attach walk for dynamic lights whose state has not changed.
 *
 * The engine re-enables every dynamic point light each frame, and each call re-walks the scene to rebuild that light's
 * attached-geometry list. In the world, Light Limit Fix lights surfaces from its clustered pipeline and reads only a light's own
 * room and portal data, so an unchanged light's walk reproduces the previous frame.
 */
namespace LightAttach
{
	/** @brief True once the gate is installed, which needs a runtime whose per-frame dynamic light call was verified. */
	[[nodiscard]] bool IsSupported();

	/** @brief Replaces the per-frame dynamic light enable call so unchanged lights skip their attach walk. */
	void Install();
}

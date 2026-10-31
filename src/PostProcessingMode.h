#pragma once

#include <functional>

/** @brief Picks the one post-processing pipeline that drives the frame, keeping Post Processing and Effects 11 exclusive. */
namespace PostProcessingMode
{
	enum class Mode
	{
		Vanilla,
		PostProcessing,
		Effects11
	};

	/** @brief Effects 11 when UseEffect is on with a loaded preset, else Post Processing unless bypassed, else the game's own. */
	Mode Get();

	/** @brief Enables the chosen pipeline and disables the other.
	 *  @return False, changing nothing, when the mode's pipeline cannot run (not loaded, or Effects 11 without a preset). */
	bool Set(Mode mode);

	/** @brief Reads the saved mode from the "General" section, defaulting to what is installed (Effects 11 with a preset, else Post Processing, else Vanilla);
	 *  a preset pack applied after the last save overrides it with the pipeline the pack chose. Applied fully by ApplyPending(). */
	void Load(const json& a_general);

	/** @brief Writes the chosen mode into the user settings "General" section once it is known, even while its pipeline cannot run. */
	void Save(json& a_general);

	/** @brief Whether the mode read by Load() will apply as Vanilla; known before Effects 11 is ready, since only an explicit Vanilla choice resolves to it. */
	bool IsVanillaPending();

	/** @brief Applies a mode read by Load() once Effects 11 is ready, running the installed default while its pipeline cannot. */
	void ApplyPending();

	/** @brief Applies a mode re-read after startup right away, since nothing else would without Effects 11 loaded. */
	void ApplyReloaded();

	/** @brief A button anchored to the right of the selector, as tall as it. */
	struct SelectorAction
	{
		const char* label = nullptr;
		const char* tooltip = nullptr;
		std::function<void()> onClick;
	};

	/**
	 * @brief Draws the Vanilla / Post Processing / Effects 11 selector shared by both feature menus.
	 * @param action Optional button for the page (open its editor), drawn right-aligned on the selector's row.
	 */
	void DrawSelector(const SelectorAction* action = nullptr);
}

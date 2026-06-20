#pragma once

/**
 * @brief Base class for Skyrim engine bug fixes applied via binary patching.
 *
 * Subclasses implement Install() to hook or patch specific engine issues.
 * Fixes are registered in static lists and batch-installed at the appropriate load stage.
 */
struct EngineFix
{
	virtual ~EngineFix() = default;

	/** @brief Returns the human-readable name of this fix (used in log messages). */
	virtual std::string GetName() = 0;

	/** @brief Applies the engine fix. Override in subclasses to perform patching. */
	virtual void Install() {}

	/** @brief Installs all fixes registered for the post-post-load stage. */
	static void InstallOnPostPostLoadFixes();
	/** @brief Installs all fixes registered for the data-loaded stage. */
	static void InstallOnDataLoadedFixes();

private:
	static const std::vector<EngineFix*>& GetOnPostPostLoadFixesList();
	static const std::vector<EngineFix*>& GetOnDataLoadedFixesList();
	static void InstallFixes(const std::vector<EngineFix*>& fixes);
};

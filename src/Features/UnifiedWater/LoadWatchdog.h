#pragma once

#include <thread>

/** @brief Logs a warning while a blocking Unified Water load step runs too long; the game thread can't draw UI while blocked. */
class LoadWatchdog
{
public:
	/** @brief Starts watching the current step; watching stops when the object is destroyed. */
	LoadWatchdog();
	/** @brief Clears the current step and stops the watcher thread. */
	~LoadWatchdog();

	/**
	 * @brief Logs and records the step now running.
	 * @param step Lowercase description of the step, e.g. "reading flow textures". Must be a string literal.
	 * @param hint Likely cause if the step stalls. Must be a string literal.
	 */
	static void SetStep(const char* step, const char* hint);

private:
	std::jthread thread;
};

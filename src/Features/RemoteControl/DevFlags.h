#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @brief Named runtime booleans that guard temporary candidate code paths, so one build can screen them on and off.
 *
 * Flags are created false on first Get or Set and live until exit. All functions are thread-safe.
 */
namespace DevFlags
{
	/** @brief The flag's storage, created false on first use; the reference stays valid until exit. */
	std::atomic<bool>& Get(std::string_view a_name);
	/** @brief Sets a flag, creating it when no call site has read it yet. */
	void Set(std::string_view a_name, bool a_value);
	/** @brief Every known flag and its value, sorted by name. */
	std::vector<std::pair<std::string, bool>> List();
	/** @brief Turns every flag off. */
	void Reset();
}

/** @brief Reads the named dev flag; each call site looks the name up once, then costs one relaxed atomic load. */
#define DEV_FLAG(name) ([]() -> std::atomic<bool>& { static std::atomic<bool>& flag = DevFlags::Get(name); return flag; }().load(std::memory_order_relaxed))

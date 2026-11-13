#include "Features/RemoteControl/DevFlags.h"

#include <functional>
#include <map>
#include <mutex>

namespace
{
	/** @brief Flag storage and the mutex guarding inserts and iteration. */
	struct Registry
	{
		std::mutex mutex;
		std::map<std::string, std::atomic<bool>, std::less<>> flags;  // node-based, so Get's references survive later inserts
	};

	/** @brief The process-wide registry, built on first use so call sites in static initializers are safe. */
	Registry& GetRegistry()
	{
		static Registry registry;
		return registry;
	}
}

namespace DevFlags
{
	std::atomic<bool>& Get(std::string_view a_name)
	{
		auto& registry = GetRegistry();
		std::scoped_lock lock(registry.mutex);
		return registry.flags.try_emplace(std::string(a_name), false).first->second;
	}

	void Set(std::string_view a_name, bool a_value)
	{
		Get(a_name).store(a_value, std::memory_order_relaxed);
	}

	std::vector<std::pair<std::string, bool>> List()
	{
		auto& registry = GetRegistry();
		std::scoped_lock lock(registry.mutex);
		std::vector<std::pair<std::string, bool>> out;
		out.reserve(registry.flags.size());
		for (const auto& entry : registry.flags)
			out.emplace_back(entry.first, entry.second.load(std::memory_order_relaxed));
		return out;
	}

	void Reset()
	{
		auto& registry = GetRegistry();
		std::scoped_lock lock(registry.mutex);
		for (auto& entry : registry.flags)
			entry.second.store(false, std::memory_order_relaxed);
	}
}

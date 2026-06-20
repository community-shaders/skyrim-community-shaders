#pragma once

#include <cstddef>

/**
 * @brief Type-erased descriptor for restart-gated settings fields.
 * 
 * Metadata container for a settings field that supports restart-based gating.
 * The jsonKey field must match the corresponding field name in NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
 * declarations, allowing external tools such as MCP and RemoteControl to reference settings
 * without requiring feature-specific integration code.
 */
namespace Util::Settings
{
	// Type-erased field descriptor for restart-gated settings.
	//
	// `jsonKey` must match the NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE field name so
	// MCP/RemoteControl can refer to it without per-feature glue.
	struct RestartFieldInfo
	{
		const char* jsonKey = nullptr;
		const char* label = nullptr;
		size_t offset = 0;
		size_t size = 0;
	};
}

#pragma once

/** @brief Editor IDs the game discards at runtime, recorded as forms load from plugins. */
namespace Util::EditorIDCache
{
	/** @brief Hooks SetFormEditorID on the form types CS reads editor IDs for; must run before data load. */
	void Install();

	/** @brief The recorded editor ID, or "" when none; the pointer stays valid for the process lifetime. */
	const char* Find(RE::FormID formId);
}

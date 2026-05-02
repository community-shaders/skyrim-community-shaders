#pragma once

namespace Util
{
	// --- SPID Helpers (load-order-portable FormID format) ---
	// SPID format: "0x{localFormId}~{pluginName}" e.g. "0x12F89E~Skyrim.esm"

	struct SpidComponents
	{
		uint32_t localFormId = 0;
		std::string pluginName;
	};

	/// Parse a SPID string into components.
	SpidComponents ParseSpid(const std::string& spid);

	/// Format a SPID string from components.
	std::string FormatSpid(uint32_t localFormId, const std::string& pluginName);

	/// Convert a runtime FormID to a portable SPID string.
	std::string FormIdToSpid(RE::FormID formId);

	/// Resolve a SPID string to a runtime FormID (0 if not found).
	RE::FormID SpidToFormId(const std::string& spid);

	/// Get a display name: "EditorID (localFormId)" or SPID fallback.
	std::string GetFormDisplayName(RE::FormID formId);

	/// Get the EditorID for a form, or empty string if not available.
	std::string GetFormEditorID(const RE::TESForm* form);

	/// Get a file-safe key for a form: always SPID format for load-order independence.
	std::string GetFormFileKey(const RE::TESForm* form);
}

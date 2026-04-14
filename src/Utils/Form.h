#pragma once

namespace Util
{
	/// Components of a SPID identifier: local FormID + plugin name.
	struct SpidComponents
	{
		uint32_t localFormId = 0;
		std::string pluginName;
	};

	/// Parse a SPID string like "0x12F89E~Skyrim.esm" into components.
	SpidComponents ParseSpid(const std::string& spid);

	/// Format a SPID string from components.
	std::string FormatSpid(uint32_t localFormId, const std::string& pluginName);

	/// Convert a runtime FormID to a portable SPID string.
	std::string FormIdToSpid(RE::FormID formId);

	/// Resolve a SPID string to a runtime FormID (0 if not found).
	RE::FormID SpidToFormId(const std::string& spid);

	/// Get a human-readable display name: "EditorID (LocalFormID)" or SPID fallback.
	std::string GetFormDisplayName(RE::FormID formId);

	/// Get the EditorID for a form, with fallback search through all forms.
	std::string GetFormEditorID(RE::TESForm* form);

	/// Get a filesystem-safe key for a form (always SPID format).
	std::string GetFormFileKey(RE::TESForm* form);
}

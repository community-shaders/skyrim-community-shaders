#pragma once

#include <unordered_map>

namespace Util
{
	// --- SPID Helpers (load-order-portable FormID format) ---
	// SPID format: "0x{localFormId}~{pluginName}" e.g. "0x12F89E~Skyrim.esm"

	/** @brief Parsed components of a SPID (load-order-portable FormID) string. */
	struct SpidComponents
	{
		uint32_t localFormId = 0;
		std::string pluginName;
	};

	/**
	 * @brief Parse a SPID string into its local form ID and plugin name components.
	 * @param spid A string in the format "0x{localFormId}~{pluginName}".
	 * @return The parsed components; localFormId is 0 on parse failure.
	 */
	SpidComponents ParseSpid(const std::string& spid);

	/**
	 * @brief Format a SPID string from a local form ID and plugin name.
	 * @param localFormId The local form ID within the plugin.
	 * @param pluginName The plugin filename (e.g. "Skyrim.esm").
	 * @return The formatted SPID string.
	 */
	std::string FormatSpid(uint32_t localFormId, const std::string& pluginName);

	/**
	 * @brief Convert a runtime FormID to a portable SPID string.
	 * @param formId The runtime form ID to convert.
	 * @return The SPID string, or a raw hex fallback if the form is not found.
	 */
	std::string FormIdToSpid(RE::FormID formId);

	/**
	 * @brief Resolve a SPID string to a runtime FormID.
	 * @param spid The SPID string to resolve.
	 * @return The runtime FormID, or 0 if the form could not be found.
	 */
	RE::FormID SpidToFormId(const std::string& spid);

	/**
	 * @brief Get a human-readable display name for a form.
	 * @param formId The runtime form ID.
	 * @return A string in the format "EditorID (localFormId)", or a SPID fallback.
	 */
	std::string GetFormDisplayName(RE::FormID formId);

	/** @brief Owns a lazy editor-ID map snapshot for one initialization batch. */
	class FormEditorIDIndex
	{
	public:
		/** @brief Find the first map alias for a form, creating the snapshot on first use. */
		std::string Find(const RE::TESForm* form);

	private:
		std::unordered_map<const RE::TESForm*, std::string> editorIDs;
		bool initialized = false;
	};

	/**
	 * @brief Get the editor ID for a form.
	 * @param form The form to look up.
	 * @param index Optional batch-local index for fallback lookups.
	 * @return The editor ID string, or empty string if not available.
	 */
	std::string GetFormEditorID(const RE::TESForm* form, FormEditorIDIndex* index = nullptr);

	/**
	 * @brief Get a file-safe key for a form in SPID format for load-order independence.
	 * @param form The form to generate a key for.
	 * @return The SPID-formatted key string.
	 */
	std::string GetFormFileKey(const RE::TESForm* form);
}

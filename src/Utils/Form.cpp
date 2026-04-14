#include "Form.h"

Util::SpidComponents Util::ParseSpid(const std::string& spid)
{
	SpidComponents result;
	auto tildePos = spid.find('~');
	std::string hexPart = (tildePos != std::string::npos) ? spid.substr(0, tildePos) : spid;
	if (tildePos != std::string::npos)
		result.pluginName = spid.substr(tildePos + 1);

	// Strip 0x/0X prefix
	if (hexPart.size() > 2 && hexPart[0] == '0' && (hexPart[1] == 'x' || hexPart[1] == 'X'))
		hexPart = hexPart.substr(2);

	try {
		result.localFormId = static_cast<uint32_t>(std::stoul(hexPart, nullptr, 16));
	} catch (...) {
		result.localFormId = 0;
	}
	return result;
}

std::string Util::FormatSpid(uint32_t localFormId, const std::string& pluginName)
{
	if (pluginName.empty())
		return std::format("0x{:X}", localFormId);
	return std::format("0x{:X}~{}", localFormId, pluginName);
}

std::string Util::FormIdToSpid(RE::FormID formId)
{
	auto* form = RE::TESForm::LookupByID(formId);
	if (!form)
		return std::format("0x{:X}", formId);
	auto* file = form->GetFile(0);
	if (!file)
		return std::format("0x{:X}", form->GetLocalFormID());
	return FormatSpid(form->GetLocalFormID(), std::string(file->GetFilename()));
}

RE::FormID Util::SpidToFormId(const std::string& spid)
{
	auto components = ParseSpid(spid);
	if (components.pluginName.empty() || components.localFormId == 0) {
		logger::warn("SpidToFormId: bad parse for '{}' — plugin='{}' localId=0x{:X}", spid, components.pluginName, components.localFormId);
		return 0;
	}
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		logger::warn("SpidToFormId: TESDataHandler is null for '{}'", spid);
		return 0;
	}

	// Primary path: use CommonLibSSE-NG's LookupForm
	auto* form = handler->LookupForm(components.localFormId, components.pluginName);
	if (form)
		return form->GetFormID();

	// Fallback: manually reconstruct the full FormID from compile indices
	auto* file = handler->LookupModByName(components.pluginName);
	if (!file) {
		logger::warn("SpidToFormId: plugin '{}' not found in load order", components.pluginName);
		return 0;
	}

	if (file->compileIndex == 0xFF) {
		logger::warn("SpidToFormId: plugin '{}' has invalid compile index 0xFF", components.pluginName);
		return 0;
	}

	RE::FormID reconstructed = (static_cast<RE::FormID>(file->compileIndex) << 24) +
	                           (static_cast<RE::FormID>(file->smallFileCompileIndex) << 12) +
	                           components.localFormId;
	auto* directForm = RE::TESForm::LookupByID(reconstructed);
	if (directForm) {
		logger::info("SpidToFormId: '{}' resolved via direct reconstruction to 0x{:08X}", spid, reconstructed);
		return reconstructed;
	}

	logger::warn("SpidToFormId: plugin '{}' index=0x{:X} smallIndex=0x{:X} localId=0x{:X} reconstructed=0x{:08X} — form not found",
		components.pluginName, file->compileIndex, file->smallFileCompileIndex, components.localFormId, reconstructed);
	return 0;
}

std::string Util::GetFormDisplayName(RE::FormID formId)
{
	auto* form = RE::TESForm::LookupByID(formId);
	if (!form)
		return std::format("0x{:X}", formId);

	const char* editorId = form->GetFormEditorID();
	if (editorId && editorId[0] != '\0')
		return std::format("{} ({:X})", editorId, form->GetLocalFormID());

	return FormIdToSpid(formId);
}

std::string Util::GetFormEditorID(RE::TESForm* form)
{
	if (!form)
		return "";

	const char* editorId = form->GetFormEditorID();
	if (editorId && editorId[0] != '\0')
		return std::string(editorId);

	// Fallback: search all forms for a matching EditorID
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler)
		return "";

	for (auto& [id, formEntry] : handler->GetFormArray<RE::TESForm>()) {
		if (formEntry && formEntry->GetFormID() == form->GetFormID()) {
			const char* eid = formEntry->GetFormEditorID();
			if (eid && eid[0] != '\0')
				return std::string(eid);
		}
	}
	return "";
}

std::string Util::GetFormFileKey(RE::TESForm* form)
{
	if (!form)
		return "";
	return FormIdToSpid(form->GetFormID());
}

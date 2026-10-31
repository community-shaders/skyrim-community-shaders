#include "Form.h"

#include <charconv>

Util::SpidComponents Util::ParseSpid(const std::string& spid)
{
	SpidComponents result;
	auto tildePos = spid.find('~');
	std::string_view hexPart = tildePos != std::string::npos ?
	                               std::string_view(spid).substr(0, tildePos) :
	                               std::string_view(spid);
	if (tildePos != std::string::npos) {
		result.pluginName = spid.substr(tildePos + 1);
		if (result.pluginName.empty() || result.pluginName.contains('~'))
			return {};
	}

	// Strip 0x/0X prefix
	if (hexPart.size() > 2 && hexPart[0] == '0' && (hexPart[1] == 'x' || hexPart[1] == 'X'))
		hexPart.remove_prefix(2);
	if (hexPart.empty())
		return {};

	const auto [end, error] = std::from_chars(
		hexPart.data(), hexPart.data() + hexPart.size(), result.localFormId, 16);
	if (error != std::errc{} || end != hexPart.data() + hexPart.size())
		return {};
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
	return GetFormFileKey(form);
}

RE::FormID Util::SpidToFormId(const std::string& spid)
{
	auto components = ParseSpid(spid);
	if (components.pluginName.empty() || components.localFormId == 0) {
		logger::warn("[FormIdentity] SpidToFormId: bad parse for '{}', plugin='{}' localId=0x{:X}", spid, components.pluginName, components.localFormId);
		return 0;
	}
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		logger::warn("[FormIdentity] SpidToFormId: TESDataHandler is null for '{}'", spid);
		return 0;
	}

	// Primary: CommonLibSSE-NG's LookupForm
	auto* form = handler->LookupForm(components.localFormId, components.pluginName);
	if (form)
		return form->GetFormID();

	// Fallback: reconstruct full FormID from compile indices
	auto* file = handler->LookupModByName(components.pluginName);
	if (!file) {
		logger::warn("[FormIdentity] SpidToFormId: plugin '{}' not found in load order", components.pluginName);
		return 0;
	}

	if (file->compileIndex == 0xFF) {
		logger::warn("[FormIdentity] SpidToFormId: plugin '{}' has invalid compile index 0xFF", components.pluginName);
		return 0;
	}

	RE::FormID reconstructed = (static_cast<RE::FormID>(file->compileIndex) << 24) |
	                           (static_cast<RE::FormID>(file->smallFileCompileIndex) << 12) |
	                           components.localFormId;
	auto* directForm = RE::TESForm::LookupByID(reconstructed);
	if (directForm) {
		logger::info("[FormIdentity] SpidToFormId: '{}' resolved via direct reconstruction to 0x{:08X}", spid, reconstructed);
		return reconstructed;
	}

	logger::warn("[FormIdentity] SpidToFormId: plugin '{}' index=0x{:X} smallIndex=0x{:X} localId=0x{:X} reconstructed=0x{:08X}, form not found",
		components.pluginName, file->compileIndex, file->smallFileCompileIndex, components.localFormId, reconstructed);
	return 0;
}

std::string Util::GetFormDisplayName(RE::FormID formId)
{
	auto* form = RE::TESForm::LookupByID(formId);
	if (!form)
		return std::format("0x{:X}", formId);

	auto editorId = GetFormEditorID(form);
	if (!editorId.empty())
		return std::format("{} ({:X})", editorId, form->GetLocalFormID());

	return GetFormFileKey(form);
}

std::string Util::FormEditorIDIndex::Find(const RE::TESForm* form)
{
	if (!form)
		return "";

	if (!initialized) {
		auto [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		if (map) {
			RE::BSReadLockGuard locker(lock);
			editorIDs.reserve(map->size());
			for (const auto& [name, entry] : *map) {
				editorIDs.try_emplace(entry, name.c_str());
			}
		}
		initialized = true;
	}

	const auto entry = editorIDs.find(form);
	return entry != editorIDs.end() ? entry->second : "";
}

std::string Util::GetFormEditorID(const RE::TESForm* form, FormEditorIDIndex* index)
{
	if (!form)
		return "";
	const char* editorId = form->GetFormEditorID();
	if (editorId && editorId[0] != '\0')
		return std::string(editorId);
	if (index)
		return index->Find(form);

	// The global map is keyed by name, so it is inverted once and rebuilt only when its size changes.
	static std::mutex reverseIndexMutex;
	static std::unordered_map<const RE::TESForm*, RE::BSFixedString> reverseIndex;
	static size_t indexedMapSize = 0;

	auto [map, lock] = RE::TESForm::GetAllFormsByEditorID();
	if (!map)
		return "";
	std::scoped_lock indexLock(reverseIndexMutex);
	RE::BSReadLockGuard locker(lock);
	const auto rebuild = [](const auto& forms) {
		reverseIndex.clear();
		reverseIndex.reserve(forms.size());
		for (const auto& [name, mappedForm] : forms)
			reverseIndex.emplace(mappedForm, name);
		indexedMapSize = forms.size();
	};
	if (map->size() != indexedMapSize)
		rebuild(*map);
	auto entry = reverseIndex.find(form);
	// A freed form's address can come back as another form without the map changing size.
	if (entry != reverseIndex.end()) {
		if (const auto mapped = map->find(entry->second); mapped == map->end() || mapped->second != form) {
			rebuild(*map);
			entry = reverseIndex.find(form);
		}
	}
	return entry != reverseIndex.end() ? std::string(entry->second.c_str()) : "";
}

std::string Util::GetFormFileKey(const RE::TESForm* form)
{
	if (!form)
		return "Invalid";

	auto* file = form->GetFile(0);
	if (file)
		return FormatSpid(form->GetLocalFormID(), std::string(file->GetFilename()));
	return std::format("0x{:X}", form->GetLocalFormID());
}

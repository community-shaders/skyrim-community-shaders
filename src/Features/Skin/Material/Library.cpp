#include "Library.h"

#include "../Editor/Files.h"

namespace SkinLibrary
{
	Entry Read(const std::string& a_key)
	{
		constexpr size_t MaxBytes = 1024 * 1024;
		Entry entry;
		entry.key = a_key;
		entry.path = SkinMaterials::SidecarPath(a_key);
		if (entry.path.empty())
			return entry;
		try {
			const auto bytes = SkinEditor::FindResource(entry.path);
			if (!bytes)
				return entry;
			const auto session = SkinEditor::SessionOutput(entry.path);
			entry.origin = SkinEditor::IsLooseResource(entry.path) ? Origin::Loose : session ? Origin::Session :
			                                                                                   Origin::Archive;
			if (entry.origin == Origin::Session)
				entry.savedTo = SkinEditor::UTF8(*session);
			if (bytes->size() > MaxBytes)
				throw std::runtime_error(T("feature.skin.material_file_too_large", "The material file exceeds 1 MiB."));
			entry.definition = SkinMaterials::Parse(*bytes);
		} catch (const std::exception& e) {
			entry.definition = {};
			entry.definition.status = SkinMaterials::Status::Invalid;
			entry.definition.diagnostic = e.what();
		}
		if (entry.definition.status == SkinMaterials::Status::Invalid)
			logger::warn("[Advanced Skin] {} ignored: {}", entry.path, entry.definition.diagnostic);
		return entry;
	}
}

#include "Characters.h"

#include "../Editor/Files.h"

#include <cctype>
#include <fstream>
#include <mutex>

namespace SkinCharacters
{
	namespace
	{
		constexpr uint32_t Version = 1;
		constexpr size_t MaxBytes = 1024 * 1024;
		constexpr size_t MaxMaterials = 256;

		std::mutex mutex;
		std::shared_ptr<const State> state = std::make_shared<State>();

		void Publish(State a_next)
		{
			a_next.revision = state->revision + 1;
			state = std::make_shared<State>(std::move(a_next));
		}

		std::string Lower(std::string_view a_text)
		{
			std::string result(a_text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}
		Target PlayerTarget()
		{
			return { "player", "player" };
		}
		Target RecordTarget(uint32_t a_localID, std::string_view a_plugin)
		{
			if (a_localID == 0x7 && Lower(a_plugin) == "skyrim.esm")
				return PlayerTarget();
			return { std::format("{:x}~{}", a_localID, Lower(a_plugin)), std::format("0x{:X}~{}", a_localID, a_plugin) };
		}
		// "player", or "<hex form ID>~<plugin>". A leading 0x and a load-order prefix are accepted and ignored.
		Target ParseTarget(const json& a_value)
		{
			if (!a_value.is_string())
				throw std::runtime_error(T("feature.skin.character_file_target", "\"target\" must be \"player\" or an NPC record such as \"0xA2C8E~Skyrim.esm\"."));
			auto text = a_value.get<std::string>();
			text.erase(0, text.find_first_not_of(" \t"));
			text.erase(text.find_last_not_of(" \t") + 1);
			if (Lower(text) == "player")
				return PlayerTarget();
			const auto split = text.find('~');
			auto digits = text.substr(0, split);
			if (digits.starts_with("0x") || digits.starts_with("0X"))
				digits.erase(0, 2);
			if (split == std::string::npos || split + 1 >= text.size() || digits.empty() || digits.size() > 8 ||
				!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
				throw std::runtime_error(T("feature.skin.character_file_target", "\"target\" must be \"player\" or an NPC record such as \"0xA2C8E~Skyrim.esm\"."));
			const auto plugin = text.substr(split + 1);
			auto* data = RE::TESDataHandler::GetSingleton();
			const auto* file = data ? data->LookupModByName(plugin) : nullptr;
			if (!file)
				throw std::runtime_error(T("feature.skin.character_file_plugin_missing", "Its plugin is not installed: ") + plugin);
			const auto id = static_cast<uint32_t>(std::stoul(digits, nullptr, 16));
			return RecordTarget(id & (file->IsLight() ? 0xFFFu : 0xFFFFFFu), file->GetFilename());
		}

		Customization DecodeCustomization(const json& a_value)
		{
			if (!a_value.is_object() || !a_value.contains("format") || a_value.at("format") != Format)
				throw std::runtime_error(T("feature.skin.character_file_wrong_format", "Not a Skin character file: \"format\" must be \"CommunityShaders.SkinCharacter\"."));
			if (!a_value.contains("version") || a_value.at("version") != Version)
				throw std::runtime_error(T("feature.skin.character_file_wrong_version", "Unsupported Skin character file version."));
			Customization result;
			if (a_value.contains("name") && a_value.at("name").is_string())
				result.name = a_value.at("name").get<std::string>();
			if (a_value.contains("all")) {
				result.all = SkinMaterials::DecodeChanges(a_value.at("all"));
				if (result.all != SkinMaterials::ScalarsOnly(result.all))
					throw std::runtime_error(T("feature.skin.character_file_all_textures", "\"all\" cannot contain textures: a texture only fits one skin texture's UV layout. Put it under \"materials\"."));
			}
			if (a_value.contains("materials")) {
				const auto& materials = a_value.at("materials");
				if (!materials.is_object() || materials.size() > MaxMaterials)
					throw std::runtime_error(T("feature.skin.character_file_materials_object", "\"materials\" must be an object with at most 256 skin textures."));
				for (const auto& [name, changes] : materials.items()) {
					const auto key = SkinMaterials::TextureKey(name);
					if (key.empty())
						throw std::runtime_error(T("feature.skin.character_file_material_key", "Not a skin texture path: ") + name);
					try {
						auto decoded = SkinMaterials::DecodeChanges(changes);
						if (!decoded.Empty())
							result.materials[key] = std::move(decoded);
					} catch (const std::exception& e) {
						throw std::runtime_error(name + ": " + e.what());
					}
				}
			}
			return result;
		}

		std::string Encode(const Target& a_target, const Customization& a_value)
		{
			auto materials = nlohmann::ordered_json::object();
			for (const auto& [key, changes] : a_value.materials)
				if (!changes.Empty())
					materials[key] = SkinMaterials::Encode(changes);
			nlohmann::ordered_json result{ { "format", Format }, { "version", Version }, { "target", a_target.text }, { "name", a_value.name } };
			result["all"] = { { "parameters", SkinMaterials::Encode(SkinMaterials::ScalarsOnly(a_value.all)).at("parameters") } };
			result["materials"] = std::move(materials);
			// Names come from game strings, which are not guaranteed to be valid UTF-8.
			const auto text = result.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
			if (text.size() > MaxBytes)
				throw std::runtime_error(T("feature.skin.character_file_too_large", "The character file exceeds 1 MiB."));
			return text;
		}

		json ReadFile(const std::filesystem::path& a_path)
		{
			// A plain stream, opened by relative path: this resolves files in a mod manager's virtual Data folder.
			std::ifstream input(a_path, std::ios::binary);
			std::string text(MaxBytes + 1, '\0');
			input.read(text.data(), static_cast<std::streamsize>(text.size()));
			text.resize(static_cast<size_t>(input.gcount()));
			if (input.bad() || (input.fail() && !input.eof()))
				throw std::runtime_error(T("feature.skin.character_file_unreadable", "The file could not be read."));
			if (text.size() > MaxBytes)
				throw std::runtime_error(T("feature.skin.character_file_too_large", "The character file exceeds 1 MiB."));
			return json::parse(text.begin(), text.end(), nullptr, true, true);
		}

		void Normalize(Customization& a_customization)
		{
			a_customization.all = SkinMaterials::ScalarsOnly(a_customization.all);
			SkinMaterials::Validate(a_customization.all);
			std::erase_if(a_customization.materials, [](const auto& item) { return item.second.Empty(); });
			for (const auto& [key, changes] : a_customization.materials)
				SkinMaterials::Validate(changes);
			if (a_customization.materials.size() > MaxMaterials)
				throw std::runtime_error(T("feature.skin.too_many_character_materials", "A character can keep adjustments for at most 256 skin textures."));
		}

		// Readable in a file browser, and unique per character without depending on its display name.
		std::filesystem::path FileName(const Target& a_target, const std::string& a_name)
		{
			if (a_target.key == "player")
				return "Player.json";
			std::string name;
			for (const unsigned char c : a_name)
				if (std::isalnum(c) || c == ' ' || c == '-' || c == '_')
					name.push_back(static_cast<char>(c));
			auto id = a_target.text;
			std::replace(id.begin(), id.end(), '~', ' ');
			return (name.empty() || name.size() > 64 ? id : name + " " + id) + ".json";
		}
	}

	bool Customization::Empty() const
	{
		return all.Empty() && std::all_of(materials.begin(), materials.end(), [](const auto& item) { return item.second.Empty(); });
	}

	Target TargetOf(RE::Actor* a_actor)
	{
		if (!a_actor)
			return {};
		if (a_actor == RE::PlayerCharacter::GetSingleton())
			return PlayerTarget();
		// Actors generated from leveled lists use a temporary record; the record their appearance comes from is stable.
		auto* npc = a_actor->GetActorBase();
		const std::array<const RE::TESNPC*, 2> records{ npc, npc ? npc->GetRootFaceNPC() : nullptr };
		for (const auto* record : records)
			if (record && !record->IsDynamicForm() && record->GetFile(0))
				return RecordTarget(record->GetLocalFormID(), record->GetFile(0)->GetFilename());
		return {};
	}

	std::filesystem::path Directory()
	{
		return std::filesystem::path("Data") / "SKSE" / "Plugins" / "CommunityShaders" / "AdvancedSkin" / "Characters";
	}

	void Reload()
	{
		State next;
		const auto root = Directory();
		std::vector<std::filesystem::path> files;
		std::error_code missing;
		if (std::filesystem::exists(root, missing)) {
			try {
				for (const auto& entry : std::filesystem::directory_iterator(root))
					if (!entry.is_directory() && Lower(SkinEditor::UTF8(entry.path().extension())) == ".json")
						files.push_back(entry.path());
			} catch (const std::exception& e) {
				next.problems.push_back({ root, e.what() });
			}
		}
		// A later file name wins when two files target the same character, like any other loose-file conflict.
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return Lower(SkinEditor::UTF8(a.filename())) < Lower(SkinEditor::UTF8(b.filename())); });
		for (const auto& file : files) {
			try {
				const auto value = ReadFile(file);
				Character character;
				character.customization = DecodeCustomization(value);
				character.target = ParseTarget(value.contains("target") ? value.at("target") : json());
				character.file = file;
				if (const auto previous = next.characters.find(character.target.key); previous != next.characters.end())
					next.problems.push_back({ previous->second.file, T("feature.skin.character_file_overridden", "Not used: a later file targets the same character: ") + SkinEditor::UTF8(file.filename()) });
				next.characters[character.target.key] = std::move(character);
			} catch (const std::exception& e) {
				next.problems.push_back({ file, e.what() });
			}
		}
		for (const auto& problem : next.problems)
			logger::warn("[Advanced Skin] {}: {}", SkinEditor::UTF8(problem.file), problem.diagnostic);
		logger::info("[Advanced Skin] {} character file(s) in use.", next.characters.size());
		std::lock_guard lock(mutex);
		Publish(std::move(next));
	}

	Snapshot Get()
	{
		std::lock_guard lock(mutex);
		return state;
	}

	const Character* Find(const State& a_state, const std::string& a_key)
	{
		const auto found = a_state.characters.find(a_key);
		return found == a_state.characters.end() ? nullptr : &found->second;
	}

	std::filesystem::path Save(const Target& a_target, Customization a_customization)
	{
		if (!a_target)
			throw std::runtime_error(T("feature.skin.character_no_identity", "This character was generated at runtime and has no record to save adjustments for."));
		Normalize(a_customization);
		std::lock_guard lock(mutex);
		State next = *state;
		const auto existing = next.characters.find(a_target.key);
		const auto file = existing != next.characters.end() ? existing->second.file : Directory() / FileName(a_target, a_customization.name);
		// Keep an empty winning file: deleting it could reveal another mod's adjustments.
		const auto text = Encode(a_target, a_customization);
		const SkinEditor::Output output{ file, { text.begin(), text.end() } };
		SkinEditor::Publish(std::span(&output, 1), false);
		next.characters[a_target.key] = { a_target, std::move(a_customization), file };
		Publish(std::move(next));
		return file;
	}

	Customization Import(const std::filesystem::path& a_path)
	{
		return DecodeCustomization(ReadFile(a_path));
	}

	void Export(const std::filesystem::path& a_path, const Target& a_target, const Customization& a_customization)
	{
		auto customization = a_customization;
		Normalize(customization);
		const auto text = Encode(a_target ? a_target : PlayerTarget(), customization);
		const SkinEditor::Output output{ a_path, { text.begin(), text.end() } };
		SkinEditor::Publish(std::span(&output, 1), false);
	}
}

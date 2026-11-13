#pragma once

#include "../Material/Material.h"

#include <filesystem>
#include <map>

// Per-character skin adjustments, kept as one editable, shareable JSON file per character in
// Data/SKSE/Plugins/CommunityShaders/AdvancedSkin/Characters.
namespace SkinCharacters
{
	inline constexpr const char* Format = "CommunityShaders.SkinCharacter";

	struct Customization
	{
		std::string name;  ///< Informational only.
		/// Applies to every skin surface of the character. Scalars only: no texture is valid for every UV layout.
		SkinMaterials::Changes all;
		/// Applies only while the character shows this skin texture (material key). Textures are allowed here.
		std::map<std::string, SkinMaterials::Changes> materials;
		bool operator==(const Customization&) const = default;
		bool Empty() const;
	};

	/// Who a file applies to: the player, or every actor that uses an NPC record.
	struct Target
	{
		std::string key;   ///< Normalized for comparison; empty when the character has no stable identity.
		std::string text;  ///< As written in files: "player" or "0xA2C8E~Skyrim.esm".
		bool operator==(const Target& a_other) const { return key == a_other.key; }
		explicit operator bool() const { return !key.empty(); }
	};
	Target TargetOf(RE::Actor* a_actor);

	struct Character
	{
		Target target;
		Customization customization;
		std::filesystem::path file;
	};
	struct Problem
	{
		std::filesystem::path file;
		std::string diagnostic;
	};
	struct State
	{
		std::map<std::string, Character> characters;  ///< By `Target::key`.
		std::vector<Problem> problems;                ///< Files that were ignored, and why.
		uint64_t revision = 1;
	};
	using Snapshot = std::shared_ptr<const State>;

	std::filesystem::path Directory();
	/// Reads every character file again. Requires loaded game data.
	void Reload();
	Snapshot Get();
	const Character* Find(const State& a_state, const std::string& a_key);

	/// Writes the character's file and applies it. An empty customization keeps a winning override with no adjustments. Returns the file.
	std::filesystem::path Save(const Target& a_target, Customization a_customization);
	/// Reads the adjustments of any character file, whoever it targets, for reuse on another character.
	Customization Import(const std::filesystem::path& a_path);
	/// Writes a copy for sharing, without changing what the game uses.
	void Export(const std::filesystem::path& a_path, const Target& a_target, const Customization& a_customization);
}

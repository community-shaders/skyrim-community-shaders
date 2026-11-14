#pragma once

#include "../Material/Material.h"

#include <filesystem>

namespace SkinEditor
{
	using Bytes = std::vector<uint8_t>;

	std::string UTF8(const std::filesystem::path& a_path);
	/// Reads an absolute/real file, or otherwise a game resource (loose file or archive). Throws when unavailable.
	Bytes ReadResource(const std::string& a_path);
	/// Reads a game resource (loose file or archive). Returns nothing when it does not exist.
	std::optional<Bytes> FindResource(const std::string& a_path);
	/// True when the game resource exists as a loose file under Data (including a mod manager's virtual Data).
	bool IsLooseResource(const std::string& a_path);
	uint64_t Fingerprint(std::span<const uint8_t> a_bytes);
	/// Size and content hash of a game resource; used to record which diffuse a material was authored for.
	SkinMaterials::Fingerprint ResourceFingerprint(const std::string& a_path);

	/// System file dialogs are only for files outside the game's resources: source images for the
	/// texture tools and copies written for sharing. Game resources are chosen with `ResourcePicker`.
	enum FileFilter
	{
		ImageFilter,
		CharacterFilter
	};
	std::optional<std::filesystem::path> ChooseFile(bool a_save, FileFilter a_filter, const wchar_t* a_extension, const std::filesystem::path& a_initial = {});
	std::optional<std::filesystem::path> ChooseFolder();
	struct Output
	{
		std::filesystem::path path;
		Bytes bytes;
	};
	/// Stages, verifies and replaces every output, rolling back on failure. Backups of replaced files are
	/// kept beside them unless `a_keepBackups` is false.
	void Publish(std::span<const Output> a_outputs, bool a_keepBackups = true);
	/// Remembers a resource written during this session. With a mod manager, files created in a mod folder
	/// while the game runs may not be visible through the virtual Data folder until the next launch;
	/// `FindResource` reads them from where they were written so previews still work.
	void RememberOutput(const std::string& a_resource, const std::filesystem::path& a_file);
	void ForgetOutputs();
	/// The file a resource was written to this session, if any.
	std::optional<std::filesystem::path> SessionOutput(const std::string& a_resource);
	/// Writes a resource below a mod folder (or the game's Data folder), verifies it and remembers it.
	void PublishResource(const std::filesystem::path& a_root, const std::string& a_resource, Bytes a_bytes);
}

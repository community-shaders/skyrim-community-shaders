#include "Files.h"

#include "../Runtime/Textures.h"
#include "Utils/FileSystem.h"

#include <cwctype>
#include <fstream>
#include <mutex>
#include <set>
#include <shlobj.h>

namespace SkinEditor
{
	namespace
	{
		constexpr size_t MaxBytes = 512 * 1024 * 1024;

		std::string ResourcePath(const std::string& a_path)
		{
			std::string path = a_path;
			std::replace(path.begin(), path.end(), '/', '\\');
			if (path.size() >= 5 && _strnicmp(path.c_str(), "data\\", 5) == 0)
				path.erase(0, 5);
			return path;
		}
		std::string OutputKey(const std::string& a_resource)
		{
			auto key = ResourcePath(a_resource);
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return key;
		}
		std::mutex outputMutex;
		std::unordered_map<std::string, std::filesystem::path> outputs;
	}

	std::optional<std::filesystem::path> SessionOutput(const std::string& a_resource)
	{
		std::lock_guard lock(outputMutex);
		const auto found = outputs.find(OutputKey(a_resource));
		return found == outputs.end() ? std::nullopt : std::optional(found->second);
	}

	void RememberOutput(const std::string& a_resource, const std::filesystem::path& a_file)
	{
		std::lock_guard lock(outputMutex);
		outputs[OutputKey(a_resource)] = std::filesystem::absolute(a_file);
	}

	void ForgetOutputs()
	{
		std::lock_guard lock(outputMutex);
		outputs.clear();
	}

	std::string UTF8(const std::filesystem::path& a_path)
	{
		return Util::PathHelpers::ToUtf8(a_path);
	}

	uint64_t Fingerprint(std::span<const uint8_t> a_bytes)
	{
		uint64_t value = 14695981039346656037ULL;
		for (auto byte : a_bytes)
			value = (value ^ byte) * 1099511628211ULL;
		return value;
	}

	std::optional<Bytes> FindResource(const std::string& a_path)
	{
		// Session replacements take precedence even while virtual Data still exposes an older file.
		if (const auto file = SessionOutput(a_path); file && std::filesystem::exists(*file))
			return ReadResource(UTF8(*file));
		RE::BSResourceNiBinaryStream input(ResourcePath(a_path));
		if (!input.good() || !input.stream)
			return std::nullopt;
		if (input.stream->totalSize > MaxBytes)
			throw std::runtime_error(T("feature.skin.asset_read_limit", "Cannot read asset or asset exceeds 512 MiB: ") + a_path);
		Bytes bytes(input.stream->totalSize);
		if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<uint32_t>(bytes.size())))
			throw std::runtime_error(T("feature.skin.could_not_read_the_complete_game_resource", "Could not read the complete game resource: ") + a_path);
		return bytes;
	}

	Bytes ReadResource(const std::string& a_path)
	{
		const auto path = std::filesystem::u8path(a_path);
		if (path.is_absolute() || std::filesystem::is_regular_file(path)) {
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			const auto size = input.tellg();
			if (!input || size < 0 || size > static_cast<std::streamoff>(MaxBytes))
				throw std::runtime_error(T("feature.skin.asset_read_limit", "Cannot read asset or asset exceeds 512 MiB: ") + a_path);
			Bytes bytes(static_cast<size_t>(size));
			input.seekg(0);
			if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
				throw std::runtime_error(T("feature.skin.could_not_read_the_complete_asset", "Could not read the complete asset: ") + a_path);
			return bytes;
		}
		if (auto bytes = FindResource(a_path))
			return std::move(*bytes);
		throw std::runtime_error(T("feature.skin.game_resource_is_unavailable", "Game resource is unavailable: ") + a_path);
	}

	bool IsLooseResource(const std::string& a_path)
	{
		// Relative to the working directory, as elsewhere in this project, so that files which only exist
		// in a mod manager's virtual Data folder resolve.
		std::error_code error;
		return std::filesystem::exists(std::filesystem::path("Data") / std::filesystem::u8path(ResourcePath(a_path)), error);
	}

	SkinMaterials::Fingerprint ResourceFingerprint(const std::string& a_path)
	{
		const auto bytes = ReadResource(a_path);
		return { bytes.size(), Fingerprint(bytes) };
	}

	std::optional<std::filesystem::path> ChooseFolder()
	{
		std::array<wchar_t, MAX_PATH> buffer{};
		const auto title = winrt::to_hstring(T("feature.skin.choose_output_folder_title", "Choose the mod folder that contains (or will contain) textures"));
		BROWSEINFOW dialog{};
		dialog.hwndOwner = GetActiveWindow();
		dialog.pszDisplayName = buffer.data();
		dialog.lpszTitle = title.c_str();
		dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX;
		const auto item = SHBrowseForFolderW(&dialog);
		if (!item)
			return std::nullopt;
		const bool resolved = SHGetPathFromIDListW(item, buffer.data()) != FALSE;
		CoTaskMemFree(item);
		if (!resolved)
			throw std::runtime_error(T("feature.skin.choose_a_file_system_folder", "Choose an ordinary file system folder."));
		return std::filesystem::path(buffer.data());
	}

	std::optional<std::filesystem::path> ChooseFile(bool a_save, FileFilter a_filter, const wchar_t* a_extension, const std::filesystem::path& a_initial)
	{
		const char* label;
		const wchar_t* pattern;
		switch (a_filter) {
		case ImageFilter:
			label = T("feature.skin.image_filter", "Texture images");
			pattern = L"*.dds;*.png;*.tga;*.tif;*.tiff;*.bmp";
			break;
		default:
			label = T("feature.skin.character_file_filter", "Skin character file");
			pattern = L"*.json";
			break;
		}
		std::wstring filter = winrt::to_hstring(label).c_str();
		filter.push_back(L'\0');
		filter.append(pattern);
		filter.append(2, L'\0');
		const auto result = Util::ShowFileDialog({ .filter = filter.c_str(), .extension = a_extension, .initial = a_initial, .save = a_save });
		if (result.error)
			throw std::runtime_error(I18n::GetSingleton()->Format("feature.skin.file_dialog_failed", { { "code", std::format("{:08X}", result.error) } }, "File dialog failed ({code})."));
		if (!result.paths.empty())
			return result.paths.front();
		return std::nullopt;
	}

	void Publish(std::span<const Output> a_outputs, bool a_keepBackups)
	{
		struct File
		{
			std::filesystem::path path, temporary, backup;
			Bytes before;
			const Output* output = nullptr;
			bool existed = false;
			bool replaced = false;
		};
		std::vector<File> files;
		std::set<std::wstring> destinations;
		try {
			for (const auto& output : a_outputs) {
				File file;
				file.path = std::filesystem::absolute(output.path).lexically_normal();
				auto key = file.path.wstring();
				std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
				if (!destinations.insert(key).second)
					throw std::runtime_error(T("feature.skin.more_than_one_output_uses_the_same_destination", "More than one output uses the same destination."));
				std::filesystem::create_directories(file.path.parent_path());
				file.output = &output;
				const auto temporary = file.path.wstring() + L".cs-skin." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
				file.temporary = temporary + L".tmp";
				for (uint32_t suffix = 1; std::filesystem::exists(file.temporary); ++suffix)
					file.temporary = temporary + L"." + std::to_wstring(suffix) + L".tmp";
				file.existed = std::filesystem::exists(file.path);
				if (file.existed) {
					file.before = ReadResource(UTF8(file.path));
					file.backup = file.path.wstring() + L".cs-skin.bak";
					for (uint32_t suffix = 1; std::filesystem::exists(file.backup); ++suffix)
						file.backup = file.path.wstring() + L".cs-skin." + std::to_wstring(suffix) + L".bak";
				}
				files.push_back(std::move(file));
				auto& staged = files.back();
				std::ofstream stream(staged.temporary, std::ios::binary);
				if (!stream.write(reinterpret_cast<const char*>(output.bytes.data()), static_cast<std::streamsize>(output.bytes.size())) || !stream.flush())
					throw std::runtime_error(T("feature.skin.could_not_stage", "Could not stage: ") + UTF8(staged.path));
				stream.close();
				if (ReadResource(UTF8(staged.temporary)) != output.bytes)
					throw std::runtime_error(T("feature.skin.staged_file_verification_failed", "Staged file verification failed: ") + UTF8(staged.path));
			}
			for (auto& file : files) {
				if (std::filesystem::exists(file.path) != file.existed || (file.existed && ReadResource(UTF8(file.path)) != file.before))
					throw std::runtime_error(T("feature.skin.output_changed_during_publication", "Output changed during publication: ") + UTF8(file.path));
				if (file.existed) {
					if (!CopyFileW(file.path.c_str(), file.backup.c_str(), TRUE)) {
						const auto code = GetLastError();
						throw std::runtime_error(I18n::GetSingleton()->Format("feature.skin.backup_write_failed", { { "path", UTF8(file.backup) }, { "code", std::to_string(code) } }, "Could not create backup: {path} (Windows error {code}). The original file has not been replaced."));
					}
					if (ReadResource(UTF8(file.backup)) != file.before)
						throw std::runtime_error(T("feature.skin.backup_verification_failed", "Backup verification failed: ") + UTF8(file.backup));
				}
				const auto unchanged = [&] {
					return std::filesystem::exists(file.path) == file.existed && (!file.existed || ReadResource(UTF8(file.path)) == file.before);
				};
				if (!unchanged())
					throw std::runtime_error(T("feature.skin.output_changed_during_publication", "Output changed during publication: ") + UTF8(file.path));
				const DWORD flags = MOVEFILE_WRITE_THROUGH | (file.existed ? MOVEFILE_REPLACE_EXISTING : 0);
				if (MoveFileExW(file.temporary.c_str(), file.path.c_str(), flags)) {
					file.replaced = true;
				} else {
					const auto code = GetLastError();
					if (!unchanged())
						throw std::runtime_error(T("feature.skin.output_changed_during_publication", "Output changed during publication: ") + UTF8(file.path));
					std::ofstream fallback(file.path, std::ios::binary | std::ios::trunc);
					if (fallback.is_open()) {
						file.replaced = true;
						fallback.write(reinterpret_cast<const char*>(file.output->bytes.data()), static_cast<std::streamsize>(file.output->bytes.size()));
						fallback.flush();
						fallback.close();
					}
					if (!file.replaced || fallback.fail())
						throw std::runtime_error(I18n::GetSingleton()->Format("feature.skin.publish_write_failed", { { "path", UTF8(file.path) }, { "code", std::to_string(code) } }, "Could not save: {path} (replacement failed with Windows error {code}; direct write also failed). Your draft is retained."));
					logger::warn("[Advanced Skin] Saved '{}' by direct write after replacement failed with Windows error {}", UTF8(file.path), code);
				}
				if (ReadResource(UTF8(file.path)) != file.output->bytes)
					throw std::runtime_error(T("feature.skin.saved_file_verification_failed", "Saved file verification failed: ") + UTF8(file.path));
				std::error_code ignored;
				std::filesystem::remove(file.temporary, ignored);
			}
			if (!a_keepBackups)
				for (const auto& file : files) {
					std::error_code ignored;
					if (file.existed)
						std::filesystem::remove(file.backup, ignored);
				}
		} catch (const std::exception& error) {
			std::string message = error.what();
			for (auto it = files.rbegin(); it != files.rend(); ++it) {
				if (!it->replaced)
					continue;
				bool restored = false;
				try {
					restored = it->existed ? CopyFileW(it->backup.c_str(), it->path.c_str(), FALSE) != FALSE && ReadResource(UTF8(it->path)) == it->before : DeleteFileW(it->path.c_str()) != FALSE;
				} catch (const std::exception&) {
				}
				if (!restored)
					message += T("feature.skin.recovery_required", " Recovery required: ") + UTF8(it->path) + T("feature.skin.backup", "; backup: ") + UTF8(it->backup);
			}
			for (const auto& file : files) {
				std::error_code ignored;
				std::filesystem::remove(file.temporary, ignored);
			}
			throw std::runtime_error(message);
		}
	}

	void PublishResource(const std::filesystem::path& a_root, const std::string& a_resource, Bytes a_bytes)
	{
		const auto file = a_root / std::filesystem::u8path(a_resource);
		const Output output{ file, std::move(a_bytes) };
		Publish(std::span(&output, 1), false);
		RememberOutput(a_resource, file);
	}
}

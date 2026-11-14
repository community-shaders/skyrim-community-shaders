#include "ResourceBrowser.h"

#include "Files.h"

#include <cctype>
#include <imgui_stdlib.h>

namespace SkinEditor
{
	namespace
	{
		constexpr const char* Root = "textures";

		std::string Lower(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}
		// Relative to the working directory, so that a mod manager's virtual Data folder is what gets listed.
		std::filesystem::path OnDisk(const std::string& a_directory)
		{
			return std::filesystem::path("Data") / std::filesystem::u8path(a_directory);
		}
		std::string Parent(const std::string& a_path)
		{
			const auto split = a_path.rfind('/');
			return split == std::string::npos ? std::string(Root) : a_path.substr(0, split);
		}
		std::string StartDirectory(const std::string& a_start)
		{
			auto directory = Lower(a_start);
			std::replace(directory.begin(), directory.end(), '\\', '/');
			if (directory.starts_with("data/"))
				directory.erase(0, 5);
			if (!directory.starts_with("textures/"))
				return Root;
			// The hint is a file, and it may come from an archive: show the nearest folder that exists loose.
			std::error_code error;
			do
				directory = Parent(directory);
			while (directory != Root && !std::filesystem::is_directory(OnDisk(directory), error));
			return directory;
		}
		void List(ResourceBrowser& a_state, std::string_view a_suffix)
		{
			a_state.folders.clear();
			a_state.files.clear();
			a_state.error.clear();
			a_state.listed = true;
			try {
				for (const auto& entry : std::filesystem::directory_iterator(OnDisk(a_state.directory))) {
					auto name = UTF8(entry.path().filename());
					if (entry.is_directory())
						a_state.folders.push_back(std::move(name));
					else if (Lower(name).ends_with(a_suffix))
						a_state.files.push_back(std::move(name));
				}
			} catch (const std::exception& e) {
				a_state.error = e.what();
			}
			const auto order = [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); };
			std::sort(a_state.folders.begin(), a_state.folders.end(), order);
			std::sort(a_state.files.begin(), a_state.files.end(), order);
		}
		void Enter(ResourceBrowser& a_state, std::string a_directory)
		{
			a_state.directory = std::move(a_directory);
			a_state.filter.clear();
			a_state.listed = false;
		}
	}

	void OpenResourceBrowser(ResourceBrowser& a_state, const std::string& a_start)
	{
		a_state = {};
		a_state.directory = StartDirectory(a_start);
		ImGui::OpenPopup("##skin_resource_browser");
	}

	std::optional<std::string> ResourcePicker(ResourceBrowser& a_state, const char* a_label, const std::string& a_start, std::string_view a_suffix)
	{
		if (ImGui::Button(a_label))
			OpenResourceBrowser(a_state, a_start);
		return ResourceBrowserPopup(a_state, a_suffix);
	}

	std::optional<std::string> ResourceBrowserPopup(ResourceBrowser& a_state, std::string_view a_suffix)
	{
		std::optional<std::string> chosen;
		if (!ImGui::BeginPopup("##skin_resource_browser"))
			return chosen;
		if (!a_state.listed)
			List(a_state, a_suffix);
		ImGui::TextUnformatted(T("feature.skin.browser_title", "Game Data folder, as the game sees it"));
		ImGui::TextDisabled("%s/", a_state.directory.c_str());
		ImGui::BeginDisabled(a_state.directory == Root);
		if (ImGui::Button(T("feature.skin.browser_up", "Up")))
			Enter(a_state, Parent(a_state.directory));
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(T("feature.skin.refresh_surfaces", "Refresh")))
			a_state.listed = false;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
		ImGui::InputTextWithHint("##filter", T("feature.skin.browser_filter", "Filter this folder"), &a_state.filter);
		if (!a_state.error.empty())
			ImGui::TextWrapped("%s", a_state.error.c_str());
		std::string enter;
		if (ImGui::BeginChild("##entries", ImVec2(ImGui::GetFontSize() * 36, ImGui::GetFontSize() * 22), ImGuiChildFlags_Borders)) {
			const auto filter = Lower(a_state.filter);
			const auto visible = [&](const std::string& name) { return filter.empty() || Lower(name).find(filter) != std::string::npos; };
			for (const auto& folder : a_state.folders)
				if (visible(folder) && ImGui::Selectable((folder + "/").c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups))
					enter = a_state.directory + "/" + folder;
			for (const auto& file : a_state.files)
				if (visible(file) && ImGui::Selectable(file.c_str()))
					chosen = Lower(a_state.directory + "/" + file);
			if (a_state.folders.empty() && a_state.files.empty() && a_state.error.empty())
				ImGui::TextDisabled("%s", T("feature.skin.browser_empty", "No matching loose files here."));
		}
		ImGui::EndChild();
		ImGui::TextDisabled("%s", T("feature.skin.browser_archive_hint", "Files packed in a BSA are not listed; enter their resource path as text instead."));
		if (!enter.empty())
			Enter(a_state, std::move(enter));
		if (chosen || ImGui::Button(T("feature.skin.cancel", "Cancel")))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return chosen;
	}
}

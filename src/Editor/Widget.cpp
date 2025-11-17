#include "Widget.h"
#include "State.h"
#include "Util.h"

void Widget::Save()
{
	SaveSettings();
	const std::string filePath = std::format("{}\\{}", State::GetSingleton()->folderPath, GetFolderName());
	const std::string file = std::format("{}\\{}.json", filePath, GetEditorID());

	std::ofstream settingsFile(file);
	if (!std::filesystem::exists(filePath) || !std::filesystem::is_directory(filePath)) {
		try {
			std::filesystem::create_directories(filePath);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error creating directory during Save ({}) : {}\n", filePath, e.what());
			return;
		}
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open settings file: {}", file);
		return;
	}

	if (settingsFile.fail()) {
		logger::warn("Unable to create settings file: {}", file);
		settingsFile.close();
		return;
	}

	logger::info("Saving settings file: {}", file);

	try {
		settingsFile << js.dump(1);

		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
}

void Widget::Load()
{
	std::string filePath = std::format("{}\\{}\\{}.json", State::GetSingleton()->folderPath, GetFolderName(), GetEditorID());

	std::ifstream settingsFile(filePath);

	if (!std::filesystem::exists(filePath)) {
		// Does not have any settings so just return.
		return;
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to load settings file: {}", filePath);
		return;
	}

	try {
		js << settingsFile;
		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
	LoadSettings();
}

void Widget::DrawMenu()
{
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("Menu")) {
			if (ImGui::MenuItem("Save")) {
				Save();
			}
			if (ImGui::MenuItem("Load")) {
				Load();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

std::string Widget::GetFolderName()
{
	switch (form->GetFormType()) {
	case RE::FormType::Weather:
		return "Weathers";
	case RE::FormType::LightingMaster:
		return "LightingTemplates";
	case RE::FormType::WorldSpace:
		return "WorldSpaces";
	default:
		return "Unknown";
	}
}

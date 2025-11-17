#pragma once

#include "Buffer.h"

#include "Weather/LightingTemplateWidget.h"
#include "Weather/WeatherWidget.h"
#include "Weather/WorldSpaceWidget.h"
#include "WeatherUtils.h"
#include "Widget.h"

class EditorWindow
{
public:
	static EditorWindow* GetSingleton()
	{
		static EditorWindow singleton;
		return &singleton;
	}

	bool open = false;
	const static int maxRecordMarkers = 10;

	Texture2D* tempTexture;

	std::vector<Widget*> weatherWidgets;
	std::vector<Widget*> worldSpaceWidgets;
	std::vector<Widget*> lightingTemplateWidgets;

	void ShowObjectsWindow();

	void ShowViewportWindow();

	void ShowWidgetWindow();

	void RenderUI();

	void SetupResources();

	void Draw();

	struct Settings
	{
		std::map<std::string, ImVec4> recordMarkers = {
			{ "TO-DO", { 0.9f, 0.15, 0.15, 1 } },
			{ "In-progress", { 0.5f, 0.8f, 0.0f, 1 } },
			{ "Done", { 0.05f, 0.85f, 0.3f, 1 } }
		};
		std::map<std::string, std::string> markedRecords;
	};

	Settings settings;

private:
	void SaveAll();
	void SaveSettings();
	void LoadSettings();
	void ShowSettingsWindow();
	void Save();
	void Load();
	json j;
	std::string settingsFilename = "EditorSettings";
	bool showSettingsWindow = false;
};
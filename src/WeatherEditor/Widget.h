
#pragma once

#include "Util.h"

class WidgetSharedData
{
private:
	int uniqueID = 0;

public:
	static WidgetSharedData* GetSingleton()
	{
		static WidgetSharedData sharedData;
		return &sharedData;
	}

	int GetNewID()
	{
		return -uniqueID++;
	}
};

class Widget
{
public:
	RE::TESForm* form = nullptr;

	virtual ~Widget() {};

	virtual std::string GetEditorID() const
	{
		return form->GetFormEditorID();
	}

	virtual std::string GetFormID() const
	{
		return std::format("{:08X}", form->GetFormID());
	}

	virtual std::string GetFilename() const
	{
		if (auto file = form->GetFile())
			return std::format("{}", file->GetFilename());
		return "Generated";
	}

	virtual void DrawWidget() = 0;

	bool open = false;

	bool IsOpen() const
	{
		return open;
	}

	void SetOpen(bool state = true)
	{
		open = state;
	}

	void Save();
	void Load();
	void Delete();
	bool HasSavedFile() const;

	virtual void LoadSettings() = 0;
	virtual void SaveSettings() = 0;
	virtual void ApplyChanges() = 0;
	virtual void RevertChanges() { LoadSettings(); }
	virtual bool HasUnsavedChanges() const { return false; }

	// Draw common header with search bar and action buttons
	void DrawWidgetHeader(const char* searchId, bool showApplyRevert = true, bool showSaveLoad = false, bool showForceWeather = false, RE::TESWeather* weather = nullptr);

	// Search functionality
	char searchBuffer[256] = "";
	bool searchActive = false;

	bool MatchesSearch(const std::string& text) const;

protected:
	json js = json();
	virtual void DrawMenu();
	std::string GetFolderName();
};
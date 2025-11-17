
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

	virtual ~Widget(){};

	virtual std::string GetEditorID()
	{
		return form->GetFormEditorID();
	}

	virtual std::string GetFormID()
	{
		return std::format("{:08X}", form->GetFormID());
	}

	virtual std::string GetFilename()
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

	virtual void LoadSettings() = 0;
	virtual void SaveSettings() = 0;

protected:
	json js = json();
	virtual void DrawMenu();
	std::string GetFolderName();
};
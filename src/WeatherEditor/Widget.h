
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
		// If cachedEditorID looks like a fallback ID, try to get the real one
		if (cachedEditorID.find("VolumetricLighting_") == 0 && form) {
			const char* editorID = form->GetFormEditorID();
			if (editorID && editorID[0] != '\0') {
				const_cast<Widget*>(this)->cachedEditorID = editorID;
				return editorID;
			}
		}
		return cachedEditorID;
	}

	virtual std::string GetFormID() const
	{
		if (!form) return "00000000";
		return std::format("{:08X}", form->GetFormID());
	}

	virtual std::string GetFilename() const
	{
		if (!form) return "Invalid";
		if (auto file = form->GetFile())
			return std::format("{}", file->GetFilename());
		return "Generated";
	}

	void CacheFormData()
	{
		if (!form) {
			cachedEditorID = "Invalid";
			return;
		}

		// Try GetFormEditorID first
		const char* editorID = form->GetFormEditorID();
		if (editorID && editorID[0] != '\0') {
			cachedEditorID = editorID;
			return;
		}

		// Search the global EditorID map
		auto [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		if (map) {
			RE::BSReadLockGuard locker(lock);
			for (const auto& [name, f] : *map) {
				if (f == form) {
					cachedEditorID = std::string(name.c_str());
					return;
				}
			}
		}

		// Fallback to FormID-based names
		auto formType = form->GetFormType();
		switch (formType) {
		case RE::FormType::ImageSpace:
			cachedEditorID = std::format("ImageSpace_{:08X}", form->GetFormID());
			break;
		case RE::FormType::VolumetricLighting:
			cachedEditorID = std::format("VolumetricLighting_{:08X}", form->GetFormID());
			break;
		case RE::FormType::ShaderParticleGeometryData:
			cachedEditorID = std::format("ShaderParticleGeometry_{:08X}", form->GetFormID());
			break;
		case RE::FormType::LensFlare:
			cachedEditorID = std::format("LensFlare_{:08X}", form->GetFormID());
			break;
		case RE::FormType::ReferenceEffect:
			cachedEditorID = std::format("VisualEffect_{:08X}", form->GetFormID());
			break;
		default:
			cachedEditorID = std::format("Form_{:08X}", form->GetFormID());
			break;
		}
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
	std::string cachedEditorID;
	virtual void DrawMenu();
	std::string GetFolderName();
};

// Simple widget for caching form data without full widget functionality
class SimpleFormWidget : public Widget
{
public:
	void DrawWidget() override {}
	void LoadSettings() override {}
	void SaveSettings() override {}
	void ApplyChanges() override {}
};
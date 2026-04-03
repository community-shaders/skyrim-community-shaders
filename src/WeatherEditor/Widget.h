
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
		// If using a fallback ID, retry getting the real EditorID
		if (isFallbackEditorID && form) {
			const char* editorID = form->GetFormEditorID();
			if (editorID && editorID[0] != '\0') {
				cachedEditorID = editorID;
				isFallbackEditorID = false;
				return editorID;
			}
		}
		return cachedEditorID;
	}

	virtual std::string GetFormID() const
	{
		if (!form)
			return "00000000";
		return std::format("{:08X}", form->GetFormID());
	}

	virtual std::string GetFilename() const
	{
		if (!form)
			return "Invalid";
		if (auto file = form->GetFile())
			return std::format("{}", file->GetFilename());
		return "Generated";
	}

	void CacheFormData()
	{
		if (!form) {
			cachedEditorID = "Invalid";
			isFallbackEditorID = false;
			return;
		}

		// Try GetFormEditorID first
		const char* editorID = form->GetFormEditorID();
		if (editorID && editorID[0] != '\0') {
			cachedEditorID = editorID;
			isFallbackEditorID = false;
			return;
		}

		// Search the global EditorID map
		auto [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		if (map) {
			RE::BSReadLockGuard locker(lock);
			for (const auto& [name, f] : *map) {
				if (f == form) {
					cachedEditorID = std::string(name.c_str());
					isFallbackEditorID = false;
					return;
				}
			}
		}

		// Fallback: use SPID-format filename (0xLocalFormID~PluginName) for load-order independence
		const auto* file = form->GetFile();
		const auto spidID = file ? std::format("0x{:X}~{}", form->GetLocalFormID(), file->GetFilename()) : std::format("0x{:X}", form->GetLocalFormID());
		auto formType = form->GetFormType();
		switch (formType) {
		case RE::FormType::ImageSpace:
			cachedEditorID = std::format("IS_{}", spidID);
			break;
		case RE::FormType::VolumetricLighting:
			cachedEditorID = std::format("VL_{}", spidID);
			break;
		case RE::FormType::ShaderParticleGeometryData:
			cachedEditorID = std::format("Particle_{}", spidID);
			break;
		case RE::FormType::LensFlare:
			cachedEditorID = std::format("LensFlare_{}", spidID);
			break;
		case RE::FormType::ReferenceEffect:
			cachedEditorID = std::format("VisualEffect_{}", spidID);
			break;
		default:
			cachedEditorID = std::format("Form_{}", spidID);
			break;
		}
		isFallbackEditorID = true;
	}

	virtual void DrawWidget() = 0;

	/// Type name for widget-type-level state sharing (window size, etc.).
	virtual const char* GetWidgetTypeName() const = 0;

	/// Call instead of SetupWidgetWindowDefaults + ImGui::Begin. Tracks per-type window size.
	bool BeginWidgetWindow();

	bool open = false;

	bool IsOpen() const
	{
		return open;
	}

	void SetOpen(bool state = true)
	{
		open = state;
	}

	/// Returns a window title with unique ImGui ID: "EditorID###FormID"
	std::string GetWindowTitle() const
	{
		return std::format("{}###{}", GetEditorID(), GetFormID());
	}

	void Save();
	void Load();
	bool HasSavedFile() const;

	virtual void Delete();
	virtual void LoadSettings() = 0;
	virtual void SaveSettings() = 0;
	virtual void ApplyChanges() = 0;
	virtual void RevertChanges() { LoadSettings(); }
	virtual bool HasUnsavedChanges() const { return false; }

	// Draw common header with search bar and action buttons
	void DrawWidgetHeader(const char* searchId, bool showApply = true, bool showSaveLoadRevert = false, bool showForceWeather = false, RE::TESWeather* weather = nullptr);

	// Search functionality
	char searchBuffer[256] = "";
	bool searchActive = false;
	int deleteConfirmationFrame = -1;

	bool MatchesSearch(const std::string& text) const;

	void DrawDeleteConfirmationModal(const char* popupId = "DeleteConfirmation");

	json js = json();

protected:
	mutable std::string cachedEditorID;
	mutable bool isFallbackEditorID = false;
	virtual void DrawMenu();
	std::string GetFolderName();
};

// Simple widget for caching form data without full widget functionality
class SimpleFormWidget : public Widget
{
public:
	void DrawWidget() override {}
	const char* GetWidgetTypeName() const override { return ""; }
	void LoadSettings() override {}
	void SaveSettings() override {}
	void ApplyChanges() override {}
};
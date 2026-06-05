#pragma once

#include "../../I18n/I18n.h"
#include "../Widget.h"

// Simple widget for displaying form information without editing
class SimpleFormWidget : public Widget
{
public:
	SimpleFormWidget() = default;
	~SimpleFormWidget() override = default;

	void SetFormData(const char* editorID, uint32_t formID, const char* filename)
	{
		this->editorID = editorID ? editorID : "";
		this->formID = formID;
		this->filename = filename ? filename : "Generated";
	}

	std::string GetEditorID() const override { return editorID; }
	std::string GetFormID() const override { return std::format("{:08X}", formID); }
	std::string GetFilename() const override { return filename; }

	void DrawWidget() override
	{
		ImGui::Text(T("cs_editor.editor_id_label", "EditorID: %s"), editorID.c_str());
		ImGui::Text(T("cs_editor.form_id_label", "FormID: %08X"), formID);
		ImGui::Text(T("cs_editor.file_label", "File: %s"), filename.c_str());
		ImGui::Separator();
		ImGui::TextWrapped("%s", T("cs_editor.form_reference_note", "This form is referenced by weather records. To change which form is used, edit the Records tab in the Weather widget."));
	}

	void LoadSettings() override {}
	void SaveSettings() override {}
	void ApplyChanges() override {}

private:
	std::string editorID;
	uint32_t formID = 0;
	std::string filename;
};

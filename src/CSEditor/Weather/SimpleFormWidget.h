#pragma once

#include "../../I18n/I18n.h"
#include "../Widget.h"

/**
 * @brief Read-only widget for displaying basic form identification without editing.
 *
 * Shows EditorID, FormID, and source filename for forms that are referenced
 * by weather records but do not have their own full editor widget.
 */
class SimpleFormWidget : public Widget
{
public:
	SimpleFormWidget() = default;
	~SimpleFormWidget() override = default;

	/**
	 * @brief Populates the widget with form identification data.
	 * @param editorID The form's editor ID string, or null for empty.
	 * @param formID   The numeric form ID.
	 * @param filename The source plugin filename, or null for "Generated".
	 */
	void SetFormData(const char* editorID, uint32_t formID, const char* filename)
	{
		this->editorID = editorID ? editorID : "";
		this->formID = formID;
		this->filename = filename ? filename : "Generated";
	}

	/** @brief Returns the cached editor ID string. */
	std::string GetEditorID() const override { return editorID; }

	/** @brief Returns the form ID formatted as an 8-digit hex string. */
	std::string GetFormID() const override { return std::format("{:08X}", formID); }

	/** @brief Returns the source plugin filename for this form. */
	std::string GetFilename() const override { return filename; }

	/** @brief Renders the form identification labels and a note about weather record references. */
	void DrawWidget() override
	{
		ImGui::Text(T("cs_editor.editor_id_label", "EditorID: %s"), editorID.c_str());
		ImGui::Text(T("cs_editor.form_id_label", "FormID: %08X"), formID);
		ImGui::Text(T("cs_editor.file_label", "File: %s"), filename.c_str());
		ImGui::Separator();
		ImGui::TextWrapped("%s", T("cs_editor.form_reference_note", "This form is referenced by weather records. To change which form is used, edit the Records tab in the Weather widget."));
	}

	/** @brief No-op; this widget has no settings to load. */
	void LoadSettings() override {}

	/** @brief No-op; this widget has no settings to save. */
	void SaveSettings() override {}

	/** @brief No-op; this widget has no changes to apply. */
	void ApplyChanges() override {}

private:
	std::string editorID;
	uint32_t formID = 0;
	std::string filename;
};

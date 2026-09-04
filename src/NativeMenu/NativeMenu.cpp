#include "NativeMenu/NativeMenu.h"

#include "NativeMenu/Vendor/SystemMenuHook.h"

#include "Globals.h"
#include "State.h"

namespace NativeMenu
{
	void __stdcall CommitAndSave(float)
	{
		if (auto* state = globals::state)
			state->Save(State::ConfigMode::USER);
	}

	void RegisterRows(const char* tab, const std::vector<Row>& rows)
	{
		for (const auto& row : rows) {
			const std::string description = row.description ? row.description : std::string{};

			switch (row.type) {
			case RowType::kHeading:
				Engine::AddLabel(tab, row.getText, Engine::Align::kCenter);
				break;
			case RowType::kCheckbox:
				Engine::AddSetting(tab, Engine::Type::kCheckbox, row.label, row.getValue, row.setValue,
					row.defaultValue, {}, row.isEnabled, nullptr, description, row.commit);
				break;
			case RowType::kDropdown:
				Engine::AddSetting(tab, Engine::Type::kDropdown, row.label, row.getValue, row.setValue,
					row.defaultValue, row.options, row.isEnabled, nullptr, description, row.commit);
				break;
			case RowType::kSlider:
				Engine::AddSetting(tab, Engine::Type::kSlider, row.label, row.getValue, row.setValue,
					row.defaultValue, {}, row.isEnabled, row.formatValue, description, row.commit);
				break;
			case RowType::kButton:
				Engine::AddButton(tab, row.label, row.onPress, description, row.isEnabled);
				break;
			}
		}
	}

	void Register()
	{
		Vendor::SystemMenuHook::Install();
		RegisterRows("Graphics", GraphicsRows());
	}
}

#pragma once

// Adapted from NativeSystemMenuFramework (https://github.com/RoseEden30/NativeSystemMenuFramework)
// License: GPL-3.0-or-later

#include <functional>

namespace NativeMenu::Vendor::VanillaSettingsEngine
{
	enum class Type
	{
		kSlider = 0,
		kDropdown = 1,
		kCheckbox = 2,
		kLabel = 3,
		kButton = 4,
	};

	enum class Align
	{
		kLeft = 0,
		kCenter = 1,
		kRight = 2,
	};

	void Tick(RE::JournalMenu* a_this, RE::GFxMovieView* a_view, RE::GFxValue& a_systemPage);
	void Reset();

	bool AddSetting(std::string a_tab, Type a_type, std::string a_label, std::function<float()> a_getValue,
		std::function<void(float)> a_onChange, float a_defaultValue, std::vector<std::string> a_options,
		std::function<bool()> a_isEnabled = nullptr,
		std::function<void(float a_value, char* a_buffer, int a_bufferSize)> a_formatValue = nullptr,
		std::string a_description = {}, std::function<void(float)> a_onCommit = nullptr);

	bool AddLabel(std::string a_tab, std::function<void(char* a_buffer, int a_bufferSize)> a_getText,
		Align a_align = Align::kLeft);

	bool AddButton(std::string a_tab, std::string a_label, std::function<void()> a_onPress,
		std::string a_description = {}, std::function<bool()> a_isEnabled = nullptr);
}

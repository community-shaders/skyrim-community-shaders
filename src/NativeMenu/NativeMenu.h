#pragma once

#include "NativeMenu/Vendor/VanillaSettingsEngine.h"

#include <cstdio>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace NativeMenu
{
	namespace Engine = Vendor::VanillaSettingsEngine;

	using SettingGetter = float(__stdcall*)();
	using SettingSetter = void(__stdcall*)(float);
	using SettingCommit = void(__stdcall*)(float);
	using SettingIsEnabled = bool(__stdcall*)();
	using SettingFormatValue = void(__stdcall*)(float, char*, int);
	using LabelGetText = void(__stdcall*)(char*, int);
	using ButtonPress = void(__stdcall*)();

	enum class RowType
	{
		kHeading,
		kCheckbox,
		kDropdown,
		kSlider,
		kButton,
	};

	struct Row
	{
		RowType type = RowType::kCheckbox;
		const char* label = nullptr;
		const char* description = nullptr;

		SettingGetter getValue = nullptr;
		SettingSetter setValue = nullptr;
		SettingCommit commit = nullptr;
		SettingIsEnabled isEnabled = nullptr;
		SettingFormatValue formatValue = nullptr;
		float defaultValue = 0.0f;
		std::vector<std::string> options;

		LabelGetText getText = nullptr;

		ButtonPress onPress = nullptr;
	};

	void __stdcall CommitAndSave(float value);

	inline void AppendRows(std::vector<Row>& dest, std::vector<Row> src)
	{
		dest.insert(dest.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
	}

	inline Row Checkbox(const char* label, SettingGetter getValue, SettingSetter setValue, float defaultValue,
		const char* description = nullptr, SettingIsEnabled isEnabled = nullptr)
	{
		Row row;
		row.type = RowType::kCheckbox;
		row.label = label;
		row.description = description;
		row.getValue = getValue;
		row.setValue = setValue;
		row.defaultValue = defaultValue;
		row.isEnabled = isEnabled;
		row.commit = &CommitAndSave;
		return row;
	}

	inline Row Dropdown(const char* label, std::vector<std::string> options, SettingGetter getValue,
		SettingSetter setValue, float defaultValue, const char* description = nullptr,
		SettingIsEnabled isEnabled = nullptr)
	{
		Row row;
		row.type = RowType::kDropdown;
		row.label = label;
		row.description = description;
		row.getValue = getValue;
		row.setValue = setValue;
		row.defaultValue = defaultValue;
		row.options = std::move(options);
		row.isEnabled = isEnabled;
		row.commit = &CommitAndSave;
		return row;
	}

	inline Row Slider(const char* label, SettingGetter getValue, SettingSetter setValue, float defaultValue,
		const char* description = nullptr, SettingIsEnabled isEnabled = nullptr,
		SettingFormatValue formatValue = nullptr)
	{
		Row row;
		row.type = RowType::kSlider;
		row.label = label;
		row.description = description;
		row.getValue = getValue;
		row.setValue = setValue;
		row.defaultValue = defaultValue;
		row.isEnabled = isEnabled;
		row.formatValue = formatValue;
		row.commit = &CommitAndSave;
		return row;
	}

	template <class Root, auto Member>
	struct Bind
	{
		using Value = std::remove_cvref_t<decltype(Root::Live().*Member)>;

		static Value& Ref() { return Root::Live().*Member; }

		static float __stdcall GetValue() { return static_cast<float>(Ref()); }
		static void __stdcall SetValue(float v) { Ref() = static_cast<Value>(v); }

		static float __stdcall GetFlag() { return Ref() != Value{} ? 1.0f : 0.0f; }
		static void __stdcall SetFlag(float v) { Ref() = static_cast<Value>(v != 0.0f ? 1 : 0); }
		static bool __stdcall IsFlagOn() { return Ref() != Value{}; }

		static float Default() { return static_cast<float>(Root::Defaults().*Member); }
	};

	template <class Root, auto Member>
	Row Checkbox(const char* label, const char* description = nullptr, SettingIsEnabled isEnabled = nullptr)
	{
		using B = Bind<Root, Member>;
		return Checkbox(label, &B::GetFlag, &B::SetFlag, B::Default(), description, isEnabled);
	}

	template <class Root, auto Member>
	Row Dropdown(const char* label, std::vector<std::string> options, const char* description = nullptr,
		SettingIsEnabled isEnabled = nullptr)
	{
		using B = Bind<Root, Member>;
		return Dropdown(label, std::move(options), &B::GetValue, &B::SetValue, B::Default(), description, isEnabled);
	}

	template <class Root, auto Member>
	Row Slider(const char* label, const char* description = nullptr, SettingIsEnabled isEnabled = nullptr,
		SettingFormatValue formatValue = nullptr)
	{
		using B = Bind<Root, Member>;
		return Slider(label, &B::GetValue, &B::SetValue, B::Default(), description, isEnabled, formatValue);
	}

	namespace detail
	{
		template <class Tag>
		struct HeadingSlot
		{
			static inline const char* text = "";
			static void __stdcall GetText(char* buffer, int size) { std::snprintf(buffer, size, "%s", text); }
		};

		template <class Tag>
		Row MakeHeading(const char* text)
		{
			HeadingSlot<Tag>::text = text;
			Row row;
			row.type = RowType::kHeading;
			row.getText = &HeadingSlot<Tag>::GetText;
			return row;
		}
	}

#define NATIVE_MENU_HEADING(text) ::NativeMenu::detail::MakeHeading<decltype([] {})>(text)

	inline Row Button(const char* label, ButtonPress onPress, const char* description = nullptr,
		SettingIsEnabled isEnabled = nullptr)
	{
		Row row;
		row.type = RowType::kButton;
		row.label = label;
		row.description = description;
		row.onPress = onPress;
		row.isEnabled = isEnabled;
		return row;
	}

	void RegisterRows(const char* tab, const std::vector<Row>& rows);

	std::vector<Row> GraphicsRows();
	std::vector<Row> HDRRows();
	std::vector<Row> SSGIRows();

	void Register();
}

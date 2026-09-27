#include "EditorWidgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>

#include "Utils/UI.h"

namespace Effects11UI
{
	namespace
	{
		// Ranges up to this width get a slider; wider ranges (e.g. 0..30000 intensities) get a drag
		// field, where a slider would move hundreds of units per pixel.
		constexpr float kSliderMaxRange = 10.0f;
		constexpr int kSliderMaxIntRange = 100;
		constexpr float kLabelColumnWeight = 0.44f;
		constexpr float kValueColumnWeight = 0.56f;

		bool IsLower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
		bool IsUpper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
		bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

		bool IsJoiningWord(std::string_view a_word)
		{
			static constexpr std::string_view words[] = { "Of", "From", "To", "And", "In", "On", "At", "For", "By" };
			return std::find(std::begin(words), std::end(words), a_word) != std::end(words);
		}

		struct ClipboardState
		{
			enum class Kind
			{
				None,
				Float,
				Color
			} kind = Kind::None;
			float values[Clipboard::kColorComponents] = {};
		} clipboard;
	}

	std::string PrettifyName(std::string_view a_key)
	{
		std::string words;
		words.reserve(a_key.size() + 8);

		for (size_t i = 0; i < a_key.size(); ++i) {
			const char c = a_key[i];
			if (c == '_') {
				if (!words.empty() && words.back() != ' ')
					words += ' ';
				continue;
			}
			if (i > 0 && !words.empty() && words.back() != ' ') {
				const char prev = a_key[i - 1];
				const char next = i + 1 < a_key.size() ? a_key[i + 1] : '\0';
				const bool lowerToUpper = IsUpper(c) && (IsLower(prev) || IsDigit(prev));
				const bool acronymEnd = IsUpper(c) && IsUpper(prev) && IsLower(next);
				const bool letterToDigit = IsDigit(c) && !IsDigit(prev);
				if (lowerToUpper || acronymEnd || letterToDigit)
					words += ' ';
			}
			words += c;
		}

		// Lowercase joining words after the first ("Depth Of Field" -> "Depth of Field")
		std::string result;
		result.reserve(words.size());
		size_t start = 0;
		bool first = true;
		while (start < words.size()) {
			size_t end = words.find(' ', start);
			if (end == std::string::npos)
				end = words.size();
			std::string word = words.substr(start, end - start);
			if (!first && IsJoiningWord(word))
				word[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(word[0])));
			if (!result.empty())
				result += ' ';
			result += word;
			first = false;
			start = end + 1;
		}
		return result;
	}

	bool ContainsNoCase(std::string_view a_text, std::string_view a_filter)
	{
		if (a_filter.empty())
			return true;
		auto it = std::search(a_text.begin(), a_text.end(), a_filter.begin(), a_filter.end(), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
		return it != a_text.end();
	}

	int DecimalsForStep(float a_step)
	{
		if (!(a_step > 0.0f))
			return 3;
		const int decimals = static_cast<int>(std::ceil(-std::log10(a_step) - 1e-4f));
		return std::clamp(decimals, 0, 4);
	}

	std::string MakeFormat(int a_decimals, std::string_view a_prefix)
	{
		std::string format;
		// '%' in a (translated) prefix would be read as a conversion
		for (char c : a_prefix) {
			if (c == '%')
				format += '%';
			format += c;
		}
		if (!a_prefix.empty())
			format += "  ";
		format += "%." + std::to_string(std::clamp(a_decimals, 0, 6)) + "f";
		return format;
	}

	bool BeginPropertyTable(const char* a_id)
	{
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
		if (!ImGui::BeginTable(a_id, 2, flags))
			return false;
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, kLabelColumnWeight);
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, kValueColumnWeight);
		return true;
	}

	void EndPropertyTable()
	{
		ImGui::EndTable();
	}

	bool PropertyLabel(const char* a_label, bool a_dimmed, const LabelBadge& a_badge)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		if (a_dimmed)
			Util::TextUnformattedDisabled(a_label);
		else
			ImGui::TextUnformatted(a_label);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		if (a_badge.text)
			Badge(a_badge.text, a_badge.color, a_badge.tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
		return hovered;
	}

	void PropertyContinuation()
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	bool FloatValue(const char* a_id, float* a_value, float a_min, float a_max, float a_step, const char* a_format)
	{
		constexpr ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
		const float range = a_max - a_min;
		if (range > 0.0f && range <= kSliderMaxRange)
			return ImGui::SliderFloat(a_id, a_value, a_min, a_max, a_format, flags);
		const float speed = a_step > 0.0f ? a_step : (std::max)(range / 1000.0f, 0.001f);
		return ImGui::DragFloat(a_id, a_value, speed, a_min, a_max, a_format, flags);
	}

	bool FloatNValue(const char* a_id, float* a_values, int a_components, float a_min, float a_max, const char* a_format)
	{
		constexpr ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
		const int components = std::clamp(a_components, 2, 4);
		const float range = a_max - a_min;
		if (range > 0.0f && range <= kSliderMaxRange)
			return ImGui::SliderScalarN(a_id, ImGuiDataType_Float, a_values, components, &a_min, &a_max, a_format, flags);
		const float speed = (std::max)(range / 1000.0f, 0.001f);
		return ImGui::DragScalarN(a_id, ImGuiDataType_Float, a_values, components, speed, &a_min, &a_max, a_format, flags);
	}

	bool IntValue(const char* a_id, int* a_value, int a_min, int a_max)
	{
		constexpr ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
		if (a_max > a_min && a_max - a_min <= kSliderMaxIntRange)
			return ImGui::SliderInt(a_id, a_value, a_min, a_max, "%d", flags);
		return ImGui::DragInt(a_id, a_value, 1.0f, a_min, a_max, "%d", flags);
	}

	bool ColorValue(const char* a_id, float* a_color, int a_components, bool a_hdr)
	{
		// NoOptions leaves right-click to the parameter's own context menu
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoOptions;
		if (a_hdr)
			flags |= ImGuiColorEditFlags_HDR;
		if (a_components >= 4)
			return ImGui::ColorEdit4(a_id, a_color, flags | ImGuiColorEditFlags_AlphaBar);
		return ImGui::ColorEdit3(a_id, a_color, flags);
	}

	void Badge(const char* a_text, const ImVec4& a_color, const char* a_tooltip)
	{
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextColored(a_color, "%s", a_text);
		if (a_tooltip)
			Util::AddTooltip(a_tooltip, ImGuiHoveredFlags_None);
	}

	void HeaderTags(std::initializer_list<std::pair<const char*, ImVec4>> a_tags, float a_rightInset)
	{
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float y = itemMin.y + (itemMax.y - itemMin.y - ImGui::GetTextLineHeight()) * 0.5f;
		float x = itemMax.x - ImGui::GetStyle().FramePadding.x - a_rightInset;

		auto* drawList = ImGui::GetWindowDrawList();
		for (auto it = std::rbegin(a_tags); it != std::rend(a_tags); ++it) {
			const auto& [text, color] = *it;
			if (!text || !*text)
				continue;
			x -= ImGui::CalcTextSize(text).x;
			drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(color), text);
			x -= spacing;
		}
	}

	namespace Clipboard
	{
		void SetFloat(float a_value)
		{
			clipboard.kind = ClipboardState::Kind::Float;
			clipboard.values[0] = a_value;
			ImGui::SetClipboardText(std::format("{:.4f}", a_value).c_str());
		}

		void SetColor(const float* a_rgb)
		{
			clipboard.kind = ClipboardState::Kind::Color;
			std::copy_n(a_rgb, kColorComponents, clipboard.values);
			ImGui::SetClipboardText(std::format("{:.4f}, {:.4f}, {:.4f}", a_rgb[0], a_rgb[1], a_rgb[2]).c_str());
		}

		bool HasFloat() { return clipboard.kind == ClipboardState::Kind::Float; }
		bool HasColor() { return clipboard.kind == ClipboardState::Kind::Color; }
		float GetFloat() { return clipboard.values[0]; }
		void GetColor(float* a_rgb) { std::copy_n(clipboard.values, kColorComponents, a_rgb); }
	}
}

#include "OverlayFiles.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>

#include "Features/Effects11/IniSerialization.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"

namespace Overlays
{
	namespace
	{
		constexpr std::string_view kSpaces = " \t\r";
		constexpr std::string_view kParamPrefix = "OVERLAYPARAM";
		constexpr std::string_view kInfoSection = "OVERLAYINFO";
		constexpr std::string_view kSettingsSection = "OVERLAYSETTINGS";
		constexpr std::string_view kListSection = "PRESETOVERLAYS";
		constexpr std::string_view kBlacklistSection = "BLACKLISTENTRY1";
		constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

		/** @brief The text without leading and trailing spaces, tabs and carriage returns. */
		std::string_view TrimView(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(kSpaces);
			if (first == std::string_view::npos)
				return {};
			const auto last = a_text.find_last_not_of(kSpaces);
			return a_text.substr(first, last - first + 1);
		}

		/** @brief Drops one pair of surrounding double quotes. */
		std::string_view Unquote(std::string_view a_text)
		{
			a_text = TrimView(a_text);
			if (a_text.size() >= 2 && a_text.front() == '"' && a_text.back() == '"')
				return TrimView(a_text.substr(1, a_text.size() - 2));
			return a_text;
		}

		/** @brief Case-insensitive prefix test. */
		bool StartsWithNoCase(std::string_view a_text, std::string_view a_prefix)
		{
			return a_text.size() >= a_prefix.size() && Util::IEquals(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		/** @brief A key's unquoted value; empty when the section or key is missing. */
		std::string ReadString(const IniSection* a_section, std::string_view a_key)
		{
			const auto* value = a_section ? a_section->Find(a_key) : nullptr;
			return value ? std::string(Unquote(*value)) : std::string();
		}

		/** @brief A key's float value; a_fallback when the section or key is missing or unreadable. */
		float ReadFloat(const IniSection* a_section, std::string_view a_key, float a_fallback)
		{
			const auto* value = a_section ? a_section->Find(a_key) : nullptr;
			return value ? ParseFloat(*value).value_or(a_fallback) : a_fallback;
		}

		/** @brief UIDesc stores line breaks as the two characters \ and n. */
		std::string UnescapeNewlines(std::string a_text)
		{
			for (size_t pos = a_text.find("\\n"); pos != std::string::npos; pos = a_text.find("\\n", pos + 1))
				a_text.replace(pos, 2, "\n");
			return a_text;
		}

		/** @brief Inverse of UnescapeNewlines: line breaks are stored as the two characters \ and n. */
		std::string EscapeNewlines(std::string a_text)
		{
			for (size_t pos = a_text.find('\n'); pos != std::string::npos; pos = a_text.find('\n', pos + 2))
				a_text.replace(pos, 1, "\\n");
			return a_text;
		}

		/** @brief A new [OVERLAYPARAMn] section for an authored op; the caller sets the section name. */
		IniSection FormatOpSection(const Op& a_op)
		{
			IniSection section;
			if (!a_op.fileName.empty())
				section.Set("FileName", a_op.fileName);
			section.Set("Category", a_op.category);
			section.Set("Name", a_op.name);
			section.Set("Operation", FormatOperation(a_op));
			return section;
		}

		/** @brief Fills [OVERLAYINFO] for an overlay created in Effects 11, using the Extender's key order. */
		void FillNewInfo(IniSection& a_info, const Overlay& a_overlay)
		{
			a_info.Set("UIName", a_overlay.uiName);
			a_info.Set("UIGroups", a_overlay.uiGroups);
			a_info.Set("UIDesc", EscapeNewlines(a_overlay.uiDesc));
			a_info.Set("UIOrdering", std::to_string(a_overlay.uiOrdering));
			a_info.Set("NeedsNoApply", "false");
			a_info.Set("IncompatibleOverlays", JoinList(a_overlay.incompatible));
			a_info.Set("AmountMin", Effects11Util::Ini::FormatFloat(a_overlay.amountMin));
			a_info.Set("AmountMax", Effects11Util::Ini::FormatFloat(a_overlay.amountMax));
			a_info.Set("AmountStep", Effects11Util::Ini::FormatFloat(a_overlay.amountStep));
		}
	}

	const std::string* IniSection::Find(std::string_view a_key) const
	{
		for (const auto& [key, value] : entries)
			if (Util::IEquals(key, a_key))
				return &value;
		return nullptr;
	}

	void IniSection::Set(std::string_view a_key, std::string a_value)
	{
		for (auto& [key, value] : entries) {
			if (Util::IEquals(key, a_key)) {
				value = std::move(a_value);
				return;
			}
		}
		entries.emplace_back(std::string(a_key), std::move(a_value));
	}

	const IniSection* IniFile::Find(std::string_view a_name) const
	{
		for (const auto& section : sections)
			if (Util::IEquals(section.name, a_name))
				return &section;
		return nullptr;
	}

	IniSection& IniFile::GetOrAdd(std::string_view a_name)
	{
		for (auto& section : sections)
			if (Util::IEquals(section.name, a_name))
				return section;
		return sections.emplace_back(IniSection{ std::string(a_name), {} });
	}

	std::optional<IniFile> ReadIni(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream)
			return std::nullopt;
		std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		std::erase(text, '\0');
		if (text.starts_with(kUtf8Bom))
			text.erase(0, kUtf8Bom.size());

		IniFile file;
		IniSection* current = nullptr;
		size_t lineStart = 0;
		while (lineStart <= text.size()) {
			const auto lineEnd = std::min(text.find('\n', lineStart), text.size());
			const auto line = TrimView(std::string_view(text).substr(lineStart, lineEnd - lineStart));
			lineStart = lineEnd + 1;
			if (line.empty() || line.front() == ';' || line.front() == '#')
				continue;
			if (line.front() == '[' && line.back() == ']') {
				current = &file.sections.emplace_back(IniSection{ std::string(TrimView(line.substr(1, line.size() - 2))), {} });
				continue;
			}
			const auto equals = line.find('=');
			if (!current || equals == std::string_view::npos)
				continue;
			current->entries.emplace_back(std::string(TrimView(line.substr(0, equals))), std::string(TrimView(line.substr(equals + 1))));
		}
		return file;
	}

	bool WriteIni(const std::filesystem::path& a_path, const IniFile& a_file)
	{
		std::string text;
		for (const auto& section : a_file.sections) {
			text += "[" + section.name + "]\r\n";
			for (const auto& [key, value] : section.entries)
				text += key + " = " + value + "\r\n";
			text += "\r\n";
		}
		return Util::FileHelpers::WriteFileAtomically(a_path, text, "preset overlay");
	}

	std::optional<float> ParseFloat(std::string_view a_text)
	{
		a_text = TrimView(a_text);
		float value = 0.0f;
		const auto* end = a_text.data() + a_text.size();
		const auto [ptr, ec] = std::from_chars(a_text.data(), end, value);
		if (ec != std::errc() || ptr != end || !std::isfinite(value))
			return std::nullopt;
		return value;
	}

	int ParseFloatList(std::string_view a_text, std::array<float, kMaxComponents>& a_out)
	{
		int count = 0;
		size_t start = 0;
		while (start <= a_text.size()) {
			const auto comma = std::min(a_text.find(',', start), a_text.size());
			const auto value = ParseFloat(a_text.substr(start, comma - start));
			if (!value || count == kMaxComponents)
				return 0;
			a_out[count++] = *value;
			start = comma + 1;
		}
		return count;
	}

	std::vector<std::string> SplitList(std::string_view a_text)
	{
		std::vector<std::string> items;
		size_t start = 0;
		while (start <= a_text.size()) {
			const auto comma = std::min(a_text.find(',', start), a_text.size());
			if (const auto item = TrimView(a_text.substr(start, comma - start)); !item.empty())
				items.emplace_back(item);
			start = comma + 1;
		}
		return items;
	}

	std::string JoinList(const std::vector<std::string>& a_items)
	{
		std::string joined;
		for (const auto& item : a_items)
			joined += (joined.empty() ? "" : ", ") + item;
		return joined;
	}

	std::optional<Op> ParseOp(const IniSection& a_section, std::string& a_error)
	{
		Op op;
		op.fileName = ReadString(&a_section, "FileName");
		op.category = ReadString(&a_section, "Category");
		op.name = ReadString(&a_section, "Name");
		const std::string operationText = ReadString(&a_section, "Operation");
		const std::string_view operation = operationText;
		if (op.category.empty() || op.name.empty() || operation.empty()) {
			a_error = "missing Category, Name or Operation";
			return std::nullopt;
		}

		const char symbol = operation.front();
		if (symbol != '+' && symbol != '=' && symbol != '*') {
			a_error = std::format("unknown operator '{}'", symbol);
			return std::nullopt;
		}
		op.op = static_cast<Operator>(symbol);

		const auto operand = TrimView(operation.substr(1));
		if (Util::IEquals(operand, "true") || Util::IEquals(operand, "false")) {
			if (op.op != Operator::Set) {
				a_error = "only = works on a true/false value";
				return std::nullopt;
			}
			op.operand.kind = ValueKind::Bool;
			op.operand.values[0] = Util::IEquals(operand, "true") ? 1.0f : 0.0f;
		} else if (const int count = ParseFloatList(operand, op.operand.values); count > 0) {
			op.operand.components = count;
		} else {
			a_error = std::format("unreadable operand '{}'", operand);
			return std::nullopt;
		}
		op.raw = a_section;
		return op;
	}

	std::string FormatOperation(const Op& a_op)
	{
		const std::string operand = a_op.operand.kind == ValueKind::Bool ? (a_op.operand.values[0] != 0.0f ? "true" : "false") :
		                                                                   Effects11Util::Ini::FormatFloatList({ a_op.operand.values.data(), static_cast<size_t>(a_op.operand.components) });
		return std::format("\"{} {}\"", static_cast<char>(a_op.op), operand);
	}

	Overlay LoadOverlay(const std::filesystem::path& a_path)
	{
		Overlay overlay;
		overlay.fileName = Util::PathHelpers::ToUtf8(a_path.filename());
		auto file = ReadIni(a_path);
		if (!file) {
			overlay.error = "could not read the file";
			return overlay;
		}

		const auto* info = file->Find(kInfoSection);
		overlay.uiName = ReadString(info, "UIName");
		overlay.uiGroups = ReadString(info, "UIGroups");
		overlay.uiDesc = UnescapeNewlines(ReadString(info, "UIDesc"));
		overlay.uiOrdering = static_cast<int>(ReadFloat(info, "UIOrdering", 0.0f));
		overlay.incompatible = SplitList(ReadString(info, "IncompatibleOverlays"));
		overlay.amountMin = ReadFloat(info, "AmountMin", overlay.amountMin);
		overlay.amountMax = ReadFloat(info, "AmountMax", overlay.amountMax);
		overlay.amountStep = ReadFloat(info, "AmountStep", overlay.amountStep);
		if (overlay.amountMin > overlay.amountMax)
			std::swap(overlay.amountMin, overlay.amountMax);
		overlay.amount = std::clamp(ReadFloat(file->Find(kSettingsSection), "Amount", overlay.amountMax), overlay.amountMin, overlay.amountMax);

		for (const auto& section : file->sections) {
			if (!StartsWithNoCase(section.name, kParamPrefix))
				continue;
			std::string error;
			if (auto op = ParseOp(section, error)) {
				overlay.ops.push_back(std::move(*op));
			} else {
				logger::warn("[Overlays] {} [{}] skipped: {}", overlay.fileName, section.name, error);
				overlay.unparsedOps.push_back(section);
			}
		}
		overlay.file = std::move(*file);
		return overlay;
	}

	bool SaveOverlay(const std::filesystem::path& a_path, const Overlay& a_overlay)
	{
		IniFile file = a_overlay.file;
		if (auto& info = file.GetOrAdd(kInfoSection); info.entries.empty())
			FillNewInfo(info, a_overlay);
		if (a_overlay.HasAmountSlider())
			file.GetOrAdd(kSettingsSection).Set("Amount", Effects11Util::Ini::FormatFloat(a_overlay.amount));

		std::erase_if(file.sections, [](const IniSection& section) { return StartsWithNoCase(section.name, kParamPrefix); });
		std::vector<IniSection> paramSections;
		for (const auto& op : a_overlay.ops)
			paramSections.push_back(op.raw ? *op.raw : FormatOpSection(op));
		paramSections.insert(paramSections.end(), a_overlay.unparsedOps.begin(), a_overlay.unparsedOps.end());
		for (size_t i = 0; i < paramSections.size(); ++i) {
			paramSections[i].name = std::format("{}{}", kParamPrefix, i + 1);
			file.sections.push_back(std::move(paramSections[i]));
		}
		return WriteIni(a_path, file);
	}

	OverlayList LoadList(const std::filesystem::path& a_seriesPath)
	{
		OverlayList list;
		if (auto file = ReadIni(a_seriesPath / kListFileName)) {
			list.active = SplitList(ReadString(file->Find(kListSection), "Active"));
			list.file = std::move(*file);
		}
		return list;
	}

	bool SaveList(const std::filesystem::path& a_seriesPath, OverlayList& a_list)
	{
		a_list.file.GetOrAdd(kListSection).Set("Active", JoinList(a_list.active));
		if (!a_list.file.Find(kBlacklistSection)) {
			auto& blacklist = a_list.file.GetOrAdd(kBlacklistSection);
			blacklist.Set("FileName", "enblocal.ini");
			blacklist.Set("Category", "GUI");
		}
		return WriteIni(a_seriesPath / kListFileName, a_list.file);
	}

	bool MatchesPattern(std::string_view a_pattern, std::string_view a_text)
	{
		size_t patternPos = 0, textPos = 0, starPattern = std::string_view::npos, starText = 0;
		const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
		while (textPos < a_text.size()) {
			if (patternPos < a_pattern.size() && (a_pattern[patternPos] == '?' || lower(a_pattern[patternPos]) == lower(a_text[textPos]))) {
				++patternPos;
				++textPos;
			} else if (patternPos < a_pattern.size() && a_pattern[patternPos] == '*') {
				starPattern = patternPos++;
				starText = textPos;
			} else if (starPattern != std::string_view::npos) {
				// Let the last * swallow one more character and retry
				patternPos = starPattern + 1;
				textPos = ++starText;
			} else {
				return false;
			}
		}
		while (patternPos < a_pattern.size() && a_pattern[patternPos] == '*')
			++patternPos;
		return patternPos == a_pattern.size();
	}

	bool HasWildcard(std::string_view a_text)
	{
		return a_text.find_first_of("*?") != std::string_view::npos;
	}

	bool IsCompatible(const Op& a_op, const OverlayValue& a_target)
	{
		if (a_op.operand.kind == ValueKind::Bool && a_target.kind != ValueKind::Bool)
			return false;
		if (a_target.kind == ValueKind::Bool)
			return a_op.op == Operator::Set;
		return a_op.operand.components == 1 || a_op.operand.components == a_target.components;
	}

	OverlayValue ApplyOperation(const OverlayValue& a_before, const Op& a_op, float a_amount)
	{
		const auto operandAt = [&](int c) { return a_op.operand.values[a_op.operand.components == 1 ? 0 : c]; };
		const auto result = [&](float current, int c) {
			switch (a_op.op) {
			case Operator::Add:
				return current + operandAt(c);
			case Operator::Multiply:
				return current * operandAt(c);
			default:
				return operandAt(c);
			}
		};

		OverlayValue after = a_before;
		if (a_before.kind != ValueKind::Float) {
			if (a_amount > 0.0f) {
				const float value = result(a_before.values[0], 0);
				after.values[0] = a_before.kind == ValueKind::Bool ? (value != 0.0f ? 1.0f : 0.0f) : std::round(value);
			}
			return after;
		}
		for (int c = 0; c < a_before.components; ++c)
			after.values[c] = a_before.values[c] + a_amount * (result(a_before.values[c], c) - a_before.values[c]);
		return after;
	}
}

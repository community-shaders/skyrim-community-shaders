#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/** @brief ENB Extender preset overlay files: format, parsing, writing and the maths of one operation. */
namespace Overlays
{
	inline constexpr const char* kFolderName = "enbpresetoverlays";
	inline constexpr const char* kListFileName = "enbpresetoverlays.ini";
	inline constexpr int kMaxComponents = 4;

	/** @brief One ini section with its entries in file order; key lookup ignores case. */
	struct IniSection
	{
		std::string name;
		std::vector<std::pair<std::string, std::string>> entries;

		/** @brief The value of the first entry with this key, or null. */
		const std::string* Find(std::string_view a_key) const;
		/** @brief Replaces the first entry with this key, or appends one. */
		void Set(std::string_view a_key, std::string a_value);
	};

	/** @brief An ini file as ordered sections; lines before the first section are dropped. */
	struct IniFile
	{
		std::vector<IniSection> sections;

		/** @brief The first section with this name, or null. */
		const IniSection* Find(std::string_view a_name) const;
		/** @brief The first section with this name, appending an empty one when missing. */
		IniSection& GetOrAdd(std::string_view a_name);
	};

	/** @brief Reads UTF-8 ini text, skipping NUL bytes (seen in Extender-written list files) and a BOM. @return Null when unreadable. */
	std::optional<IniFile> ReadIni(const std::filesystem::path& a_path);
	/** @brief Writes the file with CRLF line endings, replacing it atomically. */
	bool WriteIni(const std::filesystem::path& a_path, const IniFile& a_file);

	enum class Operator : char
	{
		Add = '+',
		Set = '=',
		Multiply = '*'
	};

	enum class ValueKind : uint8_t
	{
		Float,
		Int,
		Bool
	};

	/** @brief A value of 1 to 4 floats; ints and bools use values[0], bools as 0 or 1. */
	struct OverlayValue
	{
		std::array<float, kMaxComponents> values{};
		int components = 1;
		ValueKind kind = ValueKind::Float;

		bool operator==(const OverlayValue&) const = default;
	};

	/** @brief One [OVERLAYPARAMn] entry. */
	struct Op
	{
		std::string fileName;  ///< Empty when the FileName key is absent
		std::string category;
		std::string name;
		Operator op = Operator::Add;
		OverlayValue operand;           ///< kind Bool for true/false, else Float with one component per number
		std::optional<IniSection> raw;  ///< The section as read, so an unchanged op is written back verbatim
	};

	/** @brief One overlay file: UI info, Amount range and value, and its ops. */
	struct Overlay
	{
		std::string fileName;  ///< e.g. "DarkDungeons.ini"
		IniFile file;          ///< As read; sections other than the params are written back unchanged
		std::string uiName;
		std::string uiGroups;
		std::string uiDesc;
		int uiOrdering = 0;
		std::vector<std::string> incompatible;
		float amountMin = 1.0f;
		float amountMax = 1.0f;
		float amountStep = 0.01f;
		float amount = 1.0f;
		std::vector<Op> ops;
		std::vector<IniSection> unparsedOps;  ///< [OVERLAYPARAMn] sections that failed to parse, written back unchanged
		std::string error;                    ///< Set when the file could not be read; such an overlay cannot be enabled
		bool dirty = false;

		std::string DisplayName() const { return uiName.empty() ? fileName : uiName; }
		bool HasAmountSlider() const { return amountMin != amountMax; }
	};

	/** @brief The list file: active overlays in apply order, with every other section kept verbatim. */
	struct OverlayList
	{
		IniFile file;
		std::vector<std::string> active;
	};

	/** @brief Parses a float, ignoring surrounding spaces. */
	std::optional<float> ParseFloat(std::string_view a_text);
	/** @brief Parses up to kMaxComponents comma-separated floats. @return How many parsed, 0 on any malformed part. */
	int ParseFloatList(std::string_view a_text, std::array<float, kMaxComponents>& a_out);
	/** @brief Splits a comma-separated list, trimming each item and dropping empty ones. */
	std::vector<std::string> SplitList(std::string_view a_text);
	/** @brief Joins items with ", ", the inverse of SplitList. */
	std::string JoinList(const std::vector<std::string>& a_items);

	/** @brief Parses an [OVERLAYPARAMn] section. @param a_error Receives why it was rejected. */
	std::optional<Op> ParseOp(const IniSection& a_section, std::string& a_error);
	/** @brief The quoted Operation value, e.g. "\"+ -0.3\"". */
	std::string FormatOperation(const Op& a_op);

	/** @brief Reads one overlay file; failures are reported in Overlay::error. */
	Overlay LoadOverlay(const std::filesystem::path& a_path);
	/** @brief Writes an overlay file, keeping its other sections and unparsed ops; info is filled only for a new file. */
	bool SaveOverlay(const std::filesystem::path& a_path, const Overlay& a_overlay);

	/** @brief Reads enbpresetoverlays.ini from the enbseries folder; a missing file gives an empty list. */
	OverlayList LoadList(const std::filesystem::path& a_seriesPath);
	/** @brief Writes Active into a_list's file, adding the [PRESETOVERLAYS] and default blacklist sections to a_list when missing. */
	bool SaveList(const std::filesystem::path& a_seriesPath, OverlayList& a_list);

	/** @brief Case-insensitive match with * and ? wildcards. */
	bool MatchesPattern(std::string_view a_pattern, std::string_view a_text);
	/** @brief Whether the text contains a * or ? wildcard. */
	bool HasWildcard(std::string_view a_text);

	/** @brief Whether the op can change a value of this kind and size. */
	bool IsCompatible(const Op& a_op, const OverlayValue& a_target);
	/** @brief One op at the given Amount: floats blend toward the result, ints and bools take it whole when Amount > 0. */
	OverlayValue ApplyOperation(const OverlayValue& a_before, const Op& a_op, float a_amount);
}

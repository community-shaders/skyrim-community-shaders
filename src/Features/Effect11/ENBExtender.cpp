#include "ENBExtender.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

#include "EffectManager.h"

namespace ENBExtender
{
	// ── Shared helpers ────────────────���─────────────────────────────────

	static bool IsTruthy(const std::string& s)
	{
		return !s.empty() && s != "0" && s != "false";
	}

	static std::string BuildGroupPath(const std::vector<std::string>& stack)
	{
		std::string path;
		for (size_t i = 0; i < stack.size(); ++i) {
			if (i > 0)
				path += ".";
			path += stack[i];
		}
		return path;
	}

	static std::string ResolveGroup(const std::string& varName, const std::string& explicitGroup,
		const std::vector<std::string>& groupStack, const Effect& effect)
	{
		if (!explicitGroup.empty())
			return explicitGroup;
		if (!groupStack.empty())
			return BuildGroupPath(groupStack);
		auto it = effect.sourceGroupMap.find(varName);
		if (it != effect.sourceGroupMap.end())
			return it->second;
		return {};
	}

	static int GetSourceOrder(const std::string& varName, const Effect& effect)
	{
		auto it = effect.sourceOrderMap.find(varName);
		return (it != effect.sourceOrderMap.end()) ? it->second : INT_MAX;
	}

	static Effect::UIVariable MakeSeparator(const std::string& varName,
		const std::vector<std::string>& groupStack, const Effect& effect)
	{
		Effect::UIVariable sep = {};
		sep.isSeparator = true;
		sep.name = varName;
		sep.group = ResolveGroup(varName, {}, groupStack, effect);
		sep.sourceOrder = GetSourceOrder(varName, effect);
		return sep;
	}

	int SafeStoi(const std::string& s, int fallback)
	{
		try {
			return std::stoi(s);
		} catch (...) {
			if (!s.empty())
				logger::warn("[ENBExtender] Failed to parse int from '{}'", s);
			return fallback;
		}
	}

	float SafeStof(const std::string& s, float fallback)
	{
		try {
			return std::stof(s);
		} catch (...) {
			if (!s.empty())
				logger::warn("[ENBExtender] Failed to parse float from '{}'", s);
			return fallback;
		}
	}

	// ── KIEFX encoding ─────────────────────────────────────────────────

	static constexpr uint8_t kiefxMagic[] = { 0x4B, 0x49, 0x45, 0x46, 0x58, 0x00, 0x01 };
	static constexpr uint8_t kiefxKey[] = { 0xD8, 0x29, 0x09, 0x12, 0x64, 0x96, 0x6E, 0x2C };

	bool IsKIEFX(const std::string& content)
	{
		return content.size() >= sizeof(kiefxMagic) &&
		       memcmp(content.data(), kiefxMagic, sizeof(kiefxMagic)) == 0;
	}

	std::string DecodeKIEFX(const std::string& content)
	{
		if (!IsKIEFX(content))
			return content;

		std::string decoded;
		decoded.reserve(content.size() - sizeof(kiefxMagic));
		for (size_t i = sizeof(kiefxMagic); i < content.size(); ++i)
			decoded += static_cast<char>(static_cast<uint8_t>(content[i]) ^ kiefxKey[(i - sizeof(kiefxMagic)) % sizeof(kiefxKey)]);
		return decoded;
	}

	// ── Text parsing helpers ────────────────��───────────────────────────

	static size_t FindMatchingBrace(const std::string& text, size_t openPos)
	{
		int depth = 1;
		bool inLineComment = false;
		bool inBlockComment = false;
		bool inString = false;

		for (size_t i = openPos + 1; i < text.size(); ++i) {
			char c = text[i];
			char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

			if (inLineComment) {
				if (c == '\n')
					inLineComment = false;
				continue;
			}
			if (inBlockComment) {
				if (c == '*' && next == '/') {
					inBlockComment = false;
					++i;
				}
				continue;
			}
			if (inString) {
				if (c == '\\')
					++i;
				else if (c == '"')
					inString = false;
				continue;
			}

			if (c == '/' && next == '/') {
				inLineComment = true;
				++i;
				continue;
			}
			if (c == '/' && next == '*') {
				inBlockComment = true;
				++i;
				continue;
			}
			if (c == '"') {
				inString = true;
				continue;
			}

			if (c == '{')
				++depth;
			else if (c == '}') {
				if (--depth == 0)
					return i;
			}
		}
		return std::string::npos;
	}

	static size_t FindMatchingAngleBracket(const std::string& text, size_t openPos)
	{
		int depth = 1;
		for (size_t i = openPos + 1; i < text.size(); ++i) {
			if (text[i] == '<')
				++depth;
			else if (text[i] == '>') {
				if (--depth == 0)
					return i;
			}
		}
		return std::string::npos;
	}

	static std::string ExtractAnnotation(const std::string& annotations, const std::string& name)
	{
		size_t pos = 0;
		while (true) {
			pos = annotations.find(name, pos);
			if (pos == std::string::npos)
				return {};

			if (pos > 0 && (std::isalnum(static_cast<unsigned char>(annotations[pos - 1])) || annotations[pos - 1] == '_')) {
				pos += name.size();
				continue;
			}

			size_t eq = annotations.find('=', pos + name.size());
			if (eq == std::string::npos)
				return {};

			size_t valueStart = annotations.find_first_not_of(" \t", eq + 1);
			if (valueStart == std::string::npos)
				return {};

			if (annotations[valueStart] == '"') {
				size_t quoteEnd = annotations.find('"', valueStart + 1);
				if (quoteEnd == std::string::npos)
					return {};
				return annotations.substr(valueStart + 1, quoteEnd - valueStart - 1);
			}

			size_t valueEnd = annotations.find_first_of(";>", valueStart);
			std::string val = (valueEnd != std::string::npos)
			                      ? annotations.substr(valueStart, valueEnd - valueStart)
			                      : annotations.substr(valueStart);
			val.erase(val.find_last_not_of(" \t") + 1);
			return val;
		}
	}

	// ── ConvertFxGroups ─────────────────────────────────────────────────

	static bool IsInsidePreprocessorDirective(const std::string& content, size_t pos)
	{
		size_t lineStart = content.rfind('\n', pos);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		size_t firstChar = content.find_first_not_of(" \t", lineStart);
		if (firstChar != std::string::npos && content[firstChar] == '#')
			return true;

		if (lineStart > 1) {
			size_t checkPos = lineStart - 1;
			if (checkPos > 0 && content[checkPos - 1] == '\r')
				checkPos--;
			if (checkPos > 0 && content[checkPos - 1] == '\\')
				return true;
		}
		return false;
	}

	static bool IsWordBoundary(const std::string& text, size_t pos)
	{
		return pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
	}

	struct ParsedAnnotationBlock
	{
		std::string content;
		size_t closePos = std::string::npos;
	};

	static ParsedAnnotationBlock ParseAngleBracketBlock(const std::string& text, size_t searchFrom)
	{
		size_t open = text.find_first_not_of(" \t\r\n", searchFrom);
		if (open == std::string::npos || text[open] != '<')
			return {};
		size_t close = FindMatchingAngleBracket(text, open);
		if (close == std::string::npos)
			return {};
		return { text.substr(open + 1, close - open - 1), close };
	}

	struct ParsedBlock
	{
		std::string annotations;
		std::string body;
		size_t endPos = std::string::npos;
	};

	static ParsedBlock ParseKeywordBlock(const std::string& text, size_t afterKeyword)
	{
		size_t pos = text.find_first_not_of(" \t\r\n", afterKeyword);
		if (pos == std::string::npos)
			return {};

		ParsedBlock result;

		// Skip optional name (identifier before < or {)
		if (text[pos] != '<' && text[pos] != '{') {
			pos = text.find_first_of("<{", pos);
			if (pos == std::string::npos)
				return {};
		}

		// Parse optional <annotations>
		if (text[pos] == '<') {
			auto anno = ParseAngleBracketBlock(text, pos);
			if (anno.closePos == std::string::npos)
				return {};
			result.annotations = anno.content;
			pos = text.find_first_not_of(" \t\r\n", anno.closePos + 1);
			if (pos == std::string::npos)
				return {};
		}

		// Parse { body }
		if (text[pos] != '{')
			pos = text.find('{', pos);
		if (pos == std::string::npos)
			return {};
		size_t closePos = FindMatchingBrace(text, pos);
		if (closePos == std::string::npos)
			return {};

		result.body = text.substr(pos, closePos - pos + 1);
		result.endPos = closePos;
		return result;
	}

	void ConvertFxGroups(std::string& content)
	{
		int convertedCount = 0;
		size_t searchStart = 0;

		while (true) {
			size_t fxPos = content.find("fxgroup", searchStart);
			if (fxPos == std::string::npos)
				break;

			if (!IsWordBoundary(content, fxPos) || IsInsidePreprocessorDirective(content, fxPos)) {
				searchStart = fxPos + 7;
				continue;
			}

			// Parse the fxgroup: name <annotations> { body }
			size_t nameStart = content.find_first_not_of(" \t", fxPos + 7);
			if (nameStart == std::string::npos) {
				searchStart = fxPos + 7;
				continue;
			}
			size_t nameEnd = content.find_first_of(" \t<{", nameStart);
			if (nameEnd == std::string::npos) {
				searchStart = fxPos + 7;
				continue;
			}
			std::string groupName = content.substr(nameStart, nameEnd - nameStart);

			std::string groupAnnotations;
			size_t bodySearchFrom = nameEnd;
			auto groupAnno = ParseAngleBracketBlock(content, nameEnd);
			if (groupAnno.closePos != std::string::npos) {
				groupAnnotations = groupAnno.content;
				bodySearchFrom = groupAnno.closePos + 1;
			}

			size_t bodyOpen = content.find('{', bodySearchFrom);
			if (bodyOpen == std::string::npos) {
				searchStart = fxPos + 7;
				continue;
			}
			size_t bodyClose = FindMatchingBrace(content, bodyOpen);
			if (bodyClose == std::string::npos) {
				searchStart = fxPos + 7;
				continue;
			}

			std::string body = content.substr(bodyOpen + 1, bodyClose - bodyOpen - 1);

			// Extract technique11 blocks from the fxgroup body
			std::vector<ParsedBlock> techniques;
			size_t techSearch = 0;
			while (true) {
				size_t techPos = body.find("technique11", techSearch);
				if (techPos == std::string::npos)
					break;
				if (!IsWordBoundary(body, techPos)) {
					techSearch = techPos + 11;
					continue;
				}

				auto parsed = ParseKeywordBlock(body, techPos + 11);
				if (parsed.endPos == std::string::npos) {
					techSearch = techPos + 11;
					continue;
				}
				techniques.push_back(std::move(parsed));
				techSearch = techniques.back().endPos + 1;
			}

			// Emit flattened technique11 declarations
			std::string replacement;
			for (size_t i = 0; i < techniques.size(); ++i) {
				std::string techName = groupName;
				if (i > 0)
					techName += std::to_string(i);

				replacement += "technique11 " + techName;

				// First technique merges group + technique annotations
				std::string anno;
				if (i == 0) {
					if (!groupAnnotations.empty())
						anno = groupAnnotations;
					if (!techniques[i].annotations.empty()) {
						if (!anno.empty())
							anno += " ";
						anno += techniques[i].annotations;
					}
				} else {
					anno = techniques[i].annotations;
				}
				if (!anno.empty())
					replacement += " <" + anno + ">";

				replacement += " " + techniques[i].body + "\n";
			}

			convertedCount++;
			content.replace(fxPos, bodyClose - fxPos + 1, replacement);
			searchStart = fxPos + replacement.size();
		}

		if (convertedCount > 0)
			logger::debug("[ENBExtender] Converted {} fxgroup(s)", convertedCount);
	}

	// ── ConvertExtenderSyntax ─────────────────���─────────────────────────

	static std::string NormalizeBoolString(const std::string& val)
	{
		if (val == "false")
			return "0";
		if (val == "true")
			return "1";
		return val;
	}

	static bool HandleErrorDirective(const std::string& line, std::string& result)
	{
		size_t pos = line.find_first_not_of(" \t#");
		if (pos == std::string::npos || line.compare(pos, 5, "error") != 0)
			return false;
		size_t hashPos = line.find('#');
		if (hashPos == std::string::npos || hashPos >= pos)
			return false;
		result += "\n";
		return true;
	}

	static bool HandlePragmaExists(const std::string& line, const std::filesystem::path& enbseriesPath, std::string& result)
	{
		size_t pragmaPos = line.find("#pragma");
		if (pragmaPos == std::string::npos)
			return false;
		size_t existsPos = line.find("exists", pragmaPos + 7);
		if (existsPos == std::string::npos)
			return false;

		size_t openParen = line.find('(', existsPos);
		size_t closeParen = line.rfind(')');
		if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen)
			return false;

		std::string args = line.substr(openParen + 1, closeParen - openParen - 1);
		size_t q1 = args.find('"');
		size_t q2 = (q1 != std::string::npos) ? args.find('"', q1 + 1) : std::string::npos;
		size_t comma = (q2 != std::string::npos) ? args.find(',', q2 + 1) : std::string::npos;
		if (q1 == std::string::npos || q2 == std::string::npos || comma == std::string::npos)
			return false;

		std::string filePath = args.substr(q1 + 1, q2 - q1 - 1);
		std::string defineName = args.substr(comma + 1);
		defineName.erase(0, defineName.find_first_not_of(" \t"));
		defineName.erase(defineName.find_last_not_of(" \t") + 1);

		bool exists = std::filesystem::exists(enbseriesPath / filePath);
		result += "#define " + defineName + (exists ? " 1" : " 0") + "\n";
		return true;
	}

	static bool HandlePragmaUIDefine(std::string& line, std::istringstream& stream,
		const std::string& iniPath, const std::string& iniSection, std::string& result, std::vector<Effect::UIDefineInfo>& uiDefines)
	{
		size_t pragmaPos = line.find("pragma");
		if (pragmaPos == std::string::npos)
			return false;
		size_t uidefPos = line.find("uidefine", pragmaPos + 6);
		if (uidefPos == std::string::npos)
			return false;

		while (!line.empty()) {
			auto trailing = line.find_last_not_of(" \t\r");
			if (trailing != std::string::npos && line[trailing] == '\\') {
				line.erase(trailing);
				std::string nextLine;
				if (std::getline(stream, nextLine))
					line += nextLine;
				else
					break;
			} else {
				break;
			}
		}

		size_t openParen = line.find('(', uidefPos);
		size_t closeParen = line.rfind(')');
		if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen)
			return false;

		std::string inner = line.substr(openParen + 1, closeParen - openParen - 1);

		size_t typeEnd = inner.find_first_of(" \t");
		if (typeEnd == std::string::npos)
			return false;
		std::string typeName = inner.substr(0, typeEnd);
		typeName.erase(0, typeName.find_first_not_of(" \t"));

		size_t nameStart = inner.find_first_not_of(" \t", typeEnd);
		if (nameStart == std::string::npos)
			return false;
		size_t nameEnd = inner.find_first_of("<=", nameStart);
		std::string defineName = (nameEnd != std::string::npos)
		                             ? inner.substr(nameStart, nameEnd - nameStart)
		                             : inner.substr(nameStart);
		defineName.erase(defineName.find_last_not_of(" \t") + 1);

		std::string annotations;
		size_t angleOpen = inner.find('<', nameStart);
		size_t angleClose = inner.rfind('>');
		if (angleOpen != std::string::npos && angleClose != std::string::npos && angleClose > angleOpen)
			annotations = inner.substr(angleOpen + 1, angleClose - angleOpen - 1);

		size_t equalsPos = (angleClose != std::string::npos) ? inner.find('=', angleClose) : inner.rfind('=');
		std::string defaultVal = "0";
		if (equalsPos != std::string::npos) {
			defaultVal = inner.substr(equalsPos + 1);
			defaultVal.erase(0, defaultVal.find_first_not_of(" \t"));
			defaultVal.erase(defaultVal.find_last_not_of(" \t;") + 1);
			defaultVal = NormalizeBoolString(defaultVal);
		}

		std::string uiName = ExtractAnnotation(annotations, "UIName");
		std::string uiGroup = ExtractAnnotation(annotations, "UIGroup");

		std::string finalVal = defaultVal;
		if (!iniPath.empty() && !iniSection.empty() && !uiName.empty()) {
			std::string iniKey = uiGroup.empty() ? uiName : (uiGroup + "." + uiName);
			std::vector<char> valueBuffer(1024);
			DWORD iniResult = GetPrivateProfileStringA(iniSection.c_str(), iniKey.c_str(), "", valueBuffer.data(), 1024, iniPath.c_str());
			if (iniResult > 0) {
				std::string iniVal(valueBuffer.data());
				iniVal.erase(0, iniVal.find_first_not_of(" \t"));
				iniVal.erase(iniVal.find_last_not_of(" \t") + 1);
				finalVal = NormalizeBoolString(iniVal);
			}
		}

		if (!uiName.empty()) {
			Effect::UIDefineInfo info;
			info.defineName = defineName;
			info.displayName = uiName;
			info.group = uiGroup;
			info.type = typeName;
			info.value = finalVal;
			info.widget = ExtractAnnotation(annotations, "UIWidget");
			info.list = ExtractAnnotation(annotations, "UIList");

			std::string minStr = ExtractAnnotation(annotations, "UIMin");
			std::string maxStr = ExtractAnnotation(annotations, "UIMax");
			std::string stepStr = ExtractAnnotation(annotations, "UIStep");
			std::string orderStr = ExtractAnnotation(annotations, "UIOrdering");

			if (!minStr.empty()) {
				if (typeName == "int")
					info.intMin = SafeStoi(minStr, info.intMin);
				else
					info.floatMin = SafeStof(minStr, info.floatMin);
			}
			if (!maxStr.empty()) {
				if (typeName == "int")
					info.intMax = SafeStoi(maxStr, info.intMax);
				else
					info.floatMax = SafeStof(maxStr, info.floatMax);
			}
			if (!stepStr.empty())
				info.floatStep = SafeStof(stepStr, info.floatStep);
			if (!orderStr.empty())
				info.ordering = SafeStoi(orderStr, info.ordering);

			uiDefines.push_back(std::move(info));
		}

		result += "#define " + defineName + " " + finalVal + "\n";
		return true;
	}

	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, std::vector<Effect::UIDefineInfo>& uiDefines, const std::string& iniPath, const std::string& iniSection)
	{
		std::string result;
		result.reserve(content.size());

		std::istringstream stream(content);
		std::string line;

		while (std::getline(stream, line)) {
			if (HandleErrorDirective(line, result))
				continue;
			if (HandlePragmaExists(line, enbseriesPath, result))
				continue;
			if (HandlePragmaUIDefine(line, stream, iniPath, iniSection, result, uiDefines))
				continue;
			result += line + "\n";
		}

		content = std::move(result);
	}

	// ── ParseSourceGroupScopes ──────────────────────────────────────────

	void ParseSourceGroupScopes(const std::string& preprocessedSource, Effect& effect)
	{
		effect.sourceGroupMap.clear();
		effect.sourceOrderMap.clear();
		std::vector<std::string> groupStack;
		int declarationIndex = 0;

		std::istringstream stream(preprocessedSource);
		std::string line;

		while (std::getline(stream, line)) {
			size_t firstNonSpace = line.find_first_not_of(" \t");
			if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
				continue;

			if (line.find("UIGroupBegin") != std::string::npos) {
				std::string groupName = ExtractAnnotation(line, "UIGroup");
				if (groupName.empty()) {
					size_t angleClose = line.rfind('>');
					if (angleClose != std::string::npos) {
						size_t q1 = line.find('"', angleClose);
						size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
						if (q1 != std::string::npos && q2 != std::string::npos)
							groupName = line.substr(q1 + 1, q2 - q1 - 1);
					}
				}

				if (!groupName.empty())
					groupStack.push_back(groupName);
				continue;
			}

			if (line.find("UIGroupEnd") != std::string::npos) {
				if (!groupStack.empty())
					groupStack.pop_back();
				continue;
			}

			std::string trimmed = line.substr(firstNonSpace);
			size_t spacePos = trimmed.find_first_of(" \t");
			if (spacePos == std::string::npos)
				continue;
			std::string typePart = trimmed.substr(0, spacePos);
			if (typePart != "float" && typePart != "float2" && typePart != "float3" && typePart != "float4" &&
				typePart != "int" && typePart != "bool" && typePart != "string")
				continue;

			size_t nameStart = trimmed.find_first_not_of(" \t", spacePos);
			if (nameStart == std::string::npos)
				continue;
			size_t nameEnd = nameStart;
			while (nameEnd < trimmed.size() && (std::isalnum(static_cast<unsigned char>(trimmed[nameEnd])) || trimmed[nameEnd] == '_'))
				nameEnd++;

			if (nameEnd > nameStart) {
				std::string varName = trimmed.substr(nameStart, nameEnd - nameStart);
				if (varName.find("UIGroupBegin") == std::string::npos && varName.find("UIGroupEnd") == std::string::npos) {
					effect.sourceOrderMap[varName] = declarationIndex++;
					if (!groupStack.empty())
						effect.sourceGroupMap[varName] = BuildGroupPath(groupStack);
				}
			}
		}
	}

	// ── ProcessExtenderStringVariable ────────────────────────────────────

	static void TrackGroupMetadata(const std::string& groupPath, ID3DX11EffectVariable* variable, Effect& effect)
	{
		if (groupPath.empty() || effect.groupOrdering.find(groupPath) != effect.groupOrdering.end())
			return;

		effect.groupOrdering[groupPath] = 0;
		std::string displayName = effect.GetUIAnnotation(variable, "UIGroupName");
		if (!displayName.empty())
			effect.groupDisplayNames[groupPath] = displayName;
		std::string openStr = effect.GetUIAnnotation(variable, "UIGroupOpen");
		if (!openStr.empty())
			effect.groupDefaultOpen[groupPath] = IsTruthy(openStr);
		std::string orderStr = effect.GetUIAnnotation(variable, "UIOrdering");
		if (!orderStr.empty())
			effect.groupOrdering[groupPath] = SafeStoi(orderStr);
	}

	static bool HandleGroupBegin(ID3DX11EffectVariable* variable, std::vector<std::string>& groupStack, Effect& effect)
	{
		if (!IsTruthy(effect.GetUIAnnotation(variable, "UIGroupBegin")))
			return false;

		std::string groupName = effect.GetUIAnnotation(variable, "UIGroup");
		if (groupName.empty()) {
			auto strVar = variable->AsString();
			if (strVar && strVar->IsValid()) {
				LPCSTR val = nullptr;
				if (SUCCEEDED(strVar->GetString(&val)) && val)
					groupName = val;
			}
		}
		if (groupName.empty())
			return true;

		groupStack.push_back(groupName);
		TrackGroupMetadata(BuildGroupPath(groupStack), variable, effect);
		return true;
	}

	static bool HandleGroupEnd(ID3DX11EffectVariable* variable, std::vector<std::string>& groupStack, Effect& effect)
	{
		if (!IsTruthy(effect.GetUIAnnotation(variable, "UIGroupEnd")))
			return false;
		if (!groupStack.empty())
			groupStack.pop_back();
		return true;
	}

	static bool HandleSeparatorAnnotation(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		if (!IsTruthy(effect.GetUIAnnotation(variable, "UISeparator")))
			return false;

		auto sep = MakeSeparator(varDesc.Name, groupStack, effect);
		std::string explicitGroup = effect.GetUIAnnotation(variable, "UIGroup");
		if (!explicitGroup.empty())
			sep.group = explicitGroup;
		effect.uiVariables.push_back(sep);
		return true;
	}

	static bool HandleStringLabel(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		std::string labelText = effect.GetUIAnnotation(variable, "UIName");
		if (labelText.empty()) {
			auto strVar = variable->AsString();
			if (strVar && strVar->IsValid()) {
				LPCSTR val = nullptr;
				if (SUCCEEDED(strVar->GetString(&val)) && val && val[0] != '\0')
					labelText = val;
			}
		}

		if (labelText.empty())
			return false;

		if (labelText == "|") {
			effect.uiVariables.push_back(MakeSeparator(varDesc.Name, groupStack, effect));
			return true;
		}

		Effect::UIVariable labelVar = {};
		labelVar.name = varDesc.Name;
		labelVar.displayName = labelText;
		labelVar.type = Effect::UIVariableType::Float;
		labelVar.floatMin = 0.0f;
		labelVar.floatMax = 0.0f;
		labelVar.isReadOnly = true;
		ApplyExtenderAnnotations(labelVar, variable, groupStack, effect);

		if (labelVar.isHidden)
			return true;

		effect.uiVariables.push_back(labelVar);
		return true;
	}

	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc,
		std::vector<std::string>& groupStack, Effect& effect)
	{
		if (HandleGroupBegin(variable, groupStack, effect))
			return true;
		if (HandleGroupEnd(variable, groupStack, effect))
			return true;
		if (HandleSeparatorAnnotation(variable, varDesc, groupStack, effect))
			return true;
		HandleStringLabel(variable, varDesc, groupStack, effect);
		return true;
	}

	// ── ApplyExtenderAnnotations ───────────────��────────────────────────

	void ApplyExtenderAnnotations(Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		uiVar.group = ResolveGroup(uiVar.name, effect.GetUIAnnotation(variable, "UIGroup"), groupStack, effect);

		std::string orderStr = effect.GetUIAnnotation(variable, "UIOrdering");
		if (!orderStr.empty())
			uiVar.ordering = SafeStoi(orderStr);

		uiVar.sourceOrder = GetSourceOrder(uiVar.name, effect);

		uiVar.isReadOnly = IsTruthy(effect.GetUIAnnotation(variable, "UIReadOnly"));

		std::string hiddenStr = effect.GetUIAnnotation(variable, "UIHidden");
		std::string visibleStr = effect.GetUIAnnotation(variable, "UIVisible");
		uiVar.isHidden = IsTruthy(hiddenStr) || (!visibleStr.empty() && !IsTruthy(visibleStr));

		uiVar.isTopLevel = IsTruthy(effect.GetUIAnnotation(variable, "UITopLevel"));
		if (uiVar.isTopLevel)
			uiVar.group.clear();

		uiVar.uniqueName = effect.GetUIAnnotation(variable, "UniqueName");
		uiVar.uiBinding = effect.GetUIAnnotation(variable, "UIBinding");
		uiVar.uiBindingProperty = effect.GetUIAnnotation(variable, "UIBindingProperty");
		uiVar.uiBindingCondition = effect.GetUIAnnotation(variable, "UIBindingCondition");
		uiVar.separation = effect.GetUIAnnotation(variable, "Separation");

		TrackGroupMetadata(uiVar.group, variable, effect);
	}

	// ── InsertUIDefines ─────────────��─────────────────────��─────────────

	void InsertUIDefines(Effect& effect)
	{
		for (const auto& def : effect.uiDefines) {
			Effect::UIVariable uiVar = {};
			uiVar.name = def.defineName;
			uiVar.displayName = def.displayName;
			uiVar.group = def.group;
			uiVar.ordering = def.ordering;
			uiVar.isReadOnly = true;

			if (def.type == "bool") {
				uiVar.type = Effect::UIVariableType::Bool;
				uiVar.boolValue = (def.value != "0");
			} else if (def.type == "int") {
				uiVar.type = Effect::UIVariableType::Int;
				uiVar.intValue = SafeStoi(def.value);
				uiVar.intMin = def.intMin;
				uiVar.intMax = def.intMax;
				uiVar.widgetType = effect.ParseWidgetType(def.widget);
				if (uiVar.widgetType == Effect::UIWidgetType::Dropdown && !def.list.empty())
					uiVar.dropdownItems = effect.ParseDropdownList(def.list);
			} else {
				uiVar.type = Effect::UIVariableType::Float;
				uiVar.floatValue = SafeStof(def.value);
				uiVar.floatMin = def.floatMin;
				uiVar.floatMax = def.floatMax;
				uiVar.floatStep = def.floatStep;
			}

			effect.uiVariables.push_back(uiVar);
		}

		if (!effect.uiDefines.empty())
			logger::info("[ENBExtender] Inserted {} UI defines", effect.uiDefines.size());
	}

	// ── ParseTimePeriod ────────────────────────────────���────────────────

	void ParseTimePeriod(Effect::UIVariable& uiVar)
	{
		if (uiVar.separation.empty() || uiVar.separation == "None")
			return;

		static const char* periods[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "Interior" };
		for (const auto& period : periods) {
			std::string withIndent = std::string("|- ") + period + " - ";
			std::string plain = std::string(period) + " - ";
			if (uiVar.displayName.compare(0, withIndent.size(), withIndent) == 0 ||
				uiVar.displayName.compare(0, plain.size(), plain) == 0) {
				uiVar.timePeriod = period;
				return;
			}
		}
	}

	// ── RenderMergedEffectsList ─────────────────────────────────────────

	struct VarRef
	{
		Effect* effect;
		int index;
	};

	struct GroupNode
	{
		std::string name;
		std::string fullPath;
		std::vector<VarRef> vars;
		std::vector<std::unique_ptr<GroupNode>> children;
	};

	static std::string ComputeUniqueName(const Effect::UIVariable& var)
	{
		if (!var.uniqueName.empty())
			return var.uniqueName;
		if (!var.group.empty())
			return var.group + "." + var.displayName;
		return var.displayName;
	}

	static float GetBoundValue(const Effect::UIVariable& var)
	{
		switch (var.type) {
		case Effect::UIVariableType::Float:
			return var.floatValue;
		case Effect::UIVariableType::Int:
			return static_cast<float>(var.intValue);
		case Effect::UIVariableType::Bool:
			return var.boolValue ? 1.0f : 0.0f;
		default:
			return 0.0f;
		}
	}

	static bool EvaluateCondition(const std::string& condStr, float boundValue)
	{
		if (condStr.empty())
			return boundValue != 0.0f;

		size_t valueStart = 0;
		if (condStr.size() >= 2 && !std::isdigit(static_cast<unsigned char>(condStr[1])) && condStr[1] != '-')
			valueStart = 2;
		else if (!condStr.empty() && (condStr[0] == '<' || condStr[0] == '>'))
			valueStart = 1;
		else
			return boundValue != 0.0f;

		float comparand = SafeStof(condStr.substr(valueStart));
		char c0 = condStr[0];
		char c1 = (condStr.size() >= 2) ? condStr[1] : '\0';

		if (c0 == '=' && c1 == '=') return boundValue == comparand;
		if (c0 == '!' && c1 == '=') return boundValue != comparand;
		if (c0 == '<' && c1 == '=') return boundValue <= comparand;
		if (c0 == '>' && c1 == '=') return boundValue >= comparand;
		if (c0 == '=' && c1 == '<') return boundValue <= comparand;
		if (c0 == '=' && c1 == '>') return boundValue >= comparand;
		if (c0 == '<') return boundValue < comparand;
		if (c0 == '>') return boundValue > comparand;
		return false;
	}

	static std::pair<bool, bool> EvaluateBinding(const Effect::UIVariable& var,
		const std::unordered_map<std::string, VarRef>& uniqueNameMap)
	{
		bool visible = true;
		bool readOnly = var.isReadOnly;
		if (var.uiBinding.empty())
			return { visible, readOnly };

		auto it = uniqueNameMap.find(var.uiBinding);
		if (it == uniqueNameMap.end())
			return { visible, readOnly };

		const auto& boundVar = it->second.effect->uiVariables[it->second.index];
		bool conditionMet = EvaluateCondition(var.uiBindingCondition, GetBoundValue(boundVar));

		std::string prop = var.uiBindingProperty;
		std::transform(prop.begin(), prop.end(), prop.begin(), ::tolower);

		if (prop == "hidden")
			visible = !conditionMet;
		else if (prop == "visible")
			visible = conditionMet;
		else if (prop == "readonly")
			readOnly = conditionMet;
		else if (prop == "readwrite")
			readOnly = !conditionMet;

		return { visible, readOnly };
	}

	static bool IsLabelOnly(const Effect::UIVariable& v)
	{
		if (v.isSeparator)
			return false;
		return (v.type == Effect::UIVariableType::Float && v.floatMin == 0.0f && v.floatMax == 0.0f) ||
		       (v.type == Effect::UIVariableType::Int && v.intMin == 0 && v.intMax == 0);
	}

	// Tree construction

	static void BuildUniqueNameMap(Effect* effects[], int effectCount,
		std::unordered_map<std::string, VarRef>& uniqueNameMap)
	{
		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;
			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];
				if (!var.isSeparator)
					uniqueNameMap[ComputeUniqueName(var)] = { effect, i };
			}
		}
	}

	static GroupNode* FindOrCreateChild(GroupNode& parent, const std::string& segment, const std::string& fullPath)
	{
		for (auto& c : parent.children) {
			if (c->name == segment)
				return c.get();
		}
		auto nc = std::make_unique<GroupNode>();
		nc->name = segment;
		nc->fullPath = fullPath;
		auto* ptr = nc.get();
		parent.children.push_back(std::move(nc));
		return ptr;
	}

	struct MergedGroupMeta
	{
		std::unordered_map<std::string, std::string> displayNames;
		std::unordered_map<std::string, bool> defaultOpen;
		std::unordered_map<std::string, int> ordering;
	};

	static void BuildMergedTree(Effect* effects[], int effectCount, GroupNode& root, MergedGroupMeta& meta)
	{
		std::unordered_set<std::string> rootDisplayNames;

		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;

			for (auto& [path, name] : effect->groupDisplayNames)
				meta.displayNames.try_emplace(path, name);
			for (auto& [path, open] : effect->groupDefaultOpen)
				meta.defaultOpen.try_emplace(path, open);
			for (auto& [path, order] : effect->groupOrdering)
				meta.ordering.try_emplace(path, order);

			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];
				GroupNode* node = &root;

				if (!var.isTopLevel && !var.group.empty()) {
					std::istringstream ss(var.group);
					std::string segment;
					std::string builtPath;
					while (std::getline(ss, segment, '.')) {
						if (!builtPath.empty())
							builtPath += ".";
						builtPath += segment;
						node = FindOrCreateChild(*node, segment, builtPath);
					}
				}

				if (node == &root) {
					if (var.isSeparator)
						continue;
					if (!var.displayName.empty() && !rootDisplayNames.insert(var.displayName).second)
						continue;
				}

				node->vars.push_back({ effect, i });
			}
		}
	}

	static void SortMergedTree(GroupNode& node, const MergedGroupMeta& meta)
	{
		std::stable_sort(node.vars.begin(), node.vars.end(), [](const VarRef& a, const VarRef& b) {
			auto& va = a.effect->uiVariables[a.index];
			auto& vb = b.effect->uiVariables[b.index];
			if (va.ordering != vb.ordering)
				return va.ordering > vb.ordering;
			if (va.sourceOrder != vb.sourceOrder)
				return va.sourceOrder < vb.sourceOrder;
			return false;
		});
		std::stable_sort(node.children.begin(), node.children.end(), [&](const auto& a, const auto& b) {
			int oA = 0, oB = 0;
			auto itA = meta.ordering.find(a->fullPath);
			if (itA != meta.ordering.end())
				oA = itA->second;
			auto itB = meta.ordering.find(b->fullPath);
			if (itB != meta.ordering.end())
				oB = itB->second;
			return oA > oB;
		});
		for (auto& child : node.children)
			SortMergedTree(*child, meta);
	}

	// Root separator mapping

	static void CollectMinSourceOrder(const GroupNode& node, std::unordered_map<const Effect*, int>& out)
	{
		for (auto& ref : node.vars) {
			int so = ref.effect->uiVariables[ref.index].sourceOrder;
			auto [it, inserted] = out.try_emplace(ref.effect, so);
			if (!inserted && so < it->second)
				it->second = so;
		}
		for (auto& child : node.children)
			CollectMinSourceOrder(*child, out);
	}

	static std::unordered_set<std::string> MapRootSeparators(
		Effect* effects[], int effectCount, const GroupNode& root)
	{
		// Precompute min source order per effect per root child
		struct ChildInfo
		{
			std::string fullPath;
			std::unordered_map<const Effect*, int> minOrders;
		};
		std::vector<ChildInfo> childInfos;
		childInfos.reserve(root.children.size());
		for (auto& child : root.children) {
			ChildInfo ci;
			ci.fullPath = child->fullPath;
			CollectMinSourceOrder(*child, ci.minOrders);
			childInfos.push_back(std::move(ci));
		}

		std::unordered_set<std::string> result;
		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;
			for (auto& var : effect->uiVariables) {
				if (!var.isSeparator || !var.group.empty())
					continue;
				int bestMinSO = INT_MAX;
				int bestIdx = -1;
				for (size_t ci = 0; ci < childInfos.size(); ++ci) {
					auto it = childInfos[ci].minOrders.find(effect);
					if (it == childInfos[ci].minOrders.end())
						continue;
					if (it->second > var.sourceOrder && it->second < bestMinSO) {
						bestMinSO = it->second;
						bestIdx = static_cast<int>(ci);
					}
				}
				if (bestIdx >= 0)
					result.insert(childInfos[bestIdx].fullPath);
			}
		}
		return result;
	}

	// UI rendering

	struct RenderContext
	{
		std::unordered_map<std::string, VarRef>& uniqueNameMap;
		std::unordered_set<Effect*>& changedEffects;
		MergedGroupMeta& meta;
		std::unordered_set<std::string>& separatorsBeforeGroup;
		int tableCounter = 0;

		bool BeginVarTable()
		{
			std::string tableId = "##ut_" + std::to_string(tableCounter++);
			if (ImGui::BeginTable(tableId.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
				float availWidth = ImGui::GetContentRegionAvail().x;
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.45f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.55f);
				return true;
			}
			return false;
		}
	};

	static void RenderWidget(const std::string& label, const std::string& id,
		Effect::UIVariable& uiVar, bool readOnly, Effect* effect,
		std::unordered_set<Effect*>& changedEffects)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		if (readOnly)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

		ImGui::Text("%s", label.c_str());

		if (!IsLabelOnly(uiVar)) {
			ImGui::TableSetColumnIndex(1);
			if (readOnly)
				ImGui::BeginDisabled();

			bool changed = false;
			switch (uiVar.type) {
			case Effect::UIVariableType::Float:
				changed = ImGui::SliderFloat(id.c_str(), &uiVar.floatValue, uiVar.floatMin, uiVar.floatMax, "%.3f");
				break;
			case Effect::UIVariableType::Int:
				if ((uiVar.widgetType == Effect::UIWidgetType::Dropdown || uiVar.widgetType == Effect::UIWidgetType::Quality) && !uiVar.dropdownItems.empty()) {
					int displayIndex = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? uiVar.intValue + 1 : uiVar.intValue;
					const char* currentItem = (displayIndex >= 0 && displayIndex < static_cast<int>(uiVar.dropdownItems.size())) ? uiVar.dropdownItems[displayIndex].c_str() : "";
					if (ImGui::BeginCombo(id.c_str(), currentItem)) {
						for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
							int itemValue = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? (j - 1) : j;
							if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), uiVar.intValue == itemValue)) {
								uiVar.intValue = itemValue;
								changed = true;
							}
						}
						ImGui::EndCombo();
					}
				} else {
					changed = ImGui::SliderInt(id.c_str(), &uiVar.intValue, uiVar.intMin, uiVar.intMax);
				}
				break;
			case Effect::UIVariableType::Bool:
				changed = ImGui::Checkbox(id.c_str(), &uiVar.boolValue);
				break;
			case Effect::UIVariableType::Color3:
				if (uiVar.widgetType == Effect::UIWidgetType::Vector)
					changed = ImGui::SliderFloat3(id.c_str(), uiVar.colorValue, -1.0f, 1.0f, "%.3f");
				else
					changed = ImGui::ColorEdit3(id.c_str(), uiVar.colorValue);
				break;
			case Effect::UIVariableType::Color4:
				changed = ImGui::ColorEdit4(id.c_str(), uiVar.colorValue);
				break;
			}

			if (changed)
				changedEffects.insert(effect);
			if (readOnly)
				ImGui::EndDisabled();
		}

		if (readOnly)
			ImGui::PopStyleColor();
	}

	static void RenderVar(VarRef& ref, bool& inTable, RenderContext& ctx)
	{
		auto& uiVar = ref.effect->uiVariables[ref.index];

		if (uiVar.isSeparator) {
			if (inTable) {
				ImGui::EndTable();
				inTable = false;
			}
			ImGui::Separator();
			return;
		}

		if (uiVar.displayName.empty() || uiVar.isHidden)
			return;

		auto [bindVisible, bindReadOnly] = EvaluateBinding(uiVar, ctx.uniqueNameMap);
		if (!bindVisible)
			return;

		if (IsLabelOnly(uiVar)) {
			if (inTable) {
				ImGui::EndTable();
				inTable = false;
			}
			if (uiVar.isReadOnly)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::TextWrapped("%s", uiVar.displayName.c_str());
			if (uiVar.isReadOnly)
				ImGui::PopStyleColor();
		} else {
			if (!inTable) {
				if (!ctx.BeginVarTable())
					return;
				inTable = true;
			}
			std::string baseId = "##uv_" + std::to_string(ref.index) + "_" + ref.effect->GetName();
			RenderWidget(uiVar.displayName, baseId, uiVar, bindReadOnly, ref.effect, ctx.changedEffects);
		}
	}

	static void RenderTechDropdown(Effect* effect, std::unordered_set<Effect*>& changedEffects)
	{
		ImGui::Text("%s", effect->techniqueDropdownName.c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		const char* current = effect->uiTechniques[effect->selectedTechniqueIndex].displayName.c_str();
		if (ImGui::BeginCombo(("##TECHNIQUE_" + effect->GetName()).c_str(), current)) {
			for (uint32_t i = 0; i < effect->uiTechniques.size(); ++i) {
				if (ImGui::Selectable(effect->uiTechniques[i].displayName.c_str(), effect->selectedTechniqueIndex == i)) {
					effect->selectedTechniqueIndex = i;
					changedEffects.insert(effect);
				}
				if (effect->selectedTechniqueIndex == i)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	static void RenderGroupNode(GroupNode& node, RenderContext& ctx,
		const std::vector<std::pair<Effect*, std::string>>& techDropdowns)
	{
		for (auto& [effect, group] : techDropdowns) {
			if (!group.empty() && group == node.fullPath)
				RenderTechDropdown(effect, ctx.changedEffects);
		}

		if (!node.vars.empty()) {
			bool inTable = false;
			for (auto& ref : node.vars)
				RenderVar(ref, inTable, ctx);
			if (inTable)
				ImGui::EndTable();
		}

		for (auto& child : node.children) {
			if (ctx.separatorsBeforeGroup.count(child->fullPath))
				ImGui::Separator();

			std::string displayName = child->name;
			auto nameIt = ctx.meta.displayNames.find(child->fullPath);
			if (nameIt != ctx.meta.displayNames.end())
				displayName = nameIt->second;

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
			auto openIt = ctx.meta.defaultOpen.find(child->fullPath);
			if (openIt == ctx.meta.defaultOpen.end() || openIt->second)
				flags |= ImGuiTreeNodeFlags_DefaultOpen;

			if (ImGui::TreeNodeEx((displayName + "###ugrp_" + child->fullPath).c_str(), flags)) {
				RenderGroupNode(*child, ctx, techDropdowns);
				ImGui::TreePop();
			}
		}
	}

	void RenderMergedEffectsList(Effect* effects[], int effectCount)
	{
		std::unordered_map<std::string, VarRef> uniqueNameMap;
		BuildUniqueNameMap(effects, effectCount, uniqueNameMap);

		GroupNode root;
		MergedGroupMeta meta;
		BuildMergedTree(effects, effectCount, root, meta);
		SortMergedTree(root, meta);

		auto separatorsBeforeGroup = MapRootSeparators(effects, effectCount, root);

		std::unordered_set<Effect*> changedEffects;
		RenderContext ctx{ uniqueNameMap, changedEffects, meta, separatorsBeforeGroup };

		// Collect technique dropdowns
		std::vector<std::pair<Effect*, std::string>> techDropdowns;
		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (effect->IsCompiled() && effect->uiTechniques.size() > 1 && effect->techniqueDropdownVisible)
				techDropdowns.push_back({ effect, effect->techniqueDropdownGroup });
		}

		// Render top-level technique dropdowns
		for (auto& [effect, group] : techDropdowns) {
			if (effect->techniqueDropdownTopLevel || group.empty())
				RenderTechDropdown(effect, changedEffects);
		}

		RenderGroupNode(root, ctx, techDropdowns);

		for (auto* effect : changedEffects)
			effect->UpdateUIVariables();

		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (!effect->GetErrors().empty()) {
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s:", effect->GetName().c_str());
				for (const auto& err : effect->GetErrors())
					ImGui::TextWrapped("%s", err.c_str());
			}
		}
	}

	// ── Post-load processing ─────────────────────────────────────────────

	void RecoverGroupsFromINI(Effect& effect, const std::filesystem::path& enbseriesPath)
	{
		bool hasUngrouped = false;
		for (auto& v : effect.uiVariables) {
			if (!v.isSeparator && !v.isHidden && v.group.empty() && !v.isTopLevel && !v.displayName.empty()) {
				hasUngrouped = true;
				break;
			}
		}

		if (!hasUngrouped)
			return;

		auto groupIniPath = enbseriesPath / (effect.GetName() + ".ini");
		if (!std::filesystem::exists(groupIniPath))
			return;

		std::string section = effect.GetName();
		std::transform(section.begin(), section.end(), section.begin(), ::toupper);

		std::vector<char> keysBuf(32768);
		DWORD keysLen = GetPrivateProfileStringA(section.c_str(), nullptr, "", keysBuf.data(), static_cast<DWORD>(keysBuf.size()), groupIniPath.string().c_str());

		std::unordered_map<std::string, std::string> displayNameToGroup;
		const char* key = keysBuf.data();
		while (key < keysBuf.data() + keysLen && *key) {
			std::string fullKey(key);
			key += fullKey.size() + 1;

			size_t lastDot = fullKey.rfind('.');
			if (lastDot != std::string::npos && lastDot > 0)
				displayNameToGroup.try_emplace(fullKey.substr(lastDot + 1), fullKey.substr(0, lastDot));
		}

		int recovered = 0;
		for (auto& v : effect.uiVariables) {
			if (v.isSeparator || !v.group.empty() || v.isTopLevel || v.displayName.empty())
				continue;
			auto it = displayNameToGroup.find(v.displayName);
			if (it != displayNameToGroup.end()) {
				v.group = it->second;
				effect.groupOrdering.try_emplace(v.group, 0);
				++recovered;
			}
		}
		if (recovered > 0)
			logger::info("[ENBPP] Recovered {} group assignments from INI for ungrouped variables", recovered);
	}

	void LoadTechniqueDropdownMetadata(Effect& effect)
	{
		if (!effect.IsCompiled())
			return;

		auto* fx = effect.GetEffect();
		if (!fx)
			return;

		D3DX11_EFFECT_DESC effectDesc;
		if (FAILED(fx->GetDesc(&effectDesc)) || effectDesc.Techniques == 0)
			return;

		auto firstTech = fx->GetTechniqueByIndex(0);
		if (!firstTech || !firstTech->IsValid())
			return;

		std::string dropName = Effect::GetTechniqueAnnotation(firstTech, "UIDropdownName");
		if (!dropName.empty())
			effect.techniqueDropdownName = dropName;
		std::string dropGroup = Effect::GetTechniqueAnnotation(firstTech, "UIDropdownGroup");
		if (!dropGroup.empty())
			effect.techniqueDropdownGroup = dropGroup;
		std::string dropVisible = Effect::GetTechniqueAnnotation(firstTech, "UIDropdownVisible");
		if (!dropVisible.empty())
			effect.techniqueDropdownVisible = (dropVisible != "0" && dropVisible != "false");
		std::string dropTopLevel = Effect::GetTechniqueAnnotation(firstTech, "UIDropdownTopLevel");
		if (!dropTopLevel.empty())
			effect.techniqueDropdownTopLevel = (dropTopLevel != "0" && dropTopLevel != "false");
		std::string dropOrdering = Effect::GetTechniqueAnnotation(firstTech, "UIDropdownOrdering");
		if (!dropOrdering.empty())
			effect.techniqueDropdownOrdering = SafeStoi(dropOrdering, effect.techniqueDropdownOrdering);
	}

	static float GetPeriodWeight(const std::string& period, const EffectManager::CommonVariableData& cd)
	{
		if (period == "Dawn")
			return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Dawn)];
		if (period == "Sunrise")
			return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunrise)];
		if (period == "Day")
			return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Day)];
		if (period == "Sunset")
			return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunset)];
		if (period == "Dusk")
			return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Dusk)];
		if (period == "Night")
			return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Night)];
		if (period == "Interior")
			return cd.eInteriorFactor;
		return 0.0f;
	}

	static void SetBaseVariable(ID3DX11EffectVariable* baseVar, const Effect::UIVariable& uiVar)
	{
		switch (uiVar.type) {
		case Effect::UIVariableType::Float:
			baseVar->AsScalar()->SetFloat(uiVar.floatValue);
			break;
		case Effect::UIVariableType::Int:
			baseVar->AsScalar()->SetInt(uiVar.intValue);
			break;
		case Effect::UIVariableType::Bool:
			baseVar->AsScalar()->SetBool(uiVar.boolValue);
			break;
		case Effect::UIVariableType::Color3:
		case Effect::UIVariableType::Color4:
			baseVar->AsVector()->SetFloatVector(const_cast<float*>(uiVar.colorValue));
			break;
		}
	}

	void ApplyTimeOfDayInterpolation(Effect& effect)
	{
		struct PeriodVar
		{
			size_t index;
			float weight;
		};
		std::unordered_map<std::string, std::vector<PeriodVar>> baseGroups;

		auto& cd = EffectManager::GetSingleton().commonData;

		for (size_t i = 0; i < effect.uiVariables.size(); ++i) {
			auto& uiVar = effect.uiVariables[i];
			if (uiVar.timePeriod.empty() || uiVar.isSeparator || !uiVar.effectVariable)
				continue;

			std::string baseName;
			if (uiVar.name.size() > uiVar.timePeriod.size() &&
				uiVar.name.compare(uiVar.name.size() - uiVar.timePeriod.size(), uiVar.timePeriod.size(), uiVar.timePeriod) == 0)
				baseName = uiVar.name.substr(0, uiVar.name.size() - uiVar.timePeriod.size());
			else
				continue;

			float w = GetPeriodWeight(uiVar.timePeriod, cd);
			baseGroups[baseName].push_back({ i, w });
		}

		for (auto& [baseName, entries] : baseGroups) {
			auto baseVarIt = effect.variables.find(baseName);
			if (baseVarIt == effect.variables.end())
				continue;

			auto* baseVar = baseVarIt->second.get();
			if (!baseVar || !baseVar->IsValid())
				continue;

			float totalWeight = 0.0f;
			for (auto& e : entries)
				totalWeight += e.weight;

			if (totalWeight <= 0.0f)
				continue;

			auto& firstVar = effect.uiVariables[entries[0].index];

			// For discrete types, pick the period with the highest weight
			if (firstVar.type == Effect::UIVariableType::Int || firstVar.type == Effect::UIVariableType::Bool) {
				size_t bestIdx = entries[0].index;
				float bestWeight = entries[0].weight;
				for (size_t i = 1; i < entries.size(); ++i) {
					if (entries[i].weight > bestWeight) {
						bestWeight = entries[i].weight;
						bestIdx = entries[i].index;
					}
				}
				SetBaseVariable(baseVar, effect.uiVariables[bestIdx]);
				continue;
			}

			// For continuous types (Float, Color), weighted blend
			if (firstVar.type == Effect::UIVariableType::Float) {
				float result = 0.0f;
				for (auto& e : entries)
					result += effect.uiVariables[e.index].floatValue * (e.weight / totalWeight);
				baseVar->AsScalar()->SetFloat(result);
			} else {
				int comps = (firstVar.type == Effect::UIVariableType::Color3) ? 3 : 4;
				float result[4] = {};
				for (auto& e : entries) {
					float w = e.weight / totalWeight;
					for (int c = 0; c < comps; ++c)
						result[c] += effect.uiVariables[e.index].colorValue[c] * w;
				}
				baseVar->AsVector()->SetFloatVector(result);
			}
		}
	}

}

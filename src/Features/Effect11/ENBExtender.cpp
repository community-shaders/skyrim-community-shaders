#include "ENBExtender.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

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

	static int SafeStoi(const std::string& s, int fallback = 0)
	{
		try {
			return std::stoi(s);
		} catch (...) {
			if (!s.empty())
				logger::warn("[ENBExtender] Failed to parse int from '{}'", s);
			return fallback;
		}
	}

	static float SafeStof(const std::string& s, float fallback = 0.0f)
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

	// ── UIDefine storage ──────────────────��─────────────────────────────

	static std::vector<UIDefineInfo> lastUIDefines;

	const std::vector<UIDefineInfo>& GetLastUIDefines()
	{
		return lastUIDefines;
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

			struct TechInfo
			{
				std::string annotations;
				std::string body;
			};
			std::vector<TechInfo> techniques;

			size_t techSearch = 0;
			while (true) {
				size_t techPos = body.find("technique11", techSearch);
				if (techPos == std::string::npos)
					break;
				if (!IsWordBoundary(body, techPos)) {
					techSearch = techPos + 11;
					continue;
				}

				size_t afterKeyword = techPos + 11;
				size_t nextNonSpace = body.find_first_not_of(" \t\r\n", afterKeyword);
				if (nextNonSpace == std::string::npos) {
					techSearch = afterKeyword;
					continue;
				}

				TechInfo info;
				size_t techBodySearchFrom = nextNonSpace;

				if (body[nextNonSpace] == '<') {
					auto techAnno = ParseAngleBracketBlock(body, nextNonSpace);
					if (techAnno.closePos == std::string::npos) {
						techSearch = afterKeyword;
						continue;
					}
					info.annotations = techAnno.content;
					techBodySearchFrom = techAnno.closePos + 1;
				} else if (body[nextNonSpace] != '{') {
					size_t afterName = body.find_first_of("<{", nextNonSpace);
					if (afterName == std::string::npos) {
						techSearch = afterKeyword;
						continue;
					}
					if (body[afterName] == '<') {
						auto techAnno = ParseAngleBracketBlock(body, afterName);
						if (techAnno.closePos == std::string::npos) {
							techSearch = afterKeyword;
							continue;
						}
						info.annotations = techAnno.content;
						techBodySearchFrom = techAnno.closePos + 1;
					} else {
						techBodySearchFrom = afterName;
					}
				}

				size_t techBodyOpen = body.find('{', techBodySearchFrom);
				if (techBodyOpen == std::string::npos) {
					techSearch = afterKeyword;
					continue;
				}
				size_t techBodyClose = FindMatchingBrace(body, techBodyOpen);
				if (techBodyClose == std::string::npos) {
					techSearch = afterKeyword;
					continue;
				}

				info.body = body.substr(techBodyOpen, techBodyClose - techBodyOpen + 1);
				techniques.push_back(std::move(info));
				techSearch = techBodyClose + 1;
			}

			std::string replacement;
			for (size_t i = 0; i < techniques.size(); ++i) {
				std::string techName = groupName;
				if (i > 0)
					techName += std::to_string(i);

				replacement += "technique11 " + techName;
				if (i == 0 && (!groupAnnotations.empty() || !techniques[i].annotations.empty())) {
					replacement += " <";
					if (!groupAnnotations.empty())
						replacement += groupAnnotations;
					if (!groupAnnotations.empty() && !techniques[i].annotations.empty())
						replacement += " ";
					if (!techniques[i].annotations.empty())
						replacement += techniques[i].annotations;
					replacement += ">";
				} else if (i > 0 && !techniques[i].annotations.empty()) {
					replacement += " <" + techniques[i].annotations + ">";
				}
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
		const std::string& iniPath, const std::string& iniSection, std::string& result)
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
			UIDefineInfo info;
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

			lastUIDefines.push_back(std::move(info));
		}

		result += "#define " + defineName + " " + finalVal + "\n";
		return true;
	}

	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, const std::string& iniPath, const std::string& iniSection)
	{
		lastUIDefines.clear();

		std::string result;
		result.reserve(content.size());

		std::istringstream stream(content);
		std::string line;

		while (std::getline(stream, line)) {
			if (HandleErrorDirective(line, result))
				continue;
			if (HandlePragmaExists(line, enbseriesPath, result))
				continue;
			if (HandlePragmaUIDefine(line, stream, iniPath, iniSection, result))
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
				std::string groupName;
				size_t searchPos = 0;
				while (searchPos < line.size()) {
					size_t ugPos = line.find("UIGroup", searchPos);
					if (ugPos == std::string::npos)
						break;
					size_t afterUG = ugPos + 7;
					if (afterUG < line.size() && line[afterUG] != 'B' && line[afterUG] != 'E' && line[afterUG] != 'N' && line[afterUG] != 'O') {
						size_t eqPos = line.find('=', afterUG);
						if (eqPos != std::string::npos) {
							size_t q1 = line.find('"', eqPos + 1);
							size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
							if (q1 != std::string::npos && q2 != std::string::npos)
								groupName = line.substr(q1 + 1, q2 - q1 - 1);
						}
						break;
					}
					searchPos = afterUG;
				}

				if (groupName.empty()) {
					size_t angleClose = line.rfind('>');
					if (angleClose != std::string::npos) {
						size_t eqPos = line.find('=', angleClose);
						if (eqPos != std::string::npos) {
							size_t q1 = line.find('"', eqPos + 1);
							size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
							if (q1 != std::string::npos && q2 != std::string::npos)
								groupName = line.substr(q1 + 1, q2 - q1 - 1);
						}
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
			static const std::string types[] = { "float4 ", "float3 ", "float2 ", "float ", "int ", "bool ", "string " };

			for (const auto& type : types) {
				if (trimmed.size() >= type.size() && trimmed.compare(0, type.size(), type) == 0) {
					size_t nameStart = trimmed.find_first_not_of(" \t", type.size());
					if (nameStart == std::string::npos)
						break;
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
					break;
				}
			}
		}
	}

	// ── ProcessExtenderStringVariable ────────────────────────────────────

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
		std::string fullPath = BuildGroupPath(groupStack);

		if (effect.groupOrdering.find(fullPath) == effect.groupOrdering.end())
			effect.groupOrdering[fullPath] = 0;

		std::string displayName = effect.GetUIAnnotation(variable, "UIGroupName");
		if (!displayName.empty())
			effect.groupDisplayNames[fullPath] = displayName;

		std::string openStr = effect.GetUIAnnotation(variable, "UIGroupOpen");
		if (!openStr.empty())
			effect.groupDefaultOpen[fullPath] = IsTruthy(openStr);

		std::string orderStr = effect.GetUIAnnotation(variable, "UIOrdering");
		if (!orderStr.empty())
			effect.groupOrdering[fullPath] = SafeStoi(orderStr);

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
		std::string uiNameStr = effect.GetUIAnnotation(variable, "UIName");
		std::string labelText;
		if (!uiNameStr.empty()) {
			labelText = uiNameStr;
		} else {
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

		if (IsTruthy(effect.GetUIAnnotation(variable, "UIHidden")) ||
			!IsTruthy(effect.GetUIAnnotation(variable, "UIVisible").empty() ? "1" : effect.GetUIAnnotation(variable, "UIVisible")))
			return true;

		Effect::UIVariable labelVar = {};
		labelVar.name = varDesc.Name;
		labelVar.displayName = labelText;
		labelVar.type = Effect::UIVariableType::Float;
		labelVar.floatMin = 0.0f;
		labelVar.floatMax = 0.0f;
		labelVar.isReadOnly = true;
		labelVar.group = ResolveGroup(varDesc.Name, effect.GetUIAnnotation(variable, "UIGroup"), groupStack, effect);
		labelVar.sourceOrder = GetSourceOrder(varDesc.Name, effect);

		std::string orderStr = effect.GetUIAnnotation(variable, "UIOrdering");
		if (!orderStr.empty())
			labelVar.ordering = SafeStoi(orderStr);

		labelVar.separation = effect.GetUIAnnotation(variable, "Separation");
		labelVar.uniqueName = effect.GetUIAnnotation(variable, "UniqueName");
		labelVar.uiBinding = effect.GetUIAnnotation(variable, "UIBinding");
		labelVar.uiBindingProperty = effect.GetUIAnnotation(variable, "UIBindingProperty");
		labelVar.uiBindingCondition = effect.GetUIAnnotation(variable, "UIBindingCondition");

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

	static void TrackGroupMetadata(const Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable, Effect& effect)
	{
		if (uiVar.group.empty() || effect.groupOrdering.find(uiVar.group) != effect.groupOrdering.end())
			return;

		effect.groupOrdering[uiVar.group] = 0;
		std::string displayName = effect.GetUIAnnotation(variable, "UIGroupName");
		if (!displayName.empty())
			effect.groupDisplayNames[uiVar.group] = displayName;
		std::string openStr = effect.GetUIAnnotation(variable, "UIGroupOpen");
		if (!openStr.empty())
			effect.groupDefaultOpen[uiVar.group] = IsTruthy(openStr);
		if (uiVar.ordering != 0)
			effect.groupOrdering[uiVar.group] = uiVar.ordering;
	}

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

		TrackGroupMetadata(uiVar, variable, effect);
	}

	// ── InsertUIDefines ─────────────��─────────────────────��─────────────

	void InsertUIDefines(Effect& effect)
	{
		auto& uiDefines = GetLastUIDefines();
		for (const auto& def : uiDefines) {
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

		if (!uiDefines.empty())
			logger::info("[ENBExtender] Inserted {} UI defines", uiDefines.size());
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

		std::string op;
		size_t valueStart = 0;

		if (condStr.size() >= 2) {
			std::string twoChar = condStr.substr(0, 2);
			if (twoChar == "==" || twoChar == "!=" || twoChar == "<=" || twoChar == ">=" ||
				twoChar == "=<" || twoChar == "=>") {
				op = twoChar;
				valueStart = 2;
			}
		}
		if (op.empty() && !condStr.empty() && (condStr[0] == '<' || condStr[0] == '>')) {
			op = condStr.substr(0, 1);
			valueStart = 1;
		}
		if (op.empty())
			return boundValue != 0.0f;

		float comparand = SafeStof(condStr.substr(valueStart));

		if (op == "==")
			return boundValue == comparand;
		if (op == "!=")
			return boundValue != comparand;
		if (op == "<")
			return boundValue < comparand;
		if (op == ">")
			return boundValue > comparand;
		if (op == "<=" || op == "=<")
			return boundValue <= comparand;
		if (op == ">=" || op == "=>")
			return boundValue >= comparand;
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
					bool isDuplicate = false;
					for (auto& existing : root.vars) {
						if (!var.displayName.empty() && existing.effect->uiVariables[existing.index].displayName == var.displayName) {
							isDuplicate = true;
							break;
						}
					}
					if (isDuplicate)
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

	static int GetMinSourceOrderForEffect(const GroupNode& node, const Effect* effect)
	{
		int minOrder = INT_MAX;
		for (auto& ref : node.vars) {
			if (ref.effect == effect && ref.effect->uiVariables[ref.index].sourceOrder < minOrder)
				minOrder = ref.effect->uiVariables[ref.index].sourceOrder;
		}
		for (auto& child : node.children) {
			int childOrder = GetMinSourceOrderForEffect(*child, effect);
			if (childOrder < minOrder)
				minOrder = childOrder;
		}
		return minOrder;
	}

	static std::unordered_map<std::string, bool> MapRootSeparators(
		Effect* effects[], int effectCount, const GroupNode& root)
	{
		std::vector<VarRef> rootSeparators;
		for (int e = 0; e < effectCount; ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;
			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];
				if (var.isSeparator && var.group.empty())
					rootSeparators.push_back({ effect, i });
			}
		}

		std::unordered_map<std::string, bool> result;
		for (auto& sep : rootSeparators) {
			auto& sepVar = sep.effect->uiVariables[sep.index];
			int bestMinSO = INT_MAX;
			int bestIdx = -1;
			for (size_t ci = 0; ci < root.children.size(); ++ci) {
				int minSO = GetMinSourceOrderForEffect(*root.children[ci], sep.effect);
				if (minSO > sepVar.sourceOrder && minSO < bestMinSO) {
					bestMinSO = minSO;
					bestIdx = static_cast<int>(ci);
				}
			}
			if (bestIdx >= 0)
				result[root.children[bestIdx]->fullPath] = true;
		}
		return result;
	}

	// UI rendering

	struct RenderContext
	{
		std::unordered_map<std::string, VarRef>& uniqueNameMap;
		std::unordered_set<Effect*>& changedEffects;
		MergedGroupMeta& meta;
		std::unordered_map<std::string, bool>& separatorsBeforeGroup;
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
}

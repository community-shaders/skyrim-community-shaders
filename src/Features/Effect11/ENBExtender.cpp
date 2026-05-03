#include "ENBExtender.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

#include "EffectManager.h"

namespace ENBExtender
{
	static bool IsTruthy(const std::string& s)
	{
		return !s.empty() && s != "0" && s != "false";
	}

	static bool IsIdentChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	static void Trim(std::string& s, const char* chars = " \t")
	{
		s.erase(0, s.find_first_not_of(chars));
		s.erase(s.find_last_not_of(chars) + 1);
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
		return (it != effect.sourceGroupMap.end()) ? it->second : std::string{};
	}

	static int GetSourceOrder(const std::string& varName, const Effect& effect)
	{
		auto it = effect.sourceOrderMap.find(varName);
		return (it != effect.sourceOrderMap.end()) ? it->second : INT_MAX;
	}

	static std::string GetStringVariableValue(ID3DX11EffectVariable* variable)
	{
		auto* strVar = variable->AsString();
		if (strVar && strVar->IsValid()) {
			LPCSTR val = nullptr;
			if (SUCCEEDED(strVar->GetString(&val)) && val && val[0] != '\0')
				return val;
		}
		return {};
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

	// KIEFX

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

	// Text parsing

	static size_t FindMatchingBrace(const std::string& text, size_t openPos)
	{
		int depth = 1;
		bool inLineComment = false, inBlockComment = false, inString = false;
		for (size_t i = openPos + 1; i < text.size(); ++i) {
			char c = text[i], next = (i + 1 < text.size()) ? text[i + 1] : '\0';
			if (inLineComment) { if (c == '\n') inLineComment = false; continue; }
			if (inBlockComment) { if (c == '*' && next == '/') { inBlockComment = false; ++i; } continue; }
			if (inString) { if (c == '\\') ++i; else if (c == '"') inString = false; continue; }
			if (c == '/' && next == '/') { inLineComment = true; ++i; continue; }
			if (c == '/' && next == '*') { inBlockComment = true; ++i; continue; }
			if (c == '"') { inString = true; continue; }
			if (c == '{') ++depth;
			else if (c == '}' && --depth == 0) return i;
		}
		return std::string::npos;
	}

	static size_t FindMatchingAngleBracket(const std::string& text, size_t openPos)
	{
		int depth = 1;
		for (size_t i = openPos + 1; i < text.size(); ++i) {
			if (text[i] == '<') ++depth;
			else if (text[i] == '>' && --depth == 0) return i;
		}
		return std::string::npos;
	}

	static std::pair<std::string, size_t> ParseAngleBrackets(const std::string& text, size_t from)
	{
		size_t open = text.find_first_not_of(" \t\r\n", from);
		if (open == std::string::npos || text[open] != '<')
			return { {}, std::string::npos };
		size_t close = FindMatchingAngleBracket(text, open);
		if (close == std::string::npos)
			return { {}, std::string::npos };
		return { text.substr(open + 1, close - open - 1), close };
	}

	static std::string ExtractAnnotation(const std::string& annotations, const std::string& name)
	{
		for (size_t pos = 0;;) {
			pos = annotations.find(name, pos);
			if (pos == std::string::npos)
				return {};
			size_t end = pos + name.size();
			if ((pos > 0 && IsIdentChar(annotations[pos - 1])) ||
				(end < annotations.size() && IsIdentChar(annotations[end]))) {
				pos = end;
				continue;
			}
			size_t eq = annotations.find('=', end);
			if (eq == std::string::npos)
				return {};
			size_t vs = annotations.find_first_not_of(" \t", eq + 1);
			if (vs == std::string::npos)
				return {};
			if (annotations[vs] == '"') {
				size_t qe = annotations.find('"', vs + 1);
				return (qe != std::string::npos) ? annotations.substr(vs + 1, qe - vs - 1) : std::string{};
			}
			size_t ve = annotations.find_first_of(";>", vs);
			std::string val = (ve != std::string::npos) ? annotations.substr(vs, ve - vs) : annotations.substr(vs);
			Trim(val);
			return val;
		}
	}

	static bool IsWordBoundary(const std::string& text, size_t pos)
	{
		return pos == 0 || !IsIdentChar(text[pos - 1]);
	}

	static bool IsOnPreprocessorLine(const std::string& content, size_t pos)
	{
		size_t lineStart = content.rfind('\n', pos);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		size_t firstChar = content.find_first_not_of(" \t", lineStart);
		return firstChar != std::string::npos && content[firstChar] == '#';
	}

	// ConvertFxGroups

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
		if (text[pos] != '<' && text[pos] != '{') {
			pos = text.find_first_of("<{", pos);
			if (pos == std::string::npos)
				return {};
		}
		if (text[pos] == '<') {
			auto [anno, closePos] = ParseAngleBrackets(text, pos);
			if (closePos == std::string::npos)
				return {};
			result.annotations = anno;
			pos = text.find_first_not_of(" \t\r\n", closePos + 1);
			if (pos == std::string::npos)
				return {};
		}
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
			searchStart = fxPos + 7;

			if (!IsWordBoundary(content, fxPos) || IsOnPreprocessorLine(content, fxPos))
				continue;

			size_t nameStart = content.find_first_not_of(" \t", fxPos + 7);
			if (nameStart == std::string::npos)
				continue;
			size_t nameEnd = content.find_first_of(" \t<{", nameStart);
			if (nameEnd == std::string::npos)
				continue;
			std::string groupName = content.substr(nameStart, nameEnd - nameStart);

			std::string groupAnnotations;
			size_t bodySearchFrom = nameEnd;
			auto [ga, gaClose] = ParseAngleBrackets(content, nameEnd);
			if (gaClose != std::string::npos) {
				groupAnnotations = ga;
				bodySearchFrom = gaClose + 1;
			}

			size_t bodyOpen = content.find('{', bodySearchFrom);
			if (bodyOpen == std::string::npos)
				continue;
			size_t bodyClose = FindMatchingBrace(content, bodyOpen);
			if (bodyClose == std::string::npos)
				continue;

			std::string body = content.substr(bodyOpen + 1, bodyClose - bodyOpen - 1);

			std::vector<ParsedBlock> techniques;
			for (size_t ts = 0;;) {
				size_t tp = body.find("technique11", ts);
				if (tp == std::string::npos)
					break;
				if (!IsWordBoundary(body, tp)) { ts = tp + 11; continue; }
				auto parsed = ParseKeywordBlock(body, tp + 11);
				if (parsed.endPos == std::string::npos) { ts = tp + 11; continue; }
				ts = parsed.endPos + 1;
				techniques.push_back(std::move(parsed));
			}

			std::string replacement;
			for (size_t i = 0; i < techniques.size(); ++i) {
				std::string techName = groupName + (i > 0 ? std::to_string(i) : "");
				std::string anno = (i == 0 && !groupAnnotations.empty())
				                       ? groupAnnotations + (techniques[i].annotations.empty() ? "" : " " + techniques[i].annotations)
				                       : techniques[i].annotations;
				replacement += "technique11 " + techName;
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

	// ConvertExtenderSyntax

	static bool HandlePragmaUIDefine(std::string& line, std::istringstream& stream,
		const std::string& iniPath, const std::string& iniSection, std::string& result, std::vector<Effect::UIDefineInfo>& uiDefines)
	{
		size_t pragmaPos = line.find("pragma");
		if (pragmaPos == std::string::npos)
			return false;
		size_t uidefPos = line.find("uidefine", pragmaPos + 6);
		if (uidefPos == std::string::npos)
			return false;

		for (auto t = line.find_last_not_of(" \t\r");
			 t != std::string::npos && line[t] == '\\';
			 t = line.find_last_not_of(" \t\r")) {
			line.erase(t);
			std::string next;
			if (!std::getline(stream, next))
				break;
			line += next;
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
		Trim(typeName);

		size_t nameStart = inner.find_first_not_of(" \t", typeEnd);
		if (nameStart == std::string::npos)
			return false;
		size_t nameEnd = inner.find_first_of("<=", nameStart);
		std::string defineName = (nameEnd != std::string::npos) ? inner.substr(nameStart, nameEnd - nameStart) : inner.substr(nameStart);
		Trim(defineName);

		std::string annotations;
		size_t angleOpen = inner.find('<', nameStart);
		size_t angleClose = inner.rfind('>');
		if (angleOpen != std::string::npos && angleClose != std::string::npos && angleClose > angleOpen)
			annotations = inner.substr(angleOpen + 1, angleClose - angleOpen - 1);

		size_t equalsPos = (angleClose != std::string::npos) ? inner.find('=', angleClose) : inner.rfind('=');
		std::string defaultVal = "0";
		if (equalsPos != std::string::npos) {
			defaultVal = inner.substr(equalsPos + 1);
			Trim(defaultVal, " \t;");
			if (defaultVal == "false") defaultVal = "0";
			else if (defaultVal == "true") defaultVal = "1";
		}

		auto ann = [&](const char* name) { return ExtractAnnotation(annotations, name); };
		std::string uiName = ann("UIName");
		std::string uiGroup = ann("UIGroup");

		std::string finalVal = defaultVal;
		if (!iniPath.empty() && !iniSection.empty() && !uiName.empty()) {
			std::string iniKey = uiGroup.empty() ? uiName : (uiGroup + "." + uiName);
			char buf[1024];
			if (GetPrivateProfileStringA(iniSection.c_str(), iniKey.c_str(), "", buf, sizeof(buf), iniPath.c_str()) > 0) {
				std::string iniVal(buf);
				Trim(iniVal);
				if (iniVal == "false") iniVal = "0";
				else if (iniVal == "true") iniVal = "1";
				finalVal = iniVal;
			}
		}

		if (!uiName.empty()) {
			bool isInt = (typeName == "int");
			Effect::UIDefineInfo info;
			info.defineName = defineName;
			info.displayName = uiName;
			info.group = uiGroup;
			info.type = typeName;
			info.value = finalVal;
			info.widget = ann("UIWidget");
			info.list = ann("UIList");

			auto minStr = ann("UIMin"), maxStr = ann("UIMax");
			if (!minStr.empty()) { if (isInt) info.intMin = SafeStoi(minStr); else info.floatMin = SafeStof(minStr); }
			if (!maxStr.empty()) { if (isInt) info.intMax = SafeStoi(maxStr); else info.floatMax = SafeStof(maxStr); }
			auto stepStr = ann("UIStep");
			if (!stepStr.empty())
				info.floatStep = SafeStof(stepStr);
			auto orderStr = ann("UIOrdering");
			if (!orderStr.empty()) {
				info.ordering = SafeStoi(orderStr);
				info.hasExplicitOrdering = true;
			}
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
			// Strip #error directives
			size_t errPos = line.find_first_not_of(" \t#");
			if (errPos != std::string::npos && line.compare(errPos, 5, "error") == 0 && line.find('#') < errPos) {
				result += "\n";
				continue;
			}

			// Handle #pragma exists("file", DEFINE)
			size_t pragmaPos = line.find("#pragma");
			if (pragmaPos != std::string::npos) {
				size_t existsPos = line.find("exists", pragmaPos + 7);
				if (existsPos != std::string::npos) {
					size_t op = line.find('(', existsPos), cp = line.rfind(')');
					if (op != std::string::npos && cp != std::string::npos && cp > op) {
						std::string args = line.substr(op + 1, cp - op - 1);
						size_t q1 = args.find('"');
						size_t q2 = (q1 != std::string::npos) ? args.find('"', q1 + 1) : std::string::npos;
						size_t comma = (q2 != std::string::npos) ? args.find(',', q2 + 1) : std::string::npos;
						if (q1 != std::string::npos && q2 != std::string::npos && comma != std::string::npos) {
							std::string defName = args.substr(comma + 1);
							Trim(defName);
							bool exists = std::filesystem::exists(enbseriesPath / args.substr(q1 + 1, q2 - q1 - 1));
							result += "#define " + defName + (exists ? " 1" : " 0") + "\n";
							continue;
						}
					}
				}
			}

			if (HandlePragmaUIDefine(line, stream, iniPath, iniSection, result, uiDefines))
				continue;
			result += line + "\n";
		}

		content = std::move(result);
	}

	// ParseSourceGroupScopes

	static bool IsHLSLType(const std::string& s)
	{
		static const std::unordered_set<std::string> types = { "float", "float2", "float3", "float4", "int", "bool", "string" };
		return types.count(s) > 0;
	}

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
					size_t gt = line.rfind('>');
					size_t q1 = (gt != std::string::npos) ? line.find('"', gt) : std::string::npos;
					size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
					if (q2 != std::string::npos)
						groupName = line.substr(q1 + 1, q2 - q1 - 1);
				}
				if (!groupName.empty()) {
					groupStack.push_back(groupName);
					auto orderStr = ExtractAnnotation(line, "UIOrdering");
					if (!orderStr.empty()) {
						auto& gm = effect.groupMeta[BuildGroupPath(groupStack)];
						gm.ordering = SafeStoi(orderStr);
						gm.hasOrdering = true;
					}
				}
				continue;
			}

			if (line.find("UIGroupEnd") != std::string::npos) {
				if (!groupStack.empty())
					groupStack.pop_back();
				continue;
			}

			std::string trimmed = line.substr(firstNonSpace);
			size_t spacePos = trimmed.find_first_of(" \t");
			if (spacePos == std::string::npos || !IsHLSLType(trimmed.substr(0, spacePos)))
				continue;

			size_t nameStart = trimmed.find_first_not_of(" \t", spacePos);
			if (nameStart == std::string::npos)
				continue;
			size_t nameEnd = nameStart;
			while (nameEnd < trimmed.size() && IsIdentChar(trimmed[nameEnd]))
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

	// Group metadata tracking

	static void TrackGroupMeta(const std::string& groupPath, ID3DX11EffectVariable* variable, Effect& effect)
	{
		if (groupPath.empty())
			return;
		auto [it, inserted] = effect.groupMeta.try_emplace(groupPath);
		if (!inserted)
			return;
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };
		auto s = get("UIGroupName");
		if (!s.empty())
			it->second.displayName = s;
		s = get("UIGroupOpen");
		if (!s.empty())
			it->second.defaultOpen = IsTruthy(s);
		s = get("UIOrdering");
		if (!s.empty()) {
			it->second.ordering = SafeStoi(s);
			it->second.hasOrdering = true;
		}
	}

	// ProcessExtenderStringVariable

	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc,
		std::vector<std::string>& groupStack, Effect& effect)
	{
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };

		if (IsTruthy(get("UIGroupBegin"))) {
			std::string groupName = get("UIGroup");
			if (groupName.empty())
				groupName = GetStringVariableValue(variable);
			if (!groupName.empty()) {
				groupStack.push_back(groupName);
				TrackGroupMeta(BuildGroupPath(groupStack), variable, effect);
			}
			return true;
		}

		if (IsTruthy(get("UIGroupEnd"))) {
			if (!groupStack.empty())
				groupStack.pop_back();
			return true;
		}

		if (IsTruthy(get("UISeparator"))) {
			auto sep = MakeSeparator(varDesc.Name, groupStack, effect);
			auto explicitGroup = get("UIGroup");
			if (!explicitGroup.empty())
				sep.group = explicitGroup;
			effect.uiVariables.push_back(sep);
			return true;
		}

		std::string labelText = get("UIName");
		if (labelText.empty())
			labelText = GetStringVariableValue(variable);
		if (labelText.empty())
			return true;

		if (labelText == "|") {
			effect.uiVariables.push_back(MakeSeparator(varDesc.Name, groupStack, effect));
			return true;
		}

		Effect::UIVariable labelVar = {};
		labelVar.name = varDesc.Name;
		labelVar.displayName = labelText;
		labelVar.type = Effect::UIVariableType::Float;
		labelVar.isReadOnly = true;
		ApplyExtenderAnnotations(labelVar, variable, groupStack, effect);
		if (!labelVar.isHidden)
			effect.uiVariables.push_back(labelVar);
		return true;
	}

	// ApplyExtenderAnnotations

	void ApplyExtenderAnnotations(Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };

		uiVar.group = ResolveGroup(uiVar.name, get("UIGroup"), groupStack, effect);
		uiVar.sourceOrder = GetSourceOrder(uiVar.name, effect);

		auto orderStr = get("UIOrdering");
		if (!orderStr.empty())
			uiVar.ordering = SafeStoi(orderStr);

		uiVar.isReadOnly = IsTruthy(get("UIReadOnly"));
		auto hiddenStr = get("UIHidden"), visibleStr = get("UIVisible");
		uiVar.isHidden = IsTruthy(hiddenStr) || (!visibleStr.empty() && !IsTruthy(visibleStr));

		uiVar.isTopLevel = IsTruthy(get("UITopLevel"));
		if (uiVar.isTopLevel)
			uiVar.group.clear();

		uiVar.uniqueName = get("UniqueName");
		uiVar.uiBinding = get("UIBinding");
		uiVar.uiBindingFile = get("UIBindingFile");
		uiVar.uiBindingProperty = get("UIBindingProperty");
		uiVar.uiBindingCondition = get("UIBindingCondition");
		uiVar.ignorePerfMode = IsTruthy(get("UIIgnorePerfMode"));
		uiVar.isWeatherString = IsTruthy(get("UIWeatherString"));
		uiVar.isWeatherOnlyString = IsTruthy(get("UIWeatherOnlyString"));
		uiVar.separation = get("Separation");

		TrackGroupMeta(uiVar.group, variable, effect);
	}

	// InsertUIDefines

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

			if (!def.group.empty() && def.hasExplicitOrdering) {
				auto& gm = effect.groupMeta[def.group];
				if (!gm.hasOrdering) {
					gm.ordering = def.ordering;
					gm.hasOrdering = true;
				}
			}

			effect.uiVariables.push_back(uiVar);
		}
	}

	void ParseTimePeriod(Effect::UIVariable& uiVar)
	{
		if (uiVar.separation.empty() || uiVar.separation == "None")
			return;
		static const std::string_view periods[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "Interior" };
		for (auto period : periods) {
			auto suffix = std::string(period) + " - ";
			if (uiVar.displayName.compare(0, 3 + suffix.size(), "|- " + suffix) == 0 ||
				uiVar.displayName.compare(0, suffix.size(), suffix) == 0) {
				uiVar.timePeriod = period;
				return;
			}
		}
	}

	// UI tree and rendering

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

	static bool EvaluateCondition(const std::string& condStr, float boundValue)
	{
		if (condStr.empty())
			return boundValue != 0.0f;
		size_t valueStart = 0;
		if (condStr.size() >= 2 && !std::isdigit(static_cast<unsigned char>(condStr[1])) && condStr[1] != '-')
			valueStart = 2;
		else if (condStr[0] == '<' || condStr[0] == '>')
			valueStart = 1;
		else
			return boundValue != 0.0f;
		float cmp = SafeStof(condStr.substr(valueStart));
		char c0 = condStr[0], c1 = (condStr.size() >= 2) ? condStr[1] : '\0';
		if (c0 == '=' && c1 == '=') return boundValue == cmp;
		if (c0 == '!' && c1 == '=') return boundValue != cmp;
		if (c0 == '<' && c1 == '=') return boundValue <= cmp;
		if (c0 == '>' && c1 == '=') return boundValue >= cmp;
		if (c0 == '=' && c1 == '<') return boundValue <= cmp;
		if (c0 == '=' && c1 == '>') return boundValue >= cmp;
		if (c0 == '<') return boundValue < cmp;
		if (c0 == '>') return boundValue > cmp;
		return false;
	}

	using FileUniqueNameMap = std::unordered_map<std::string, std::unordered_map<std::string, VarRef>>;

	static std::pair<bool, bool> EvaluateBinding(const Effect::UIVariable& var,
		const std::unordered_map<std::string, VarRef>& uniqueNameMap,
		const FileUniqueNameMap& fileUniqueNameMap)
	{
		bool visible = true, readOnly = var.isReadOnly;
		if (var.uiBinding.empty())
			return { visible, readOnly };

		const VarRef* boundRef = nullptr;
		if (!var.uiBindingFile.empty()) {
			auto fileIt = fileUniqueNameMap.find(var.uiBindingFile);
			if (fileIt != fileUniqueNameMap.end()) {
				auto varIt = fileIt->second.find(var.uiBinding);
				if (varIt != fileIt->second.end())
					boundRef = &varIt->second;
			}
		} else {
			auto it = uniqueNameMap.find(var.uiBinding);
			if (it != uniqueNameMap.end())
				boundRef = &it->second;
		}

		if (!boundRef)
			return { visible, readOnly };

		const auto& bv = boundRef->effect->uiVariables[boundRef->index];
		float val = 0.0f;
		switch (bv.type) {
		case Effect::UIVariableType::Float: val = bv.floatValue; break;
		case Effect::UIVariableType::Int: val = static_cast<float>(bv.intValue); break;
		case Effect::UIVariableType::Bool: val = bv.boolValue ? 1.0f : 0.0f; break;
		default: break;
		}
		bool cond = EvaluateCondition(var.uiBindingCondition, val);

		std::string prop = var.uiBindingProperty;
		std::transform(prop.begin(), prop.end(), prop.begin(), ::tolower);
		if (prop == "hidden") visible = !cond;
		else if (prop == "visible") visible = cond;
		else if (prop == "readonly") readOnly = cond;
		else if (prop == "readwrite") readOnly = !cond;

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

	static void BuildUniqueNameMap(std::span<Effect*> effects,
		std::unordered_map<std::string, VarRef>& uniqueNameMap,
		FileUniqueNameMap& fileUniqueNameMap)
	{
		for (size_t e = 0; e < effects.size(); ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;
			auto& fileMap = fileUniqueNameMap[effect->GetName()];
			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];
				if (!var.isSeparator) {
					std::string uname = !var.uniqueName.empty() ? var.uniqueName
					                    : !var.group.empty()    ? var.group + "." + var.displayName
					                                            : var.displayName;
					uniqueNameMap[uname] = { effect, i };
					fileMap[uname] = { effect, i };
				}
			}
		}
	}

	static GroupNode* FindOrCreateChild(GroupNode& parent, const std::string& segment, const std::string& fullPath)
	{
		for (auto& c : parent.children)
			if (c->name == segment)
				return c.get();
		auto nc = std::make_unique<GroupNode>();
		nc->name = segment;
		nc->fullPath = fullPath;
		auto* ptr = nc.get();
		parent.children.push_back(std::move(nc));
		return ptr;
	}

	using MergedGroupMeta = std::unordered_map<std::string, Effect::GroupMeta>;

	static GroupNode* TraverseGroupPath(GroupNode& root, const std::string& groupPath)
	{
		GroupNode* node = &root;
		size_t start = 0;
		while (start < groupPath.size()) {
			size_t dot = groupPath.find('.', start);
			if (dot == std::string::npos)
				dot = groupPath.size();
			node = FindOrCreateChild(*node, groupPath.substr(start, dot - start), groupPath.substr(0, dot));
			start = dot + 1;
		}
		return node;
	}

	static void BuildMergedTree(std::span<Effect*> effects, GroupNode& root, MergedGroupMeta& meta)
	{
		std::unordered_map<GroupNode*, std::unordered_set<std::string>> seenDisplayNames;

		for (size_t e = 0; e < effects.size(); ++e) {
			auto* effect = effects[e];
			if (!effect->IsCompiled())
				continue;

			for (auto& [path, gm] : effect->groupMeta)
				meta.try_emplace(path, gm);

			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];
				GroupNode* node = (!var.isTopLevel && !var.group.empty()) ? TraverseGroupPath(root, var.group) : &root;

				if (var.isSeparator) {
					if (node != &root)
						node->vars.push_back({ effect, i });
					continue;
				}
				if (!var.displayName.empty() && !seenDisplayNames[node].insert(var.displayName).second)
					continue;
				node->vars.push_back({ effect, i });
			}
		}
	}

	static void PropagateGroupOrdering(GroupNode& node, MergedGroupMeta& meta)
	{
		for (auto& child : node.children) {
			PropagateGroupOrdering(*child, meta);
			auto it = meta.find(child->fullPath);
			if (it != meta.end() && !it->second.hasOrdering && !child->children.empty()) {
				int maxChildOrder = 0;
				for (auto& gc : child->children) {
					auto gcIt = meta.find(gc->fullPath);
					if (gcIt != meta.end() && gcIt->second.ordering > maxChildOrder)
						maxChildOrder = gcIt->second.ordering;
				}
				if (maxChildOrder > 0) {
					it->second.ordering = maxChildOrder;
					it->second.hasOrdering = true;
				}
			}
		}
	}

	static void SortMergedTree(GroupNode& node, const MergedGroupMeta& meta)
	{
		std::stable_sort(node.vars.begin(), node.vars.end(), [](const VarRef& a, const VarRef& b) {
			auto& va = a.effect->uiVariables[a.index];
			auto& vb = b.effect->uiVariables[b.index];
			if (va.ordering != vb.ordering) return va.ordering > vb.ordering;
			if (va.sourceOrder != vb.sourceOrder) return va.sourceOrder < vb.sourceOrder;
			return false;
		});
		std::stable_sort(node.children.begin(), node.children.end(), [&](const auto& a, const auto& b) {
			auto itA = meta.find(a->fullPath), itB = meta.find(b->fullPath);
			int oA = (itA != meta.end()) ? itA->second.ordering : 0;
			int oB = (itB != meta.end()) ? itB->second.ordering : 0;
			return oA > oB;
		});
		for (auto& child : node.children)
			SortMergedTree(*child, meta);
	}

	// Root separator mapping

	static std::unordered_set<std::string> MapRootSeparators(std::span<Effect*> effects, const GroupNode& root)
	{
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
			std::function<void(const GroupNode&)> collect = [&](const GroupNode& n) {
				for (auto& ref : n.vars) {
					int so = ref.effect->uiVariables[ref.index].sourceOrder;
					auto [it, ins] = ci.minOrders.try_emplace(ref.effect, so);
					if (!ins && so < it->second)
						it->second = so;
				}
				for (auto& c : n.children)
					collect(*c);
			};
			collect(*child);
			childInfos.push_back(std::move(ci));
		}

		std::unordered_set<std::string> result;
		for (auto* effect : effects) {
			if (!effect->IsCompiled())
				continue;
			for (auto& var : effect->uiVariables) {
				if (!var.isSeparator || !var.group.empty())
					continue;
				int bestMinSO = INT_MAX;
				int bestIdx = -1;
				bool hasGroupBefore = false;
				for (size_t ci = 0; ci < childInfos.size(); ++ci) {
					auto it = childInfos[ci].minOrders.find(effect);
					if (it == childInfos[ci].minOrders.end())
						continue;
					if (it->second <= var.sourceOrder)
						hasGroupBefore = true;
					if (it->second > var.sourceOrder && it->second < bestMinSO) {
						bestMinSO = it->second;
						bestIdx = static_cast<int>(ci);
					}
				}
				if (bestIdx >= 0 && hasGroupBefore)
					result.insert(childInfos[bestIdx].fullPath);
			}
		}
		return result;
	}

	// UI rendering

	struct RenderContext
	{
		std::unordered_map<std::string, VarRef>& uniqueNameMap;
		FileUniqueNameMap& fileUniqueNameMap;
		std::unordered_set<Effect*>& changedEffects;
		MergedGroupMeta& meta;
		std::unordered_set<std::string>& separatorsBeforeGroup;
		bool performanceMode = false;
		int tableCounter = 0;

		bool BeginVarTable()
		{
			std::string tableId = "##ut_" + std::to_string(tableCounter++);
			if (ImGui::BeginTable(tableId.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
				float w = ImGui::GetContentRegionAvail().x;
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, w * 0.45f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, w * 0.55f);
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
					int di = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? uiVar.intValue + 1 : uiVar.intValue;
					const char* cur = (di >= 0 && di < static_cast<int>(uiVar.dropdownItems.size())) ? uiVar.dropdownItems[di].c_str() : "";
					if (ImGui::BeginCombo(id.c_str(), cur)) {
						for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
							int iv = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? (j - 1) : j;
							if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), uiVar.intValue == iv)) {
								uiVar.intValue = iv;
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
				changed = (uiVar.widgetType == Effect::UIWidgetType::Vector)
				              ? ImGui::SliderFloat3(id.c_str(), uiVar.colorValue, -1.0f, 1.0f, "%.3f")
				              : ImGui::ColorEdit3(id.c_str(), uiVar.colorValue);
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

	static void RenderVar(VarRef& ref, bool& inTable, bool& lastWasSeparator, RenderContext& ctx)
	{
		auto& uiVar = ref.effect->uiVariables[ref.index];

		if (uiVar.isSeparator) {
			if (!lastWasSeparator) {
				if (inTable) { ImGui::EndTable(); inTable = false; }
				ImGui::Separator();
				lastWasSeparator = true;
			}
			return;
		}

		if (uiVar.displayName.empty() || uiVar.isHidden || uiVar.isWeatherOnlyString)
			return;
		if (ctx.performanceMode && !uiVar.ignorePerfMode)
			return;

		auto [bindVisible, bindReadOnly] = EvaluateBinding(uiVar, ctx.uniqueNameMap, ctx.fileUniqueNameMap);
		if (!bindVisible)
			return;

		lastWasSeparator = false;

		if (IsLabelOnly(uiVar)) {
			if (inTable) { ImGui::EndTable(); inTable = false; }
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
			RenderWidget(uiVar.displayName, "##uv_" + std::to_string(ref.index) + "_" + ref.effect->GetName(),
				uiVar, bindReadOnly, ref.effect, ctx.changedEffects);
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
		for (auto& [effect, group] : techDropdowns)
			if (!group.empty() && group == node.fullPath)
				RenderTechDropdown(effect, ctx.changedEffects);

		if (!node.vars.empty()) {
			bool inTable = false, lastWasSeparator = false;
			for (auto& ref : node.vars)
				RenderVar(ref, inTable, lastWasSeparator, ctx);
			if (inTable)
				ImGui::EndTable();
		}

		for (auto& child : node.children) {
			if (ctx.separatorsBeforeGroup.count(child->fullPath))
				ImGui::Separator();
			std::string displayName = child->name;
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
			auto metaIt = ctx.meta.find(child->fullPath);
			if (metaIt != ctx.meta.end()) {
				if (!metaIt->second.displayName.empty())
					displayName = metaIt->second.displayName;
				if (!metaIt->second.defaultOpen)
					flags = ImGuiTreeNodeFlags_None;
			}
			if (ImGui::TreeNodeEx((displayName + "###ugrp_" + child->fullPath).c_str(), flags)) {
				RenderGroupNode(*child, ctx, techDropdowns);
				ImGui::TreePop();
			}
		}
	}

	void RenderUI(std::span<Effect*> effects)
	{
		std::unordered_map<std::string, VarRef> uniqueNameMap;
		FileUniqueNameMap fileUniqueNameMap;
		BuildUniqueNameMap(effects, uniqueNameMap, fileUniqueNameMap);

		GroupNode root;
		MergedGroupMeta meta;
		BuildMergedTree(effects, root, meta);
		PropagateGroupOrdering(root, meta);
		SortMergedTree(root, meta);
		auto separatorsBeforeGroup = MapRootSeparators(effects, root);

		std::unordered_set<Effect*> changedEffects;
		RenderContext ctx{ uniqueNameMap, fileUniqueNameMap, changedEffects, meta, separatorsBeforeGroup,
			EffectManager::GetSingleton().performanceMode };

		std::vector<std::pair<Effect*, std::string>> techDropdowns;
		for (auto* effect : effects) {
			if (!effect->IsCompiled() || effect->uiTechniques.size() <= 1 || !effect->techniqueDropdownVisible)
				continue;
			techDropdowns.push_back({ effect, effect->techniqueDropdownGroup });
			if (!effect->techniqueDropdownGroup.empty()) {
				auto [it, inserted] = meta.try_emplace(effect->techniqueDropdownGroup);
				if (inserted) {
					it->second.displayName = effect->techniqueDropdownGroupName;
					it->second.defaultOpen = effect->techniqueDropdownGroupOpen;
					it->second.ordering = effect->techniqueDropdownOrdering;
					it->second.hasOrdering = true;
				}
				TraverseGroupPath(root, effect->techniqueDropdownGroup);
			}
		}

		for (auto& [effect, group] : techDropdowns)
			if (effect->techniqueDropdownTopLevel || group.empty())
				RenderTechDropdown(effect, changedEffects);

		RenderGroupNode(root, ctx, techDropdowns);

		for (auto* effect : changedEffects)
			effect->UpdateUIVariables();

		for (auto* effect : effects) {
			if (!effect->GetErrors().empty()) {
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s:", effect->GetName().c_str());
				for (const auto& err : effect->GetErrors())
					ImGui::TextWrapped("%s", err.c_str());
			}
		}
	}

	void RenderUI(Effect& effect)
	{
		Effect* ptr = &effect;
		RenderUI({ &ptr, 1 });
	}

	// Post-load processing

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
		auto* tech = fx->GetTechniqueByIndex(0);
		if (!tech || !tech->IsValid())
			return;

		auto get = [&](const char* name) { return Effect::GetTechniqueAnnotation(tech, name); };
		auto s = get("UIDropdownName");
		if (!s.empty()) effect.techniqueDropdownName = s;
		s = get("UIDropdownGroup");
		if (!s.empty()) effect.techniqueDropdownGroup = s;
		s = get("UIDropdownGroupName");
		if (!s.empty()) effect.techniqueDropdownGroupName = s;
		s = get("UIDropdownGroupOpen");
		if (!s.empty()) effect.techniqueDropdownGroupOpen = IsTruthy(s);
		s = get("UIDropdownVisible");
		if (!s.empty()) effect.techniqueDropdownVisible = IsTruthy(s);
		s = get("UIDropdownTopLevel");
		if (!s.empty()) effect.techniqueDropdownTopLevel = IsTruthy(s);
		s = get("UIDropdownOrdering");
		if (!s.empty()) effect.techniqueDropdownOrdering = SafeStoi(s, effect.techniqueDropdownOrdering);
	}

	// Time-of-day interpolation

	static float GetPeriodWeight(const std::string& period, const EffectManager::CommonVariableData& cd)
	{
		if (period == "Dawn") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Dawn)];
		if (period == "Sunrise") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunrise)];
		if (period == "Day") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Day)];
		if (period == "Sunset") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunset)];
		if (period == "Dusk") return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Dusk)];
		if (period == "Night") return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Night)];
		if (period == "Interior") return cd.eInteriorFactor;
		return 0.0f;
	}

	void ApplyTimeOfDayInterpolation(Effect& effect)
	{
		struct PeriodVar { size_t index; float weight; };
		std::unordered_map<std::string, std::vector<PeriodVar>> baseGroups;
		auto& cd = EffectManager::GetSingleton().commonData;

		for (size_t i = 0; i < effect.uiVariables.size(); ++i) {
			auto& uiVar = effect.uiVariables[i];
			if (uiVar.timePeriod.empty() || uiVar.isSeparator || !uiVar.effectVariable)
				continue;
			auto& name = uiVar.name;
			auto& period = uiVar.timePeriod;
			if (name.size() <= period.size() || name.compare(name.size() - period.size(), period.size(), period) != 0)
				continue;
			baseGroups[name.substr(0, name.size() - period.size())].push_back({ i, GetPeriodWeight(period, cd) });
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

			if (firstVar.type == Effect::UIVariableType::Int || firstVar.type == Effect::UIVariableType::Bool) {
				size_t bestIdx = entries[0].index;
				float bestWeight = entries[0].weight;
				for (size_t i = 1; i < entries.size(); ++i)
					if (entries[i].weight > bestWeight) { bestWeight = entries[i].weight; bestIdx = entries[i].index; }
				auto& best = effect.uiVariables[bestIdx];
				switch (best.type) {
				case Effect::UIVariableType::Int: baseVar->AsScalar()->SetInt(best.intValue); break;
				case Effect::UIVariableType::Bool: baseVar->AsScalar()->SetBool(best.boolValue); break;
				default: break;
				}
				continue;
			}

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

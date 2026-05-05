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

	static Effect::UIWidgetType ParseWidgetType(const std::string& widget)
	{
		std::string lower = widget;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		if (lower == "dropdown") return Effect::UIWidgetType::Dropdown;
		if (lower == "vector") return Effect::UIWidgetType::Vector;
		if (lower == "quality") return Effect::UIWidgetType::Quality;
		if (lower == "color") return Effect::UIWidgetType::Color;
		return Effect::UIWidgetType::Default;
	}

	static std::vector<std::string> ParseDropdownList(const std::string& list)
	{
		std::vector<std::string> items;
		std::stringstream ss(list);
		std::string item;
		while (std::getline(ss, item, ',')) {
			Trim(item);
			items.push_back(item);
		}
		return items;
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
				std::string combined;
				size_t cur = vs;
				while (cur < annotations.size() && annotations[cur] == '"') {
					size_t qe = annotations.find('"', cur + 1);
					if (qe == std::string::npos)
						return combined;
					combined += annotations.substr(cur + 1, qe - cur - 1);
					cur = qe + 1;
					while (cur < annotations.size() && (annotations[cur] == ' ' || annotations[cur] == '\t'))
						++cur;
				}
				return combined;
			}
			size_t ve = annotations.find_first_of(";>", vs);
			std::string val = (ve != std::string::npos) ? annotations.substr(vs, ve - vs) : annotations.substr(vs);
			Trim(val);
			return val;
		}
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
			bool alreadyExists = std::any_of(uiDefines.begin(), uiDefines.end(),
				[&](const Effect::UIDefineInfo& existing) { return existing.defineName == defineName; });
			if (!alreadyExists) {
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
				auto orderStr = ann("UIOrdering");
				if (!orderStr.empty()) {
					info.ordering = SafeStoi(orderStr);
					info.hasExplicitOrdering = true;
				}
				uiDefines.push_back(std::move(info));
			}
		}

		result += "#define " + defineName + " " + finalVal + "\n";
		return true;
	}

	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, std::vector<Effect::UIDefineInfo>& uiDefines, const std::string& iniPath, const std::string& iniSection)
	{
		std::string result;
		result.reserve(content.size());
		result += "#define ENB_EXT_VER 1\n";
		std::istringstream stream(content);
		std::string line;

		while (std::getline(stream, line)) {
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

	// Source-based group scoping (compiled effect reorders variable types, so source text is the ground truth for declaration order)

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
				std::string groupName;
				size_t gt = line.rfind('>');
				size_t q1 = (gt != std::string::npos) ? line.find('"', gt) : std::string::npos;
				size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
				if (q2 != std::string::npos)
					groupName = line.substr(q1 + 1, q2 - q1 - 1);
				if (groupName.empty())
					groupName = ExtractAnnotation(line, "UIGroup");
				if (!groupName.empty()) {
					groupStack.push_back(groupName);
					auto groupPath = BuildGroupPath(groupStack);
					auto& gm = effect.groupMeta[groupPath];
					auto orderStr = ExtractAnnotation(line, "UIOrdering");
					if (!orderStr.empty()) {
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
					auto [it, inserted] = effect.sourceOrderMap.try_emplace(varName, declarationIndex);
					if (inserted) {
						declarationIndex++;
						if (!groupStack.empty())
							effect.sourceGroupMap[varName] = BuildGroupPath(groupStack);
					}
				}
			}
		}
	}

	// Group metadata tracking

	static void CollectGroupMeta(const std::string& groupPath, ID3DX11EffectVariable* variable, Effect& effect)
	{
		if (groupPath.empty())
			return;
		auto& gm = effect.groupMeta[groupPath];
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };
		if (gm.displayName.empty()) {
			auto s = get("UIGroupName");
			if (!s.empty())
				gm.displayName = s;
		}
		if (!gm.defaultOpen) {
			auto s = get("UIGroupOpen");
			if (!s.empty())
				gm.defaultOpen = IsTruthy(s);
		}
		if (!gm.hasOrdering) {
			auto s = get("UIOrdering");
			if (!s.empty()) {
				gm.ordering = SafeStoi(s);
				gm.hasOrdering = true;
			}
		}
		if (!gm.isTopLevel) {
			auto s = get("UITopLevel");
			if (!s.empty())
				gm.isTopLevel = IsTruthy(s);
		}
	}

	// Shared annotation application (file-local, used by CreateUIVariable and ProcessExtenderStringVariable)

	static void ApplyAnnotations(Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };

		auto explicitGroup = get("UIGroup");
		uiVar.group = ResolveGroup(uiVar.name, explicitGroup, groupStack, effect);
		uiVar.sourceOrder = GetSourceOrder(uiVar.name, effect);

		auto orderStr = get("UIOrdering");
		if (!orderStr.empty())
			uiVar.ordering = SafeStoi(orderStr);

		uiVar.isReadOnly = IsTruthy(get("UIReadOnly"));
		auto hiddenStr = get("UIHidden"), visibleStr = get("UIVisible");
		uiVar.isHidden = IsTruthy(hiddenStr) || (!visibleStr.empty() && !IsTruthy(visibleStr));

		CollectGroupMeta(uiVar.group, variable, effect);
		if (IsTruthy(get("UITopLevel")) && explicitGroup.empty())
			uiVar.group.clear();

		uiVar.uniqueName = get("UniqueName");
		uiVar.uiBinding = get("UIBinding");
		uiVar.uiBindingFile = get("UIBindingFile");
		uiVar.uiBindingProperty = get("UIBindingProperty");
		uiVar.uiBindingCondition = get("UIBindingCondition");
		uiVar.ignorePerfMode = IsTruthy(get("UIIgnorePerfMode"));
		uiVar.isWeatherOnlyString = IsTruthy(get("UIWeatherOnlyString"));
		if (uiVar.type == Effect::UIVariableType::Float || uiVar.type == Effect::UIVariableType::Float2 ||
			uiVar.type == Effect::UIVariableType::Float3 || uiVar.type == Effect::UIVariableType::Float4)
			uiVar.separation = get("Separation");
	}

	static void ParseTimePeriod(Effect::UIVariable& uiVar)
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

	// CreateUIVariable — consolidated annotation reading for compiled effect variables

	bool CreateUIVariable(Effect::UIVariable& out, ID3DX11EffectVariable* variable,
		const D3DX11_EFFECT_VARIABLE_DESC& varDesc, const D3DX11_EFFECT_TYPE_DESC& typeDesc,
		const std::vector<std::string>& groupStack, Effect& effect)
	{
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };

		std::string uiName = get("UIName");
		if (uiName.empty())
			return false;

		out = {};
		out.name = varDesc.Name;
		out.displayName = uiName;
		out.effectVariable.copy_from(variable);

		if (typeDesc.Class == D3D_SVC_SCALAR) {
			switch (typeDesc.Type) {
			case D3D_SVT_FLOAT: out.type = Effect::UIVariableType::Float; break;
			case D3D_SVT_INT: out.type = Effect::UIVariableType::Int; break;
			case D3D_SVT_BOOL: out.type = Effect::UIVariableType::Bool; break;
			default: return false;
			}
		} else if (typeDesc.Class == D3D_SVC_VECTOR && typeDesc.Type == D3D_SVT_FLOAT && typeDesc.Elements == 0) {
			if (typeDesc.Columns == 2) out.type = Effect::UIVariableType::Float2;
			else if (typeDesc.Columns == 3) out.type = Effect::UIVariableType::Float3;
			else if (typeDesc.Columns == 4) out.type = Effect::UIVariableType::Float4;
			else return false;
		} else {
			return false;
		}

		out.widgetType = ParseWidgetType(get("UIWidget"));

		if (out.type == Effect::UIVariableType::Float || out.type == Effect::UIVariableType::Float2 ||
			out.type == Effect::UIVariableType::Float3 || out.type == Effect::UIVariableType::Float4) {
			auto s = get("UIMin"); if (!s.empty()) out.floatMin = SafeStof(s, out.floatMin);
			s = get("UIMax"); if (!s.empty()) out.floatMax = SafeStof(s, out.floatMax);
		} else if (out.type == Effect::UIVariableType::Int) {
			auto s = get("UIMin"); if (!s.empty()) out.intMin = SafeStoi(s, out.intMin);
			s = get("UIMax"); if (!s.empty()) out.intMax = SafeStoi(s, out.intMax);
			if (out.widgetType == Effect::UIWidgetType::Dropdown) {
				s = get("UIList"); if (!s.empty()) out.dropdownItems = ParseDropdownList(s);
			} else if (out.widgetType == Effect::UIWidgetType::Quality) {
				out.dropdownItems = { "Very High", "High", "Medium", "Low", "Very Low" };
				out.intMin = -1;
				out.intMax = 3;
			}
		}

		ApplyAnnotations(out, variable, groupStack, effect);
		if (out.isHidden)
			return false;

		ParseTimePeriod(out);
		return true;
	}

	// ProcessExtenderStringVariable

	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc,
		std::vector<std::string>& groupStack, Effect& effect)
	{
		auto get = [&](const char* name) { return effect.GetUIAnnotation(variable, name); };

		if (IsTruthy(get("UIGroupBegin"))) {
			std::string groupName = GetStringVariableValue(variable);
			if (groupName.empty())
				groupName = get("UIGroup");
			if (!groupName.empty()) {
				groupStack.push_back(groupName);
				CollectGroupMeta(BuildGroupPath(groupStack), variable, effect);
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
		Trim(labelText);
		if (labelText.empty())
			return true;

		Effect::UIVariable labelVar = {};
		labelVar.name = varDesc.Name;
		labelVar.displayName = labelText;
		labelVar.isLabel = true;
		ApplyAnnotations(labelVar, variable, groupStack, effect);
		if (!labelVar.isHidden)
			effect.uiVariables.push_back(labelVar);
		return true;
	}

	// InsertUIDefines

	void InsertUIDefines(Effect& effect)
	{
		int defineSourceOrder = -static_cast<int>(effect.uiDefines.size());
		for (const auto& def : effect.uiDefines) {
			Effect::UIVariable uiVar = {};
			uiVar.name = def.defineName;
			uiVar.displayName = def.displayName;
			uiVar.group = def.group;
			uiVar.ordering = def.ordering;
			uiVar.sourceOrder = defineSourceOrder++;
			uiVar.isDefine = true;

			if (def.type == "bool") {
				uiVar.type = Effect::UIVariableType::Bool;
				uiVar.boolValue = (def.value != "0");
			} else if (def.type == "int") {
				uiVar.type = Effect::UIVariableType::Int;
				uiVar.intValue = SafeStoi(def.value);
				uiVar.intMin = def.intMin;
				uiVar.intMax = def.intMax;
				uiVar.widgetType = ParseWidgetType(def.widget);
				if (uiVar.widgetType == Effect::UIWidgetType::Dropdown && !def.list.empty())
					uiVar.dropdownItems = ParseDropdownList(def.list);
				else if (uiVar.widgetType == Effect::UIWidgetType::Quality) {
					uiVar.dropdownItems = { "Very High", "High", "Medium", "Low", "Very Low" };
					uiVar.intMin = -1;
					uiVar.intMax = 3;
				}
			} else {
				uiVar.type = Effect::UIVariableType::Float;
				uiVar.floatValue = SafeStof(def.value);
				uiVar.floatMin = def.floatMin;
				uiVar.floatMax = def.floatMax;
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

	// Tree construction

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

	using GroupMetaMap = std::unordered_map<std::string, Effect::GroupMeta>;

	static GroupNode* TraverseGroupPath(GroupNode& root, const std::string& groupPath, const GroupMetaMap& meta = {})
	{
		GroupNode* node = &root;
		size_t start = 0;
		while (start < groupPath.size()) {
			size_t dot = groupPath.find('.', start);
			if (dot == std::string::npos)
				dot = groupPath.size();
			std::string fullPath = groupPath.substr(0, dot);
			auto metaIt = meta.find(fullPath);
			if (metaIt != meta.end() && metaIt->second.isTopLevel)
				node = &root;
			node = FindOrCreateChild(*node, groupPath.substr(start, dot - start), fullPath);
			start = dot + 1;
		}
		return node;
	}

	static void BuildUITree(std::span<Effect*> effects, GroupNode& root, GroupMetaMap& meta,
		std::unordered_map<std::string, VarRef>& uniqueNameMap, FileUniqueNameMap& fileUniqueNameMap)
	{
		std::unordered_map<GroupNode*, std::unordered_set<std::string>> seenDisplayNames;
		std::unordered_map<GroupNode*, std::unordered_set<int>> seenSepOrders;

		for (auto* effect : effects) {
			if (!effect->IsCompiled())
				continue;

			for (auto& [path, gm] : effect->groupMeta) {
				auto [it, inserted] = meta.try_emplace(path, gm);
				if (!inserted) {
					if (it->second.displayName.empty() && !gm.displayName.empty())
						it->second.displayName = gm.displayName;
					if (!it->second.defaultOpen && gm.defaultOpen)
						it->second.defaultOpen = gm.defaultOpen;
					if (!it->second.hasOrdering && gm.hasOrdering) {
						it->second.ordering = gm.ordering;
						it->second.hasOrdering = true;
					}
					if (!it->second.isTopLevel && gm.isTopLevel)
						it->second.isTopLevel = true;
				}
			}

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

				GroupNode* node = !var.group.empty() ? TraverseGroupPath(root, var.group, meta) : &root;

				if (var.isSeparator) {
					if (seenSepOrders[node].insert(var.sourceOrder).second)
						node->vars.push_back({ effect, i });
					continue;
				}
				if (!var.displayName.empty() && !seenDisplayNames[node].insert(var.displayName).second)
					continue;
				node->vars.push_back({ effect, i });
			}
		}
	}

	static void InheritChildOrdering(GroupNode& node, GroupMetaMap& meta)
	{
		for (auto& child : node.children) {
			InheritChildOrdering(*child, meta);
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

	static void ApplyUIOrdering(GroupNode& node, const GroupMetaMap& meta)
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
			ApplyUIOrdering(*child, meta);
	}

	// UI rendering

	static int ComputeMinSourceOrder(const GroupNode& node)
	{
		int minSO = INT_MAX;
		for (auto& ref : node.vars) {
			int so = ref.effect->uiVariables[ref.index].sourceOrder;
			if (so < minSO)
				minSO = so;
		}
		for (auto& c : node.children) {
			int cso = ComputeMinSourceOrder(*c);
			if (cso < minSO)
				minSO = cso;
		}
		return minSO;
	}

	struct RenderContext
	{
		std::unordered_map<std::string, VarRef>& uniqueNameMap;
		FileUniqueNameMap& fileUniqueNameMap;
		std::unordered_set<Effect*>& changedEffects;
		GroupMetaMap& meta;
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
		case Effect::UIVariableType::Float2:
			changed = ImGui::SliderFloat2(id.c_str(), uiVar.vectorValue, uiVar.floatMin, uiVar.floatMax, "%.3f");
			break;
		case Effect::UIVariableType::Float3:
			if (uiVar.widgetType == Effect::UIWidgetType::Color)
				changed = ImGui::ColorEdit3(id.c_str(), uiVar.vectorValue);
			else if (uiVar.widgetType == Effect::UIWidgetType::Vector)
				changed = ImGui::SliderFloat3(id.c_str(), uiVar.vectorValue, -1.0f, 1.0f, "%.3f");
			else
				changed = ImGui::SliderFloat3(id.c_str(), uiVar.vectorValue, uiVar.floatMin, uiVar.floatMax, "%.3f");
			break;
		case Effect::UIVariableType::Float4:
			if (uiVar.widgetType == Effect::UIWidgetType::Color)
				changed = ImGui::ColorEdit4(id.c_str(), uiVar.vectorValue);
			else
				changed = ImGui::SliderFloat4(id.c_str(), uiVar.vectorValue, uiVar.floatMin, uiVar.floatMax, "%.3f");
			break;
		}
		if (changed)
			changedEffects.insert(effect);
		if (readOnly)
			ImGui::EndDisabled();

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

		if (uiVar.isLabel) {
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

	static void RenderTechniqueDropdown(Effect* effect, std::unordered_set<Effect*>& changedEffects)
	{
		ImGui::Text("%s", effect->techniqueDropdown.name.c_str());
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
			if (!group.empty() && group == node.fullPath && !effect->techniqueDropdown.topLevel)
				RenderTechniqueDropdown(effect, ctx.changedEffects);

		int firstChildMinSO = INT_MAX;
		for (auto& child : node.children) {
			int cso = ComputeMinSourceOrder(*child);
			if (cso < firstChildMinSO)
				firstChildMinSO = cso;
		}

		std::vector<VarRef*> interGroupSeps;
		bool lastWasSeparator = false;

		if (!node.vars.empty()) {
			bool inTable = false;
			for (auto& ref : node.vars) {
				auto& var = ref.effect->uiVariables[ref.index];
				if (var.isSeparator && var.sourceOrder >= firstChildMinSO) {
					interGroupSeps.push_back(&ref);
					continue;
				}
				RenderVar(ref, inTable, lastWasSeparator, ctx);
			}
			if (inTable)
				ImGui::EndTable();
		}

		std::sort(interGroupSeps.begin(), interGroupSeps.end(), [](const VarRef* a, const VarRef* b) {
			return a->effect->uiVariables[a->index].sourceOrder < b->effect->uiVariables[b->index].sourceOrder;
		});

		size_t sepIdx = 0;
		for (auto& child : node.children) {
			int childMinSO = ComputeMinSourceOrder(*child);
			while (sepIdx < interGroupSeps.size()) {
				int sepSO = interGroupSeps[sepIdx]->effect->uiVariables[interGroupSeps[sepIdx]->index].sourceOrder;
				if (sepSO < childMinSO) {
					if (!lastWasSeparator)
						ImGui::Separator();
					lastWasSeparator = true;
					sepIdx++;
				} else {
					break;
				}
			}
			lastWasSeparator = false;

			std::string displayName = child->name;
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
			auto metaIt = ctx.meta.find(child->fullPath);
			if (metaIt != ctx.meta.end()) {
				if (!metaIt->second.displayName.empty())
					displayName = metaIt->second.displayName;
				if (metaIt->second.defaultOpen)
					flags = ImGuiTreeNodeFlags_DefaultOpen;
			}
			if (ImGui::TreeNodeEx((displayName + "###ugrp_" + child->fullPath).c_str(), flags)) {
				RenderGroupNode(*child, ctx, techDropdowns);
				ImGui::TreePop();
			}
		}

		while (sepIdx < interGroupSeps.size()) {
			if (!lastWasSeparator)
				ImGui::Separator();
			lastWasSeparator = true;
			sepIdx++;
		}
	}

	void RenderUI(std::span<Effect*> effects)
	{
		std::unordered_map<std::string, VarRef> uniqueNameMap;
		FileUniqueNameMap fileUniqueNameMap;

		GroupNode root;
		GroupMetaMap meta;
		BuildUITree(effects, root, meta, uniqueNameMap, fileUniqueNameMap);

		std::vector<std::pair<Effect*, std::string>> techDropdowns;
		for (auto* effect : effects) {
			if (!effect->IsCompiled() || effect->uiTechniques.size() <= 1 || !effect->techniqueDropdown.visible)
				continue;
			techDropdowns.push_back({ effect, effect->techniqueDropdown.group });
			if (!effect->techniqueDropdown.group.empty()) {
				auto [it, inserted] = meta.try_emplace(effect->techniqueDropdown.group);
				if (inserted) {
					it->second.displayName = effect->techniqueDropdown.groupName;
					it->second.defaultOpen = effect->techniqueDropdown.groupOpen;
					it->second.ordering = effect->techniqueDropdown.ordering;
					it->second.hasOrdering = true;
				}
				TraverseGroupPath(root, effect->techniqueDropdown.group, meta);
			}
		}

		InheritChildOrdering(root, meta);
		ApplyUIOrdering(root, meta);

		std::unordered_set<Effect*> changedEffects;
		RenderContext ctx{ uniqueNameMap, fileUniqueNameMap, changedEffects, meta,
			EffectManager::GetSingleton().performanceMode };

		for (auto& [effect, group] : techDropdowns)
			if (effect->techniqueDropdown.topLevel || group.empty())
				RenderTechniqueDropdown(effect, changedEffects);

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
		if (FAILED(fx->GetDesc(&effectDesc)))
			return;

		ID3DX11EffectGroup* annotationGroup = nullptr;
		ID3DX11EffectTechnique* annotationTechnique = nullptr;

		for (UINT g = 0; g < effectDesc.Groups; ++g) {
			auto* group = fx->GetGroupByIndex(g);
			if (!group || !group->IsValid())
				continue;
			D3DX11_GROUP_DESC groupDesc;
			if (FAILED(group->GetDesc(&groupDesc)))
				continue;

			if (groupDesc.Name && groupDesc.Name[0]) {
				annotationGroup = group;
				break;
			} else if (!annotationTechnique && groupDesc.Techniques > 0) {
				auto* tech = group->GetTechniqueByIndex(0);
				if (tech && tech->IsValid())
					annotationTechnique = tech;
			}
		}

		if (!annotationGroup && !annotationTechnique)
			return;

		auto get = [&](const char* name) -> std::string {
			if (annotationGroup)
				return Effect::GetGroupAnnotation(annotationGroup, name);
			return Effect::GetTechniqueAnnotation(annotationTechnique, name);
		};

		auto str = [&](const char* name, std::string& out) { auto s = get(name); if (!s.empty()) out = s; };
		auto flag = [&](const char* name, bool& out) { auto s = get(name); if (!s.empty()) out = IsTruthy(s); };

		auto& td = effect.techniqueDropdown;
		str("UIDropdownName", td.name);
		str("UIDropdownGroup", td.group);
		str("UIDropdownGroupName", td.groupName);
		flag("UIDropdownGroupOpen", td.groupOpen);
		flag("UIDropdownVisible", td.visible);
		flag("UIDropdownTopLevel", td.topLevel);
		auto s = get("UIDropdownOrdering");
		if (!s.empty())
			td.ordering = SafeStoi(s, td.ordering);
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

			auto& sep = effect.uiVariables[entries[0].index].separation;
			if (sep == "ExteriorWeather" && cd.eInteriorFactor > 0.0f)
				continue;

			float totalWeight = 0.0f;
			for (auto& e : entries)
				totalWeight += e.weight;
			if (totalWeight <= 0.0f)
				continue;

			auto& firstVar = effect.uiVariables[entries[0].index];

			if (firstVar.type == Effect::UIVariableType::Float) {
				float result = 0.0f;
				for (auto& e : entries)
					result += effect.uiVariables[e.index].floatValue * (e.weight / totalWeight);
				baseVar->AsScalar()->SetFloat(result);
			} else {
				int comps = (firstVar.type == Effect::UIVariableType::Float2) ? 2 : (firstVar.type == Effect::UIVariableType::Float3) ? 3 : 4;
				float result[4] = {};
				for (auto& e : entries) {
					float w = e.weight / totalWeight;
					for (int c = 0; c < comps; ++c)
						result[c] += effect.uiVariables[e.index].vectorValue[c] * w;
				}
				baseVar->AsVector()->SetFloatVector(result);
			}
		}
	}

}

#include "ShaderPatches.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Util::ShaderPatches
{
	static std::vector<Entry> entries;
	static bool loaded = false;
	static std::vector<UIDefineInfo> lastUIDefines;

	const std::vector<UIDefineInfo>& GetLastUIDefines()
	{
		return lastUIDefines;
	}

	void Load()
	{
		entries.clear();
		loaded = true;

		std::filesystem::path path = "Data\\Shaders\\Effect11\\ShaderPatches.json";
		std::ifstream ifs(path);
		if (!ifs.is_open())
			return;

		try {
			nlohmann::json root = nlohmann::json::parse(ifs);
			for (auto& item : root) {
				Entry entry;
				entry.file = item.at("file").get<std::string>();
				for (auto& r : item.at("patches")) {
					Replacement rep;
					rep.find = r.at("find").get<std::string>();
					rep.replace = r.at("replace").get<std::string>();
					entry.replacements.push_back(std::move(rep));
				}
				entries.push_back(std::move(entry));
			}
		} catch (const std::exception& e) {
			logger::error("[ShaderPatches] Failed to parse {}: {}", path.string(), e.what());
		}

		if (!entries.empty())
			logger::info("[ShaderPatches] Loaded {} entries", entries.size());
	}

	static bool FilenameMatches(const char* includePath, const std::string& pattern)
	{
		std::string path(includePath);
		for (auto& c : path)
			if (c == '/')
				c = '\\';

		std::string pat = pattern;
		for (auto& c : pat)
			if (c == '/')
				c = '\\';

		if (pat.size() > path.size())
			return false;

		auto pathEnd = path.end();
		auto pathStart = pathEnd - static_cast<ptrdiff_t>(pat.size());
		if (pathStart != path.begin() && *(pathStart - 1) != '\\')
			return false;

		return std::equal(pathStart, pathEnd, pat.begin(), pat.end(), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
	}

	bool Apply(const char* filename, std::string& content)
	{
		if (!loaded)
			Load();

		bool modified = false;
		for (auto& entry : entries) {
			if (!FilenameMatches(filename, entry.file))
				continue;
			for (auto& rep : entry.replacements) {
				size_t pos = 0;
				while ((pos = content.find(rep.find, pos)) != std::string::npos) {
					content.replace(pos, rep.find.size(), rep.replace);
					pos += rep.replace.size();
					modified = true;
				}
			}
		}

		if (modified)
			logger::debug("[ShaderPatches] Patched {}", filename);

		return modified;
	}

	bool Apply(const char* filename, std::vector<char>& buffer)
	{
		std::string content(buffer.begin(), buffer.end());
		if (Apply(filename, content)) {
			buffer.assign(content.begin(), content.end());
			return true;
		}
		return false;
	}

	std::string DecodeENBSource(const std::string& content)
	{
		static constexpr uint8_t magic[] = { 0x4B, 0x49, 0x45, 0x46, 0x58, 0x00, 0x01 };
		static constexpr uint8_t key[] = { 0xD8, 0x29, 0x09, 0x12, 0x64, 0x96, 0x6E, 0x2C };

		if (content.size() < sizeof(magic))
			return content;

		if (memcmp(content.data(), magic, sizeof(magic)) != 0)
			return content;

		std::string decoded;
		decoded.reserve(content.size() - sizeof(magic));

		for (size_t i = sizeof(magic); i < content.size(); ++i)
			decoded += static_cast<char>(static_cast<uint8_t>(content[i]) ^ key[(i - sizeof(magic)) % sizeof(key)]);

		return decoded;
	}

	static size_t FindMatchingBrace(const std::string& text, size_t openPos)
	{
		int depth = 1;
		for (size_t i = openPos + 1; i < text.size(); ++i) {
			if (text[i] == '{')
				++depth;
			else if (text[i] == '}') {
				--depth;
				if (depth == 0)
					return i;
			}
		}
		return std::string::npos;
	}

	void ConvertFxGroups(std::string& content)
	{
		int convertedCount = 0;
		size_t searchStart = 0;
		while (true) {
			size_t fxPos = content.find("fxgroup", searchStart);
			if (fxPos == std::string::npos)
				break;

			if (fxPos > 0 && (std::isalnum(static_cast<unsigned char>(content[fxPos - 1])) || content[fxPos - 1] == '_')) {
				searchStart = fxPos + 7;
				continue;
			}

			// Skip fxgroup inside #define directives
			size_t lineStart = content.rfind('\n', fxPos);
			lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
			size_t firstChar = content.find_first_not_of(" \t", lineStart);
			if (firstChar != std::string::npos && content[firstChar] == '#') {
				searchStart = fxPos + 7;
				continue;
			}
			// Also check if inside a multi-line macro (previous line ends with backslash)
			if (lineStart > 1) {
				size_t prevLineEnd = lineStart - 1;  // points to \n
				size_t checkPos = prevLineEnd;
				if (checkPos > 0 && content[checkPos - 1] == '\r')
					checkPos--;
				if (checkPos > 0 && content[checkPos - 1] == '\\') {
					searchStart = fxPos + 7;
					continue;
				}
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
			size_t bodyOpen;

			size_t angleBracket = content.find_first_not_of(" \t", nameEnd);
			if (angleBracket != std::string::npos && content[angleBracket] == '<') {
				size_t angleClose = std::string::npos;
				int depth = 1;
				for (size_t i = angleBracket + 1; i < content.size(); ++i) {
					if (content[i] == '<')
						++depth;
					else if (content[i] == '>') {
						--depth;
						if (depth == 0) {
							angleClose = i;
							break;
						}
					}
				}
				if (angleClose == std::string::npos) {
					searchStart = fxPos + 7;
					continue;
				}
				groupAnnotations = content.substr(angleBracket + 1, angleClose - angleBracket - 1);
				bodyOpen = content.find('{', angleClose + 1);
			} else {
				bodyOpen = content.find('{', nameEnd);
			}

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

			struct TechniqueInfo
			{
				std::string annotations;
				std::string body;
			};
			std::vector<TechniqueInfo> techniques;

			size_t techSearch = 0;
			while (true) {
				size_t techPos = body.find("technique11", techSearch);
				if (techPos == std::string::npos)
					break;

				if (techPos > 0 && (std::isalnum(static_cast<unsigned char>(body[techPos - 1])) || body[techPos - 1] == '_')) {
					techSearch = techPos + 11;
					continue;
				}

				size_t afterKeyword = techPos + 11;
				size_t nextNonSpace = body.find_first_not_of(" \t\r\n", afterKeyword);
				if (nextNonSpace == std::string::npos) {
					techSearch = afterKeyword;
					continue;
				}

				TechniqueInfo info;

				size_t techBodyOpen;
				if (body[nextNonSpace] == '<') {
					int aDepth = 1;
					size_t aClose = std::string::npos;
					for (size_t i = nextNonSpace + 1; i < body.size(); ++i) {
						if (body[i] == '<')
							++aDepth;
						else if (body[i] == '>') {
							--aDepth;
							if (aDepth == 0) {
								aClose = i;
								break;
							}
						}
					}
					if (aClose == std::string::npos) {
						techSearch = afterKeyword;
						continue;
					}
					info.annotations = body.substr(nextNonSpace + 1, aClose - nextNonSpace - 1);
					techBodyOpen = body.find('{', aClose + 1);
				} else if (body[nextNonSpace] == '{') {
					techBodyOpen = nextNonSpace;
				} else {
					size_t afterName = body.find_first_of("<{", nextNonSpace);
					if (afterName == std::string::npos) {
						techSearch = afterKeyword;
						continue;
					}
					if (body[afterName] == '<') {
						int aDepth = 1;
						size_t aClose = std::string::npos;
						for (size_t i = afterName + 1; i < body.size(); ++i) {
							if (body[i] == '<')
								++aDepth;
							else if (body[i] == '>') {
								--aDepth;
								if (aDepth == 0) {
									aClose = i;
									break;
								}
							}
						}
						if (aClose == std::string::npos) {
							techSearch = afterKeyword;
							continue;
						}
						info.annotations = body.substr(afterName + 1, aClose - afterName - 1);
						techBodyOpen = body.find('{', aClose + 1);
					} else {
						techBodyOpen = afterName;
					}
				}

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

				if (i == 0) {
					bool hasGroup = !groupAnnotations.empty();
					bool hasTech = !techniques[i].annotations.empty();
					if (hasGroup || hasTech) {
						replacement += " <";
						if (hasGroup)
							replacement += groupAnnotations;
						if (hasGroup && hasTech)
							replacement += " ";
						if (hasTech)
							replacement += techniques[i].annotations;
						replacement += ">";
					}
				} else if (!techniques[i].annotations.empty()) {
					replacement += " <" + techniques[i].annotations + ">";
				}

				replacement += " " + techniques[i].body + "\n";
			}

			logger::debug("[ShaderPatches] ConvertFxGroups: converted fxgroup '{}' with {} technique(s)", groupName, techniques.size());
			convertedCount++;
			content.replace(fxPos, bodyClose - fxPos + 1, replacement);
			searchStart = fxPos + replacement.size();
		}
		if (convertedCount > 0)
			logger::debug("[ShaderPatches] ConvertFxGroups: converted {} fxgroup(s) total", convertedCount);
	}

	static std::string ExtractAnnotationString(const std::string& annotations, const std::string& name)
	{
		size_t pos = 0;
		while (true) {
			pos = annotations.find(name, pos);
			if (pos == std::string::npos)
				return "";

			if (pos > 0 && (std::isalnum(static_cast<unsigned char>(annotations[pos - 1])) || annotations[pos - 1] == '_')) {
				pos += name.size();
				continue;
			}

			size_t afterName = pos + name.size();
			size_t eq = annotations.find('=', afterName);
			if (eq == std::string::npos)
				return "";

			size_t quoteStart = annotations.find('"', eq + 1);
			if (quoteStart == std::string::npos)
				return "";
			size_t quoteEnd = annotations.find('"', quoteStart + 1);
			if (quoteEnd == std::string::npos)
				return "";

			return annotations.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
		}
	}

	static std::string ExtractAnnotationValue(const std::string& annotations, const std::string& name)
	{
		size_t pos = 0;
		while (true) {
			pos = annotations.find(name, pos);
			if (pos == std::string::npos)
				return "";

			if (pos > 0 && (std::isalnum(static_cast<unsigned char>(annotations[pos - 1])) || annotations[pos - 1] == '_')) {
				pos += name.size();
				continue;
			}

			size_t afterName = pos + name.size();
			size_t eq = annotations.find('=', afterName);
			if (eq == std::string::npos)
				return "";

			// Check for string value (quoted)
			size_t valueStart = annotations.find_first_not_of(" \t", eq + 1);
			if (valueStart == std::string::npos)
				return "";

			if (annotations[valueStart] == '"') {
				size_t quoteEnd = annotations.find('"', valueStart + 1);
				if (quoteEnd == std::string::npos)
					return "";
				return annotations.substr(valueStart + 1, quoteEnd - valueStart - 1);
			}

			// Non-quoted: read until ; or end
			size_t valueEnd = annotations.find_first_of(";>", valueStart);
			std::string val;
			if (valueEnd != std::string::npos)
				val = annotations.substr(valueStart, valueEnd - valueStart);
			else
				val = annotations.substr(valueStart);

			// Trim
			val.erase(val.find_last_not_of(" \t") + 1);
			return val;
		}
	}

	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, const std::string& iniPath, const std::string& iniSection)
	{
		lastUIDefines.clear();

		std::string result;
		result.reserve(content.size());

		std::istringstream stream(content);
		std::string line;

		while (std::getline(stream, line)) {
			// Strip #error directives (extender presets use these to block non-extender compilation)
			{
				size_t pos = line.find_first_not_of(" \t#");
				if (pos != std::string::npos && line.compare(pos, 5, "error") == 0) {
					// Check it's actually a preprocessor directive
					size_t hashPos = line.find('#');
					if (hashPos != std::string::npos && hashPos < pos) {
						result += "\n";
						continue;
					}
				}
			}

			// Handle #pragma exists("path", DEFINE_NAME)
			// Checks if a file exists relative to enbseries folder and defines the variable
			{
				size_t pos = line.find("#pragma");
				if (pos != std::string::npos) {
					size_t existsPos = line.find("exists", pos + 7);
					if (existsPos != std::string::npos) {
						size_t openParen = line.find('(', existsPos);
						size_t closeParen = line.rfind(')');
						if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
							std::string args = line.substr(openParen + 1, closeParen - openParen - 1);

							size_t firstQuote = args.find('"');
							size_t secondQuote = args.find('"', firstQuote + 1);
							size_t comma = args.find(',', secondQuote + 1);

							if (firstQuote != std::string::npos && secondQuote != std::string::npos && comma != std::string::npos) {
								std::string filePath = args.substr(firstQuote + 1, secondQuote - firstQuote - 1);
								std::string defineName = args.substr(comma + 1);

								// Trim whitespace
								defineName.erase(0, defineName.find_first_not_of(" \t"));
								defineName.erase(defineName.find_last_not_of(" \t") + 1);

								bool exists = std::filesystem::exists(enbseriesPath / filePath);
								result += "#define " + defineName + (exists ? " 1" : " 0") + "\n";
								continue;
							}
						}
					}
				}
			}

			// Handle #pragma uidefine(type NAME<annotations> =DEFAULT)
			// Convert to #define NAME VALUE (reads from INI if available)
			{
				size_t pos = line.find("pragma");
				if (pos != std::string::npos) {
					size_t uidefPos = line.find("uidefine", pos + 6);
					if (uidefPos != std::string::npos) {
						// Join continuation lines (\) to handle multi-line uidefines
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
						if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
							std::string inner = line.substr(openParen + 1, closeParen - openParen - 1);

							// Skip type keyword (bool, int, float, etc.)
							size_t typeEnd = inner.find_first_of(" \t");
							if (typeEnd == std::string::npos) {
								result += line + "\n";
								continue;
							}
							size_t nameStart = inner.find_first_not_of(" \t", typeEnd);
							if (nameStart == std::string::npos) {
								result += line + "\n";
								continue;
							}

							// Name ends at '<' (annotations) or '=' (default value)
							size_t nameEnd = inner.find_first_of("<=", nameStart);
							std::string defineName;
							if (nameEnd != std::string::npos) {
								defineName = inner.substr(nameStart, nameEnd - nameStart);
							} else {
								defineName = inner.substr(nameStart);
							}
							defineName.erase(defineName.find_last_not_of(" \t") + 1);

							// Extract annotations from <...> block
							std::string annotations;
							size_t angleOpen = inner.find('<', nameStart);
							size_t angleBracketClose = inner.rfind('>');
							if (angleOpen != std::string::npos && angleBracketClose != std::string::npos && angleBracketClose > angleOpen) {
								annotations = inner.substr(angleOpen + 1, angleBracketClose - angleOpen - 1);
							}

							// Find default value after annotations
							size_t equalsPos;
							if (angleBracketClose != std::string::npos) {
								equalsPos = inner.find('=', angleBracketClose);
							} else {
								equalsPos = inner.rfind('=');
							}

							std::string defaultVal = "0";
							if (equalsPos != std::string::npos) {
								defaultVal = inner.substr(equalsPos + 1);
								defaultVal.erase(0, defaultVal.find_first_not_of(" \t"));
								defaultVal.erase(defaultVal.find_last_not_of(" \t;") + 1);

								if (defaultVal == "false")
									defaultVal = "0";
								else if (defaultVal == "true")
									defaultVal = "1";
							}

							std::string typeName = inner.substr(0, typeEnd);
							typeName.erase(0, typeName.find_first_not_of(" \t"));

							std::string uiName = ExtractAnnotationString(annotations, "UIName");
							std::string uiGroup = ExtractAnnotationString(annotations, "UIGroup");

							// Try reading value from INI file using UIName/UIGroup as the key
							std::string finalVal = defaultVal;
							if (!iniPath.empty() && !iniSection.empty() && !uiName.empty()) {
								std::string iniKey = uiGroup.empty() ? uiName : (uiGroup + "." + uiName);
								std::vector<char> valueBuffer(1024);
								DWORD iniResult = GetPrivateProfileStringA(iniSection.c_str(), iniKey.c_str(), "", valueBuffer.data(), 1024, iniPath.c_str());
								if (iniResult > 0) {
									std::string iniVal(valueBuffer.data());
									iniVal.erase(0, iniVal.find_first_not_of(" \t"));
									iniVal.erase(iniVal.find_last_not_of(" \t") + 1);

									if (iniVal == "false")
										iniVal = "0";
									else if (iniVal == "true")
										iniVal = "1";

									finalVal = iniVal;
									logger::debug("[ShaderPatches] uidefine '{}' = {} (from INI key '{}')", defineName, finalVal, iniKey);
								}
							}

							// Collect metadata for UI display
							if (!uiName.empty()) {
								UIDefineInfo info;
								info.defineName = defineName;
								info.displayName = uiName;
								info.group = uiGroup;
								info.type = typeName;
								info.value = finalVal;
								info.widget = ExtractAnnotationString(annotations, "UIWidget");
								info.list = ExtractAnnotationString(annotations, "UIList");

								std::string minStr = ExtractAnnotationValue(annotations, "UIMin");
								std::string maxStr = ExtractAnnotationValue(annotations, "UIMax");
								std::string stepStr = ExtractAnnotationValue(annotations, "UIStep");
								std::string orderStr = ExtractAnnotationValue(annotations, "UIOrdering");

								try {
									if (!minStr.empty()) {
										if (typeName == "int")
											info.intMin = std::stoi(minStr);
										else
											info.floatMin = std::stof(minStr);
									}
									if (!maxStr.empty()) {
										if (typeName == "int")
											info.intMax = std::stoi(maxStr);
										else
											info.floatMax = std::stof(maxStr);
									}
									if (!stepStr.empty())
										info.floatStep = std::stof(stepStr);
									if (!orderStr.empty())
										info.ordering = std::stoi(orderStr);
								} catch (...) {
								}

								lastUIDefines.push_back(std::move(info));
							}

							result += "#define " + defineName + " " + finalVal + "\n";
							continue;
						}
					}
				}
			}

			result += line + "\n";
		}

		content = std::move(result);
	}
}

#include "ENBExtender.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace ENBExtender
{
	namespace
	{
		// Fix duplicate #endif markers that ENB Extender uses
		std::string FixDuplicateEndif(const std::string& content)
		{
			std::vector<std::string> guards;
			std::istringstream stream(content);
			std::string line;
			bool inGuardCheck = true;

			while (std::getline(stream, line) && inGuardCheck) {
				std::string trimmed = line;
				size_t start = trimmed.find_first_not_of(" \t\r\n");
				if (start == std::string::npos)
					continue;
				trimmed = trimmed.substr(start);

				if (trimmed.starts_with("//"))
					continue;

				if (trimmed.starts_with("#ifndef ")) {
					std::string guard = trimmed.substr(8);
					size_t end = guard.find_first_of(" \t\r\n");
					if (end != std::string::npos)
						guard = guard.substr(0, end);
					guards.push_back(guard);
				} else if (trimmed.starts_with("#define ") && !guards.empty()) {
					continue;
				} else {
					inGuardCheck = false;
				}
			}

			if (guards.empty())
				return content;

			std::string result = content;
			size_t endifCount = 0;
			size_t pos = 0;
			std::vector<size_t> endifPositions;

			while ((pos = result.find("#endif", pos)) != std::string::npos) {
				endifPositions.push_back(pos);
				endifCount++;
				pos += 6;
			}

			size_t ifCount = 0;
			pos = 0;
			while ((pos = result.find("#if", pos)) != std::string::npos) {
				size_t matchLen = 3;
				bool isValidIf = false;

				if (pos + 3 < result.size()) {
					char nextChar = result[pos + 3];
					if (nextChar == 'n' && pos + 7 <= result.size() && result.substr(pos, 7) == "#ifndef") {
						isValidIf = true;
						matchLen = 7;
					} else if (nextChar == 'd' && pos + 6 <= result.size() && result.substr(pos, 6) == "#ifdef") {
						isValidIf = true;
						matchLen = 6;
					} else if (nextChar == ' ' || nextChar == '\t' || nextChar == '(') {
						isValidIf = true;
						matchLen = 3;
					}
				}

				if (isValidIf) {
					ifCount++;
				}
				pos += matchLen;
			}

			while (endifCount > ifCount && !endifPositions.empty()) {
				size_t lastEndifPos = endifPositions.back();
				endifPositions.pop_back();

				result.replace(lastEndifPos, 6, "//dup ");

				endifCount--;
			}

			return result;
		}

		// Fix malformed #elif lines that have trailing content after the condition
		// Example: "#elif EBM_ENABLE == 2    string UIGroup = ..." -> "#elif EBM_ENABLE == 2\n// string UIGroup = ..."
		std::string FixMalformedElif(const std::string& content)
		{
			std::string result;
			std::istringstream stream(content);
			std::string line;

			while (std::getline(stream, line)) {
				// Check if line contains #elif or #if with trailing non-preprocessor content
				size_t elifPos = line.find("#elif ");
				size_t ifPos = line.find("#if ");

				if (elifPos != std::string::npos || ifPos != std::string::npos) {
					// Find where the condition likely ends (look for common patterns)
					// Conditions typically end before "string", "float", "int", "bool", etc.
					std::regex trailingPattern(R"((#(?:el)?if\s+[^/\n]+?\s+)(string|float|int|bool|float2|float3|float4)\s)");
					std::smatch match;
					if (std::regex_search(line, match, trailingPattern)) {
						// Split the line: keep the directive part, comment out the rest
						std::string directivePart = match[1].str();
						std::string trailingPart = line.substr(match.position(1) + match.length(1) - match.length(2));
						result += directivePart + "\n// ENB Extender trailing content: " + trailingPart + "\n";
						continue;
					}
				}

				result += line + "\n";
			}

			return result;
		}

		// Handle files that start with #elif (comment out the orphaned directive)
		// These are ENB Extender UI grouping directives that don't have a matching #if
		std::string FixOrphanedElif(const std::string& content)
		{
			std::string result;
			std::istringstream stream(content);
			std::string line;
			bool foundIfDirective = false;
			bool inPreScan = true;
			std::vector<std::string> preScanLines;

			// First pass: scan to see if #elif appears before any #if
			while (std::getline(stream, line)) {
				preScanLines.push_back(line);

				if (!inPreScan)
					continue;

				size_t start = line.find_first_not_of(" \t\r");
				if (start == std::string::npos)
					continue;

				std::string trimmed = line.substr(start);

				if (trimmed.starts_with("//"))
					continue;

				if (trimmed.starts_with("#if ") || trimmed.starts_with("#ifdef ") || trimmed.starts_with("#ifndef ")) {
					foundIfDirective = true;
					inPreScan = false;
				} else if (trimmed.starts_with("#elif ") || trimmed.starts_with("#else")) {
					inPreScan = false;
				}
			}

			// Second pass: comment out orphaned #elif/#else lines at the start
			bool stillOrphaned = !foundIfDirective;
			for (const auto& l : preScanLines) {
				size_t start = l.find_first_not_of(" \t\r");
				std::string trimmed = (start != std::string::npos) ? l.substr(start) : "";

				if (stillOrphaned && (trimmed.starts_with("#elif ") || trimmed.starts_with("#else"))) {
					result += "// ENB Extender orphaned: " + l + "\n";
				} else {
					if (trimmed.starts_with("#if ") || trimmed.starts_with("#ifdef ") || trimmed.starts_with("#ifndef ")) {
						stillOrphaned = false;
					}
					result += l + "\n";
				}
			}

			return result;
		}
	}

	std::string PreprocessSource(const std::string& content)
	{
		std::string result = content;
		result = FixOrphanedElif(result);
		result = FixMalformedElif(result);
		result = FixDuplicateEndif(result);
		return result;
	}

	IncludeHandler::IncludeHandler(const std::filesystem::path& basePath) :
		basePath(basePath)
	{
	}

	HRESULT __stdcall IncludeHandler::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID /*pParentData*/, LPCVOID* ppData, UINT* pBytes)
	{
		std::filesystem::path includePath;
		std::string fileName = pFileName;

		// Strip leading slashes from include paths (ENB Extender convention)
		while (!fileName.empty() && (fileName[0] == '/' || fileName[0] == '\\')) {
			fileName = fileName.substr(1);
		}

		if (IncludeType == D3D_INCLUDE_LOCAL || IncludeType == D3D_INCLUDE_SYSTEM) {
			includePath = basePath / fileName;
		} else {
			return E_FAIL;
		}

		std::ifstream file(includePath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			logger::warn("[ENBPP] Include file not found: {}", includePath.string());
			return E_FAIL;
		}

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::string content(size, '\0');
		if (!file.read(content.data(), size)) {
			return E_FAIL;
		}
		file.close();

		content = PreprocessSource(content);

		char* buffer = new char[content.size()];
		memcpy(buffer, content.data(), content.size());

		*ppData = buffer;
		*pBytes = static_cast<UINT>(content.size());

		return S_OK;
	}

	HRESULT __stdcall IncludeHandler::Close(LPCVOID pData)
	{
		delete[] static_cast<const char*>(pData);
		return S_OK;
	}
}

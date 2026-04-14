#include "ENBExtender.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace ENBExtender
{
	namespace
	{
		/**
		 * @brief Comments out surplus `#endif` directives when an include-guard header pattern is detected.
		 *
		 * Detects a leading sequence of `#ifndef <GUARD>` (optionally followed by `#define <GUARD>`) and, if such guards are found,
		 * compares the number of `#if`-family directives to the number of `#endif` occurrences. When there are more `#endif`
		 * occurrences than recognized `#if`-family directives, the extra `#endif` tokens are replaced with `//dup ` to mark/comment them out.
		 *
		 * @param content Input source text to scan and modify.
		 * @return std::string The transformed source: extra `#endif` directives replaced by `//dup ` when include-guard patterns are present; otherwise the original `content`.
		 */
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
		/**
		 * @brief Removes trailing non-preprocessor content following `#if`/`#elif` directives by commenting it out.
		 *
		 * Scans the input source line-by-line; when a `#if` or `#elif` directive contains a condition followed by trailing tokens that look like declarations
		 * (e.g., `string`, `float`, `int`, `bool`, `float2`, `float3`, `float4`), preserves the directive and replaces the trailing portion with a comment
		 * prefixed by "`// ENB Extender trailing content: `". Lines without such malformed directives are returned unchanged.
		 *
		 * @param content Source text to preprocess; may contain preprocessor directives and code on the same line.
		 * @return std::string The transformed source text with trailing non-preprocessor content after `#if`/`#elif` directives commented out.
		 */
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
		/**
		 * @brief Comments out leading orphaned `#elif`/`#else` directives that appear before any `#if`-family directive.
		 *
		 * Scans the provided source text and, for any `#elif` or `#else` lines that occur at the start of the file
		 * before a matching `#if`, `#ifdef`, or `#ifndef` has been encountered, replaces those lines with a commented
		 * marker of the form `// ENB Extender orphaned: <originalLine>`. All other lines are preserved and returned
		 * with their original ordering and newline termination.
		 *
		 * @param content The input source text to inspect and rewrite.
		 * @return std::string The transformed source text with leading orphaned conditional directives commented out.
		 */
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

	/**
	 * @brief Applies ENB Extender preprocessing transformations to source text.
	 *
	 * Processes the provided source string and corrects common ENB Extender preprocessor irregularities:
	 * orphaned `#elif`/`#else` directives are commented out, trailing non-preprocessor content after `#if`/`#elif` conditions is removed and commented, and surplus `#endif` directives are commented to avoid duplicates.
	 *
	 * @param content Original source text to preprocess (e.g., shader or include file contents).
	 * @return std::string The input `content` with ENB Extender preprocessing fixes applied.
	 */
	std::string PreprocessSource(const std::string& content)
	{
		std::string result = content;
		result = FixOrphanedElif(result);
		result = FixMalformedElif(result);
		result = FixDuplicateEndif(result);
		return result;
	}

	/**
	 * @brief Constructs an IncludeHandler that resolves include requests relative to a base directory.
	 *
	 * Stores the provided filesystem path as the handler's base path, which is used to resolve
	 * local and system include filenames passed to Open().
	 *
	 * @param basePath Filesystem directory used as the root for resolving include file paths.
	 */
	IncludeHandler::IncludeHandler(const std::filesystem::path& basePath) :
		basePath(basePath)
	{
	}

	/**
	 * @brief Opens and returns a preprocessed include file's contents resolved relative to the handler's base path.
	 *
	 * Resolves pFileName (leading '/' or '\' characters are stripped), restricts resolution to local or system include types,
	 * reads the target file as binary, runs the ENB Extender preprocessing pipeline over the file contents, and returns a
	 * heap-allocated buffer containing the transformed bytes via `ppData` with its length in `pBytes`.
	 *
	 * @param IncludeType Type of include request; must be `D3D_INCLUDE_LOCAL` or `D3D_INCLUDE_SYSTEM`.
	 * @param pFileName Path of the include file; leading path-separators ('/' or '\') are removed before resolution.
	 * @param ppData Out pointer that receives a newly allocated `char[]` buffer containing the preprocessed file data.
	 *               The buffer is allocated with `new[]` and ownership is transferred to the caller (caller must free it).
	 * @param pBytes Out pointer that receives the size in bytes of the buffer placed in `ppData`.
	 * @return HRESULT `S_OK` on success; `E_FAIL` if the include type is unsupported, the file cannot be opened/read, or other I/O errors occur.
	 */
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

	/**
	 * @brief Releases a buffer previously returned by IncludeHandler::Open.
	 *
	 * Frees the heap allocation created for the included file content.
	 *
	 * @param pData Pointer to the buffer returned by Open (must have been allocated with `new char[]`).
	 * @return HRESULT S_OK on success.
	 */
	HRESULT __stdcall IncludeHandler::Close(LPCVOID pData)
	{
		delete[] static_cast<const char*>(pData);
		return S_OK;
	}
}

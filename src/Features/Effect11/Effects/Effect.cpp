#include "Effect.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>

#include "../ENBExtender.h"
#include "../PresetManager.h"
#include "../TextureManager.h"
#include "State.h"
#include "Utils/ShaderPatches.h"

namespace
{
	void StripLineDirectives(std::string& source)
	{
		std::string result;
		result.reserve(source.size());
		std::istringstream stream(source);
		std::string line;
		while (std::getline(stream, line)) {
			size_t firstNonSpace = line.find_first_not_of(" \t");
			if (firstNonSpace != std::string::npos && line[firstNonSpace] == '#') {
				auto directive = line.substr(firstNonSpace + 1);
				size_t dirStart = directive.find_first_not_of(" \t");
				if (dirStart != std::string::npos && directive.compare(dirStart, 4, "line") == 0) {
					result += "\n";
					continue;
				}
			}
			result += line;
			result += "\n";
		}
		source = std::move(result);
	}

	std::string ReadAndProcessInclude(const std::filesystem::path& fullPath,
		const std::filesystem::path& basePath,
		const std::string& iniPath,
		const std::string& iniSection,
		std::vector<std::filesystem::path>& includeDirs,
		std::unordered_set<std::string>& visited,
		int depth);

	std::string InlineIncludes(const std::string& source,
		const std::filesystem::path& basePath,
		const std::string& iniPath,
		const std::string& iniSection,
		std::vector<std::filesystem::path>& includeDirs,
		std::unordered_set<std::string>& visited,
		int depth = 0)
	{
		if (depth > 20)
			return source;

		std::string result;
		result.reserve(source.size());
		std::istringstream stream(source);
		std::string line;
		while (std::getline(stream, line)) {
			size_t firstNonSpace = line.find_first_not_of(" \t");
			if (firstNonSpace != std::string::npos && line[firstNonSpace] == '#') {
				auto rest = line.substr(firstNonSpace + 1);
				size_t dirStart = rest.find_first_not_of(" \t");
				if (dirStart != std::string::npos && rest.compare(dirStart, 7, "include") == 0) {
					size_t q1 = rest.find('"', dirStart + 7);
					size_t q2 = (q1 != std::string::npos) ? rest.find('"', q1 + 1) : std::string::npos;
					if (q1 != std::string::npos && q2 != std::string::npos) {
						std::string includeName = rest.substr(q1 + 1, q2 - q1 - 1);

						std::string_view nameView(includeName);
						while (!nameView.empty() && (nameView.front() == '/' || nameView.front() == '\\'))
							nameView.remove_prefix(1);
						includeName = std::string(nameView);

						std::filesystem::path fullPath;
						bool found = false;
						for (auto& dir : includeDirs) {
							auto candidate = dir / includeName;
							if (std::filesystem::exists(candidate)) {
								fullPath = candidate;
								found = true;
								break;
							}
						}
						if (!found)
							fullPath = basePath / includeName;

						std::string canonical = fullPath.string();
						if (visited.count(canonical)) {
							result += "\n";
							continue;
						}

						std::string expanded = ReadAndProcessInclude(fullPath, basePath, iniPath, iniSection, includeDirs, visited, depth + 1);
						result += expanded;
						result += "\n";
						continue;
					}
				}
			}
			result += line;
			result += "\n";
		}
		return result;
	}

	std::string ReadAndProcessInclude(const std::filesystem::path& fullPath,
		const std::filesystem::path& basePath,
		const std::string& iniPath,
		const std::string& iniSection,
		std::vector<std::filesystem::path>& includeDirs,
		std::unordered_set<std::string>& visited,
		int depth)
	{
		std::string canonical = fullPath.string();
		visited.insert(canonical);

		std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
		if (!file.is_open())
			return "";

		auto size = file.tellg();
		if (size < 0)
			return "";
		file.seekg(0, std::ios::beg);

		std::string content(static_cast<size_t>(size), '\0');
		if (!file.read(content.data(), size))
			return "";

		content = ENBExtender::DecodeKIEFX(content);
		ENBExtender::ConvertExtenderSyntax(content, basePath, iniPath, iniSection);
		Util::ShaderPatches::Apply(fullPath.filename().string().c_str(), content);

		auto parentDir = fullPath.parent_path();
		if (std::find(includeDirs.begin(), includeDirs.end(), parentDir) == includeDirs.end())
			includeDirs.push_back(parentDir);

		return InlineIncludes(content, basePath, iniPath, iniSection, includeDirs, visited, depth);
	}

	class PresetInclude : public ID3DInclude
	{
	public:
		PresetInclude(const std::filesystem::path& a_basePath, const std::string& a_iniPath = "", const std::string& a_iniSection = "") :
			basePath(a_basePath), iniPath(a_iniPath), iniSection(a_iniSection) {}

		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override
		{
			std::string_view name(pFileName);
			while (!name.empty() && (name.front() == '/' || name.front() == '\\'))
				name.remove_prefix(1);

			std::filesystem::path fullPath;
			bool found = false;

			// Try each known include directory
			for (auto& dir : includeDirs) {
				auto candidate = dir / name;
				if (std::filesystem::exists(candidate)) {
					fullPath = candidate;
					found = true;
					break;
				}
			}

			if (!found) {
				fullPath = basePath / name;
			}

			std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
			if (!file.is_open()) {
				logger::debug("[ENBPP] Include file not found: '{}' (resolved: '{}')", std::string(name), fullPath.string());
				static const char emptyContent[] = "\n";
				auto* buf = new char[1];
				buf[0] = '\n';
				*ppData = buf;
				*pBytes = 1;
				return S_OK;
			}

			auto size = file.tellg();
			if (size < 0)
				return E_FAIL;
			file.seekg(0, std::ios::beg);

			std::string content(static_cast<size_t>(size), '\0');
			if (!file.read(content.data(), size))
				return E_FAIL;

			content = ENBExtender::DecodeKIEFX(content);
			ENBExtender::ConvertExtenderSyntax(content, basePath, iniPath, iniSection);
			Util::ShaderPatches::Apply(pFileName, content);

			auto parentDir = fullPath.parent_path();
			if (std::find(includeDirs.begin(), includeDirs.end(), parentDir) == includeDirs.end())
				includeDirs.push_back(parentDir);

			auto* buf = new char[content.size()];
			memcpy(buf, content.data(), content.size());
			*ppData = buf;
			*pBytes = static_cast<UINT>(content.size());
			return S_OK;
		}

		HRESULT __stdcall Close(LPCVOID pData) override
		{
			delete[] static_cast<const char*>(pData);
			return S_OK;
		}

	private:
		std::filesystem::path basePath;
		std::string iniPath;
		std::string iniSection;
		std::vector<std::filesystem::path> includeDirs;
	};
}

bool Effect::Load()
{
	// Create ini file path based on effect name
	std::filesystem::path iniPath = PresetManager::GetSingleton().GetENBSeriesPath();
	iniPath /= GetName() + ".ini";

	// Check if file exists
	if (!std::filesystem::exists(iniPath)) {
		logger::info("[ENBPP] Could not find ini file '{}' for effect '{}', using defaults", iniPath.string(), GetName());
		return true;  // Not an error, just use defaults
	}

	// Skip reload if the file has not changed since last load
	auto writeTime = std::filesystem::last_write_time(iniPath);
	if (writeTime == lastIniWriteTime) {
		logger::info("[ENBPP] Skipping unchanged ini file '{}' for effect '{}'", iniPath.string(), GetName());
		return true;
	}
	lastIniWriteTime = writeTime;

	// Prepare section name
	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (auto& uiVar : uiVariables) {
		if (!uiVar.effectVariable || uiVar.isSeparator)
			continue;
		std::string iniKey = uiVar.group.empty() ? uiVar.displayName : (uiVar.group + "." + uiVar.displayName);
		std::vector<char> valueBuffer(1024);
		DWORD result = GetPrivateProfileStringA(section.c_str(), iniKey.c_str(), "", valueBuffer.data(), 1024, iniPath.string().c_str());
		if (result > 0) {
			std::string value(valueBuffer.data());
			LoadVariableFromString(uiVar, value);
		}
	}

	// Load technique index (stored as 1-indexed in .ini, convert to 0-indexed)
	if (!uiTechniques.empty()) {
		uint32_t techniqueFromIni = static_cast<uint32_t>(GetPrivateProfileIntA(section.c_str(), "TECHNIQUE", selectedTechniqueIndex + 1, iniPath.string().c_str()));
		// Convert from 1-indexed to 0-indexed and clamp to valid range
		if (techniqueFromIni > 0) {
			uint32_t maxIndex = static_cast<uint32_t>(uiTechniques.size() - 1);
			selectedTechniqueIndex = (techniqueFromIni - 1 < maxIndex) ? (techniqueFromIni - 1) : maxIndex;
		} else {
			selectedTechniqueIndex = 0;
		}
	}

	logger::info("[ENBPP] Loaded settings from '{}' for effect '{}'", iniPath.string(), GetName());
	return true;
}

void Effect::Save()
{
	// Create ini file path based on effect name
	std::filesystem::path iniPath = PresetManager::GetSingleton().GetENBSeriesPath();
	iniPath /= GetName() + ".ini";

	// Prepare section name
	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (const auto& uiVar : uiVariables) {
		if (!uiVar.effectVariable || uiVar.isSeparator)
			continue;

		std::string value;

		switch (uiVar.type) {
		case UIVariableType::Float:
			value = std::to_string(uiVar.floatValue);
			break;
		case UIVariableType::Int:
			value = std::to_string(uiVar.intValue);
			break;
		case UIVariableType::Bool:
			value = uiVar.boolValue ? "true" : "false";
			break;
		case UIVariableType::Color3:
		case UIVariableType::Color4:
			{
				std::ostringstream oss;
				int numComponents = (uiVar.type == UIVariableType::Color3) ? 3 : 4;

				std::copy(uiVar.colorValue, uiVar.colorValue + numComponents - 1,
					std::ostream_iterator<float>(oss, ", "));
				oss << uiVar.colorValue[numComponents - 1];

				value = oss.str();
			}
			break;
		}

		std::string iniKey = uiVar.group.empty() ? uiVar.displayName : (uiVar.group + "." + uiVar.displayName);
		BOOL result = WritePrivateProfileStringA(section.c_str(), iniKey.c_str(), value.c_str(), iniPath.string().c_str());
		if (!result) {
			logger::warn("[ENBPP] Failed to write key '{}' to ini file '{}'", iniKey, iniPath.string());
		}
	}

	std::string techniqueValue = std::to_string(selectedTechniqueIndex + 1u);
	BOOL techniqueResult = WritePrivateProfileStringA(section.c_str(), "TECHNIQUE", techniqueValue.c_str(), iniPath.string().c_str());
	if (!techniqueResult) {
		logger::warn("[ENBPP] Failed to write TECHNIQUE key to ini file '{}'", iniPath.string());
	}

	// Flush Windows .ini cache to disk to ensure Load() reads fresh data
	WritePrivateProfileStringA(NULL, NULL, NULL, iniPath.string().c_str());

	logger::info("[ENBPP] Saved settings to '{}' for effect '{}'", iniPath.string(), GetName());
}

bool Effect::Apply()
{
	logger::info("[ENBPP] Applying effect '{}'", GetName());

	Unload();

	if (!LoadFXFile()) {
		errors.push_back("Failed to compile FX file");
		logger::error("[ENBPP] Failed to compile FX file for effect '{}'", GetName());
		return false;
	}

	if (!Load()) {
		errors.push_back("Failed to load settings");
		logger::error("[ENBPP] Failed to load settings for effect '{}'", GetName());
		return false;
	}

	// Call virtual texture creation function
	CreateEffectTextures();

	logger::info("[ENBPP] Successfully applied effect '{}'", GetName());
	return true;
}

void Effect::Unload()
{
	effect = nullptr;

	techniques.clear();
	variables.clear();
	customTextureCache.clear();
	uiVariables.clear();
	availableTechniques.clear();
	effectTextureCache.clear();
	uiTechniques.clear();
	selectedTechniqueIndex = 0;
	groupDisplayNames.clear();
	groupDefaultOpen.clear();
	groupOrdering.clear();
	techniqueDropdownName = "Technique";
	techniqueDropdownGroup.clear();
	techniqueDropdownVisible = true;
	techniqueDropdownTopLevel = false;
	techniqueDropdownOrdering = 1;
	sourceGroupMap.clear();

	ClearVariableCache();

	errors.clear();

	// Reset write time so the next Load() after Apply() always reads fresh values
	lastIniWriteTime = {};

	logger::info("[ENBPP] Unloaded effect '{}'", GetName());
}

bool Effect::LoadFXFile()
{
	auto filePath = PresetManager::GetSingleton().GetENBSeriesPath();
	filePath /= GetName();

	// Read main effect file
	std::ifstream mainFile(filePath, std::ios::binary | std::ios::ate);
	if (!mainFile.is_open()) {
		errors.push_back("Failed to open effect file: " + filePath.string());
		logger::error("[ENBPP] Failed to open effect file: {}", filePath.string());
		return false;
	}

	std::streamsize size = mainFile.tellg();
	if (size < 0) {
		errors.push_back("Failed to determine size of effect file: " + filePath.string());
		logger::error("[ENBPP] Failed to determine size of effect file: {}", filePath.string());
		return false;
	}
	mainFile.seekg(0, std::ios::beg);
	std::string sourceCode(size, '\0');
	if (!mainFile.read(sourceCode.data(), size)) {
		errors.push_back("Failed to read effect file: " + filePath.string());
		logger::error("[ENBPP] Failed to read effect file: {}", filePath.string());
		return false;
	}
	mainFile.close();

	isKIEFX = ENBExtender::IsKIEFX(sourceCode);
	sourceCode = ENBExtender::DecodeKIEFX(sourceCode);

	auto enbseriesPath = filePath.parent_path();
	auto iniFilePath = enbseriesPath / (GetName() + ".ini");
	std::string iniPathStr = iniFilePath.string();
	std::string iniSection = GetName();
	std::transform(iniSection.begin(), iniSection.end(), iniSection.begin(), ::toupper);

	ENBExtender::ConvertExtenderSyntax(sourceCode, enbseriesPath, iniPathStr, iniSection);
	Util::ShaderPatches::Apply(GetName().c_str(), sourceCode);

	// Try to preprocess first for group scope analysis.
	// If preprocessing fails, fall back to direct compilation.
	bool usedPreprocessing = false;
	{
		PresetInclude ppInclude(enbseriesPath, iniPathStr, iniSection);
		winrt::com_ptr<ID3DBlob> preprocessedBlob;
		winrt::com_ptr<ID3DBlob> preprocessErrors;

		HRESULT ppHr = D3DPreprocess(
			sourceCode.c_str(),
			sourceCode.size(),
			filePath.string().c_str(),
			nullptr,
			&ppInclude,
			preprocessedBlob.put(),
			preprocessErrors.put());

		if (SUCCEEDED(ppHr) && preprocessedBlob) {
			std::string preprocessedSource(
				static_cast<const char*>(preprocessedBlob->GetBufferPointer()),
				preprocessedBlob->GetBufferSize());

			ENBExtender::ParseSourceGroupScopes(preprocessedSource, *this);

			StripLineDirectives(preprocessedSource);
			ENBExtender::ConvertFxGroups(preprocessedSource);

			winrt::com_ptr<ID3DBlob> compiledShader;
			winrt::com_ptr<ID3DBlob> errorBlob;

			HRESULT hr = D3DCompile(
				preprocessedSource.c_str(),
				preprocessedSource.size(),
				filePath.string().c_str(),
				nullptr,
				nullptr,
				nullptr,
				"fx_5_0",
				0,
				0,
				compiledShader.put(),
				errorBlob.put());

			if (SUCCEEDED(hr)) {
				HRESULT effectHr = D3DX11CreateEffectFromMemory(
					compiledShader->GetBufferPointer(),
					compiledShader->GetBufferSize(),
					0,
					globals::d3d::device,
					effect.put());

				if (SUCCEEDED(effectHr)) {
					usedPreprocessing = true;
				} else {
					logger::warn("[ENBPP] Preprocessed compilation created effect but D3DX11CreateEffectFromMemory failed for '{}', falling back", filePath.string());
				}
			} else {
				logger::warn("[ENBPP] Preprocessed source failed to compile for '{}', falling back to direct compilation", filePath.string());
			}
		} else {
			std::string ppMsg;
			if (preprocessErrors && preprocessErrors->GetBufferSize() > 0)
				ppMsg = std::string(static_cast<const char*>(preprocessErrors->GetBufferPointer()), preprocessErrors->GetBufferSize());
			logger::warn("[ENBPP] D3DPreprocess failed for '{}' (HRESULT 0x{:08X}), falling back: {}", filePath.string(), static_cast<unsigned int>(ppHr), ppMsg);
		}
	}

	// Fallback: direct compilation with include handler
	if (!usedPreprocessing) {
		{
			std::vector<std::filesystem::path> inlineDirs = { enbseriesPath };
			std::unordered_set<std::string> visited;
			std::string inlinedSource = InlineIncludes(sourceCode, enbseriesPath, iniPathStr, iniSection, inlineDirs, visited);

			winrt::com_ptr<ID3DBlob> ppBlob2;
			winrt::com_ptr<ID3DBlob> ppErr2;
			HRESULT ppHr2 = D3DPreprocess(inlinedSource.c_str(), inlinedSource.size(), filePath.string().c_str(), nullptr, nullptr, ppBlob2.put(), ppErr2.put());
			if (SUCCEEDED(ppHr2) && ppBlob2) {
				std::string ppSource2(static_cast<const char*>(ppBlob2->GetBufferPointer()), ppBlob2->GetBufferSize());
				ENBExtender::ParseSourceGroupScopes(ppSource2, *this);

				StripLineDirectives(ppSource2);
				ENBExtender::ConvertFxGroups(ppSource2);

				winrt::com_ptr<ID3DBlob> compiledShader2;
				winrt::com_ptr<ID3DBlob> errorBlob2;
				HRESULT hr2 = D3DCompile(ppSource2.c_str(), ppSource2.size(), filePath.string().c_str(), nullptr, nullptr, nullptr, "fx_5_0", 0, 0, compiledShader2.put(), errorBlob2.put());
				if (SUCCEEDED(hr2)) {
					HRESULT effectHr2 = D3DX11CreateEffectFromMemory(compiledShader2->GetBufferPointer(), compiledShader2->GetBufferSize(), 0, globals::d3d::device, effect.put());
					if (SUCCEEDED(effectHr2)) {
						usedPreprocessing = true;
					} else {
						logger::warn("[ENBPP] InlineIncludes fallback: D3DX11CreateEffectFromMemory failed for '{}' (0x{:08X})", filePath.string(), static_cast<unsigned int>(effectHr2));
					}
				} else {
					std::string errMsg;
					if (errorBlob2 && errorBlob2->GetBufferSize() > 0)
						errMsg = std::string(static_cast<const char*>(errorBlob2->GetBufferPointer()), errorBlob2->GetBufferSize());
					logger::warn("[ENBPP] InlineIncludes fallback: D3DCompile failed for '{}': {}", filePath.string(), errMsg);
				}
			} else {
				std::string ppMsg2;
				if (ppErr2 && ppErr2->GetBufferSize() > 0)
					ppMsg2 = std::string(static_cast<const char*>(ppErr2->GetBufferPointer()), ppErr2->GetBufferSize());
				logger::warn("[ENBPP] InlineIncludes+D3DPreprocess also failed for '{}' (HRESULT 0x{:08X}): {}", filePath.string(), static_cast<unsigned int>(ppHr2), ppMsg2);
			}

		}

		if (!usedPreprocessing) {
			ENBExtender::ConvertFxGroups(sourceCode);
			PresetInclude includeHandler(enbseriesPath, iniPathStr, iniSection);
			winrt::com_ptr<ID3DBlob> compiledShader;
			winrt::com_ptr<ID3DBlob> errorBlob;

			HRESULT hr = D3DCompile(
				sourceCode.c_str(),
				sourceCode.size(),
				filePath.string().c_str(),
				nullptr,
				&includeHandler,
				nullptr,
				"fx_5_0",
				0,
				0,
				compiledShader.put(),
				errorBlob.put());

			if (FAILED(hr)) {
				std::string errorMsg = "Compilation failed";
				if (errorBlob) {
					errorMsg = std::string(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
					logger::error("[ENBPP] Effect compilation failed for '{}'", filePath.string());
					std::istringstream errorStream(errorMsg);
					std::string errorLine;
					while (std::getline(errorStream, errorLine)) {
						if (!errorLine.empty()) {
							logger::error("[ENBPP]   {}", errorLine);
						}
					}
				} else {
					logger::error("[ENBPP] Effect compilation failed for '{}': HRESULT 0x{:08X}", filePath.string(), static_cast<unsigned int>(hr));
				}
				errors.push_back(errorMsg);
				return false;
			}

			HRESULT effectHr = D3DX11CreateEffectFromMemory(
				compiledShader->GetBufferPointer(),
				compiledShader->GetBufferSize(),
				0,
				globals::d3d::device,
				effect.put());

			if (FAILED(effectHr)) {
				std::string errorMsg = "Failed to create effect from compiled shader";
				logger::error("[ENBPP] {} for '{}': HRESULT 0x{:08X}", errorMsg, filePath.string(), static_cast<unsigned int>(effectHr));
				errors.push_back(errorMsg);
				return false;
			}
		}
	}

	// Common textures and variables are now managed by EffectManager
	EnumerateAllVariables();

	SetupCustomTextures();
	LoadTechniques();
	LoadUITechniques();

	logger::info("[ENBPP] Effect '{}' compiled successfully with {} UI techniques", GetName(), uiTechniques.size());

	// Populate available techniques for UI selection
	availableTechniques = GetBaseTechniqueNames();

	// Set default selected technique to first annotated technique
	if (!uiTechniques.empty()) {
		selectedTechniqueIndex = 0;  // Default to first annotated technique
	}

	LoadUIVariables();

	// If some UI variables have no group, try to recover groups from the INI file
	// The INI stores keys as "Group.SubGroup.DisplayName", so we can parse group paths from keys
	bool hasUngrouped = false;
	for (auto& v : uiVariables) {
		if (!v.isSeparator && !v.isHidden && v.group.empty() && !v.isTopLevel && !v.displayName.empty()) {
			hasUngrouped = true;
			break;
		}
	}

	if (hasUngrouped) {
		auto groupIniPath = enbseriesPath / (GetName() + ".ini");
		if (std::filesystem::exists(groupIniPath)) {
			std::string section = GetName();
			std::transform(section.begin(), section.end(), section.begin(), ::toupper);

			std::vector<char> keysBuf(32768);
			DWORD keysLen = GetPrivateProfileStringA(section.c_str(), nullptr, "", keysBuf.data(), static_cast<DWORD>(keysBuf.size()), groupIniPath.string().c_str());

			struct IniEntry
			{
				std::string displayName;
				std::string group;
				bool consumed = false;
			};
			std::vector<IniEntry> iniEntries;

			const char* key = keysBuf.data();
			while (key < keysBuf.data() + keysLen && *key) {
				std::string fullKey(key);
				key += fullKey.size() + 1;

				size_t lastDot = fullKey.rfind('.');
				if (lastDot != std::string::npos && lastDot > 0) {
					std::string groupPath = fullKey.substr(0, lastDot);
					std::string displayName = fullKey.substr(lastDot + 1);
					if (!displayName.empty())
						iniEntries.push_back({ displayName, groupPath, false });
				}
			}

			int recovered = 0;
			for (auto& v : uiVariables) {
				if (!v.isSeparator && v.group.empty() && !v.isTopLevel && !v.displayName.empty()) {
					for (auto& entry : iniEntries) {
						if (!entry.consumed && entry.displayName == v.displayName) {
							v.group = entry.group;
							entry.consumed = true;
							if (groupOrdering.find(v.group) == groupOrdering.end())
								groupOrdering[v.group] = 0;
							++recovered;
							break;
						}
					}
				}
			}
			if (recovered > 0)
				logger::info("[ENBPP] Recovered {} group assignments from INI for ungrouped variables", recovered);
		}
	}

	logger::info("[ENBPP] Successfully loaded FX file: {}", filePath.string());
	return true;
}

Effect::TechniqueSequenceResult Effect::ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp)
{
	if (!IsCompiled() || !effect) {
		return {};
	}

	auto context = globals::d3d::context;

	if (a_baseTechniqueName.empty()) {
		return {};
	}

	// Check if the technique sequence exists
	auto sequenceIt = techniques.find(a_baseTechniqueName);
	if (sequenceIt == techniques.end()) {
		logger::debug("[ENBPP] Technique sequence '{}' not found", a_baseTechniqueName);
		return {};
	}

	const auto& sequence = sequenceIt->second;

	if (sequence.empty()) {
		logger::debug("[ENBPP] Technique sequence '{}' is empty", a_baseTechniqueName);
		return {};
	}

	auto sourceTexture = effect->GetVariableByName("TextureColor")->AsShaderResource();

	uint32_t swapCounter = 0;  // Track swap count for ping-ponging between output and temp
	bool targetInOutput = false;

	ID3D11ShaderResourceView* inputSRV = nullptr;
	ID3D11RenderTargetView* outputRTV = nullptr;

	for (size_t i = 0; i < sequence.size(); ++i) {
		auto& techniqueInfo = sequence[i];

		if (!techniqueInfo.technique) {
			logger::warn("[ENBPP] Technique {} in sequence '{}' is null, skipping", i, a_baseTechniqueName);
			continue;
		}

		D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		// Determine input and output for this technique
		if (sequence.size() == 1 || swapCounter == 0) {
			// Single technique or first pass: input -> output
			inputSRV = a_input;
			outputRTV = a_output.rtv.get();
		} else {
			// Subsequent passes: ping-pong between output and temp
			bool useTemp = (swapCounter & 1) == 0;  // Use counter LSB for swap determination
			if (useTemp) {
				// Read from temp, write to output
				inputSRV = a_temp.srv.get();
				outputRTV = a_output.rtv.get();
			} else {
				// Read from output, write to temp
				inputSRV = a_output.srv.get();
				outputRTV = a_temp.rtv.get();
			}
		}

		// Handle custom render target if specified
		if (!techniqueInfo.renderTargetName.empty()) {
			outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, outputRTV);
		} else {
			swapCounter++;  // Increment counter for next iteration
		}

		targetInOutput = (outputRTV == a_output.rtv.get());

		if (sourceTexture && sourceTexture->IsValid()) {
			sourceTexture->AsShaderResource()->SetResource(inputSRV);
		}

		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		// Get input and output dimensions for Size variables
		uint32_t inputWidth = 0, inputHeight = 0;
		uint32_t outputWidth = 0, outputHeight = 0;

		// Get input dimensions from SRV
		winrt::com_ptr<ID3D11Resource> inputResource;
		inputSRV->GetResource(inputResource.put());
		winrt::com_ptr<ID3D11Texture2D> inputTexture;
		if (inputResource) {
			inputResource.try_as(inputTexture);
			if (inputTexture) {
				D3D11_TEXTURE2D_DESC inputDesc;
				inputTexture->GetDesc(&inputDesc);
				inputWidth = inputDesc.Width;
				inputHeight = inputDesc.Height;
			}
		}

		// Get output dimensions from RTV
		winrt::com_ptr<ID3D11Resource> outputResource;
		outputRTV->GetResource(outputResource.put());
		winrt::com_ptr<ID3D11Texture2D> outputTexture;
		if (outputResource) {
			outputResource.try_as(outputTexture);
			if (outputTexture) {
				D3D11_TEXTURE2D_DESC outputDesc;
				outputTexture->GetDesc(&outputDesc);
				outputWidth = outputDesc.Width;
				outputHeight = outputDesc.Height;
			}
		}

		// Update ScreenSize in shader
		UpdateSizeVariables(effect.get(), outputWidth, outputHeight);

		// Set viewport based on render target description
		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(outputWidth);
		viewport.Height = static_cast<float>(outputHeight);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			techniqueInfo.technique->GetPassByIndex(p)->Apply(0, context);
			context->Draw(4, 0);
		}
	}

	return { true, targetInOutput };
}

void Effect::ExecuteTechnique(const std::string& techniqueName, TextureManager::Texture& output)
{
	if (!IsCompiled() || !effect) {
		return;
	}

	auto context = globals::d3d::context;

	// Find the technique
	auto technique = effect->GetTechniqueByName(techniqueName.c_str());
	if (!technique || !technique->IsValid()) {
		logger::debug("[ENBPP] Technique '{}' not found or invalid", techniqueName);
		return;
	}

	// Set output render target
	ID3D11RenderTargetView* rtvArray[] = { output.rtv.get() };
	context->OMSetRenderTargets(1, rtvArray, nullptr);

	// Set viewport based on render target description
	D3D11_TEXTURE2D_DESC texDesc;
	output.texture->GetDesc(&texDesc);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(texDesc.Width);
	viewport.Height = static_cast<float>(texDesc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Execute technique
	D3DX11_TECHNIQUE_DESC techDesc;
	technique->GetDesc(&techDesc);

	for (UINT p = 0; p < techDesc.Passes; p++) {
		technique->GetPassByIndex(p)->Apply(0, context);
		context->Draw(4, 0);
	}
}

void Effect::SetupCustomTextures()
{
	// Iterate through all variables to find texture variables with ResourceName annotations
	for (auto& [varName, effectVar] : variables) {
		// Get ResourceName annotation
		std::string resourceName = GetResourceNameFromVariable(effectVar.get());

		if (!resourceName.empty()) {
			logger::debug("[ENBPP] Loading texture for variable '{}': {}", varName, resourceName);

			// Load the texture
			auto srv = LoadTextureFromFile(resourceName);
			if (srv) {
				// Set the texture on the variable
				auto shaderResourceVar = effectVar->AsShaderResource();
				if (shaderResourceVar && shaderResourceVar->IsValid()) {
					shaderResourceVar->SetResource(srv);
					logger::debug("[ENBPP] Successfully bound texture '{}' to variable '{}'", resourceName, varName);
				}
			} else {
				logger::warn("[ENBPP] Failed to load texture '{}' for variable '{}'", resourceName, varName);
			}
		}
	}
}

ID3D11ShaderResourceView* Effect::LoadTextureFromFile(const std::string& filename)
{
	auto device = globals::d3d::device;

	// Check cache first
	auto cacheIt = customTextureCache.find(filename);
	if (cacheIt != customTextureCache.end()) {
		return cacheIt->second.get();
	}

	std::filesystem::path filepath = PresetManager::GetSingleton().GetENBSeriesPath() / filename;

	winrt::com_ptr<ID3D11Resource> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;

	HRESULT hr = DirectX::CreateDDSTextureFromFile(device, filepath.c_str(), texture.put(), srv.put());

	if (FAILED(hr)) {
		// Try loading as other format (PNG, BMP, etc.)
		hr = DirectX::CreateWICTextureFromFile(device, filepath.c_str(), texture.put(), srv.put());
	}

	auto fileString = filepath.string();

	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to load texture file: {} (HRESULT: 0x{:08X})", fileString, static_cast<uint32_t>(hr));
		return nullptr;
	}

	// Cache the loaded texture
	customTextureCache[filename] = srv;

	logger::debug("[ENBPP] Successfully loaded texture: {}", fileString);
	return srv.get();
}

std::string Effect::GetResourceNameFromVariable(ID3DX11EffectVariable* variable)
{
	return GetUIAnnotation(variable, "ResourceName");
}

template <typename Callback>
static void ForEachTechniqueSequence(ID3DX11Effect* effect, Callback&& callback)
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc)))
		return;

	std::string currentSequenceBaseName;
	int currentSequenceIndex = 0;

	for (UINT i = 0; i < effectDesc.Techniques; ++i) {
		auto technique = effect->GetTechniqueByIndex(i);
		if (!technique->IsValid())
			continue;

		D3DX11_TECHNIQUE_DESC techDesc;
		if (FAILED(technique->GetDesc(&techDesc)))
			continue;

		std::string techniqueName = techDesc.Name ? techDesc.Name : ("technique" + std::to_string(i));
		std::string baseName;
		int sequenceNumber = 0;

		if (!currentSequenceBaseName.empty()) {
			std::string expectedName = currentSequenceBaseName + std::to_string(currentSequenceIndex + 1);
			if (techniqueName == expectedName) {
				baseName = currentSequenceBaseName;
				sequenceNumber = currentSequenceIndex + 1;
				currentSequenceIndex++;
			} else {
				baseName = techniqueName;
				currentSequenceBaseName = techniqueName;
				currentSequenceIndex = 0;
			}
		} else {
			baseName = techniqueName;
			currentSequenceBaseName = techniqueName;
			currentSequenceIndex = 0;
		}

		callback(technique, baseName, techniqueName, sequenceNumber);
	}
}

void Effect::LoadTechniques()
{
	ForEachTechniqueSequence(effect.get(), [this](ID3DX11EffectTechnique* technique, const std::string& baseName, [[maybe_unused]] const std::string& techniqueName, int sequenceNumber) {
		std::string renderTargetName = GetRenderTargetFromTechnique(technique);

		if (techniques[baseName].size() <= static_cast<size_t>(sequenceNumber))
			techniques[baseName].resize(sequenceNumber + 1);

		TechniqueInfo techInfo;
		techInfo.technique.copy_from(technique);
		techInfo.renderTargetName = renderTargetName;
		techniques[baseName][sequenceNumber] = std::move(techInfo);

	});
}

std::vector<std::string> Effect::GetBaseTechniqueNames()
{
	std::vector<std::string> baseNames;
	baseNames.reserve(techniques.size());

	for (const auto& [baseName, sequence] : techniques) {
		if (!sequence.empty() && sequence[0].technique) {
			baseNames.push_back(baseName);
		}
	}

	return baseNames;
}

static std::string GetTechniqueAnnotation(ID3DX11EffectTechnique* technique, const char* annotationName);

void Effect::LoadUITechniques()
{
	uiTechniques.clear();
	selectedTechniqueIndex = 0;

	// Read technique dropdown metadata from first technique
	D3DX11_EFFECT_DESC firstEffectDesc;
	if (SUCCEEDED(effect->GetDesc(&firstEffectDesc)) && firstEffectDesc.Techniques > 0) {
		auto firstTech = effect->GetTechniqueByIndex(0);
		if (firstTech && firstTech->IsValid()) {
			std::string dropName = GetTechniqueAnnotation(firstTech, "UIDropdownName");
			if (!dropName.empty())
				techniqueDropdownName = dropName;
			std::string dropGroup = GetTechniqueAnnotation(firstTech, "UIDropdownGroup");
			if (!dropGroup.empty())
				techniqueDropdownGroup = dropGroup;
			std::string dropVisible = GetTechniqueAnnotation(firstTech, "UIDropdownVisible");
			if (!dropVisible.empty())
				techniqueDropdownVisible = (dropVisible != "0" && dropVisible != "false");
			std::string dropTopLevel = GetTechniqueAnnotation(firstTech, "UIDropdownTopLevel");
			if (!dropTopLevel.empty())
				techniqueDropdownTopLevel = (dropTopLevel != "0" && dropTopLevel != "false");
			std::string dropOrdering = GetTechniqueAnnotation(firstTech, "UIDropdownOrdering");
			if (!dropOrdering.empty()) {
				try {
					techniqueDropdownOrdering = std::stoi(dropOrdering);
				} catch (...) {
				}
			}
		}
	}

	ForEachTechniqueSequence(effect.get(), [this](ID3DX11EffectTechnique* technique, const std::string& baseName, [[maybe_unused]] const std::string& techniqueName, [[maybe_unused]] int sequenceNumber) {
		std::string uiName = GetUINameFromTechnique(technique);
		if (uiName.empty())
			return;

		for (const auto& existing : uiTechniques) {
			if (existing.techniqueName == baseName)
				return;
		}

		UITechnique uiTech;
		uiTech.techniqueName = baseName;
		uiTech.displayName = uiName;
		uiTechniques.push_back(uiTech);

	});
}

static std::string GetTechniqueAnnotation(ID3DX11EffectTechnique* technique, const char* annotationName)
{
	if (!technique)
		return "";

	D3DX11_TECHNIQUE_DESC techDesc;
	if (FAILED(technique->GetDesc(&techDesc)))
		return "";

	for (UINT i = 0; i < techDesc.Annotations; ++i) {
		auto annotation = technique->GetAnnotationByIndex(i);
		if (!annotation || !annotation->IsValid())
			continue;

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(annotation->GetDesc(&annotationDesc)))
			continue;

		if (std::string(annotationDesc.Name) == annotationName) {
			auto stringVar = annotation->AsString();
			if (stringVar && stringVar->IsValid()) {
				LPCSTR value = nullptr;
				if (SUCCEEDED(stringVar->GetString(&value)) && value)
					return std::string(value);
			}
		}
	}
	return "";
}

std::string Effect::GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique)
{
	return GetTechniqueAnnotation(technique, "RenderTarget");
}

std::string Effect::GetUINameFromTechnique(ID3DX11EffectTechnique* technique)
{
	return GetTechniqueAnnotation(technique, "UIName");
}

TextureManager::Texture* Effect::GetEffectTexture(const std::string& name)
{
	auto it = effectTextureCache.find(name);
	if (it != effectTextureCache.end()) {
		return &it->second;
	}
	return nullptr;
}

ID3D11RenderTargetView* Effect::GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback)
{
	if (renderTargetName.empty()) {
		return fallback;
	}

	auto* texture = GetEffectTexture(renderTargetName);
	if (texture && texture->rtv)
		return texture->rtv.get();

	// Get render target from EffectManager's common texture cache (using our pointer cache)
	texture = GetCachedCommonTexture(renderTargetName);
	if (texture && texture->rtv)
		return texture->rtv.get();

	logger::warn("[ENBPP] Render target '{}' not found in cache, using fallback", renderTargetName);
	return fallback;
}

void Effect::LoadUIVariables()
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc))) {
		return;
	}

	uiVariables.clear();

	// First pass: process UIGroupBegin/UIGroupEnd scope and collect UI variables
	std::vector<std::string> groupStack;

	for (UINT i = 0; i < effectDesc.GlobalVariables; ++i) {
		auto variable = effect->GetVariableByIndex(i);
		if (!variable || !variable->IsValid())
			continue;

		D3DX11_EFFECT_VARIABLE_DESC varDesc;
		if (FAILED(variable->GetDesc(&varDesc)))
			continue;

		D3DX11_EFFECT_TYPE_DESC typeDesc;
		auto effectType = variable->GetType();
		if (FAILED(effectType->GetDesc(&typeDesc)))
			continue;

		// Handle string variables for UIGroupBegin/UIGroupEnd/UISeparator
		if (typeDesc.Class == D3D_SVC_OBJECT && typeDesc.Type == D3D_SVT_STRING) {
			ENBExtender::ProcessExtenderStringVariable(variable, varDesc, groupStack, *this);
			continue;
		}

		std::string uiName = GetUIAnnotation(variable, "UIName");
		if (uiName.empty())
			continue;

		// Check UIVisible
		std::string visibleStr = GetUIAnnotation(variable, "UIVisible");
		if (visibleStr == "0" || visibleStr == "false")
			continue;

		UIVariable uiVar = {};
		uiVar.name = varDesc.Name;
		uiVar.displayName = uiName;
		uiVar.effectVariable.copy_from(variable);

		ENBExtender::ApplyExtenderAnnotations(uiVar, variable, groupStack, *this);
		if (uiVar.isHidden)
			continue;

		// Determine variable type
		if (typeDesc.Class == D3D_SVC_SCALAR) {
			switch (typeDesc.Type) {
			case D3D_SVT_FLOAT:
				uiVar.type = UIVariableType::Float;
				break;
			case D3D_SVT_INT:
				uiVar.type = UIVariableType::Int;
				break;
			case D3D_SVT_BOOL:
				uiVar.type = UIVariableType::Bool;
				break;
			default:
				continue;
			}
		} else if (typeDesc.Class == D3D_SVC_VECTOR && typeDesc.Type == D3D_SVT_FLOAT && typeDesc.Elements == 0) {
			if (typeDesc.Columns == 3)
				uiVar.type = UIVariableType::Color3;
			else if (typeDesc.Columns == 4)
				uiVar.type = UIVariableType::Color4;
			else
				continue;
		} else {
			continue;
		}

		// Parse UI widget type
		std::string widgetStr = GetUIAnnotation(variable, "UIWidget");
		uiVar.widgetType = ParseWidgetType(widgetStr);

		// Parse UI properties based on type
		try {
			if (uiVar.type == UIVariableType::Float) {
				std::string minStr = GetUIAnnotation(variable, "UIMin");
				std::string maxStr = GetUIAnnotation(variable, "UIMax");
				std::string stepStr = GetUIAnnotation(variable, "UIStep");
				if (!minStr.empty())
					uiVar.floatMin = std::stof(minStr);
				if (!maxStr.empty())
					uiVar.floatMax = std::stof(maxStr);
				if (!stepStr.empty())
					uiVar.floatStep = std::stof(stepStr);
			} else if (uiVar.type == UIVariableType::Int) {
				std::string minStr = GetUIAnnotation(variable, "UIMin");
				std::string maxStr = GetUIAnnotation(variable, "UIMax");
				if (!minStr.empty())
					uiVar.intMin = std::stoi(minStr);
				if (!maxStr.empty())
					uiVar.intMax = std::stoi(maxStr);

				if (uiVar.widgetType == UIWidgetType::Dropdown) {
					std::string listStr = GetUIAnnotation(variable, "UIList");
					if (!listStr.empty())
						uiVar.dropdownItems = ParseDropdownList(listStr);
				} else if (uiVar.widgetType == UIWidgetType::Quality) {
					uiVar.dropdownItems = { "Very High", "High", "Medium", "Low", "Very Low" };
					uiVar.intMin = -1;
					uiVar.intMax = 3;
				}
			}
		} catch (const std::exception& e) {
			logger::warn("[ENBPP] Failed to parse UI annotations for variable '{}': {}", uiVar.name, e.what());
		}

		LoadUIVariableValue(uiVar);
		ENBExtender::ParseTimePeriod(uiVar);

		uiVariables.push_back(uiVar);
	}

	ENBExtender::InsertUIDefines(*this);

	logger::info("[ENBPP] Loaded {} UI variables for effect '{}'", uiVariables.size(), GetName());
}

std::string Effect::GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName)
{
	if (!variable)
		return "";

	D3DX11_EFFECT_VARIABLE_DESC varDesc;
	if (FAILED(variable->GetDesc(&varDesc)))
		return "";

	// Try by name first (Effects11 supports case-insensitive lookup)
	auto annotation = variable->GetAnnotationByName(annotationName.c_str());
	auto readAnnotationValue = [](ID3DX11EffectVariable* ann) -> std::string {
		auto stringVar = ann->AsString();
		if (stringVar && stringVar->IsValid()) {
			LPCSTR value = nullptr;
			if (SUCCEEDED(stringVar->GetString(&value)) && value)
				return std::string(value);
		}

		auto scalarVar = ann->AsScalar();
		if (scalarVar && scalarVar->IsValid()) {
			auto annType = ann->GetType();
			D3DX11_EFFECT_TYPE_DESC annTypeDesc;
			if (annType && SUCCEEDED(annType->GetDesc(&annTypeDesc))) {
				switch (annTypeDesc.Type) {
				case D3D_SVT_INT: {
					int intValue;
					if (SUCCEEDED(scalarVar->GetInt(&intValue)))
						return std::to_string(intValue);
					break;
				}
				case D3D_SVT_FLOAT: {
					float floatValue;
					if (SUCCEEDED(scalarVar->GetFloat(&floatValue)))
						return std::to_string(floatValue);
					break;
				}
				case D3D_SVT_BOOL: {
					bool boolValue;
					if (SUCCEEDED(scalarVar->GetBool(&boolValue)))
						return std::to_string(boolValue ? 1 : 0);
					break;
				}
				default:
					break;
				}
			}
			int intValue;
			if (SUCCEEDED(scalarVar->GetInt(&intValue)))
				return std::to_string(intValue);
			float floatValue;
			if (SUCCEEDED(scalarVar->GetFloat(&floatValue)))
				return std::to_string(floatValue);
		}
		return "";
	};

	if (annotation && annotation->IsValid()) {
		std::string result = readAnnotationValue(annotation);
		if (!result.empty())
			return result;
	}

	// Fallback: iterate and do case-insensitive comparison
	for (UINT i = 0; i < varDesc.Annotations; ++i) {
		auto ann = variable->GetAnnotationByIndex(i);
		if (!ann || !ann->IsValid())
			continue;

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(ann->GetDesc(&annotationDesc)))
			continue;

		if (_stricmp(annotationDesc.Name, annotationName.c_str()) == 0) {
			std::string result = readAnnotationValue(ann);
			if (!result.empty())
				return result;
		}
	}

	return "";
}

Effect::UIWidgetType Effect::ParseWidgetType(const std::string& widget)
{
	std::string lowerWidget = widget;
	std::transform(lowerWidget.begin(), lowerWidget.end(), lowerWidget.begin(), ::tolower);

	if (lowerWidget == "spinner")
		return UIWidgetType::Spinner;
	if (lowerWidget == "dropdown")
		return UIWidgetType::Dropdown;
	if (lowerWidget == "vector")
		return UIWidgetType::Vector;
	if (lowerWidget == "quality")
		return UIWidgetType::Quality;
	if (lowerWidget == "color")
		return UIWidgetType::Color;
	return UIWidgetType::Default;
}

std::vector<std::string> Effect::ParseDropdownList(const std::string& list)
{
	std::vector<std::string> items;
	std::stringstream ss(list);
	std::string item;

	while (std::getline(ss, item, ',')) {
		// Trim whitespace
		item.erase(0, item.find_first_not_of(" \t"));
		item.erase(item.find_last_not_of(" \t") + 1);
		items.push_back(item);
	}

	return items;
}

void Effect::LoadUIVariableValue(UIVariable& uiVar)
{
	switch (uiVar.type) {
	case UIVariableType::Float:
		if (SUCCEEDED(uiVar.effectVariable->AsScalar()->GetFloat(&uiVar.floatValue))) {
			// Successfully loaded float value
		}
		break;
	case UIVariableType::Int:
		if (SUCCEEDED(uiVar.effectVariable->AsScalar()->GetInt(&uiVar.intValue))) {
			// Successfully loaded int value
		}
		break;
	case UIVariableType::Bool:
		if (SUCCEEDED(uiVar.effectVariable->AsScalar()->GetBool(&uiVar.boolValue))) {
			// Successfully loaded bool value
		}
		break;
	case UIVariableType::Color3:
	case UIVariableType::Color4:
		if (SUCCEEDED(uiVar.effectVariable->AsVector()->GetFloatVector(uiVar.colorValue))) {
		}
		break;
	}
}

void Effect::LoadVariableFromString(UIVariable& uiVar, const std::string& value)
{
	try {
		switch (uiVar.type) {
		case UIVariableType::Float:
			{
				uiVar.floatValue = std::stof(value);
				uiVar.effectVariable->AsScalar()->SetFloat(uiVar.floatValue);
			}
			break;
		case UIVariableType::Int:
			{
				uiVar.intValue = std::stoi(value);
				uiVar.effectVariable->AsScalar()->SetInt(uiVar.intValue);
			}
			break;
		case UIVariableType::Bool:
			{
				std::string lowerValue = value;
				std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
				if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on") {
					uiVar.boolValue = true;
				} else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off") {
					uiVar.boolValue = false;
				} else {
					uiVar.boolValue = std::stoi(value) != 0;
				}
				uiVar.effectVariable->AsScalar()->SetBool(uiVar.boolValue);
			}
			break;
		case UIVariableType::Color3:
		case UIVariableType::Color4:
			{
				std::istringstream ss(value);
				int numComponents = (uiVar.type == UIVariableType::Color3) ? 3 : 4;
				for (int i = 0; i < numComponents; ++i) {
					char sep;
					ss >> uiVar.colorValue[i];
					if (ss.peek() == ',') {
						ss >> sep;
					}
				}
				uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.colorValue);
			}
			break;
		}
	} catch (const std::exception& e) {
		logger::warn("[ENBPP] Failed to parse value '{}' for variable '{}': {}", value, uiVar.name, e.what());
	}
}

static bool IsPlayerInterior()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return false;
	auto cell = player->GetParentCell();
	return cell && cell->IsInteriorCell();
}

static constexpr int kNumExteriorPeriods = 6;
static constexpr const char* kPeriodNames[kNumExteriorPeriods] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };

static void GetTimeOfDayWeights(float weights[kNumExteriorPeriods])
{
	float hour = 12.0f;
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (calendar)
		hour = calendar->GetHour();

	for (int i = 0; i < kNumExteriorPeriods; ++i)
		weights[i] = 0.0f;

	// Dawn=0, Sunrise=1, Day=2, Sunset=3, Dusk=4, Night=5
	if (hour < 4.0f) {
		weights[5] = 1.0f;
	} else if (hour < 5.0f) {
		float t = hour - 4.0f;
		weights[5] = 1.0f - t;
		weights[0] = t;
	} else if (hour < 6.0f) {
		weights[0] = 1.0f;
	} else if (hour < 7.0f) {
		float t = hour - 6.0f;
		weights[0] = 1.0f - t;
		weights[1] = t;
	} else if (hour < 8.0f) {
		weights[1] = 1.0f;
	} else if (hour < 9.0f) {
		float t = hour - 8.0f;
		weights[1] = 1.0f - t;
		weights[2] = t;
	} else if (hour < 15.0f) {
		weights[2] = 1.0f;
	} else if (hour < 16.0f) {
		float t = hour - 15.0f;
		weights[2] = 1.0f - t;
		weights[3] = t;
	} else if (hour < 17.0f) {
		weights[3] = 1.0f;
	} else if (hour < 18.0f) {
		float t = hour - 17.0f;
		weights[3] = 1.0f - t;
		weights[4] = t;
	} else if (hour < 19.0f) {
		weights[4] = 1.0f;
	} else if (hour < 20.0f) {
		float t = hour - 19.0f;
		weights[4] = 1.0f - t;
		weights[5] = t;
	} else {
		weights[5] = 1.0f;
	}
}

void Effect::UpdateUIVariables()
{
	// Phase 1: Set all UI variable values to their effect variables
	for (auto& uiVar : uiVariables) {
		if (!uiVar.effectVariable || uiVar.isSeparator)
			continue;

		switch (uiVar.type) {
		case UIVariableType::Float:
			uiVar.effectVariable->AsScalar()->SetFloat(uiVar.floatValue);
			break;
		case UIVariableType::Int:
			uiVar.effectVariable->AsScalar()->SetInt(uiVar.intValue);
			break;
		case UIVariableType::Bool:
			uiVar.effectVariable->AsScalar()->SetBool(uiVar.boolValue);
			break;
		case UIVariableType::Color3:
		case UIVariableType::Color4:
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.colorValue);
			break;
		}
	}

	// Phase 2: Time-of-day interpolation for separated variables
	// Group time-period variables by base HLSL name
	struct TimePeriodEntry
	{
		size_t index;
		std::string period;
	};
	std::unordered_map<std::string, std::vector<TimePeriodEntry>> timeGroups;
	bool hasTimePeriodVars = false;

	for (size_t i = 0; i < uiVariables.size(); ++i) {
		auto& uiVar = uiVariables[i];
		if (uiVar.timePeriod.empty() || uiVar.isSeparator || !uiVar.effectVariable)
			continue;

		hasTimePeriodVars = true;

		// Extract base HLSL name by stripping the period suffix from the variable name
		std::string baseName;
		if (uiVar.name.size() > uiVar.timePeriod.size() &&
			uiVar.name.compare(uiVar.name.size() - uiVar.timePeriod.size(), uiVar.timePeriod.size(), uiVar.timePeriod) == 0)
			baseName = uiVar.name.substr(0, uiVar.name.size() - uiVar.timePeriod.size());
		else
			continue;

		timeGroups[baseName].push_back({ i, uiVar.timePeriod });
	}

	if (!hasTimePeriodVars)
		return;

	float weights[kNumExteriorPeriods];
	GetTimeOfDayWeights(weights);
	bool isInterior = IsPlayerInterior();

	for (auto& [baseName, entries] : timeGroups) {
		auto baseVarIt = variables.find(baseName);
		if (baseVarIt == variables.end())
			continue;

		auto* baseVar = baseVarIt->second.get();
		if (!baseVar || !baseVar->IsValid())
			continue;

		// Check for Interior entry
		size_t interiorIdx = SIZE_MAX;
		for (auto& entry : entries) {
			if (entry.period == "Interior") {
				interiorIdx = entry.index;
				break;
			}
		}

		auto& firstVar = uiVariables[entries[0].index];
		bool useInterior = isInterior && interiorIdx != SIZE_MAX;

		if (useInterior) {
			auto& intVar = uiVariables[interiorIdx];
			switch (intVar.type) {
			case UIVariableType::Float:
				baseVar->AsScalar()->SetFloat(intVar.floatValue);
				break;
			case UIVariableType::Int:
				baseVar->AsScalar()->SetInt(intVar.intValue);
				break;
			case UIVariableType::Bool:
				baseVar->AsScalar()->SetBool(intVar.boolValue);
				break;
			case UIVariableType::Color3:
			case UIVariableType::Color4:
				baseVar->AsVector()->SetFloatVector(intVar.colorValue);
				break;
			}
		} else {
			// Build period-to-index map for exterior periods
			size_t periodIndex[kNumExteriorPeriods] = {};
			bool hasPeriod[kNumExteriorPeriods] = {};
			for (auto& entry : entries) {
				if (entry.period == "Interior")
					continue;
				for (int p = 0; p < kNumExteriorPeriods; ++p) {
					if (entry.period == kPeriodNames[p]) {
						periodIndex[p] = entry.index;
						hasPeriod[p] = true;
						break;
					}
				}
			}

			// Normalize weights to only available periods
			float availWeights[kNumExteriorPeriods] = {};
			float totalWeight = 0.0f;
			for (int p = 0; p < kNumExteriorPeriods; ++p) {
				if (hasPeriod[p]) {
					availWeights[p] = weights[p];
					totalWeight += weights[p];
				}
			}

			if (totalWeight <= 0.0f) {
				// Redistribute: if only Day/Night exist, split weights
				for (int p = 0; p < kNumExteriorPeriods; ++p) {
					if (hasPeriod[p]) {
						availWeights[p] = 1.0f;
						totalWeight = 1.0f;
						break;
					}
				}
			}

			if (totalWeight <= 0.0f)
				continue;

			// Redistribute missing period weights to nearest available neighbors
			for (int p = 0; p < kNumExteriorPeriods; ++p) {
				if (!hasPeriod[p] && weights[p] > 0.0f) {
					// Find nearest available period
					int nearest = -1;
					int minDist = kNumExteriorPeriods;
					for (int q = 0; q < kNumExteriorPeriods; ++q) {
						if (hasPeriod[q]) {
							int dist = std::min(std::abs(p - q), kNumExteriorPeriods - std::abs(p - q));
							if (dist < minDist) {
								minDist = dist;
								nearest = q;
							}
						}
					}
					if (nearest >= 0) {
						availWeights[nearest] += weights[p];
						totalWeight += weights[p];
					}
				}
			}

			// Normalize
			for (int p = 0; p < kNumExteriorPeriods; ++p)
				availWeights[p] /= totalWeight;

			switch (firstVar.type) {
			case UIVariableType::Float:
				{
					float result = 0.0f;
					for (int p = 0; p < kNumExteriorPeriods; ++p) {
						if (hasPeriod[p] && availWeights[p] > 0.0f)
							result += uiVariables[periodIndex[p]].floatValue * availWeights[p];
					}
					baseVar->AsScalar()->SetFloat(result);
				}
				break;
			case UIVariableType::Int:
				{
					// For ints, use the period with highest weight
					int bestPeriod = -1;
					float bestWeight = -1.0f;
					for (int p = 0; p < kNumExteriorPeriods; ++p) {
						if (hasPeriod[p] && availWeights[p] > bestWeight) {
							bestWeight = availWeights[p];
							bestPeriod = p;
						}
					}
					if (bestPeriod >= 0)
						baseVar->AsScalar()->SetInt(uiVariables[periodIndex[bestPeriod]].intValue);
				}
				break;
			case UIVariableType::Bool:
				{
					int bestPeriod = -1;
					float bestWeight = -1.0f;
					for (int p = 0; p < kNumExteriorPeriods; ++p) {
						if (hasPeriod[p] && availWeights[p] > bestWeight) {
							bestWeight = availWeights[p];
							bestPeriod = p;
						}
					}
					if (bestPeriod >= 0)
						baseVar->AsScalar()->SetBool(uiVariables[periodIndex[bestPeriod]].boolValue);
				}
				break;
			case UIVariableType::Color3:
			case UIVariableType::Color4:
				{
					float result[4] = {};
					int comps = (firstVar.type == UIVariableType::Color3) ? 3 : 4;
					for (int p = 0; p < kNumExteriorPeriods; ++p) {
						if (hasPeriod[p] && availWeights[p] > 0.0f) {
							for (int c = 0; c < comps; ++c)
								result[c] += uiVariables[periodIndex[p]].colorValue[c] * availWeights[p];
						}
					}
					baseVar->AsVector()->SetFloatVector(result);
				}
				break;
			}
		}
	}
}

void Effect::RenderImGui()
{
	if (ImGui::CollapsingHeader(GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		bool valuesChanged = false;

		// Build unique name lookup for UIBinding evaluation
		auto computeUniqueName = [](const UIVariable& var) -> std::string {
			if (!var.uniqueName.empty())
				return var.uniqueName;
			if (!var.group.empty())
				return var.group + "." + var.displayName;
			return var.displayName;
		};

		std::unordered_map<std::string, int> uniqueNameMap;
		for (int i = 0; i < static_cast<int>(uiVariables.size()); ++i) {
			if (!uiVariables[i].isSeparator)
				uniqueNameMap[computeUniqueName(uiVariables[i])] = i;
		}

		auto evaluateBinding = [&](const UIVariable& var) -> std::pair<bool, bool> {
			bool visible = true;
			bool readOnly = var.isReadOnly;

			if (var.uiBinding.empty())
				return { visible, readOnly };

			auto it = uniqueNameMap.find(var.uiBinding);
			if (it == uniqueNameMap.end())
				return { visible, readOnly };

			const auto& boundVar = uiVariables[it->second];
			float boundValue = 0.0f;
			switch (boundVar.type) {
			case UIVariableType::Float:
				boundValue = boundVar.floatValue;
				break;
			case UIVariableType::Int:
				boundValue = static_cast<float>(boundVar.intValue);
				break;
			case UIVariableType::Bool:
				boundValue = boundVar.boolValue ? 1.0f : 0.0f;
				break;
			default:
				break;
			}

			bool conditionMet = false;
			if (var.uiBindingCondition.empty()) {
				conditionMet = (boundValue != 0.0f);
			} else {
				std::string cond = var.uiBindingCondition;
				std::string op;
				float comparand = 0.0f;

				if (cond.size() >= 2 && (cond.substr(0, 2) == "==" || cond.substr(0, 2) == "!=" ||
											cond.substr(0, 2) == "<=" || cond.substr(0, 2) == ">=" ||
											cond.substr(0, 2) == "=<" || cond.substr(0, 2) == "=>")) {
					op = cond.substr(0, 2);
					try {
						comparand = std::stof(cond.substr(2));
					} catch (...) {
					}
				} else if (!cond.empty() && (cond[0] == '<' || cond[0] == '>')) {
					op = cond.substr(0, 1);
					try {
						comparand = std::stof(cond.substr(1));
					} catch (...) {
					}
				}

				if (op == "==")
					conditionMet = (boundValue == comparand);
				else if (op == "!=")
					conditionMet = (boundValue != comparand);
				else if (op == "<")
					conditionMet = (boundValue < comparand);
				else if (op == ">")
					conditionMet = (boundValue > comparand);
				else if (op == "<=" || op == "=<")
					conditionMet = (boundValue <= comparand);
				else if (op == ">=" || op == "=>")
					conditionMet = (boundValue >= comparand);
			}

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
		};

		// Technique dropdown
		if (uiTechniques.size() > 1 && techniqueDropdownVisible) {
			ImGui::Text("%s", techniqueDropdownName.c_str());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			const char* currentDisplayName = uiTechniques[selectedTechniqueIndex].displayName.c_str();
			if (ImGui::BeginCombo(("##TECHNIQUE_" + GetName()).c_str(), currentDisplayName)) {
				for (uint32_t i = 0; i < uiTechniques.size(); ++i) {
					const bool isSelected = (selectedTechniqueIndex == i);
					if (ImGui::Selectable(uiTechniques[i].displayName.c_str(), isSelected)) {
						selectedTechniqueIndex = i;
						valuesChanged = true;
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// Build nested group tree
		struct GroupNode
		{
			std::string name;
			std::string fullPath;
			std::vector<int> varIndices;
			std::vector<std::unique_ptr<GroupNode>> children;
		};
		GroupNode root;

		for (int i = 0; i < static_cast<int>(uiVariables.size()); ++i) {
			GroupNode* node = &root;
			const std::string& groupPath = uiVariables[i].group;

			if (!uiVariables[i].isTopLevel && !groupPath.empty()) {
				std::istringstream ss(groupPath);
				std::string segment;
				std::string builtPath;
				while (std::getline(ss, segment, '.')) {
					if (!builtPath.empty())
						builtPath += ".";
					builtPath += segment;

					GroupNode* child = nullptr;
					for (auto& c : node->children) {
						if (c->name == segment) {
							child = c.get();
							break;
						}
					}
					if (!child) {
						auto newChild = std::make_unique<GroupNode>();
						newChild->name = segment;
						newChild->fullPath = builtPath;
						child = newChild.get();
						node->children.push_back(std::move(newChild));
					}
					node = child;
				}
			}

			node->varIndices.push_back(i);
		}

		// Sort variables within each node by UIOrdering (higher values first)
		std::function<void(GroupNode&)> sortNode = [&](GroupNode& node) {
			std::stable_sort(node.varIndices.begin(), node.varIndices.end(), [this](int a, int b) {
				return uiVariables[a].ordering > uiVariables[b].ordering;
			});
			std::stable_sort(node.children.begin(), node.children.end(), [this](const std::unique_ptr<GroupNode>& a, const std::unique_ptr<GroupNode>& b) {
				int orderA = 0, orderB = 0;
				auto itA = groupOrdering.find(a->fullPath);
				if (itA != groupOrdering.end())
					orderA = itA->second;
				auto itB = groupOrdering.find(b->fullPath);
				if (itB != groupOrdering.end())
					orderB = itB->second;
				return orderA > orderB;
			});
			for (auto& child : node.children)
				sortNode(*child);
		};
		sortNode(root);

		// Helper to render a single widget row (reusable for main and weather-separated values)
		auto renderWidget = [&](const std::string& label, const std::string& id, UIVariable& uiVar, UIVariableType type, UIWidgetType widget,
								float* floatVal, int* intVal, bool* boolVal, float* colorVal,
								bool readOnly) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			if (readOnly)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

			ImGui::Text("%s", label.c_str());

			bool isLabelOnly = ((type == UIVariableType::Float && uiVar.floatMin == 0 && uiVar.floatMax == 0) ||
								(type == UIVariableType::Int && uiVar.intMin == 0 && uiVar.intMax == 0));

			if (!isLabelOnly) {
				ImGui::TableSetColumnIndex(1);

				if (readOnly)
					ImGui::BeginDisabled();

				switch (type) {
				case UIVariableType::Float:
					if (ImGui::SliderFloat(id.c_str(), floatVal, uiVar.floatMin, uiVar.floatMax, "%.3f"))
						valuesChanged = true;
					break;
				case UIVariableType::Int:
					if ((widget == UIWidgetType::Dropdown || widget == UIWidgetType::Quality) && !uiVar.dropdownItems.empty()) {
						int displayIndex = *intVal;
						if (widget == UIWidgetType::Quality)
							displayIndex = *intVal + 1;
						const char* currentItem = (displayIndex >= 0 && displayIndex < (int)uiVar.dropdownItems.size()) ? uiVar.dropdownItems[displayIndex].c_str() : "";
						if (ImGui::BeginCombo(id.c_str(), currentItem)) {
							for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
								int itemValue = (widget == UIWidgetType::Quality) ? (j - 1) : j;
								if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), *intVal == itemValue)) {
									*intVal = itemValue;
									valuesChanged = true;
								}
							}
							ImGui::EndCombo();
						}
					} else {
						if (ImGui::SliderInt(id.c_str(), intVal, uiVar.intMin, uiVar.intMax))
							valuesChanged = true;
					}
					break;
				case UIVariableType::Bool:
					if (ImGui::Checkbox(id.c_str(), boolVal))
						valuesChanged = true;
					break;
				case UIVariableType::Color3:
					if (widget == UIWidgetType::Vector) {
						if (ImGui::SliderFloat3(id.c_str(), colorVal, -1.0f, 1.0f, "%.3f"))
							valuesChanged = true;
					} else {
						if (ImGui::ColorEdit3(id.c_str(), colorVal))
							valuesChanged = true;
					}
					break;
				case UIVariableType::Color4:
					if (ImGui::ColorEdit4(id.c_str(), colorVal))
						valuesChanged = true;
					break;
				}

				if (readOnly)
					ImGui::EndDisabled();
			}

			if (readOnly)
				ImGui::PopStyleColor();
		};

		auto renderVariable = [&](int varIdx) {
			auto& uiVar = uiVariables[varIdx];

			if (uiVar.isSeparator) {
				ImGui::Separator();
				return;
			}

			if (uiVar.displayName.empty())
				return;

			auto [bindVisible, bindReadOnly] = evaluateBinding(uiVar);
			if (!bindVisible)
				return;

			std::string baseId = "##" + std::to_string(varIdx) + "_" + GetName();

			// Render main variable
			renderWidget(uiVar.displayName, baseId, uiVar, uiVar.type, uiVar.widgetType,
				&uiVar.floatValue, &uiVar.intValue, &uiVar.boolValue, uiVar.colorValue,
				bindReadOnly);
		};

		int tableCounter = 0;
		std::function<void(GroupNode&)> renderGroup = [&](GroupNode& node) {
			if (!node.varIndices.empty()) {
				std::string tableId = "##et_" + GetName() + "_" + std::to_string(tableCounter++);
				if (ImGui::BeginTable(tableId.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
					float availWidth = ImGui::GetContentRegionAvail().x;
					ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.45f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.55f);

					for (int idx : node.varIndices)
						renderVariable(idx);

					ImGui::EndTable();
				}
			}

			for (auto& child : node.children) {
				std::string displayName = child->name;
				auto nameIt = groupDisplayNames.find(child->fullPath);
				if (nameIt != groupDisplayNames.end())
					displayName = nameIt->second;

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
				auto openIt = groupDefaultOpen.find(child->fullPath);
				if (openIt == groupDefaultOpen.end() || openIt->second)
					flags |= ImGuiTreeNodeFlags_DefaultOpen;

				std::string nodeLabel = displayName + "###grp_" + GetName() + "_" + child->fullPath;
				if (ImGui::TreeNodeEx(nodeLabel.c_str(), flags)) {
					renderGroup(*child);
					ImGui::TreePop();
				}
			}
		};

		renderGroup(root);

		if (valuesChanged)
			UpdateUIVariables();

		if (!errors.empty()) {
			for (const auto& error : errors)
				ImGui::TextWrapped("%s", error.c_str());
		}
	}
}

void Effect::EnumerateAllVariables()
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc))) {
		return;
	}

	variables.clear();

	// Iterate through all global variables in the effect
	for (UINT i = 0; i < effectDesc.GlobalVariables; ++i) {
		auto variable = effect->GetVariableByIndex(i);
		if (!variable || !variable->IsValid()) {
			continue;
		}

		D3DX11_EFFECT_VARIABLE_DESC varDesc;
		if (FAILED(variable->GetDesc(&varDesc))) {
			continue;
		}

		std::string varName = varDesc.Name;
		variables[varName].copy_from(variable);

	}

	logger::info("[ENBPP] Enumerated {} effect variables", variables.size());
}

ID3DX11EffectVariable* Effect::GetCachedVariable(const std::string& name)
{
	if (!effect)
		return nullptr;

	auto it = variableCache.find(name);
	if (it != variableCache.end()) {
		return it->second;
	}

	auto variable = effect->GetVariableByName(name.c_str());
	variableCache[name] = variable;
	return variable;
}

TextureManager::Texture* Effect::GetCachedCommonTexture(const std::string& name)
{
	auto it = commonTexturePointerCache.find(name);
	if (it != commonTexturePointerCache.end()) {
		return it->second;
	}

	auto* texture = TextureManager::GetSingleton().GetCommonTexture(name);
	commonTexturePointerCache[name] = texture;
	return texture;
}

void Effect::ClearVariableCache()
{
	variableCache.clear();
	commonTexturePointerCache.clear();
}

bool Effect::SetShaderResourceVariable(const std::string& variableName, ID3D11ShaderResourceView* resource)
{
	auto variable = GetCachedVariable(variableName);
	if (variable) {
		auto srVar = variable->AsShaderResource();
		if (srVar && srVar->IsValid()) {
			srVar->SetResource(resource);
			return true;
		}
	}
	return false;
}

bool Effect::SetShaderResourceVariable(ID3DX11Effect* effect, const std::string& variableName, ID3D11ShaderResourceView* resource)
{
	if (!effect)
		return false;

	auto variable = effect->GetVariableByName(variableName.c_str())->AsShaderResource();
	if (variable && variable->IsValid()) {
		variable->SetResource(resource);
		return true;
	}
	return false;
}

bool Effect::SetVectorVariable(ID3DX11Effect* effect, const std::string& variableName, const void* data, uint32_t size)
{
	if (!effect)
		return false;

	auto variable = effect->GetVariableByName(variableName.c_str());
	if (variable && variable->IsValid()) {
		variable->SetRawValue(data, 0, size);
		return true;
	}
	return false;
}

bool Effect::SetVectorVariable(const std::string& variableName, const void* data, uint32_t size)
{
	auto variable = GetCachedVariable(variableName);
	if (variable && variable->IsValid()) {
		variable->SetRawValue(data, 0, size);
		return true;
	}
	return false;
}

TextureManager::Texture Effect::CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName)
{
	auto device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	TextureManager::Texture texture{};
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, texture.texture.put()));
	DX::ThrowIfFailed(device->CreateRenderTargetView(texture.texture.get(), nullptr, texture.rtv.put()));
	DX::ThrowIfFailed(device->CreateShaderResourceView(texture.texture.get(), nullptr, texture.srv.put()));

	if (!debugName.empty()) {
		Util::SetResourceName(texture.texture.get(), (debugName).c_str());
		Util::SetResourceName(texture.rtv.get(), (debugName + " RTV").c_str());
		Util::SetResourceName(texture.srv.get(), (debugName + " SRV").c_str());
	}

	return texture;
}

std::string Effect::GetSelectedTechnique() const
{
	if (selectedTechniqueIndex < uiTechniques.size()) {
		return uiTechniques[selectedTechniqueIndex].techniqueName;
	} else if (selectedTechniqueIndex < availableTechniques.size()) {
		return availableTechniques[selectedTechniqueIndex];
	}
	// Fall back to first available technique
	if (!techniques.empty()) {
		return techniques.begin()->first;
	}
	return "";
}

void Effect::UpdateSizeVariables(ID3DX11Effect* effect, uint32_t outputWidth, uint32_t outputHeight)
{
	if (!effect)
		return;

	// Update ScreenSize (output)
	if (outputWidth > 0 && outputHeight > 0) {
		float screenSize[4];
		float aspect = static_cast<float>(outputWidth) / static_cast<float>(outputHeight);
		screenSize[0] = static_cast<float>(outputWidth);
		screenSize[1] = 1.0f / screenSize[0];
		screenSize[2] = aspect;
		screenSize[3] = 1.0f / aspect;
		SetVectorVariable(effect, "ScreenSize", screenSize, sizeof(screenSize));
	}
}
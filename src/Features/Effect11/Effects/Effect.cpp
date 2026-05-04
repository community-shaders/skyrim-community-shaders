#include "Effect.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

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
		std::vector<Effect::UIDefineInfo>& uiDefines,
		int depth);

	std::string InlineIncludes(const std::string& source,
		const std::filesystem::path& basePath,
		const std::string& iniPath,
		const std::string& iniSection,
		std::vector<std::filesystem::path>& includeDirs,
		std::unordered_set<std::string>& visited,
		std::vector<Effect::UIDefineInfo>& uiDefines,
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

						std::string expanded = ReadAndProcessInclude(fullPath, basePath, iniPath, iniSection, includeDirs, visited, uiDefines, depth + 1);
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
		std::vector<Effect::UIDefineInfo>& uiDefines,
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
		ENBExtender::ConvertExtenderSyntax(content, uiDefines, iniPath, iniSection);
		Util::ShaderPatches::Apply(fullPath.filename().string().c_str(), content);

		auto parentDir = fullPath.parent_path();
		if (std::find(includeDirs.begin(), includeDirs.end(), parentDir) == includeDirs.end())
			includeDirs.push_back(parentDir);

		return InlineIncludes(content, basePath, iniPath, iniSection, includeDirs, visited, uiDefines, depth);
	}

	class PresetInclude : public ID3DInclude
	{
	public:
		PresetInclude(const std::filesystem::path& a_basePath, std::vector<Effect::UIDefineInfo>& a_uiDefines, const std::string& a_iniPath = "", const std::string& a_iniSection = "") :
			basePath(a_basePath), uiDefines(a_uiDefines), iniPath(a_iniPath), iniSection(a_iniSection) {}

		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override
		{
			std::string_view name(pFileName);
			while (!name.empty() && (name.front() == '/' || name.front() == '\\'))
				name.remove_prefix(1);

			std::filesystem::path fullPath;
			bool found = false;

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
			ENBExtender::ConvertExtenderSyntax(content, uiDefines, iniPath, iniSection);
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
		std::vector<Effect::UIDefineInfo>& uiDefines;
		std::string iniPath;
		std::string iniSection;
		std::vector<std::filesystem::path> includeDirs;
	};
}

bool Effect::Load()
{
	std::filesystem::path iniPath = PresetManager::GetSingleton().GetENBSeriesPath();
	iniPath /= GetName() + ".ini";

	if (!std::filesystem::exists(iniPath)) {
		logger::info("[ENBPP] Could not find ini file '{}' for effect '{}', using defaults", iniPath.string(), GetName());
		return true;
	}

	auto writeTime = std::filesystem::last_write_time(iniPath);
	if (writeTime == lastIniWriteTime) {
		logger::info("[ENBPP] Skipping unchanged ini file '{}' for effect '{}'", iniPath.string(), GetName());
		return true;
	}
	lastIniWriteTime = writeTime;

	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (auto& uiVar : uiVariables) {
		if (uiVar.isSeparator || uiVar.isLabel)
			continue;
		if (!uiVar.effectVariable && !uiVar.isDefine)
			continue;
		std::string iniKey = GetVariableIniKey(uiVar);
		if (iniKey.empty())
			continue;
		std::vector<char> valueBuffer(1024);
		DWORD result = GetPrivateProfileStringA(section.c_str(), iniKey.c_str(), "", valueBuffer.data(), 1024, iniPath.string().c_str());
		if (result > 0) {
			std::string value(valueBuffer.data());
			LoadVariableFromString(uiVar, value);
		}
	}

	if (!uiTechniques.empty()) {
		uint32_t techniqueFromIni = static_cast<uint32_t>(GetPrivateProfileIntA(section.c_str(), "TECHNIQUE", selectedTechniqueIndex + 1, iniPath.string().c_str()));
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
	std::filesystem::path iniPath = PresetManager::GetSingleton().GetENBSeriesPath();
	iniPath /= GetName() + ".ini";

	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (const auto& uiVar : uiVariables) {
		if (uiVar.isSeparator || uiVar.isLabel)
			continue;
		if (!uiVar.effectVariable && !uiVar.isDefine)
			continue;
		std::string iniKey = GetVariableIniKey(uiVar);
		if (iniKey.empty())
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
		case UIVariableType::Float2:
		case UIVariableType::Float3:
		case UIVariableType::Float4:
			{
				std::ostringstream oss;
				int numComponents = (uiVar.type == UIVariableType::Float2) ? 2 : (uiVar.type == UIVariableType::Float3) ? 3 : 4;

				std::copy(uiVar.vectorValue, uiVar.vectorValue + numComponents - 1,
					std::ostream_iterator<float>(oss, ", "));
				oss << uiVar.vectorValue[numComponents - 1];

				value = oss.str();
			}
			break;
		}

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

	if (!isKIEFX) {
		if (!Load()) {
			errors.push_back("Failed to load settings");
			logger::error("[ENBPP] Failed to load settings for effect '{}'", GetName());
			return false;
		}
	}

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
	effectTextureCache.clear();
	uiTechniques.clear();
	selectedTechniqueIndex = 0;
	groupMeta.clear();
	techniqueDropdown = {};
	sourceGroupMap.clear();
	sourceOrderMap.clear();
	preprocessedSource.clear();

	ClearVariableCache();

	errors.clear();

	lastIniWriteTime = {};

	logger::info("[ENBPP] Unloaded effect '{}'", GetName());
}

bool Effect::LoadFXFile()
{
	auto filePath = PresetManager::GetSingleton().GetENBSeriesPath();
	filePath /= GetName();

	std::ifstream mainFile(filePath, std::ios::binary | std::ios::ate);
	if (!mainFile.is_open()) {
		errors.push_back("Failed to open effect file: " + filePath.string());
		return false;
	}

	std::streamsize size = mainFile.tellg();
	if (size < 0) {
		errors.push_back("Failed to determine size of effect file: " + filePath.string());
		return false;
	}
	mainFile.seekg(0, std::ios::beg);
	std::string sourceCode(size, '\0');
	if (!mainFile.read(sourceCode.data(), size)) {
		errors.push_back("Failed to read effect file: " + filePath.string());
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

	uiDefines.clear();
	ENBExtender::ConvertExtenderSyntax(sourceCode, uiDefines, iniPathStr, iniSection);
	Util::ShaderPatches::Apply(GetName().c_str(), sourceCode);

	auto filePathStr = filePath.string();

	auto compile = [&](const std::string& source, ID3DInclude* include) -> bool {
		winrt::com_ptr<ID3DBlob> compiled, err;
		HRESULT hr = D3DCompile(source.c_str(), source.size(), filePathStr.c_str(),
			nullptr, include, nullptr, "fx_5_0", 0, 0, compiled.put(), err.put());
		if (FAILED(hr)) {
			if (err)
				logger::warn("[ENBPP] D3DCompile failed for '{}': {}", filePathStr,
					std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()));
			return false;
		}
		return SUCCEEDED(D3DX11CreateEffectFromMemory(compiled->GetBufferPointer(),
			compiled->GetBufferSize(), 0, globals::d3d::device, effect.put()));
	};

	auto preprocess = [&](const std::string& source, ID3DInclude* include) -> std::string {
		winrt::com_ptr<ID3DBlob> blob, err;
		if (FAILED(D3DPreprocess(source.c_str(), source.size(), filePathStr.c_str(),
				nullptr, include, blob.put(), err.put())) || !blob)
			return {};
		return { static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize() };
	};

	auto tryPreprocessAndCompile = [&](const std::string& source, ID3DInclude* include) -> bool {
		auto pp = preprocess(source, include);
		if (pp.empty())
			return false;
		if (isKIEFX)
			preprocessedSource = pp;
		else
			ENBExtender::ParseSourceGroupScopes(pp, *this);
		StripLineDirectives(pp);
		return compile(pp, nullptr);
	};

	bool compiled = false;

	{
		PresetInclude ppInclude(enbseriesPath, uiDefines, iniPathStr, iniSection);
		compiled = tryPreprocessAndCompile(sourceCode, &ppInclude);
	}

	if (!compiled) {
		std::vector<std::filesystem::path> dirs = { enbseriesPath };
		std::unordered_set<std::string> visited;
		auto inlined = InlineIncludes(sourceCode, enbseriesPath, iniPathStr, iniSection, dirs, visited, uiDefines);
		compiled = tryPreprocessAndCompile(inlined, nullptr);
	}

	if (!compiled) {
		PresetInclude includeHandler(enbseriesPath, uiDefines, iniPathStr, iniSection);
		winrt::com_ptr<ID3DBlob> compiledShader, errorBlob;
		HRESULT hr = D3DCompile(sourceCode.c_str(), sourceCode.size(), filePathStr.c_str(),
			nullptr, &includeHandler, nullptr, "fx_5_0", 0, 0, compiledShader.put(), errorBlob.put());
		if (FAILED(hr)) {
			std::string errorMsg = "Compilation failed";
			if (errorBlob) {
				errorMsg = std::string(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
				logger::error("[ENBPP] Effect compilation failed for '{}'", filePathStr);
				std::istringstream errorStream(errorMsg);
				std::string errorLine;
				while (std::getline(errorStream, errorLine))
					if (!errorLine.empty())
						logger::error("[ENBPP]   {}", errorLine);
			}
			errors.push_back(errorMsg);
			return false;
		}
		if (FAILED(D3DX11CreateEffectFromMemory(compiledShader->GetBufferPointer(),
				compiledShader->GetBufferSize(), 0, globals::d3d::device, effect.put()))) {
			errors.push_back("Failed to create effect from compiled shader");
			return false;
		}
	}

	EnumerateAllVariables();
	SetupCustomTextures();
	LoadTechniques();
	LoadUITechniques();

	if (!isKIEFX)
		LoadUIVariables();

	logger::info("[ENBPP] Successfully loaded FX file: {}", filePathStr);
	return true;
}

Effect::TechniqueSequenceResult Effect::ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp)
{
	if (!IsCompiled() || !effect)
		return {};

	auto context = globals::d3d::context;

	if (a_baseTechniqueName.empty())
		return {};

	auto sequenceIt = techniques.find(a_baseTechniqueName);
	if (sequenceIt == techniques.end())
		return {};

	const auto& sequence = sequenceIt->second;
	if (sequence.empty())
		return {};

	auto sourceTexture = effect->GetVariableByName("TextureColor")->AsShaderResource();

	uint32_t swapCounter = 0;
	bool targetInOutput = false;

	ID3D11ShaderResourceView* inputSRV = nullptr;
	ID3D11RenderTargetView* outputRTV = nullptr;

	for (size_t i = 0; i < sequence.size(); ++i) {
		auto& techniqueInfo = sequence[i];

		if (!techniqueInfo.technique)
			continue;

		D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		if (sequence.size() == 1 || swapCounter == 0) {
			inputSRV = a_input;
			outputRTV = a_output.rtv.get();
		} else {
			bool useTemp = (swapCounter & 1) == 0;
			if (useTemp) {
				inputSRV = a_temp.srv.get();
				outputRTV = a_output.rtv.get();
			} else {
				inputSRV = a_output.srv.get();
				outputRTV = a_temp.rtv.get();
			}
		}

		if (!techniqueInfo.renderTargetName.empty()) {
			outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, outputRTV);
		} else {
			swapCounter++;
		}

		targetInOutput = (outputRTV == a_output.rtv.get());

		if (sourceTexture && sourceTexture->IsValid())
			sourceTexture->AsShaderResource()->SetResource(inputSRV);

		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		uint32_t outputWidth = 0, outputHeight = 0;

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

		UpdateSizeVariables(effect.get(), outputWidth, outputHeight);

		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(outputWidth);
		viewport.Height = static_cast<float>(outputHeight);
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
	if (!IsCompiled() || !effect)
		return;

	auto context = globals::d3d::context;

	auto technique = effect->GetTechniqueByName(techniqueName.c_str());
	if (!technique || !technique->IsValid())
		return;

	ID3D11RenderTargetView* rtvArray[] = { output.rtv.get() };
	context->OMSetRenderTargets(1, rtvArray, nullptr);

	D3D11_TEXTURE2D_DESC texDesc;
	output.texture->GetDesc(&texDesc);

	D3D11_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(texDesc.Width);
	viewport.Height = static_cast<float>(texDesc.Height);
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	D3DX11_TECHNIQUE_DESC techDesc;
	technique->GetDesc(&techDesc);

	for (UINT p = 0; p < techDesc.Passes; p++) {
		technique->GetPassByIndex(p)->Apply(0, context);
		context->Draw(4, 0);
	}
}

void Effect::SetupCustomTextures()
{
	for (auto& [varName, effectVar] : variables) {
		std::string resourceName = GetUIAnnotation(effectVar.get(), "ResourceName");
		if (resourceName.empty())
			continue;

		auto srv = LoadTextureFromFile(resourceName);
		if (srv) {
			auto shaderResourceVar = effectVar->AsShaderResource();
			if (shaderResourceVar && shaderResourceVar->IsValid())
				shaderResourceVar->SetResource(srv);
		}
	}
}

ID3D11ShaderResourceView* Effect::LoadTextureFromFile(const std::string& filename)
{
	auto device = globals::d3d::device;

	auto cacheIt = customTextureCache.find(filename);
	if (cacheIt != customTextureCache.end())
		return cacheIt->second.get();

	std::filesystem::path filepath = PresetManager::GetSingleton().GetENBSeriesPath() / filename;

	winrt::com_ptr<ID3D11Resource> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;

	HRESULT hr = DirectX::CreateDDSTextureFromFile(device, filepath.c_str(), texture.put(), srv.put());
	if (FAILED(hr))
		hr = DirectX::CreateWICTextureFromFile(device, filepath.c_str(), texture.put(), srv.put());

	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to load texture file: {} (HRESULT: 0x{:08X})", filepath.string(), static_cast<uint32_t>(hr));
		return nullptr;
	}

	customTextureCache[filename] = srv;
	return srv.get();
}

void Effect::LoadTechniques()
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc)))
		return;

	for (UINT g = 0; g < effectDesc.Groups; ++g) {
		auto group = effect->GetGroupByIndex(g);
		if (!group || !group->IsValid())
			continue;

		D3DX11_GROUP_DESC groupDesc;
		if (FAILED(group->GetDesc(&groupDesc)))
			continue;

		bool isNamedGroup = groupDesc.Name && groupDesc.Name[0];

		for (UINT t = 0; t < groupDesc.Techniques; ++t) {
			auto technique = group->GetTechniqueByIndex(t);
			if (!technique || !technique->IsValid())
				continue;

			D3DX11_TECHNIQUE_DESC techDesc;
			if (FAILED(technique->GetDesc(&techDesc)))
				continue;

			std::string key = isNamedGroup ? std::string(groupDesc.Name) : (techDesc.Name ? std::string(techDesc.Name) : ("technique" + std::to_string(t)));

			TechniqueInfo info;
			info.technique.copy_from(technique);
			info.renderTargetName = GetTechniqueAnnotation(technique, "RenderTarget");
			techniques[key].push_back(std::move(info));
		}
	}
}

void Effect::LoadUITechniques()
{
	uiTechniques.clear();
	selectedTechniqueIndex = 0;

	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc)))
		return;

	ENBExtender::LoadTechniqueDropdownMetadata(*this);

	uint32_t defaultIndex = 0;

	for (UINT g = 0; g < effectDesc.Groups; ++g) {
		auto group = effect->GetGroupByIndex(g);
		if (!group || !group->IsValid())
			continue;

		D3DX11_GROUP_DESC groupDesc;
		if (FAILED(group->GetDesc(&groupDesc)))
			continue;

		bool isNamedGroup = groupDesc.Name && groupDesc.Name[0];

		if (isNamedGroup) {
			std::string uiName = GetGroupAnnotation(group, "UIName");
			if (uiName.empty())
				continue;

			std::string isDefault = GetGroupAnnotation(group, "UIDefault");
			if (!isDefault.empty() && isDefault != "0" && isDefault != "false")
				defaultIndex = static_cast<uint32_t>(uiTechniques.size());

			uiTechniques.push_back({ std::string(groupDesc.Name), uiName });
		} else {
			for (UINT t = 0; t < groupDesc.Techniques; ++t) {
				auto technique = group->GetTechniqueByIndex(t);
				if (!technique || !technique->IsValid())
					continue;

				D3DX11_TECHNIQUE_DESC techDesc;
				if (FAILED(technique->GetDesc(&techDesc)))
					continue;

				std::string uiName = GetTechniqueAnnotation(technique, "UIName");
				if (uiName.empty())
					continue;

				std::string techName = techDesc.Name ? std::string(techDesc.Name) : "";

				std::string isDefault = GetTechniqueAnnotation(technique, "UIDefault");
				if (!isDefault.empty() && isDefault != "0" && isDefault != "false")
					defaultIndex = static_cast<uint32_t>(uiTechniques.size());

				uiTechniques.push_back({ techName, uiName });
			}
		}
	}

	if (defaultIndex < uiTechniques.size())
		selectedTechniqueIndex = defaultIndex;
}

ID3D11RenderTargetView* Effect::GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback)
{
	if (renderTargetName.empty())
		return fallback;

	auto it = effectTextureCache.find(renderTargetName);
	if (it != effectTextureCache.end() && it->second.rtv)
		return it->second.rtv.get();

	auto* texture = GetCachedCommonTexture(renderTargetName);
	if (texture && texture->rtv)
		return texture->rtv.get();

	return fallback;
}

void Effect::LoadUIVariables()
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc)))
		return;

	uiVariables.clear();

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

		if (typeDesc.Class == D3D_SVC_OBJECT && typeDesc.Type == D3D_SVT_STRING) {
			ENBExtender::ProcessExtenderStringVariable(variable, varDesc, groupStack, *this);
			continue;
		}

		UIVariable uiVar = {};
		if (ENBExtender::CreateUIVariable(uiVar, variable, varDesc, typeDesc, groupStack, *this)) {
			LoadUIVariableValue(uiVar);
			uiVariables.push_back(std::move(uiVar));
		}
	}

	ENBExtender::InsertUIDefines(*this);

	logger::info("[ENBPP] Loaded {} UI variables for effect '{}'", uiVariables.size(), GetName());
}

static std::string ReadAnnotationValue(ID3DX11EffectVariable* annotation)
{
	if (!annotation || !annotation->IsValid())
		return "";

	auto stringVar = annotation->AsString();
	if (stringVar && stringVar->IsValid()) {
		LPCSTR value = nullptr;
		if (SUCCEEDED(stringVar->GetString(&value)) && value)
			return std::string(value);
	}

	auto scalarVar = annotation->AsScalar();
	if (scalarVar && scalarVar->IsValid()) {
		auto annType = annotation->GetType();
		D3DX11_EFFECT_TYPE_DESC typeDesc;
		if (annType && SUCCEEDED(annType->GetDesc(&typeDesc))) {
			switch (typeDesc.Type) {
			case D3D_SVT_INT: { int v; if (SUCCEEDED(scalarVar->GetInt(&v))) return std::to_string(v); break; }
			case D3D_SVT_FLOAT: { float v; if (SUCCEEDED(scalarVar->GetFloat(&v))) return std::to_string(v); break; }
			case D3D_SVT_BOOL: { bool v; if (SUCCEEDED(scalarVar->GetBool(&v))) return std::to_string(v ? 1 : 0); break; }
			default: break;
			}
		}
		int intValue;
		if (SUCCEEDED(scalarVar->GetInt(&intValue)))
			return std::to_string(intValue);
	}
	return "";
}

std::string Effect::GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName)
{
	if (!variable)
		return "";

	auto annotation = variable->GetAnnotationByName(annotationName.c_str());
	if (annotation && annotation->IsValid()) {
		auto result = ReadAnnotationValue(annotation);
		if (!result.empty())
			return result;
	}

	D3DX11_EFFECT_VARIABLE_DESC varDesc;
	if (FAILED(variable->GetDesc(&varDesc)))
		return "";
	for (UINT i = 0; i < varDesc.Annotations; ++i) {
		auto ann = variable->GetAnnotationByIndex(i);
		if (!ann || !ann->IsValid())
			continue;
		D3DX11_EFFECT_VARIABLE_DESC annDesc;
		if (FAILED(ann->GetDesc(&annDesc)))
			continue;
		if (_stricmp(annDesc.Name, annotationName.c_str()) == 0)
			return ReadAnnotationValue(ann);
	}
	return "";
}

std::string Effect::GetTechniqueAnnotation(ID3DX11EffectTechnique* technique, const std::string& annotationName)
{
	if (!technique)
		return "";
	auto annotation = technique->GetAnnotationByName(annotationName.c_str());
	return ReadAnnotationValue(annotation);
}

std::string Effect::GetGroupAnnotation(ID3DX11EffectGroup* group, const std::string& annotationName)
{
	if (!group)
		return "";
	auto annotation = group->GetAnnotationByName(annotationName.c_str());
	return ReadAnnotationValue(annotation);
}

std::string Effect::GetVariableIniKey(const UIVariable& uiVar)
{
	if (!uiVar.uniqueName.empty())
		return uiVar.uniqueName;
	return uiVar.group.empty() ? uiVar.displayName : uiVar.group + "." + uiVar.displayName;
}

void Effect::LoadUIVariableValue(UIVariable& uiVar)
{
	switch (uiVar.type) {
	case UIVariableType::Float:
		uiVar.effectVariable->AsScalar()->GetFloat(&uiVar.floatValue);
		break;
	case UIVariableType::Int:
		uiVar.effectVariable->AsScalar()->GetInt(&uiVar.intValue);
		break;
	case UIVariableType::Bool:
		uiVar.effectVariable->AsScalar()->GetBool(&uiVar.boolValue);
		break;
	case UIVariableType::Float2:
	case UIVariableType::Float3:
	case UIVariableType::Float4:
		uiVar.effectVariable->AsVector()->GetFloatVector(uiVar.vectorValue);
		break;
	}
}

void Effect::LoadVariableFromString(UIVariable& uiVar, const std::string& value)
{
	try {
		switch (uiVar.type) {
		case UIVariableType::Float:
			uiVar.floatValue = std::stof(value);
			if (uiVar.effectVariable)
				uiVar.effectVariable->AsScalar()->SetFloat(uiVar.floatValue);
			break;
		case UIVariableType::Int:
			uiVar.intValue = std::stoi(value);
			if (uiVar.effectVariable)
				uiVar.effectVariable->AsScalar()->SetInt(uiVar.intValue);
			break;
		case UIVariableType::Bool:
			{
				std::string lowerValue = value;
				std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
				if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on")
					uiVar.boolValue = true;
				else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off")
					uiVar.boolValue = false;
				else
					uiVar.boolValue = std::stoi(value) != 0;
				if (uiVar.effectVariable)
					uiVar.effectVariable->AsScalar()->SetBool(uiVar.boolValue);
			}
			break;
		case UIVariableType::Float2:
		case UIVariableType::Float3:
		case UIVariableType::Float4:
			{
				std::istringstream ss(value);
				int numComponents = (uiVar.type == UIVariableType::Float2) ? 2 : (uiVar.type == UIVariableType::Float3) ? 3 : 4;
				for (int i = 0; i < numComponents; ++i) {
					char sep;
					ss >> uiVar.vectorValue[i];
					if (ss.peek() == ',')
						ss >> sep;
				}
				if (uiVar.effectVariable)
					uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.vectorValue);
			}
			break;
		}
	} catch (const std::exception& e) {
		logger::warn("[ENBPP] Failed to parse value '{}' for variable '{}': {}", value, uiVar.name, e.what());
	}
}

void Effect::UpdateUIVariables()
{
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
		case UIVariableType::Float2:
		case UIVariableType::Float3:
		case UIVariableType::Float4:
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.vectorValue);
			break;
		}
	}

	ENBExtender::ApplyTimeOfDayInterpolation(*this);
}

void Effect::RenderImGui()
{
	ENBExtender::RenderUI(*this);
}

void Effect::EnumerateAllVariables()
{
	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc)))
		return;

	variables.clear();

	for (UINT i = 0; i < effectDesc.GlobalVariables; ++i) {
		auto variable = effect->GetVariableByIndex(i);
		if (!variable || !variable->IsValid())
			continue;

		D3DX11_EFFECT_VARIABLE_DESC varDesc;
		if (FAILED(variable->GetDesc(&varDesc)))
			continue;

		variables[varDesc.Name].copy_from(variable);
	}
}

ID3DX11EffectVariable* Effect::GetCachedVariable(const std::string& name)
{
	if (!effect)
		return nullptr;

	auto it = variableCache.find(name);
	if (it != variableCache.end())
		return it->second;

	auto variable = effect->GetVariableByName(name.c_str());
	variableCache[name] = variable;
	return variable;
}

TextureManager::Texture* Effect::GetCachedCommonTexture(const std::string& name)
{
	auto it = commonTexturePointerCache.find(name);
	if (it != commonTexturePointerCache.end())
		return it->second;

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
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

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
	if (selectedTechniqueIndex < uiTechniques.size())
		return uiTechniques[selectedTechniqueIndex].techniqueName;
	if (!techniques.empty())
		return techniques.begin()->first;
	return "";
}

void Effect::UpdateSizeVariables(ID3DX11Effect* effect, uint32_t outputWidth, uint32_t outputHeight)
{
	if (!effect || outputWidth == 0 || outputHeight == 0)
		return;

	float aspect = static_cast<float>(outputWidth) / static_cast<float>(outputHeight);
	float screenSize[4] = { static_cast<float>(outputWidth), 1.0f / outputWidth, aspect, 1.0f / aspect };
	SetVectorVariable(effect, "ScreenSize", screenSize, sizeof(screenSize));
}

#include "Effect.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>

#include "../ENBExtender.h"
#include "../TextureManager.h"
#include "State.h"

bool Effect::Load()
{
	logger::debug("[ENBPP] Loading settings for effect '{}'", GetName());

	// Create ini file path based on effect name
	std::filesystem::path iniPath = "enbseries";
	iniPath /= GetName() + ".ini";

	// Check if file exists
	if (!std::filesystem::exists(iniPath)) {
		logger::debug("[ENBPP] Could not find ini file '{}' for effect '{}', using defaults", iniPath.string(), GetName());
		return true;  // Not an error, just use defaults
	}

	// Skip reload if the file has not changed since last load
	auto writeTime = std::filesystem::last_write_time(iniPath);
	if (writeTime == lastIniWriteTime) {
		logger::debug("[ENBPP] Skipping unchanged ini file '{}' for effect '{}'", iniPath.string(), GetName());
		return true;
	}
	lastIniWriteTime = writeTime;

	// Prepare section name
	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (auto& uiVar : uiVariables) {
		std::vector<char> valueBuffer(1024);
		DWORD result = GetPrivateProfileStringA(section.c_str(), uiVar.displayName.c_str(), "", valueBuffer.data(), 1024, iniPath.string().c_str());
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
	logger::debug("[ENBPP] Saving settings for effect '{}'", GetName());

	// Create ini file path based on effect name
	std::filesystem::path iniPath = "enbseries";
	iniPath /= GetName() + ".ini";

	// Prepare section name
	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (const auto& uiVar : uiVariables) {
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

		BOOL result = WritePrivateProfileStringA(section.c_str(), uiVar.displayName.c_str(), value.c_str(), iniPath.string().c_str());
		if (!result) {
			logger::warn("[ENBPP] Failed to write key '{}' to ini file '{}'", uiVar.displayName, iniPath.string());
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

	logger::debug("[ENBPP] Successfully applied effect '{}'", GetName());
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

	ClearVariableCache();

	errors.clear();

	// Reset write time so the next Load() after Apply() always reads fresh values
	lastIniWriteTime = {};

	logger::debug("[ENBPP] Unloaded effect '{}'", GetName());
}

bool Effect::LoadFXFile()
{
	auto filePath = std::filesystem::path("enbseries");
	filePath /= GetName();

	// Read main effect file
	std::ifstream mainFile(filePath, std::ios::binary | std::ios::ate);
	if (!mainFile.is_open()) {
		errors.push_back("Failed to open effect file: " + filePath.string());
		logger::error("[ENBPP] Failed to open effect file: {}", filePath.string());
		return false;
	}

	std::streamsize size = mainFile.tellg();
	mainFile.seekg(0, std::ios::beg);
	std::string sourceCode(size, '\0');
	if (!mainFile.read(sourceCode.data(), size)) {
		errors.push_back("Failed to read effect file: " + filePath.string());
		logger::error("[ENBPP] Failed to read effect file: {}", filePath.string());
		return false;
	}
	mainFile.close();

	// Preprocess main source code for ENB Extender compatibility
	sourceCode = ENBExtender::PreprocessSource(sourceCode);

	// Create custom include handler for ENB Extender compatibility
	ENBExtender::IncludeHandler includeHandler(std::filesystem::path("enbseries"));

	winrt::com_ptr<ID3DBlob> compiledShader;
	winrt::com_ptr<ID3DBlob> errorBlob;

	// Compile the effect with custom include handler
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
			errorMsg = std::string(static_cast<const char*>(errorBlob->GetBufferPointer()));
			logger::error("[ENBPP] Effect compilation failed for '{}'", filePath.string());
			// Log each line of the error separately for better readability in log file
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

	// Create effect from compiled shader
	hr = D3DX11CreateEffectFromMemory(
		compiledShader->GetBufferPointer(),
		compiledShader->GetBufferSize(),
		0,
		globals::d3d::device,
		effect.put());

	if (FAILED(hr)) {
		std::string errorMsg = "Failed to create effect from compiled shader";
		logger::error("[ENBPP] {} for '{}': HRESULT 0x{:08X}", errorMsg, filePath.string(), static_cast<unsigned int>(hr));
		errors.push_back(errorMsg);
		return false;
	}

	// Common textures and variables are now managed by EffectManager
	EnumerateAllVariables();

	SetupCustomTextures();
	LoadTechniques();
	LoadUITechniques();

	logger::debug("[ENBPP] Effect '{}' compiled successfully with {} UI techniques", GetName(), uiTechniques.size());

	// Populate available techniques for UI selection
	availableTechniques = GetBaseTechniqueNames();

	// Set default selected technique to first annotated technique
	if (!uiTechniques.empty()) {
		selectedTechniqueIndex = 0;  // Default to first annotated technique
	}

	LoadUIVariables();

	logger::debug("[ENBPP] Successfully loaded FX file: {}", filePath.string());
	return true;
}

bool Effect::ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp)
{
	if (!IsCompiled() || !effect) {
		return false;  // Skip execution if not compiled
	}

	auto context = globals::d3d::context;

	// Check if the technique sequence exists
	auto sequenceIt = techniques.find(a_baseTechniqueName);
	if (sequenceIt == techniques.end()) {
		logger::debug("[ENBPP] Technique sequence '{}' not found", a_baseTechniqueName);
		return false;
	}

	const auto& sequence = sequenceIt->second;

	if (sequence.empty()) {
		logger::debug("[ENBPP] Technique sequence '{}' is empty", a_baseTechniqueName);
		return false;
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
			inputResource.as(inputTexture);
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
			outputResource.as(outputTexture);
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

	return targetInOutput;
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

	// Construct full path - check enbseries folder first
	std::filesystem::path filepath = std::filesystem::path{ "enbseries" } / filename;

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

		std::string techniqueName = techDesc.Name;
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
	ForEachTechniqueSequence(effect.get(), [this](ID3DX11EffectTechnique* technique, const std::string& baseName, const std::string& techniqueName, int sequenceNumber) {
		std::string renderTargetName = GetRenderTargetFromTechnique(technique);

		if (techniques[baseName].size() <= static_cast<size_t>(sequenceNumber))
			techniques[baseName].resize(sequenceNumber + 1);

		TechniqueInfo techInfo;
		techInfo.technique.copy_from(technique);
		techInfo.renderTargetName = renderTargetName;
		techniques[baseName][sequenceNumber] = std::move(techInfo);

		logger::debug("[ENBPP] Loaded technique '{}' as base '{}' sequence {}", techniqueName, baseName, sequenceNumber);
	});

	for (const auto& [baseName, sequence] : techniques) {
		logger::debug("[ENBPP] Technique sequence '{}' has {} techniques", baseName, sequence.size());
	}
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

void Effect::LoadUITechniques()
{
	uiTechniques.clear();
	selectedTechniqueIndex = 0;

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

		logger::debug("[ENBPP] Added UI technique '{}' with display name '{}'", baseName, uiName);
	});

	logger::debug("[ENBPP] Loaded {} UI techniques", uiTechniques.size());
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

		// Check if this variable has UI annotations
		std::string uiName = GetUIAnnotation(variable, "UIName");
		if (uiName.empty()) {
			continue;  // No UI annotation, skip
		}

		UIVariable uiVar = {};
		uiVar.name = varDesc.Name;
		uiVar.displayName = uiName;
		uiVar.effectVariable.copy_from(variable);

		// Determine variable type
		D3DX11_EFFECT_TYPE_DESC typeDesc;
		auto effectType = variable->GetType();
		if (SUCCEEDED(effectType->GetDesc(&typeDesc))) {
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
				if (typeDesc.Columns == 3) {
					uiVar.type = UIVariableType::Color3;
				} else if (typeDesc.Columns == 4) {
					uiVar.type = UIVariableType::Color4;
				} else {
					continue;
				}
			} else {
				continue;
			}
		} else {
			continue;
		}

		// Parse UI widget type
		std::string widgetStr = GetUIAnnotation(variable, "UIWidget");
		uiVar.widgetType = ParseWidgetType(widgetStr);

		logger::debug("[ENBPP] Variable '{}': UIWidget='{}', parsed as {}",
			uiVar.name, widgetStr, static_cast<int>(uiVar.widgetType));

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

				// Parse dropdown list if it's a dropdown widget
				if (uiVar.widgetType == UIWidgetType::Dropdown) {
					std::string listStr = GetUIAnnotation(variable, "UIList");
					logger::debug("[ENBPP] Variable '{}': UIList='{}'", uiVar.name, listStr);
					if (!listStr.empty()) {
						uiVar.dropdownItems = ParseDropdownList(listStr);
						logger::debug("[ENBPP] Parsed {} dropdown items", uiVar.dropdownItems.size());
					}
				}
			}
		} catch (const std::exception& e) {
			logger::warn("[ENBPP] Failed to parse UI annotations for variable '{}': {}", uiVar.name, e.what());
		}

		// Load current value
		LoadUIVariableValue(uiVar);

		uiVariables.push_back(uiVar);

		// Debug logging
		if (uiVar.widgetType == UIWidgetType::Dropdown) {
			logger::debug("[ENBPP] Loaded UI variable '{}' with display name '{}', dropdown items: {}",
				uiVar.name, uiVar.displayName, uiVar.dropdownItems.size());
			for (const auto& item : uiVar.dropdownItems) {
				logger::debug("[ENBPP]   - {}", item);
			}
		} else {
			logger::debug("[ENBPP] Loaded UI variable '{}' with display name '{}'", uiVar.name, uiVar.displayName);
		}
	}

	logger::debug("[ENBPP] Loaded {} UI variables", uiVariables.size());
}

std::string Effect::GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName)
{
	if (!variable) {
		return "";
	}

	D3DX11_EFFECT_VARIABLE_DESC varDesc;
	if (FAILED(variable->GetDesc(&varDesc))) {
		return "";
	}

	for (UINT i = 0; i < varDesc.Annotations; ++i) {
		auto annotation = variable->GetAnnotationByIndex(i);
		if (!annotation || !annotation->IsValid()) {
			continue;
		}

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(annotation->GetDesc(&annotationDesc))) {
			continue;
		}

		if (std::string(annotationDesc.Name) == annotationName) {
			// Try string annotation first
			auto stringVar = annotation->AsString();
			if (stringVar && stringVar->IsValid()) {
				LPCSTR value = nullptr;
				if (SUCCEEDED(stringVar->GetString(&value)) && value) {
					return std::string(value);
				}
			}

			// Try integer annotation (for UIMin, UIMax, etc.)
			auto intVar = annotation->AsScalar();
			if (intVar && intVar->IsValid()) {
				int intValue;
				if (SUCCEEDED(intVar->GetInt(&intValue))) {
					return std::to_string(intValue);
				}

				// Also try float annotation
				float floatValue;
				if (SUCCEEDED(intVar->GetFloat(&floatValue))) {
					return std::to_string(floatValue);
				}
			}
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

void Effect::UpdateUIVariables()
{
	for (auto& uiVar : uiVariables) {
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
}

void Effect::RenderImGui()
{
	if (ImGui::CollapsingHeader(GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		bool valuesChanged = false;

		// Use table
		if (ImGui::BeginTable(("effect_table_" + GetName()).c_str(), 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			if (!uiTechniques.empty()) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("TECHNIQUE");

				ImGui::TableSetColumnIndex(1);
				const char* currentDisplayName = uiTechniques[selectedTechniqueIndex].displayName.c_str();
				if (ImGui::BeginCombo(("##TECHNIQUE_" + GetName()).c_str(), currentDisplayName)) {
					for (uint32_t i = 0; i < uiTechniques.size(); ++i) {
						const bool isSelected = (selectedTechniqueIndex == i);
						if (ImGui::Selectable(uiTechniques[i].displayName.c_str(), isSelected)) {
							selectedTechniqueIndex = i;
							valuesChanged = true;
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			for (auto& uiVar : uiVariables) {
				if (uiVar.displayName.empty() || std::all_of(uiVar.displayName.begin(), uiVar.displayName.end(), [](char c) { return std::isspace(c); })) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Spacing();
					continue;
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", uiVar.displayName.c_str());

				// Skip inputs
				bool isLabelOnly = ((uiVar.type == UIVariableType::Float && uiVar.floatMin == 0 && uiVar.floatMax == 0) ||
									(uiVar.type == UIVariableType::Int && uiVar.intMin == 0 && uiVar.intMax == 0));

				if (isLabelOnly) {
					continue;
				}

				ImGui::TableSetColumnIndex(1);

				std::string id = "##" + uiVar.displayName + "_" + GetName();
				const char* currentItem = "";

				switch (uiVar.type) {
				case UIVariableType::Float:
					if (ImGui::SliderFloat(id.c_str(), &uiVar.floatValue, uiVar.floatMin, uiVar.floatMax, "%.3f")) {
						valuesChanged = true;
					}
					break;
				case UIVariableType::Int:
					if (uiVar.widgetType == UIWidgetType::Dropdown && !uiVar.dropdownItems.empty()) {
						// For dropdowns
						currentItem = (uiVar.intValue >= 0 && uiVar.intValue < (int)uiVar.dropdownItems.size()) ? uiVar.dropdownItems[uiVar.intValue].c_str() : "";
						if (ImGui::BeginCombo(id.c_str(), currentItem)) {
							for (int i = 0; i < uiVar.dropdownItems.size(); ++i) {
								if (ImGui::Selectable(uiVar.dropdownItems[i].c_str(), uiVar.intValue == i)) {
									uiVar.intValue = i;
									valuesChanged = true;
								}
							}
							ImGui::EndCombo();
						}
					} else {
						if (ImGui::SliderInt(id.c_str(), &uiVar.intValue, uiVar.intMin, uiVar.intMax)) {
							valuesChanged = true;
						}
					}
					break;
				case UIVariableType::Bool:
					if (ImGui::Checkbox(id.c_str(), &uiVar.boolValue)) {
						valuesChanged = true;
					}
					break;
				case UIVariableType::Color3:
					if (ImGui::ColorEdit3(id.c_str(), uiVar.colorValue)) {
						valuesChanged = true;
					}
					break;
				case UIVariableType::Color4:
					if (ImGui::ColorEdit4(id.c_str(), uiVar.colorValue)) {
						valuesChanged = true;
					}
					break;
				}
			}

			ImGui::EndTable();
		}

		// Update shader variables if any values changed
		if (valuesChanged) {
			UpdateUIVariables();
		}

		// Show compilation errors if any
		if (!errors.empty()) {
			for (const auto& error : errors) {
				ImGui::TextWrapped("%s", error.c_str());
			}
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

		logger::debug("[ENBPP] Enumerated variable: {}", varName);
	}

	logger::debug("[ENBPP] Enumerated {} effect variables", variables.size());
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
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

	selectedTechniqueIndex = static_cast<uint32_t>(GetPrivateProfileIntA(section.c_str(), "TECHNIQUE", selectedTechniqueIndex, iniPath.string().c_str()));

	selectedTechniqueIndex = std::clamp(selectedTechniqueIndex, 1u, (uint)uiTechniques.size()) - 1u;

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

	errors.clear();

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

void Effect::ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp)
{
	if (!IsCompiled() || !effect) {
		return;  // Skip execution if not compiled
	}

	auto context = globals::d3d::context;

	// Check if the technique sequence exists
	auto sequenceIt = techniques.find(a_baseTechniqueName);
	if (sequenceIt == techniques.end()) {
		logger::debug("[ENBPP] Technique sequence '{}' not found", a_baseTechniqueName);
		return;
	}

	const auto& sequence = sequenceIt->second;

	if (sequence.empty()) {
		logger::debug("[ENBPP] Technique sequence '{}' is empty", a_baseTechniqueName);
		return;
	}

	auto sourceTexture = effect->GetVariableByName("TextureColor")->AsShaderResource();

	uint32_t swapCounter = 0;  // Track swap count for ping-ponging between output and temp
	bool targetInOutput = false;

	for (size_t i = 0; i < sequence.size(); ++i) {
		auto& techniqueInfo = sequence[i];

		if (!techniqueInfo.technique) {
			logger::warn("[ENBPP] Technique {} in sequence '{}' is null, skipping", i, a_baseTechniqueName);
			continue;
		}

		D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		// Determine input and output for this technique
		ID3D11ShaderResourceView* inputSRV;
		ID3D11RenderTargetView* outputRTV;

		if (sequence.size() == 1) {
			// Single technique: input -> output
			inputSRV = a_input;
			outputRTV = a_output.rtv.get();
			if (techniqueInfo.renderTargetName.empty())
				targetInOutput = true;
		} else if (swapCounter == 0) {
			// First pass: input -> output (start the ping-pong with output)
			inputSRV = a_input;
			outputRTV = a_output.rtv.get();
			if (techniqueInfo.renderTargetName.empty())
				targetInOutput = true;
		} else {
			// Subsequent passes: ping-pong between output and temp
			bool useTemp = (swapCounter & 1) == 0;  // Use counter LSB for swap determination
			if (useTemp) {
				// Read from temp, write to output
				inputSRV = a_temp.srv.get();
				outputRTV = a_output.rtv.get();
				if (techniqueInfo.renderTargetName.empty())
					targetInOutput = true;
			} else {
				// Read from output, write to temp
				inputSRV = a_output.srv.get();
				outputRTV = a_temp.rtv.get();
				if (techniqueInfo.renderTargetName.empty())
					targetInOutput = false;
			}
		}

		// Handle custom render target if specified
		if (!techniqueInfo.renderTargetName.empty()) {
			outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, outputRTV);
		} else {
			swapCounter++;  // Increment counter for next iteration
		}

		if (sourceTexture && sourceTexture->IsValid()) {
			sourceTexture->AsShaderResource()->SetResource(inputSRV);
		}

		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		// Set viewport based on render target description
		winrt::com_ptr<ID3D11Resource> resource;
		outputRTV->GetResource(resource.put());
		winrt::com_ptr<ID3D11Texture2D> texture;
		resource.as(texture);
		D3D11_TEXTURE2D_DESC texDesc;
		texture->GetDesc(&texDesc);

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(texDesc.Width);
		viewport.Height = static_cast<float>(texDesc.Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			techniqueInfo.technique->GetPassByIndex(p)->Apply(0, context);
			context->Draw(4, 0);
		}
	}

	if (!targetInOutput) {
		context->CopyResource(a_output.texture.get(), a_temp.texture.get());
	}
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
	if (!variable) {
		return "";
	}

	// Get the variable's annotation count
	D3DX11_EFFECT_VARIABLE_DESC varDesc;
	if (FAILED(variable->GetDesc(&varDesc))) {
		return "";
	}

	// Look for ResourceName annotation
	for (UINT i = 0; i < varDesc.Annotations; ++i) {
		auto annotation = variable->GetAnnotationByIndex(i);
		if (!annotation || !annotation->IsValid()) {
			continue;
		}

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(annotation->GetDesc(&annotationDesc))) {
			continue;
		}

		// Check if this is the ResourceName annotation
		if (std::string(annotationDesc.Name) == "ResourceName") {
			// Get the string value
			auto stringVar = annotation->AsString();
			if (stringVar && stringVar->IsValid()) {
				LPCSTR resourceName = nullptr;
				if (SUCCEEDED(stringVar->GetString(&resourceName)) && resourceName) {
					return std::string(resourceName);
				}
			}
		}
	}

	return "";
}

void Effect::LoadTechniques()
{
	D3DX11_EFFECT_DESC effectDesc;
	DX::ThrowIfFailed(effect->GetDesc(&effectDesc));

	std::string currentSequenceBaseName;
	int currentSequenceIndex = 0;

	// Load all techniques and organize them into sequences
	for (UINT i = 0; i < effectDesc.Techniques; ++i) {
		auto technique = effect->GetTechniqueByIndex(i);
		if (!technique->IsValid()) {
			continue;
		}

		D3DX11_TECHNIQUE_DESC techDesc;
		DX::ThrowIfFailed(technique->GetDesc(&techDesc));

		std::string techniqueName = techDesc.Name;

		// Determine the base technique name and sequence number
		std::string baseName;
		int sequenceNumber = 0;

		// Check if this continues the current sequence
		if (!currentSequenceBaseName.empty()) {
			std::string expectedName = currentSequenceBaseName + std::to_string(currentSequenceIndex + 1);
			if (techniqueName == expectedName) {
				// Continue current sequence
				baseName = currentSequenceBaseName;
				sequenceNumber = currentSequenceIndex + 1;
				currentSequenceIndex++;
			} else {
				// Start new sequence with this technique
				baseName = techniqueName;
				sequenceNumber = 0;
				currentSequenceBaseName = techniqueName;
				currentSequenceIndex = 0;
			}
		} else {
			// First technique or start new sequence
			baseName = techniqueName;
			sequenceNumber = 0;
			currentSequenceBaseName = techniqueName;
			currentSequenceIndex = 0;
		}

		// Get RenderTarget annotation
		std::string renderTargetName = GetRenderTargetFromTechnique(technique);

		// Ensure the technique sequence vector exists and is large enough
		if (techniques[baseName].size() <= sequenceNumber) {
			techniques[baseName].resize(sequenceNumber + 1);
		}

		// Store the technique info in the correct sequence position
		TechniqueInfo techInfo;
		techInfo.technique.copy_from(technique);
		techInfo.renderTargetName = renderTargetName;
		techniques[baseName][sequenceNumber] = std::move(techInfo);

		logger::debug("[ENBPP] Loaded technique '{}' as base '{}' sequence {}", techniqueName, baseName, sequenceNumber);
	}

	// Log the technique sequences found
	for (const auto& [baseName, sequence] : techniques) {
		logger::debug("[ENBPP] Technique sequence '{}' has {} techniques", baseName, sequence.size());
	}
}

std::vector<std::string> Effect::GetBaseTechniqueNames()
{
	std::vector<std::string> baseNames;
	baseNames.reserve(techniques.size());

	for (const auto& [baseName, sequence] : techniques) {
		// Only include sequences that have at least the base technique (index 0)
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

	D3DX11_EFFECT_DESC effectDesc;
	if (FAILED(effect->GetDesc(&effectDesc))) {
		return;
	}

	// Load all techniques that have UIName annotations
	for (UINT i = 0; i < effectDesc.Techniques; ++i) {
		auto technique = effect->GetTechniqueByIndex(i);
		if (!technique->IsValid()) {
			continue;
		}

		D3DX11_TECHNIQUE_DESC techDesc;
		if (FAILED(technique->GetDesc(&techDesc))) {
			continue;
		}

		std::string techniqueName = techDesc.Name;
		std::string uiName = GetUINameFromTechnique(technique);

		// Only include techniques with UIName annotations
		if (!uiName.empty()) {
			UITechnique uiTech;
			uiTech.techniqueName = techniqueName;
			uiTech.displayName = uiName;
			uiTechniques.push_back(uiTech);

			logger::debug("[ENBPP] Added UI technique '{}' with display name '{}'", techniqueName, uiName);
		}
	}

	logger::debug("[ENBPP] Loaded {} UI techniques", uiTechniques.size());
}

std::string Effect::GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique)
{
	if (!technique) {
		return "";
	}

	// Get the technique's annotation count
	D3DX11_TECHNIQUE_DESC techDesc;
	if (FAILED(technique->GetDesc(&techDesc))) {
		return "";
	}

	// Look for RenderTarget annotation
	for (UINT i = 0; i < techDesc.Annotations; ++i) {
		auto annotation = technique->GetAnnotationByIndex(i);
		if (!annotation || !annotation->IsValid()) {
			continue;
		}

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(annotation->GetDesc(&annotationDesc))) {
			continue;
		}

		// Check if this is the RenderTarget annotation
		if (std::string(annotationDesc.Name) == "RenderTarget") {
			// Get the string value
			auto stringVar = annotation->AsString();
			if (stringVar && stringVar->IsValid()) {
				LPCSTR renderTargetName = nullptr;
				if (SUCCEEDED(stringVar->GetString(&renderTargetName)) && renderTargetName) {
					return std::string(renderTargetName);
				}
			}
		}
	}

	return "";
}

std::string Effect::GetUINameFromTechnique(ID3DX11EffectTechnique* technique)
{
	if (!technique) {
		return "";
	}

	// Get the technique's annotation count
	D3DX11_TECHNIQUE_DESC techDesc;
	if (FAILED(technique->GetDesc(&techDesc))) {
		return "";
	}

	// Look for UIName annotation
	for (UINT i = 0; i < techDesc.Annotations; ++i) {
		auto annotation = technique->GetAnnotationByIndex(i);
		if (!annotation || !annotation->IsValid()) {
			continue;
		}

		D3DX11_EFFECT_VARIABLE_DESC annotationDesc;
		if (FAILED(annotation->GetDesc(&annotationDesc))) {
			continue;
		}

		// Check if this is the UIName annotation
		if (std::string(annotationDesc.Name) == "UIName") {
			// Get the string value
			auto stringVar = annotation->AsString();
			if (stringVar && stringVar->IsValid()) {
				LPCSTR uiName = nullptr;
				if (SUCCEEDED(stringVar->GetString(&uiName)) && uiName) {
					return std::string(uiName);
				}
			}
		}
	}

	return "";
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

	// Get render target from EffectManager's common texture cache
	auto& textureManager = TextureManager::GetSingleton();
	texture = textureManager.GetCommonTexture(renderTargetName);
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
	if (ImGui::CollapsingHeader(GetName().c_str())) {
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

bool Effect::SetShaderResourceVariable(const std::string& variableName, ID3D11ShaderResourceView* resource)
{
	return SetShaderResourceVariable(effect.get(), variableName, resource);
}

bool Effect::SetShaderResourceVariable(ID3DX11Effect* effect, const std::string& variableName, ID3D11ShaderResourceView* resource)
{
	auto variable = effect->GetVariableByName(variableName.c_str())->AsShaderResource();
	if (variable && variable->IsValid()) {
		variable->SetResource(resource);
		return true;
	}
	return false;
}

bool Effect::SetVectorVariable(ID3DX11Effect* effect, const std::string& variableName, const void* data, uint32_t size)
{
	auto variable = effect->GetVariableByName(variableName.c_str());
	if (variable && variable->IsValid()) {
		variable->SetRawValue(data, 0, size);
		return true;
	}
	return false;
}

bool Effect::SetVectorVariable(const std::string& variableName, const void* data, uint32_t size)
{
	auto variable = effect->GetVariableByName(variableName.c_str());
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
	}
	return "";
}
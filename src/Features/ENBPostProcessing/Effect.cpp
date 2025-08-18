#include "Effect.h"
#include "EffectManager.h"
#include "Globals.h"
#include "PCH.h"
#include "State.h"

#include <chrono>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>

bool Effect::Load()
{
	logger::debug("Loading settings for effect '{}'", GetName());
	return true;
}

void Effect::Save()
{
	logger::debug("Saving settings for effect '{}'", GetName());
}

bool Effect::Apply()
{
	logger::info("Applying effect '{}'", GetName());

	Unload();

	if (!Load()) {
		errors.push_back("Failed to load settings");
		logger::error("Failed to load settings for effect '{}'", GetName());
		return false;
	}

	if (!LoadFXFile()) {
		errors.push_back("Failed to compile FX file");
		logger::error("Failed to compile FX file for effect '{}'", GetName());
		return false;
	}

	logger::info("Successfully applied effect '{}'", GetName());
	return true;
}

void Effect::Unload()
{
	effect.Reset();
	techniques.clear();
	variables.clear();
	customTextureCache.clear();
	uiVariables.clear();
	availableTechniques.clear();
	selectedTechnique.clear();

	errors.clear();

	logger::info("Unloaded effect '{}'", GetName());
}

bool Effect::LoadFXFile()
{
	auto filePath = std::filesystem::path(GetName());

	ComPtr<ID3DBlob> compiledShader;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DX11CompileEffectFromFile(
		filePath.c_str(),
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		NULL,
		NULL,
		globals::d3d::device,
		effect.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr)) {
		std::string errorMsg = "Compilation failed";
		if (errorBlob) {
			errorMsg = std::string(static_cast<const char*>(errorBlob->GetBufferPointer()));
			logger::error("Effect compilation failed: {}", errorMsg);
		}
		errors.push_back(errorMsg);
		return false;
	}

	// Common textures and variables are now managed by EffectManager
	EnumerateAllVariables();

	SetupCustomTextures();
	LoadTechniques();

	// Populate available techniques for UI selection
	availableTechniques = GetBaseTechniqueNames();
	if (!availableTechniques.empty()) {
		selectedTechnique = availableTechniques[0];  // Default to first technique
	}

	LoadUIVariables();

	logger::debug("Successfully loaded FX file: {}", filePath.string());
	return true;
}

void Effect::ExecuteTechniqueSequence(const std::string& baseTechniqueName, Texture& input, Texture& output, Texture& swap)
{
	if (!IsCompiled() || !effect) {
		return;  // Skip execution if not compiled
	}

	auto context = globals::d3d::context;

	// Check if the technique sequence exists
	auto sequenceIt = techniques.find(baseTechniqueName);
	if (sequenceIt == techniques.end()) {
		logger::error("Technique sequence '{}' not found", baseTechniqueName);
		return;
	}

	const auto& sequence = sequenceIt->second;
	logger::debug("Executing technique sequence '{}' with {} techniques", baseTechniqueName, sequence.size());

	// Track which buffer contains the current result
	bool currentIsInOutput = false;

	auto sourceTexture = effect->GetVariableByName(GetSourceTexture())->AsShaderResource();

	for (size_t i = 0; i < sequence.size(); ++i) {
		auto& techniqueInfo = sequence[i];

		// Skip null techniques (gaps in the sequence)
		if (!techniqueInfo.technique) {
			continue;
		}

		logger::debug("Executing technique {} in sequence '{}'", i, baseTechniqueName);

		// Determine input and output for this technique
		ID3D11ShaderResourceView* inputSRV;
		ID3D11RenderTargetView* outputRTV;

		if (i == 0) {
			// First technique: read from input
			inputSRV = input.srv.Get();
			outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, output.rtv.Get());
			currentIsInOutput = true;
		} else {
			// Subsequent techniques: ping-pong between output and swap
			if (currentIsInOutput) {
				inputSRV = output.srv.Get();
				outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, swap.rtv.Get());
				currentIsInOutput = false;
			} else {
				inputSRV = swap.srv.Get();
				outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, output.rtv.Get());
				currentIsInOutput = true;
			}
		}

		sourceTexture->AsShaderResource()->SetResource(inputSRV);
		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			techniqueInfo.technique->GetPassByIndex(p)->Apply(0, context);
			context->Draw(4, 0);
		}
	}

	// Ensure the final result is in the output buffer
	if (!currentIsInOutput) {
		context->CopyResource(output.texture.Get(), swap.texture.Get());
	}
}

void Effect::ExecuteTechnique(const std::string& techniqueName, Texture& input, Texture& output)
{
	if (!IsCompiled() || !effect) {
		return;
	}

	auto context = globals::d3d::context;

	// Find the technique
	auto technique = effect->GetTechniqueByName(techniqueName.c_str());
	if (!technique || !technique->IsValid()) {
		logger::error("Technique '{}' not found or invalid", techniqueName);
		return;
	}

	logger::debug("Executing single technique '{}'", techniqueName);

	// Set input texture
	auto sourceTexture = effect->GetVariableByName(GetSourceTexture())->AsShaderResource();
	if (sourceTexture && sourceTexture->IsValid()) {
		sourceTexture->SetResource(input.srv.Get());
	}

	// Set output render target
	context->OMSetRenderTargets(1, output.rtv.GetAddressOf(), nullptr);

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
		std::string resourceName = GetResourceNameFromVariable(effectVar.Get());

		if (!resourceName.empty()) {
			logger::info("Loading texture for variable '{}': {}", varName, resourceName);

			// Load the texture
			auto srv = LoadTextureFromFile(resourceName);
			if (srv) {
				// Set the texture on the variable
				auto shaderResourceVar = effectVar->AsShaderResource();
				if (shaderResourceVar && shaderResourceVar->IsValid()) {
					shaderResourceVar->SetResource(srv);
					logger::info("Successfully bound texture '{}' to variable '{}'", resourceName, varName);
				}
			} else {
				logger::warn("Failed to load texture '{}' for variable '{}'", resourceName, varName);
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
		return cacheIt->second.Get();
	}

	// Construct full path - check enbseries folder first
	std::filesystem::path filepath = std::filesystem::path{ "enbseries" } / filename;

	ComPtr<ID3D11Resource> texture;
	ComPtr<ID3D11ShaderResourceView> srv;

	HRESULT hr = DirectX::CreateDDSTextureFromFile(device, filepath.c_str(), texture.GetAddressOf(), srv.GetAddressOf());

	if (FAILED(hr)) {
		// Try loading as other format (PNG, BMP, etc.)
		hr = DirectX::CreateWICTextureFromFile(device, filepath.c_str(), texture.GetAddressOf(), srv.GetAddressOf());
	}

	auto fileString = filepath.string();

	if (FAILED(hr)) {
		logger::error("Failed to load texture file: {} (HRESULT: 0x{:08X})", fileString, static_cast<uint32_t>(hr));
		return nullptr;
	}

	// Cache the loaded texture
	customTextureCache[filename] = srv;

	logger::info("Successfully loaded texture: {}", fileString);
	return srv.Get();
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

		// Check if technique name ends with a number
		size_t lastChar = techniqueName.length() - 1;
		if (lastChar > 0 && std::isdigit(techniqueName[lastChar])) {
			// Find where the number starts
			size_t numberStart = lastChar;
			while (numberStart > 0 && std::isdigit(techniqueName[numberStart - 1])) {
				numberStart--;
			}

			// Extract base name and sequence number
			baseName = techniqueName.substr(0, numberStart);
			sequenceNumber = std::stoi(techniqueName.substr(numberStart));
		} else {
			// This is a base technique
			baseName = techniqueName;
			sequenceNumber = 0;
		}

		// Get RenderTarget annotation
		std::string renderTargetName = GetRenderTargetFromTechnique(technique);

		// Ensure the technique sequence vector exists and is large enough
		if (techniques[baseName].size() <= sequenceNumber) {
			techniques[baseName].resize(sequenceNumber + 1);
		}

		// Store the technique info in the correct sequence position
		techniques[baseName][sequenceNumber] = { technique, renderTargetName };

		logger::debug("Loaded technique '{}' as base '{}' sequence {}", techniqueName, baseName, sequenceNumber);
	}

	// Log the technique sequences found
	for (const auto& [baseName, sequence] : techniques) {
		logger::info("Technique sequence '{}' has {} techniques", baseName, sequence.size());
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

ID3D11RenderTargetView* Effect::GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback)
{
	if (renderTargetName.empty()) {
		return fallback;
	}

	// Get render target from EffectManager's common texture cache
	auto& effectManager = EffectManager::GetSingleton();
	auto* texture = effectManager.GetCommonTexture(renderTargetName);
	if (texture && texture->rtv)
		return texture->rtv.Get();

	logger::warn("Render target '{}' not found in cache, using fallback", renderTargetName);
	return fallback;
}

void Effect::LoadUIVariables()
{
	if (!effect) {
		return;
	}

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
		uiVar.effectVariable = variable;

		// Determine variable type
		D3DX11_EFFECT_TYPE_DESC typeDesc;
		auto effectType = variable->GetType();
		if (SUCCEEDED(effectType->GetDesc(&typeDesc))) {
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
				continue;  // Unsupported type
			}
		}

		// Parse UI widget type
		std::string widgetStr = GetUIAnnotation(variable, "UIWidget");
		uiVar.widgetType = ParseWidgetType(widgetStr);

		logger::debug("Variable '{}': UIWidget='{}', parsed as {}",
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
				logger::debug("Variable '{}': UIList='{}'", uiVar.name, listStr);
				if (!listStr.empty()) {
					uiVar.dropdownItems = ParseDropdownList(listStr);
					logger::debug("Parsed {} dropdown items", uiVar.dropdownItems.size());
				}
			}
		}

		// Load current value
		LoadUIVariableValue(uiVar);

		uiVariables.push_back(uiVar);

		// Debug logging
		if (uiVar.widgetType == UIWidgetType::Dropdown) {
			logger::debug("Loaded UI variable '{}' with display name '{}', dropdown items: {}",
				uiVar.name, uiVar.displayName, uiVar.dropdownItems.size());
			for (const auto& item : uiVar.dropdownItems) {
				logger::debug("  - {}", item);
			}
		} else {
			logger::debug("Loaded UI variable '{}' with display name '{}'", uiVar.name, uiVar.displayName);
		}
	}

	logger::info("Loaded {} UI variables", uiVariables.size());
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
		}
	}
}

void Effect::RenderImGui()
{
	if (ImGui::TreeNodeEx("enbeffect.fx", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool valuesChanged = false;

		// Technique selection dropdown
		if (!availableTechniques.empty()) {
			if (ImGui::BeginCombo("Technique", selectedTechnique.c_str())) {
				for (const auto& technique : availableTechniques) {
					const bool isSelected = (selectedTechnique == technique);
					if (ImGui::Selectable(technique.c_str(), isSelected)) {
						selectedTechnique = technique;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();
		}

		const float labelWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWWWWWWW").x;  // 25 chars
		const float inputWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWW").x;       // 20 chars

		for (auto& uiVar : uiVariables) {
			// Skip spacers
			if (uiVar.displayName.empty() || std::all_of(uiVar.displayName.begin(), uiVar.displayName.end(), [](char c) { return std::isspace(c); })) {
				ImGui::Spacing();
				continue;
			}

			// Clamp label to fixed width
			std::string label = uiVar.displayName;
			while (ImGui::CalcTextSize(label.c_str()).x > labelWidth && !label.empty()) {
				label.pop_back();
			}

			// Draw label and position input
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s", label.c_str());
			ImGui::SameLine(labelWidth + ImGui::GetStyle().ItemSpacing.x);

			// Skip inputs for labels (min == max == 0)
			bool isLabelOnly = (uiVar.type == UIVariableType::Float && uiVar.floatMin == 0 && uiVar.floatMax == 0) ||
			                   (uiVar.type == UIVariableType::Int && uiVar.intMin == 0 && uiVar.intMax == 0);

			if (!isLabelOnly) {
				if (ImGui::BeginChild(("##input_" + uiVar.name).c_str(), ImVec2(inputWidth, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar)) {
					ImGui::SetNextItemWidth(-1);

					if (uiVar.type == UIVariableType::Float) {
						if (ImGui::InputFloat(("##" + uiVar.name).c_str(), &uiVar.floatValue, 0.0f, 0.0f, "%.3f")) {
							valuesChanged = true;
						}
					} else if (uiVar.type == UIVariableType::Int) {
						if (uiVar.widgetType == UIWidgetType::Dropdown && !uiVar.dropdownItems.empty()) {
							const char* currentItem = uiVar.dropdownItems[uiVar.intValue].c_str();
							if (ImGui::BeginCombo(("##" + uiVar.name).c_str(), currentItem)) {
								for (int i = 0; i < uiVar.dropdownItems.size(); ++i) {
									if (ImGui::Selectable(uiVar.dropdownItems[i].c_str(), uiVar.intValue == i)) {
										uiVar.intValue = i;
										valuesChanged = true;
									}
								}
								ImGui::EndCombo();
							}
						} else {
							if (ImGui::InputInt(("##" + uiVar.name).c_str(), &uiVar.intValue)) {
								valuesChanged = true;
							}
						}
					} else if (uiVar.type == UIVariableType::Bool) {
						if (ImGui::Checkbox(("##" + uiVar.name).c_str(), &uiVar.boolValue)) {
							valuesChanged = true;
						}
					}
				}
				ImGui::EndChild();
			}
		}

		// Update shader variables if any values changed
		if (valuesChanged) {
			UpdateUIVariables();
		}

		ImGui::TreePop();
	}
}

void Effect::EnumerateAllVariables()
{
	if (!effect) {
		return;
	}

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
		variables[varName] = variable;

		logger::debug("Enumerated variable: {}", varName);
	}

	logger::info("Enumerated {} effect variables", variables.size());
}
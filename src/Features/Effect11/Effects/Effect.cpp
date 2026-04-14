#include "Effect.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>

#include "../ENBExtender.h"
#include "../TextureManager.h"
#include "State.h"

/**
 * Load effect settings from enbseries/<EffectName>.ini and apply them to UI variables and the selected technique.
 *
 * If the ini file does not exist this function leaves current values unchanged (defaults) and returns.
 * If the file's last write time matches the cached value, the reload is skipped.
 * Per-UI-variable values are read from the ini section named after the effect (uppercased) using each variable's display name;
 * when a value is present it is parsed and applied via LoadVariableFromString.
 * When UI techniques are defined, the function reads the 1-indexed `TECHNIQUE` entry, converts it to 0-indexed,
 * clamps it to the valid range, and updates selectedTechniqueIndex.
 *
 * @return `true` if settings were loaded or defaults were used.
 */
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

/**
 * @brief Persist UI-controlled effect settings to the effect's INI file.
 *
 * Writes the current UI variable values and the selected technique into enbseries/<EffectName>.ini
 * under a section named by the effect name converted to uppercase. Scalar values are written as
 * decimal strings, booleans as "true"/"false", and color values as comma-separated float components.
 * After writing, the Windows INI cache is flushed so subsequent reads pick up the changes.
 *
 * @note The TECHNIQUE entry is stored as a 1-based index (selectedTechniqueIndex + 1).
 */
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

/**
 * @brief Initializes and activates the effect by compiling its FX file, loading saved settings, and creating effect textures.
 *
 * Resets any previous effect state before attempting to apply. On failure, a descriptive message is appended to the effect's internal error list and the effect remains unloaded.
 *
 * @return true if initialization succeeded and the effect is ready, false otherwise.
 */
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

/**
 * @brief Releases the effect and resets all runtime state and caches.
 *
 * Clears compiled effect state, technique and variable registries, custom texture and effect texture caches,
 * UI variable/technique lists, and recorded errors. Also resets selection index and the cached INI write time
 * so subsequent loads will re-read settings from disk.
 */
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

/**
 * @brief Compile and initialize the effect from its .fx source in enbseries/<EffectName>.
 *
 * Reads the main effect file, preprocesses it for ENB Extender, compiles the shader with a custom
 * include handler, and creates the D3D11 effect from the compiled bytecode. On success this
 * populates the effect instance: enumerates global variables, binds any custom annotated textures,
 * loads technique sequences and UI-annotated techniques, populates available technique names,
 * sets the default selected UI technique when present, and loads current UI variable values from
 * the effect.
 *
 * On failure the function records a descriptive message in the `errors` vector and logs details.
 *
 * @return `true` if the effect was compiled and initialized successfully, `false` otherwise.
 */
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

/**
 * @brief Executes a named technique sequence, rendering each technique pass with ping-ponging support.
 *
 * Executes the sequence identified by a_baseTechniqueName using a_input as the initial shader resource view,
 * writing to a_output and using a_temp for intermediate ping-pong render targets. The method updates shader
 * size variables and viewports as needed and applies each technique's passes in order. If a technique specifies
 * a custom render target annotation, that override will be used for that pass.
 *
 * @param a_baseTechniqueName Base name of the technique sequence to execute.
 * @param a_input Shader resource view used as the initial input texture.
 * @param a_output Render target texture used as the primary output.
 * @param a_temp Temporary render target texture used for ping-pong intermediate passes.
 * @return true if the final pass wrote into a_output, false if it wrote into the temporary target or execution was skipped
 *         (for example when the effect is not compiled or the named sequence is missing/empty).
 */
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

/**
 * @brief Renders the specified effect technique into the provided output texture.
 *
 * Sets the output render target and viewport, applies every pass of the named technique,
 * and issues full-screen draw calls so the technique's shader renders into the given output.
 *
 * @param techniqueName Name of the technique to execute.
 * @param output       Render target texture into which the technique will draw.
 */
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

/**
 * @brief Loads textures specified by the "ResourceName" annotation and binds them to effect variables.
 *
 * Iterates effect global variables, and for each variable that provides a non-empty `ResourceName`
 * annotation, loads the named texture and sets it as the variable's shader-resource view. Logs a
 * warning when a texture cannot be loaded and logs debug information on successful bindings.
 */
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

/**
 * @brief Loads a texture file from the enbseries folder and returns a shader-resource view, caching results.
 *
 * Attempts to load the texture named by `filename` from the enbseries/ directory. Tries DDS loader first and falls back to WIC-based formats (PNG, BMP, etc.). Caches successful loads in the effect's custom texture cache so subsequent calls for the same `filename` return the cached SRV.
 *
 * @param filename Filename or relative path under enbseries (e.g., "textures/mytex.dds").
 * @return ID3D11ShaderResourceView* Pointer to the created shader-resource view on success, or `nullptr` on failure.
 */
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

/**
 * @brief Retrieves the `ResourceName` annotation value from an effect variable.
 *
 * @param variable Pointer to the effect variable to inspect.
 * @return std::string The annotation value for `ResourceName`, or an empty string if not present.
 */
std::string Effect::GetResourceNameFromVariable(ID3DX11EffectVariable* variable)
{
	return GetUIAnnotation(variable, "ResourceName");
}

template <typename Callback>
/**
 * @brief Iterates all effect techniques and invokes a callback for each, grouping numbered sequences by common base name.
 *
 * Techniques are grouped when a technique name equals the previous base name followed by an increasing decimal index
 * (e.g., "Bloom", "Bloom1", "Bloom2" → base name "Bloom" with sequence numbers 0,1,2). For the first technique in a
 * group the sequence number is 0; subsequent numbered entries increment the sequence number.
 *
 * @param effect Pointer to the D3D11 effect whose techniques will be enumerated. If `GetDesc` fails, the function returns immediately.
 * @param callback Callable invoked for each valid technique with signature compatible with:
 *                 `(ID3DX11EffectTechnique* technique, const std::string& baseName, const std::string& techniqueName, int sequenceNumber)`.
 *                 Techniques that are invalid or whose `GetDesc` fails are skipped.
 */
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

/**
 * @brief Populate the effect's technique sequences from the compiled effect.
 *
 * @details Enumerates all techniques in the effect, groups them into numeric sequences by base name,
 * and stores each technique's handle and its optional render-target override into the `techniques` map.
 * Each sequence vector is resized to accommodate the technique's sequence index. Logs each loaded
 * technique and a summary count per base sequence.
 */
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

/**
 * @brief Enumerates base technique names whose first sequence element contains a valid technique.
 *
 * @return std::vector<std::string> A list of base technique names where the sequence is non-empty and its first element has a valid technique pointer.
 */
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

/**
 * @brief Populate the list of UI-exposed techniques from the compiled effect.
 *
 * Clears the current UI technique list and selected index, then scans all technique sequences
 * for a non-empty `UIName` annotation. For each unique technique base name that provides a
 * `UIName`, adds a UITechnique entry with the base name as `techniqueName` and the annotation
 * value as `displayName`.
 */
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

/**
 * @brief Retrieve a string annotation value from a technique by name.
 *
 * Searches the technique's annotations for one whose name equals @p annotationName
 * and returns its string value.
 *
 * @param technique The technique to inspect; may be nullptr.
 * @param annotationName The annotation name to look up.
 * @return std::string The annotation's string value if found and valid, otherwise an empty string.
 */
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

/**
 * @brief Retrieves the "RenderTarget" annotation value from a technique.
 *
 * @param technique Technique to read the annotation from.
 * @return std::string The annotation value specifying a render target name, or an empty string if the annotation is missing or cannot be read.
 */
std::string Effect::GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique)
{
	return GetTechniqueAnnotation(technique, "RenderTarget");
}

/**
 * @brief Retrieves the UI display name annotation for a technique.
 *
 * @param technique Effect technique to query.
 * @return std::string The value of the "UIName" annotation for the technique, or an empty string if the annotation is missing or the technique is invalid.
 */
std::string Effect::GetUINameFromTechnique(ID3DX11EffectTechnique* technique)
{
	return GetTechniqueAnnotation(technique, "UIName");
}

/**
 * @brief Retrieve a cached effect texture by name.
 *
 * @param name The texture identifier stored in the effect's texture cache.
 * @return TextureManager::Texture* Pointer to the cached texture if found, `nullptr` otherwise.
 */
TextureManager::Texture* Effect::GetEffectTexture(const std::string& name)
{
	auto it = effectTextureCache.find(name);
	if (it != effectTextureCache.end()) {
		return &it->second;
	}
	return nullptr;
}

/**
 * @brief Resolve a render-target view by name from the effect's caches, using a provided fallback when unavailable.
 *
 * Looks up a texture by `renderTargetName` first in the effect-managed texture cache, then in the shared/common texture cache.
 *
 * @param renderTargetName Name of the render target texture to resolve; an empty string causes the function to return `fallback`.
 * @param fallback Render target view to return if no matching cached render target is found.
 * @return ID3D11RenderTargetView* The resolved render-target view from the caches, or `fallback` if none was found.
 */
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

/**
 * @brief Populates the effect's UI variable list from annotated global effect variables.
 *
 * Scans the compiled effect for global variables annotated with `UIName`, constructs
 * UIVariable entries (name, display name, type, widget metadata, min/max/step or dropdown
 * items as applicable), loads each variable's current value into the UIVariable, and
 * appends them to the `uiVariables` vector.
 *
 * If the effect is not available or its descriptor cannot be retrieved, the function
 * returns without modifying `uiVariables`. Parsing errors for individual annotations
 * are caught and logged; a failed parse does not abort the scan of other variables.
 */
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

/**
 * @brief Retrieves a named annotation value from an effect variable.
 *
 * Looks up an annotation whose name equals `annotationName` on `variable` and returns its value as a string.
 * If the annotation is a string the string value is returned. If the annotation is a scalar the integer
 * value is returned, or the floating-point value if integer extraction fails. Returns an empty string
 * when `variable` is null, the annotation is not found, or its value cannot be extracted.
 *
 * @param variable Effect variable to query annotations from.
 * @param annotationName Name of the annotation to retrieve.
 * @return std::string The annotation value as a string, or an empty string if missing or unreadable.
 */
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

/**
 * @brief Parses a widget type name and maps it to a UIWidgetType enum.
 *
 * Performs a case-insensitive match of common widget names.
 *
 * @param widget Widget type name (case-insensitive).
 * @return Effect::UIWidgetType `Spinner` if `widget` equals "spinner", `Dropdown` if `widget` equals "dropdown", `Default` otherwise.
 */
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

/**
 * @brief Parses a comma-separated list into individual, trimmed items.
 *
 * Splits the input string on commas and removes leading and trailing spaces and tabs from each entry.
 *
 * @param list Comma-separated items.
 * @return std::vector<std::string> Vector of trimmed items in the same order as they appear in the input.
 */
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

/**
 * @brief Reads the current value from the underlying effect variable and stores it into the given UI variable container.
 *
 * The function queries the effect variable according to uiVar.type and updates the matching field on uiVar:
 * - Float -> uiVar.floatValue
 * - Int -> uiVar.intValue
 * - Bool -> uiVar.boolValue
 * - Color3/Color4 -> uiVar.colorValue (array of floats)
 *
 * @param uiVar UI variable container whose value will be populated from its associated effect variable.
 */
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

/**
 * @brief Parses a string and applies its value to a UI variable and the underlying shader variable.
 *
 * Parses `value` according to `uiVar.type`, updates the corresponding field on `uiVar`
 * (floatValue / intValue / boolValue / colorValue) and writes the value into the associated
 * effect variable (scalar or vector) so the shader sees the change.
 *
 * Supported formats:
 * - Float: parsed with `std::stof`.
 * - Int: parsed with `std::stoi`.
 * - Bool: case-insensitive `true`, `1`, `yes`, `on` → true; `false`, `0`, `no`, `off` → false;
 *   otherwise falls back to `std::stoi(value) != 0`.
 * - Color3/Color4: comma-separated floats (3 or 4 components respectively).
 *
 * If parsing fails or an exception is thrown, a warning is logged and the function leaves the
 * variable unchanged.
 *
 * @param uiVar Reference to the UI variable metadata and storage to update.
 * @param value String representation of the value to parse and apply.
 */
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

/**
 * @brief Pushes all stored UI values into their corresponding shader variables.
 *
 * Iterates the effect's UI variable list and updates each underlying effect variable
 * with the value currently held in the UI representation.
 *
 * - Float -> SetFloat
 * - Int -> SetInt
 * - Bool -> SetBool
 * - Color3/Color4 -> SetFloatVector
 */
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

/**
 * @brief Render the effect's ImGui user interface, allowing technique selection and parameter editing.
 *
 * Renders a collapsible UI block named after the effect that displays a technique selector (when available)
 * and a two-column table of UI-exposed parameters (float, int, bool, color3, color4). When the user modifies
 * any control, the function pushes the new values back into the effect by calling UpdateUIVariables().
 *
 * Also displays any stored compilation or load errors as wrapped text below the controls.
 */
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

/**
 * @brief Enumerates the effect's global variables and caches them by name.
 *
 * Clears the internal `variables` map, inspects the effect's global variable list,
 * and stores a copy of each valid global variable under its declared name.
 * Logs each enumerated variable and a final count.
 */
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

/**
 * @brief Retrieve an effect variable by name, using an internal cache to avoid repeated lookups.
 *
 * @param name The effect variable name to look up.
 * @return ID3DX11EffectVariable* Pointer to the cached or newly retrieved effect variable; `nullptr` if the effect is not initialized.
 */
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

/**
 * @brief Retrieve a cached pointer to a common texture by name.
 *
 * If the texture is not already cached, queries TextureManager::GetSingleton().GetCommonTexture(name)
 * and caches the result for subsequent calls.
 *
 * @param name Name of the common texture to look up.
 * @return TextureManager::Texture* Pointer to the texture, or `nullptr` if the texture was not found.
 */
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

/**
 * @brief Clears cached effect variables and cached common texture pointers.
 *
 * Removes all entries from the internal variable lookup cache and the cache
 * that stores pointers to commonly used textures.
 */
void Effect::ClearVariableCache()
{
	variableCache.clear();
	commonTexturePointerCache.clear();
}

/**
 * @brief Binds a shader resource view to an effect variable by name.
 *
 * Looks up the effect variable cached under `variableName`, casts it to a
 * shader-resource variable and sets its resource to `resource` when valid.
 *
 * @param variableName Name of the effect variable to set.
 * @param resource Shader resource view to bind to the variable (may be nullptr to clear).
 * @return true if the variable was found, is a valid shader-resource variable, and the resource was set; `false` otherwise.
 */
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

/**
 * @brief Binds a shader-resource view to a named shader resource variable in the given effect.
 *
 * @param effect Pointer to the effect containing the variable; if null the function returns false.
 * @param variableName Name of the shader variable to set (as declared in the effect).
 * @param resource Shader-resource view to bind to the variable (may be nullptr to unbind).
 * @return true if the variable was found and the resource was bound; false otherwise.
 */
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

/**
 * @brief Sets a raw vector/array value for a named effect variable in the given effect.
 *
 * Attempts to find the variable named `variableName` on `effect` and writes `size` bytes
 * from `data` into the variable using a raw value write.
 *
 * @param effect Pointer to the effect containing the variable.
 * @param variableName Name of the effect variable to set.
 * @param data Pointer to the source data to copy into the variable (interpreted as raw bytes).
 * @param size Number of bytes to copy from `data`.
 * @return true if the variable was found and updated, false otherwise.
 */
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

/**
 * @brief Sets a shader vector/raw value for a cached effect variable by name.
 *
 * Copies `size` bytes from `data` into the effect variable identified by `variableName`.
 *
 * @param variableName Name of the effect variable to update.
 * @param data Pointer to the raw data to write into the variable.
 * @param size Number of bytes to copy from `data` into the variable.
 * @return true if the variable was found, valid, and updated; false otherwise.
 */
bool Effect::SetVectorVariable(const std::string& variableName, const void* data, uint32_t size)
{
	auto variable = GetCachedVariable(variableName);
	if (variable && variable->IsValid()) {
		variable->SetRawValue(data, 0, size);
		return true;
	}
	return false;
}

/**
 * @brief Create a 2D texture configured as both a render target and shader resource.
 *
 * Allocates a single-mip, single-array-slice D3D11 texture with the requested width, height,
 * and DXGI format, and creates an associated render target view (RTV) and shader resource view (SRV).
 *
 * @param width Width of the texture in pixels.
 * @param height Height of the texture in pixels.
 * @param format DXGI format to use for the texture.
 * @param debugName Optional base name assigned to the created resources for debugging.
 * @return TextureManager::Texture Struct containing the created texture, RTV, and SRV.
 */
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

/**
 * @brief Get the name of the currently selected technique.
 *
 * Returns the UI-annotated technique name when `selectedTechniqueIndex` addresses `uiTechniques`;
 * otherwise returns the base technique name from `availableTechniques` when the index is valid;
 * if the index is out of range for both lists, returns an empty string.
 *
 * @return std::string The selected technique name, or an empty string if no valid selection exists.
 */
std::string Effect::GetSelectedTechnique() const
{
	if (selectedTechniqueIndex < uiTechniques.size()) {
		return uiTechniques[selectedTechniqueIndex].techniqueName;
	} else if (selectedTechniqueIndex < availableTechniques.size()) {
		return availableTechniques[selectedTechniqueIndex];
	}
	return "";
}

/**
 * @brief Updates the shader "ScreenSize" vector using the provided output dimensions.
 *
 * When both outputWidth and outputHeight are greater than zero, sets the effect vector
 * "ScreenSize" to an array where:
 * - [0] = output width
 * - [1] = 1 / output width
 * - [2] = aspect ratio (width / height)
 * - [3] = 1 / aspect ratio
 *
 * If `effect` is null or either dimension is zero, no change is made.
 *
 * @param effect Pointer to the D3D11 effect to update.
 * @param outputWidth Output render target width in pixels.
 * @param outputHeight Output render target height in pixels.
 */
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
#include "PCH.h"
#include "Effect11.h"
#include "Globals.h"
#include "State.h"

#include <fstream>
#include <d3dcompiler.h>
#include <chrono>
#include <filesystem>
#include <sstream>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>


void Effect11::Initialize()
{
	CreateRenderStates();
    CreateQuadGeometry();
}

bool Effect11::LoadFXFile(std::filesystem::path a_filePath)
 {
    ComPtr<ID3DBlob> compiledShader;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DX11CompileEffectFromFile(
		a_filePath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        NULL,
		NULL,
		globals::d3d::device,
        effect.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            logger::error("Effect compilation failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

	SetupCommonTextures();

    SetupCommonVariables();
	SetupEffectVariables();

    LoadResourceNameTextures();
    LoadTechniques();

    // Populate available techniques for UI selection
    availableTechniques = GetBaseTechniqueNames();
    if (!availableTechniques.empty()) {
        selectedTechnique = availableTechniques[0]; // Default to first technique
    }

    LoadUIVariables();

	logger::debug("Successfully loaded FX file: {}", a_filePath.string());
    return true;
}

void Effect11::ExecuteTechniqueSequence(const std::string& baseTechniqueName, ID3D11RenderTargetView* renderTarget)
{
	auto context = globals::d3d::context;

    UpdateCommonVariables();
	UpdateEffectVariables();

    // Check if the technique sequence exists
    auto sequenceIt = techniques.find(baseTechniqueName);
    if (sequenceIt == techniques.end()) {
        logger::error("Technique sequence '{}' not found", baseTechniqueName);
        return;
    }

    const auto& sequence = sequenceIt->second;
    logger::debug("Executing technique sequence '{}' with {} techniques", baseTechniqueName, sequence.size());

    for (size_t i = 0; i < sequence.size(); ++i) {
        auto& techniqueInfo = sequence[i];
        
        // Skip null techniques (gaps in the sequence)
        if (!techniqueInfo.technique) {
            continue;
        }

        logger::debug("Executing technique {} in sequence '{}'", i, baseTechniqueName);

		context->RSSetState(rasterizerState.Get());
		context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
		context->OMSetDepthStencilState(nullptr, 0);

        // Use technique-specific render target if specified, otherwise use fallback
        auto techniqueRenderTarget = GetRenderTargetView(techniqueInfo.renderTargetName, renderTarget);
		context->OMSetRenderTargets(1, &techniqueRenderTarget, nullptr);

        D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			techniqueInfo.technique->GetPassByIndex(p)->Apply(0, context);
			
            UINT stride = sizeof(float) * 5;
			UINT offset = 0;
			ID3D11Buffer* vertexBuffers[] = { quadVertexBuffer.Get() };
			context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			
            context->Draw(4, 0);
        }
	}
}

void Effect11::CreateQuadGeometry()
{
    struct Vertex
    {
        float position[3];
        float texCoord[2];
    };

    const Vertex vertices[] = {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } }
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, quadVertexBuffer.GetAddressOf()));
}

void Effect11::CreateRenderStates()
{
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthBias = 0;
    rastDesc.DepthBiasClamp = 0.0f;
    rastDesc.SlopeScaledDepthBias = 0.0f;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.ScissorEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;

    DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rastDesc, rasterizerState.GetAddressOf()));

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, blendState.GetAddressOf()));
}

void Effect11::SetupCommonTextures()
{
    auto device = globals::d3d::device;
    auto state = globals::state;
    
    UINT screenWidth = static_cast<UINT>(state->screenSize.x);
    UINT screenHeight = static_cast<UINT>(state->screenSize.y);
    
    D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = screenWidth;
	texDesc.Height = screenHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

    {
        Texture bloomTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, bloomTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(bloomTexture.texture.Get(), nullptr, bloomTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(bloomTexture.texture.Get(), nullptr, bloomTexture.srv.GetAddressOf()));     
        commonTextureCache.insert({ "TextureBloom", bloomTexture });
    }
    
    {
        Texture lensTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, lensTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(lensTexture.texture.Get(), nullptr, lensTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(lensTexture.texture.Get(), nullptr, lensTexture.srv.GetAddressOf()));    
        commonTextureCache.insert({ "TextureLens", lensTexture });
    }
    
    {
        Texture adaptationTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, adaptationTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.srv.GetAddressOf()));    
        commonTextureCache.insert({ "TextureAdaptation", adaptationTexture });
    }
    
    {
        Texture apertureTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, apertureTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(apertureTexture.texture.Get(), nullptr, apertureTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(apertureTexture.texture.Get(), nullptr, apertureTexture.srv.GetAddressOf()));	
        commonTextureCache.insert({ "TextureAperture", apertureTexture });
    }
    
    logger::info("Created standard textures: TextureBloom, TextureLens, TextureAdaptation, TextureAperture");
}

std::vector<uint8_t> Effect11::LoadFileToMemory(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    auto size = file.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

void Effect11::SetupCommonVariables()
{
	Timer = effect->GetVariableByName("Timer")->AsVector();
	ScreenSize = effect->GetVariableByName("ScreenSize")->AsVector();
	AdaptiveQuality = effect->GetVariableByName("AdaptiveQuality")->AsVector();
	Weather = effect->GetVariableByName("Weather")->AsVector();
	TimeOfDay1 = effect->GetVariableByName("TimeOfDay1")->AsVector();
	TimeOfDay2 = effect->GetVariableByName("TimeOfDay2")->AsVector();
}

void Effect11::UpdateCommonVariables()
{
	auto state = globals::state;

    {
		auto modifiedTimer = (1000.0f * state->timer);
		modifiedTimer = std::fmodf(modifiedTimer, 16777216);
		modifiedTimer /= 16777216.0f;

		float4 timer = { modifiedTimer, 60.0f, 0.0f, *globals::game::deltaTime };

		Timer->SetRawValue(&timer, 0, sizeof(timer));
	}
	
    {
		float aspect = state->screenSize.x / state->screenSize.y;

		float4 screenSize = { state->screenSize.x, state->screenSize.y, aspect, 1.0f / aspect };
		ScreenSize->SetRawValue(&screenSize, 0, sizeof(screenSize));
	}

	auto sky = globals::game::sky;

    {
		float4 weather = { static_cast<float>(sky->currentWeather->formID), static_cast<float>(sky->lastWeather->formID), sky->currentWeatherPct, sky->currentGameHour };

		Weather->SetRawValue(&weather, 0, sizeof(weather));
	}

	{
		float currentTime = sky->currentGameHour;

		float sunriseBegin = sky->GetSunriseBegin();
		float sunriseEnd = sky->GetSunriseEnd();
		float sunsetBegin = sky->GetSunsetBegin();
		float sunsetEnd = sky->GetSunsetEnd();

		float dawnMid = sunriseBegin + (sunriseEnd - sunriseBegin) * 0.5f;
		float duskMid = sunsetBegin + (sunsetEnd - sunsetBegin) * 0.5f;

		auto range01 = [](float t, float a, float b) {
			// Handles wrap-around if b < a
			float range = b - a;
			if (range < 0.0f)
				range += 24.0f;
			float value = t - a;
			if (value < 0.0f)
				value += 24.0f;
			return std::clamp(value / range, 0.0f, 1.0f);
		};

		float4 timeOfDay1 = { 0, 0, 0, 0 };  // dawn, sunrise, day, sunset
		float4 timeOfDay2 = { 0, 0, 0, 0 };  // dusk, night

		// Dawn → Sunrise
		if (currentTime >= sunriseBegin && currentTime < dawnMid) {
			float f = range01(currentTime, sunriseBegin, dawnMid);
			timeOfDay1.x = 1.0f - f;  // dawn
			timeOfDay1.y = f;         // sunrise
		} else if (currentTime >= dawnMid && currentTime < sunriseEnd) {
			float f = range01(currentTime, dawnMid, sunriseEnd);
			timeOfDay1.y = 1.0f - f;  // sunrise
			timeOfDay1.z = f;         // day
		}
		// Day → Sunset
		else if (currentTime >= sunriseEnd && currentTime < sunsetBegin) {
			float f = range01(currentTime, sunriseEnd, sunsetBegin);
			timeOfDay1.z = 1.0f - f;  // day
			timeOfDay1.w = f;         // sunset
		}
		// Sunset → Dusk
		else if (currentTime >= sunsetBegin && currentTime < duskMid) {
			float f = range01(currentTime, sunsetBegin, duskMid);
			timeOfDay1.w = 1.0f - f;  // sunset
			timeOfDay2.x = f;         // dusk
		} else if (currentTime >= duskMid && currentTime < sunsetEnd) {
			float f = range01(currentTime, duskMid, sunsetEnd);
			timeOfDay2.x = 1.0f - f;  // dusk
			timeOfDay2.y = f;         // night
		}
		// Night → Dawn (wrap)
		else {
			float f = range01(currentTime, sunsetEnd, sunriseBegin);
			timeOfDay2.y = 1.0f - f;  // night
			timeOfDay1.x = f;         // dawn
		}

		// Send to shaders
		TimeOfDay1->SetRawValue(&timeOfDay1, 0, sizeof(timeOfDay1));
		TimeOfDay2->SetRawValue(&timeOfDay2, 0, sizeof(timeOfDay2));
	}

    {
		float eNightDayFactor = std::fabs(sky->currentGameHour - 12.0f);
		if (eNightDayFactor > 12.0f)
			eNightDayFactor = 24.0f - eNightDayFactor;
		eNightDayFactor = 1.0f - eNightDayFactor / 12.0f;

		ENightDayFactor->SetRawValue(&eNightDayFactor, 0, sizeof(eNightDayFactor));
    }

	{
		float eInteriorFactor = sky->mode.any(RE::Sky::Mode::kInterior);

		EInteriorFactor->SetRawValue(&eInteriorFactor, 0, sizeof(eInteriorFactor));
	}
}

void Effect11::SetupEffectVariables()
{
	Params01 = effect->GetVariableByName("Params01")->AsVector();
	ENBParams01 = effect->GetVariableByName("ENBParams01")->AsVector();
}

void Effect11::UpdateEffectVariables()
{
	float4 params01[7]{};
	Params01->SetRawValue(&params01, 0, sizeof(params01));

	float4 enbParams01{};
	ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));
}

void Effect11::LoadResourceNameTextures()
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

ID3D11ShaderResourceView* Effect11::LoadTextureFromFile(const std::string& filename)
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

std::string Effect11::GetResourceNameFromVariable(ID3DX11EffectVariable* variable)
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

void Effect11::LoadTechniques()
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

std::vector<std::string> Effect11::GetBaseTechniqueNames()
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

std::string Effect11::GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique)
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

ID3D11RenderTargetView* Effect11::GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback)
{
    if (renderTargetName.empty()) {
        return fallback;
    }

    auto cacheIt = commonTextureCache.find(renderTargetName);
	if (cacheIt != commonTextureCache.end()) {
		return cacheIt->second.rtv.Get();
    }

    logger::critical("Render target '{}' not found in cache", renderTargetName);
    
    return nullptr;
}

void Effect11::LoadUIVariables()
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
            continue; // No UI annotation, skip
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
                    continue; // Unsupported type
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
            
            if (!minStr.empty()) uiVar.floatMin = std::stof(minStr);
            if (!maxStr.empty()) uiVar.floatMax = std::stof(maxStr);
            if (!stepStr.empty()) uiVar.floatStep = std::stof(stepStr);
        }
        else if (uiVar.type == UIVariableType::Int) {
            std::string minStr = GetUIAnnotation(variable, "UIMin");
            std::string maxStr = GetUIAnnotation(variable, "UIMax");
            
            if (!minStr.empty()) uiVar.intMin = std::stoi(minStr);
            if (!maxStr.empty()) uiVar.intMax = std::stoi(maxStr);
            
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

std::string Effect11::GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName)
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

Effect11::UIWidgetType Effect11::ParseWidgetType(const std::string& widget)
{
    std::string lowerWidget = widget;
    std::transform(lowerWidget.begin(), lowerWidget.end(), lowerWidget.begin(), ::tolower);
    
    if (lowerWidget == "spinner") return UIWidgetType::Spinner;
    if (lowerWidget == "dropdown") return UIWidgetType::Dropdown;
    return UIWidgetType::Default;
}

std::vector<std::string> Effect11::ParseDropdownList(const std::string& list)
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

void Effect11::LoadUIVariableValue(UIVariable& uiVar)
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

void Effect11::UpdateUIVariables()
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

void Effect11::RenderImGui()
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

        // Define fixed widths
        const float labelWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWWWWWWW").x; // 25 characters for labels
        const float inputAreaWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWW").x; // 20 characters for inputs

        for (auto& uiVar : uiVariables) {
            // Skip empty UI names (spacers) - detect any string with only whitespace
            if (uiVar.displayName.empty() || std::all_of(uiVar.displayName.begin(), uiVar.displayName.end(), [](char c) { return std::isspace(c); })) {
                ImGui::Spacing();
                continue;
            }

            // Clamp label text to fixed width
            std::string clampedLabel = uiVar.displayName;
            while (ImGui::CalcTextSize(clampedLabel.c_str()).x > labelWidth && !clampedLabel.empty()) {
                clampedLabel.pop_back();
            }

            // Draw label with fixed width
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", clampedLabel.c_str());
            ImGui::SameLine(labelWidth + ImGui::GetStyle().ItemSpacing.x);
            
            // Create child window with fixed width for inputs
            if (ImGui::BeginChild(("input_" + uiVar.name).c_str(), ImVec2(inputAreaWidth, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar)) {
                switch (uiVar.type) {
                    case UIVariableType::Float:
                    {
                        // Detect labels - skip input for label-only items
                        if (uiVar.floatMin == 0 && uiVar.floatMax == 0) {
                            // This is a label, already drawn above
                        }
                        else {
                            ImGui::SetNextItemWidth(-1); // Fill available width
                            if (ImGui::InputFloat(("##" + uiVar.name).c_str(), &uiVar.floatValue, 0.0f, 0.0f, "%.3f")) {
                                // Clamp to min/max if specified
                                if (uiVar.floatMin != uiVar.floatMax) {
                                    uiVar.floatValue = std::clamp(uiVar.floatValue, uiVar.floatMin, uiVar.floatMax);
                                }
                                valuesChanged = true;
                            }
                        }
                        break;
                    }
                    case UIVariableType::Int:
                    {
                        if (uiVar.widgetType == UIWidgetType::Dropdown && !uiVar.dropdownItems.empty()) {
                            ImGui::SetNextItemWidth(-1); // Fill available width
                            const char* currentItem = uiVar.dropdownItems[uiVar.intValue].c_str();
                            if (ImGui::BeginCombo(("##" + uiVar.name).c_str(), currentItem)) {
                                for (int i = 0; i < uiVar.dropdownItems.size(); ++i) {
                                    const bool isSelected = (uiVar.intValue == i);
                                    if (ImGui::Selectable(uiVar.dropdownItems[i].c_str(), isSelected)) {
                                        uiVar.intValue = i;
                                        valuesChanged = true;
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                        // Detect labels - skip input for label-only items
                        else if (uiVar.intMin == 0 && uiVar.intMax == 0) {
                            // This is a label, already drawn above
                        }
                        else {
                            ImGui::SetNextItemWidth(-1); // Fill available width
                            if (ImGui::InputInt(("##" + uiVar.name).c_str(), &uiVar.intValue)) {
                                // Clamp to min/max if specified
                                if (uiVar.intMin != uiVar.intMax) {
                                    uiVar.intValue = std::clamp(uiVar.intValue, uiVar.intMin, uiVar.intMax);
                                }
                                valuesChanged = true;
                            }
                        }
                        break;
                    }
                    case UIVariableType::Bool:
                    {
                        if (ImGui::Checkbox(("##" + uiVar.name).c_str(), &uiVar.boolValue)) {
                            valuesChanged = true;
                        }
                        break;
                    }
                }
            }
            ImGui::EndChild();
        }

        // Update shader variables if any values changed
        if (valuesChanged) {
            UpdateUIVariables();
        }

		ImGui::TreePop();
    }
}
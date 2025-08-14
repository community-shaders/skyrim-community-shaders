#include "PCH.h"
#include "Effect11.h"
#include "Globals.h"
#include "State.h"

#include <fstream>
#include <d3dcompiler.h>
#include <chrono>
#include <filesystem>

// DirectXTK for texture loading
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
        nullptr,
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

    SetupCommonVariables();
	SetupEffectVariables();

    LoadResourceNameTextures();

	logger::debug(std::format("Successfully loaded FX file: {}", a_filePath.string()));
    return true;
}

void Effect11::ExecuteTechniqueSequence(const std::string& baseTechniqueName, ID3D11RenderTargetView* renderTarget)
{
	auto context = globals::d3d::context;

    UpdateCommonVariables();
	UpdateEffectVariables();

    for (auto& technique : techniques[baseTechniqueName]){
		context->RSSetState(rasterizerState.Get());
		context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
		context->OMSetDepthStencilState(nullptr, 0);

		context->OMSetRenderTargets(1, &renderTarget, nullptr);

        D3DX11_TECHNIQUE_DESC techDesc;
		technique->GetDesc(&techDesc);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			technique->GetPassByIndex(p)->Apply(0, context);
			
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

		float4 screenSize = { state->screenSize.x, 1.0f / state->screenSize.x, aspect, 1.0f / aspect };
		Timer->SetRawValue(&screenSize, 0, sizeof(screenSize));
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
	Params01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));
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
    auto cacheIt = textureCache.find(filename);
    if (cacheIt != textureCache.end()) {
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

    if (FAILED(hr)) {
		logger::error(std::format("Failed to load texture file: {} (HRESULT: 0x{:08X})", filepath.string(), static_cast<uint32_t>(hr)));
        return nullptr;
    }

    // Cache the loaded texture
    textureCache[filename] = srv;
    
    logger::info(std::format("Successfully loaded texture: {}", filepath.string()));
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
#include "EffectManager.h"
#include "ENBEffect.h"
#include "ENBBloom.h"
#include "Globals.h"
#include <functional>
#include <d3dcompiler.h>
#include "State.h"

EffectManager& EffectManager::GetSingleton()
{
    static EffectManager instance;
    return instance;
}

void EffectManager::Initialize()
{
    InitializeSharedResources();
	RegisterEffects();
	ApplyEffects();
}

void EffectManager::RegisterEffects()
{
    auto registerEffect = [this](auto effect) {
        std::string name = effect->GetName();
        effects[name] = std::move(effect);
        logger::info("Registered effect: {}", name);
    };
    
    registerEffect(std::make_unique<ENBEffect>());
    registerEffect(std::make_unique<ENBBloom>());
}

void EffectManager::ApplyEffects()
{
    logger::info("Applying effects");

    for (auto& [name, effect] : effects) {
        effect->Apply();
    }

	logger::info("Applied effects");
}

void EffectManager::ExecuteEffects(RE::BSGraphics::RenderTargetData& input, 
                                     RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output)
{
	auto context = globals::d3d::context;

	context->RSSetState(sharedRasterizerState.Get());
	context->OMSetBlendState(sharedBlendState.Get(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

    UINT stride = sizeof(float) * 5;
	UINT offset = 0;
	ID3D11Buffer* vertexBuffers[] = { sharedQuadVertexBuffer.Get() };
	context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
	context->IASetInputLayout(sharedInputLayout.Get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    for (auto& [name, effect] : effects) {
        if (effect->IsCompiled()) {
            effect->Execute(input, swap, output);
        }
    }
}

void EffectManager::RenderImGui()
{
    if (ImGui::CollapsingHeader("Effect Manager", ImGuiTreeNodeFlags_DefaultOpen)) {

        if (ImGui::Button("Apply")) {
            ApplyEffects();
        }
        
        for (auto& [name, effect] : effects) {     
            bool isCompiled = effect->IsCompiled();
            const auto& errors = effect->GetErrors();
            
            // Color-code header based on status
            if (!isCompiled) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Red for errors
            }
            
            if (ImGui::CollapsingHeader(effect->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!isCompiled) {
                    ImGui::PopStyleColor();
                }
                
				if (isCompiled) {
					effect->RenderImGui();
				} else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Effect has compilation errors:");
                    ImGui::Indent();
                    for (const auto& error : errors) {
                        ImGui::BulletText("%s", error.c_str());
                    }
                    ImGui::Unindent();
				}
				ImGui::TreePop();
			} else if (!isCompiled) {
                ImGui::PopStyleColor();
            }
        }
    }
}

void EffectManager::InitializeSharedResources()
{
    CreateSharedQuadGeometry();
    CreateSharedRenderStates();
    SetupCommonTextures();
}

void EffectManager::CleanupSharedResources()
{
    sharedQuadVertexBuffer.Reset();
    sharedInputLayout.Reset();
    sharedRasterizerState.Reset();
    sharedBlendState.Reset();
    
    // Clear common textures
    commonTextureCache.clear();
}

void EffectManager::CreateSharedQuadGeometry()
{
    // Create a fullscreen quad vertex buffer that all effects can share
    struct QuadVertex {
        float position[3];
        float texCoord[2];
    };

    QuadVertex vertices[] = {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },  // Bottom left
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } },  // Top left
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },  // Bottom right
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } }   // Top right
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, sharedQuadVertexBuffer.GetAddressOf()));

    // Create input layout for ENB post-processing
    D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    // Create a simple vertex shader for the input layout
    ComPtr<ID3DBlob> vertexShaderBlob;
    const char* vertexShaderSource = R"(
        struct VS_INPUT_POST { float3 pos : POSITION; float2 txcoord : TEXCOORD0; };
        struct VS_OUTPUT_POST { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };
        VS_OUTPUT_POST VS_Draw(VS_INPUT_POST IN) {
            VS_OUTPUT_POST OUT;
            OUT.pos = float4(IN.pos, 1.0);
            OUT.txcoord0 = IN.txcoord;
            return OUT;
        }
    )";

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr, 
                           "VS_Draw", "vs_4_0", 0, 0, vertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());
    
    if (SUCCEEDED(hr)) {
        hr = globals::d3d::device->CreateInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs),
                                                    vertexShaderBlob->GetBufferPointer(), 
                                                    vertexShaderBlob->GetBufferSize(), 
                                                    sharedInputLayout.GetAddressOf());
        if (FAILED(hr)) {
            logger::error("Failed to create shared input layout for ENB effects");
        }
    }
}

void EffectManager::CreateSharedRenderStates()
{
    // Rasterizer state for fullscreen quads
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthBias = 0;
    rastDesc.DepthBiasClamp = 0.0f;
    rastDesc.SlopeScaledDepthBias = 0.0f;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.ScissorEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;

    DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rastDesc, sharedRasterizerState.GetAddressOf()));

    // Blend state for standard rendering (no blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, sharedBlendState.GetAddressOf()));
}

void EffectManager::SetupCommonTextures()
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

    // Create TextureBloom
    {
        Texture bloomTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, bloomTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(bloomTexture.texture.Get(), nullptr, bloomTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(bloomTexture.texture.Get(), nullptr, bloomTexture.srv.GetAddressOf()));     
        commonTextureCache.insert({ "TextureBloom", bloomTexture });
    }
    
    // Create TextureLens
    {
        Texture lensTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, lensTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(lensTexture.texture.Get(), nullptr, lensTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(lensTexture.texture.Get(), nullptr, lensTexture.srv.GetAddressOf()));    
        commonTextureCache.insert({ "TextureLens", lensTexture });
    }
    
    // Create 1x1 textures for adaptation and aperture
    texDesc.Width = 1;
    texDesc.Height = 1;

    // Create TextureAdaptation
    {
        Texture adaptationTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, adaptationTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.srv.GetAddressOf()));    
        commonTextureCache.insert({ "TextureAdaptation", adaptationTexture });
    }
    
    // Create TextureAperture
    {
        Texture apertureTexture{};
        DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, apertureTexture.texture.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateRenderTargetView(apertureTexture.texture.Get(), nullptr, apertureTexture.rtv.GetAddressOf()));
        DX::ThrowIfFailed(device->CreateShaderResourceView(apertureTexture.texture.Get(), nullptr, apertureTexture.srv.GetAddressOf()));	
        commonTextureCache.insert({ "TextureAperture", apertureTexture });
    }
    
    logger::info("Created shared common textures: TextureBloom, TextureLens, TextureAdaptation, TextureAperture");
}

void EffectManager::UpdateCommonData()
{
    auto state = globals::state;
    auto sky = globals::game::sky;

    // Update timer
    {
        auto modifiedTimer = (1000.0f * state->timer);
        modifiedTimer = std::fmodf(modifiedTimer, 16777216);
        modifiedTimer /= 16777216.0f;

        commonData.timer[0] = modifiedTimer;
        commonData.timer[1] = 60.0f;
        commonData.timer[2] = 0.0f;
        commonData.timer[3] = *globals::game::deltaTime;
    }
    
    // Update screen size
    {
        float aspect = state->screenSize.x / state->screenSize.y;

        commonData.screenSize[0] = state->screenSize.x;
        commonData.screenSize[1] = state->screenSize.y;
        commonData.screenSize[2] = aspect;
        commonData.screenSize[3] = 1.0f / aspect;
    }

    // Update weather
    {
        commonData.weather[0] = sky->currentWeather ? static_cast<float>(sky->currentWeather->formID) : 0;
        commonData.weather[1] = sky->lastWeather ? static_cast<float>(sky->lastWeather->formID) : 0;
        commonData.weather[2] = sky->currentWeatherPct;
        commonData.weather[3] = sky->currentGameHour;
    }

    // Update time of day
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

        // Initialize to zero
        commonData.timeOfDay1[0] = commonData.timeOfDay1[1] = commonData.timeOfDay1[2] = commonData.timeOfDay1[3] = 0.0f;
        commonData.timeOfDay2[0] = commonData.timeOfDay2[1] = commonData.timeOfDay2[2] = commonData.timeOfDay2[3] = 0.0f;

        // Dawn → Sunrise
        if (currentTime >= sunriseBegin && currentTime < dawnMid) {
            float f = range01(currentTime, sunriseBegin, dawnMid);
            commonData.timeOfDay1[0] = 1.0f - f;  // dawn
            commonData.timeOfDay1[1] = f;         // sunrise
        } else if (currentTime >= dawnMid && currentTime < sunriseEnd) {
            float f = range01(currentTime, dawnMid, sunriseEnd);
            commonData.timeOfDay1[1] = 1.0f - f;  // sunrise
            commonData.timeOfDay1[2] = f;         // day
        }
        // Day → Sunset
        else if (currentTime >= sunriseEnd && currentTime < sunsetBegin) {
            float f = range01(currentTime, sunriseEnd, sunsetBegin);
            commonData.timeOfDay1[2] = 1.0f - f;  // day
            commonData.timeOfDay1[3] = f;         // sunset
        }
        // Sunset → Dusk
        else if (currentTime >= sunsetBegin && currentTime < duskMid) {
            float f = range01(currentTime, sunsetBegin, duskMid);
            commonData.timeOfDay1[3] = 1.0f - f;  // sunset
            commonData.timeOfDay2[0] = f;         // dusk
        } else if (currentTime >= duskMid && currentTime < sunsetEnd) {
            float f = range01(currentTime, duskMid, sunsetEnd);
            commonData.timeOfDay2[0] = 1.0f - f;  // dusk
            commonData.timeOfDay2[1] = f;         // night
        }
        // Night → Dawn (wrap)
        else {
            float f = range01(currentTime, sunsetEnd, sunriseBegin);
            commonData.timeOfDay2[1] = 1.0f - f;  // night
            commonData.timeOfDay1[0] = f;         // dawn
        }
    }

    // Update night/day factor
    {
        commonData.eNightDayFactor = std::fabs(sky->currentGameHour - 12.0f);
        if (commonData.eNightDayFactor > 12.0f)
            commonData.eNightDayFactor = 24.0f - commonData.eNightDayFactor;
        commonData.eNightDayFactor = 1.0f - commonData.eNightDayFactor / 12.0f;
    }

    // Update interior factor
    {
        commonData.eInteriorFactor = sky->mode.any(RE::Sky::Mode::kInterior) ? 1.0f : 0.0f;
    }
}

void EffectManager::UpdateAllCommonVariables()
{
    UpdateCommonData();
    
    for (auto& [name, effect] : effects) {
        if (effect->IsCompiled()) {
            UpdateCommonVariablesForEffect(effect->GetEffect());       
        }
    }
}

void EffectManager::UpdateCommonVariablesForEffect(ID3DX11Effect* effect)
{
    if (!effect) return;

    // Get variable pointers and set common textures
    auto textureColor = effect->GetVariableByName("TextureColor")->AsShaderResource();
    auto textureBloom = effect->GetVariableByName("TextureBloom")->AsShaderResource();
    auto textureLens = effect->GetVariableByName("TextureLens")->AsShaderResource();
    auto textureAdaptation = effect->GetVariableByName("TextureAdaptation")->AsShaderResource();
    auto textureAperture = effect->GetVariableByName("TextureAperture")->AsShaderResource();

    auto timer = effect->GetVariableByName("Timer")->AsVector();
    auto screenSize = effect->GetVariableByName("ScreenSize")->AsVector();
    auto weather = effect->GetVariableByName("Weather")->AsVector();
    auto timeOfDay1 = effect->GetVariableByName("TimeOfDay1")->AsVector();
    auto timeOfDay2 = effect->GetVariableByName("TimeOfDay2")->AsVector();
    auto eNightDayFactor = effect->GetVariableByName("ENightDayFactor")->AsVector();
    auto eInteriorFactor = effect->GetVariableByName("EInteriorFactor")->AsVector();

    // Set texture resources
	if (textureColor && textureColor->IsValid()) {
		textureColor->SetResource(commonTextureCache["TextureColor"].srv.Get());
	}
    if (textureBloom && textureBloom->IsValid()) {
        textureBloom->SetResource(commonTextureCache["TextureBloom"].srv.Get());
    }
    if (textureLens && textureLens->IsValid()) {
        textureLens->SetResource(commonTextureCache["TextureLens"].srv.Get());
    }
    if (textureAdaptation && textureAdaptation->IsValid()) {
        textureAdaptation->SetResource(commonTextureCache["TextureAdaptation"].srv.Get());
    }
    if (textureAperture && textureAperture->IsValid()) {
        textureAperture->SetResource(commonTextureCache["TextureAperture"].srv.Get());
    }

    // Set variable data
    if (timer && timer->IsValid()) {
        timer->SetRawValue(commonData.timer, 0, sizeof(commonData.timer));
    }
    if (screenSize && screenSize->IsValid()) {
        screenSize->SetRawValue(commonData.screenSize, 0, sizeof(commonData.screenSize));
    }
    if (weather && weather->IsValid()) {
        weather->SetRawValue(commonData.weather, 0, sizeof(commonData.weather));
    }
    if (timeOfDay1 && timeOfDay1->IsValid()) {
        timeOfDay1->SetRawValue(commonData.timeOfDay1, 0, sizeof(commonData.timeOfDay1));
    }
    if (timeOfDay2 && timeOfDay2->IsValid()) {
        timeOfDay2->SetRawValue(commonData.timeOfDay2, 0, sizeof(commonData.timeOfDay2));
    }
    if (eNightDayFactor && eNightDayFactor->IsValid()) {
        eNightDayFactor->SetRawValue(&commonData.eNightDayFactor, 0, sizeof(commonData.eNightDayFactor));
    }
    if (eInteriorFactor && eInteriorFactor->IsValid()) {
        eInteriorFactor->SetRawValue(&commonData.eInteriorFactor, 0, sizeof(commonData.eInteriorFactor));
    }
}
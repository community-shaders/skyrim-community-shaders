#include "PCH.h"
#include "Effect11.h"
#include "Globals.h"
#include "State.h"
#include "Util.h"

#include <algorithm>
#include <chrono>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/WICTextureLoader.h>

using json = nlohmann::json;

void Effect11::Initialize()
{
	CreateRenderStates();
	CreateQuadGeometry();
}

bool Effect11::LoadFXFile(std::filesystem::path a_filePath)
{
	Microsoft::WRL::ComPtr<ID3DBlob> compiledShader;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DX11CompileEffectFromFile(
		a_filePath.c_str(),
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		globals::d3d::device,
		effect.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("Effect compilation failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return false;
	}

	SetupCommonTextures();

	SetupCommonVariables();
	SetupEffectVariables();
	EnumerateAllVariables();

	SetupCustomTextures();
	LoadTechniques();

	// Populate available techniques for UI selection
	availableTechniques = GetBaseTechniqueNames();
	if (!availableTechniques.empty()) {
		selectedTechnique = availableTechniques[0];  // Default to first technique
	}

	LoadUIVariables();

	// Store the loaded effect path
	loadedEffectPath = a_filePath.string();

	// Initialize preset system
	availablePresets = GetAvailablePresets();
	if (std::find(availablePresets.begin(), availablePresets.end(), "Default") == availablePresets.end()) {
		CreateDefaultPreset();
	}

	logger::debug("Successfully loaded FX file: {}", a_filePath.string());
	return true;
}

void Effect11::Execute(RE::BSGraphics::RenderTargetData& input, RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output)
{
	ExecuteTechniqueSequence(selectedTechnique, input, swap, output);
}

void Effect11::ExecuteTechniqueSequence(const std::string& baseTechniqueName, RE::BSGraphics::RenderTargetData& input, RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output)
{
	auto context = globals::d3d::context;

	// Safety check: ensure effect is loaded before calling update methods
	if (!effect) {
		logger::error("Effect is not loaded, cannot execute technique sequence");
		return;
	}

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

	// Track which buffer contains the current result
	bool currentIsInOutput = false;

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

		// Determine input and output for this technique
		ID3D11ShaderResourceView* inputSRV;
		ID3D11RenderTargetView* outputRTV;

		if (i == 0) {
			// First technique: read from input
			inputSRV = input.SRV;
			outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, output.RTV);
			currentIsInOutput = true;
		} else {
			// Subsequent techniques: ping-pong between output and swap
			if (currentIsInOutput) {
				inputSRV = output.SRV;
				outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, swap.RTV);
				currentIsInOutput = false;
			} else {
				inputSRV = swap.SRV;
				outputRTV = GetRenderTargetView(techniqueInfo.renderTargetName, output.RTV);
				currentIsInOutput = true;
			}
		}

        if (TextureColor) {
            TextureColor->AsShaderResource()->SetResource(inputSRV);
        }
        
        // Set up viewport for proper rendering
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = static_cast<float>(globals::state->screenSize.x);
        viewport.Height = static_cast<float>(globals::state->screenSize.y);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        
        context->OMSetRenderTargets(1, &outputRTV, nullptr);

		TextureColor->AsShaderResource()->SetResource(inputSRV);
		context->OMSetRenderTargets(1, &outputRTV, nullptr);
        if (TextureColor) {
            TextureColor->AsShaderResource()->SetResource(inputSRV);
        }
        
        // Set up viewport for proper rendering
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = static_cast<float>(globals::state->screenSize.x);
        viewport.Height = static_cast<float>(globals::state->screenSize.y);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        
        context->OMSetRenderTargets(1, &outputRTV, nullptr);

		D3DX11_TECHNIQUE_DESC techDesc;
		techniqueInfo.technique->GetDesc(&techDesc);

		for (UINT p = 0; p < techDesc.Passes; p++) {
			techniqueInfo.technique->GetPassByIndex(p)->Apply(0, context);

			UINT stride = sizeof(float) * 5;
			UINT offset = 0;
			ID3D11Buffer* vertexBuffers[] = { quadVertexBuffer.Get() };
			context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
			context->IASetInputLayout(inputLayout.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

			context->Draw(4, 0);
		}
	}

	// Ensure the final result is in the output buffer
	if (!currentIsInOutput) {
		context->CopyResource(output.texture, swap.texture);
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
		{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } }
	};

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(vertices);
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = vertices;

	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, quadVertexBuffer.GetAddressOf()));

	// Create input layout for ENB post-processing
	D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	// We need to get the vertex shader bytecode from the effect to create the input layout
	// For now, we'll create a simple input layout that should work with most effects
	Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
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

	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr,
		"VS_Draw", "vs_4_0", 0, 0, vertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());

	if (SUCCEEDED(hr)) {
		hr = globals::d3d::device->CreateInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs),
			vertexShaderBlob->GetBufferPointer(),
			vertexShaderBlob->GetBufferSize(),
			inputLayout.GetAddressOf());
		if (FAILED(hr)) {
			logger::error("Failed to create input layout for ENB quad");
		}
	}
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
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture adaptationTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, adaptationTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureAdaptation", adaptationTexture });
	}

	{
		texDesc.Width = 1;
		texDesc.Height = 1;

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
	// Get variables safely with null checks
	auto getVariable = [this](const char* name) -> Microsoft::WRL::ComPtr<ID3DX11EffectVariable> {
		if (!effect)
			return nullptr;
		auto var = effect->GetVariableByName(name);
		if (var && var->IsValid()) {
			Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
			comPtr.Attach(var);
			return comPtr;
		}
		return nullptr;
	};

	auto getShaderResource = [&](const char* name) -> Microsoft::WRL::ComPtr<ID3DX11EffectVariable> {
		auto var = getVariable(name);
		if (var) {
			auto shaderResourceVar = var->AsShaderResource();
			if (shaderResourceVar && shaderResourceVar->IsValid()) {
				Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
				comPtr.Attach(shaderResourceVar);
				return comPtr;
			}
		}
		return nullptr;
	};

	auto getVector = [&](const char* name) -> Microsoft::WRL::ComPtr<ID3DX11EffectVariable> {
		auto var = getVariable(name);
		if (var) {
			auto vectorVar = var->AsVector();
			if (vectorVar && vectorVar->IsValid()) {
				Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
				comPtr.Attach(vectorVar);
				return comPtr;
			}
		}
		return nullptr;
	};

	TextureColor = getShaderResource("TextureColor");
	TextureBloom = getShaderResource("TextureBloom");
	TextureLens = getShaderResource("TextureLens");
	TextureAdaptation = getShaderResource("TextureAdaptation");
	TextureAperture = getShaderResource("TextureAperture");

	Timer = getVector("Timer");
	ScreenSize = getVector("ScreenSize");
	AdaptiveQuality = getVector("AdaptiveQuality");
	Weather = getVector("Weather");
	TimeOfDay1 = getVector("TimeOfDay1");
	TimeOfDay2 = getVector("TimeOfDay2");
	ENightDayFactor = getVector("ENightDayFactor");
	EInteriorFactor = getVector("EInteriorFactor");

	// Set texture resources only if the variables exist
	if (TextureBloom && commonTextureCache.find("TextureBloom") != commonTextureCache.end()) {
		TextureBloom->AsShaderResource()->SetResource(commonTextureCache["TextureBloom"].srv.Get());
	}
	if (TextureLens && commonTextureCache.find("TextureLens") != commonTextureCache.end()) {
		TextureLens->AsShaderResource()->SetResource(commonTextureCache["TextureLens"].srv.Get());
	}
	if (TextureAdaptation && commonTextureCache.find("TextureAdaptation") != commonTextureCache.end()) {
		TextureAdaptation->AsShaderResource()->SetResource(commonTextureCache["TextureAdaptation"].srv.Get());
	}
	if (TextureAperture && commonTextureCache.find("TextureAperture") != commonTextureCache.end()) {
		TextureAperture->AsShaderResource()->SetResource(commonTextureCache["TextureAperture"].srv.Get());
	}
}

void Effect11::UpdateCommonVariables()
{
	// Safety check: ensure effect is loaded
	if (!effect) {
		return;
	}

	auto state = globals::state;

	// Timer variable
	if (Timer) {
		auto modifiedTimer = (1000.0f * state->timer);
		modifiedTimer = std::fmodf(modifiedTimer, 16777216);
		modifiedTimer /= 16777216.0f;

		float4 timer = { modifiedTimer, 60.0f, 0.0f, *globals::game::deltaTime };

		Timer->SetRawValue(&timer, 0, sizeof(timer));
	}

	// ScreenSize variable
	if (ScreenSize) {
		float aspect = state->screenSize.x / state->screenSize.y;

		float4 screenSize = { state->screenSize.x, state->screenSize.y, aspect, 1.0f / aspect };
		ScreenSize->SetRawValue(&screenSize, 0, sizeof(screenSize));
	}

	auto sky = globals::game::sky;

	// Weather variable
	if (Weather) {
		float4 weather = { sky->currentWeather ? static_cast<float>(sky->currentWeather->formID) : 0, sky->lastWeather ? static_cast<float>(sky->lastWeather->formID) : 0, sky->currentWeatherPct, sky->currentGameHour };

		Weather->SetRawValue(&weather, 0, sizeof(weather));
	}

	// TimeOfDay variables
	if (TimeOfDay1 || TimeOfDay2) {
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
		if (TimeOfDay1) {
			TimeOfDay1->SetRawValue(&timeOfDay1, 0, sizeof(timeOfDay1));
		}
		if (TimeOfDay2) {
			TimeOfDay2->SetRawValue(&timeOfDay2, 0, sizeof(timeOfDay2));
		}
	}

	// ENightDayFactor variable
	if (ENightDayFactor) {
		float eNightDayFactor = std::fabs(sky->currentGameHour - 12.0f);
		if (eNightDayFactor > 12.0f)
			eNightDayFactor = 24.0f - eNightDayFactor;
		eNightDayFactor = 1.0f - eNightDayFactor / 12.0f;

		ENightDayFactor->SetRawValue(&eNightDayFactor, 0, sizeof(eNightDayFactor));
	}

	// EInteriorFactor variable
	if (EInteriorFactor) {
		float eInteriorFactor = sky->mode.any(RE::Sky::Mode::kInterior);

		EInteriorFactor->SetRawValue(&eInteriorFactor, 0, sizeof(eInteriorFactor));
	}
}

void Effect11::SetupEffectVariables()
{
	// Get variables safely with null checks
	auto getVariable = [this](const char* name) -> Microsoft::WRL::ComPtr<ID3DX11EffectVariable> {
		if (!effect)
			return nullptr;
		auto var = effect->GetVariableByName(name);
		if (var && var->IsValid()) {
			Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
			comPtr.Attach(var);
			return comPtr;
		}
		return nullptr;
	};

	auto getVector = [&](const char* name) -> Microsoft::WRL::ComPtr<ID3DX11EffectVariable> {
		auto var = getVariable(name);
		if (var) {
			auto vectorVar = var->AsVector();
			if (vectorVar && vectorVar->IsValid()) {
				Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
				comPtr.Attach(vectorVar);
				return comPtr;
			}
		}
		return nullptr;
	};

	Params01 = getVector("Params01");
	ENBParams01 = getVector("ENBParams01");
}

void Effect11::UpdateEffectVariables()
{
	// Safety check: ensure effect is loaded
	if (!effect) {
		return;
	}

	if (Params01) {
		float4 params01[7]{
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f }
		};

		params01[4].w = 0.0f;
		params01[5].w = 0.0f;

		Params01->SetRawValue(&params01, 0, sizeof(params01));
	}

	if (ENBParams01) {
		float4 enbParams01{};
		ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));
	}
}

void Effect11::SetupCustomTextures()
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

	Microsoft::WRL::ComPtr<ID3D11Resource> texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;

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
		TechniqueInfo techInfo;
		techInfo.technique.Attach(technique);
		techInfo.renderTargetName = renderTargetName;
		techniques[baseName][sequenceNumber] = std::move(techInfo);

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
			continue;  // No UI annotation, skip
		}

		UIVariable uiVar = {};
		uiVar.name = varDesc.Name;
		uiVar.displayName = uiName;
		uiVar.description = GetUIAnnotation(variable, "UIDescription");  // Load description for tooltips
		uiVar.effectVariable = variable;

		D3DX11_EFFECT_TYPE_DESC typeDesc;
		auto effectType = variable->GetType();
		if (SUCCEEDED(effectType->GetDesc(&typeDesc))) {
			switch (typeDesc.Type) {
			case D3D_SVT_FLOAT:
				if (typeDesc.Elements == 0) {
					if (typeDesc.Columns == 1)
						uiVar.type = UIVariableType::Float;
					else if (typeDesc.Columns == 2)
						uiVar.type = UIVariableType::Float2;
					else if (typeDesc.Columns == 3)
						uiVar.type = UIVariableType::Float3;
					else if (typeDesc.Columns == 4)
						uiVar.type = UIVariableType::Float4;
					else
						continue;  // Unsupported
				} else
					continue;  // Arrays not supported yet
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

		// Categorize the variable
		uiVar.category = CategorizeVariable(uiVar.name, uiVar.displayName);

		logger::debug("Variable '{}': UIWidget='{}', parsed as {}, category: {}",
			uiVar.name, widgetStr, static_cast<int>(uiVar.widgetType), CategoryToString(uiVar.category));

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

	if (lowerWidget == "spinner")
		return UIWidgetType::Spinner;
	if (lowerWidget == "dropdown")
		return UIWidgetType::Dropdown;
	return UIWidgetType::Default;
}

Effect11::UIVariableCategory Effect11::CategorizeVariable(const std::string& variableName, const std::string& uiName)
{
	std::string lowerName = variableName;
	std::string lowerUIName = uiName;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
	std::transform(lowerUIName.begin(), lowerUIName.end(), lowerUIName.begin(), ::tolower);

	// Camera-related variables
	if (lowerName.find("camera") != std::string::npos ||
		lowerName.find("fov") != std::string::npos ||
		lowerName.find("focus") != std::string::npos ||
		lowerName.find("dof") != std::string::npos ||
		lowerName.find("depth") != std::string::npos ||
		lowerUIName.find("camera") != std::string::npos ||
		lowerUIName.find("focus") != std::string::npos ||
		lowerUIName.find("depth of field") != std::string::npos) {
		return UIVariableCategory::Camera;
	}

	// Time of Day variables
	if (lowerName.find("timeofday") != std::string::npos ||
		lowerName.find("time_of_day") != std::string::npos ||
		lowerName.find("tod") != std::string::npos ||
		lowerName.find("day") != std::string::npos ||
		lowerName.find("night") != std::string::npos ||
		lowerName.find("dawn") != std::string::npos ||
		lowerName.find("dusk") != std::string::npos ||
		lowerName.find("sunset") != std::string::npos ||
		lowerName.find("sunrise") != std::string::npos ||
		lowerUIName.find("time") != std::string::npos ||
		lowerUIName.find("day") != std::string::npos ||
		lowerUIName.find("night") != std::string::npos) {
		return UIVariableCategory::TimeOfDay;
	}

	// Adaptation variables
	if (lowerName.find("adapt") != std::string::npos ||
		lowerName.find("exposure") != std::string::npos ||
		lowerName.find("eyeadapt") != std::string::npos ||
		lowerName.find("eye_adapt") != std::string::npos ||
		lowerUIName.find("adapt") != std::string::npos ||
		lowerUIName.find("exposure") != std::string::npos ||
		lowerUIName.find("eye") != std::string::npos) {
		return UIVariableCategory::Adaptation;
	}

	// Color variables
	if (lowerName.find("color") != std::string::npos ||
		lowerName.find("tint") != std::string::npos ||
		lowerName.find("hue") != std::string::npos ||
		lowerName.find("saturation") != std::string::npos ||
		lowerUIName.find("color") != std::string::npos ||
		lowerUIName.find("tint") != std::string::npos ||
		lowerUIName.find("hue") != std::string::npos ||
		lowerUIName.find("saturation") != std::string::npos) {
		return UIVariableCategory::Colors;
	}

	// Tone Mapping variables
	if (lowerName.find("brightness") != std::string::npos ||
		lowerName.find("contrast") != std::string::npos ||
		lowerName.find("gamma") != std::string::npos ||
		lowerName.find("tonemap") != std::string::npos ||
		lowerName.find("tone_map") != std::string::npos ||
		lowerUIName.find("brightness") != std::string::npos ||
		lowerUIName.find("contrast") != std::string::npos ||
		lowerUIName.find("gamma") != std::string::npos ||
		lowerUIName.find("tone") != std::string::npos) {
		return UIVariableCategory::ToneMapping;
	}

	// Lens Effects variables
	if (lowerName.find("vignette") != std::string::npos ||
		lowerName.find("lens") != std::string::npos ||
		lowerName.find("chromatic") != std::string::npos ||
		lowerName.find("distortion") != std::string::npos ||
		lowerName.find("aberration") != std::string::npos ||
		lowerUIName.find("vignette") != std::string::npos ||
		lowerUIName.find("lens") != std::string::npos ||
		lowerUIName.find("chromatic") != std::string::npos ||
		lowerUIName.find("distortion") != std::string::npos) {
		return UIVariableCategory::LensEffects;
	}

	// Processing variables
	if (lowerName.find("blur") != std::string::npos ||
		lowerName.find("sharp") != std::string::npos ||
		lowerName.find("noise") != std::string::npos ||
		lowerName.find("grain") != std::string::npos ||
		lowerUIName.find("blur") != std::string::npos ||
		lowerUIName.find("sharp") != std::string::npos ||
		lowerUIName.find("noise") != std::string::npos ||
		lowerUIName.find("grain") != std::string::npos) {
		return UIVariableCategory::Processing;
	}

	// Lighting variables
	if (lowerName.find("light") != std::string::npos ||
		lowerName.find("shadow") != std::string::npos ||
		lowerName.find("ambient") != std::string::npos ||
		lowerName.find("specular") != std::string::npos ||
		lowerName.find("diffuse") != std::string::npos ||
		lowerUIName.find("light") != std::string::npos ||
		lowerUIName.find("shadow") != std::string::npos ||
		lowerUIName.find("ambient") != std::string::npos) {
		return UIVariableCategory::Lighting;
	}

	// Atmosphere variables
	if (lowerName.find("fog") != std::string::npos ||
		lowerName.find("sky") != std::string::npos ||
		lowerName.find("cloud") != std::string::npos ||
		lowerName.find("atmosphere") != std::string::npos ||
		lowerName.find("weather") != std::string::npos ||
		lowerUIName.find("fog") != std::string::npos ||
		lowerUIName.find("sky") != std::string::npos ||
		lowerUIName.find("cloud") != std::string::npos ||
		lowerUIName.find("atmosphere") != std::string::npos) {
		return UIVariableCategory::Atmosphere;
	}

	// Post Processing variables
	if (lowerName.find("bloom") != std::string::npos ||
		lowerName.find("glare") != std::string::npos ||
		lowerName.find("glow") != std::string::npos ||
		lowerName.find("flare") != std::string::npos ||
		lowerName.find("postprocess") != std::string::npos ||
		lowerUIName.find("bloom") != std::string::npos ||
		lowerUIName.find("glow") != std::string::npos ||
		lowerUIName.find("postprocess") != std::string::npos) {
		return UIVariableCategory::PostProcessing;
	}

	// Check if this should be in General (ONLY preset management and technique selection)
	if ((lowerName.find("technique") != std::string::npos) ||
		(lowerName.find("preset") != std::string::npos) ||
		(lowerUIName.find("technique") != std::string::npos) ||
		(lowerUIName.find("preset") != std::string::npos) ||
		(lowerName == "technique") ||
		(lowerName == "preset") ||
		(lowerUIName == "technique") ||
		(lowerUIName == "preset")) {
		return UIVariableCategory::General;
	}

	// Default to Other for everything else (including enable/disable toggles)
	return UIVariableCategory::Other;
}

std::string Effect11::CategoryToString(UIVariableCategory category)
{
	switch (category) {
	case UIVariableCategory::General:
		return "General";
	case UIVariableCategory::Camera:
		return "Camera";
	case UIVariableCategory::TimeOfDay:
		return "Time of Day";
	case UIVariableCategory::Adaptation:
		return "Adaptation";
	case UIVariableCategory::Colors:
		return "Colors";
	case UIVariableCategory::ToneMapping:
		return "Tone Mapping";
	case UIVariableCategory::LensEffects:
		return "Lens Effects";
	case UIVariableCategory::Processing:
		return "Processing";
	case UIVariableCategory::Lighting:
		return "Lighting";
	case UIVariableCategory::Atmosphere:
		return "Atmosphere";
	case UIVariableCategory::PostProcessing:
		return "Post Processing";
	case UIVariableCategory::Custom:
		return "Custom";
	case UIVariableCategory::Other:
		return "Other";
	default:
		return "Other";
	}
}

std::vector<Effect11::UIVariableCategory> Effect11::GetAvailableCategories()
{
	std::set<UIVariableCategory> categorySet;

	for (const auto& uiVar : uiVariables) {
		categorySet.insert(uiVar.category);
	}

	std::vector<UIVariableCategory> categories(categorySet.begin(), categorySet.end());

	// Sort categories with General first
	std::sort(categories.begin(), categories.end(), [](UIVariableCategory a, UIVariableCategory b) {
		if (a == UIVariableCategory::General)
			return true;
		if (b == UIVariableCategory::General)
			return false;
		return a < b;
	});

	return categories;
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
	case UIVariableType::Float2:
		if (SUCCEEDED(uiVar.effectVariable->AsVector()->GetFloatVector(uiVar.float2Value))) {
			// Successfully loaded float2 value
		}
		break;
	case UIVariableType::Float3:
		if (SUCCEEDED(uiVar.effectVariable->AsVector()->GetFloatVector(uiVar.float3Value))) {
			// Successfully loaded float3 value
		}
		break;
	case UIVariableType::Float4:
		if (SUCCEEDED(uiVar.effectVariable->AsVector()->GetFloatVector(uiVar.float4Value))) {
			// Successfully loaded float4 value
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
		case UIVariableType::Float2:
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.float2Value);
			break;
		case UIVariableType::Float3:
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.float3Value);
			break;
		case UIVariableType::Float4:
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.float4Value);
			break;
		}
	}
}

void Effect11::RenderImGui()
{
	bool valuesChanged = false;

	// Only render if effect is loaded
	if (!effect) {
		ImGui::TextDisabled("No effect loaded");
		return;
	}

	// Render main content in a tab bar
	if (ImGui::BeginTabBar("ENBEffectTabs")) {
		// General tab (always present)
		if (ImGui::BeginTabItem("General")) {
			RenderGeneralTab(valuesChanged);
			ImGui::EndTabItem();
		}

		// Dynamic category tabs based on available variables
		auto availableCategories = GetAvailableCategories();
		for (auto category : availableCategories) {
			if (category == UIVariableCategory::General)
				continue;  // Already handled above

			std::string tabName = CategoryToString(category);
			if (ImGui::BeginTabItem(tabName.c_str())) {
				RenderCategoryTab(category, tabName, valuesChanged);
				ImGui::EndTabItem();
			}
		}

		// About tab - condensed info
		if (ImGui::BeginTabItem("About")) {
			RenderCondensedAboutTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Update shader variables if any values changed
	if (valuesChanged) {
		UpdateUIVariables();
	}
}

void Effect11::RenderGeneralTab(bool& valuesChanged)
{
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
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Select the rendering technique to use from the loaded effect.");
		}
		ImGui::Separator();
	}

	// Preset management section
	if (ImGui::CollapsingHeader("Preset Management", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Current preset display
		ImGui::Text("Current Preset: %s", currentPresetName.c_str());

		// Save preset
		static char savePresetName[256] = "";
		ImGui::PushItemWidth(200);
		if (ImGui::InputText("##SavePresetName", savePresetName, sizeof(savePresetName))) {
			// Input changed
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Save Preset")) {
			if (strlen(savePresetName) > 0) {
				if (SavePreset(savePresetName)) {
					logger::info("Saved preset: {}", savePresetName);
					RefreshPresetList();
					strcpy_s(savePresetName, sizeof(savePresetName), "");
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enter a name and click Save to create a new preset with current settings.");
		}

		// Load preset
		if (!availablePresets.empty()) {
			ImGui::PushItemWidth(200);
			if (ImGui::BeginCombo("##LoadPreset", "Select Preset...")) {
				for (const auto& preset : availablePresets) {
					if (ImGui::Selectable(preset.c_str())) {
						if (LoadPreset(preset)) {
							logger::info("Loaded preset: {}", preset);
							valuesChanged = true;
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGui::Button("Refresh##Presets")) {
				RefreshPresetList();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Load a previously saved preset. This will override current settings.");
			}
		}

		ImGui::Separator();
	}

	// Render General category variables
	RenderCategoryTab(UIVariableCategory::General, "General", valuesChanged);
}

void Effect11::RenderCategoryTab(UIVariableCategory category, const std::string& tabName, bool& valuesChanged)
{
	const float labelWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWWWWWWW").x;
	const float inputWidth = ImGui::CalcTextSize("WWWWWWWWWWWWWWWWWWWW").x;

	// Filter variables by category
	std::vector<UIVariable*> categoryVariables;
	for (auto& uiVar : uiVariables) {
		if (uiVar.category == category) {
			categoryVariables.push_back(&uiVar);
		}
	}

	if (categoryVariables.empty()) {
		ImGui::TextDisabled("No %s settings available in this effect.", tabName.c_str());
		return;
	}

	// Render variables for this category
	for (auto* uiVar : categoryVariables) {
		// Skip spacers
		if (uiVar->displayName.empty() || std::all_of(uiVar->displayName.begin(), uiVar->displayName.end(), [](char c) { return std::isspace(c); })) {
			ImGui::Spacing();
			continue;
		}

		RenderVariableUI(*uiVar, labelWidth, inputWidth, valuesChanged);
	}
}

void Effect11::RenderAboutTab()
{
	if (!loadedEffectPath.empty()) {
		ImGui::Text("Loaded Effect:");
		ImGui::Indent();
		ImGui::TextWrapped("%s", loadedEffectPath.c_str());
		ImGui::Unindent();
		ImGui::Separator();
	}

	if (!availableTechniques.empty()) {
		ImGui::Text("Available Techniques: %zu", availableTechniques.size());
		ImGui::Indent();
		for (const auto& technique : availableTechniques) {
			ImGui::BulletText("%s", technique.c_str());
		}
		ImGui::Unindent();
		ImGui::Separator();
	}

	if (!uiVariables.empty()) {
		ImGui::Text("Effect Variables: %zu", uiVariables.size());

		// Show category breakdown
		auto availableCategories = GetAvailableCategories();
		ImGui::Indent();
		for (auto category : availableCategories) {
			int count = 0;
			for (const auto& uiVar : uiVariables) {
				if (uiVar.category == category)
					count++;
			}
			ImGui::BulletText("%s: %d variables", CategoryToString(category).c_str(), count);
		}
		ImGui::Unindent();
		ImGui::Separator();
	}

	ImGui::TextWrapped(
		"ENB Post Processing uses DirectX 11 Effects (.fx files) to apply "
		"advanced post-processing effects to the rendered scene. Effects are "
		"automatically categorized based on their variable names and purposes.");

	ImGui::Spacing();
	ImGui::TextWrapped(
		"For best performance, disable effects you don't need and use "
		"presets to quickly switch between different visual configurations.");
}

void Effect11::RenderCondensedAboutTab()
{
	if (!loadedEffectPath.empty()) {
		ImGui::Text("Loaded Effect: %s", std::filesystem::path(loadedEffectPath).filename().string().c_str());
	}

	if (!availableTechniques.empty()) {
		ImGui::Text("Available Techniques: %zu", availableTechniques.size());
		if (!uiVariables.empty()) {
			ImGui::SameLine();
			ImGui::Text("| Effect Variables: %zu", uiVariables.size());
		}
	}

	ImGui::Spacing();
}

void Effect11::EnumerateAllVariables()
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
		Microsoft::WRL::ComPtr<ID3DX11EffectVariable> comPtr;
		comPtr.Attach(variable);
		variables[varName] = comPtr;

		logger::debug("Enumerated variable: {}", varName);
	}

	logger::info("Enumerated {} effect variables", variables.size());
}

bool Effect11::SavePreset(const std::string& presetName)
{
	if (presetName.empty() || uiVariables.empty()) {
		logger::warn("Cannot save preset: invalid name or no variables loaded");
		return false;
	}

	try {
		// Create presets directory if it doesn't exist
		std::filesystem::path presetDir = "enbseries/presets";
		std::filesystem::create_directories(presetDir);

		// Create preset file path
		std::filesystem::path presetPath = presetDir / (presetName + ".json");

		json presetData;
		presetData["effectPath"] = loadedEffectPath;
		presetData["selectedTechnique"] = selectedTechnique;
		presetData["variables"] = json::object();

		// Save all UI variable values
		for (const auto& uiVar : uiVariables) {
			json varData;
			varData["type"] = static_cast<int>(uiVar.type);

			switch (uiVar.type) {
			case UIVariableType::Float:
				varData["value"] = uiVar.floatValue;
				break;
			case UIVariableType::Int:
				varData["value"] = uiVar.intValue;
				break;
			case UIVariableType::Bool:
				varData["value"] = uiVar.boolValue;
				break;
			case UIVariableType::Float2:
				varData["value"] = std::vector<float>(uiVar.float2Value, uiVar.float2Value + 2);
				break;
			case UIVariableType::Float3:
				varData["value"] = std::vector<float>(uiVar.float3Value, uiVar.float3Value + 3);
				break;
			case UIVariableType::Float4:
				varData["value"] = std::vector<float>(uiVar.float4Value, uiVar.float4Value + 4);
				break;
			}

			presetData["variables"][uiVar.name] = varData;
		}

		// Write to file
		std::ofstream file(presetPath);
		if (!file.is_open()) {
			logger::error("Failed to create preset file: {}", presetPath.string());
			return false;
		}

		file << presetData.dump(4);
		file.close();

		logger::info("Saved preset '{}' with {} variables", presetName, uiVariables.size());

		// Update available presets list
		availablePresets = GetAvailablePresets();
		currentPresetName = presetName;

		return true;
	} catch (const std::exception& e) {
		logger::error("Error saving preset '{}': {}", presetName, e.what());
		return false;
	}
}

bool Effect11::LoadPreset(const std::string& presetName)
{
	if (presetName.empty()) {
		logger::warn("Cannot load preset: invalid name");
		return false;
	}

	try {
		std::filesystem::path presetPath = std::filesystem::path("enbseries/presets") / (presetName + ".json");

		if (!std::filesystem::exists(presetPath)) {
			logger::warn("Preset file does not exist: {}", presetPath.string());
			return false;
		}

		std::ifstream file(presetPath);
		if (!file.is_open()) {
			logger::error("Failed to open preset file: {}", presetPath.string());
			return false;
		}

		json presetData;
		file >> presetData;
		file.close();

		// Validate preset data
		if (!presetData.contains("variables") || !presetData["variables"].is_object()) {
			logger::error("Invalid preset format: missing or invalid variables");
			return false;
		}

		// Load variable values
		int loadedCount = 0;
		for (auto& uiVar : uiVariables) {
			if (presetData["variables"].contains(uiVar.name)) {
				const auto& varData = presetData["variables"][uiVar.name];

				if (!varData.contains("type") || !varData.contains("value")) {
					continue;
				}

				UIVariableType savedType = static_cast<UIVariableType>(varData["type"].get<int>());
				if (savedType != uiVar.type) {
					logger::warn("Type mismatch for variable '{}', skipping", uiVar.name);
					continue;
				}

				try {
					switch (uiVar.type) {
					case UIVariableType::Float:
						uiVar.floatValue = varData["value"].get<float>();
						break;
					case UIVariableType::Int:
						uiVar.intValue = varData["value"].get<int>();
						break;
					case UIVariableType::Bool:
						uiVar.boolValue = varData["value"].get<bool>();
						break;
					case UIVariableType::Float2:
						{
							auto values = varData["value"].get<std::vector<float>>();
							if (values.size() >= 2) {
								std::copy(values.begin(), values.begin() + 2, uiVar.float2Value);
							}
							break;
						}
					case UIVariableType::Float3:
						{
							auto values = varData["value"].get<std::vector<float>>();
							if (values.size() >= 3) {
								std::copy(values.begin(), values.begin() + 3, uiVar.float3Value);
							}
							break;
						}
					case UIVariableType::Float4:
						{
							auto values = varData["value"].get<std::vector<float>>();
							if (values.size() >= 4) {
								std::copy(values.begin(), values.begin() + 4, uiVar.float4Value);
							}
							break;
						}
					}
					loadedCount++;
				} catch (const std::exception& e) {
					logger::warn("Error loading value for variable '{}': {}", uiVar.name, e.what());
				}
			}
		}

		// Load technique selection if it exists and is valid
		if (presetData.contains("selectedTechnique") && presetData["selectedTechnique"].is_string()) {
			std::string savedTechnique = presetData["selectedTechnique"].get<std::string>();
			auto it = std::find(availableTechniques.begin(), availableTechniques.end(), savedTechnique);
			if (it != availableTechniques.end()) {
				selectedTechnique = savedTechnique;
			}
		}

		// Update shader variables with loaded values
		UpdateUIVariables();

		currentPresetName = presetName;
		logger::info("Loaded preset '{}' with {} variables", presetName, loadedCount);

		return true;
	} catch (const std::exception& e) {
		logger::error("Error loading preset '{}': {}", presetName, e.what());
		return false;
	}
}

std::vector<std::string> Effect11::GetAvailablePresets()
{
	std::vector<std::string> presets;

	try {
		std::filesystem::path presetDir = "enbseries/presets";

		if (!std::filesystem::exists(presetDir) || !std::filesystem::is_directory(presetDir)) {
			return presets;
		}

		for (const auto& entry : std::filesystem::directory_iterator(presetDir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				std::string presetName = entry.path().stem().string();
				presets.push_back(presetName);
			}
		}

		// Sort presets alphabetically
		std::sort(presets.begin(), presets.end());

	} catch (const std::exception& e) {
		logger::error("Error scanning presets directory: {}", e.what());
	}

	return presets;
}

void Effect11::CreateDefaultPreset()
{
	if (!uiVariables.empty()) {
		SavePreset("Default");
	}
}

void Effect11::RefreshPresetList()
{
	availablePresets = GetAvailablePresets();
}

void Effect11::RenderVariableUI(UIVariable& uiVar, float labelWidth, float inputWidth, bool& valuesChanged)
{
	// Clamp label to fixed width
	std::string label = uiVar.displayName;
	while (ImGui::CalcTextSize(label.c_str()).x > labelWidth && !label.empty()) {
		label.pop_back();
	}

	// Draw label and position input
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s", label.c_str());

	// Add tooltip if description is available
	if (!uiVar.description.empty() && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", uiVar.description.c_str());
	}

	ImGui::SameLine(labelWidth + ImGui::GetStyle().ItemSpacing.x);

	// Skip inputs for labels (min == max == 0)
	bool isLabelOnly = (uiVar.type == UIVariableType::Float && uiVar.floatMin == 0 && uiVar.floatMax == 0) ||
	                   (uiVar.type == UIVariableType::Int && uiVar.intMin == 0 && uiVar.intMax == 0);

	if (!isLabelOnly) {
		if (ImGui::BeginChild(("##input_" + uiVar.name).c_str(), ImVec2(inputWidth, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar)) {
			ImGui::SetNextItemWidth(-1);

			if (uiVar.type == UIVariableType::Float) {
				if (uiVar.floatMin != uiVar.floatMax) {
					if (ImGui::SliderFloat(("##" + uiVar.name).c_str(), &uiVar.floatValue, uiVar.floatMin, uiVar.floatMax, "%.3f")) {
						valuesChanged = true;
					}
				} else {
					if (ImGui::InputFloat(("##" + uiVar.name).c_str(), &uiVar.floatValue, 0.0f, 0.0f, "%.3f")) {
						valuesChanged = true;
					}
				}
			} else if (uiVar.type == UIVariableType::Float2) {
				if (ImGui::InputFloat2(("##" + uiVar.name).c_str(), uiVar.float2Value, "%.3f")) {
					valuesChanged = true;
				}
			} else if (uiVar.type == UIVariableType::Float3) {
				if (ImGui::ColorEdit3(("##" + uiVar.name).c_str(), uiVar.float3Value)) {
					valuesChanged = true;
				}
			} else if (uiVar.type == UIVariableType::Float4) {
				if (ImGui::ColorEdit4(("##" + uiVar.name).c_str(), uiVar.float4Value)) {
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
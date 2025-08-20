#include "EffectManager.h"
#include "ENBAdaptation.h"
#include "ENBBloom.h"
#include "ENBDepthOfField.h"
#include "ENBEffect.h"
#include "ENBEffectPostPass.h"
#include "ENBEffectPrePass.h"
#include "ENBLens.h"
#include "Effect.h"
#include "Globals.h"
#include "State.h"
#include <d3dcompiler.h>
#include <functional>

EffectManager& EffectManager::GetSingleton()
{
	static EffectManager instance;
	return instance;
}

void EffectManager::Initialize()
{
	CreateCommonResources();
	RegisterEffects();
	ApplyEffects();
	LoadENBSettings();
}

void EffectManager::RegisterEffects()
{
	auto registerEffect = [this](auto effect) {
		std::string name = effect->GetName();
		effects.emplace_back(name, std::move(effect));
		logger::info("Registered effect: {}", name);
	};

	registerEffect(std::make_unique<ENBEffectPrePass>());
	registerEffect(std::make_unique<ENBDepthOfField>());
	registerEffect(std::make_unique<ENBAdaptation>());
	registerEffect(std::make_unique<ENBLens>());
	registerEffect(std::make_unique<ENBBloom>());
	registerEffect(std::make_unique<ENBEffect>());
	registerEffect(std::make_unique<ENBEffectPostPass>());
}

void EffectManager::ApplyEffects()
{
	logger::info("Applying effects");

	for (auto& [name, effect] : effects) {
		effect->Apply();
	}

	logger::info("Applied effects");
}

void EffectManager::LoadEffects()
{
	logger::info("Loading effects");

	for (auto& [name, effect] : effects) {
		effect->Load();
	}

	logger::info("Loaded effects");
}

void EffectManager::SaveEffects()
{
	logger::info("Saving effects");

	for (auto& [name, effect] : effects) {
		effect->Save();
	}

	logger::info("Saved effects");
}

void EffectManager::ExecuteEffects()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	UpdateCommonData();

	// Perform shared downsampling once
	Downsampler::GetSingleton().Downsample(textureOriginal.SRV, sharedDownsampleChain);

	// Backup current render state
	ComPtr<ID3D11RasterizerState> previousRS;
	ComPtr<ID3D11BlendState> previousBS;
	ComPtr<ID3D11DepthStencilState> previousDSS;
	ComPtr<ID3D11InputLayout> previousIL;
	FLOAT previousBlendFactor[4];
	UINT previousSampleMask;
	UINT previousStencilRef;

	context->RSGetState(previousRS.GetAddressOf());
	context->OMGetBlendState(previousBS.GetAddressOf(), previousBlendFactor, &previousSampleMask);
	context->OMGetDepthStencilState(previousDSS.GetAddressOf(), &previousStencilRef);
	context->IAGetInputLayout(previousIL.GetAddressOf());

	ID3D11Buffer* previousVBs[1] = { nullptr };
	UINT previousStrides[1] = { 0 };
	UINT previousOffsets[1] = { 0 };
	D3D11_PRIMITIVE_TOPOLOGY previousTopology;
	context->IAGetVertexBuffers(0, 1, previousVBs, previousStrides, previousOffsets);
	context->IAGetPrimitiveTopology(&previousTopology);

	// Set our render state
	context->RSSetState(rasterizerState.Get());
	context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

	UINT stride = sizeof(float) * 5;
	UINT offset = 0;
	ID3D11Buffer* vertexBuffers[] = { quadVertexBuffer.Get() };
	context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
	context->IASetInputLayout(inputLayout.Get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	for (auto& [name, effect] : effects) {
		if (effect->IsCompiled()) {
			auto state = globals::state;
			state->BeginPerfEvent(std::format("{}", effect->GetName()));
			UpdateCommonVariablesForEffect(effect->GetEffect());
			effect->Execute();
			state->EndPerfEvent();
		}
	}

	// Restore previous render state
	context->RSSetState(previousRS.Get());
	context->OMSetBlendState(previousBS.Get(), previousBlendFactor, previousSampleMask);
	context->OMSetDepthStencilState(previousDSS.Get(), previousStencilRef);
	context->IASetInputLayout(previousIL.Get());
	context->IASetVertexBuffers(0, 1, previousVBs, previousStrides, previousOffsets);
	context->IASetPrimitiveTopology(previousTopology);

	// Clean up retrieved interfaces
	if (previousVBs[0])
		previousVBs[0]->Release();
}

Effect::Texture* EffectManager::GetCommonTexture(const std::string& name)
{
	auto it = commonTextureCache.find(name);
	if (it != commonTextureCache.end()) {
		return &it->second;
	}
	return nullptr;
}

void EffectManager::RenderImGui()
{
	// Two-column layout
	if (ImGui::BeginTable("EffectManagerTable", 2, ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("ENB Settings", ImGuiTableColumnFlags_WidthFixed, 400.0f);
		ImGui::TableSetupColumn("Effects", ImGuiTableColumnFlags_WidthStretch);
			
		ImGui::TableNextRow();
			
		// Left Side - ENB Settings
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild("ENBSettings", ImVec2(0, 0), false)) {

			if (ImGui::Button("Apply")) {
				ApplyEffects();
			}

			ImGui::SameLine();

			if (ImGui::Button("Load")) {
				LoadEffects();
			}

			ImGui::SameLine();

			if (ImGui::Button("Save")) {
				SaveEffects();
				SaveENBSettings();
			}

			if (ImGui::TreeNodeEx("COLORCORRECTION", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat("Brightness", &enbSettings.COLORCORRECTION.Brightness);
				ImGui::DragFloat("GammaCurve", &enbSettings.COLORCORRECTION.GammaCurve);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("ADAPTATION", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat("AdaptationSensitivity", &enbSettings.ADAPTATION.AdaptationSensitivity);
				ImGui::Checkbox("ForceMinMaxValues", &enbSettings.ADAPTATION.ForceMinMaxValues);
				ImGui::DragFloat("AdaptationMin", &enbSettings.ADAPTATION.AdaptationMin);
				ImGui::DragFloat("AdaptationMax", &enbSettings.ADAPTATION.AdaptationMax);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("DEPTHOFFIELD", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat("FocusingTime", &enbSettings.DEPTHOFFIELD.FocusingTime);
				ImGui::DragFloat("ApertureTime", &enbSettings.DEPTHOFFIELD.ApertureTime);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("BLOOM", ImGuiTreeNodeFlags_DefaultOpen)) {
				RenderTimeOfDaySettings("Amount", enbSettings.BLOOM.Amount);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("LENS", ImGuiTreeNodeFlags_DefaultOpen)) {
				RenderTimeOfDaySettings("Amount", enbSettings.LENS.Amount);

				ImGui::TreePop();
			}
		}
		ImGui::EndChild();

		// Right side - Effects
		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("Effects", ImVec2(0, 0), false)) {
			for (auto& [name, effect] : effects) {
				bool isCompiled = effect->IsCompiled();
				const auto& errors = effect->GetErrors();

				if (ImGui::TreeNodeEx(effect->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
					if (isCompiled) {
						effect->RenderImGui();
					} else {
						for (const auto& error : errors) {
							ImGui::BulletText("%s", error.c_str());
						}
					}
					ImGui::TreePop();
				}
			}
		}
		ImGui::EndChild();
			
		ImGui::EndTable();
	}
}

void EffectManager::CreateCommonResources()
{
	CreateQuadGeometry();
	CreateRenderStates();
	CreateCopyShaders();
	CreateCommonTextures();

	// Initialize downsampler and create shared downsample chain
	Downsampler::GetSingleton().Initialize();

	auto state = globals::state;
	UINT screenWidth = static_cast<UINT>(state->screenSize.x);
	UINT screenHeight = static_cast<UINT>(state->screenSize.y);

	// Create shared downsample chain that goes down to 1x1
	sharedDownsampleChain = Downsampler::GetSingleton().CreateDownsampleChain(
		screenWidth, screenHeight, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void EffectManager::CreateQuadGeometry()
{
	// Create a fullscreen quad vertex buffer that all effects can share
	struct QuadVertex
	{
		float position[3];
		float texCoord[2];
	};

	QuadVertex vertices[] = {
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },  // Bottom left
		{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },   // Top left
		{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },   // Bottom right
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } }     // Top right
	};

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(vertices);
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = vertices;

	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, quadVertexBuffer.GetAddressOf()));

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
			inputLayout.GetAddressOf());
		if (FAILED(hr)) {
			logger::error("Failed to create shared input layout for ENB effects");
		}
	}
}

void EffectManager::CreateRenderStates()
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

	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rastDesc, rasterizerState.GetAddressOf()));

	// Blend state for standard rendering (no blending)
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, blendState.GetAddressOf()));
}

void EffectManager::CreateCopyShaders()
{
	// Compile pixel shader for texture copy
	const char* pixelShaderSource = R"(
		Texture2D sourceTexture : register(t0);

		struct PS_INPUT { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };

		float4 main(PS_INPUT input) : SV_TARGET {
			int2 pixelPos = int2(input.pos.xy);
			return sourceTexture.Load(int3(pixelPos, 0));
		}
	)";

	ComPtr<ID3DBlob> psBlob, errorBlob;
	auto hr = D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr, nullptr, nullptr,
		"main", "ps_4_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("Failed to compile copy pixel shader: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	hr = globals::d3d::device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, copyPixelShader.GetAddressOf());
	if (FAILED(hr)) {
		logger::error("Failed to create copy pixel shader");
		return;
	}

	logger::info("Created texture copy shaders successfully");
}

void EffectManager::CreateCommonTextures()
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
		Effect::Texture bloomTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, bloomTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(bloomTexture.texture.Get(), nullptr, bloomTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(bloomTexture.texture.Get(), nullptr, bloomTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureBloom", bloomTexture });
	}

	// Create TextureColorTemp
	{
		Effect::Texture textureColor{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureColor.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureColor.texture.Get(), nullptr, textureColor.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureColor.texture.Get(), nullptr, textureColor.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureColorTemp", textureColor });
	}

	// Create TextureLens
	{
		Effect::Texture lensTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, lensTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(lensTexture.texture.Get(), nullptr, lensTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(lensTexture.texture.Get(), nullptr, lensTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureLens", lensTexture });
	}

	// Create RenderTargetRGBA32 (R8G8B8A8 32 bit ldr format)
	{
		D3D11_TEXTURE2D_DESC rgba32Desc = texDesc;
		rgba32Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Effect::Texture rgba32Texture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&rgba32Desc, nullptr, rgba32Texture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(rgba32Texture.texture.Get(), nullptr, rgba32Texture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(rgba32Texture.texture.Get(), nullptr, rgba32Texture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetRGBA32", rgba32Texture });
	}

	// Create RenderTargetRGBA64 (R16B16G16A16 64 bit ldr format)
	{
		D3D11_TEXTURE2D_DESC rgba64Desc = texDesc;
		rgba64Desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
		Effect::Texture rgba64Texture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&rgba64Desc, nullptr, rgba64Texture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(rgba64Texture.texture.Get(), nullptr, rgba64Texture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(rgba64Texture.texture.Get(), nullptr, rgba64Texture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetRGBA64", rgba64Texture });
	}

	// Create RenderTargetRGBA64F (R16B16G16A16F 64 bit hdr format)
	{
		D3D11_TEXTURE2D_DESC rgba64fDesc = texDesc;
		rgba64fDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		Effect::Texture rgba64fTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&rgba64fDesc, nullptr, rgba64fTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(rgba64fTexture.texture.Get(), nullptr, rgba64fTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(rgba64fTexture.texture.Get(), nullptr, rgba64fTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetRGBA64F", rgba64fTexture });
	}

	// Create RenderTargetR16F (R16F 16 bit hdr format with red channel only)
	{
		D3D11_TEXTURE2D_DESC r16fDesc = texDesc;
		r16fDesc.Format = DXGI_FORMAT_R16_FLOAT;
		Effect::Texture r16fTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&r16fDesc, nullptr, r16fTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(r16fTexture.texture.Get(), nullptr, r16fTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(r16fTexture.texture.Get(), nullptr, r16fTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetR16F", r16fTexture });
	}

	// Create RenderTargetR32F (R32F 32 bit hdr format with red channel only)
	{
		D3D11_TEXTURE2D_DESC r32fDesc = texDesc;
		r32fDesc.Format = DXGI_FORMAT_R32_FLOAT;
		Effect::Texture r32fTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&r32fDesc, nullptr, r32fTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(r32fTexture.texture.Get(), nullptr, r32fTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(r32fTexture.texture.Get(), nullptr, r32fTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetR32F", r32fTexture });
	}

	// Create RenderTargetRGB32F (32 bit hdr format without alpha)
	{
		D3D11_TEXTURE2D_DESC rgb32fDesc = texDesc;
		rgb32fDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		Effect::Texture rgb32fTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&rgb32fDesc, nullptr, rgb32fTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(rgb32fTexture.texture.Get(), nullptr, rgb32fTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(rgb32fTexture.texture.Get(), nullptr, rgb32fTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "RenderTargetRGB32F", rgb32fTexture });
	}

	// Create 1x1 textures for adaptation and aperture
	texDesc.Width = 1;
	texDesc.Height = 1;

	// Create TextureAdaptation
	{
		Effect::Texture adaptationTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, adaptationTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(adaptationTexture.texture.Get(), nullptr, adaptationTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureAdaptation", adaptationTexture });
	}

	// Create TextureAperture
	{
		Effect::Texture apertureTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, apertureTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(apertureTexture.texture.Get(), nullptr, apertureTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(apertureTexture.texture.Get(), nullptr, apertureTexture.srv.GetAddressOf()));
		commonTextureCache.insert({ "TextureAperture", apertureTexture });
	}

	logger::info("Created shared common textures: TextureBloom, TextureLens, RenderTargetRGBA32, RenderTargetRGBA64, RenderTargetRGBA64F, RenderTargetR16F, RenderTargetR32F, RenderTargetRGB32F, TextureAdaptation, TextureAperture");
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

void EffectManager::UpdateCommonVariablesForEffect(ID3DX11Effect* effect)
{
	if (!effect)
		return;

	auto renderer = globals::game::renderer;

	// Get variable pointers and set common textures
	auto textureDepth = effect->GetVariableByName("TextureDepth")->AsShaderResource();

	auto renderTargetRGBA32 = effect->GetVariableByName("RenderTargetRGBA32")->AsShaderResource();
	auto renderTargetRGBA64 = effect->GetVariableByName("RenderTargetRGBA64")->AsShaderResource();
	auto renderTargetRGBA64F = effect->GetVariableByName("RenderTargetRGBA64F")->AsShaderResource();
	auto renderTargetR16F = effect->GetVariableByName("RenderTargetR16F")->AsShaderResource();
	auto renderTargetR32F = effect->GetVariableByName("RenderTargetR32F")->AsShaderResource();
	auto renderTargetRGB32F = effect->GetVariableByName("RenderTargetRGB32F")->AsShaderResource();

	auto timer = effect->GetVariableByName("Timer")->AsVector();
	auto screenSize = effect->GetVariableByName("ScreenSize")->AsVector();
	auto weather = effect->GetVariableByName("Weather")->AsVector();
	auto timeOfDay1 = effect->GetVariableByName("TimeOfDay1")->AsVector();
	auto timeOfDay2 = effect->GetVariableByName("TimeOfDay2")->AsVector();
	auto eNightDayFactor = effect->GetVariableByName("ENightDayFactor")->AsVector();
	auto eInteriorFactor = effect->GetVariableByName("EInteriorFactor")->AsVector();

	if (textureDepth && textureDepth->IsValid()) {
		textureDepth->SetResource(renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV);
	}

	if (renderTargetRGBA32 && renderTargetRGBA32->IsValid()) {
		renderTargetRGBA32->SetResource(commonTextureCache["RenderTargetRGBA32"].srv.Get());
	}
	if (renderTargetRGBA64 && renderTargetRGBA64->IsValid()) {
		renderTargetRGBA64->SetResource(commonTextureCache["RenderTargetRGBA64"].srv.Get());
	}
	if (renderTargetRGBA64F && renderTargetRGBA64F->IsValid()) {
		renderTargetRGBA64F->SetResource(commonTextureCache["RenderTargetRGBA64F"].srv.Get());
	}
	if (renderTargetR16F && renderTargetR16F->IsValid()) {
		renderTargetR16F->SetResource(commonTextureCache["RenderTargetR16F"].srv.Get());
	}
	if (renderTargetR32F && renderTargetR32F->IsValid()) {
		renderTargetR32F->SetResource(commonTextureCache["RenderTargetR32F"].srv.Get());
	}
	if (renderTargetRGB32F && renderTargetRGB32F->IsValid()) {
		renderTargetRGB32F->SetResource(commonTextureCache["RenderTargetRGB32F"].srv.Get());
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

void EffectManager::CopyTexture(ID3D11ShaderResourceView* a_source, ID3D11RenderTargetView* a_dest)
{
	if (!a_source || !a_dest || !copyPixelShader) {
		logger::critical("Invalid parameters or shaders not initialized for texture copy");
		return;
	}

	auto context = globals::d3d::context;

	// Set up for copy operation
	context->OMSetRenderTargets(1, &a_dest, nullptr);
	context->OMSetDepthStencilState(nullptr, 0);

	// Set shaders
	context->PSSetShader(copyPixelShader.Get(), nullptr, 0);

	// Set source texture
	context->PSSetShaderResources(0, 1, &a_source);

	// Draw fullscreen quad
	context->Draw(4, 0);
}

void EffectManager::LoadENBSettings()
{
	// Initialize with default values (already set in struct)
	logger::info("Loaded ENB settings");
}

void EffectManager::SaveENBSettings()
{
	// TODO: Save to file/registry if needed
	logger::debug("Saved ENB settings");
}

void EffectManager::RenderTimeOfDaySettings(const std::string& prefix, TimeOfDaySettings& settings)
{
	const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };
	
	for (const auto& timeOfDay : timeOfDayNames) {
		std::string label = prefix + timeOfDay;
		ImGui::DragFloat(label.c_str(), &settings[timeOfDay]);
	}
}
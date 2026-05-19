#include "MotionBlur.h"
#include "Features/Upscaling.h"
#include "ShaderCache.h"
#include "Util.h"

#pragma warning(disable: 4324)

// Define serialization for settings
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MotionBlur::Settings,
	VelocityScale,
	SampleCount,
	ScalePreset)

void MotionBlur::SetupResources()
{
	auto device = globals::d3d::device;

	// Create samplers
	D3D11_SAMPLER_DESC samplerDesc = {
		.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.MinLOD = 0,
		.MaxLOD = D3D11_FLOAT32_MAX
	};

	device->CreateSamplerState(&samplerDesc, linearSampler.put());

	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	device->CreateSamplerState(&samplerDesc, pointSampler.put());

	// Compile shaders
	CompileComputeShaders();

	// Initialize constant buffer structs
	motionBlurCB = {
		.VelocityParams = { GetScaleValueFromPreset(settings.ScalePreset), 1.0f, 1.0f, 0.0f },
		.SampleCount = (settings.SampleCount * 2) & ~1  // Double and ensure it's always even
	};

	reductionPassCB = {
		.VelocityParams = { GetScaleValueFromPreset(settings.ScalePreset), 1.0f, 1.0f, 0.0f }
	};

	// Create the actual D3D constant buffers
	try {
		// Create constant buffers using the ConstantBuffer helper class
		blurConstantBufferObj = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<MotionBlurConstantBuffer>());
		reductionPassConstantBufferObj = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<ReductionPassConstantBuffer>());

		// Initial update
		blurConstantBufferObj->Update(motionBlurCB);
		reductionPassConstantBufferObj->Update(reductionPassCB);

		// Cache the initial values
		lastMotionBlurCB = motionBlurCB;
		lastReductionPassCB = reductionPassCB;
	} catch (const std::exception& e) {
		logger::error("Motion blur error initializing constant buffers: {}", e.what());
	}

	logger::info("Motion blur resources initialized");
}

void MotionBlur::CompileComputeShaders()
{
	// Clear existing shaders
	horizontalPassShader = nullptr;
	verticalPassShader = nullptr;
	neighborMaxPassShader = nullptr;
	blurPassShader = nullptr;

	struct ShaderInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* shader;
		const char* filename;
	};

	ShaderInfo shaders[] = {
		{ &horizontalPassShader, "motionblur_horizontalpass.cs.hlsl" },
		{ &verticalPassShader, "motionblur_verticalpass.cs.hlsl" },
		{ &neighborMaxPassShader, "motionblur_neighborpass.cs.hlsl" },
		{ &blurPassShader, "motionblur_blurpass.cs.hlsl" }
	};

	// Compile each shader
	for (const auto& info : shaders) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\MotionBlur") / info.filename;

		try {
			auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(
				Util::CompileShader(path.c_str(), {}, "cs_5_0", "main"));

			if (rawPtr) {
				info.shader->attach(rawPtr);
				logger::info("Compiled shader: {}", info.filename);
			} else {
				logger::error("Failed to compile shader: {}", info.filename);
			}
		} catch (const std::exception& e) {
			logger::error("Failed to compile {}: {}", info.filename, e.what());
		}
	}

	if (!horizontalPassShader || !verticalPassShader || !neighborMaxPassShader || !blurPassShader) {
		logger::error("One or more motion blur shaders failed to compile");
	}
}

void MotionBlur::ClearShaderCache()
{
	// Release resources
	horizontalPassShader = nullptr;
	verticalPassShader = nullptr;
	neighborMaxPassShader = nullptr;
	blurPassShader = nullptr;

	horizontalPassTexture = nullptr;
	verticalPassTexture = nullptr;
	neighborMaxTexture = nullptr;
	blurOutputTexture = nullptr;

	// Release constant buffer objects
	blurConstantBufferObj = nullptr;
	reductionPassConstantBufferObj = nullptr;

	lastWidth = lastHeight = 0;
}

void MotionBlur::RestoreDefaultSettings()
{
	// Reset to defaults
	settings = Settings{};
	settings.VelocityScale = 300.0f;
	settings.SampleCount = 8;  // 8 base samples = 16 actual samples
	settings.ScalePreset = MotionScale::Medium;
}

void MotionBlur::LoadSettings(json& j)
{
	try {
		settings = j;

		// Enforce valid ranges
		settings.VelocityScale = std::clamp(settings.VelocityScale, 10.0f, 800.0f);
		settings.SampleCount = std::clamp(settings.SampleCount, 8, 16);

		// Ensure valid enum value
		int scalePreset = static_cast<int>(settings.ScalePreset);
		if (scalePreset < 0 || scalePreset > 4) {
			settings.ScalePreset = MotionScale::Medium;
		}
	} catch (json::exception&) {
		RestoreDefaultSettings();
	}
}

void MotionBlur::SaveSettings(json& j)
{
	j = settings;
}

void MotionBlur::DrawSettings()
{
	ImGui::Text("Motion Blur Settings");

	// Motion scale presets
	const char* presets[] = {
		"Very Short", "Short", "Medium", "Long", "Very Long"
	};

	int preset = static_cast<int>(settings.ScalePreset);
	if (ImGui::Combo("Motion Length", &preset, presets, IM_ARRAYSIZE(presets))) {
		settings.ScalePreset = static_cast<MotionScale>(preset);
	}

	// Samples (each UI sample represents 2 actual samples)
	ImGui::SliderInt("Samples", &settings.SampleCount, 8, 16, "%d");
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text("Sample count is doubled internally for smoother results.\nMore samples = better quality but slower performance");
		ImGui::EndTooltip();
	}
}

void MotionBlur::Draw(TextureInfo& inout_tex)
{
	// Skip if disabled
	if (!enabled)
		return;

	try {
		auto renderer = globals::game::renderer;
		if (!renderer) {
			logger::error("Motion blur error: Renderer is null");
			return;
		}

		// First validate that motion vector and post-upscale depth resources exist and are valid
		auto& motionVectorTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;

		// Check that the required resources are valid
		if (!motionVectorTex.texture || !motionVectorTex.SRV) {
			logger::error("Motion blur error: Motion vector texture is invalid");
			return;
		}

		if (!depthSRV) {
			logger::error("Motion blur error: Depth texture is invalid");
			return;
		}

		// Check that the input texture is valid
		if (!inout_tex.tex || !inout_tex.srv) {
			logger::error("Motion blur error: Input texture is invalid");
			return;
		}

		// Check for resize and update resources if needed
		CheckAndResizeResources(inout_tex);

		// Compute dynamic resolution dimensions for dispatch (without overwriting lastWidth/lastHeight
		// which are used for resource size tracking)
		dynamicWidth = lastWidth;
		dynamicHeight = lastHeight;
		if (dynamicWidth > 0 && dynamicHeight > 0) {
			float2 res = { (float)dynamicWidth, (float)dynamicHeight };
			res = Util::ConvertToDynamic(res);
			dynamicWidth = (uint32_t)res.x;
			dynamicHeight = (uint32_t)res.y;
		}

		// Update constant buffers
		UpdateConstantBuffers();

		// Execute passes
		ExecuteVerticalPass();
		ExecuteNeighborMaxPass();
		ExecuteBlurPass(inout_tex);
	} catch (const std::exception& e) {
		logger::error("Motion blur error: {}", e.what());
	} catch (...) {
		logger::error("Motion blur error: Unknown exception occurred");
	}
}

bool MotionBlur::CheckAndResizeResources(const TextureInfo& inout_tex)
{
	if (!inout_tex.tex) {
		logger::error("Motion blur error: Input texture is null in CheckAndResizeResources");
		return false;
	}

	try {
		// Get dimensions
		D3D11_TEXTURE2D_DESC texDesc;
		inout_tex.tex->GetDesc(&texDesc);
		uint32_t width = texDesc.Width;
		uint32_t height = texDesc.Height;

		// Check if dimensions changed
		if (width == lastWidth && height == lastHeight && verticalPassTexture) {
			return false;
		}

		// Update tracking
		lastWidth = width;
		lastHeight = height;

		auto device = globals::d3d::device;
		if (!device) {
			logger::error("Motion blur error: D3D device is null in CheckAndResizeResources");
			return false;
		}

		// Fixed grid dimensions
		uint32_t gridWidth = FixedGridSize;
		uint32_t gridHeight = FixedGridSize;
		uint32_t horizontalPassHeight = height;

		// Create texture descriptor
		D3D11_TEXTURE2D_DESC gridDesc = {
			.Width = gridWidth,
			.Height = gridHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		// Create horizontal pass texture descriptor
		D3D11_TEXTURE2D_DESC horizontalDesc = gridDesc;
		horizontalDesc.Height = horizontalPassHeight;

		// Release previous resources
		verticalPassTexture = nullptr;
		neighborMaxTexture = nullptr;
		blurOutputTexture = nullptr;
		horizontalPassTexture = nullptr;

		// Create textures with error handling
		try {
			// For grid-based textures (using R16G16B16A16_FLOAT format)
			D3D11_SHADER_RESOURCE_VIEW_DESC gridSrvDesc = {
				.Format = gridDesc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC gridUavDesc = {
				.Format = gridDesc.Format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			horizontalPassTexture = eastl::make_unique<Texture2D>(horizontalDesc);
			horizontalPassTexture->CreateSRV(gridSrvDesc);
			horizontalPassTexture->CreateUAV(gridUavDesc);

			verticalPassTexture = eastl::make_unique<Texture2D>(gridDesc);
			verticalPassTexture->CreateSRV(gridSrvDesc);
			verticalPassTexture->CreateUAV(gridUavDesc);

			neighborMaxTexture = eastl::make_unique<Texture2D>(gridDesc);
			neighborMaxTexture->CreateSRV(gridSrvDesc);
			neighborMaxTexture->CreateUAV(gridUavDesc);

			// Create full-resolution output texture with format matching the input texture
			D3D11_TEXTURE2D_DESC blurDesc = texDesc;
			blurDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			// Output texture views need to match its format
			D3D11_SHADER_RESOURCE_VIEW_DESC blurSrvDesc = {
				.Format = blurDesc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC blurUavDesc = {
				.Format = blurDesc.Format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			blurOutputTexture = eastl::make_unique<Texture2D>(blurDesc);
			blurOutputTexture->CreateSRV(blurSrvDesc);
			blurOutputTexture->CreateUAV(blurUavDesc);
		} catch (const std::exception& e) {
			logger::error("Motion blur error creating textures: {}", e.what());
			return false;
		}

		// Validate that all textures were created successfully
		if (!horizontalPassTexture || !verticalPassTexture || !neighborMaxTexture || !blurOutputTexture) {
			logger::error("Motion blur error: Failed to create one or more required textures");
			return false;
		}

		return true;
	} catch (const std::exception& e) {
		logger::error("Motion blur resource resize error: {}", e.what());
		return false;
	} catch (...) {
		logger::error("Motion blur resource resize error: Unknown exception occurred");
		return false;
	}
}

bool MotionBlur::UpdateConstantBuffers()
{
	auto context = globals::d3d::context;
	if (!context) {
		logger::error("Motion blur error: D3D context is null in UpdateConstantBuffers");
		return false;
	}

	if (!blurConstantBufferObj || !reductionPassConstantBufferObj) {
		logger::error("Motion blur error: Constant buffers are invalid");
		return false;
	}

	bool updated = false;

	// Get actual velocity scale value from preset
	float velocityScale = GetScaleValueFromPreset(settings.ScalePreset);
	float2 velocityTextureScale = { 1.0f, 1.0f };
	float2 targetResolution = { static_cast<float>(lastWidth), static_cast<float>(lastHeight) };

	auto& upscaling = globals::features::upscaling;
	if (upscaling.loaded && upscaling.IsUpscalingActive()) {
		velocityTextureScale.x = std::clamp(upscaling.resolutionScale.x, 0.0f, 1.0f);
		velocityTextureScale.y = std::clamp(upscaling.resolutionScale.y, 0.0f, 1.0f);
	}

	// Set current values
	motionBlurCB = {
		.VelocityParams = { velocityScale, velocityTextureScale.x, velocityTextureScale.y, 0.0f },
		.SampleCount = (settings.SampleCount * 2) & ~1  // Double and ensure it's always even
	};

	reductionPassCB = {
		.VelocityParams = { velocityScale, velocityTextureScale.x, velocityTextureScale.y, 0.0f },
		.TargetResolution = { targetResolution.x, targetResolution.y, 0.0f, 0.0f }
	};

	// Update blur constant buffer if needed
	if (memcmp(&motionBlurCB, &lastMotionBlurCB, sizeof(MotionBlurConstantBuffer)) != 0) {
		try {
			blurConstantBufferObj->Update(motionBlurCB);
			lastMotionBlurCB = motionBlurCB;
			updated = true;
		} catch (const std::exception& e) {
			logger::error("Motion blur error updating blur constant buffer: {}", e.what());
			return false;
		}
	}

	// Update reduction pass constant buffer if needed
	if (memcmp(&reductionPassCB, &lastReductionPassCB, sizeof(ReductionPassConstantBuffer)) != 0) {
		try {
			reductionPassConstantBufferObj->Update(reductionPassCB);
			lastReductionPassCB = reductionPassCB;
			updated = true;
		} catch (const std::exception& e) {
			logger::error("Motion blur error updating reduction constant buffer: {}", e.what());
			return false;
		}
	}

	return updated;
}

void MotionBlur::SetupComputePass(
	ID3D11ComputeShader* shader,
	ID3D11ShaderResourceView** srvs,
	uint32_t srvCount,
	ID3D11UnorderedAccessView* uav,
	ID3D11Buffer* constantBuffer)
{
	auto context = globals::d3d::context;
	context->CSSetShader(shader, nullptr, 0);
	context->CSSetShaderResources(0, srvCount, srvs);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetConstantBuffers(0, 1, &constantBuffer);
}

void MotionBlur::ClearComputeResources(uint32_t srvCount)
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };

	context->CSSetShaderResources(0, srvCount, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, nullCB);
}

void MotionBlur::ExecuteVerticalPass()
{
	auto context = globals::d3d::context;

	// First do horizontal reduction
	ExecuteHorizontalPass();

	if (!verticalPassTexture || !verticalPassTexture->uav || !horizontalPassTexture || !horizontalPassTexture->srv || !verticalPassShader || !reductionPassConstantBufferObj)
		return;

	// Setup vertical pass with horizontal pass texture as input
	ID3D11ShaderResourceView* horizontalSRV = horizontalPassTexture->srv.get();
	ID3D11Buffer* reductionCB = reductionPassConstantBufferObj->CB();
	SetupComputePass(verticalPassShader.get(), &horizontalSRV, 1, verticalPassTexture->uav.get(), reductionCB);

	// Dispatch vertical pass: output is [GRID_SIZE × GRID_SIZE], so dispatch covers grid dimensions only
	uint32_t dispatchX = (FixedGridSize + 7) / 8;
	uint32_t dispatchY = (FixedGridSize + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ClearComputeResources(1);
}

void MotionBlur::ExecuteHorizontalPass()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	if (!context || !renderer || !horizontalPassShader || !reductionPassConstantBufferObj)
		return;

	// Get motion vectors from engine
	auto& motionVectorTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	if (!motionVectorTex.texture || !motionVectorTex.SRV || !horizontalPassTexture || !horizontalPassTexture->uav)
		return;

	ID3D11ShaderResourceView* velocitySRV = motionVectorTex.SRV;

	// Setup horizontal pass
	ID3D11Buffer* reductionCB = reductionPassConstantBufferObj->CB();
	SetupComputePass(horizontalPassShader.get(), &velocitySRV, 1, horizontalPassTexture->uav.get(), reductionCB);

	// Dispatch horizontal pass: output is [GRID_SIZE × height], so dispatch covers grid width and full height
	uint32_t dispatchX = (FixedGridSize + 7) / 8;
	uint32_t dispatchY = (dynamicHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ClearComputeResources(1);
}

void MotionBlur::ExecuteNeighborMaxPass()
{
	auto context = globals::d3d::context;

	if (!context || !verticalPassTexture || !verticalPassTexture->srv || !neighborMaxTexture || !neighborMaxTexture->uav || !neighborMaxPassShader || !reductionPassConstantBufferObj)
		return;

	// Setup neighbor pass
	ID3D11ShaderResourceView* verticalPassSRV = verticalPassTexture->srv.get();
	ID3D11Buffer* reductionCB = reductionPassConstantBufferObj->CB();
	SetupComputePass(neighborMaxPassShader.get(), &verticalPassSRV, 1, neighborMaxTexture->uav.get(), reductionCB);

	// Dispatch neighbor pass: operates on [GRID_SIZE × GRID_SIZE] grid
	uint32_t dispatchX = (FixedGridSize + 7) / 8;
	uint32_t dispatchY = (FixedGridSize + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ClearComputeResources(1);
}

void MotionBlur::ExecuteBlurPass(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	if (!context || !renderer || !blurPassShader || !blurConstantBufferObj)
		return;

	// Get engine resources
	auto& motionVectorTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;

	if (!motionVectorTex.SRV || !depthSRV || !neighborMaxTexture || !neighborMaxTexture->srv || !blurOutputTexture || !blurOutputTexture->uav)
		return;

	ID3D11ShaderResourceView* velocitySRV = motionVectorTex.SRV;

	// Set samplers
	if (!linearSampler || !pointSampler)
		return;

	ID3D11SamplerState* samplers[] = { linearSampler.get(), pointSampler.get() };
	context->CSSetSamplers(0, 2, samplers);

	// Setup blur pass
	ID3D11ShaderResourceView* srvs[] = { inout_tex.srv, velocitySRV, neighborMaxTexture->srv.get(), depthSRV };
	ID3D11Buffer* blurCB = blurConstantBufferObj->CB();

	SetupComputePass(blurPassShader.get(), srvs, 4, blurOutputTexture->uav.get(), blurCB);

	// Dispatch blur pass at dynamic resolution (full-screen blur)
	uint32_t dispatchX = (dynamicWidth + 7) / 8;
	uint32_t dispatchY = (dynamicHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	// Cleanup
	ClearComputeResources(4);
	ID3D11SamplerState* nullSamplers[2] = { nullptr, nullptr };
	context->CSSetSamplers(0, 2, nullSamplers);
	context->CSSetShader(nullptr, nullptr, 0);

	// Set output
	if (blurOutputTexture && blurOutputTexture->resource && blurOutputTexture->srv) {
		inout_tex = { blurOutputTexture->resource.get(), blurOutputTexture->srv.get() };
	}
}

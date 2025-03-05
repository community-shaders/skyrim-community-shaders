#include "MotionBlur.h"
#include "ShaderCache.h"

#pragma warning(disable: 4324)

// Define serialization for settings
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MotionBlur::Settings,
    TileSize,
	VelocityScale,
    ShowNeighborMax,
    ApplyBlur,
	BlurScale,
	SampleCount,
	VisualizationMode,
	CameraMotionReduction)

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
    
	// Create linear and point samplers
	device->CreateSamplerState(&samplerDesc, linearSampler.put());

    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	device->CreateSamplerState(&samplerDesc, pointSampler.put());

	// Compile shaders
    CompileComputeShaders();
    
	// Initialize constant buffers - actual creation happens in CheckAndResizeResources
	// when we know the viewport dimensions
	motionBlurCB = {
		.TileSize = static_cast<uint32_t>(settings.TileSize),
		.VelocityScale = settings.VelocityScale,
		.BlurScale = settings.BlurScale,
		.SampleCount = settings.SampleCount,
		.VisualizationMode = settings.VisualizationMode,
		.CameraMotionReduction = settings.CameraMotionReduction,
		.PaddingX = 0.0f,
		.PaddingY = 0.0f
	};

	tilePassCB = {
		.TileSize = static_cast<uint32_t>(settings.TileSize),
		.VelocityScale = settings.VelocityScale
	};

	// Resource textures will be created on demand in the Draw method
    logger::info("Motion blur resources initialized");
}

void MotionBlur::CompileComputeShaders()
{
	// Clear existing shaders
	tileMaxPassShader = nullptr;
	neighborMaxPassShader = nullptr;
	blurPassShader = nullptr;

	// Define shader compilation information
	struct ShaderInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* shader;
		const char* filename;
	};

	ShaderInfo shaders[] = {
		{ &tileMaxPassShader, "motionblur_tilemaxpass.cs.hlsl" },       // First pass - tile max velocities
		{ &neighborMaxPassShader, "motionblur_neighborpass.cs.hlsl" },  // Second pass - neighbor max
		{ &blurPassShader, "motionblur_blurpass.cs.hlsl" }              // Final pass - apply blur
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

	// Check if all required shaders were compiled
	if (!tileMaxPassShader || !neighborMaxPassShader || !blurPassShader) {
		logger::error("One or more motion blur shaders failed to compile");
    }
}

void MotionBlur::ClearShaderCache()
{
	// Release all shaders and textures
	tileMaxPassShader = nullptr;
	neighborMaxPassShader = nullptr;
	blurPassShader = nullptr;

	// Reset render resources to force recreation
    tileMaxTexture = nullptr;
    neighborMaxTexture = nullptr;
    blurOutputTexture = nullptr;

	// Reset dimension tracking to force resource recreation on next use
	lastWidth = lastHeight = 0;
}

void MotionBlur::RestoreDefaultSettings()
{
	// Reset to defaults
    settings = Settings{};
    settings.TileSize = 32;
	settings.VelocityScale = 100.0f;
	settings.BlurScale = 1.0f;
	settings.SampleCount = 12;
}

void MotionBlur::LoadSettings(json& j)
{
	try {
    // Load settings from JSON
        settings = j;

		// Enforce valid ranges
		settings.TileSize = std::clamp(settings.TileSize, 8, 64);
		settings.VelocityScale = std::clamp(settings.VelocityScale, 10.0f, 200.0f);
		settings.BlurScale = std::clamp(settings.BlurScale, 0.1f, 3.0f);
		settings.SampleCount = std::clamp(settings.SampleCount, 4, 24);
		settings.VisualizationMode = std::clamp(settings.VisualizationMode, 0, 5);
		settings.CameraMotionReduction = std::clamp(settings.CameraMotionReduction, 0.0f, 1.0f);
	} catch (json::exception&) {
        RestoreDefaultSettings();
    }
}

void MotionBlur::SaveSettings(json& j)
{
	// Save to JSON
    j = settings;
}

void MotionBlur::DrawSettings()
{
	ImGui::Text("Motion Blur Settings");

	// Core settings
	ImGui::SliderFloat("Blur Strength", &settings.BlurScale, 0.0f, 2.0f);
	ImGui::SliderInt("Samples", &settings.SampleCount, 4, 16);
	ImGui::SliderFloat("Motion Scale", &settings.VelocityScale, 0.0f, 200.0f);
	ImGui::SliderFloat("Camera Reduction", &settings.CameraMotionReduction, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
		ImGui::Text("0.0 = Include camera motion (classic motion blur)\n1.0 = Object motion only (no camera/radial blur)");
                ImGui::EndTooltip();
            }
            
	// Debug visualization
	const char* vizModes[] = {
		"Normal Blur", "Direction Vectors", "Raw Motion",
		"Sample Weights", "Camera Motion", "Object-Only Motion"
	};
	int vizMode = settings.VisualizationMode;
	if (ImGui::Combo("Visualization", &vizMode, vizModes, IM_ARRAYSIZE(vizModes))) {
		settings.VisualizationMode = vizMode;
	}
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
		ImGui::Text(
			"Debug visualization modes:\n"
			"Normal Blur - Standard motion blur effect\n"
			"Direction Vectors - Visualize tile-based motion directions\n"
			"Raw Motion - Show raw per-pixel motion vectors\n"
			"Sample Weights - Display sample contribution weights\n"
			"Camera Motion - Show camera movement vectors only\n"
			"Object-Only Motion - Show object motion with camera motion removed");
                ImGui::EndTooltip();
            }
            
	// Advanced settings (collapsible)
	if (ImGui::CollapsingHeader("Advanced Settings")) {
		ImGui::SliderInt("Tile Size", &settings.TileSize, 8, 64);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
			ImGui::Text("Size of each tile for motion vector sampling.\nLarger values improve performance but may reduce quality.");
                ImGui::EndTooltip();
            }
            
		ImGui::Checkbox("Apply Blur", &settings.ApplyBlur);
		if (!settings.ApplyBlur) {
			ImGui::Checkbox("Show Neighbor Max", &settings.ShowNeighborMax);
		}
	}

	ImGui::TextDisabled("Performance Impact: Medium");
}

void MotionBlur::Draw(TextureInfo& inout_tex)
{
	// Skip processing if disabled
	if (!enabled)
		return;

	try {
		// Check if we need to resize resources based on the current render target
		CheckAndResizeResources(inout_tex);

		// Update constant buffers (only if values have changed)
		UpdateConstantBuffers();

		// Run TileMax pass (first pass)
		ExecuteTileMaxPass();

		// Run NeighborMax pass (second pass)
		ExecuteNeighborMaxPass();

		// Handle visualization-only mode
		if (!settings.ApplyBlur) {
			SetVisualizationOutput(inout_tex);
                return;
		}

		// Run final blur pass and update the output texture
		ExecuteBlurPass(inout_tex);
    } catch (const std::exception& e) {
		logger::error("Motion blur error: {}", e.what());
    }
}

bool MotionBlur::CheckAndResizeResources(const TextureInfo& inout_tex)
{
        // Get render target dimensions
        D3D11_TEXTURE2D_DESC texDesc;
        inout_tex.tex->GetDesc(&texDesc);
        uint32_t width = texDesc.Width;
        uint32_t height = texDesc.Height;
        
	// Check if dimensions have changed since last time
	if (width == lastWidth && height == lastHeight && tileMaxTexture) {
		// No change, no need to recreate resources
		return false;
	}

	// Update tracked dimensions
	lastWidth = width;
	lastHeight = height;

	auto device = globals::d3d::device;

	// Calculate downsampled dimensions
	uint32_t tileWidth = (width + settings.TileSize - 1) / settings.TileSize;
	uint32_t tileHeight = (height + settings.TileSize - 1) / settings.TileSize;

	// Create downsampled textures for tile processing
	D3D11_TEXTURE2D_DESC tileDesc = {
		.Width = tileWidth,
		.Height = tileHeight,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.SampleDesc = { 1, 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = 0
	};

	// Create view descriptors
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = tileDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = tileDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	// Release previous resources if they exist
	tileMaxTexture = nullptr;
	neighborMaxTexture = nullptr;
	blurOutputTexture = nullptr;

	// Create tile max texture
	tileMaxTexture = eastl::make_unique<Texture2D>(tileDesc);
	tileMaxTexture->CreateSRV(srvDesc);
	tileMaxTexture->CreateUAV(uavDesc);

	// Create neighbor max texture
	neighborMaxTexture = eastl::make_unique<Texture2D>(tileDesc);
	neighborMaxTexture->CreateSRV(srvDesc);
	neighborMaxTexture->CreateUAV(uavDesc);

	// Create blur output texture (full resolution)
	D3D11_TEXTURE2D_DESC blurDesc = texDesc;
	blurDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	blurOutputTexture = eastl::make_unique<Texture2D>(blurDesc);
	blurOutputTexture->CreateSRV(srvDesc);
	blurOutputTexture->CreateUAV(uavDesc);

	// Create constant buffers if needed
	if (!blurConstantBuffer) {
		D3D11_BUFFER_DESC cbDesc = {
			.ByteWidth = sizeof(MotionBlurConstantBuffer),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
		};
		device->CreateBuffer(&cbDesc, nullptr, blurConstantBuffer.put());
	}

	if (!tilePassConstantBuffer) {
		D3D11_BUFFER_DESC tileCbDesc = {
			.ByteWidth = sizeof(TilePassConstantBuffer),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
		};
		device->CreateBuffer(&tileCbDesc, nullptr, tilePassConstantBuffer.put());
	}

	return true;
}

bool MotionBlur::UpdateConstantBuffers()
{
	auto context = globals::d3d::context;
	bool updated = false;

	// Update main constant buffer values
	motionBlurCB = {
		.TileSize = static_cast<uint32_t>(settings.TileSize),
		.VelocityScale = settings.VelocityScale,
		.BlurScale = settings.BlurScale,
		.SampleCount = settings.SampleCount,
		.VisualizationMode = settings.VisualizationMode,
		.CameraMotionReduction = settings.CameraMotionReduction,
		.PaddingX = 0.0f,
		.PaddingY = 0.0f
	};

	// Update tile pass constant buffer values
	tilePassCB = {
		.TileSize = static_cast<uint32_t>(settings.TileSize),
		.VelocityScale = settings.VelocityScale
	};

	// Only update the blur constant buffer if values have changed
	if (memcmp(&motionBlurCB, &lastMotionBlurCB, sizeof(MotionBlurConstantBuffer)) != 0) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(blurConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, &motionBlurCB, sizeof(MotionBlurConstantBuffer));
		context->Unmap(blurConstantBuffer.get(), 0);

		// Update the cache
		lastMotionBlurCB = motionBlurCB;
		updated = true;
	}

	// Only update the tile pass constant buffer if values have changed
	if (memcmp(&tilePassCB, &lastTilePassCB, sizeof(TilePassConstantBuffer)) != 0) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(tilePassConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, &tilePassCB, sizeof(TilePassConstantBuffer));
		context->Unmap(tilePassConstantBuffer.get(), 0);

		// Update the cache
		lastTilePassCB = tilePassCB;
		updated = true;
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

	// Create arrays of nullptrs
	ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };

	// Clear resources
	context->CSSetShaderResources(0, srvCount, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, nullCB);
}

void MotionBlur::ExecuteTileMaxPass()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	// Get motion vector texture from the engine
        auto motionVectorTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
        ID3D11ShaderResourceView* velocitySRV = motionVectorTex.SRV;
        
	// Set up compute shader for tile max pass
	ID3D11Buffer* tileCB = tilePassConstantBuffer.get();
	SetupComputePass(tileMaxPassShader.get(), &velocitySRV, 1, tileMaxTexture->uav.get(), tileCB);

	// Dispatch the compute shader (8x8 thread groups)
	uint32_t width = lastWidth;
	uint32_t height = lastHeight;
	uint32_t dispatchX = (width + 7) / 8;
	uint32_t dispatchY = (height + 7) / 8;
        context->Dispatch(dispatchX, dispatchY, 1);
        
	// Clean up resources
	ClearComputeResources(1);
}

void MotionBlur::ExecuteNeighborMaxPass()
{
	auto context = globals::d3d::context;

	// Set up compute shader for neighbor max pass
	ID3D11ShaderResourceView* tileMaxSRVPtr = tileMaxTexture->srv.get();
	ID3D11Buffer* tileCB = tilePassConstantBuffer.get();
	SetupComputePass(neighborMaxPassShader.get(), &tileMaxSRVPtr, 1, neighborMaxTexture->uav.get(), tileCB);

	// Dispatch the compute shader (same dimensions as tile max pass)
	uint32_t width = lastWidth;
	uint32_t height = lastHeight;
	uint32_t dispatchX = (width + 7) / 8;
	uint32_t dispatchY = (height + 7) / 8;
        context->Dispatch(dispatchX, dispatchY, 1);
        
	// Clean up resources
	ClearComputeResources(1);
}

void MotionBlur::SetVisualizationOutput(TextureInfo& inout_tex)
{
	// Set the output texture based on whether to show neighbor max or tile max
	inout_tex = settings.ShowNeighborMax ?
	                TextureInfo{ neighborMaxTexture->resource.get(), neighborMaxTexture->srv.get() } :
	                TextureInfo{ tileMaxTexture->resource.get(), tileMaxTexture->srv.get() };
}

void MotionBlur::ExecuteBlurPass(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	// Get the required resources from the engine
	auto motionVectorTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depthData = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	ID3D11ShaderResourceView* velocitySRV = motionVectorTex.SRV;
	ID3D11ShaderResourceView* depthSRV = depthData.depthSRV;
        
        // Set samplers
        ID3D11SamplerState* samplers[] = { linearSampler.get(), pointSampler.get() };
        context->CSSetSamplers(0, 2, samplers);
        
	// Set shader resources
	ID3D11ShaderResourceView* srvs[] = { inout_tex.srv, velocitySRV, neighborMaxTexture->srv.get(), depthSRV };
	ID3D11Buffer* blurCB = blurConstantBuffer.get();

	// Set up compute shader for final blur pass
	SetupComputePass(blurPassShader.get(), srvs, 4, blurOutputTexture->uav.get(), blurCB);

	// Dispatch the compute shader
	uint32_t width = lastWidth;
	uint32_t height = lastHeight;
	uint32_t dispatchX = (width + 7) / 8;
	uint32_t dispatchY = (height + 7) / 8;
        context->Dispatch(dispatchX, dispatchY, 1);
        
	// Clean up resources
	ClearComputeResources(4);
        ID3D11SamplerState* nullSamplers[2] = { nullptr, nullptr };
        context->CSSetSamplers(0, 2, nullSamplers);
        context->CSSetShader(nullptr, nullptr, 0);
        
	// Set the output texture
	inout_tex = { blurOutputTexture->resource.get(), blurOutputTexture->srv.get() };
}
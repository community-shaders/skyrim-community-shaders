#include "NIS.h"

#include "../../../Deferred.h"
#include "../../../Hooks.h"
#include "../../../State.h"
#include "../../../Util.h"
#include "../../Upscaling.h"

void NIS::Initialize()
{
	logger::info("[Upscaling] Creating NIS resources");

	CreateCoefficientTextures();
	CreateComputeShader();

	// Create constant buffer for NIS configuration
	nisConfigCB = new ConstantBuffer(ConstantBufferDesc<NISConfig>());
}

void NIS::CreateCoefficientTextures()
{
	auto device = globals::d3d::device;

	// Create scaler coefficients texture
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = 8; // kFilterSize
		texDesc.Height = 64; // kPhaseCount
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_IMMUTABLE;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		texDesc.CPUAccessFlags = 0;

		// Convert coefficient data to the correct format
		std::vector<uint16_t> scalerData(8 * 64 * 4); // 4 components per texel

		// Use the pre-computed FP16 coefficients from NIS_Config.h
		for (size_t phase = 0; phase < 64; ++phase) {
			for (size_t coef = 0; coef < 8; ++coef) {
				size_t index = (phase * 8 + coef) * 4;
				// Use the FP16 coefficient data directly (kFilterSize is already 8)
				scalerData[index] = coef_scale_fp16[phase][coef];
				scalerData[index + 1] = 0;
				scalerData[index + 2] = 0;
				scalerData[index + 3] = 0;
			}
		}

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = scalerData.data();
		initData.SysMemPitch = 8 * sizeof(uint16_t) * 4;
		initData.SysMemSlicePitch = 0;

		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, &initData, nisCoefScalerTexture.put()));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		DX::ThrowIfFailed(device->CreateShaderResourceView(nisCoefScalerTexture.get(), &srvDesc, nisCoefScalerSRV.put()));
	}

	// Create USM coefficients texture
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = 8; // kFilterSize
		texDesc.Height = 64; // kPhaseCount
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_IMMUTABLE;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		texDesc.CPUAccessFlags = 0;

		// Convert coefficient data to the correct format
		std::vector<uint16_t> usmData(8 * 64 * 4); // 4 components per texel

		// Use the pre-computed FP16 coefficients from NIS_Config.h
		for (size_t phase = 0; phase < 64; ++phase) {
			for (size_t coef = 0; coef < 8; ++coef) {
				size_t index = (phase * 8 + coef) * 4;
				// Use the FP16 coefficient data directly (kFilterSize is already 8)
				usmData[index] = coef_usm_fp16[phase][coef];
				usmData[index + 1] = 0;
				usmData[index + 2] = 0;
				usmData[index + 3] = 0;
			}
		}

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = usmData.data();
		initData.SysMemPitch = 8 * sizeof(uint16_t) * 4;
		initData.SysMemSlicePitch = 0;

		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, &initData, nisCoefUsmTexture.put()));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		DX::ThrowIfFailed(device->CreateShaderResourceView(nisCoefUsmTexture.get(), &srvDesc, nisCoefUsmSRV.put()));
	}

	logger::debug("[Upscaling] Created coefficient textures");
}

void NIS::CreateComputeShader()
{
	logger::debug("[Upscaling] Compiling NIS compute shader");

	std::vector<std::pair<const char*, const char*>> defines = {
		{ "NIS_SCALER", "0" }, // Use sharpen-only mode
		{ "NIS_HLSL", "1" },
		{ "NIS_BLOCK_WIDTH", "32" },
		{ "NIS_BLOCK_HEIGHT", "32" },
		{ "NIS_THREAD_GROUP_SIZE", "256" }
	};

	nisComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\NIS\\NIS_Sharpen.hlsl", defines, "cs_5_0"));

	logger::debug("[Upscaling] NIS compute shader compiled successfully");
}

void NIS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("NIS Sharpening");

	uint32_t screenWidth = (uint32_t)state->screenSize.x;
	uint32_t screenHeight = (uint32_t)state->screenSize.y;

	NISConfig config;
	bool configSuccess = NVSharpenUpdateConfig(config, sharpness,
		0, 0,
		screenWidth, screenHeight,
		screenWidth, screenHeight,
		0, 0,
		NISHDRMode::None);

	if (!configSuccess) {
		logger::error("[Upscaling] Failed to configure NIS settings");
		return;
	}

	// Update constant buffer
	nisConfigCB->Update(config);
	auto bufferArray = nisConfigCB->CB();

	// Set compute shader and resources
	context->CSSetShader(nisComputeShader.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &bufferArray);

	// Set sampler
	auto deferred = globals::deferred;
	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->CSSetSamplers(0, 1, samplers);

	// Set SRVs
	ID3D11ShaderResourceView* srvs[] = {
		inputSRV,
		nisCoefScalerSRV.get(),
		nisCoefUsmSRV.get()
	};
	context->CSSetShaderResources(0, 3, srvs);

	// Set UAV
	ID3D11UnorderedAccessView* uavs[] = { outputUAV };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	// Dispatch compute shader
	uint32_t dispatchX = (screenWidth + 31) / 32;
	uint32_t dispatchY = (screenHeight + 31) / 32;
	context->Dispatch(dispatchX, dispatchY, 1);

	// Cleanup
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, 3, nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}
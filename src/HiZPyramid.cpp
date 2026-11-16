#include "HiZPyramid.h"

#include "Features/TerrainBlending.h"
#include "Profiler.h"
#include "State.h"

void HiZPyramid::SetupResources()
{
	paramsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BaseParams>(), "HiZPyramid::ParamsCB");
}

void HiZPyramid::ClearShaderCache()
{
	baseCS = nullptr;
	spdCS = nullptr;
	baseCSFailed = false;
	spdCSFailed = false;
	valid = false;
	builtFrame = UINT32_MAX;
}

ID3D11ShaderResourceView* HiZPyramid::GetSRV() const
{
	return IsValid() && texture ? texture->srv.get() : nullptr;
}

bool HiZPyramid::IsValid() const
{
	return valid && builtFrame == globals::state->frameCount;
}

ID3D11ShaderResourceView* HiZPyramid::GetSourceDepthSRV()
{
	// Bypass Terrain Blending's replacement SRV.
	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.settings.Enabled && terrainBlending.prepassSRVBackup)
		return terrainBlending.prepassSRVBackup;

	if (auto* renderer = globals::game::renderer)
		return renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;

	return nullptr;
}

ID3D11ShaderResourceView* HiZPyramid::GetLiveDepthSRV()
{
	// Terrain Blending retains the original kMAIN SRV.
	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.depthSRVBackup)
		return terrainBlending.depthSRVBackup;

	auto* renderer = globals::game::renderer;
	if (!renderer)
		return nullptr;

	return renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
}

bool HiZPyramid::CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH, uint32_t outputMipCount)
{
	uint32_t availableOutputMips = 0;
	for (uint32_t size = std::max(dstW, dstH); size > 1; size >>= 1)
		++availableOutputMips;
	outputMipCount = std::min(outputMipCount, availableOutputMips);

	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = dstW;
	textureDesc.Height = dstH;
	textureDesc.MipLevels = outputMipCount + 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

	try {
		auto newTexture = std::make_unique<Texture2D>(textureDesc, "HiZPyramid::Texture");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
		newTexture->CreateSRV(srvDesc);

		// Restrict SPD reads to mip 0 so they cannot overlap its output UAVs.
		winrt::com_ptr<ID3D11ShaderResourceView> newMip0SRV;
		srvDesc.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(newTexture->resource.get(), &srvDesc, newMip0SRV.put()));
		Util::SetResourceName(newMip0SRV.get(), "HiZPyramid::Texture Mip0 SRV");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		newTexture->CreateUAV(uavDesc);

		decltype(outputUAVs) newOutputUAVs(outputMipCount);
		for (uint32_t i = 0; i < outputMipCount; ++i) {
			uavDesc.Texture2D.MipSlice = i + 1;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(newTexture->resource.get(), &uavDesc, newOutputUAVs[i].put()));
			Util::SetResourceName(newOutputUAVs[i].get(), "HiZPyramid::Texture Mip%u UAV", i + 1);
		}

		// Replace resources only after every view was created.
		texture = std::move(newTexture);
		mip0SRV = std::move(newMip0SRV);
		outputUAVs = std::move(newOutputUAVs);

		logger::debug("[HiZ Pyramid] Created {}x{} texture with {} mip levels", dstW, dstH, textureDesc.MipLevels);
		return true;

	} catch (...) {
		logger::error("[HiZ Pyramid] Texture or view creation failed");
		return false;
	}
}

bool HiZPyramid::Build(ID3D11Device* device, ID3D11DeviceContext* ctx, bool forceRefresh)
{
	const uint32_t currentFrame = globals::state->frameCount;
	if (!forceRefresh && builtFrame == currentFrame)
		return valid;

	valid = false;

	if (!paramsCB || !globals::game::renderer)
		return false;

	const float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	// Depth uses the scaled viewport even while dynamic resolution is locked.
	const auto renderSize = Util::ConvertToDynamic(screenSize, true);

	const uint32_t srcW = std::max(1u, (uint32_t)std::lround(renderSize.x));
	const uint32_t srcH = std::max(1u, (uint32_t)std::lround(renderSize.y));

	const uint32_t validW = (srcW + kDownsampleFactor - 1) / kDownsampleFactor;
	const uint32_t validH = (srcH + kDownsampleFactor - 1) / kDownsampleFactor;

	usingLiveDepth = true;
	ID3D11ShaderResourceView* srcSRV = GetLiveDepthSRV();
	if (!srcSRV) {
		usingLiveDepth = false;
		srcSRV = GetSourceDepthSRV();
	}
	if (!srcSRV)
		return false;

	// Full-resolution allocation avoids resizing as dynamic resolution changes.
	const auto padToTile = [](uint32_t v) { return (v + kTileSize - 1) & ~(kTileSize - 1); };
	const uint32_t padW = padToTile(((uint32_t)screenSize.x + kDownsampleFactor - 1) / kDownsampleFactor);
	const uint32_t padH = padToTile(((uint32_t)screenSize.y + kDownsampleFactor - 1) / kDownsampleFactor);

	if (!padW || !padH)
		return false;
	if ((!texture || texture->desc.Width != padW || texture->desc.Height != padH) && !CreateTexture(device, padW, padH, kOutputMipCount))
		return false;

	width = validW;
	height = validH;
	// Preserve fractional tiles to keep UVs aligned with source depth.
	uvScale = float2{ (float)srcW / kDownsampleFactor, (float)srcH / kDownsampleFactor };

	if (!baseCS) {
		if (baseCSFailed)
			return false;

		baseCS.attach(static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\Common\\HiZ\\GrassHiZCS.hlsl", {}, "cs_5_0")));
		if (!baseCS) {
			baseCSFailed = true;
			logger::error("[HiZ Pyramid] HiZ CS load failed — occlusion culling disabled");
			return false;
		}
	}

	if (!spdCS && !spdCSFailed) {
		spdCS.attach(static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\Common\\HiZ\\SPD.hlsl", {}, "cs_5_0")));
		if (!spdCS) {
			spdCSFailed = true;
			logger::error("[HiZ Pyramid] SPD load failed — large instances will not be occlusion culled");
		}
	}

	paramsCB->Update(BaseParams{ srcW, srcH, padW, padH });

	ID3D11UnorderedAccessView* nullUAVs[kOutputMipCount]{};
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->CSSetUnorderedAccessViews(0, kOutputMipCount, nullUAVs, nullptr);

	globals::profiler->BeginPass("HiZPyramid::Base");
	// Detach kMAIN's DSV before sampling its depth.
	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	ID3D11DepthStencilView* dsv = nullptr;
	if (usingLiveDepth) {
		ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	ID3D11Buffer* cb = paramsCB->CB();
	ID3D11UnorderedAccessView* baseUAV = texture->uav.get();
	ctx->CSSetShader(baseCS.get(), nullptr, 0);
	ctx->CSSetConstantBuffers(0, 1, &cb);
	ctx->CSSetShaderResources(0, 1, &srcSRV);
	ctx->CSSetUnorderedAccessViews(0, 1, &baseUAV, nullptr);
	ctx->Dispatch((padW + 7) / 8, (padH + 7) / 8, 1);
	ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	ctx->CSSetShaderResources(0, 1, &nullSRV);

	if (usingLiveDepth) {
		ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
		for (auto* rtv : rtvs) {
			if (rtv)
				rtv->Release();
		}
		if (dsv)
			dsv->Release();
	}

	globals::profiler->EndPass();

	// Each group reduces one 64x64 base tile in shared memory.
	if (spdCS) {
		globals::profiler->BeginPass("HiZPyramid::Mips");

		const uint32_t groupsX = padW / kTileSize;
		const uint32_t groupsY = padH / kTileSize;
		const uint32_t outputMips = GetMipCount() - 1;

		paramsCB->Update(SPDParams{ padW, padH, outputMips, groupsX * groupsY });

		ID3D11UnorderedAccessView* spdUAVs[kOutputMipCount]{};
		for (uint32_t i = 0; i < outputMips; ++i)
			spdUAVs[i] = outputUAVs[i].get();
		ID3D11ShaderResourceView* spdSourceSRV = mip0SRV.get();

		ctx->CSSetShader(spdCS.get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &spdSourceSRV);
		ctx->CSSetUnorderedAccessViews(0, kOutputMipCount, spdUAVs, nullptr);
		ctx->Dispatch(groupsX, groupsY, 1);

		ctx->CSSetUnorderedAccessViews(0, kOutputMipCount, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 1, &nullSRV);
		globals::profiler->EndPass();
	}

	valid = true;
	builtFrame = currentFrame;
	return true;
}

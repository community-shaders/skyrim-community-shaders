#include "HiZPyramid.h"

#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"
#include "Profiler.h"

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
	// With Terrain Blending active, read its backup of the original post-Z-prepass view.
	auto& tb = globals::features::terrainBlending;
	if (tb.loaded && tb.settings.Enabled && tb.prepassSRVBackup)
		return tb.prepassSRVBackup;
	if (auto* renderer = globals::game::renderer)
		return renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
	return nullptr;
}

ID3D11ShaderResourceView* HiZPyramid::GetLiveDepthSRV()
{
	// Terrain Blending keeps the original kMAIN view here while its replacement is bound.
	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.depthSRVBackup)
		return terrainBlending.depthSRVBackup;

	auto* renderer = globals::game::renderer;
	if (!renderer)
		return nullptr;
	return renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
}

bool HiZPyramid::CreateTexture(ID3D11Device* device, uint32_t dstW, uint32_t dstH)
{
	mipUAVs.clear();
	mip0SRV = nullptr;
	texture.reset();
	paddedWidth = 0;
	paddedHeight = 0;
	mipCount = 1;

	uint32_t mips = 1;
	for (uint32_t d = std::max(dstW, dstH); d > 1; d >>= 1)
		++mips;
	mips = std::min(mips, kExactMips + 1u);

	D3D11_TEXTURE2D_DESC td{};
	td.Width = dstW;
	td.Height = dstH;
	td.MipLevels = mips;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R32_FLOAT;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	try {
		texture = std::make_unique<Texture2D>(td, "HiZPyramid::Texture");

		// Full-chain SRV for culling.
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_R32_FLOAT;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels = mips;
		texture->CreateSRV(sd);

		// Mip-0-only SRV, so SPD reads mip 0 without overlapping its output UAVs.
		sd.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture->resource.get(), &sd, mip0SRV.put()));
		Util::SetResourceName(mip0SRV.get(), "HiZPyramid::Texture Mip0 SRV");
	} catch (...) {
		logger::error("[HiZ Pyramid] HiZ texture create failed");
		texture.reset();
		return false;
	}

	for (uint32_t m = 0; m < mips; ++m) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
		ud.Format = DXGI_FORMAT_R32_FLOAT;
		ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		ud.Texture2D.MipSlice = m;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		if (FAILED(device->CreateUnorderedAccessView(texture->resource.get(), &ud, uav.put()))) {
			logger::error("[HiZ Pyramid] HiZ mip UAV create failed");
			return false;
		}
		Util::SetResourceName(uav.get(), "HiZPyramid::Texture Mip%u UAV", m);
		mipUAVs.push_back(uav);
	}

	paddedWidth = dstW;
	paddedHeight = dstH;
	mipCount = mips;
	return true;
}

bool HiZPyramid::Build(ID3D11Device* device, ID3D11DeviceContext* ctx, bool forceRefresh)
{
	const uint32_t currentFrame = globals::state->frameCount;
	if (!forceRefresh && builtFrame == currentFrame)
		return valid;

	valid = false;

	if (!paramsCB || !globals::game::renderer)
		return false;

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	const uint32_t srcW = std::max(1u, (uint32_t)std::lround(renderSize.x));
	const uint32_t srcH = std::max(1u, (uint32_t)std::lround(renderSize.y));

	const uint32_t validW = (srcW + kDownsampleFactor - 1) / kDownsampleFactor;
	const uint32_t validH = (srcH + kDownsampleFactor - 1) / kDownsampleFactor;
	if (!validW || !validH)
		return false;

	// Prefer the live main depth; the post-Z-prepass copy is only a fallback.
	usingLiveDepth = true;
	ID3D11ShaderResourceView* srcSRV = GetLiveDepthSRV();
	if (!srcSRV) {
		usingLiveDepth = false;
		srcSRV = GetSourceDepthSRV();
	}
	if (!srcSRV)
		return false;

	// Size from the nominal extent so dynamic resolution never reallocates, padded to SPD tiles so every level halves exactly.
	const auto padToTile = [](uint32_t v) { return (v + tileSize - 1) & ~(tileSize - 1); };
	const uint32_t padW = padToTile(((uint32_t)screenSize.x + kDownsampleFactor - 1) / kDownsampleFactor);
	const uint32_t padH = padToTile(((uint32_t)screenSize.y + kDownsampleFactor - 1) / kDownsampleFactor);

	if (!padW || !padH || ((padW != paddedWidth || padH != paddedHeight) && !CreateTexture(device, padW, padH)))
		return false;

	width = validW;
	height = validH;

	// Both depth sources are R24_UNORM_X8_TYPELESS views, so one permutation covers them.
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

	// Texels outside the rendered extent reduce to far depth, so padding never occludes.
	paramsCB->Update(BaseParams{ srcW, srcH, padW, padH });

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11ShaderResourceView* nullSRV = nullptr;

	globals::profiler->BeginPass("HiZPyramid::Base");
	// kMAIN cannot be bound as a DSV while it is read as an SRV, so unbind the outputs for the dispatch.
	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	ID3D11DepthStencilView* dsv = nullptr;
	if (usingLiveDepth) {
		ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	ID3D11Buffer* cb = paramsCB->CB();
	ID3D11UnorderedAccessView* baseUAV = mipUAVs[0].get();
	ctx->CSSetShader(baseCS.get(), nullptr, 0);
	ctx->CSSetConstantBuffers(0, 1, &cb);
	ctx->CSSetShaderResources(0, 1, &srcSRV);
	ctx->CSSetUnorderedAccessViews(0, 1, &baseUAV, nullptr);
	ctx->Dispatch((padW + 7) / 8, (padH + 7) / 8, 1);
	ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
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

	// One dispatch reduces the whole chain; each group reduces one 64x64 tile in LDS.
	if (spdCS && GetMipCount() > 1) {
		globals::profiler->BeginPass("HiZPyramid::Mips");

		const uint32_t outputMips = GetMipCount() - 1;
		const uint32_t groupsX = padW / tileSize;
		const uint32_t groupsY = padH / tileSize;

		const uint32_t totalGroups = groupsX * groupsY;
		paramsCB->Update(SPDParams{ padW, padH, outputMips, totalGroups });

		ID3D11UnorderedAccessView* spdUAVs[kExactMips]{};
		for (uint32_t i = 0; i < outputMips; ++i)
			spdUAVs[i] = mipUAVs[i + 1].get();
		ID3D11ShaderResourceView* spdSourceSRV = mip0SRV.get();

		ctx->CSSetShader(spdCS.get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &spdSourceSRV);
		ctx->CSSetUnorderedAccessViews(0, kExactMips, spdUAVs, nullptr);
		ctx->Dispatch(groupsX, groupsY, 1);

		ID3D11UnorderedAccessView* spdNulls[kExactMips]{};
		ctx->CSSetUnorderedAccessViews(0, kExactMips, spdNulls, nullptr);
		ctx->CSSetShaderResources(0, 1, &nullSRV);
		globals::profiler->EndPass();
	}

	// Log only when the build parameters change.
	const std::array<uint32_t, 10> logKey{ validW, validH, padW, padH, GetMipCount(), srcW, srcH,
		(uint32_t)screenSize.x, (uint32_t)screenSize.y, usingLiveDepth ? 1u : 0u };
	if (logKey != lastLogKey) {
		lastLogKey = logKey;
		const auto& rt = globals::game::graphicsState->GetRuntimeData();
		logger::info("[HiZ Pyramid] HiZ occlusion cull active: {}x{} tiles (1/{} res) in a {}x{} texture, {} mips, source={}; screen {}x{}, depth extent {}x{}, dynRes ratio {:.3f}x{:.3f} lock={}, upscale scale {:.3f}x{:.3f}",
			validW, validH, kDownsampleFactor, padW, padH, GetMipCount(),
			usingLiveDepth ? "kMAIN" : "kPOST_ZPREPASS_COPY (fallback)",
			(uint32_t)screenSize.x, (uint32_t)screenSize.y, srcW, srcH,
			rt.dynamicResolutionWidthRatio, rt.dynamicResolutionHeightRatio, (int)rt.dynamicResolutionLock,
			globals::features::upscaling.resolutionScale.x, globals::features::upscaling.resolutionScale.y);
	}

	valid = true;
	builtFrame = currentFrame;
	return true;
}

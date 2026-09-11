#include "HiZPyramid.h"

#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"
#include "Profiler.h"

void HiZPyramid::SetupResources()
{
	paramsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BaseParams>(), "GrassOptimizations::HiZParamsCB");
}

void HiZPyramid::ClearShaderCache()
{
	if (baseCS)
		baseCS->Release();
	baseCS = nullptr;
	if (spdCS)
		spdCS->Release();
	spdCS = nullptr;
}

ID3D11ShaderResourceView* HiZPyramid::GetSourceDepthSRV()
{
	// Grass runs before the terrain blending pass, so it takes the original prepass copy; the blended
	// texture would be a frame stale and flicker grass on fast camera movement. Both branches return
	// that same R24_UNORM_X8_TYPELESS view, so GrassHiZCS needs no TERRAIN_BLENDING variant of its
	// `unorm float` declaration the way Util::GetCurrentSceneDepthSRV's R32_FLOAT consumers do.
	auto& tb = globals::features::terrainBlending;
	if (tb.loaded && tb.settings.Enabled && tb.prepassSRVBackup)
		return tb.prepassSRVBackup;
	if (auto* renderer = globals::game::renderer)
		return renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
	return nullptr;
}

ID3D11ShaderResourceView* HiZPyramid::GetLiveDepthSRV()
{
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
		texture = std::make_unique<Texture2D>(td, "GrassOptimizations::HiZ");

		// Full-chain SRV for the cull plus single-level views for the reduction passes.
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_R32_FLOAT;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels = mips;
		texture->CreateSRV(sd);

		// A mip-0-only SRV lets SPD read mip 0 without overlapping the output UAVs.
		sd.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture->resource.get(), &sd, mip0SRV.put()));
		Util::SetResourceName(mip0SRV.get(), "GrassOptimizations::HiZ Mip0 SRV");
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] HiZ texture create failed");
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
			logger::error("[GRASS OPTIMIZATIONS] HiZ mip UAV create failed");
			return false;
		}
		Util::SetResourceName(uav.get(), "GrassOptimizations::HiZ Mip%u UAV", m);
		mipUAVs.push_back(uav);
	}

	paddedWidth = dstW;
	paddedHeight = dstH;
	mipCount = mips;
	return true;
}

bool HiZPyramid::Build(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
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

	// kPOST_ZPREPASS_COPY is written before the opaque prepass, so it holds depth the scene no longer has, biased near enough to over-cull. Prefer the live target, and keep it only as a fallback.
	usingLiveDepth = true;
	ID3D11ShaderResourceView* srcSRV = GetLiveDepthSRV();
	if (!srcSRV) {
		usingLiveDepth = false;
		srcSRV = GetSourceDepthSRV();
	}
	if (!srcSRV)
		return false;

	// Converts the cull's nominal-pixel projPx into texels, derived from the extent resolved above so
	// the two cannot disagree. One scalar covers both axes, so the axis that shrank least wins: too
	// small a radius picks a level whose fixed 3x3 footprint misses part of the instance.
	const float effX = (float)srcW / std::max(1.0f, (float)screenSize.x);
	const float effY = (float)srcH / std::max(1.0f, (float)screenSize.y);
	texelPixels = kDownsampleFactor / std::clamp(std::max(effX, effY), 0.01f, 1.0f);

	// Sized from the nominal extent so a shifting dynamic-resolution ratio never reallocates, then padded to SPD's tile granularity so every allocated level halves exactly. An odd level would drop its last row, underestimate the max, and cull visible grass.
	const auto padToTile = [](uint32_t v) { return (v + tileSize - 1) & ~(tileSize - 1); };
	const uint32_t padW = padToTile(((uint32_t)screenSize.x + kDownsampleFactor - 1) / kDownsampleFactor);
	const uint32_t padH = padToTile(((uint32_t)screenSize.y + kDownsampleFactor - 1) / kDownsampleFactor);

	if (!padW || !padH || ((padW != paddedWidth || padH != paddedHeight) && !CreateTexture(device, padW, padH)))
		return false;

	width = validW;
	height = validH;

	// One variant only, since the only source is the game's R24_UNORM_X8_TYPELESS prepass copy.
	if (!baseCS) {
		baseCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\GrassOptimizations\\GrassHiZCS.hlsl", {}, "cs_5_0"));
		if (!baseCS) {
			logger::error("[GRASS OPTIMIZATIONS] HiZ CS load failed — occlusion culling disabled");
			return false;
		}
	}

	if (!spdCS) {
		spdCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\GrassOptimizations\\SPD\\SPD.hlsl", {}, "cs_5_0"));
		if (!spdCS)
			logger::error("[GRASS OPTIMIZATIONS] SPD load failed — large instances will not be occlusion culled");
	}

	// Threads past the rendered sub-rect read beyond it, so the base pass's out-of-bounds guard writes 1.0 there and neither the padding nor the unrendered margin can cull.
	paramsCB->Update(BaseParams{ srcW, srcH, padW, padH });

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11ShaderResourceView* nullSRV = nullptr;

	globals::profiler->BeginPass("GrassOptimizations::HiZBase");
	// Unbind kMAIN for the dispatch, so its use solely as an SRV, since a resource cannot be bound as both a DSV and an SRV at the same time.
	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	ID3D11DepthStencilView* dsv = nullptr;
	if (usingLiveDepth) {
		ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	ID3D11Buffer* cb = paramsCB->CB();
	ID3D11UnorderedAccessView* baseUAV = mipUAVs[0].get();
	ctx->CSSetShader(baseCS, nullptr, 0);
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

	// Each level is the exact max of the one above, so an instance of any on-screen size is testable against a fixed number of texels.
	// One dispatch for the whole chain, every group reducing its own tile from LDS.
	if (spdCS && GetMipCount() > 1) {
		globals::profiler->BeginPass("GrassOptimizations::HiZMips");

		const uint32_t outputMips = GetMipCount() - 1;
		const uint32_t groupsX = padW / tileSize;
		const uint32_t groupsY = padH / tileSize;

		const uint32_t totalGroups = groupsX * groupsY;
		paramsCB->Update(SPDParams{ padW, padH, outputMips, totalGroups });

		ID3D11UnorderedAccessView* spdUAVs[6]{};
		for (uint32_t i = 0; i < outputMips; ++i)
			spdUAVs[i] = mipUAVs[i + 1].get();
		ID3D11ShaderResourceView* spdSourceSRV = mip0SRV.get();

		ctx->CSSetShader(spdCS, nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &spdSourceSRV);
		ctx->CSSetUnorderedAccessViews(0, 6, spdUAVs, nullptr);
		ctx->Dispatch(groupsX, groupsY, 1);

		ID3D11UnorderedAccessView* spdNulls[6]{};
		ctx->CSSetUnorderedAccessViews(0, 6, spdNulls, nullptr);
		ctx->CSSetShaderResources(0, 1, &nullSRV);
		globals::profiler->EndPass();
	}

	// The first build runs before Upscaling, so update the log after so accurate values are logged. The log key is the build's parameters, so it only logs when they change.
	const std::array<uint32_t, 10> logKey{ validW, validH, padW, padH, GetMipCount(), srcW, srcH,
		(uint32_t)screenSize.x, (uint32_t)screenSize.y, usingLiveDepth ? 1u : 0u };
	if (logKey != lastLogKey) {
		lastLogKey = logKey;
		const auto& rt = globals::game::graphicsState->GetRuntimeData();
		logger::info("[GRASS OPTIMIZATIONS] HiZ occlusion cull active: {}x{} tiles (1/{} res) in a {}x{} texture, {} mips, source={}; screen {}x{}, depth extent {}x{}, dynRes ratio {:.3f}x{:.3f} lock={}, upscale scale {:.3f}x{:.3f}",
			validW, validH, kDownsampleFactor, padW, padH, GetMipCount(),
			usingLiveDepth ? "LIVE kMAIN copy" : "POST_ZPREPASS_COPY (stale fallback)",
			(uint32_t)screenSize.x, (uint32_t)screenSize.y, srcW, srcH,
			rt.dynamicResolutionWidthRatio, rt.dynamicResolutionHeightRatio, (int)rt.dynamicResolutionLock,
			globals::features::upscaling.resolutionScale.x, globals::features::upscaling.resolutionScale.y);
	}

	valid = true;
	return true;
}

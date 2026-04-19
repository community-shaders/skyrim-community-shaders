#include "NvVrsController.h"

#include "VrsLutManager.h"

#include <algorithm>
#include <array>
#include <vector>

#include "Globals.h"

namespace
{
	constexpr uint32_t kTileWidth = NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
	constexpr uint32_t kTileHeight = NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

	inline uint32_t CeilDiv(uint32_t v, uint32_t d)
	{
		return (v + d - 1u) / d;
	}
}

NvVrsController::~NvVrsController()
{
	ReleaseResources();
	if (debugState.initialized) {
		NvAPI_Unload();
	}
}

/// Per-frame entry point: guards → lazy-init → surface → pattern → bind.
void NvVrsController::Update(const Settings& settings, const FrameInfo& frameInfo, ID3D11Device* device, ID3D11DeviceContext* context)
{
	currentSettings = settings;
	currentFrame = frameInfo;

	if (currentSettings.lutPreset != lastLutPreset) {
		rateTableDirty = true;
		lastLutPreset = currentSettings.lutPreset;
	}

	if (!settings.enable) {
		SetLastDisableReason(DisableReason::SettingsDisabled);
		Disable(context);
		return;
	}

	if (!device || !context) {
		SetLastDisableReason(DisableReason::InvalidContext);
		Disable(context);
		return;
	}

	if (!EnsureInitialized(device)) {
		SetLastDisableReason(DisableReason::InitFailed);
		Disable(context);
		return;
	}

	if (!EnsureSurface(device)) {
		SetLastDisableReason(DisableReason::SurfaceFailed);
		Disable(context);
		return;
	}

	UpdateSurfaceData(context);

	// Keep official-style enable order for SRS path: bind SRRV first, then enable viewport shading rates.
	if ((!debugState.active || surfaceDirty) && !BindSurface(context)) {
		SetLastDisableReason(DisableReason::BindSurfaceFailed);
		Disable(context);
		return;
	}

	if ((!debugState.active || rateTableDirty) && !BindRateTable(context)) {
		SetLastDisableReason(DisableReason::BindRateTableFailed);
		Disable(context);
		return;
	}

	debugState.active = true;
	surfaceDirty = false;
	rateTableDirty = false;
}

/// Unbind VRS: clear SRRV, reset viewports to 1×1, mark dirty for next Update.
void NvVrsController::Disable(ID3D11DeviceContext* context)
{
	if (!context || !debugState.initialized) {
		debugState.active = false;
		return;
	}

	if (!debugState.active) {
		return;
	}

	NvAPI_D3D11_RSSetShadingRateResourceView(context, nullptr);

	const uint32_t viewportCount = QueryViewportCount(context, 2);
	std::vector<NV_D3D11_VIEWPORT_SHADING_RATE_DESC> vsrd(viewportCount);
	for (auto& desc : vsrd) {
		desc.enableVariablePixelShadingRate = false;
		VrsLutManager::FillDisabledRateTable(desc.shadingRateTable, static_cast<uint32_t>(std::size(desc.shadingRateTable)));
	}

	NV_D3D11_VIEWPORTS_SHADING_RATE_DESC srd{};
	srd.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
	srd.numViewports = viewportCount;
	srd.pViewports = vsrd.data();

	auto status = NvAPI_D3D11_RSSetViewportsPixelShadingRates(context, &srd);
	if (status != NVAPI_OK) {
		MarkError(static_cast<int>(status), "Disable.RSSetViewportsPixelShadingRates.Dynamic");
	}
	debugState.lastViewportCount = viewportCount;

	debugState.active = false;
	surfaceDirty = true;
	rateTableDirty = true;
}

/// Lazy-init NvAPI and check VPS hardware capability (called once).
bool NvVrsController::EnsureInitialized(ID3D11Device* device)
{
	if (debugState.initialized) {
		return debugState.supported;
	}

	auto initStatus = NvAPI_Initialize();
	if (initStatus != NVAPI_OK) {
		MarkError(static_cast<int>(initStatus), "EnsureInitialized.NvAPI_Initialize");
		debugState.initialized = false;
		debugState.supported = false;
		return false;
	}

	NV_D3D1x_GRAPHICS_CAPS caps{};
	const NvAPI_Status graphicsCapsStatus = NvAPI_D3D1x_GetGraphicsCapabilities(device, NV_D3D1x_GRAPHICS_CAPS_VER, &caps);
	if (graphicsCapsStatus != NVAPI_OK) {
		MarkError(static_cast<int>(graphicsCapsStatus), "EnsureInitialized.GetGraphicsCapabilities");
		debugState.initialized = true;
		debugState.supported = false;
		return false;
	}

	if (!caps.bVariablePixelRateShadingSupported) {
		debugState.initialized = true;
		debugState.supported = false;
		debugState.lastNvapiStatus = NVAPI_OK;
		return false;
	}

	debugState.initialized = true;
	debugState.supported = true;
	debugState.lastNvapiStatus = NVAPI_OK;
	return true;
}

/// (Re)create R8_UINT shading rate surface + SRRV + debug texture when resolution changes.
bool NvVrsController::EnsureSurface(ID3D11Device* device)
{
	const uint32_t surfaceWidth = CeilDiv(static_cast<uint32_t>(std::max(currentFrame.displayWidth, 1)), kTileWidth);
	const uint32_t surfaceHeight = CeilDiv(static_cast<uint32_t>(std::max(currentFrame.displayHeight, 1)), kTileHeight);

	debugState.tileWidth = surfaceWidth;
	debugState.tileHeight = surfaceHeight;

	if (shadingRateSurface && surfaceWidth == lastSurfaceWidth && surfaceHeight == lastSurfaceHeight) {
		return true;
	}

	debugState.lastRebuildReason = shadingRateSurface ? RebuildReason::ResolutionChanged : RebuildReason::FirstCreate;

	shadingRateSurface.Reset();
	if (shadingRateResourceView) {
		shadingRateResourceView->Release();
		shadingRateResourceView = nullptr;
	}

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = surfaceWidth;
	texDesc.Height = surfaceHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8_UINT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	auto hr = device->CreateTexture2D(&texDesc, nullptr, shadingRateSurface.ReleaseAndGetAddressOf());
	if (FAILED(hr) || !shadingRateSurface) {
		MarkError(static_cast<int>(hr), "EnsureSurface.CreateTexture2D");
		return false;
	}

	NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC srrvDesc{};
	srrvDesc.version = NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
	srrvDesc.Format = DXGI_FORMAT_R8_UINT;
	srrvDesc.ViewDimension = NV_SRRV_DIMENSION_TEXTURE2D;
	srrvDesc.Texture2D.MipSlice = 0;

	auto status = NvAPI_D3D11_CreateShadingRateResourceView(device, shadingRateSurface.Get(), &srrvDesc, &shadingRateResourceView);
	if (status != NVAPI_OK || !shadingRateResourceView) {
		MarkError(static_cast<int>(status), "EnsureSurface.CreateShadingRateResourceView");
		return false;
	}

	lastSurfaceWidth = surfaceWidth;
	lastSurfaceHeight = surfaceHeight;
	surfaceDirty = true;
	rateTableDirty = true;

	// Create debug visualisation texture (R8G8B8A8_UNORM, same tile dimensions).
	debugVisualizationTexture.Reset();
	debugVisualizationSRV.Reset();
	{
		D3D11_TEXTURE2D_DESC dbgDesc{};
		dbgDesc.Width = surfaceWidth;
		dbgDesc.Height = surfaceHeight;
		dbgDesc.MipLevels = 1;
		dbgDesc.ArraySize = 1;
		dbgDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		dbgDesc.SampleDesc.Count = 1;
		dbgDesc.Usage = D3D11_USAGE_DEFAULT;
		dbgDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		auto dbgHr = device->CreateTexture2D(&dbgDesc, nullptr, debugVisualizationTexture.ReleaseAndGetAddressOf());
		if (SUCCEEDED(dbgHr) && debugVisualizationTexture) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			auto srvHr = device->CreateShaderResourceView(debugVisualizationTexture.Get(), &srvDesc, debugVisualizationSRV.ReleaseAndGetAddressOf());
			if (FAILED(srvHr) || !debugVisualizationSRV) {
				debugVisualizationTexture.Reset();
				debugVisualizationSRV.Reset();
			}
		}
	}
	srsBuilder = VrsSrsBuilder{};

	return true;
}

/// Rebuild SRS pattern if dirty/changed, then upload to GPU via UpdateSubresource.
void NvVrsController::UpdateSurfaceData(ID3D11DeviceContext* context)
{
	const uint32_t width = lastSurfaceWidth;
	const uint32_t height = lastSurfaceHeight;
	const uint32_t renderWidth = CeilDiv(static_cast<uint32_t>(std::max(currentFrame.renderWidth, 1)), kTileWidth);
	const uint32_t renderHeight = CeilDiv(static_cast<uint32_t>(std::max(currentFrame.renderHeight, 1)), kTileHeight);

	if (patternBuffer.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
		patternBuffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
	}

	VrsSrsBuilder::Params srsParams;
	srsParams.leftSubrectUV = currentSettings.leftSubrectUV;
	srsParams.rightSubrectUV = currentSettings.rightSubrectUV;
	srsParams.ringGrowthRate = currentSettings.ringGrowthRate;
	srsParams.srsPreset = currentSettings.srsPreset;
	srsParams.enableDirectionalRates = currentSettings.enableDirectionalRates;
	srsParams.enableBoundaryDither = currentSettings.enableBoundaryDither;

	if (!surfaceDirty && !srsBuilder.NeedsRebuild(width, height, renderWidth, renderHeight, srsParams)) {
		debugState.patternReuseCount++;
		return;
	}

	srsBuilder.Build(patternBuffer.data(), width, height, renderWidth, renderHeight, srsParams);
	debugState.patternRebuildCount++;

	if (currentSettings.enableDiagnostics) {
		std::fill(std::begin(debugState.lastTileLevelCount), std::end(debugState.lastTileLevelCount), 0ull);
		for (size_t i = 0; i < patternBuffer.size(); ++i) {
			const uint8_t level = patternBuffer[i];
			if (level < SrsLevel::kCount) {
				debugState.lastTileLevelCount[level]++;
			}
		}

		// Build debug visualisation into a separate RGBA buffer.
		if (debugVisualizationBuffer.size() != static_cast<size_t>(width) * height) {
			debugVisualizationBuffer.resize(static_cast<size_t>(width) * height);
		}
		VrsSrsBuilder::BuildDebugVisualization(debugVisualizationBuffer.data(), patternBuffer.data(), width, height);
	}

	// rowPitch uses bytes per row; for R8_UINT each tile is 1 byte, so rowPitch == width.
	context->UpdateSubresource(shadingRateSurface.Get(), 0, nullptr, patternBuffer.data(), width, 0);

	// Upload debug visualisation (RGBA, 4 bytes per tile).
	if (currentSettings.enableDiagnostics && debugVisualizationTexture) {
		context->UpdateSubresource(debugVisualizationTexture.Get(), 0, nullptr,
			debugVisualizationBuffer.data(), width * sizeof(uint32_t), 0);
	}

	srsBuilder.UpdateCache(width, height, renderWidth, renderHeight, srsParams);
}

bool NvVrsController::BindRateTable(ID3D11DeviceContext* context)
{
	const uint32_t viewportCount = QueryViewportCount(context, 2);
	std::vector<NV_D3D11_VIEWPORT_SHADING_RATE_DESC> vsrd(viewportCount);
	for (auto& desc : vsrd) {
		desc.enableVariablePixelShadingRate = true;
		VrsLutManager::FillActiveRateTable(desc.shadingRateTable, static_cast<uint32_t>(std::size(desc.shadingRateTable)), currentSettings.lutPreset);
	}

	NV_D3D11_VIEWPORTS_SHADING_RATE_DESC srd{};
	srd.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
	srd.numViewports = viewportCount;
	srd.pViewports = vsrd.data();

	auto status = NvAPI_D3D11_RSSetViewportsPixelShadingRates(context, &srd);
	if (status != NVAPI_OK) {
		MarkError(static_cast<int>(status), "BindRateTable.RSSetViewportsPixelShadingRates");
		return false;
	}

	debugState.lastNvapiStatus = NVAPI_OK;
	debugState.lastViewportCount = viewportCount;
	return true;
}

bool NvVrsController::BindSurface(ID3D11DeviceContext* context)
{
	auto status = NvAPI_D3D11_RSSetShadingRateResourceView(context, shadingRateResourceView);
	if (status != NVAPI_OK) {
		MarkError(static_cast<int>(status), "BindSurface.RSSetShadingRateResourceView");
		return false;
	}

	debugState.lastNvapiStatus = NVAPI_OK;
	return true;
}

void NvVrsController::ReleaseResources()
{
	if (shadingRateResourceView) {
		shadingRateResourceView->Release();
		shadingRateResourceView = nullptr;
	}
	shadingRateSurface.Reset();
	lastSurfaceWidth = 0;
	lastSurfaceHeight = 0;
	patternBuffer.clear();
	debugVisualizationTexture.Reset();
	debugVisualizationSRV.Reset();
	debugVisualizationBuffer.clear();
}

void NvVrsController::MarkError(int status, const char* site)
{
	debugState.lastNvapiStatus = status;
	debugState.lastFailureSite = site;
	debugState.failureCount++;
}

uint32_t NvVrsController::QueryViewportCount(ID3D11DeviceContext* context, uint32_t fallback) const
{
	if (!context) {
		return fallback;
	}

	UINT viewportCount = 0;
	context->RSGetViewports(&viewportCount, nullptr);
	if (viewportCount == 0) {
		return fallback;
	}

	return static_cast<uint32_t>(viewportCount);
}

ID3D11ShaderResourceView* NvVrsController::GetDebugVisualizationSRV() const
{
	return debugVisualizationSRV.Get();
}

void NvVrsController::SetLastDisableReason(DisableReason reason)
{
	if (reason != DisableReason::None) {
		debugState.lastDisableReason = reason;
	}
}

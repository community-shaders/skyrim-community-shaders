#include "CloudShadows.h"

#include "../I18n/I18n.h"
#include "Effects11.h"
#include "Effects11/SettingManager.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

#include <sstream>

#define I18N_KEY_PREFIX "feature.cloud_shadows."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Opacity)

void CloudShadows::DrawSettings()
{
	const Util::LockedSection enbLock(globals::features::effects11.OwnsFeature(E11Handoff::Feature::CloudShadows),
		T("common.settings_managed_by_enb", "This setting is managed by Effects 11."));

	ImGui::SliderFloat(T(TKEY("opacity"), "Opacity"), &settings.Opacity, 0.0f, 4.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("opacity_tooltip"),
							  "Higher values make cloud shadows darker."));
	}
}

#undef I18N_KEY_PREFIX

void CloudShadows::LoadSettings(json& o_json)
{
	settings = o_json;
}

void CloudShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void CloudShadows::RestoreDefaultSettings()
{
	settings = {};
}

CloudShadows::Settings CloudShadows::GetCommonBufferData()
{
	if (!loaded)
		return settings;

	auto data = settings;

	if (globals::features::effects11.OwnsFeature(E11Handoff::Feature::CloudShadows)) {
		auto& settingManager = SettingManager::GetSingleton();
		if (settingManager.GetValue<bool>("EnableCloudShadows", "EFFECT")) {
			data.Opacity = settingManager.GetInterpolatedTimeOfDayValue("Amount", "CLOUDSHADOWS");
		} else {
			data.Opacity = 0.0f;
		}
	}

	return data;
}

void CloudShadows::CheckResourcesSide(int side)
{
	if (!frame_checker[side].IsNewFrame())
		return;

	if (previouslyRenderedSide >= 0 && previouslyRenderedSide != side)
		PropagateToCompletion(previouslyRenderedSide);
	previouslyRenderedSide = side;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	globals::profiler->BeginPass("CloudShadows::ClearLayer");
	context->ClearRenderTargetView(cloudShadowLayerRTVs[0][side], black);
	globals::profiler->EndPass();
	renderedLayersMask[side] = 0;
}

void CloudShadows::PropagateToCompletion(int side)
{
	uint32_t mask = renderedLayersMask[side];
	unsigned long highBit;
	int fromLayer = _BitScanReverse(&highBit, mask) ? static_cast<int>(highBit) : 0;

	auto context = globals::d3d::context;

	globals::profiler->BeginPass("CloudShadows::Propagate");
	uint32_t newLayers = mask & ~globalRenderedMask;
	if (newLayers) {
		unsigned long bit;
		uint32_t remaining = newLayers;
		while (_BitScanForward(&bit, remaining)) {
			int newLayer = static_cast<int>(bit);
			for (int otherSide = 0; otherSide < 6; otherSide++) {
				if (otherSide == side)
					continue;
				if (renderedLayersMask[otherSide] & (1u << newLayer))
					continue;
				uint32_t belowMask = renderedLayersMask[otherSide] & ((1u << newLayer) - 1);
				unsigned long nearestBit;
				int srcLayer = _BitScanReverse(&nearestBit, belowMask) ? static_cast<int>(nearestBit) : 0;
				UINT otherSub = D3D11CalcSubresource(0, otherSide, cubemapMipLevels);
				context->CopySubresourceRegion(
					texCloudShadowLayers[newLayer]->resource.get(), otherSub, 0, 0, 0,
					texCloudShadowLayers[srcLayer]->resource.get(), otherSub, nullptr);
				renderedLayersMask[otherSide] |= (1u << newLayer);
			}
			remaining &= ~(1u << bit);
		}
		globalRenderedMask |= mask;
	}

	if (fromLayer < kMaxCloudLayers - 1) {
		UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
		context->CopySubresourceRegion(
			texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get(), subresource, 0, 0, 0,
			texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
	}
	globals::profiler->EndPass();
}

void CloudShadows::SkyShaderHacks()
{
	auto context = globals::d3d::context;

	// Outside the capture, a cloud layer reads the occlusion of the layers up to and including itself
	if (bindLayerSelfShadow) {
		bindLayerSelfShadow = false;
		if (captureLayerFromView) {
			captureLayerFromView = false;
			CaptureLayerFromView();
		}
		ID3D11ShaderResourceView* selfShadowSRV = texCloudShadowLayers[currentLayerForDraw]->srv.get();
		context->PSSetShaderResources(26, 1, &selfShadowSRV);
	}

	if (!overrideSky)
		return;
	overrideSky = false;

	auto renderer = globals::game::renderer;

	auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	// render targets
	ID3D11RenderTargetView* rtvs[3] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	context->OMGetRenderTargets(3, rtvs, &dsv);

	int side = -1;
	for (int i = 0; i < 6; ++i)
		if (rtvs[0] == reflections.cubeSideRTV[i]) {
			side = i;
			break;
		}

	if (side >= 0) {
		CheckResourcesSide(side);

		int layer = currentLayerForDraw;

		unsigned long highBit;
		int prevLayer = _BitScanReverse(&highBit, renderedLayersMask[side]) ? static_cast<int>(highBit) : -1;

		UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);

		int fromLayer = std::max(prevLayer, 0);

		if (layer > 0) {
			globals::profiler->BeginPass("CloudShadows::LayerSeed");
			context->CopySubresourceRegion(
				texCloudShadowLayers[layer]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
			globals::profiler->EndPass();
		}

		// The layer chain is being written by this draw, so self shadowing reads last frame's composite
		ID3D11ShaderResourceView* selfShadowSRV = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(26, 1, &selfShadowSRV);

		ID3D11RenderTargetView* captureRTVs[4] = { rtvs[0], rtvs[1], rtvs[2], cloudShadowLayerRTVs[layer][side] };
		context->OMSetRenderTargets(4, captureRTVs, nullptr);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);
		// Make the engine re-apply its own targets and blend state on the next draw
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
		context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);

		renderedLayersMask[side] |= (1u << layer);
		lastCaptureFrame = lastReflectionsCaptureFrame = globals::state->frameCount;
	}

	// Release the references OMGetRenderTargets added
	for (int i = 0; i < 3; ++i) {
		if (rtvs[i])
			rtvs[i]->Release();
	}
	if (dsv)
		dsv->Release();
}

int CloudShadows::FindCloudLayer(RE::BSRenderPass* Pass)
{
	auto sky = globals::game::sky;
	if (!sky || !sky->clouds)
		return -1;

	// Slots past numLayers can still hold a previous weather's cloud geometry
	const int layerCount = std::min(static_cast<int>(sky->clouds->numLayers), kMaxCloudLayers);
	for (int i = 0; i < layerCount; i++) {
		if (sky->clouds->clouds[i].get() == Pass->geometry)
			return i;
	}
	return -1;
}

void CloudShadows::ModifySky(RE::BSRenderPass* Pass)
{
	auto shadowState = globals::game::shadowState;

	auto& cubeMapRenderTarget = shadowState->GetRuntimeData().cubeMapRenderTarget;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS)
		return;

	int layer = FindCloudLayer(Pass);
	if (layer < 0)
		return;

	currentLayerForDraw = layer;
	occlusionClearedWithoutClouds = false;
	if (cubeMapRenderTarget == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS) {
		overrideSky = true;
	} else {
		bindLayerSelfShadow = true;
		// The reflections camera only runs for dynamic-cubemap water, so the main view captures whenever it did not
		const auto state = globals::state;
		captureLayerFromView = state->inWorld && !state->isMapMenuOpen && lastReflectionsCaptureFrame != state->frameCount;
		captureGeometry = Pass->geometry;
	}
}

ID3D11VertexShader* CloudShadows::GetCaptureVertexShader()
{
	const auto state = globals::state;
	if (!state->currentShader)
		return nullptr;

	const uint32_t descriptor = state->currentVertexDescriptor;
	auto& entry = captureVertexShaders[descriptor];
	if (entry.checkedFrame == state->frameCount)
		return entry.shader.get();
	entry.checkedFrame = state->frameCount;

	// Same defines as the bound sky vertex shader, so the capture variant matches its input layout and the bound pixel shader
	const auto shaderDefines = SIE::ShaderCache::GetDefinesString(*state->currentShader, descriptor);
	const auto defines = shaderDefines + ";" + state->GetDefinesString();
	if (entry.defines == defines)
		return entry.shader.get();
	entry.defines = defines;
	entry.shader = nullptr;

	std::vector<std::string> names;
	std::vector<std::string> values;
	std::istringstream tokens(shaderDefines);
	for (std::string token; tokens >> token;) {
		const auto separator = token.find('=');
		names.push_back(token.substr(0, separator));
		values.push_back(separator == std::string::npos ? std::string{} : token.substr(separator + 1));
	}
	std::vector<std::pair<const char*, const char*>> macros;
	for (size_t i = 0; i < names.size(); i++)
		macros.emplace_back(names[i].c_str(), values[i].c_str());
	macros.emplace_back("CLOUD_SHADOWS_CAPTURE", "");

	entry.shader.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\Sky.hlsl", macros, "vs_5_0")));
	if (entry.shader)
		Util::SetResourceName(entry.shader.get(), "CloudShadows::CaptureVS[%X]", descriptor);
	else
		logger::warn("[Cloud Shadows] Capture vertex shader for sky descriptor {:X} failed to compile; the main view does not capture it", descriptor);
	return entry.shader.get();
}

void CloudShadows::CaptureLayerFromView()
{
	auto* geometry = std::exchange(captureGeometry, nullptr);
	auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
	if (!triShape)
		return;
	const UINT indexCount = 3u * triShape->GetTrishapeRuntimeData().triangleCount;
	if (!indexCount)
		return;

	auto context = globals::d3d::context;
	winrt::com_ptr<ID3D11Buffer> indexBuffer;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
	UINT indexOffset = 0;
	context->IAGetIndexBuffer(indexBuffer.put(), &indexFormat, &indexOffset);
	if (!indexBuffer)
		return;

	auto* captureVS = GetCaptureVertexShader();
	if (!captureVS)
		return;

	const uint32_t frame = globals::state->frameCount;
	const int layer = currentLayerForDraw;

	if (viewCaptureFrame != frame) {
		float black[4] = { 0, 0, 0, 0 };
		// After a gap the composite holds another sky; faces not yet recaptured read as clear
		if (frame - lastCaptureFrame > kCaptureFramesPerCube) {
			for (auto* rtv : cloudShadowLayerRTVs[kMaxCloudLayers - 1])
				context->ClearRenderTargetView(rtv, black);
		}
		viewCaptureFrame = frame;
		for (uint32_t i = 0; i < kCaptureFacesPerFrame; i++) {
			const uint32_t face = (frame % kCaptureFramesPerCube) * kCaptureFacesPerFrame + i;
			viewCaptureFaces[i] = face;
			context->ClearRenderTargetView(cloudShadowLayerRTVs[0][face], black);
			renderedLayersMask[face] = 0;
		}
		viewCapturePending = true;
	}

	globals::state->BeginPerfEvent("CloudShadows::Capture");

	ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* savedDSV = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
	ID3D11BlendState* savedBlendState = nullptr;
	float savedBlendFactor[4] = {};
	UINT savedSampleMask = 0;
	context->OMGetBlendState(&savedBlendState, savedBlendFactor, &savedSampleMask);
	ID3D11RasterizerState* savedRasterizerState = nullptr;
	context->RSGetState(&savedRasterizerState);
	D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
	UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&savedViewportCount, savedViewports);
	ID3D11VertexShader* savedVS = nullptr;
	context->VSGetShader(&savedVS, nullptr, nullptr);
	ID3D11Buffer* savedFaceCB = nullptr;
	context->VSGetConstantBuffers(kCaptureFaceSlot, 1, &savedFaceCB);

	D3D11_TEXTURE2D_DESC layerDesc{};
	texCloudShadowLayers[layer]->resource->GetDesc(&layerDesc);
	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(layerDesc.Width), static_cast<float>(layerDesc.Height), 0.0f, 1.0f };
	context->RSSetViewports(1, &viewport);
	context->RSSetState(captureRasterizerState.get());
	const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	context->OMSetBlendState(cloudShadowBlendState, blendFactor, 0xffffffff);
	context->VSSetShader(captureVS, nullptr, 0);
	// The layer chain is being written, so self shadowing reads the last composite
	ID3D11ShaderResourceView* selfShadowSRV = texCubemapCloudOccCopy->srv.get();
	context->PSSetShaderResources(26, 1, &selfShadowSRV);

	for (const uint32_t face : viewCaptureFaces) {
		unsigned long highBit;
		const int prevLayer = _BitScanReverse(&highBit, renderedLayersMask[face]) ? static_cast<int>(highBit) : -1;
		const int fromLayer = std::max(prevLayer, 0);
		if (layer > 0 && fromLayer != layer) {
			const UINT subresource = D3D11CalcSubresource(0, face, cubemapMipLevels);
			context->CopySubresourceRegion(
				texCloudShadowLayers[layer]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
		}

		ID3D11Buffer* faceCB = captureFaceCBs[face].get();
		context->VSSetConstantBuffers(kCaptureFaceSlot, 1, &faceCB);
		// The occlusion is the pixel shader's fourth target; null targets discard its other outputs
		ID3D11RenderTargetView* captureRTVs[4] = { nullptr, nullptr, nullptr, cloudShadowLayerRTVs[layer][face] };
		context->OMSetRenderTargets(4, captureRTVs, nullptr);
		context->DrawIndexed(indexCount, 0, 0);

		renderedLayersMask[face] |= (1u << layer);
	}
	lastCaptureFrame = frame;

	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
	context->OMSetBlendState(savedBlendState, savedBlendFactor, savedSampleMask);
	context->RSSetState(savedRasterizerState);
	context->RSSetViewports(savedViewportCount, savedViewports);
	context->VSSetShader(savedVS, nullptr, 0);
	context->VSSetConstantBuffers(kCaptureFaceSlot, 1, &savedFaceCB);

	for (auto* rtv : savedRTVs) {
		if (rtv)
			rtv->Release();
	}
	for (IUnknown* object : std::initializer_list<IUnknown*>{ savedDSV, savedBlendState, savedRasterizerState, savedVS, savedFaceCB }) {
		if (object)
			object->Release();
	}

	globals::state->EndPerfEvent();
}

void CloudShadows::PropagateViewCapture()
{
	if (!viewCapturePending)
		return;
	viewCapturePending = false;
	for (const uint32_t face : viewCaptureFaces)
		PropagateToCompletion(static_cast<int>(face));
}

void CloudShadows::ReflectionsPrepass()
{
	if (frameChecker.IsNewFrame()) {
		PropagateViewCapture();

		if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
			!globals::game::sky->currentClimate)
			return;

		auto context = globals::d3d::context;

		globals::profiler->BeginPass("CloudShadows::OcclusionCopy");
		// No cloud layer draws, so nothing would overwrite the last weather's occlusion
		auto clouds = globals::game::sky->clouds;
		if (!clouds || clouds->numLayers == 0) {
			if (!occlusionClearedWithoutClouds) {
				float black[4] = { 0, 0, 0, 0 };
				for (auto* rtv : cloudShadowLayerRTVs[kMaxCloudLayers - 1])
					context->ClearRenderTargetView(rtv, black);
				occlusionClearedWithoutClouds = true;
			}
		} else {
			occlusionClearedWithoutClouds = false;
		}

		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get());
		globals::profiler->EndPass();

		ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(25, 1, &srv);
		context->CSSetShaderResources(25, 1, &srv);
	}
}

void CloudShadows::EarlyPrepass()
{
	if (previouslyRenderedSide >= 0) {
		PropagateToCompletion(previouslyRenderedSide);
		previouslyRenderedSide = -1;
	}
	PropagateViewCapture();
	globalRenderedMask = 0;

	// Draws outside the reflections pass read t25 too; an occlusion older than one cube of captures belongs to another sky
	auto context = globals::d3d::context;
	const bool current = globals::state->frameCount - lastCaptureFrame <= kCaptureFramesPerCube;
	if (current)
		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get());

	ID3D11ShaderResourceView* srv = current ? texCubemapCloudOccCopy->srv.get() : nullptr;
	context->PSSetShaderResources(25, 1, &srv);
	context->CSSetShaderResources(25, 1, &srv);
}

void CloudShadows::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	{
		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};

		reflections.texture->GetDesc(&texDesc);
		reflections.SRV->GetDesc(&srvDesc);

		texDesc.Format = srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		cubemapMipLevels = texDesc.MipLevels;

		for (int layer = 0; layer < kMaxCloudLayers; ++layer) {
			char name[64];
			snprintf(name, sizeof(name), "CloudShadows::Layer[%d]", layer);
			texCloudShadowLayers[layer] = new Texture2D(texDesc, name);
			texCloudShadowLayers[layer]->CreateSRV(srvDesc);

			for (int face = 0; face < 6; ++face) {
				reflections.cubeSideRTV[face]->GetDesc(&rtvDesc);
				rtvDesc.Format = texDesc.Format;
				DX::ThrowIfFailed(device->CreateRenderTargetView(texCloudShadowLayers[layer]->resource.get(), &rtvDesc, &cloudShadowLayerRTVs[layer][face]));
				Util::SetResourceName(cloudShadowLayerRTVs[layer][face], "CloudShadows::Layer[%d] RTV[%d]", layer, face);
				// Faces are only written once the engine renders them; self shadowing can read them before that
				float black[4] = { 0, 0, 0, 0 };
				context->ClearRenderTargetView(cloudShadowLayerRTVs[layer][face], black);
			}
		}

		texCubemapCloudOccCopy = new Texture2D(texDesc, "CloudShadows::CubemapCloudOccCopy");
		texCubemapCloudOccCopy->CreateSRV(srvDesc);
		// Bound for self shadowing before the first reflections prepass fills it
		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[0]->resource.get());
	}
	{
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;

		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &cloudShadowBlendState));
		Util::SetResourceName(cloudShadowBlendState, "CloudShadows::BlendState");
	}
	{
		// A face projection can flip the winding the engine culls by, so the redraw culls nothing
		D3D11_RASTERIZER_DESC rasterizerDesc = {};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.DepthClipEnable = true;

		DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, captureRasterizerState.put()));
		Util::SetResourceName(captureRasterizerState.get(), "CloudShadows::CaptureRasterizerState");
	}
	{
		struct alignas(16) CaptureFace
		{
			uint32_t index;
			uint32_t pad[3];
		};

		for (uint32_t face = 0; face < 6; ++face) {
			const CaptureFace data = { face, {} };
			D3D11_BUFFER_DESC cbDesc = ConstantBufferDesc<CaptureFace>(false);
			cbDesc.Usage = D3D11_USAGE_IMMUTABLE;
			const D3D11_SUBRESOURCE_DATA initData = { &data, 0, 0 };
			DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, &initData, captureFaceCBs[face].put()));
			Util::SetResourceName(captureFaceCBs[face].get(), "CloudShadows::CaptureFace[%u]", face);
		}
	}
}

void CloudShadows::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::state->UpdateSkyShaderPermutation(Pass);
	globals::features::cloudShadows.ModifySky(Pass);
	func(This, Pass, RenderFlags);
}

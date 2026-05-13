#include "CloudShadows.h"

#include <bit>

#include "CloudRelight.h"
#include "State.h"
#include "Utils/D3D.h"

namespace
{
	int HighestSetLayer(uint32_t mask, int emptyValue)
	{
		return mask ? static_cast<int>(std::bit_width(mask) - 1) : emptyValue;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Opacity)

void CloudShadows::DrawSettings()
{
	ImGui::SliderFloat("Opacity", &settings.Opacity, 0.0f, 4.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Higher values make cloud shadows darker.");
	}
}

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

CloudShadows::Settings CloudShadows::GetCommonBufferData() const
{
	return settings;
}

bool CloudShadows::CloudRelightEnabled() const
{
	return globals::features::cloudRelight.loaded &&
	       globals::features::cloudRelight.settings.enabled &&
	       texSelfShadowCopy &&
	       texCloudShadowLayers[0];
}

void CloudShadows::CheckResourcesSide(int side)
{
	static Util::FrameChecker global_frame_checker;
	static Util::FrameChecker frame_checker[6];

	if (global_frame_checker.IsNewFrame())
		globalRenderedMask = 0;

	if (!frame_checker[side].IsNewFrame())
		return;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	if (!CloudRelightEnabled()) {
		context->ClearRenderTargetView(cubemapCloudOccRTVs[side], black);
		return;
	}

	if (previouslyRenderedSide >= 0 && previouslyRenderedSide != side)
		PropagateToCompletion(previouslyRenderedSide);
	previouslyRenderedSide = side;

	context->ClearRenderTargetView(cloudShadowLayerRTVs[0][side], black);
	renderedLayersMask[side] = 0;
}

void CloudShadows::PropagateToCompletion(int side)
{
	if (!CloudRelightEnabled() || side < 0)
		return;

	const uint32_t mask = renderedLayersMask[side];
	const int fromLayer = HighestSetLayer(mask, 0);

	auto context = globals::d3d::context;

	uint32_t newLayers = mask & ~globalRenderedMask;
	if (newLayers) {
		uint32_t remaining = newLayers;
		while (remaining) {
			const int newLayer = static_cast<int>(std::countr_zero(remaining));
			for (int otherSide = 0; otherSide < 6; otherSide++) {
				if (otherSide == side)
					continue;
				if (renderedLayersMask[otherSide] & (1u << newLayer))
					continue;

				const uint32_t belowMask = renderedLayersMask[otherSide] & ((1u << newLayer) - 1);
				const int srcLayer = HighestSetLayer(belowMask, 0);
				const UINT otherSubresource = D3D11CalcSubresource(0, otherSide, cubemapMipLevels);
				context->CopySubresourceRegion(
					texCloudShadowLayers[newLayer]->resource.get(), otherSubresource, 0, 0, 0,
					texCloudShadowLayers[srcLayer]->resource.get(), otherSubresource, nullptr);
				renderedLayersMask[otherSide] |= 1u << newLayer;
			}
			remaining &= ~(1u << newLayer);
		}
		globalRenderedMask |= mask;
	}

	if (fromLayer < kMaxCloudLayers - 1) {
		const UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
		context->CopySubresourceRegion(
			texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get(), subresource, 0, 0, 0,
			texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
	}
}

void CloudShadows::SkyShaderHacks()
{
	if (!overrideSky)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	ID3D11RenderTargetView* rtvs[4];
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(3, rtvs, &dsv);

	int side = -1;
	for (int i = 0; i < 6; ++i) {
		if (rtvs[0] == reflections.cubeSideRTV[i]) {
			side = i;
			break;
		}
	}

	if (side == -1) {
		for (int i = 0; i < 3; ++i) {
			if (rtvs[i])
				rtvs[i]->Release();
		}
		if (dsv)
			dsv->Release();
		overrideSky = false;
		return;
	}

	CheckResourcesSide(side);

	int layer = 0;
	if (CloudRelightEnabled()) {
		layer = std::clamp(currentLayerForDraw, 0, kMaxCloudLayers - 1);
		const uint32_t belowMask = renderedLayersMask[side] & ((1u << layer) - 1);
		const int fromLayer = HighestSetLayer(belowMask, 0);
		const UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);

		context->CopyResource(texSelfShadowCopy->resource.get(), texCloudShadowLayers[layer]->resource.get());

		if (layer > 0) {
			context->CopySubresourceRegion(
				texCloudShadowLayers[layer]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
		}

		ID3D11ShaderResourceView* selfShadowSrv = texSelfShadowCopy->srv.get();
		context->PSSetShaderResources(26, 1, &selfShadowSrv);

		rtvs[3] = cloudShadowLayerRTVs[layer][side];
	} else {
		rtvs[3] = cubemapCloudOccRTVs[side];
	}

	context->OMSetRenderTargets(4, rtvs, nullptr);

	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	UINT sampleMask = 0xffffffff;

	context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);

	auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
	context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);

	for (int i = 0; i < 3; ++i) {
		if (rtvs[i])
			rtvs[i]->Release();
	}
	if (dsv)
		dsv->Release();

	if (CloudRelightEnabled())
		renderedLayersMask[side] |= 1u << layer;

	overrideSky = false;
}

int CloudShadows::FindCloudLayer(RE::BSRenderPass* Pass)
{
	auto sky = globals::game::sky;
	if (!Pass || !sky || !sky->clouds)
		return -1;

	for (int i = 0; i < kMaxCloudLayers; i++) {
		if (sky->clouds->clouds[i].get() == Pass->geometry)
			return i;
	}
	return -1;
}

void CloudShadows::ModifySky(RE::BSRenderPass* Pass)
{
	auto shadowState = globals::game::shadowState;

	GET_INSTANCE_MEMBER(cubeMapRenderTarget, shadowState);

	if (!Pass || !Pass->shaderProperty)
		return;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS)
		return;

	if (!CloudRelightEnabled()) {
		if (cubeMapRenderTarget == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS)
			overrideSky = true;
		return;
	}

	const int layer = FindCloudLayer(Pass);
	if (layer < 0)
		return;

	if (cubeMapRenderTarget == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS) {
		currentLayerForDraw = layer;
		overrideSky = true;
	} else {
		auto context = globals::d3d::context;
		ID3D11ShaderResourceView* srv = texCloudShadowLayers[layer]->srv.get();
		context->PSSetShaderResources(26, 1, &srv);
	}
}

void CloudShadows::ReflectionsPrepass()
{
	Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
			!globals::game::sky->currentClimate)
			return;

		auto context = globals::d3d::context;

		if (CloudRelightEnabled()) {
			context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get());
		} else {
			context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCubemapCloudOcc->resource.get());
		}

		ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(25, 1, &srv);
		context->CSSetShaderResources(25, 1, &srv);
	}
}

void CloudShadows::EarlyPrepass()
{
	if (CloudRelightEnabled()) {
		if (previouslyRenderedSide >= 0) {
			PropagateToCompletion(previouslyRenderedSide);
			previouslyRenderedSide = -1;
		}
		return;
	}

	if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
		!globals::game::sky->currentClimate)
		return;

	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* srv = texCubemapCloudOcc->srv.get();
	context->PSSetShaderResources(25, 1, &srv);
	context->CSSetShaderResources(25, 1, &srv);
}

void CloudShadows::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};

		reflections.texture->GetDesc(&texDesc);
		reflections.SRV->GetDesc(&srvDesc);

		texDesc.Format = srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		cubemapMipLevels = texDesc.MipLevels;

		texCubemapCloudOcc = new Texture2D(texDesc, "CloudShadows::CubemapCloudOcc");
		texCubemapCloudOcc->CreateSRV(srvDesc);

		for (int i = 0; i < 6; ++i) {
			reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
			rtvDesc.Format = texDesc.Format;
			DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOcc->resource.get(), &rtvDesc, cubemapCloudOccRTVs + i));
			Util::SetResourceName(cubemapCloudOccRTVs[i], "CloudShadows::CubemapCloudOcc RTV[%d]", i);
		}

		if (globals::features::cloudRelight.loaded) {
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
				}
			}
		}

		texCubemapCloudOccCopy = new Texture2D(texDesc, "CloudShadows::CubemapCloudOccCopy");
		texCubemapCloudOccCopy->CreateSRV(srvDesc);

		if (globals::features::cloudRelight.loaded) {
			texSelfShadowCopy = new Texture2D(texDesc, "CloudShadows::SelfShadowCopy");
			texSelfShadowCopy->CreateSRV(srvDesc);
		} else {
			for (int i = 0; i < 6; ++i) {
				reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
				rtvDesc.Format = texDesc.Format;
				DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOccCopy->resource.get(), &rtvDesc, cubemapCloudOccCopyRTVs + i));
				Util::SetResourceName(cubemapCloudOccCopyRTVs[i], "CloudShadows::CubemapCloudOccCopy RTV[%d]", i);
			}
		}
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
}

void CloudShadows::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::state->UpdateSkyShaderPermutation(Pass);
	globals::features::cloudShadows.ModifySky(Pass);
	func(This, Pass, RenderFlags);
}

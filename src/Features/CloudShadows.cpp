#include "CloudShadows.h"

#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Opacity)

void CloudShadows::DrawSettings()
{
	ImGui::SliderFloat("Opacity", &settings.Opacity, 0.0f, 1.0f, "%.1f");
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

void CloudShadows::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	if (previouslyRenderedSide >= 0 && previouslyRenderedSide != side)
		PropagateToCompletion(previouslyRenderedSide);
	previouslyRenderedSide = side;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(cloudShadowLayerRTVs[0][side], black);
	renderedLayersMask[side] = 0;
}

void CloudShadows::PropagateToCompletion(int side)
{
	uint32_t mask = renderedLayersMask[side];
	unsigned long highBit;
	int fromLayer = _BitScanReverse(&highBit, mask) ? static_cast<int>(highBit) : 0;
	if (fromLayer >= kMaxCloudLayers - 1)
		return;
	UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
	auto context = globals::d3d::context;
	for (int i = fromLayer + 1; i < kMaxCloudLayers; i++) {
		context->CopySubresourceRegion(
			texCloudShadowLayers[i]->resource.get(), subresource, 0, 0, 0,
			texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
	}
}

void CloudShadows::SkyShaderHacks()
{
	if (overrideSky) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		ID3D11RenderTargetView* rtvs[4];
		ID3D11DepthStencilView* dsv;
		context->OMGetRenderTargets(3, rtvs, &dsv);

		int side = -1;
		for (int i = 0; i < 6; ++i)
			if (rtvs[0] == reflections.cubeSideRTV[i]) {
				side = i;
				break;
			}
		if (side == -1)
			return;

		CheckResourcesSide(side);

		int layer = currentLayerForDraw;

		unsigned long highBit;
		int prevLayer = _BitScanReverse(&highBit, renderedLayersMask[side]) ? static_cast<int>(highBit) : -1;

		UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);

		int fromLayer = std::max(prevLayer, 0);
		for (int gap = fromLayer + 1; gap < layer; gap++) {
			context->CopySubresourceRegion(
				texCloudShadowLayers[gap]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
		}

		if (layer > 0) {
			context->CopySubresourceRegion(
				texCloudShadowLayers[layer]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[layer - 1]->resource.get(), subresource, nullptr);

			ID3D11ShaderResourceView* prevSrv = texCloudShadowLayers[layer - 1]->srv.get();
			context->PSSetShaderResources(26, 1, &prevSrv);
		} else {
			ID3D11ShaderResourceView* nullSrv = nullptr;
			context->PSSetShaderResources(26, 1, &nullSrv);
		}

		rtvs[3] = cloudShadowLayerRTVs[layer][side];
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

		renderedLayersMask[side] |= (1u << layer);

		overrideSky = false;
	}
}

int CloudShadows::FindCloudLayer(RE::BSRenderPass* Pass)
{
	auto sky = globals::game::sky;
	if (!sky || !sky->clouds)
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

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS)
		return;

	int layer = FindCloudLayer(Pass);
	if (layer < 0)
		return;

	if (cubeMapRenderTarget == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS) {
		currentLayerForDraw = layer;
		overrideSky = true;
	} else {
		auto context = globals::d3d::context;
		if (layer > 0) {
			ID3D11ShaderResourceView* srv = texCloudShadowLayers[layer - 1]->srv.get();
			context->PSSetShaderResources(26, 1, &srv);
		} else {
			ID3D11ShaderResourceView* nullSrv = nullptr;
			context->PSSetShaderResources(26, 1, &nullSrv);
		}
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

		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get());

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

		texCubemapCloudOccCopy = new Texture2D(texDesc, "CloudShadows::CubemapCloudOccCopy");
		texCubemapCloudOccCopy->CreateSRV(srvDesc);
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

#include "CloudShadows.h"

#include "../I18n/I18n.h"
#include "Effects11.h"
#include "Effects11/SettingManager.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

#define I18N_KEY_PREFIX "feature.cloud_shadows."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Opacity)

void CloudShadows::DrawSettings()
{
	if (globals::features::effects11.loaded) {
		auto& enb = globals::features::effects11;
		if (enb.enableEffect) {
			ImGui::TextColored(globals::menu->GetSettings().Theme.StatusPalette.Warning, "%s", T("common.settings_managed_by_enb", "Settings are currently managed by ENB."));
			return;
		}
	}

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

	if (globals::features::effects11.loaded) {
		auto& enb = globals::features::effects11;
		if (enb.enableEffect) {
			auto& settingManager = SettingManager::GetSingleton();
			if (settingManager.GetValue<bool>("EnableCloudShadows", "EFFECT")) {
				data.Opacity = settingManager.GetInterpolatedTimeOfDayValue("Amount", "CLOUDSHADOWS");
			} else {
				data.Opacity = 0.0f;
			}
		}
	}

	return data;
}

void CloudShadows::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	if (previouslyRenderedSide >= 0 && previouslyRenderedSide != side)
		PropagateToCompletion(previouslyRenderedSide);
	previouslyRenderedSide = side;
	chainLastDeck[side] = -1;
}

void CloudShadows::PropagateToCompletion(int side)
{
	auto context = globals::d3d::context;

	int deck = chainLastDeck[side];
	auto* source = deck >= 0 ? texOcclusionChain[deck]->resource.get() : texOcclusionBase->resource.get();

	UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
	context->CopySubresourceRegion(
		texCubemapCloudOcc->resource.get(), subresource, 0, 0, 0,
		source, subresource, nullptr);
}

void CloudShadows::SkyShaderHacks()
{
	if (!reflectionSkyDraw)
		return;
	reflectionSkyDraw = false;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	ID3D11RenderTargetView* rtvs[4] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	context->OMGetRenderTargets(3, rtvs, &dsv);

	int side = -1;
	for (int i = 0; i < 6; ++i)
		if (rtvs[0] == reflections.cubeSideRTV[i]) {
			side = i;
			break;
		}

	if (side >= 0)
		CheckResourcesSide(side);

	if (side >= 0 && overrideSky) {
		int deck = currentDeckForDraw;
		assert(deck >= 0 && deck < kMaxCloudDecks);
		int previousDeck = chainLastDeck[side];

		// A deck that draws twice for one face keeps accumulating in place, and must
		// not copy a resource onto itself.
		if (previousDeck != deck) {
			auto* source = previousDeck >= 0 ? texOcclusionChain[previousDeck]->resource.get() : texOcclusionBase->resource.get();
			UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
			context->CopySubresourceRegion(
				texOcclusionChain[deck]->resource.get(), subresource, 0, 0, 0,
				source, subresource, nullptr);
		}

		rtvs[3] = occlusionChainRTVs[deck][side];
		context->OMSetRenderTargets(4, rtvs, nullptr);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);

		auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
		context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);

		chainLastDeck[side] = deck;
	}
	overrideSky = false;

	// rtvs[3] is ours and was never referenced, so it is excluded from the release loop.
	for (int i = 0; i < 3; ++i) {
		if (rtvs[i])
			rtvs[i]->Release();
	}
	if (dsv)
		dsv->Release();
}

int CloudShadows::FindCloudDeck(RE::BSRenderPass* Pass)
{
	auto sky = globals::game::sky;
	if (!sky || !sky->clouds)
		return -1;

	// Slots past numLayers can still hold pointers left over from a previous weather.
	int deckCount = std::min(static_cast<int>(sky->clouds->numLayers), kMaxCloudDecks);
	for (int i = 0; i < deckCount; i++) {
		if (sky->clouds->clouds[i].get() == Pass->geometry)
			return i;
	}
	return -1;
}

void CloudShadows::ModifySky(RE::BSRenderPass* Pass)
{
	auto shadowState = globals::game::shadowState;

	auto& cubeMapRenderTarget = shadowState->GetRuntimeData().cubeMapRenderTarget;

	if (cubeMapRenderTarget != RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS)
		return;

	// Any sky draw starts its face's chain, so a face that loses its clouds publishes the empty base.
	reflectionSkyDraw = true;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);
	if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS)
		return;

	int deck = FindCloudDeck(Pass);
	if (deck < 0)
		return;

	currentDeckForDraw = deck;
	overrideSky = true;
}

void CloudShadows::ReflectionsPrepass()
{
	auto sky = globals::game::sky;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->currentClimate)
		return;

	auto context = globals::d3d::context;

	context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCubemapCloudOcc->resource.get());

	ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
	context->PSSetShaderResources(25, 1, &srv);
	context->CSSetShaderResources(25, 1, &srv);
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

		auto createFaceRTVs = [&](Texture2D* texture, ID3D11RenderTargetView*(&faceRTVs)[6], const char* name) {
			for (int face = 0; face < 6; ++face) {
				reflections.cubeSideRTV[face]->GetDesc(&rtvDesc);
				rtvDesc.Format = texDesc.Format;
				DX::ThrowIfFailed(device->CreateRenderTargetView(texture->resource.get(), &rtvDesc, &faceRTVs[face]));
				Util::SetResourceName(faceRTVs[face], "%s RTV[%d]", name, face);
			}
		};

		texOcclusionBase = new Texture2D(texDesc, "CloudShadows::OcclusionBase");
		ID3D11RenderTargetView* baseRTVs[6] = {};
		createFaceRTVs(texOcclusionBase, baseRTVs, "CloudShadows::OcclusionBase");

		// Nothing draws into the base, so one clear keeps it zeroed for good.
		float black[4] = { 0, 0, 0, 0 };
		for (auto* rtv : baseRTVs) {
			context->ClearRenderTargetView(rtv, black);
			rtv->Release();
		}

		for (int deck = 0; deck < kMaxCloudDecks; ++deck) {
			char name[64];
			snprintf(name, sizeof(name), "CloudShadows::OcclusionChain[%d]", deck);
			texOcclusionChain[deck] = new Texture2D(texDesc, name);
			createFaceRTVs(texOcclusionChain[deck], occlusionChainRTVs[deck], name);
		}

		texCubemapCloudOcc = new Texture2D(texDesc, "CloudShadows::CubemapCloudOcc");

		texCubemapCloudOccCopy = new Texture2D(texDesc, "CloudShadows::CubemapCloudOccCopy");
		texCubemapCloudOccCopy->CreateSRV(srvDesc);

		// Faces are only written as the engine gets round to rendering them, so start
		// both cleared rather than letting undrawn faces serve whatever was in memory.
		context->CopyResource(texCubemapCloudOcc->resource.get(), texOcclusionBase->resource.get());
		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texOcclusionBase->resource.get());
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

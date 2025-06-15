#include "CloudShadows.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Opacity)

/**
 * @brief Renders ImGui controls for cloud shadow settings.
 *
 * This function displays a slider to control the opacity of the cloud shadows.
 */
void CloudShadows::DrawSettings()
{
	ImGui::SliderFloat("Opacity", &settings.Opacity, 0.0f, 1.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Higher values make cloud shadows darker.");
	}
}

/**
 * @brief Loads cloud shadow settings from a JSON object.
 *
 * This function assigns the provided JSON object to the internal settings structure.
 * It expects the JSON to contain all necessary fields for CloudShadows::Settings.
 *
 * @param o_json Reference to a JSON object containing the settings to load.
 *
 * @note If the JSON object is missing required fields, default values (if any) will be used.
 * @see CloudShadows::Settings
 */
void CloudShadows::LoadSettings(json& o_json)
{
    settings = o_json;
}

/**
 * @brief Saves current cloud shadow settings to a JSON object.
 *
 * This function serializes the internal settings structure into the provided
 * JSON object.
 *
 * @param o_json Reference to a JSON object where settings will be saved.
 * @see CloudShadows::Settings
 */
void CloudShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

/**
 * @brief Resets cloud shadow settings to their default values.
 *
 * This function reinitializes the internal settings structure to its default state.
 * @see CloudShadows::Settings
 */
void CloudShadows::RestoreDefaultSettings()
{
	settings = {};
}

/**
 * @brief Checks and prepares resources for a specific cubemap side.
 *
 * This function ensures that rendering operations for a given cubemap side
 * are performed only once per frame. It clears the render target view
 * for the specified side of the cloud occlusion cubemap.
 *
 * @param side The index of the cubemap side (0-5) to check and prepare.
 */
void CloudShadows::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(cubemapCloudOccRTVs[side], black);
}

/**
 * @brief Applies modifications to the sky rendering pipeline for cloud shadows.
 *
 * This function is called when `overrideSky` is true. It sets the appropriate
 * render targets (including the cloud occlusion cubemap side) and blend state
 * to allow cloud shadows to be rendered onto the sky cubemap. It also sets
 * the cubemap depth stencil view as a shader resource.
 */
void CloudShadows::SkyShaderHacks()
{
	if (overrideSky) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		// render targets
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

		rtvs[3] = cubemapCloudOccRTVs[side];
		context->OMSetRenderTargets(4, rtvs, nullptr);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);

		auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
		context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);

		overrideSky = false;
	}
}

/**
 * @brief Modifies sky rendering properties based on the render pass.
 *
 * This function checks if the current render pass is for sky reflections and if
 * the sky object being rendered is clouds. If so, it sets `overrideSky` to true,
 * signaling `SkyShaderHacks` to apply modifications for cloud shadow rendering.
 *
 * @param Pass Pointer to the current `RE::BSRenderPass`.
 */
void CloudShadows::ModifySky(RE::BSRenderPass* Pass)
{
	auto shadowState = globals::game::shadowState;

	GET_INSTANCE_MEMBER(cubeMapRenderTarget, shadowState);

	if (cubeMapRenderTarget != RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS)
		return;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS) {
		overrideSky = true;
	}
}

/**
 * @brief Performs prepass operations for reflections related to cloud shadows.
 *
 * This function is called once per frame before reflection rendering. If the sky
 * is fully active, it copies the main cloud occlusion cubemap to a temporary
 * cubemap and sets the temporary cubemap as a pixel and compute shader resource.
 * This ensures that reflections use a consistent state of cloud shadows for the frame.
 */
void CloudShadows::ReflectionsPrepass()
{
	Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
			!globals::game::sky->currentClimate)
			return;

		auto context = globals::d3d::context;

		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCubemapCloudOcc->resource.get());

		ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(25, 1, &srv);
		context->CSSetShaderResources(25, 1, &srv);
	}
}

/**
 * @brief Performs early prepass operations for cloud shadows.
 *
 * This function is called early in the frame. If the sky is fully active,
 * it sets the main cloud occlusion cubemap as a pixel and compute shader resource.
 */
void CloudShadows::EarlyPrepass()
{
	if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
		!globals::game::sky->currentClimate)
		return;

	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* srv = texCubemapCloudOcc->srv.get();
	context->PSSetShaderResources(25, 1, &srv);
	context->CSSetShaderResources(25, 1, &srv);
}

/**
 * @brief Initializes and sets up D3D11 resources required for cloud shadows.
 *
 * This function creates the necessary textures (cubemap for cloud occlusion and its copy),
 * their shader resource views, and render target views for each cubemap face.
 * It also creates a custom blend state used for rendering cloud shadows.
 */
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

		texCubemapCloudOcc = new Texture2D(texDesc);
		texCubemapCloudOcc->CreateSRV(srvDesc);

		for (int i = 0; i < 6; ++i) {
			reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
			rtvDesc.Format = texDesc.Format;
			DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOcc->resource.get(), &rtvDesc, cubemapCloudOccRTVs + i));
		}

		texCubemapCloudOccCopy = new Texture2D(texDesc);
		texCubemapCloudOccCopy->CreateSRV(srvDesc);

		for (int i = 0; i < 6; ++i) {
			reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
			rtvDesc.Format = texDesc.Format;
			DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOccCopy->resource.get(), &rtvDesc, cubemapCloudOccCopyRTVs + i));
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
	}
}

/**
 * @brief Hook for the BSSkyShader::SetupMaterial method.
 *
 * This thunk calls `CloudShadows::ModifySky` before invoking the original
 * `BSSkyShader::SetupMaterial` function. This allows interception and modification
 * of the sky rendering setup to integrate cloud shadows.
 *
 * @param This Pointer to the `RE::BSShader` instance.
 * @param Pass Pointer to the `RE::BSRenderPass` being processed.
 * @param RenderFlags Flags associated with the rendering operation.
 */
void CloudShadows::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::cloudShadows->ModifySky(Pass);
	func(This, Pass, RenderFlags);
}

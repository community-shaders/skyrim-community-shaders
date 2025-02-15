#include "SubsurfaceScattering.h"

#include "Deferred.h"
#include "Features/TerrainBlending.h"
#include "ShaderCache.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubsurfaceScattering::DiffusionProfile,
	BlurRadius, Thickness, Strength, Falloff)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SubsurfaceScattering::Settings,
	EnableCharacterLighting,
	BaseProfile,
	HumanProfile)

void SubsurfaceScattering::DrawSettings()
{
	if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Character Lighting", (bool*)&settings.EnableCharacterLighting);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Vanilla feature, not recommended.");
		}

		if (ImGui::TreeNodeEx("Base Profile", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Blur Radius", &settings.BaseProfile.BlurRadius, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Blur radius.");
			}

			ImGui::SliderFloat("Thickness", &settings.BaseProfile.Thickness, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Blur radius relative to depth.");
			}

			updateKernels = updateKernels || ImGui::ColorEdit3("Strength", (float*)&settings.BaseProfile.Strength);
			updateKernels = updateKernels || ImGui::ColorEdit3("Falloff", (float*)&settings.BaseProfile.Falloff);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Human Profile", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Blur Radius", &settings.HumanProfile.BlurRadius, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Blur radius.");
			}

			ImGui::SliderFloat("Thickness", &settings.HumanProfile.Thickness, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Blur radius relative to depth.");
			}

			updateKernels = updateKernels || ImGui::ColorEdit3("Strength", (float*)&settings.HumanProfile.Strength);
			updateKernels = updateKernels || ImGui::ColorEdit3("Falloff", (float*)&settings.HumanProfile.Falloff);

			ImGui::TreePop();
		}

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TreePop();
	}
}

float3 SubsurfaceScattering::Gaussian(DiffusionProfile& a_profile, float variance, float r)
{
	/**
     * We use a falloff to modulate the shape of the profile. Big falloffs
     * spreads the shape making it wider, while small falloffs make it
     * narrower.
     */
	float falloff[3] = { a_profile.Falloff.x, a_profile.Falloff.y, a_profile.Falloff.z };
	float g[3];
	for (int i = 0; i < 3; i++) {
		float rr = r / (0.001f + falloff[i]);
		g[i] = exp((-(rr * rr)) / (2.0f * variance)) / (2.0f * 3.14f * variance);
	}
	return float3(g[0], g[1], g[2]);
}

float3 SubsurfaceScattering::Profile(DiffusionProfile& a_profile, float r)
{
	/**
     * We used the red channel of the original skin profile defined in
     * [d'Eon07] for all three channels. We noticed it can be used for green
     * and blue channels (scaled using the falloff parameter) without
     * introducing noticeable differences and allowing for total control over
     * the profile. For example, it allows to create blue SSS gradients, which
     * could be useful in case of rendering blue creatures.
     */
	return  // 0.233f * gaussian(0.0064f, r) + /* We consider this one to be directly bounced light, accounted by the strength parameter (see @STRENGTH) */
		0.100f * Gaussian(a_profile, 0.0484f, r) +
		0.118f * Gaussian(a_profile, 0.187f, r) +
		0.113f * Gaussian(a_profile, 0.567f, r) +
		0.358f * Gaussian(a_profile, 1.99f, r) +
		0.078f * Gaussian(a_profile, 7.41f, r);
}

void SubsurfaceScattering::CalculateKernel(DiffusionProfile& a_profile, Kernel& kernel)
{
	uint nSamples = SSSS_N_SAMPLES;

	const float RANGE = nSamples > 20 ? 3.0f : 2.0f;
	const float EXPONENT = 2.0f;

	// Calculate the offsets:
	float step = 2.0f * RANGE / (nSamples - 1);
	for (uint i = 0; i < nSamples; i++) {
		float o = -RANGE + float(i) * step;
		float sign = o < 0.0f ? -1.0f : 1.0f;
		kernel.Sample[i].w = RANGE * sign * abs(pow(o, EXPONENT)) / pow(RANGE, EXPONENT);
	}

	// Calculate the weights:
	for (uint i = 0; i < nSamples; i++) {
		float w0 = i > 0 ? abs(kernel.Sample[i].w - kernel.Sample[i - 1].w) : 0.0f;
		float w1 = i < nSamples - 1 ? abs(kernel.Sample[i].w - kernel.Sample[i + 1].w) : 0.0f;
		float area = (w0 + w1) / 2.0f;
		float3 t = area * Profile(a_profile, kernel.Sample[i].w);
		kernel.Sample[i].x = t.x;
		kernel.Sample[i].y = t.y;
		kernel.Sample[i].z = t.z;
	}

	// We want the offset 0.0 to come first:
	float4 t = kernel.Sample[nSamples / 2];
	for (uint i = nSamples / 2; i > 0; i--)
		kernel.Sample[i] = kernel.Sample[i - 1];
	kernel.Sample[0] = t;

	// Calculate the sum of the weights, we will need to normalize them below:
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	for (uint i = 0; i < nSamples; i++)
		sum += float3(kernel.Sample[i]);

	// Normalize the weights:
	for (uint i = 0; i < nSamples; i++) {
		kernel.Sample[i].x /= sum.x;
		kernel.Sample[i].y /= sum.y;
		kernel.Sample[i].z /= sum.z;
	}

	// Tweak them using the desired strength. The first one is:
	//     lerp(1.0, kernel[0].rgb, strength)
	kernel.Sample[0].x = (1.0f - a_profile.Strength.x) * 1.0f + a_profile.Strength.x * kernel.Sample[0].x;
	kernel.Sample[0].y = (1.0f - a_profile.Strength.y) * 1.0f + a_profile.Strength.y * kernel.Sample[0].y;
	kernel.Sample[0].z = (1.0f - a_profile.Strength.z) * 1.0f + a_profile.Strength.z * kernel.Sample[0].z;

	// The others:
	//     lerp(0.0, kernel[0].rgb, strength)
	for (uint i = 1; i < nSamples; i++) {
		kernel.Sample[i].x *= a_profile.Strength.x;
		kernel.Sample[i].y *= a_profile.Strength.y;
		kernel.Sample[i].z *= a_profile.Strength.z;
	}
}

void SubsurfaceScattering::DrawSSS()
{
	if (!validMaterials)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering");

	validMaterials = false;

	auto dispatchCount = Util::GetScreenDispatchCount();

	{
		auto cameraData = Util::GetCameraData(0);

		blurCBData.SSSS_FOVY = atan(1.0f / cameraData.projMat.m[0][0]) * 2.0f * (180.0f / 3.14159265359f);

		blurCBData.BaseProfile = { settings.BaseProfile.BlurRadius, settings.BaseProfile.Thickness, 0, 0 };
		blurCBData.HumanProfile = { settings.HumanProfile.BlurRadius, settings.HumanProfile.Thickness, 0, 0 };

		blurCB->Update(blurCBData);
	}

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffer[1] = { blurCB->CB() };
		context->CSSetConstantBuffers(1, 1, buffer);

		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		auto mask = renderer->GetRuntimeData().renderTargets[MASKS];

		ID3D11UnorderedAccessView* uav = blurHorizontalTemp->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		auto terrainBlending = globals::features::terrainBlending;

		ID3D11ShaderResourceView* views[3];
		views[0] = main.SRV;
		views[1] = terrainBlending->loaded ? terrainBlending->blendedDepthTexture16->srv.get() : depth.depthSRV,
		views[2] = mask.SRV;

		context->CSSetShaderResources(0, 3, views);

		// Horizontal pass to temporary texture
		{
			TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Horizontal");

			auto shader = GetComputeShaderHorizontalBlur();
			context->CSSetShader(shader, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		// Vertical pass to main texture
		{
			TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Vertical");

			views[0] = blurHorizontalTemp->srv.get();
			context->CSSetShaderResources(0, 1, views);

			ID3D11UnorderedAccessView* uavs[1] = { main.UAV };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

			auto shader = GetComputeShaderVerticalBlur();
			context->CSSetShader(shader, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}
	}

	ID3D11Buffer* buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);

	ID3D11ShaderResourceView* views[3]{ nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, 3, views);

	ID3D11UnorderedAccessView* uavs[1]{ nullptr };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
}

void SubsurfaceScattering::SetupResources()
{
	{
		blurCB = new ConstantBuffer(ConstantBufferDesc<BlurCB>());
	}

	auto renderer = globals::game::renderer;

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);

		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		main.SRV->GetDesc(&srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		main.UAV->GetDesc(&uavDesc);

		blurHorizontalTemp = new Texture2D(texDesc);
		blurHorizontalTemp->CreateSRV(srvDesc);
		blurHorizontalTemp->CreateUAV(uavDesc);
	}
}

void SubsurfaceScattering::Reset()
{
	auto shaderManager = globals::game::smState;
	auto shaderCache = globals::shaderCache;
	shaderManager->characterLightEnabled = shaderCache->IsEnabled() ? settings.EnableCharacterLighting : true;

	if (updateKernels) {
		updateKernels = false;
		CalculateKernel(settings.BaseProfile, blurCBData.BaseKernel);
		CalculateKernel(settings.HumanProfile, blurCBData.HumanKernel);
	}
}

void SubsurfaceScattering::RestoreDefaultSettings()
{
	settings = {};
}

void SubsurfaceScattering::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SubsurfaceScattering::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SubsurfaceScattering::ClearShaderCache()
{
	if (horizontalSSBlur) {
		horizontalSSBlur->Release();
		horizontalSSBlur = nullptr;
	}
	if (verticalSSBlur) {
		verticalSSBlur->Release();
		verticalSSBlur = nullptr;
	}
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderHorizontalBlur()
{
	if (!horizontalSSBlur) {
		logger::debug("Compiling horizontalSSBlur");
		horizontalSSBlur = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl", { { "HORIZONTAL", "" } }, "cs_5_0");
	}
	return horizontalSSBlur;
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderVerticalBlur()
{
	if (!verticalSSBlur) {
		logger::debug("Compiling verticalSSBlur");
		verticalSSBlur = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl", {}, "cs_5_0");
	}
	return verticalSSBlur;
}

void SubsurfaceScattering::DataLoaded()
{
	isBeastRaceKeyword = RE::TESForm::LookupByEditorID("IsBeastRace")->As<RE::BGSKeyword>();
}

void SubsurfaceScattering::PostPostLoad()
{
	Hooks::Install();
}

void SubsurfaceScattering::BSLightingShader_SetupSkin(RE::BSRenderPass* a_pass)
{
	auto deferred = globals::deferred;
	auto state = globals::state;

	if (deferred->deferredPass) {
		if (a_pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFace, RE::BSShaderProperty::EShaderPropertyFlag::kFaceGenRGBTint)) {
			bool isBeastRace = true;

			auto geometry = a_pass->geometry;
			if (auto userData = geometry->GetUserData())
				if (auto actor = userData->As<RE::Actor>())
					if (auto race = actor->GetRace())
						isBeastRace = race->HasKeyword(isBeastRaceKeyword);

			validMaterials = true;

			if (isBeastRace)
				state->currentExtraDescriptor |= (uint)State::ExtraShaderDescriptors::IsBeastRace;
		}
	}
}

void SubsurfaceScattering::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::subsurfaceScattering->BSLightingShader_SetupSkin(Pass);
	func(This, Pass, RenderFlags);
}

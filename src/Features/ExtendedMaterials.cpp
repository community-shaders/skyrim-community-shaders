#include "ExtendedMaterials.h"

#include "Deferred.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedMaterials::Settings,
	EnableComplexMaterial,
	EnableParallax,
	EnableTerrain,
	EnableHeightBlending,
	DisplacementScale)

void ExtendedMaterials::DataLoaded()
{
	if (&settings.EnableTerrain) {
		if (auto bLandSpecular = globals::game::iniSettingCollection->GetSetting("bLandSpecular:Landscape"); bLandSpecular) {
			if (!bLandSpecular->data.b) {
				logger::info("[CPM] Changing bLandSpecular from {} to {} to support Terrain Parallax", bLandSpecular->data.b, true);
				bLandSpecular->data.b = true;
			}
		}
	}
}

void ExtendedMaterials::DrawSettings()
{
	if (ImGui::TreeNodeEx("Complex Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Complex Material", (bool*)&settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables support for the Complex Material specification which makes use of the environment mask. "
				"This includes parallax, as well as more realistic metals and specular reflections. "
				"May lead to some warped textures on modded content which have an invalid alpha channel in their environment mask. ");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Screen Space Displacement", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Displacement", (bool*)&settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables screen-space displacement mapping (SSDM) on meshes and terrain.");
		}

		if (ImGui::Checkbox("Enable Legacy Terrain", (bool*)&settings.EnableTerrain)) {
			if (settings.EnableTerrain) {
				DataLoaded();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables terrain parallax using the alpha channel of each landscape texture. "
				"Therefore, all landscape textures must support parallax for this effect to work properly. ");
		}
		ImGui::Checkbox("Enable Terrain Height Blending", (bool*)&settings.EnableHeightBlending);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables landscape texture blending based on height. ");
		}
		ImGui::SliderFloat("Displacement Scale", &settings.DisplacementScale, 0.001f, 0.2f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Controls the strength of screen-space displacement. Higher values produce more dramatic depth.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE_TITLE(texDisplacement, "Displacement", debugRescale);

		for (int i = 0; i < SSDM_MIP_LEVELS; ++i) {
			if (texSSDMLevel[i] && texSSDMLevel[i]->srv.get()) {
				char buf[128];
				snprintf(buf, sizeof(buf), "SSDM Level %d (%ux%u)", i, texSSDMLevel[i]->desc.Width, texSSDMLevel[i]->desc.Height);
				if (ImGui::TreeNode(buf)) {
					ImGui::Image(texSSDMLevel[i]->srv.get(), { texSSDMLevel[i]->desc.Width * debugRescale, texSSDMLevel[i]->desc.Height * debugRescale });
					ImGui::TreePop();
				}
			}
		}

		ImGui::TreePop();
	}
}

void ExtendedMaterials::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ExtendedMaterials::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ExtendedMaterials::RestoreDefaultSettings()
{
	settings = {};
}

bool ExtendedMaterials::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Lighting:
		return true;
	default:
		return false;
	}
}

void ExtendedMaterials::SetupResources()
{
	auto device = globals::d3d::device;
	auto renderer = globals::game::renderer;
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainDesc{};
	mainTex.texture->GetDesc(&mainDesc);

	uint w = mainDesc.Width;
	uint h = mainDesc.Height;

	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = w,
			.Height = h,
			.MipLevels = SSDM_MIP_LEVELS,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		texDisplacement = eastl::make_unique<Texture2D>(texDesc);
		texDisplacement->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = SSDM_MIP_LEVELS } });

		CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(D3D11_RTV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R16G16_FLOAT, 0);
		DX::ThrowIfFailed(device->CreateRenderTargetView(texDisplacement->resource.get(), &rtvDesc, rtvDisplacement.put()));

		for (int i = 0; i < SSDM_MIP_LEVELS; ++i) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC mipUav = {
				.Format = DXGI_FORMAT_R16G16_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = (UINT)i }
			};
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texDisplacement->resource.get(), &mipUav, uavDisplacement[i].put()));
		}
	}

	for (int i = 0; i < SSDM_MIP_LEVELS; ++i) {
		D3D11_TEXTURE2D_DESC levelDesc = {
			.Width = std::max(1u, w >> i),
			.Height = std::max(1u, h >> i),
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		texSSDMLevel[i] = eastl::make_unique<Texture2D>(levelDesc);
		texSSDMLevel[i]->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		texSSDMLevel[i]->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	ssdmCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSDMCB>());

	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, ssdmLinearSampler.put()));
	}

	ClearShaderCache();
}

void ExtendedMaterials::ClearShaderCache()
{
	ssdmBuildPyramidCS = nullptr;
	ssdmDisplaceCS = nullptr;

	auto shaderDir = std::filesystem::path("Data\\Shaders\\ExtendedMaterials");

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(
			Util::CompileShader((shaderDir / "SSDMBuildPyramidCS.hlsl").c_str(), {}, "cs_5_0")))
		ssdmBuildPyramidCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(
			Util::CompileShader((shaderDir / "SSDMDisplaceCS.hlsl").c_str(), {}, "cs_5_0")))
		ssdmDisplaceCS.attach(rawPtr);
}

void ExtendedMaterials::RegisterDisplacementRT()
{
	if (!texDisplacement || !rtvDisplacement)
		return;
	auto renderer = globals::game::renderer;
	auto& rt = renderer->GetRuntimeData().renderTargets[SSDM_DISPLACEMENT];
	rt.texture = texDisplacement->resource.get();
	rt.SRV = texDisplacement->srv.get();
	rt.RTV = rtvDisplacement.get();
}

ID3D11ShaderResourceView* ExtendedMaterials::GetSSDMOffsetSRV() const
{
	return (texSSDMLevel[0] && settings.EnableParallax) ? texSSDMLevel[0]->srv.get() : nullptr;
}

void ExtendedMaterials::ClearDisplacementTexture()
{
	if (!rtvDisplacement)
		return;
	const float clearColor[4] = { 0, 0, 0, 0 };
	globals::d3d::context->ClearRenderTargetView(rtvDisplacement.get(), clearColor);
}

void ExtendedMaterials::DrawSSDM()
{
	if (!settings.EnableParallax)
		return;
	if (!ssdmBuildPyramidCS || !ssdmDisplaceCS)
		return;
	if (!texDisplacement || !texSSDMLevel[0])
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSDM");

	auto context = globals::d3d::context;

	uint w = texDisplacement->desc.Width;
	uint h = texDisplacement->desc.Height;
	int maxMip = SSDM_MIP_LEVELS - 1;

	ID3D11SamplerState* samplers[] = { ssdmLinearSampler.get() };
	context->CSSetSamplers(0, 1, samplers);

	ID3D11Buffer* cbs[] = { ssdmCB->CB() };
	context->CSSetConstantBuffers(0, 1, cbs);

	// Build mip pyramid of displacement vectors (level 0 → levels 1..maxMip)
	context->CSSetShader(ssdmBuildPyramidCS.get(), nullptr, 0);
	for (int mip = 1; mip <= maxMip; ++mip) {
		SSDMCB cb = {};
		cb.FullDimX = (float)w;
		cb.FullDimY = (float)h;
		cb.RcpFullDimX = 1.0f / w;
		cb.RcpFullDimY = 1.0f / h;
		cb.SrcMipLevel = mip - 1;
		ssdmCB->Update(cb);

		ID3D11ShaderResourceView* srvs[] = { texDisplacement->srv.get() };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[] = { uavDisplacement[mip].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		uint dw = std::max(1u, w >> mip);
		uint dh = std::max(1u, h >> mip);
		context->Dispatch((dw + 7) / 8, (dh + 7) / 8, 1);

		ID3D11ShaderResourceView* nullSrv[] = { nullptr };
		ID3D11UnorderedAccessView* nullUav[] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSrv);
		context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
	}

	// Iterative refinement: coarse (maxMip) → fine (0)
	context->CSSetShader(ssdmDisplaceCS.get(), nullptr, 0);
	for (int mip = maxMip; mip >= 0; --mip) {
		SSDMCB cb = {};
		cb.FullDimX = (float)w;
		cb.FullDimY = (float)h;
		cb.RcpFullDimX = 1.0f / w;
		cb.RcpFullDimY = 1.0f / h;
		cb.MipLevel = mip;
		cb.IsCoarsest = (mip == maxMip) ? 1 : 0;
		ssdmCB->Update(cb);

		ID3D11ShaderResourceView* srvs[2] = {
			texDisplacement->srv.get(),
			(mip < maxMip) ? texSSDMLevel[mip + 1]->srv.get() : nullptr
		};
		context->CSSetShaderResources(0, 2, srvs);

		ID3D11UnorderedAccessView* uavs[] = { texSSDMLevel[mip]->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		uint dw = std::max(1u, w >> mip);
		uint dh = std::max(1u, h >> mip);
		context->Dispatch((dw + 7) / 8, (dh + 7) / 8, 1);

		ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUav[] = { nullptr };
		context->CSSetShaderResources(0, 2, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
	}

	// Cleanup
	context->CSSetShader(nullptr, nullptr, 0);
	ID3D11Buffer* nullCb[] = { nullptr };
	context->CSSetConstantBuffers(0, 1, nullCb);
	ID3D11SamplerState* nullSampler[] = { nullptr };
	context->CSSetSamplers(0, 1, nullSampler);
}

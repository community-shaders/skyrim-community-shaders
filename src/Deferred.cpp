#include "Deferred.h"

#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"

#include "Features/DynamicCubemaps.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"

struct DepthStates
{
	ID3D11DepthStencilState* a[6][40];
};

struct BlendStates
{
	ID3D11BlendState* a[7][2][13][2];

	static BlendStates* GetSingleton()
	{
		static auto blendStates = reinterpret_cast<BlendStates*>(REL::RelocationID(524749, 411364).address());
		return blendStates;
	}
};

void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags)
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	texDesc.BindFlags = bindFlags;
	texDesc.Format = format;
	srvDesc.Format = format;
	rtvDesc.Format = format;
	uavDesc.Format = format;

	auto& data = renderer->GetRuntimeData().renderTargets[target];
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, &data.texture));

	if (texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
		DX::ThrowIfFailed(device->CreateShaderResourceView(data.texture, &srvDesc, &data.SRV));

	if (texDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
		DX::ThrowIfFailed(device->CreateRenderTargetView(data.texture, &rtvDesc, &data.RTV));

	if (texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(data.texture, &uavDesc, &data.UAV));
}

void Deferred::SetupResources()
{
	auto renderer = globals::game::renderer;

	{
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		main.texture->GetDesc(&texDesc);
		main.SRV->GetDesc(&srvDesc);
		main.RTV->GetDesc(&rtvDesc);
		main.UAV->GetDesc(&uavDesc);

		// Available targets:
		// MAIN ONLY ALPHA
		// WATER REFLECTIONS
		// BLURFULL_BUFFER
		// LENSFLAREVIS
		// SAO DOWNSCALED
		// SAO CAMERAZ+MIP_LEVEL_0_ESRAM
		// SAO_RAWAO_DOWNSCALED
		// SAO_RAWAO_PREVIOUS_DOWNSCALDE
		// SAO_TEMP_BLUR_DOWNSCALED
		// INDIRECT
		// INDIRECT_DOWNSCALED
		// RAWINDIRECT
		// RAWINDIRECT_DOWNSCALED
		// RAWINDIRECT_PREVIOUS
		// RAWINDIRECT_PREVIOUS_DOWNSCALED
		// RAWINDIRECT_SWAP
		// VOLUMETRIC_LIGHTING_HALF_RES
		// VOLUMETRIC_LIGHTING_BLUR_HALF_RES
		// VOLUMETRIC_LIGHTING_QUARTER_RES
		// VOLUMETRIC_LIGHTING_BLUR_QUARTER_RES
		// TEMPORAL_AA_WATER_1
		// TEMPORAL_AA_WATER_2

		// Albedo
		SetupRenderTarget(ALBEDO, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Specular
		SetupRenderTarget(SPECULAR, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Reflectance
		SetupRenderTarget(REFLECTANCE, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Normal + Roughness
		SetupRenderTarget(NORMALROUGHNESS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks
		SetupRenderTarget(MASKS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	}

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = 1;

		sbDesc.StructureByteStride = sizeof(PerGeometry);
		sbDesc.ByteWidth = sizeof(PerGeometry) * numElements;
		perShadow = new Buffer(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		perShadow->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		perShadow->CreateUAV(uavDesc);

		copyShadowCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CopyShadowDataCS.hlsl", {}, "cs_5_0"));
	}

	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		prevDiffuseAmbientTexture = new Texture2D(texDesc);
		prevDiffuseAmbientTexture->CreateSRV(srvDesc);
		prevDiffuseAmbientTexture->CreateUAV(uavDesc);
	}
}

void Deferred::CopyShadowData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowData");

	auto context = globals::d3d::context;

	ID3D11UnorderedAccessView* uavs[1]{ perShadow->uav.get() };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11Buffer* buffers[3];
	context->PSGetConstantBuffers(0, 3, buffers);
	context->PSGetConstantBuffers(12, 1, buffers + 1);

	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(copyShadowCS, nullptr, 0);

	context->Dispatch(1, 1, 1);

	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	std::fill(buffers, buffers + ARRAYSIZE(buffers), nullptr);
	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(nullptr, nullptr, 0);

	{
		context->PSGetShaderResources(4, 1, &shadowView);

		ID3D11ShaderResourceView* srvs[2]{
			shadowView,
			perShadow->srv.get(),
		};

		context->PSSetShaderResources(18, ARRAYSIZE(srvs), srvs);
	}
}

void Deferred::ReflectionsPrepasses()
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false);

	ZoneScoped;
	TracyD3D11Zone(globals::game::graphicsState->tracyCtx, "Early Prepass");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			feature->ReflectionsPrepass();
		}
	}
}

void Deferred::EarlyPrepasses()
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false);

	ZoneScoped;
	TracyD3D11Zone(globals::game::graphicsState->tracyCtx, "Early Prepass");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			feature->EarlyPrepass();
		}
	}
}

void Deferred::PrepassPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto context = globals::game::renderer->GetRuntimeData().context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	globals::truePBR->PrePass();
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			feature->Prepass();
		}
	}
}

void Deferred::StartDeferred()
{
	globals::state->UpdateSharedData(true);

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(setRenderTargetMode, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

	// Backup original render targets
	for (uint i = 0; i < 4; i++) {
		forwardRenderTargets[i] = renderTargets[i];
	}

	RE::RENDER_TARGET targets[8]{
		RE::RENDER_TARGET::kMAIN,
		RE::RENDER_TARGET::kMOTION_VECTOR,
		NORMALROUGHNESS,
		ALBEDO,
		SPECULAR,
		REFLECTANCE,
		MASKS,
		RE::RENDER_TARGET::kNONE
	};

	for (uint i = 2; i < 8; i++) {
		renderTargets[i] = targets[i];                                             // We must use unused targets to be indexable
		setRenderTargetMode[i] = RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR;  // Dirty from last frame, this calls ClearRenderTargetView once
	}

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = true;

	{
		auto context = globals::d3d::context;

		static REL::Relocation<ID3D11Buffer**> perFrame{ REL::RelocationID(524768, 411384) };
		ID3D11Buffer* buffers[1] = { *perFrame.get() };

		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	PrepassPasses();

	OverrideBlendStates();
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		static REL::Relocation<ID3D11Buffer**> perFrame{ REL::RelocationID(524768, 411384) };
		ID3D11Buffer* buffers[1] = { *perFrame.get() };
		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];

	auto main = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[0]];
	auto normals = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[2]];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto reflectance = renderer->GetRuntimeData().renderTargets[REFLECTANCE];

	auto motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool interior = true;
	if (auto sky = globals::game::sky)
		interior = sky->mode.get() != RE::Sky::Mode::kFull;

	auto skylighting = globals::features::skylighting;

	auto ssgi = globals::features::screenSpaceGI;
	if (ssgi->loaded)
		ssgi->DrawSSGI(prevDiffuseAmbientTexture);
	auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi->GetOutputTextures();
	bool ssgi_hq_spec = ssgi->settings.EnableExperimentalSpecularGI;

	auto dispatchCount = Util::GetScreenDispatchCount();

	if (ssgi->loaded) {
		// Ambient Composite
		{
			TracyD3D11Zone(globals::state->tracyCtx, "Ambient Composite");

			ID3D11ShaderResourceView* srvs[8]{
				albedo.SRV,
				normalRoughness.SRV,
				skylighting->loaded || REL::Module::IsVR() ? depth.depthSRV : nullptr,
				skylighting->loaded ? skylighting->texProbeArray->srv.get() : nullptr,
				skylighting->loaded ? skylighting->stbn_vec3_2Dx1D_128x128x64.get() : nullptr,
				ssgi_ao,
				ssgi_y,
				ssgi_cocg,
			};

			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			ID3D11UnorderedAccessView* uavs[2]{ main.UAV, prevDiffuseAmbientTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			auto shader = interior ? GetComputeAmbientCompositeInterior() : GetComputeAmbientComposite();
			context->CSSetShader(shader, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		// Clear
		{
			ID3D11ShaderResourceView* views[8]{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[2]{ nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(nullptr, nullptr, 0);
		}
	}

	auto sss = globals::features::subsurfaceScattering;
	if (sss->loaded)
		sss->DrawSSS();

	auto dynamicCubemaps = globals::features::dynamicCubemaps;
	if (dynamicCubemaps->loaded)
		dynamicCubemaps->UpdateCubemap();

	auto terrainBlending = globals::features::terrainBlending;

	// Deferred Composite
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite");

		ID3D11ShaderResourceView* srvs[14]{
			specular.SRV,
			albedo.SRV,
			normalRoughness.SRV,
			masks.SRV,
			dynamicCubemaps->loaded || REL::Module::IsVR() ? (terrainBlending->loaded ? terrainBlending->blendedDepthTexture16->srv.get() : depth.depthSRV) : nullptr,
			dynamicCubemaps->loaded ? reflectance.SRV : nullptr,
			dynamicCubemaps->loaded ? dynamicCubemaps->envTexture->srv.get() : nullptr,
			dynamicCubemaps->loaded ? dynamicCubemaps->envReflectionsTexture->srv.get() : nullptr,
			dynamicCubemaps->loaded && skylighting->loaded ? skylighting->texProbeArray->srv.get() : nullptr,
			dynamicCubemaps->loaded && skylighting->loaded ? skylighting->stbn_vec3_2Dx1D_128x128x64.get() : nullptr,
			ssgi_ao,
			ssgi_hq_spec ? nullptr : ssgi_y,
			ssgi_hq_spec ? nullptr : ssgi_cocg,
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,
		};

		if (dynamicCubemaps->loaded)
			context->CSSetSamplers(0, 1, &linearSampler);

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[3]{ main.UAV, normals.UAV, motionVectors.UAV };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		auto shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite();
		context->CSSetShader(shader, nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	// Clear
	{
		ID3D11ShaderResourceView* views[14]{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3]{ nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(12, 1, buffers);

		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (dynamicCubemaps->loaded)
		dynamicCubemaps->PostDeferred();
}

void Deferred::EndDeferred()
{
	if (!inWorld)
		return;

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

	// Do not render to our targets past this point
	for (uint i = 0; i < 4; i++) {
		renderTargets[i] = forwardRenderTargets[i];
	}

	for (uint i = 4; i < 8; i++) {
		renderTargets[i] = RE::RENDER_TARGET::kNONE;
	}

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	DeferredPasses();  // Perform deferred passes and composite forward buffers

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = false;

	ResetBlendStates();
}

void Deferred::OverrideBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	static std::once_flag setup;
	std::call_once(setup, [&]() {
		auto device = globals::d3d::device;

		for (int a = 0; a < 7; a++) {
			for (int b = 0; b < 2; b++) {
				for (int c = 0; c < 13; c++) {
					for (int d = 0; d < 2; d++) {
						forwardBlendStates[a][b][c][d] = blendStates->a[a][b][c][d];

						if (auto blendState = forwardBlendStates[a][b][c][d]) {
							D3D11_BLEND_DESC blendDesc;
							forwardBlendStates[a][b][c][d]->GetDesc(&blendDesc);

							blendDesc.IndependentBlendEnable = true;

							// Start at 1 to ignore Diffuse
							for (int i = 1; i < 8; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
							}

							DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &deferredBlendStates[a][b][c][d]));
						} else {
							deferredBlendStates[a][b][c][d] = nullptr;
						}
					}
				}
			}
		}
	});

	// Set modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = deferredBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::ResetBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	// Restore modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = forwardBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::ClearShaderCache()
{
	if (ambientCompositeCS) {
		ambientCompositeCS->Release();
		ambientCompositeCS = nullptr;
	}
	if (ambientCompositeInteriorCS) {
		ambientCompositeInteriorCS->Release();
		ambientCompositeInteriorCS = nullptr;
	}
	if (mainCompositeCS) {
		mainCompositeCS->Release();
		mainCompositeCS = nullptr;
	}
	if (mainCompositeInteriorCS) {
		mainCompositeInteriorCS->Release();
		mainCompositeInteriorCS = nullptr;
	}
}

ID3D11ComputeShader* Deferred::GetComputeAmbientComposite()
{
	if (!ambientCompositeCS) {
		logger::debug("Compiling AmbientCompositeCS");

		std::vector<std::pair<const char*, const char*>> defines;

		if (globals::features::skylighting->loaded)
			defines.push_back({ "SKYLIGHTING", nullptr });

		if (globals::features::screenSpaceGI->loaded)
			defines.push_back({ "SSGI", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		ambientCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\AmbientCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return ambientCompositeCS;
}

ID3D11ComputeShader* Deferred::GetComputeAmbientCompositeInterior()
{
	if (!ambientCompositeInteriorCS) {
		logger::debug("Compiling AmbientCompositeCS INTERIOR");

		std::vector<std::pair<const char*, const char*>> defines;
		defines.push_back({ "INTERIOR", nullptr });

		if (globals::features::screenSpaceGI->loaded)
			defines.push_back({ "SSGI", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		ambientCompositeInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\AmbientCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return ambientCompositeInteriorCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	if (!mainCompositeCS) {
		logger::debug("Compiling DeferredCompositeCS");

		std::vector<std::pair<const char*, const char*>> defines;

		if (globals::features::dynamicCubemaps->loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::skylighting->loaded)
			defines.push_back({ "SKYLIGHTING", nullptr });

		if (globals::features::screenSpaceGI->loaded)
			defines.push_back({ "SSGI", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		mainCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeInterior()
{
	if (!mainCompositeInteriorCS) {
		logger::debug("Compiling DeferredCompositeCS INTERIOR");

		std::vector<std::pair<const char*, const char*>> defines;
		defines.push_back({ "INTERIOR", nullptr });

		if (globals::features::dynamicCubemaps->loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::screenSpaceGI->loaded)
			defines.push_back({ "SSGI", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		mainCompositeInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeInteriorCS;
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	func();
	globals::deferred->EarlyPrepasses();
};

void Deferred::Hooks::Main_RenderWorld::thunk(bool a1)
{
	auto deferred = globals::deferred;
	deferred->inWorld = true;
	func(a1);
	deferred->inWorld = false;
};

void Deferred::Hooks::Main_RenderWorld_Start::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
	auto deferred = globals::deferred;
	auto shaderCache = globals::shaderCache;

	if (shaderCache->IsEnabled() && deferred->inWorld) {
		// Here is where the first opaque objects start rendering
		deferred->StartDeferred();
		func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches                                                               // RenderBatches
	} else {
		func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches
	}
};

void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;
	auto terrainBlending = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;

	if (shaderCache->IsEnabled() && deferred->inWorld) {
		// Defer terrain rendering until after everything else
		if (terrainBlending->loaded)
			terrainBlending->RenderTerrainBlendingPasses();
	}

	// Deferred blended decals
	deferred->inBlendedDecals = true;
	func(This, RenderFlags);
	deferred->inBlendedDecals = false;

	deferred->EndDeferred();

	// Blended decals
	deferred->inDecals = true;
	func(This, RenderFlags);
	deferred->inDecals = false;

	// After this point, water starts rendering
};

void Deferred::Hooks::BSShaderAccumulator_BlendedDecals_RenderGeometryGroup::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
	auto deferred = globals::deferred;

	if (deferred->inBlendedDecals) {
		func(This, StartRange, EndRanges, RenderFlags, 12);
	} else {
		func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);
	}
};

void Deferred::Hooks::BSShaderAccumulator_FirstPerson_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;

	deferred->inBlendedDecals = true;
	func(This, RenderFlags);
	deferred->inBlendedDecals = false;
	func(This, RenderFlags);
	deferred->inDecals = false;
};

void Deferred::Hooks::BSShaderAccumulator_ShadowMapOrMask_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;

	deferred->inBlendedDecals = true;
	func(This, RenderFlags);
	deferred->inBlendedDecals = false;
	func(This, RenderFlags);
	deferred->inDecals = false;
};

void Deferred::Hooks::BSCubeMapCamera_RenderCubemap::thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
{
	auto deferred = globals::deferred;

	deferred->inReflections = true;
	deferred->ReflectionsPrepasses();
	func(camera, a2, a3, a4, a5);
	deferred->inReflections = false;
}

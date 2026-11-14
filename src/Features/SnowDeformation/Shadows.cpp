#include "Features/SnowDeformation.h"

#include "Globals.h"
#include "State.h"

void SnowDeformation::CaptureShadowAtlas()
{
	if (!settings.EnableSnowDeformation || !globals::state->HasDirectionalShadows()) {
		shadowAtlasLiveSRV = nullptr;
		return;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> liveAtlasSRV;
	globals::d3d::context->PSGetShaderResources(4, 1, liveAtlasSRV.put());

	D3D11_TEXTURE2D_DESC atlasDesc{};
	if (liveAtlasSRV) {
		winrt::com_ptr<ID3D11Resource> liveRes;
		liveAtlasSRV->GetResource(liveRes.put());
		if (auto liveTex = liveRes.try_as<ID3D11Texture2D>()) {
			liveTex->GetDesc(&atlasDesc);
		}
		RecordSunCascadeSlices(atlasDesc.ArraySize);

		shadowAtlasLiveSRV = liveAtlasSRV;
	}

	static uint32_t shadowLogCount = 0;
	static int lastValidState = -1;
	const int validState = ShadowAtlasSRV() ? 1 : 0;
	if ((shadowLogCount < 6 || validState != lastValidState) && shadowLogCount < 40) {
		shadowLogCount++;
		lastValidState = validState;
		D3D11_SHADER_RESOURCE_VIEW_DESC liveViewDesc{};
		if (liveAtlasSRV)
			liveAtlasSRV->GetDesc(&liveViewDesc);
		logger::info("[SNOW DEFORMATION] ShadowAtlas: atlasLive={} valid={} atlas={}x{} slices={} fmt={} samples={} liveViewDim={} liveViewFmt={}",
			liveAtlasSRV != nullptr, validState,
			atlasDesc.Width, atlasDesc.Height, atlasDesc.ArraySize, uint32_t(atlasDesc.Format), atlasDesc.SampleDesc.Count,
			uint32_t(liveViewDesc.ViewDimension), uint32_t(liveViewDesc.Format));
	}
}

void SnowDeformation::CapturePointShadowMask()
{
	if (!settings.EnableSnowDeformation || REL::Module::IsVR())
		return;

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto context = globals::d3d::context;
	winrt::com_ptr<ID3D11ShaderResourceView> liveAtlasSRV;
	context->PSGetShaderResources(4, 1, liveAtlasSRV.put());
	if (!liveAtlasSRV)
		return;
	winrt::com_ptr<ID3D11Resource> liveRes;
	liveAtlasSRV->GetResource(liveRes.put());
	auto liveTex = liveRes.try_as<ID3D11Texture2D>();
	if (!liveTex)
		return;
	D3D11_TEXTURE2D_DESC liveDesc;
	liveTex->GetDesc(&liveDesc);
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	liveAtlasSRV->GetDesc(&srvDesc);
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;

	if (pointShadowLiveTex != liveTex) {
		pointShadowLiveSRV = nullptr;
		pointShadowLiveTex = nullptr;
		srvDesc.Texture2DArray.ArraySize = liveDesc.ArraySize;
		if (FAILED(globals::d3d::device->CreateShaderResourceView(liveTex.get(), &srvDesc, pointShadowLiveSRV.put())))
			return;
		pointShadowLiveTex = liveTex;
	}

	for (auto& lightPtr : shadowSceneNode->GetRuntimeData().activeShadowLights) {
		auto* bsLight = lightPtr.get();
		if (!bsLight || !bsLight->IsShadowLight())
			continue;
		auto* light = static_cast<RE::BSShadowLight*>(bsLight);
		const bool isFrustum = light->GetIsFrustumLight();
		const bool isParabolic = light->GetIsParabolicLight();
		if (!isFrustum && !isParabolic)
			continue;
		auto& lightRuntime = light->GetRuntimeData();
		const uint32_t maskIndex = lightRuntime.maskIndex;
		if (maskIndex >= kPointShadowMaxLights)
			continue;
		if (lightRuntime.shadowmapDescriptors.empty())
			continue;
		auto& desc = lightRuntime.shadowmapDescriptors[0];
		if (!desc.isEnabled || desc.renderTarget == static_cast<RE::RENDER_TARGET_DEPTHSTENCIL>(-1))
			continue;
		if (desc.shadowmapIndex >= liveDesc.ArraySize)
			continue;

		ID3D11Texture2D* lightTargetTex = nullptr;
		if (auto* gameRenderer = globals::game::renderer) {
			auto& dsData = gameRenderer->GetDepthStencilData().depthStencils[desc.renderTarget];
			lightTargetTex = reinterpret_cast<ID3D11Texture2D*>(dsData.texture);
		}
		if (lightTargetTex != liveTex.get())
			continue;

		PointShadowLightData& dd = pendingPointShadows[maskIndex];
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
		DirectX::XMStoreFloat4x4(&dd.LightTransform, proj);
		dd.SliceIndex = desc.shadowmapIndex;
		dd.LightType = isFrustum ? 1u : (light->GetIsOmniLight() ? 3u : 2u);
		dd.ShadowRadius = light->light ? light->light->GetLightRuntimeData().radius.x : 0.0f;

		static uint32_t layoutLogCount = 0;
		if (layoutLogCount < 12) {
			layoutLogCount++;
			logger::info("[SNOW DEFORMATION] PointShadow: mask={} type={} srcSlice={} rt={} arr={} biasScale={:.3f}",
				maskIndex, dd.LightType, desc.shadowmapIndex, uint32_t(desc.renderTarget), liveDesc.ArraySize, lightRuntime.shadowBiasScale);
		}
	}
}

void SnowDeformation::UpdatePointShadowLights()
{
	auto context = globals::d3d::context;

	if (!pointShadowLights) {
		D3D11_BUFFER_DESC sbDesc = StructuredBufferDesc<PointShadowLightData>(uint64_t(kPointShadowMaxLights), false, true);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = kPointShadowMaxLights;

		pointShadowLights = new Buffer(sbDesc, nullptr, "SnowDeformation::PointShadowLights");
		pointShadowLights->CreateSRV(srvDesc);
	}

	PointShadowLightData table[kPointShadowMaxLights]{};
	if (PointShadowAtlasSRV())
		memcpy(table, pendingPointShadows, sizeof(table));

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(context->Map(pointShadowLights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, table, sizeof(table));
		context->Unmap(pointShadowLights->resource.get(), 0);
	}
}

void SnowDeformation::RecordSunCascadeSlices(uint32_t a_atlasSlices)
{
	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	auto* sunShadowLight = shadowSceneNode ? shadowSceneNode->GetRuntimeData().sunShadowDirLight : nullptr;
	if (!sunShadowLight || a_atlasSlices == 0)
		return;
	const auto& descriptors = sunShadowLight->GetRuntimeData().shadowmapDescriptors;
	const uint32_t count = std::min<uint32_t>((uint32_t)descriptors.size(), 2u);
	bool fallback = false;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t slice = descriptors[i].shadowmapIndex;
		if (slice >= a_atlasSlices) {
			slice = i;
			fallback = true;
		}
		if (slice < a_atlasSlices)
			sunCascadeSlice[i] = slice;
	}
	const uint32_t raw0 = count > 0 ? descriptors[0].shadowmapIndex : 0xFFu;
	const uint32_t raw1 = count > 1 ? descriptors[1].shadowmapIndex : 0xFFu;
	const uint32_t packed = (sunCascadeSlice[0] & 0xFF) | ((sunCascadeSlice[1] & 0xFF) << 8) | ((raw0 & 0xFF) << 16) | ((raw1 & 0xFF) << 24);
	static uint32_t lastPacked = 0xFFFFFFFFu;
	static uint32_t changeLogCount = 0;
	if (packed != lastPacked && changeLogCount < 200) {
		lastPacked = packed;
		changeLogCount++;
		logger::info("[SNOW DEFORMATION] Sun cascades: slices used = {}, {} (raw shadowmapIndex = {}, {}; descriptors = {}; atlas has {}; fallback = {})",
			sunCascadeSlice[0], sunCascadeSlice[1], raw0, raw1, (uint32_t)descriptors.size(), a_atlasSlices, fallback);
	}
}

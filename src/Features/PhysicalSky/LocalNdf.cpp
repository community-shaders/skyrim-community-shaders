#include "Features/PhysicalSky.h"

#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "I18n/I18n.h"
#include "Util.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <imgui_stdlib.h>

#define I18N_KEY_PREFIX "feature.physical_sky."

namespace
{
	constexpr const char* kAssetRoot = "Data/Textures/PhysicalSky/LocalNdf";

	float ClampFinite(float value, float minimum, float maximum, float fallback)
	{
		return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
	}

	eastl::unique_ptr<Texture2D> CreateLocalTexture(uint32_t dimension, uint32_t count, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = dimension;
		desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.ArraySize = count;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto texture = eastl::make_unique<Texture2D>(desc, name);
		texture->CreateSRV({ .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY, .Texture2DArray = { .MipLevels = 1, .ArraySize = count } });
		texture->CreateUAV({ .Format = desc.Format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY, .Texture2DArray = { .ArraySize = count } });
		return texture;
	}
}

std::filesystem::path LocalNdfManager::AssetPath(const std::string& asset)
{
	if (asset.empty() || !std::ranges::all_of(asset, [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		}))
		return {};
	return std::filesystem::path(kAssetRoot) / asset;
}

void LocalNdfManager::RefreshAssets()
{
	assets.clear();
	std::error_code error;
	for (std::filesystem::directory_iterator it(kAssetRoot, error), end; !error && it != end; it.increment(error)) {
		if (it->is_directory(error)) {
			const auto id = it->path().filename().string();
			if (!AssetPath(id).empty())
				assets.push_back(id);
		}
	}
	std::ranges::sort(assets);
	scanned = true;
	key.clear();
}

void LocalNdfManager::SetupResources()
{
	cb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<Parameters>());
	height = CreateLocalTexture(1, 1, "PhysicalSky::LocalNdfHeight");
	modeling = CreateLocalTexture(1, 1, "PhysicalSky::LocalNdfModeling");
	maximum = CreateLocalTexture(1, 1, "PhysicalSky::LocalNdfMaximum");
	const float clear[4] = {};
	globals::d3d::context->ClearUnorderedAccessViewFloat(height->uav.get(), clear);
	globals::d3d::context->ClearUnorderedAccessViewFloat(modeling->uav.get(), clear);
	globals::d3d::context->ClearUnorderedAccessViewFloat(maximum->uav.get(), clear);
	D3D11_SAMPLER_DESC desc{};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	desc.MaxAnisotropy = 1;
	sampler = nullptr;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&desc, sampler.put()));
	weights = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<float4>(1u, true), 1, "PhysicalSky::LocalNdfWeights");
	weights->CreateSRV();
	layerWeights.assign(1, float4{});
	weights->Update(layerWeights.data(), sizeof(float4));
	targets.clear();
	previews.clear();
	activeAssets.clear();
}

void LocalNdfManager::CompileShaders(Util::AsyncShaderSet& a_shaders)
{
	key.clear();
	altitude = {};
	a_shaders.Add(program, L"Data\\Shaders\\PhysicalSky\\LocalNdf.cs.hlsl");
}

NdfTextureSet LocalNdfManager::GetTextures() const
{
	return { height ? height->srv.get() : nullptr, modeling ? modeling->srv.get() : nullptr };
}

bool LocalNdfManager::Update(const NdfSettings& settings, TextureManager& textures, const std::string& worldspace)
{
	if (!program || !cb || !sampler)
		return false;
	struct Layer
	{
		Parameters data;
		std::array<ID3D11ShaderResourceView*, 3> sources;
		float weight;
		std::string asset;
	};
	std::vector<Layer> layers;
	rejectedAssets.clear();
	float2 minimum = { FLT_MAX, FLT_MAX }, maximumPosition = { -FLT_MAX, -FLT_MAX };
	float minAltitude = FLT_MAX, maxAltitude = -FLT_MAX;
	for (const auto& endpoint : settings.localCloud.states) {
		LocalNdfState instance;
		try {
			instance = endpoint.at("value").get<LocalNdfState>();
		} catch (const nlohmann::json::exception&) {
			rejectedAssets.push_back(T(TKEY("local_ndf_invalid_state"), "Invalid cloud state"));
			continue;
		}
		const float endpointWeight = endpoint.at("weight").get<float>();
		if (endpointWeight <= 0.f)
			continue;
		if (!instance.enabled || instance.worldspace.empty() || instance.worldspace != worldspace)
			continue;
		const float strength = ClampFinite(instance.strength, 0.f, 1.f, 1.f);
		const float modelWeight = strength * ClampFinite(instance.modelingWeight, 0.f, 1.f, 1.f);
		const float heightWeight = strength * ClampFinite(instance.heightWeight, 0.f, 1.f, 1.f);
		if (modelWeight == 0.f && heightWeight == 0.f)
			continue;
		const auto path = AssetPath(instance.asset);
		if (path.empty()) {
			rejectedAssets.push_back(instance.asset);
			continue;
		}
		const auto heightPath = (path / "Height.dds").string();
		const auto modelingPath = (path / "Modeling.dds").string();
		const auto maskPath = (path / "Mask.dds").string();
		if (heightWeight > 0.f)
			textures.EnsureLoaded(heightPath);
		textures.EnsureLoaded(modelingPath);
		std::error_code error;
		const bool hasMask = std::filesystem::exists(maskPath, error);
		if (error) {
			rejectedAssets.push_back(instance.asset);
			continue;
		}
		if (hasMask)
			textures.EnsureLoaded(maskPath);
		NdfTextureSet maps{ heightWeight > 0.f ? textures.Query(heightPath) : nullptr, textures.Query(modelingPath) };
		auto* mask = hasMask ? textures.Query(maskPath) : nullptr;
		if (!NdfManager::IsTextureNdf(maps.modeling, 3) || (heightWeight > 0.f && !NdfManager::IsTexturePair(maps)) || (hasMask && !NdfManager::IsTextureNdf(mask, 1))) {
			rejectedAssets.push_back(instance.asset);
			continue;
		}
		Parameters data{};
		data.centerSize = { ClampFinite(instance.center.x, -1e7f, 1e7f, 0.f), ClampFinite(instance.center.y, -1e7f, 1e7f, 0.f),
			ClampFinite(instance.size.x, 1.f, 128000.f, 16384.f), ClampFinite(instance.size.y, 1.f, 128000.f, 16384.f) };
		const float angle = ClampFinite(instance.rotation, -36000.f, 36000.f, 0.f) * 0.01745329252f;
		data.rotationWeights = { std::cos(angle), std::sin(angle), modelWeight, heightWeight };
		data.heightFeather = { ClampFinite(instance.altitude, -24000.f, 48000.f, 256.f), ClampFinite(instance.heightSpan, 1.f, 24000.f, 1792.f),
			ClampFinite(instance.feather, 0.f, std::min(data.centerSize.z, data.centerSize.w) * 0.5f, 200.f), 0.f };
		data.hasMask = hasMask ? 1u : 0u;
		data.modelingAlpha = instance.modelingAlpha ? 1u : 0u;
		data.modelingBlend = instance.modelingBlend == LocalNdfBlendMode::Maximum ? 1u : 0u;
		const float2 radius = { (std::abs(data.rotationWeights.x) * data.centerSize.z + std::abs(data.rotationWeights.y) * data.centerSize.w) * 0.5f,
			(std::abs(data.rotationWeights.y) * data.centerSize.z + std::abs(data.rotationWeights.x) * data.centerSize.w) * 0.5f };
		minimum.x = std::min(minimum.x, data.centerSize.x - radius.x);
		minimum.y = std::min(minimum.y, data.centerSize.y - radius.y);
		maximumPosition.x = std::max(maximumPosition.x, data.centerSize.x + radius.x);
		maximumPosition.y = std::max(maximumPosition.y, data.centerSize.y + radius.y);
		if (heightWeight > 0.f) {
			minAltitude = std::min(minAltitude, data.heightFeather.x);
			maxAltitude = std::max(maxAltitude, data.heightFeather.x + data.heightFeather.y);
		}
		layers.push_back({ data, { maps.height, maps.modeling, mask }, endpointWeight, instance.asset });
	}
	std::sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b) {
		if (a.asset != b.asset)
			return a.asset < b.asset;
		return std::memcmp(&a.data, &b.data, sizeof(Parameters)) < 0;
	});
	std::vector<float4> nextWeights;
	for (const auto& layer : layers)
		nextWeights.push_back({ layer.weight, static_cast<float>(layer.data.modelingBlend), 0.f, 0.f });
	if (nextWeights.empty())
		nextWeights.push_back({});
	if (nextWeights.size() != layerWeights.size()) {
		weights = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<float4>(static_cast<UINT>(nextWeights.size()), true), static_cast<UINT>(nextWeights.size()), "PhysicalSky::LocalNdfWeights");
		weights->CreateSRV();
	}
	if (nextWeights.size() != layerWeights.size() || std::memcmp(nextWeights.data(), layerWeights.data(), nextWeights.size() * sizeof(float4)) != 0)
		weights->Update(nextWeights.data(), nextWeights.size() * sizeof(float4));
	layerWeights = std::move(nextWeights);
	std::string nextKey = worldspace;
	nextKey.push_back('\0');
	for (const auto& layer : layers) {
		nextKey.append(reinterpret_cast<const char*>(&layer.data), sizeof(layer.data));
		nextKey.append(layer.asset).push_back('\0');
	}
	const float requestedTexelSize = ClampFinite(settings.localTexelSize, 1.f, 512.f, 32.f);
	nextKey.append(reinterpret_cast<const char*>(&requestedTexelSize), sizeof(requestedTexelSize));
	if (key == nextKey && textureRevision == textures.revision)
		return false;
	activeAssets.clear();
	rect = altitude = {};
	uint32_t dimension = 1;
	effectiveTexelSize = 0.f;
	if (!layers.empty()) {
		const float extent = std::max(maximumPosition.x - minimum.x, maximumPosition.y - minimum.y);
		effectiveTexelSize = std::max(requestedTexelSize, extent / 2044.f);
		minimum.x = std::floor(minimum.x / effectiveTexelSize) * effectiveTexelSize - effectiveTexelSize;
		minimum.y = std::floor(minimum.y / effectiveTexelSize) * effectiveTexelSize - effectiveTexelSize;
		dimension = std::clamp(static_cast<uint32_t>(std::ceil(std::max(maximumPosition.x - minimum.x, maximumPosition.y - minimum.y) / effectiveTexelSize)) + 1u, 1u, 2048u);
		const float side = dimension * effectiveTexelSize;
		rect = { minimum.x, minimum.y, 1.f / side, 1.f / side };
		altitude = { minAltitude <= maxAltitude ? minAltitude : 0.f, minAltitude <= maxAltitude ? maxAltitude - minAltitude : 0.f, 1.f, static_cast<float>(layers.size()) };
	}
	const auto count = static_cast<uint32_t>(std::max<size_t>(layers.size(), 1));
	if (!height || height->desc.Width != dimension || height->desc.ArraySize != count || targets.empty()) {
		height = CreateLocalTexture(dimension, count, "PhysicalSky::LocalNdfHeight");
		modeling = CreateLocalTexture(dimension, count, "PhysicalSky::LocalNdfModeling");
		maximum = CreateLocalTexture(dimension, count, "PhysicalSky::LocalNdfMaximum");
		targets.clear();
		previews.clear();
		targets.resize(count);
		previews.resize(count);
		std::array<Texture2D*, 3> maps{ height.get(), modeling.get(), maximum.get() };
		for (uint32_t slice = 0; slice < count; ++slice) {
			for (uint32_t map = 0; map < maps.size(); ++map) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{ .Format = maps[map]->desc.Format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY, .Texture2DArray = { .FirstArraySlice = slice, .ArraySize = 1 } };
				DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(maps[map]->resource.get(), &uav, targets[slice][map].put()));
			}
		}
	}
	auto* context = globals::d3d::context;
	winrt::com_ptr<ID3D11ComputeShader> previousShader;
	winrt::com_ptr<ID3D11Buffer> previousCb;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> previousSources;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> previousLocalSources;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 3> previousTargets;
	context->CSGetShader(previousShader.put(), nullptr, nullptr);
	context->CSGetConstantBuffers(1, 1, previousCb.put());
	for (UINT i = 0; i < previousSources.size(); ++i)
		context->CSGetShaderResources(i, 1, previousSources[i].put());
	for (UINT i = 0; i < previousTargets.size(); ++i) {
		context->CSGetUnorderedAccessViews(i, 1, previousTargets[i].put());
		context->CSGetShaderResources(32 + i, 1, previousLocalSources[i].put());
	}
	ID3D11ShaderResourceView* empty[3] = {};
	context->CSSetShaderResources(32, 3, empty);
	const float clear[4] = {};
	context->ClearUnorderedAccessViewFloat(height->uav.get(), clear);
	context->ClearUnorderedAccessViewFloat(modeling->uav.get(), clear);
	context->ClearUnorderedAccessViewFloat(maximum->uav.get(), clear);
	winrt::com_ptr<ID3D11SamplerState> savedSampler;
	context->CSGetSamplers(0, 1, savedSampler.put());
	auto* samp = sampler.get();
	auto* buffer = cb->CB();
	context->CSSetSamplers(0, 1, &samp);
	context->CSSetConstantBuffers(1, 1, &buffer);
	context->CSSetShader(program.get(), nullptr, 0);
	globals::profiler->BeginPass("PhysicalSky::LocalNdf");
	for (size_t index = 0; index < layers.size(); ++index) {
		auto& layer = layers[index];
		layer.data.rect = rect;
		layer.data.altitude = altitude;
		layer.data.dimension = dimension;
		cb->Update(layer.data);
		ID3D11ShaderResourceView* sources[] = { layer.sources[0], layer.sources[1], layer.sources[2] };
		ID3D11UnorderedAccessView* outputViews[] = { targets[index][0].get(), targets[index][1].get(), targets[index][2].get() };
		context->CSSetShaderResources(0, 3, sources);
		context->CSSetUnorderedAccessViews(0, 3, outputViews, nullptr);
		context->Dispatch((dimension + 7u) / 8u, (dimension + 7u) / 8u, 1);
		ID3D11ShaderResourceView* emptySources[3] = {};
		ID3D11UnorderedAccessView* emptyTargets[3] = {};
		context->CSSetShaderResources(0, 3, emptySources);
		context->CSSetUnorderedAccessViews(0, 3, emptyTargets, nullptr);
		activeAssets.push_back(layer.asset);
	}
	globals::profiler->EndPass();
	ID3D11ShaderResourceView* nullSources[3] = {};
	ID3D11UnorderedAccessView* nullUavs[3] = {};
	context->CSSetShaderResources(0, 3, nullSources);
	context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
	buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);
	samp = savedSampler.get();
	context->CSSetSamplers(0, 1, &samp);
	context->CSSetShader(previousShader.get(), nullptr, 0);
	buffer = previousCb.get();
	context->CSSetConstantBuffers(1, 1, &buffer);
	for (UINT i = 0; i < previousSources.size(); ++i) {
		auto* source = previousSources[i].get();
		context->CSSetShaderResources(i, 1, &source);
	}
	for (UINT i = 0; i < previousTargets.size(); ++i) {
		auto* target = previousTargets[i].get();
		auto* source = previousLocalSources[i].get();
		context->CSSetUnorderedAccessViews(i, 1, &target, nullptr);
		context->CSSetShaderResources(32 + i, 1, &source);
	}
	textureRevision = textures.revision;
	key = std::move(nextKey);
	return true;
}

void LocalNdfManager::DrawSettings(NdfSettings& settings, const std::string& worldspace, float2 cameraMeters)
{
	if (!scanned)
		RefreshAssets();
	if (ImGui::Button(T(TKEY("local_ndf_scan"), "Scan asset folders")))
		RefreshAssets();
	ImGui::TextUnformatted(kAssetRoot);
	ImGui::TextWrapped("%s", T(TKEY("local_ndf_scene_hint"), "Each scene stores one complete local cloud. Weather and time transitions crossfade the endpoint cloud maps; an empty state fades back to global clouds."));
	const auto* armed = SceneWidgetInterceptor::GetArmedContext();
	const bool scene = armed && !armed->baseline;
	auto* manager = SceneSettingsManager::GetSingleton();
	const SceneSettingsManager::SettingIdentity identity{ "PhysicalSky", { "cloudMap" }, "localCloud" };
	std::optional<size_t> entryIndex;
	nlohmann::json value = settings.localCloud;
	bool editable = true;
	if (scene) {
		entryIndex = manager->FindContextUserEntry(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
		const auto entries = manager->GetContextEntries(armed->contextId);
		const bool active = entryIndex && *entryIndex < entries.size() && !entries[*entryIndex].deleted;
		if (active) {
			value = entries[*entryIndex].value;
			editable = !entries[*entryIndex].paused;
		} else {
			if (ImGui::Button(T(TKEY("local_ndf_scene_override"), "Override local cloud in this scene"))) {
				if (entryIndex)
					manager->ClearContextTombstone(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
				manager->AddContextSetting(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
			}
			return;
		}
		if (active && ImGui::Button(T(TKEY("local_ndf_scene_inherit"), "Remove override / inherit"))) {
			manager->RemoveContextSetting(armed->contextId, *entryIndex);
			return;
		}
	} else {
		ImGui::DragFloat(T(TKEY("local_ndf_texel"), "Requested texel size (m)"), &settings.localTexelSize, 1.f, 1.f, 512.f);
	}
	LocalNdfState state;
	state.enabled = false;
	state.worldspace = worldspace;
	state.center = cameraMeters;
	if (!assets.empty())
		state.asset = assets.front();
	if (SceneBlend::IsValid(value) && !value["states"].empty()) {
		const auto endpoint = std::max_element(value["states"].begin(), value["states"].end(), [](const auto& lhs, const auto& rhs) {
			return lhs["weight"].template get<float>() < rhs["weight"].template get<float>();
		});
		try {
			state = (*endpoint)["value"].get<LocalNdfState>();
		} catch (const nlohmann::json::exception&) {
		}
		if (value["states"].size() > 1)
			ImGui::TextWrapped("%s", T(TKEY("local_ndf_edit_mixed"), "A transition is active. Editing captures the strongest endpoint as a single cloud state."));
	}
	ImGui::BeginDisabled(!editable);
	bool changed = false;
	changed |= ImGui::Checkbox(T(TKEY("local_ndf_enabled"), "Enable local cloud"), &state.enabled);
	changed |= ImGui::InputText(T(TKEY("local_ndf_asset"), "Asset folder ID"), &state.asset);
	if (ImGui::BeginCombo(T(TKEY("local_ndf_assets"), "Available assets"), state.asset.c_str())) {
		for (const auto& asset : assets)
			if (ImGui::Selectable(asset.c_str(), asset == state.asset)) {
				state.asset = asset;
				changed = true;
			}
		ImGui::EndCombo();
	}
	changed |= ImGui::InputText(T(TKEY("local_ndf_worldspace"), "Worldspace Editor ID"), &state.worldspace);
	if (state.worldspace.empty() || state.worldspace != worldspace)
		ImGui::TextWrapped("%s", T(TKEY("local_ndf_other_world"), "Inactive here: assign the current worldspace to preview this state."));
	if (ImGui::Button(T(TKEY("local_ndf_camera"), "Move to camera / current worldspace"))) {
		state.center = cameraMeters;
		state.worldspace = worldspace;
		changed = true;
	}
	if (ImGui::Button(T(TKEY("local_ndf_stationary_maximum"), "Stationary maximum overlay preset"))) {
		state.center = {};
		state.size = { 16384.f, 16384.f };
		state.rotation = 0.f;
		state.strength = state.modelingWeight = 1.f;
		state.heightWeight = state.feather = 0.f;
		state.modelingAlpha = true;
		state.modelingBlend = LocalNdfBlendMode::Maximum;
		changed = true;
	}
	ImGui::TextWrapped("%s", T(TKEY("local_ndf_stationary_maximum_hint"), "Preset: world origin, 16384 m square, RGB maximum at full strength, no height influence or edge feather. Global cloud settings are unchanged."));
	changed |= ImGui::DragFloat2(T(TKEY("local_ndf_center"), "World X / Y (m)"), &state.center.x, 10.f);
	changed |= ImGui::DragFloat2(T(TKEY("local_ndf_size"), "Width / length (m)"), &state.size.x, 10.f, 1.f, 128000.f);
	changed |= ImGui::DragFloat(T(TKEY("local_ndf_rotation"), "Rotation (degrees)"), &state.rotation, 1.f);
	ImGui::BeginDisabled(state.heightWeight <= 0.f);
	changed |= ImGui::DragFloat(T(TKEY("local_ndf_altitude"), "Height zero (m)"), &state.altitude, 10.f, -24000.f, 48000.f);
	changed |= ImGui::DragFloat(T(TKEY("local_ndf_height_span"), "Height span (m)"), &state.heightSpan, 10.f, 1.f, 24000.f);
	ImGui::EndDisabled();
	changed |= ImGui::SliderFloat(T(TKEY("local_ndf_strength"), "Local cloud strength"), &state.strength, 0.f, 1.f);
	const char* blendNames[] = { T(TKEY("local_ndf_interpolate"), "Interpolate"), T(TKEY("local_ndf_maximum"), "Maximum") };
	int blend = state.modelingBlend == LocalNdfBlendMode::Maximum ? 1 : 0;
	if (ImGui::Combo(T(TKEY("local_ndf_blend"), "Modeling blend"), &blend, blendNames, 2)) {
		state.modelingBlend = static_cast<LocalNdfBlendMode>(blend);
		changed = true;
	}
	changed |= ImGui::SliderFloat(T(TKEY("local_ndf_model_weight"), "Modeling influence"), &state.modelingWeight, 0.f, 1.f);
	changed |= ImGui::SliderFloat(T(TKEY("local_ndf_height_weight"), "Height influence"), &state.heightWeight, 0.f, 1.f);
	if (state.heightWeight <= 0.f)
		ImGui::TextWrapped("%s", T(TKEY("local_ndf_no_height"), "Height influence is zero: Height.dds is not required and does not affect cloud bounds."));
	changed |= ImGui::DragFloat(T(TKEY("local_ndf_feather"), "Edge feather (m)"), &state.feather, 1.f, 0.f, 64000.f);
	changed |= ImGui::Checkbox(T(TKEY("local_ndf_alpha"), "Use modeling alpha as mask"), &state.modelingAlpha);
	if (state.modelingBlend == LocalNdfBlendMode::Interpolate)
		ImGui::TextWrapped("%s", T(TKEY("local_ndf_interpolate_hint"), "Alpha zero preserves the background weather, including its height when height influence is enabled. Disable alpha to replace the field at full influence; zero source coverage then clears background clouds."));
	if (state.modelingBlend == LocalNdfBlendMode::Maximum)
		ImGui::TextWrapped("%s", T(TKEY("local_ndf_maximum_hint"), "Maximum combines coverage and profile types independently, so they may come from different maps. It never clears background clouds and ignores modeling alpha for RGB. Height blending still uses alpha when enabled."));
	ImGui::EndDisabled();
	if (changed && editable) {
		globals::features::physicalSky.volMainHistoryValid = false;
		SceneBlend selection;
		selection.states.push_back({ { "value", state }, { "weight", 1.f } });
		if (scene && entryIndex) {
			const SceneSettingsManager::EntryValueUpdate update{ *entryIndex, selection };
			manager->UpdateContextEntryValues(armed->contextId, { &update, 1 }, true);
		} else {
			settings.localCloud = selection;
			manager->RecordBaselineEdit(identity, selection);
		}
	}
	for (const auto& asset : rejectedAssets)
		ImGui::TextWrapped("%s: %s", T(TKEY("local_ndf_rejected"), "Rejected local cloud asset"), asset.c_str());
}

void LocalNdfManager::DrawDebug(TextureManager& textures, float scale)
{
	if (!ImGui::TreeNode(T(TKEY("local_ndf_debug"), "Local NDF resources")))
		return;
	ImGui::Text("%s: %zu", T(TKEY("local_ndf_active"), "Active transition endpoints"), activeAssets.size());
	ImGui::Text("%s: %.2f", T(TKEY("local_ndf_effective_texel"), "Effective texel size (m), atlas limited to 2048"), effectiveTexelSize);
	ImGui::Text("XY: %.2f, %.2f; Z: %.2f .. %.2f", rect.x, rect.y, altitude.x, altitude.x + altitude.y);
	auto& sky = globals::features::physicalSky;
	for (size_t i = 0; i < activeAssets.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("%s: %.4f", activeAssets[i].c_str(), layerWeights[i].x);
		std::array<Texture2D*, 3> maps{ height.get(), modeling.get(), maximum.get() };
		const char* labels[] = { T(TKEY("local_ndf_debug_height"), "Height / influence"),
			T(TKEY("local_ndf_debug_model"), "Modeling / opacity"), T(TKEY("local_ndf_debug_maximum"), "Maximum floor") };
		for (size_t map = 0; map < maps.size(); ++map) {
			auto& preview = previews[i][map];
			if (!preview) {
				auto desc = maps[map]->desc;
				desc.ArraySize = 1;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				preview = eastl::make_unique<Texture2D>(desc, "PhysicalSky::LocalNdfPreview");
				preview->CreateSRV({ .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MipLevels = 1 } });
			}
			globals::d3d::context->CopySubresourceRegion(preview->resource.get(), 0, 0, 0, 0, maps[map]->resource.get(), static_cast<UINT>(i), nullptr);
			const auto id = "localState" + std::to_string(i) + "Map" + std::to_string(map);
			sky.DrawDebugCloudTexture(preview->srv.get(), id.c_str(), labels[map], scale);
		}
		ImGui::PopID();
	}
	auto sources = activeAssets;
	sources.insert(sources.end(), rejectedAssets.begin(), rejectedAssets.end());
	std::ranges::sort(sources);
	sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
	for (size_t i = 0; i < sources.size(); ++i) {
		const auto path = AssetPath(sources[i]);
		if (path.empty())
			continue;
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(sources[i].c_str());
		sky.DrawDebugCloudTexture(textures.Query((path / "Height.dds").string()), "heightInput", T(TKEY("local_ndf_height_input"), "Height input"), scale);
		sky.DrawDebugCloudTexture(textures.Query((path / "Modeling.dds").string()), "modelInput", T(TKEY("local_ndf_model_input"), "Modeling input / alpha mask"), scale);
		sky.DrawDebugCloudTexture(textures.Query((path / "Mask.dds").string()), "maskInput", T(TKEY("local_ndf_mask_input"), "Optional mask input"), scale, true);
		ImGui::PopID();
	}
	ImGui::TreePop();
}

#undef I18N_KEY_PREFIX

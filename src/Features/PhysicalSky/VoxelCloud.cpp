#include "Features/PhysicalSky.h"

#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "I18n/I18n.h"
#include "Util.h"
#include <DirectXTex.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <imgui_stdlib.h>
#include <numbers>

#define I18N_KEY_PREFIX "feature.physical_sky."

namespace
{
	constexpr const char* kRoot = "Data/Textures/PhysicalSky/VoxelClouds";

	std::filesystem::path AssetPath(const std::string& id, const std::string& file)
	{
		const std::filesystem::path relative = std::filesystem::path(id) / file;
		if (relative.has_root_path() || std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
			throw std::runtime_error("Voxel cloud assets must be relative to VoxelClouds");
		return std::filesystem::path(kRoot) / relative;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> LoadVolume(const std::filesystem::path& path, bool slices, const char* name = nullptr)
	{
		DirectX::ScratchImage source, decoded;
		DX::ThrowIfFailed(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, source));
		if (source.GetMetadata().dimension != DirectX::TEX_DIMENSION_TEXTURE3D)
			throw std::runtime_error("Expected a 3D DDS volume: " + path.string());
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		if (slices) {
			// Keep native BC6H/BC1 compression. The shader supplies Z interpolation;
			// Modeling and SDF evaluations both use base-level field data.
			const auto& metadata = source.GetMetadata();
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = static_cast<UINT>(metadata.width);
			desc.Height = static_cast<UINT>(metadata.height);
			desc.ArraySize = static_cast<UINT>(metadata.depth);
			desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.Format = metadata.format;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			std::vector<D3D11_SUBRESOURCE_DATA> images(desc.ArraySize);
			for (UINT z = 0; z < desc.ArraySize; ++z) {
				const auto* image = source.GetImage(0, 0, z);
				images[z] = { image->pixels, static_cast<UINT>(image->rowPitch), static_cast<UINT>(image->slicePitch) };
			}
			winrt::com_ptr<ID3D11Texture2D> texture;
			DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&desc, images.data(), texture.put()));
			DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(texture.get(), nullptr, view.put()));
		} else {
			if (DirectX::IsCompressed(source.GetMetadata().format)) {
				DX::ThrowIfFailed(DirectX::Decompress(source.GetImages(), source.GetImageCount(), source.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, decoded));
				source = std::move(decoded);
			}
			DX::ThrowIfFailed(DirectX::CreateShaderResourceView(globals::d3d::device, source.GetImages(), source.GetImageCount(), source.GetMetadata(), view.put()));
		}
		const auto resourceName = name ? std::string(name) : "PhysicalSky::VoxelCloud " + path.string();
		Util::SetResourceName(view.get(), "%s SRV", resourceName.c_str());
		winrt::com_ptr<ID3D11Resource> resource;
		view->GetResource(resource.put());
		Util::SetResourceName(resource.get(), "%s", resourceName.c_str());
		return view;
	}

	float3 HalfBounds(const VoxelCloudData& cloud)
	{
		return {
			(std::abs(cloud.axisX.x) / cloud.axisX.w + std::abs(cloud.axisY.x) / cloud.axisY.w + std::abs(cloud.axisZ.x) / cloud.axisZ.w) * .5f,
			(std::abs(cloud.axisX.y) / cloud.axisX.w + std::abs(cloud.axisY.y) / cloud.axisY.w + std::abs(cloud.axisZ.y) / cloud.axisZ.w) * .5f,
			(std::abs(cloud.axisX.z) / cloud.axisX.w + std::abs(cloud.axisY.z) / cloud.axisY.w + std::abs(cloud.axisZ.z) / cloud.axisZ.w) * .5f
		};
	}
}

void from_json(const nlohmann::json& json, VoxelCloudAsset& asset)
{
	asset = VoxelCloudAsset{};
	asset.name = json.value("name", asset.name);
	asset.modeling = json.value("ModelingDataTexture", json.value("modeling", asset.modeling));
	asset.distance = json.value("SignedDistanceFieldTexture", json.value("distance", asset.distance));
	asset.size = json.value("size", asset.size);
	asset.boxSize = json.value("BoxSize", asset.boxSize);
	asset.noiseFrequency = json.value("HighQualityModeNoiseFrequencyScale", json.value("noiseFrequency", asset.noiseFrequency));
	asset.noiseFrequencyAdjustmentDistance = json.value("HighQualityModeNoiseFrequencyAdjustmentDistance", asset.noiseFrequencyAdjustmentDistance);
	asset.noiseMipDistance = json.value("noiseMipDistance", asset.noiseMipDistance);
	asset.noiseMipsEnabled = json.value("noiseMipsEnabled", asset.noiseMipsEnabled);
	asset.placement = json.value("placement", asset.placement);
	if (json.contains("BoxCenter") && !(json.contains("placement") && json.at("placement").contains("center"))) {
		const auto& center = json.at("BoxCenter");
		asset.placement.center = { center.at("X").get<float>(), center.at("Y").get<float>(), center.at("Z").get<float>() };
	}
	if (json.contains("BoxSize") && !(std::isfinite(asset.boxSize) && asset.boxSize > 0.f))
		throw std::runtime_error("BoxSize must be finite and positive");
}

void VoxelCloudManager::LoadInstances()
{
	assets.clear();
	resources.clear();
	noiseVoxel = nullptr;
	noiseAttempted = false;
	lightingValid = false;
	active.clear();
	bindings.fill(nullptr);
	key.clear();
	error.clear();
	scanned = true;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(kRoot, ec), end; !ec && it != end; it.increment(ec)) {
		const auto file = it->path() / "Instance.json";
		if (!std::filesystem::is_regular_file(file, ec))
			continue;
		try {
			std::ifstream stream(file);
			auto asset = nlohmann::json::parse(stream).get<VoxelCloudAsset>();
			if (asset.boxSize > 0.f) {
				DirectX::TexMetadata metadata;
				DX::ThrowIfFailed(DirectX::GetMetadataFromDDSFile(AssetPath(it->path().filename().string(), asset.distance).c_str(), DirectX::DDS_FLAGS_NONE, metadata));
				if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE3D)
					throw std::runtime_error("SignedDistanceFieldTexture must be a 3D DDS volume");
				// Derive all three extents from the SDF dimensions.
				const float voxelSize = asset.boxSize / static_cast<float>(std::max({ metadata.width, metadata.height, metadata.depth }));
				asset.size = float3{ static_cast<float>(metadata.width), static_cast<float>(metadata.height), static_cast<float>(metadata.depth) } * voxelSize;
			}
			if (!(asset.size.x > 0.f && asset.size.y > 0.f && asset.size.z > 0.f && asset.noiseFrequency > 0.f && asset.noiseMipDistance > 0.f))
				throw std::runtime_error("Volume size and noise scales must be positive");
			assets.emplace(it->path().filename().string(), std::move(asset));
		} catch (const std::exception& exception) {
			error = file.string() + ": " + exception.what();
			logger::error("PhysicalSky voxel cloud: {}", error);
		}
	}
}

VoxelCloudManager::Resource& VoxelCloudManager::LoadResource(const std::string& id, const VoxelCloudAsset& asset)
{
	auto& resource = resources[id];
	if (!resource.attempted) {
		resource.attempted = true;
		try {
			resource.views = { LoadVolume(AssetPath(id, asset.modeling), true), LoadVolume(AssetPath(id, asset.distance), true) };
		} catch (const std::exception& exception) {
			resource.views = {};
			error = id + ": " + exception.what();
			logger::error("PhysicalSky voxel cloud: {}", error);
		}
	}
	return resource;
}

ID3D11ShaderResourceView* VoxelCloudManager::Noise()
{
	if (!noiseAttempted) {
		noiseAttempted = true;
		try {
			noiseVoxel = LoadVolume("Data/Textures/PhysicalSky/NoiseVoxel.dds", false, "PhysicalSky::NoiseVoxel");
		} catch (const std::exception& exception) {
			error = exception.what();
			logger::error("PhysicalSky voxel cloud: {}", error);
		}
	}
	return noiseVoxel.get();
}

void VoxelCloudManager::Update(const SceneBlend& selection, const std::string& worldspace, bool enabled)
{
	if (!scanned)
		LoadInstances();
	const std::string nextKey = worldspace + (enabled ? selection.states.dump() : "disabled");
	if (key == nextKey)
		return;
	key = nextKey;
	active.clear();
	bindings.fill(nullptr);
	altitude = {};
	if (!enabled)
		return;
	// Merge identical placements shared by scene endpoints. Their weights should
	// sum, not produce two copies of the same absorbing medium during a transition.
	std::map<std::string, std::pair<VoxelCloudPlacement, float>> placements;
	try {
		for (const auto& endpoint : selection.states) {
			const float weight = endpoint.at("weight").get<float>();
			if (!(weight > 0.f))
				continue;
			for (const auto& item : endpoint.at("value").value("placements", nlohmann::json::array())) {
				auto placement = item.get<VoxelCloudPlacement>();
				if (!placement.enabled || placement.worldspace != worldspace || placement.worldspace.empty() ||
					!(placement.scale > 0.f && placement.strength > 0.f && placement.density > 0.f))
					continue;
				auto& entry = placements[item.dump()];
				entry.first = std::move(placement);
				entry.second += weight;
			}
		}
	} catch (const nlohmann::json::exception& exception) {
		error = exception.what();
		return;
	}
	if (placements.empty())
		return;
	if (!Noise())
		return;
	bindings[kAssetSlots * 2] = noiseVoxel.get();
	std::map<std::string, uint32_t> slots;
	for (const auto& [_, entry] : placements) {
		const auto& [placement, weight] = entry;
		const auto found = assets.find(placement.asset);
		if (found == assets.end()) {
			error = "Missing voxel cloud asset: " + placement.asset;
			continue;
		}
		if (!slots.contains(placement.asset) && slots.size() >= kAssetSlots) {
			error = "At most 8 different voxel cloud assets can be visible across scene transition endpoints";
			continue;
		}
		if (active.size() >= D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION / 32u) {
			error = "Voxel cloud lighting supports at most 64 simultaneous placements";
			break;
		}
		const auto& asset = found->second;
		const float assetBoxSize = std::max({ asset.size.x, asset.size.y, asset.size.z });
		const float scale = placement.scale * (placement.boxSize > 0.f ? placement.boxSize / assetBoxSize : 1.f);
		const float frequency = placement.noiseFrequency > 0.f ? placement.noiseFrequency : asset.noiseFrequency;
		const float adjustmentDistance = placement.noiseFrequencyAdjustmentDistance >= 0.f ? placement.noiseFrequencyAdjustmentDistance : asset.noiseFrequencyAdjustmentDistance;
		const float mipDistance = placement.noiseMipDistance > 0.f ? placement.noiseMipDistance : asset.noiseMipDistance;
		const bool noiseMips = placement.noiseMipsEnabled >= 0 ? placement.noiseMipsEnabled != 0 : asset.noiseMipsEnabled;
		auto& resource = LoadResource(placement.asset, asset);
		if (!resource.views[0])
			continue;
		const auto [slot, inserted] = slots.try_emplace(placement.asset, static_cast<uint32_t>(slots.size()));
		if (inserted)
			for (uint32_t channel = 0; channel < resource.views.size(); ++channel)
				bindings[channel * kAssetSlots + slot->second] = resource.views[channel].get();
		const auto angle = placement.rotation * (std::numbers::pi_v<float> / 180.f);
		const auto rotation = DirectX::SimpleMath::Matrix::CreateRotationX(angle.x) *
		                      DirectX::SimpleMath::Matrix::CreateRotationY(angle.y) * DirectX::SimpleMath::Matrix::CreateRotationZ(angle.z);
		const float3 axisX = float3::TransformNormal({ 1.f, 0.f, 0.f }, rotation);
		const float3 axisY = float3::TransformNormal({ 0.f, 1.f, 0.f }, rotation);
		const float3 axisZ = float3::TransformNormal({ 0.f, 0.f, 1.f }, rotation);
		VoxelCloudData cloud{
			.centerWeight = { placement.center.x, placement.center.y, placement.center.z, std::clamp(weight, 0.f, 1.f) },
			.axisX = { axisX.x, axisX.y, axisX.z, 1.f / (asset.size.x * scale) },
			.axisY = { axisY.x, axisY.y, axisY.z, 1.f / (asset.size.y * scale) },
			.axisZ = { axisZ.x, axisZ.y, axisZ.z, 1.f / (asset.size.z * scale) },
			.parameters = { scale, placement.density, std::clamp(placement.strength, 0.f, 1.f), static_cast<float>(slot->second) },
			.noise = { frequency, noiseMips ? 1.f / mipDistance : 0.f, adjustmentDistance, 0.f }
		};
		const auto half = HalfBounds(cloud);
		if (active.empty())
			altitude = { placement.center.z - half.z, placement.center.z + half.z };
		else {
			altitude.x = std::min(altitude.x, placement.center.z - half.z);
			altitude.y = std::max(altitude.y, placement.center.z + half.z);
		}
		active.push_back(cloud);
	}
	if (active.empty())
		return;
	if (capacity != Count()) {
		capacity = Count();
		data = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<VoxelCloudData>(capacity, true), capacity, "PhysicalSky::VoxelCloudPlacements");
		data->CreateSRV();
	}
	data->Update(active.data(), active.size() * sizeof(VoxelCloudData));
	bindings.back() = data->SRV();
}

void VoxelCloudManager::Bind() const
{
	globals::d3d::context->CSSetShaderResources(60, static_cast<UINT>(bindings.size()), bindings.data());
}

void VoxelCloudManager::UpdateLighting(ID3D11ComputeShader* shader, float3 direction)
{
	if (!shader || active.empty())
		return;
	auto geometry = active;
	for (auto& cloud : geometry) {
		// The cached integral is in local UVW under a directional light. Moving
		// the placement cannot change it; density and fade are applied at lookup.
		cloud.centerWeight = {};
		cloud.parameters.y = 0.f;
		cloud.noise = {};
	}
	const bool changed = geometry.size() != lightingGeometry.size() ||
	                     std::memcmp(geometry.data(), lightingGeometry.data(), geometry.size() * sizeof(VoxelCloudData)) != 0;
	const UINT slices = Count() * 32u;
	if (!lighting || lighting->desc.ArraySize != slices) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 256;
		desc.ArraySize = slices;
		desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R16_FLOAT;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		lighting = eastl::make_unique<Texture2D>(desc, "PhysicalSky::VoxelCloudLighting");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srv.Texture2DArray.MipLevels = 1;
		srv.Texture2DArray.ArraySize = slices;
		lighting->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uav.Texture2DArray.ArraySize = slices;
		lighting->CreateUAV(uav);
		lightingValid = false;
	}
	if (!lightingConstants)
		lightingConstants = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<uint32_t, 4>>());
	const bool rebuild = changed || !lightingValid;
	if (rebuild || (lightingRemaining == 0 && (direction - lightingDirection).LengthSquared() > 1e-8f)) {
		lightingGeometry = std::move(geometry);
		lightingDirection = direction;
		lightingNextSlice = 0;
		lightingRemaining = slices;
	}
	auto* context = globals::d3d::context;
	if (lightingRemaining > 0) {
		ID3D11ShaderResourceView* nullView = nullptr;
		context->CSSetShaderResources(85, 1, &nullView);
		const uint32_t count = rebuild ? lightingRemaining : std::min(lightingRemaining, 4u);
		lightingConstants->Update(std::array<uint32_t, 4>{ lightingNextSlice, count, 0u, 0u });
		auto* cb = lightingConstants->CB();
		context->CSSetConstantBuffers(4, 1, &cb);
		auto* output = lighting->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->CSSetShader(shader, nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::VoxelCloudLighting");
		context->Dispatch(64, 64, (count + 3u) / 4u);
		globals::profiler->EndPass();
		output = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		cb = nullptr;
		context->CSSetConstantBuffers(4, 1, &cb);
		lightingNextSlice += count;
		lightingRemaining -= count;
		lightingValid = true;
	}
	auto* view = lighting->srv.get();
	context->CSSetShaderResources(85, 1, &view);
}

std::array<uint32_t, 4> VoxelCloudManager::ScreenRect(float bottomZ, uint32_t width, uint32_t height) const
{
	if (active.empty())
		return {};
	if (REL::Module::IsVR())
		return { 0, 0, width, height };
	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto matrix = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	float2 lower{ 1.f, 1.f }, upper{};
	for (const auto& cloud : active) {
		// Match the longest local lighting probe, as for storm screen rectangles.
		const float3 extent = HalfBounds(cloud) + float3{ 240.f, 240.f, 240.f };
		uint32_t outside = 31u;
		bool crossesEye = false;
		float2 boxLower{ 1.f, 1.f }, boxUpper{};
		for (uint32_t corner = 0; corner < 8; ++corner) {
			const float4 point{
				(cloud.centerWeight.x + (corner & 1u ? extent.x : -extent.x)) / Util::Units::GAME_UNIT_TO_M - camera.x,
				(cloud.centerWeight.y + (corner & 2u ? extent.y : -extent.y)) / Util::Units::GAME_UNIT_TO_M - camera.y,
				(cloud.centerWeight.z + (corner & 4u ? extent.z : -extent.z)) / Util::Units::GAME_UNIT_TO_M + bottomZ - camera.z, 1.f
			};
			const auto clip = float4::Transform(point, matrix);
			crossesEye |= clip.w <= 0.f;
			if (clip.w > 0.f) {
				const float x = clip.x / clip.w * .5f + .5f, y = .5f - clip.y / clip.w * .5f;
				boxLower.x = std::min(boxLower.x, x);
				boxLower.y = std::min(boxLower.y, y);
				boxUpper.x = std::max(boxUpper.x, x);
				boxUpper.y = std::max(boxUpper.y, y);
			}
			outside &= (clip.x < -clip.w ? 1u : 0u) | (clip.x > clip.w ? 2u : 0u) |
			           (clip.y < -clip.w ? 4u : 0u) | (clip.y > clip.w ? 8u : 0u) | (clip.w <= 0.f ? 16u : 0u);
		}
		if (outside)
			continue;
		if (crossesEye)
			return { 0, 0, width, height };
		lower.x = std::min(lower.x, boxLower.x);
		lower.y = std::min(lower.y, boxLower.y);
		upper.x = std::max(upper.x, boxUpper.x);
		upper.y = std::max(upper.y, boxUpper.y);
	}
	if (upper.x <= lower.x || upper.y <= lower.y)
		return {};
	return { static_cast<uint32_t>(std::max(0.f, std::floor(lower.x * width) - 1.f)),
		static_cast<uint32_t>(std::max(0.f, std::floor(lower.y * height) - 1.f)),
		static_cast<uint32_t>(std::min(float(width), std::ceil(upper.x * width) + 1.f)),
		static_cast<uint32_t>(std::min(float(height), std::ceil(upper.y * height) + 1.f)) };
}

void VoxelCloudManager::DrawSettings(SceneBlend& selection, const std::string& worldspace, float3 cameraMeters)
{
	if (!scanned)
		LoadInstances();
	if (ImGui::Button(T(TKEY("voxel_scan"), "Reload voxel cloud assets")))
		LoadInstances();
	ImGui::TextUnformatted(kRoot);
	ImGui::TextWrapped("%s", T(TKEY("voxel_scene_hint"), "Each scene has its own voxel cloud placements. When switching scenes, the two cloud configurations fade in and out."));
	const auto* armed = SceneWidgetInterceptor::GetArmedContext();
	const bool scene = armed && !armed->baseline;
	auto* manager = SceneSettingsManager::GetSingleton();
	const SceneSettingsManager::SettingIdentity identity{ "PhysicalSky", {}, "voxelClouds" };
	std::optional<size_t> entryIndex;
	nlohmann::json value = selection;
	bool editable = true;
	if (scene) {
		entryIndex = manager->FindContextUserEntry(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
		const auto entries = manager->GetContextEntries(armed->contextId);
		if (!entryIndex || *entryIndex >= entries.size() || entries[*entryIndex].deleted) {
			if (ImGui::Button(T(TKEY("voxel_override"), "Override voxel clouds in this scene"))) {
				if (entryIndex)
					manager->ClearContextTombstone(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
				manager->AddContextSetting(armed->contextId, identity.featureShortName, identity.settingPath, identity.settingKey);
			}
			return;
		}
		value = entries[*entryIndex].value;
		editable = !entries[*entryIndex].paused;
		if (ImGui::Button(T(TKEY("voxel_inherit"), "Remove voxel cloud override / inherit"))) {
			manager->RemoveContextSetting(armed->contextId, *entryIndex);
			return;
		}
	}
	std::vector<VoxelCloudPlacement> placements;
	if (SceneBlend::IsValid(value) && !value["states"].empty()) {
		const auto endpoint = std::max_element(value["states"].begin(), value["states"].end(), [](const auto& a, const auto& b) {
			return a["weight"].template get<float>() < b["weight"].template get<float>();
		});
		try {
			placements = endpoint->at("value").value("placements", nlohmann::json::array()).get<std::vector<VoxelCloudPlacement>>();
		} catch (const nlohmann::json::exception&) {}
		if (value["states"].size() > 1)
			ImGui::TextWrapped("%s", T(TKEY("voxel_edit_mixed"), "A scene transition is active. Editing uses the cloud configuration with the highest current weight."));
	}
	ImGui::BeginDisabled(!editable);
	bool changed = false;
	if (ImGui::BeginCombo(T(TKEY("voxel_add"), "Add voxel cloud"), "")) {
		for (const auto& [id, asset] : assets)
			if (ImGui::Selectable((asset.name.empty() ? id : asset.name).c_str())) {
				auto placement = asset.placement;
				placement.asset = id;
				placement.worldspace = worldspace;
				placement.center = cameraMeters;
				placements.push_back(placement);
				changed = true;
			}
		ImGui::EndCombo();
	}
	for (size_t i = 0; i < placements.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto& placement = placements[i];
		if (ImGui::TreeNode(placement.asset.c_str())) {
			changed |= ImGui::Checkbox(T(TKEY("voxel_enabled"), "Enable placement"), &placement.enabled);
			changed |= ImGui::InputText(T(TKEY("voxel_worldspace"), "Worldspace Editor ID"), &placement.worldspace);
			changed |= ImGui::DragFloat3(T(TKEY("voxel_center"), "Center X / Y / Z (m)"), &placement.center.x, 1.f);
			changed |= ImGui::DragFloat3(T(TKEY("voxel_rotation"), "Rotation X / Y / Z (degrees)"), &placement.rotation.x, 1.f);
			changed |= ImGui::DragFloat(T(TKEY("voxel_scale"), "Uniform scale"), &placement.scale, .01f, .001f, 100.f);
			changed |= ImGui::SliderFloat(T(TKEY("voxel_strength"), "Height reveal"), &placement.strength, 0.f, 1.f);
			changed |= ImGui::DragFloat(T(TKEY("voxel_density"), "Density multiplier"), &placement.density, .01f, 0.f, 10.f);
			if (const auto found = assets.find(placement.asset); found != assets.end()) {
				const auto& asset = found->second;
				float boxSize = placement.boxSize > 0.f ? placement.boxSize : std::max({ asset.size.x, asset.size.y, asset.size.z });
				if (ImGui::DragFloat(T(TKEY("voxel_box_size"), "Bounding box longest axis (m)"), &boxSize, 1.f, 1.f, 100000.f)) {
					placement.boxSize = boxSize;
					changed = true;
				}
				float frequency = placement.noiseFrequency > 0.f ? placement.noiseFrequency : asset.noiseFrequency;
				if (ImGui::DragFloat(T(TKEY("voxel_noise_frequency"), "Noise frequency multiplier"), &frequency, .01f, .001f, 100.f)) {
					placement.noiseFrequency = frequency;
					changed = true;
				}
				float adjustment = placement.noiseFrequencyAdjustmentDistance >= 0.f ? placement.noiseFrequencyAdjustmentDistance : asset.noiseFrequencyAdjustmentDistance;
				if (ImGui::DragFloat(T(TKEY("voxel_noise_adjustment"), "Noise frequency adjustment distance (m)"), &adjustment, 1.f, 0.f, 100000.f)) {
					placement.noiseFrequencyAdjustmentDistance = adjustment;
					changed = true;
				}
				bool mips = placement.noiseMipsEnabled >= 0 ? placement.noiseMipsEnabled != 0 : asset.noiseMipsEnabled;
				if (ImGui::Checkbox(T(TKEY("voxel_noise_mips"), "Adjust noise detail with distance"), &mips)) {
					placement.noiseMipsEnabled = mips ? 1 : 0;
					changed = true;
				}
				float mipDistance = placement.noiseMipDistance > 0.f ? placement.noiseMipDistance : asset.noiseMipDistance;
				ImGui::BeginDisabled(!mips);
				if (ImGui::DragFloat(T(TKEY("voxel_noise_mip_distance"), "Noise detail transition distance (m)"), &mipDistance, 1.f, 1.f, 100000.f)) {
					placement.noiseMipDistance = mipDistance;
					changed = true;
				}
				ImGui::EndDisabled();
				if (ImGui::Button(T(TKEY("voxel_asset_defaults"), "Restore default size and noise settings"))) {
					placement.boxSize = placement.noiseFrequency = placement.noiseMipDistance = 0.f;
					placement.noiseFrequencyAdjustmentDistance = -1.f;
					placement.noiseMipsEnabled = -1;
					changed = true;
				}
			}
			if (ImGui::Button(T(TKEY("voxel_camera"), "Move to camera / current worldspace"))) {
				placement.center = cameraMeters;
				placement.worldspace = worldspace;
				changed = true;
			}
			if (ImGui::Button(T(TKEY("voxel_remove"), "Remove placement"))) {
				placements.erase(placements.begin() + i);
				--i;
				changed = true;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::EndDisabled();
	if (changed && editable) {
		SceneBlend updated;
		updated.states.push_back({ { "value", { { "placements", placements } } }, { "weight", 1.f } });
		if (scene && entryIndex) {
			const SceneSettingsManager::EntryValueUpdate update{ *entryIndex, updated };
			manager->UpdateContextEntryValues(armed->contextId, { &update, 1 }, true);
		} else {
			selection = updated;
			manager->RecordBaselineEdit(identity, updated);
		}
		globals::features::physicalSky.volMainHistoryValid = false;
	}
	if (!error.empty())
		ImGui::TextWrapped("%s: %s", T(TKEY("voxel_error"), "Voxel cloud asset error"), error.c_str());
}

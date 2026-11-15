#include "Features/PhysicalSky.h"

#include <cctype>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <imgui_stdlib.h>
#include <sstream>

#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "I18n/I18n.h"
#include "Utils/Form.h"

#define I18N_KEY_PREFIX "feature.physical_sky."

namespace
{
	constexpr const char* kStormsDirectory = "Data/Textures/PhysicalSky/Storms";
	constexpr uint32_t kDimension = 512;

	std::filesystem::path StormAssetPath(const std::string& instance, const std::string& asset)
	{
		const std::filesystem::path relative(asset);
		if (asset.empty() || relative.has_root_path() || std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
			return {};
		return std::filesystem::path(kStormsDirectory) / instance / relative;
	}

	eastl::unique_ptr<Texture2D> StormTexture(uint32_t count, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = kDimension;
		desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.ArraySize = count;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto texture = eastl::make_unique<Texture2D>(desc, name);
		texture->CreateSRV({ .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY, .Texture2DArray = { .MipLevels = 1, .ArraySize = count } });
		return texture;
	}
}

void CloudStormManager::LoadInstances()
{
	instances.clear();
	error.clear();
	std::error_code ec;
	for (std::filesystem::directory_iterator it(kStormsDirectory, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code fileError;
		if (!it->is_directory(fileError))
			continue;
		const auto path = it->path() / "Instance.json";
		if (!std::filesystem::is_regular_file(path, fileError))
			continue;
		try {
			std::ifstream stream(path);
			instances.emplace(it->path().filename().string(), nlohmann::json::parse(stream).get<CloudStormSettings>());
		} catch (const std::exception& exception) {
			error = path.filename().string() + ": " + exception.what();
			logger::error("PhysicalSky storm instance {}: {}", path.string(), exception.what());
		}
	}
	if (!instances.contains(selected))
		selected = instances.empty() ? "" : instances.begin()->first;
	key.clear();
	++revision;
}

void CloudStormManager::SetupResources()
{
	generatorCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CloudStormData>(), "PhysicalSky::StormInstanceConstants");
	D3D11_SAMPLER_DESC desc{};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&desc, sampler.put()));
	Util::SetResourceName(sampler.get(), "PhysicalSky::StormNdfSampler");
}

void CloudStormManager::CompileShaders(Util::AsyncShaderSet& a_shaders)
{
	key.clear();
	active.clear();
	wind.clear();
	references.clear();
	replacementNodes.clear();
	replacementTextures.clear();
	a_shaders.Add(program, L"Data\\Shaders\\PhysicalSky\\StormNdf.cs.hlsl");
}

void CloudStormManager::Update(TextureManager& textures, ID3D11ShaderResourceView* pattern, uint64_t noiseRevision, const std::string& worldspace, bool enabled)
{
	if (!enabled || !program || !generatorCb || !sampler || !pattern) {
		active.clear();
		wind.clear();
		references.clear();
		replacementNodes.clear();
		replacementTextures.clear();
		key.clear();
		altitude = {};
		return;
	}
	const auto nextKey = nlohmann::json{ worldspace, textures.revision, noiseRevision }.dump();
	if (key == nextKey)
		return;
	active.clear();
	wind.clear();
	references.clear();
	replacementNodes.clear();
	replacementTextures.clear();
	altitude = { FLT_MAX, -FLT_MAX };
	std::vector<std::array<ID3D11ShaderResourceView*, 5>> sources;
	for (const auto& [id, instance] : instances) {
		if (!instance.enabled || instance.worldspace.empty() || instance.worldspace != worldspace)
			continue;
		const float values[] = { instance.center.x, instance.center.y, instance.radius, instance.backHalfSize,
			instance.backRotation, instance.coreHeight, instance.altitude, instance.height, instance.strength,
			instance.topOffset.x, instance.topOffset.y, instance.windInfluence, instance.rotation, instance.updraft,
			instance.density, instance.albedo, instance.emission.x, instance.emission.y, instance.emission.z, instance.emissionHeight };
		if (!std::ranges::all_of(values, [](float v) { return std::isfinite(v); }) ||
			instance.radius < 1.f || instance.height < 1.f || instance.strength <= 0.f || instance.density <= 0.f)
			continue;
		NdfTextureSet maps;
		if (!instance.asset.empty()) {
			const auto path = StormAssetPath(id, instance.asset);
			if (path.empty())
				continue;
			const auto heightPath = (path / "Height.dds").string();
			const auto modelingPath = (path / "Modeling.dds").string();
			textures.EnsureLoaded(heightPath);
			textures.EnsureLoaded(modelingPath);
			maps = { textures.Query(heightPath), textures.Query(modelingPath) };
			if (!NdfManager::IsTexturePair(maps))
				continue;
		}
		NdfTextureSet coreMaps;
		if (!instance.coreAsset.empty()) {
			const auto path = StormAssetPath(id, instance.coreAsset);
			if (path.empty())
				continue;
			const auto heightPath = (path / "Height.dds").string();
			const auto modelingPath = (path / "Modeling.dds").string();
			textures.EnsureLoaded(heightPath);
			textures.EnsureLoaded(modelingPath);
			coreMaps = { textures.Query(heightPath), textures.Query(modelingPath) };
			if (!NdfManager::IsTexturePair(coreMaps))
				continue;
		}
		// The reference core pattern is Perlin-Worley. Weather noise slots are editable and
		// our default slot 2 is Perlin, so an authored storm must carry its own
		// core pattern rather than inherit that slot's different morphology.
		auto* corePattern = pattern;
		const auto patternPath = std::filesystem::path(kStormsDirectory) / id / "Pattern.dds";
		if (!coreMaps && std::filesystem::is_regular_file(patternPath)) {
			textures.EnsureLoaded(patternPath.string());
			corePattern = textures.Query(patternPath.string());
			if (!NdfManager::IsTextureNdf(corePattern, 1)) {
				logger::error("PhysicalSky storm instance {}: invalid core pattern {}", id, patternPath.string());
				continue;
			}
		}
		CloudStormData gpu;
		gpu.centerRadiusStrength = { instance.center.x, instance.center.y, std::clamp(instance.radius, 1.f, 16000.f), std::clamp(instance.strength, 0.f, 1.f) };
		gpu.altitudeMotion = { std::clamp(instance.altitude, -24000.f, 48000.f), std::clamp(instance.height, 1.f, 24000.f),
			std::clamp(instance.rotation, -4.f, 4.f), std::clamp(instance.updraft, -80.f, 80.f) };
		gpu.driftDensity = { instance.mode == "Volcanic" ? std::clamp(instance.topOffset.x, -16000.f, 16000.f) : 0.f,
			instance.mode == "Volcanic" ? std::clamp(instance.topOffset.y, -16000.f, 16000.f) : 0.f,
			std::clamp(instance.density, 0.f, 4.f), std::clamp(instance.albedo, 0.f, 0.9999f) };
		gpu.emission = { std::max(instance.emission.x, 0.f), std::max(instance.emission.y, 0.f), std::max(instance.emission.z, 0.f), std::clamp(instance.emissionHeight, 0.001f, 1.f) };
		const float backSize = std::clamp(instance.backHalfSize, 1.f, 16000.f);
		gpu.animation = { 0.f, 0.f, maps ? std::max(gpu.centerRadiusStrength.z, backSize) : gpu.centerRadiusStrength.z, backSize };
		gpu.generation = { instance.backRotation * 0.01745329252f, instance.mirrorBack ? 1.f : 0.f, std::clamp(instance.coreHeight, 0.f, 1.f), (maps ? 1.f : 0.f) + (coreMaps ? 2.f : 0.f) + (instance.mode == "Volcanic" ? 4.f : 0.f) };
		active.push_back(gpu);
		wind.push_back({ .baseOffset = { gpu.driftDensity.x, gpu.driftDensity.y },
			.influence = instance.mode == "Volcanic" ? std::clamp(instance.windInfluence, 0.f, 2.f) : 0.f });
		for (const auto& node : instance.replacementNodes)
			if (!node.empty())
				replacementNodes.emplace_back(node);
		sources.push_back({ corePattern, maps.height, maps.modeling, coreMaps.height, coreMaps.modeling });
		const auto formID = instance.replacementReference.empty() ? 0 : Util::SpidToFormId(instance.replacementReference);
		references.push_back(formID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(formID) : nullptr);
		for (const auto& referenceID : instance.replacementReferences) {
			const auto form = Util::SpidToFormId(referenceID);
			if (auto* ref = form ? RE::TESForm::LookupByID<RE::TESObjectREFR>(form) : nullptr)
				references.push_back(ref);
		}
		for (const auto& texture : instance.replacementTextures)
			if (!texture.empty())
				replacementTextures.push_back(texture);
		altitude.x = std::min(altitude.x, gpu.altitudeMotion.x);
		altitude.y = std::max(altitude.y, gpu.altitudeMotion.x + gpu.altitudeMotion.y);
	}
	key = nextKey;
	if (active.empty()) {
		altitude = {};
		return;
	}
	const auto count = Count();
	if (!height || height->desc.ArraySize != count) {
		height = StormTexture(count, "PhysicalSky::StormHeight");
		modeling = StormTexture(count, "PhysicalSky::StormModeling");
		data = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<CloudStormData>(count, true), count, "PhysicalSky::StormInstances");
		data->CreateSRV();
		targets.clear();
		targets.resize(count);
		Texture2D* maps[] = { height.get(), modeling.get() };
		for (uint32_t index = 0; index < count; ++index)
			for (uint32_t map = 0; map < 2; ++map) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC desc{ .Format = maps[map]->desc.Format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY, .Texture2DArray = { .FirstArraySlice = index, .ArraySize = 1 } };
				DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(maps[map]->resource.get(), &desc, targets[index][map].put()));
				Util::SetResourceName(targets[index][map].get(), std::format("PhysicalSky::Storm {} Slice {} UAV", map, index).c_str());
			}
	}
	auto* context = globals::d3d::context;
	winrt::com_ptr<ID3D11ComputeShader> previousShader;
	winrt::com_ptr<ID3D11Buffer> previousCb;
	winrt::com_ptr<ID3D11SamplerState> previousSampler;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5> previousSources;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> previousStorm;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 2> previousTargets;
	context->CSGetShader(previousShader.put(), nullptr, nullptr);
	context->CSGetConstantBuffers(1, 1, previousCb.put());
	context->CSGetSamplers(0, 1, previousSampler.put());
	for (UINT i = 0; i < 3; ++i) {
		context->CSGetShaderResources(55 + i, 1, previousStorm[i].put());
	}
	for (UINT i = 0; i < 5; ++i)
		context->CSGetShaderResources(i, 1, previousSources[i].put());
	for (UINT i = 0; i < 2; ++i)
		context->CSGetUnorderedAccessViews(i, 1, previousTargets[i].put());
	ID3D11ShaderResourceView* empty[5] = {};
	context->CSSetShaderResources(55, 3, empty);
	auto* buffer = generatorCb->CB();
	auto* samp = sampler.get();
	context->CSSetConstantBuffers(1, 1, &buffer);
	context->CSSetSamplers(0, 1, &samp);
	context->CSSetShader(program.get(), nullptr, 0);
	globals::profiler->BeginPass("PhysicalSky::StormNdf");
	for (UINT i = 0; i < count; ++i) {
		generatorCb->Update(active[i]);
		ID3D11UnorderedAccessView* outputs[] = { targets[i][0].get(), targets[i][1].get() };
		context->CSSetShaderResources(0, 5, sources[i].data());
		context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		context->Dispatch(kDimension / 8, kDimension / 8, 1);
	}
	globals::profiler->EndPass();
	ID3D11UnorderedAccessView* emptyTargets[2] = {};
	context->CSSetUnorderedAccessViews(0, 2, emptyTargets, nullptr);
	context->CSSetShaderResources(0, 5, empty);
	context->CSSetShader(previousShader.get(), nullptr, 0);
	buffer = previousCb.get();
	samp = previousSampler.get();
	context->CSSetConstantBuffers(1, 1, &buffer);
	context->CSSetSamplers(0, 1, &samp);
	for (UINT i = 0; i < 3; ++i) {
		auto* stormSource = previousStorm[i].get();
		context->CSSetShaderResources(55 + i, 1, &stormSource);
	}
	for (UINT i = 0; i < 5; ++i) {
		auto* source = previousSources[i].get();
		context->CSSetShaderResources(i, 1, &source);
	}
	for (UINT i = 0; i < 2; ++i) {
		auto* target = previousTargets[i].get();
		context->CSSetUnorderedAccessViews(i, 1, &target, nullptr);
	}
	data->Update(active.data(), active.size() * sizeof(CloudStormData));
	++revision;
}

void CloudStormManager::UpdatePlume(CloudStormData& instance, WindState& motion, float2 lowWind, float2 highWind, float lowAltitude, float highAltitude, double time)
{
	if ((static_cast<uint32_t>(instance.generation.w) & 4u) == 0u)
		return;
	const bool initialize = motion.time < 0.0 || time < motion.time;
	const float dt = initialize ? 0.f : static_cast<float>(time - motion.time);
	const float span = instance.altitudeMotion.y, radius = instance.centerRadiusStrength.z;
	const float rise = std::max(instance.altitudeMotion.w, 1.f);
	const float travel = span / rise;
	const auto velocityAt = [&](float z) {
		const float blend = std::clamp((z - lowAltitude) / std::max(highAltitude - lowAltitude, 1.f), 0.f, 1.f);
		return (lowWind + (highWind - lowWind) * blend) * motion.influence;
	};
	constexpr float heights[] = { .05f, .22f, .41f, .63f, .79f, .79f, .76f, .72f };
	constexpr float sizes[] = { .11f, .17f, .24f, .32f, .36f, .40f, .45f, .50f };
	float2 target{}, horizontal{};
	float age = 0.f;
	instance.plumeLower = { FLT_MAX, FLT_MAX, FLT_MAX, 0.f };
	instance.plumeUpper = { -FLT_MAX, -FLT_MAX, -FLT_MAX, 0.f };
	for (size_t i = 0; i < instance.parcels.size(); ++i) {
		auto& parcel = instance.parcels[i];
		const float z = instance.altitudeMotion.x + heights[i] * span;
		const float r = sizes[i] * radius;
		float vertical = rise * (1.6f - heights[i]);
		if (i > 0 && i < 5) {
			// Integrate entrained horizontal momentum along the rising trajectory.
			// The vent is fixed, but every subsequent section responds to the wind.
			const float segmentTime = (heights[i] - heights[i - 1]) * span / vertical;
			const float entrainment = std::max(r / rise, 1.f);
			const float2 ambient = velocityAt(z - .5f * (heights[i] - heights[i - 1]) * span);
			const float response = -std::expm1(-segmentTime / entrainment);
			target += ambient * segmentTime + (horizontal - ambient) * (entrainment * response);
			horizontal += (ambient - horizontal) * response;
			age += segmentTime;
		} else if (i >= 5) {
			const float segmentTime = travel * .35f;
			horizontal = velocityAt(instance.altitudeMotion.x + .79f * span);
			target += horizontal * segmentTime;
			age += segmentTime;
			vertical = (heights[i] - heights[i - 1]) * span / segmentTime;
		}
		const float manualBlend = heights[i] * heights[i];
		float2 centre = float2{ instance.centerRadiusStrength.x, instance.centerRadiusStrength.y } + target + motion.baseOffset * manualBlend;
		if (i == 0)
			centre = { instance.centerRadiusStrength.x, instance.centerRadiusStrength.y };
		const float responseTime = std::clamp(age * .2f, 2.f, 45.f);
		const float blend = initialize ? 1.f : -std::expm1(-dt / responseTime);
		if (!initialize)
			centre = float2{ parcel.sphere.x, parcel.sphere.y } + (centre - float2{ parcel.sphere.x, parcel.sphere.y }) * blend;
		// Bound each connected segment, not the whole plume. Long downwind
		// transport remains possible without opening holes between the parcels.
		if (i > 0) {
			const auto& previous = instance.parcels[i - 1].sphere;
			float2 delta = centre - float2{ previous.x, previous.y };
			const float separation = .98f * (previous.w + r);
			const float dz = z - previous.z;
			const float limit = std::sqrt(std::max(separation * separation - dz * dz, 0.f));
			const float length = delta.Length();
			if (length > limit && length > 0.f)
				centre = float2{ previous.x, previous.y } + delta * (limit / length);
		}
		parcel.sphere = { centre.x, centre.y, z, r };
		const float dilution = i < 5 ? 1.f : std::pow(sizes[4] / sizes[i], 3.f) * std::exp(-1.5f * static_cast<float>(i - 4));
		parcel.flow = { horizontal.x, horizontal.y, vertical, dilution };
		// Independently orient each local roll around the trajectory. Use rigid
		// transforms only: neither bending nor downwind spread scales the noise.
		float3 tangent{ horizontal.x, horizontal.y, vertical };
		if (tangent.LengthSquared() < 1e-6f)
			tangent = { 0.f, 0.f, 1.f };
		tangent.Normalize();
		const float azimuth = static_cast<float>(i) * 2.39996323f;
		float3 outward{ std::cos(azimuth), std::sin(azimuth), 0.f };
		const float windSpeed = horizontal.Length();
		if (i >= 4 && windSpeed > 1.f) {
			outward += float3{ horizontal.x / windSpeed, horizontal.y / windSpeed, 0.f } * 1.5f;
			outward.Normalize();
		}
		float3 axis = tangent.Cross(outward);
		if (axis.LengthSquared() < 1e-6f)
			axis = { 0.f, 1.f, 0.f };
		axis.Normalize();
		const float omega = rise / std::max(r, 1.f) * (.45f + .15f * std::sin(azimuth)) * instance.altitudeMotion.z;
		if (initialize) {
			parcel.rotation = { 0.f, 0.f, 0.f, 1.f };
			parcel.noiseOffset = { -centre.x, -centre.y, -z, 0.f };
		} else {
			using DirectX::SimpleMath::Quaternion;
			Quaternion rotation{ parcel.rotation.x, parcel.rotation.y, parcel.rotation.z, parcel.rotation.w };
			rotation *= Quaternion::CreateFromAxisAngle(axis, -omega * dt);
			rotation.Normalize();
			parcel.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
			const float3 velocity = float3::Transform(float3{ parcel.flow.x, parcel.flow.y, parcel.flow.z }, rotation);
			parcel.noiseOffset.x += velocity.x * dt;
			parcel.noiseOffset.y += velocity.y * dt;
			parcel.noiseOffset.z += velocity.z * dt;
		}
		if (initialize)
			motion.history[i] = parcel;
		const float extent = r + 200.f;
		instance.plumeLower.x = std::min(instance.plumeLower.x, centre.x - extent);
		instance.plumeLower.y = std::min(instance.plumeLower.y, centre.y - extent);
		instance.plumeLower.z = std::min(instance.plumeLower.z, z - extent);
		instance.plumeUpper.x = std::max(instance.plumeUpper.x, centre.x + extent);
		instance.plumeUpper.y = std::max(instance.plumeUpper.y, centre.y + extent);
		instance.plumeUpper.z = std::max(instance.plumeUpper.z, z + extent);
	}
}

void CloudStormManager::UpdateMotion(double time, double previousTime, float2 lowWind, float2 highWind, float lowWindAltitude, float highWindAltitude)
{
	if (active.empty() || !data)
		return;
	for (size_t index = 0; index < active.size(); ++index) {
		auto& instance = active[index];
		auto& motion = wind[index];
		if (motion.time != time) {
			UpdatePlume(instance, motion, lowWind, highWind, lowWindAltitude, highWindAltitude, time);
			motion.time = time;
		}
		instance.animation.x = static_cast<float>(time);
		instance.animation.y = static_cast<float>(previousTime);
		for (size_t i = 0; i < instance.parcels.size(); ++i) {
			auto& parcel = instance.parcels[i];
			const auto& previous = previousTime != time ? motion.history[i] : parcel;
			parcel.previousSphere = previous.sphere;
			parcel.previousRotation = previous.rotation;
			parcel.previousNoiseOffset = previous.noiseOffset;
		}
	}
	data->Update(active.data(), active.size() * sizeof(CloudStormData));
	if (active.size() == 1)
		generatorCb->Update(active.front());
}

void CloudStormManager::CommitMotion()
{
	for (size_t index = 0; index < active.size(); ++index)
		wind[index].history = active[index].parcels;
}

bool CloudStormManager::Replaces(const RE::BSGeometry* geometry) const
{
	if (!geometry || active.empty())
		return false;
	for (const RE::NiAVObject* node = geometry; node; node = node->parent) {
		if (std::ranges::find(replacementNodes, node->name) != replacementNodes.end())
			return true;
		for (const auto* reference : references)
			if (reference && node == reference->Get3D())
				return true;
	}
	if (!replacementTextures.empty()) {
		auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
		auto* material = property ? property->GetBaseMaterial() : nullptr;
		if (material && material->GetType() == RE::BSShaderMaterial::Type::kEffect) {
			const auto* effect = static_cast<const RE::BSEffectShaderMaterial*>(material);
			const std::string_view actual = effect->sourceTexturePath.c_str();
			for (const auto& expected : replacementTextures) {
				const auto normalize = [](char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
				if (actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin(),
															[&](char a, char b) { return normalize(a) == normalize(b); }))
					return true;
			}
		}
	}
	return false;
}

std::array<uint32_t, 4> CloudStormManager::ScreenRect(float bottomZ, uint32_t width, uint32_t viewHeight, float2 weatherAltitude, float curvatureRadius) const
{
	if (REL::Module::IsVR())
		return active.empty() ? std::array<uint32_t, 4>{} : std::array<uint32_t, 4>{ 0, 0, width, viewHeight };
	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto matrix = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	float2 screenMin{ 1.f, 1.f }, screenMax{};
	for (const auto& storm : active) {
		// Include the longest local light probe, so an off-screen storm can still
		// shadow a visible cloud. Distant lighting and reflections use the full path.
		const float extent = storm.animation.z + 270.f;
		float3 lower{ storm.centerRadiusStrength.x + std::min(storm.driftDensity.x, 0.f) - extent,
			storm.centerRadiusStrength.y + std::min(storm.driftDensity.y, 0.f) - extent, storm.altitudeMotion.x - 270.f };
		float3 upper{ storm.centerRadiusStrength.x + std::max(storm.driftDensity.x, 0.f) + extent,
			storm.centerRadiusStrength.y + std::max(storm.driftDensity.y, 0.f) + extent, storm.altitudeMotion.x + storm.altitudeMotion.y + 270.f };
		if ((static_cast<uint32_t>(storm.generation.w) & 4u) != 0u) {
			lower = { storm.plumeLower.x - 270.f, storm.plumeLower.y - 270.f, storm.plumeLower.z - 270.f };
			upper = { storm.plumeUpper.x + 270.f, storm.plumeUpper.y + 270.f, storm.plumeUpper.z + 270.f };
		} else {
			// Match SupercellInfluenceBounds: NDF replacement includes the old
			// weather layer, not just the new storm's visible height range.
			const float farX = std::abs(storm.centerRadiusStrength.x - camera.x * 0.01428f) + extent;
			const float farY = std::abs(storm.centerRadiusStrength.y - camera.y * 0.01428f) + extent;
			const float curvature = (farX * farX + farY * farY) / (curvatureRadius * 0.01428f);
			lower.z = std::min(lower.z, weatherAltitude.x * 0.01428f - curvature - 270.f);
			upper.z = std::max(upper.z, weatherAltitude.y * 0.01428f + 270.f);
		}
		uint32_t outside = 31u;
		bool crossesEye = false;
		float2 lowerScreen{ 1.f, 1.f }, upperScreen{};
		for (uint32_t corner = 0; corner < 8; ++corner) {
			const float4 point{ ((corner & 1u) ? upper.x : lower.x) / 0.01428f - camera.x,
				((corner & 2u) ? upper.y : lower.y) / 0.01428f - camera.y,
				((corner & 4u) ? upper.z : lower.z) / 0.01428f + bottomZ - camera.z, 1.f };
			const auto clip = float4::Transform(point, matrix);
			crossesEye |= clip.w <= 0.f;
			if (clip.w > 0.f) {
				const float x = clip.x / clip.w * 0.5f + 0.5f;
				const float y = 0.5f - clip.y / clip.w * 0.5f;
				lowerScreen.x = std::min(lowerScreen.x, x);
				lowerScreen.y = std::min(lowerScreen.y, y);
				upperScreen.x = std::max(upperScreen.x, x);
				upperScreen.y = std::max(upperScreen.y, y);
			}
			outside &= (clip.x < -clip.w ? 1u : 0u) | (clip.x > clip.w ? 2u : 0u) |
			           (clip.y < -clip.w ? 4u : 0u) | (clip.y > clip.w ? 8u : 0u) | (clip.w <= 0.f ? 16u : 0u);
		}
		if (outside != 0u)
			continue;
		if (crossesEye)
			return { 0, 0, width, viewHeight };
		screenMin.x = std::min(screenMin.x, lowerScreen.x);
		screenMin.y = std::min(screenMin.y, lowerScreen.y);
		screenMax.x = std::max(screenMax.x, upperScreen.x);
		screenMax.y = std::max(screenMax.y, upperScreen.y);
	}
	if (screenMax.x <= screenMin.x || screenMax.y <= screenMin.y)
		return {};
	// Round out to whole groups, with one pixel for the temporal sample phase.
	const auto lowerPixel = [](float v, uint32_t size) { return static_cast<uint32_t>(std::max(std::floor(v * size) - 1.f, 0.f)) & ~7u; };
	const auto upperPixel = [](float v, uint32_t size) { return std::min((static_cast<uint32_t>(std::max(std::ceil(v * size) + 1.f, 0.f)) + 7u) & ~7u, size); };
	return { lowerPixel(screenMin.x, width), lowerPixel(screenMin.y, viewHeight), upperPixel(screenMax.x, width), upperPixel(screenMax.y, viewHeight) };
}

void PhysicalSky::UpdateCloudStorm(const std::string& worldspace)
{
	cloudStormManager.Update(ndfTexManager, ndfManager.GetNoiseInputs()[2], ndfManager.GetNoiseRevision(), worldspace,
		IsActive() && settings.enableVolumetricClouds);
}

bool PhysicalSky::IsReplacedSkyGeometry(const RE::BSGeometry* geometry) const
{
	return loaded && cbData.enabled && cloudStormManager.Replaces(geometry);
}

void PhysicalSky::Hooks::BSCullingProcess_AppendVirtual::thunk(RE::BSCullingProcess* process, RE::BSGeometry& geometry, std::int32_t alphaGroupIndex)
{
	// Cull before the geometry enters the shader accumulator/render-pass queues.
	// Leave engine visibility flags intact so weather and disabled instances restore normally.
	if (!globals::features::physicalSky.IsReplacedSkyGeometry(&geometry))
		func(process, geometry, alphaGroupIndex);
}

RE::BSShaderProperty::RenderPassArray* PhysicalSky::Hooks::BSEffectShaderProperty_GetRenderPasses::thunk(
	RE::BSEffectShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderMode, RE::BSShaderAccumulator* accumulator)
{
	// Ordered sky effects can be submitted without BSCullingProcess::AppendVirtual.
	// Reject them on the CPU before any effect passes or draw calls are produced.
	if (globals::features::physicalSky.IsReplacedSkyGeometry(geometry))
		return nullptr;
	return func(property, geometry, renderMode, accumulator);
}

void PhysicalSky::DrawCloudStormSection()
{
	cloudStormManager.DrawSettings();
}

void CloudStormManager::DrawSettings()
{
	if (!ImGui::CollapsingHeader(T(TKEY("storm_section"), "Storm Instances"), ImGuiTreeNodeFlags_SpanAvailWidth))
		return;
	ImGui::TextWrapped("%s", T(TKEY("storm_description"), "World-placed storm instances combine a supercell back template, mesocyclone NDF and rotating noise bands. Each instance has its own placement and replacement reference."));
	ImGui::TextUnformatted(kStormsDirectory);
	ImGui::BeginDisabled(SceneWidgetInterceptor::IsArmed());
	if (ImGui::Button(T(TKEY("storm_reload"), "Reload instances")))
		LoadInstances();
	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("storm_new"), "New instance"))) {
		for (uint32_t i = 1;; ++i) {
			const auto id = std::format("Storm{}", i);
			if (instances.contains(id))
				continue;
			selected = id;
			auto& instance = instances[id];
			instance.name = id;
			instance.enabled = false;
			key.clear();
			++revision;
			break;
		}
	}
	if (ImGui::BeginCombo(T(TKEY("storm_instance"), "Instance"), selected.c_str())) {
		for (const auto& [id, instance] : instances)
			if (ImGui::Selectable(id.c_str(), id == selected))
				selected = id;
		ImGui::EndCombo();
	}
	const auto found = instances.find(selected);
	if (found != instances.end()) {
		auto& storm = found->second;
		bool changed = false;
		changed |= ImGui::InputText(T(TKEY("storm_name"), "Name"), &storm.name);
		if (ImGui::BeginCombo(T(TKEY("storm_mode"), "Storm mode"), storm.mode.c_str())) {
			for (const char* mode : { "Supercell", "Volcanic" })
				if (ImGui::Selectable(mode, storm.mode == mode)) {
					storm.mode = mode;
					changed = true;
				}
			ImGui::EndCombo();
		}
		changed |= ImGui::Checkbox(T(TKEY("storm_enabled"), "Enable storm"), &storm.enabled);
		changed |= ImGui::InputText(T(TKEY("storm_asset"), "Storm back folder (relative to instance)"), &storm.asset);
		changed |= ImGui::InputText(T(TKEY("storm_core_asset"), "Core NDF folder (relative to instance, optional)"), &storm.coreAsset);
		changed |= ImGui::InputText(T(TKEY("storm_replacement_reference"), "Replaced reference (0xFormID~Plugin)"), &storm.replacementReference);
		const auto editRules = [&](const char* label, std::vector<std::string>& rules) {
			std::string text;
			for (const auto& rule : rules) {
				if (!text.empty())
					text += '\n';
				text += rule;
			}
			if (!ImGui::InputTextMultiline(label, &text))
				return false;
			rules.clear();
			std::istringstream lines(text);
			for (std::string line; std::getline(lines, line);)
				if (!line.empty())
					rules.push_back(line);
			return true;
		};
		changed |= editRules(T(TKEY("storm_replacement_references"), "Additional references (one per line)"), storm.replacementReferences);
		changed |= editRules(T(TKEY("storm_replacement_nodes"), "Replaced node names (one per line)"), storm.replacementNodes);
		changed |= editRules(T(TKEY("storm_replacement_textures"), "Replaced effect textures (one per line)"), storm.replacementTextures);
		changed |= ImGui::InputText(T(TKEY("storm_worldspace"), "Storm worldspace Editor ID"), &storm.worldspace);
		changed |= ImGui::DragFloat2(T(TKEY("storm_center"), "Storm centre X / Y (m)"), &storm.center.x, 10.f);
		changed |= ImGui::DragFloat(T(TKEY("storm_radius"), "Vortex radius (m)"), &storm.radius, 10.f, 1.f, 16000.f);
		changed |= ImGui::DragFloat(T(TKEY("storm_back_half_size"), "Storm back half-size (m)"), &storm.backHalfSize, 10.f, 1.f, 16000.f);
		changed |= ImGui::DragFloat(T(TKEY("storm_back_rotation"), "Storm back rotation (degrees)"), &storm.backRotation, 1.f);
		changed |= ImGui::Checkbox(T(TKEY("storm_back_mirror"), "Mirror storm back"), &storm.mirrorBack);
		changed |= ImGui::SliderFloat(T(TKEY("storm_core_height"), "Core height within NDF span"), &storm.coreHeight, 0.f, 1.f);
		changed |= ImGui::DragFloat(T(TKEY("storm_altitude"), "NDF height zero (m)"), &storm.altitude, 10.f, -24000.f, 48000.f);
		changed |= ImGui::DragFloat(T(TKEY("storm_height"), "NDF height span (m)"), &storm.height, 10.f, 1.f, 24000.f);
		changed |= ImGui::SliderFloat(T(TKEY("storm_strength"), "Storm strength"), &storm.strength, 0.f, 1.f);
		if (storm.mode == "Volcanic") {
			changed |= ImGui::DragFloat2(T(TKEY("storm_top_offset"), "Plume top offset X / Y (m)"), &storm.topOffset.x, 10.f, -16000.f, 16000.f);
			changed |= ImGui::SliderFloat(T(TKEY("storm_wind_influence"), "Wind bending strength"), &storm.windInfluence, 0.f, 2.f);
		}
		changed |= ImGui::SliderFloat(T(TKEY("storm_rotation"), "Storm rotation speed"), &storm.rotation, -4.f, 4.f);
		changed |= ImGui::SliderFloat(T(TKEY("storm_updraft"), "Storm updraft (m/s)"), &storm.updraft, -80.f, 80.f);
		changed |= ImGui::SliderFloat(T(TKEY("storm_density"), "Storm density"), &storm.density, 0.f, 4.f);
		changed |= ImGui::SliderFloat(T(TKEY("storm_albedo"), "Storm single scattering albedo"), &storm.albedo, 0.f, 0.9999f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::ColorEdit3(T(TKEY("storm_emission"), "Storm emission"), &storm.emission.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		changed |= ImGui::SliderFloat(T(TKEY("storm_emission_height"), "Emitting height fraction"), &storm.emissionHeight, 0.001f, 1.f);
		if (changed) {
			key.clear();
			++revision;
			globals::features::physicalSky.volMainHistoryValid = false;
		}
		if (ImGui::Button(T(TKEY("storm_save"), "Save instance"))) {
			try {
				const auto folder = std::filesystem::path(kStormsDirectory) / selected;
				std::filesystem::create_directories(folder);
				const auto path = folder / "Instance.json";
				std::ofstream stream(path);
				stream << nlohmann::json(storm).dump(4) << '\n';
				if (!stream)
					throw std::runtime_error("Cannot write instance");
				error.clear();
			} catch (const std::exception& exception) {
				error = exception.what();
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::TextWrapped("%s", T(TKEY("storm_asset_hint"), "Storm back Modeling alpha is the template blend weight. The generator combines this template with the mesocyclone and writes the final storm mask separately. The optional core asset supplies final NDF channels within its valid height footprint; profile reconstruction and animated noise remain shared. Asset folders are relative to the instance folder; use . for textures beside Instance.json."));
	ImGui::TextWrapped("%s", T(TKEY("storm_replacement_hint"), "Only active, successfully generated instances hide their configured references. Disable an instance and save it to restore its original geometry."));
	if (!error.empty())
		ImGui::TextWrapped("%s", error.c_str());
}

#undef I18N_KEY_PREFIX

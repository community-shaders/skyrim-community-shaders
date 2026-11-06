#pragma once

#include "Ndf.h"

// Scene endpoints hold placements, rather than interpolating the volume transform.
struct VoxelCloudPlacement
{
	std::string asset;
	std::string worldspace;
	bool enabled = true;
	float3 center = {};    // metres, Z relative to Physical Sky's worldspace height zero
	float3 rotation = {};  // degrees around X, Y, Z
	float scale = 1.f;
	float strength = 1.f;
	float density = 1.f;
	// Zero inherits the asset. The adjustment distance may itself be zero.
	float boxSize = 0.f;
	float noiseFrequency = 0.f;
	float noiseFrequencyAdjustmentDistance = -1.f;
	float noiseMipDistance = 0.f;
	int noiseMipsEnabled = -1;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(VoxelCloudPlacement, asset, worldspace, enabled, center, rotation, scale, strength, density, boxSize, noiseFrequency, noiseFrequencyAdjustmentDistance, noiseMipDistance, noiseMipsEnabled)

struct VoxelCloudAsset
{
	std::string name;
	std::string modeling = "Modeling.dds";
	std::string distance = "Distance.dds";
	float3 size = { 4096.f, 4096.f, 512.f };
	float boxSize = 0.f;  // Longest box axis; zero keeps the legacy explicit size
	float noiseFrequency = 1.2f;
	float noiseFrequencyAdjustmentDistance = 256.f;  // Reference high-quality frequency adjustment distance
	float noiseMipDistance = 1500.f;
	bool noiseMipsEnabled = true;
	VoxelCloudPlacement placement;
};
void from_json(const nlohmann::json& json, VoxelCloudAsset& asset);

struct VoxelCloudData
{
	float4 centerWeight;
	float4 axisX, axisY, axisZ;  // orthonormal local axes, inverse extent in w
	float4 parameters;           // native distance scale, density, reveal strength, asset slot
	float4 noise;                // frequency, inverse mip distance (zero disables), native adjustment distance, reserved
};
static_assert(sizeof(VoxelCloudData) == 96);

struct VoxelCloudManager
{
	static constexpr uint32_t kAssetSlots = 8;
	void LoadInstances();
	void Update(const SceneBlend& selection, const std::string& worldspace, bool enabled);
	void Bind() const;
	/** Load the shared noise independently of whether a voxel placement is active. */
	ID3D11ShaderResourceView* Noise();
	void UpdateLighting(ID3D11ComputeShader* shader, float3 direction);
	void DrawSettings(SceneBlend& selection, const std::string& worldspace, float3 cameraMeters);
	uint32_t Count() const { return static_cast<uint32_t>(active.size()); }
	float2 Altitude() const { return altitude; }
	std::array<uint32_t, 4> ScreenRect(float bottomZ, uint32_t width, uint32_t height) const;

private:
	struct Resource
	{
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> views;
		bool attempted = false;
	};
	std::map<std::string, VoxelCloudAsset> assets;
	std::map<std::string, Resource> resources;
	std::vector<VoxelCloudData> active;
	std::array<ID3D11ShaderResourceView*, kAssetSlots * 2 + 2> bindings{};
	winrt::com_ptr<ID3D11ShaderResourceView> noiseVoxel;
	bool noiseAttempted = false;
	eastl::unique_ptr<StructuredBuffer> data;
	eastl::unique_ptr<Texture2D> lighting;
	eastl::unique_ptr<ConstantBuffer> lightingConstants;
	std::vector<VoxelCloudData> lightingGeometry;
	float3 lightingDirection{};
	uint32_t lightingNextSlice = 0, lightingRemaining = 0;
	bool lightingValid = false;
	uint32_t capacity = 0;
	float2 altitude = {};
	std::string key, error;
	bool scanned = false;
	Resource& LoadResource(const std::string& id, const VoxelCloudAsset& asset);
};

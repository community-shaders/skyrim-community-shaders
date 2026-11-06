#pragma once

#include "Buffer.h"

#include <array>
#include <cstdint>

struct CloudNoiseBand
{
	uint32_t frequency = 3;
	uint32_t octaves = 4;
	float persistence = 0.5f;
	float exponent = 1.f;
	float contrast = 1.f;
	float bias = 0.f;
	float perlinMix = 0.f;
	float padding = 0.f;
};
static_assert(sizeof(CloudNoiseBand) == 32);

struct CloudNoiseSettings
{
	bool skipShapeNoise = false;
	bool useVoxelNoise = false;
	bool procedural = true;
	uint32_t seed = 1337;
	std::array<CloudNoiseBand, 4> shape = { { { 3, 5, 0.625f, 3.489f, 0.960f, 0.133f, 1.f },
		{ 3, 4, 0.5f, 1.164f, 1.628f, 0.317f },
		{ 4, 4, 0.5f, 0.996f, 1.262f, 0.115f },
		{ 9, 4, 0.6f, 1.031f, 1.463f, 0.226f } } };
	std::array<CloudNoiseBand, 2> warp = { { { 4, 4, 0.65f, 2.417f, 1.516f, 0.176f },
		{ 3, 4, 1.f, 2.368f, 2.708f, 0.817f } } };
	float warpCorrelation = -0.35f;
};

class CloudNoiseGenerator
{
public:
	void SetupResources();
	void CompileShaders();
	bool Update(const CloudNoiseSettings& settings, ID3D11ShaderResourceView* adjustment);
	void DrawSettings(CloudNoiseSettings& settings);
	ID3D11ShaderResourceView* Shape() const { return shape ? shape->srv.get() : nullptr; }
	ID3D11ShaderResourceView* Adjustment() const { return adjustment ? adjustment->srv.get() : nullptr; }

private:
	static constexpr uint32_t kShapeSize = 128;
	static constexpr uint32_t kSlicesPerFrame = 4;
	struct Parameters
	{
		std::array<CloudNoiseBand, 4> shape;
		std::array<CloudNoiseBand, 2> warp;
		uint32_t seed;
		uint32_t firstSlice = 0;
		uint32_t mip = 0;
		float warpCorrelation;
	};
	static_assert(sizeof(Parameters) == 208);
	Parameters requested = {};
	Parameters generated = {};
	bool pending = false;
	bool valid = false;
	uint32_t nextSlice = 0;
	eastl::unique_ptr<ConstantBuffer> parameters;
	eastl::unique_ptr<Texture3D> shape, pendingShape;
	eastl::unique_ptr<Texture2D> adjustment, pendingAdjustment;
	winrt::com_ptr<ID3D11ShaderResourceView> generatedSource, pendingSource;
	winrt::com_ptr<ID3D11ComputeShader> shapeProgram, adjustmentProgram, adjustmentMipProgram;
};

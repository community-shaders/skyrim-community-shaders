#include "Features/PhysicalSky.h"

#include "Deferred.h"
#include "Features/CloudShadows.h"
#include "Features/PostProcessing/RasterPass.h"
#include "Features/TerrainShadows.h"
#include "Features/ViewAtmosphere.h"
#include "State.h"
#include "Util.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
	constexpr float KilometersToGameUnits(float value) { return value / Util::Units::GAME_UNIT_TO_KM; }

	struct CloudMsQuadrature
	{
		std::array<double, 32> nodes, weights;

		CloudMsQuadrature()
		{
			for (uint32_t i = 0; i < nodes.size(); ++i) {
				double x = std::cos(std::numbers::pi * (i + 0.75) / (nodes.size() + 0.5));
				double derivative = 0.0;
				for (uint32_t iteration = 0; iteration < 16; ++iteration) {
					double previous = 1.0, current = x;
					for (uint32_t degree = 2; degree <= nodes.size(); ++degree) {
						const double next = ((2.0 * degree - 1.0) * x * current - (degree - 1.0) * previous) / degree;
						previous = current;
						current = next;
					}
					derivative = nodes.size() * (x * current - previous) / (x * x - 1.0);
					const double step = current / derivative;
					x -= step;
					if (std::abs(step) < 1e-14)
						break;
				}
				nodes[i] = 0.5 * (x + 1.0);
				weights[i] = 1.0 / ((1.0 - x * x) * derivative * derivative);
			}
		}
	};

	struct CloudMiePhase
	{
		float4 coefficients;

		explicit CloudMiePhase(float diameter)
		{
			const float d = std::isfinite(diameter) ? std::clamp(diameter, 5.f, 50.f) : 20.f;
			// Jendersie and d'Eon (2023), mean water-droplet diameter in micrometers.
			coefficients = {
				std::exp(-0.0990567f / (d - 1.67154f)),
				std::exp(-2.20679f / (d + 3.91029f) - 0.428934f),
				std::exp(3.62489f - 8.29288f / (d + 5.52825f)),
				std::exp(-0.599085f / (d - 0.641583f) - 0.665888f)
			};
		}

		double MeanCosine() const
		{
			const double g = coefficients.y;
			const double alpha = coefficients.z;
			const double meanDraine = g * (1.0 + alpha * (3.0 + 2.0 * g * g) / 5.0) /
			                          (1.0 + alpha * (1.0 + 2.0 * g * g) / 3.0);
			return std::lerp(static_cast<double>(coefficients.x), meanDraine, static_cast<double>(coefficients.w));
		}

		float4 AmbientPhase(float albedo) const
		{
			const double g = coefficients.y;
			const double g2 = g * g;
			const double alpha = coefficients.z;
			const double secondDraine = (g2 + alpha * (2.0 / 15.0 + 11.0 * g2 / 21.0 + 12.0 * g2 * g2 / 35.0)) /
			                            (1.0 + alpha * (1.0 + 2.0 * g2) / 3.0);
			const double first = MeanCosine();
			const double second = std::lerp(double(coefficients.x) * coefficients.x, secondDraine, double(coefficients.w));
			double fraction = first * first;
			double a = (first - fraction) / (1.0 - fraction);
			double b = (second - fraction) / (1.0 - fraction);
			const double vertex = b > 0.0 ? std::clamp(-a / (5.0 * b), -1.0, 1.0) : -1.0;
			if (b < 0.0 || 1.0 + 3.0 * a * vertex + 2.5 * b * (3.0 * vertex * vertex - 1.0) < 0.0) {
				fraction = std::max(0.0, (3.0 * first - 1.0) / 2.0);
				a = (first - fraction) / (1.0 - fraction);
				b = 0.0;
			}
			const double extinction = 1.0 - albedo * fraction;
			return { float(extinction), float(albedo * (1.0 - fraction) / extinction), float(std::sqrt(a)), float(std::sqrt(b)) };
		}

		double Evaluate4Pi(double cosine) const
		{
			const auto hg = [cosine](double g) {
				const double complement = 1.0 - g;
				const double denominator = complement * complement + 2.0 * g * (1.0 - cosine);
				return complement * (1.0 + g) / (denominator * std::sqrt(denominator));
			};
			const double g = coefficients.y;
			const double alpha = coefficients.z;
			const double draine = hg(g) * (1.0 + alpha * cosine * cosine) /
			                      (1.0 + alpha * (1.0 + 2.0 * g * g) / 3.0);
			return std::lerp(hg(coefficients.x), draine, static_cast<double>(coefficients.w));
		}
	};

	const std::array<float4, PhysicalSky::kCloudMsCalibrationSize>& CloudMsCoefficients(float albedo, const CloudMiePhase& phase)
	{
		static const CloudMsQuadrature quadrature;
		struct Integral
		{
			double depth, densityScale;
			std::array<double, 32> powers, weights, depths;
		};
		// Integrate a power-law boundary profile with finite-slab escape.
		static const auto integrals = [] {
			std::array<std::array<Integral, 4>, PhysicalSky::kCloudMsCalibrationSize> result{};
			for (uint32_t i = 0; i < result.size(); ++i) {
				const double referenceDepth = std::exp2(-12.0 + 0.5 * i);
				for (uint32_t j = 0; j < 4; ++j) {
					const double exponent = 0.35 + 0.1 * j;
					auto& integral = result[i][j];
					integral.depth = referenceDepth * std::tgamma(0.5) * std::tgamma(exponent + 1.0) / std::tgamma(exponent + 1.5);
					integral.densityScale = std::pow(referenceDepth / (exponent + 1.0), exponent / (exponent + 1.0));
					const double range = std::min(integral.depth, 40.0);
					for (uint32_t n = 0; n < quadrature.nodes.size(); ++n) {
						const double u = quadrature.nodes[n];
						const double depth = range * u * u;
						double escape = 0.0;
						for (uint32_t m = 0; m < quadrature.nodes.size(); ++m)
							escape += quadrature.weights[m] * std::exp(-depth / quadrature.nodes[m]);
						integral.depths[n] = depth;
						integral.powers[n] = std::pow(depth, exponent / (exponent + 1.0));
						integral.weights[n] = quadrature.weights[n] * range * u * escape;
					}
				}
			}
			return result;
		}();
		static std::array<float, 5> previous = { -1.f, 0.f, 0.f, 0.f, 0.f };
		static std::array<float4, PhysicalSky::kCloudMsCalibrationSize> coefficients{};
		const auto& fit = phase.coefficients;
		const std::array<float, 5> parameters = { albedo, fit.x, fit.y, fit.z, fit.w };
		if (parameters == previous)
			return coefficients;
		previous = parameters;
		const double omega = albedo;
		const double transport = 1.0 - omega * phase.MeanCosine();
		const double ratio = std::sqrt((1.0 - omega) / transport);
		const double reflectance = (1.0 - ratio) / (1.0 + ratio);
		const double attenuation = std::sqrt((1.0 - omega) * transport);
		std::array<double, 32> singleWeights{};
		for (uint32_t n = 0; n < quadrature.nodes.size(); ++n) {
			const double mu = quadrature.nodes[n];
			singleWeights[n] = quadrature.weights[n] * 0.5 * omega * phase.Evaluate4Pi(-mu) * mu / (1.0 + mu);
		}
		for (uint32_t i = 0; i < coefficients.size(); ++i) {
			std::array<float, 4> row{};
			for (uint32_t j = 0; j < row.size(); ++j) {
				const auto& integral = integrals[i][j];
				std::array<double, 32> transportWeights{};
				for (uint32_t n = 0; n < quadrature.nodes.size(); ++n)
					transportWeights[n] = integral.weights[n] * std::exp(-transport * integral.depths[n]);
				const double loss = -std::expm1(-2.0 * attenuation * integral.depth);
				const double total = reflectance * loss / (1.0 - reflectance * reflectance * (1.0 - loss));
				double single = 0.0;
				for (uint32_t n = 0; n < quadrature.nodes.size(); ++n) {
					const double mu = quadrature.nodes[n];
					single += singleWeights[n] * -std::expm1(-(1.0 + 1.0 / mu) * integral.depth);
				}
				if (total <= single)
					continue;
				double lower = 0.0, upper = 20.0;
				double maximum = 0.0;
				for (uint32_t n = 0; n < quadrature.nodes.size(); ++n) {
					const double transmission = std::exp(-upper * integral.powers[n]);
					maximum += omega * transportWeights[n] * omega * (1.0 - transmission) / (1.0 - omega + omega * transmission);
				}
				// Strong absorption can put the two-flux target outside the local closure's range.
				if (total - single >= maximum) {
					row[j] = static_cast<float>(upper * integral.densityScale);
					continue;
				}
				double amplitude = 1.0;
				for (uint32_t iteration = 0; iteration < 20; ++iteration) {
					double multiple = 0.0, derivative = 0.0;
					for (uint32_t n = 0; n < quadrature.nodes.size(); ++n) {
						const double transmission = std::exp(-amplitude * integral.powers[n]);
						const double denominator = 1.0 - omega + omega * transmission;
						const double gain = omega * (1.0 - transmission) / denominator;
						multiple += omega * transportWeights[n] * gain;
						derivative += omega * transportWeights[n] * omega * transmission * integral.powers[n] / (denominator * denominator);
					}
					const double error = multiple - (total - single);
					if (std::abs(error) < 1e-9)
						break;
					if (error < 0.0)
						lower = amplitude;
					else
						upper = amplitude;
					const double candidate = amplitude - error / std::max(derivative, 1e-20);
					amplitude = candidate > lower && candidate < upper ? candidate : 0.5 * (lower + upper);
				}
				row[j] = static_cast<float>(amplitude * integral.densityScale);
			}
			coefficients[i] = { row[0], row[1], row[2], row[3] };
		}
		return coefficients;
	}

	float2 CloudVelocity(float2 velocity)
	{
		const float speed = std::hypot(velocity.x, velocity.y);
		if (!std::isfinite(speed))
			return {};
		return velocity * (speed > 80.f ? 80.f / speed : 1.f);
	}

	float CloudUnitParameter(float value)
	{
		return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
	}

	bool IsVolumeTexture(ID3D11ShaderResourceView* srv)
	{
		if (!srv)
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		srv->GetDesc(&desc);
		return desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE3D && NdfManager::IsLinearFormat(desc.Format, 4);
	}

	bool LoadProfileImage(const wchar_t* path, DirectX::ScratchImage& image, bool* hasGreen = nullptr)
	{
		DirectX::ScratchImage source;
		if (FAILED(DirectX::LoadFromDDSFile(path, DirectX::DDS_FLAGS_NONE, nullptr, source)))
			return false;
		const auto* base = source.GetImage(0, 0, 0);
		if (!base || source.GetMetadata().dimension != DirectX::TEX_DIMENSION_TEXTURE2D || source.GetMetadata().arraySize != 1)
			return false;
		if (!NdfManager::IsLinearFormat(base->format, 1))
			return false;
		if (hasGreen)
			*hasGreen = NdfManager::IsLinearFormat(base->format, 2);
		if (base->format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
			image = std::move(source);
			return true;
		}
		if (DirectX::IsCompressed(base->format))
			return SUCCEEDED(DirectX::Decompress(*base, DXGI_FORMAT_R32G32B32A32_FLOAT, image));
		return SUCCEEDED(DirectX::Convert(*base, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.f, image));
	}

	float ProfileSample(const DirectX::Image& image, float u, float v, uint32_t channel = 0)
	{
		const float x = u * static_cast<float>(image.width) - 0.5f;
		const float y = v * static_cast<float>(image.height) - 0.5f;
		const int ix = static_cast<int>(std::floor(x));
		const int iy = static_cast<int>(std::floor(y));
		const auto sample = [&](int px, int py) {
			px = std::clamp(px, 0, static_cast<int>(image.width) - 1);
			py = std::clamp(py, 0, static_cast<int>(image.height) - 1);
			return reinterpret_cast<const float*>(image.pixels + py * image.rowPitch)[px * 4 + channel];
		};
		return std::lerp(std::lerp(sample(ix, iy), sample(ix + 1, iy), x - ix),
			std::lerp(sample(ix, iy + 1), sample(ix + 1, iy + 1), x - ix), y - iy);
	}

	void CreateProfileFallbacks(winrt::com_ptr<ID3D11ShaderResourceView>& profile, winrt::com_ptr<ID3D11ShaderResourceView>& adjustment)
	{
		if (profile && adjustment)
			return;
		DirectX::ScratchImage topImage, bottomImage;
		bool hasTopProfile = false;
		const bool hasTop = LoadProfileImage(L"Data\\Textures\\PhysicalSky\\top_lut.dds", topImage, &hasTopProfile);
		const bool hasBottom = LoadProfileImage(L"Data\\Textures\\PhysicalSky\\bottom_lut.dds", bottomImage);
		constexpr uint32_t size = 64;
		std::array<float4, size * size> profiles, adjustments;
		const auto smooth = [](float a, float b, float value) {
			const float t = std::clamp((value - a) / (b - a), 0.f, 1.f);
			return t * t * (3.f - 2.f * t);
		};
		for (uint32_t y = 0; y < size; ++y) {
			const float height = (y + 0.5f) / size;
			for (uint32_t x = 0; x < size; ++x) {
				const float type = (x + 0.5f) / size;
				const float top = hasTop && hasTopProfile ? ProfileSample(*topImage.GetImage(0, 0, 0), type, 1.f - height, 1) :
				                                            1.f - smooth(0.04f + type * 0.45f, 0.12f + type * 0.9f, height);
				const float bottom = hasBottom ? ProfileSample(*bottomImage.GetImage(0, 0, 0), type, 1.f - height) :
				                                 smooth(0.f, 0.08f + type * 0.22f, height);
				const float expansion = hasTop ? ProfileSample(*topImage.GetImage(0, 0, 0), type, 1.f - height) :
				                                 1.f - smooth(0.1f + type * 0.35f, 0.25f + type * 0.75f, height);
				profiles[y * size + x] = { bottom, top, 0.f, 1.f };
				adjustments[y * size + x] = { expansion, 0.5f, 0.5f, 1.f };
			}
		}
		const auto upload = [&](const auto& pixels, winrt::com_ptr<ID3D11ShaderResourceView>& srv, const char* name) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = size;
			desc.MipLevels = 7;
			desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			winrt::com_ptr<ID3D11Texture2D> texture;
			DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&desc, nullptr, texture.put()));
			globals::d3d::context->UpdateSubresource(texture.get(), 0, nullptr, pixels.data(), size * sizeof(float4), 0);
			DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(texture.get(), nullptr, srv.put()));
			globals::d3d::context->GenerateMips(srv.get());
			Util::SetResourceName(texture.get(), "%s", name);
		};
		if (!profile)
			upload(profiles, profile, "PhysicalSky::FallbackCloudProfile");
		if (!adjustment)
			upload(adjustments, adjustment, "PhysicalSky::FallbackCloudAdjustment");
		logger::info("Cloud profile fallback: top {}, bottom {}", hasTop ? "texture" : "analytic", hasBottom ? "texture" : "analytic");
	}
}

CloudWindSettings CloudWindSettings::Interpolate(const CloudWindSettings& from, const CloudWindSettings& to, float weight)
{
	const float blend = CloudUnitParameter(weight);
	const float2 lowFrom = CloudVelocity(from.lowVelocity);
	const float2 highFrom = CloudVelocity(from.highVelocity);
	return {
		.lowVelocity = lowFrom + (CloudVelocity(to.lowVelocity) - lowFrom) * blend,
		.highVelocity = highFrom + (CloudVelocity(to.highVelocity) - highFrom) * blend,
		.development = std::lerp(CloudUnitParameter(from.development), CloudUnitParameter(to.development), blend),
		.disturbance = std::lerp(CloudUnitParameter(from.disturbance), CloudUnitParameter(to.disturbance), blend)
	};
}

float2 LowCloudSettings::GetNdfAltitudeRangeKm() const
{
	const float offset = std::isfinite(ndfAltitudeOffset) ? std::clamp(ndfAltitudeOffset, 0.f, 20000.f) : 256.f;
	const float scale = std::isfinite(ndfAltitudeScale) ? std::clamp(ndfAltitudeScale, 1.f, 20000.f) : 1792.f;
	return { offset * 0.001f, (offset + scale) * 0.001f };
}

float LowCloudSettings::GetCurvatureRadiusKm() const
{
	return std::isfinite(curvatureRadius) ? std::clamp(curvatureRadius, 25.f, 10000.f) : 74.946f;
}

float LowCloudSettings::GetShadowBottomKm(float bottomKm, float rangeKm, uint32_t resolution) const
{
	const float extent = rangeKm * (0.5f + 1.f / static_cast<float>(resolution));
	return bottomKm - 2.f * extent * extent / GetCurvatureRadiusKm();
}

float CirrusSettings::GetAltitudeKm() const
{
	return (std::isfinite(altitude) ? std::clamp(altitude, 1.f, 24000.f) : 8000.f) * 0.001f;
}

float CirrusSettings::GetUvSize() const
{
	return std::isfinite(uvSize) ? std::clamp(uvSize, 0.1f, 32.f) : 1.f;
}

void PhysicalSky::LoadCloudTextures()
{
	volMainHistoryValid = false;
	cloudAmbientValid = false;
	auto loadDDS = [&](const wchar_t* path, winrt::com_ptr<ID3D11ShaderResourceView>& srv, bool required = false) {
		srv = nullptr;
		const auto key = TextureManager::Key(path);
		std::error_code error;
		if (!std::filesystem::exists(path, error) && !error && !required) {
			if (ndfTexManager.texList.erase(key))
				++ndfTexManager.revision;
			ndfTexManager.loadResults.erase(key);
			return;
		}
		ndfTexManager.EnsureLoaded(key);
		srv.copy_from(ndfTexManager.Query(key));
	};

	loadDDS(L"Data\\Textures\\PhysicalSky\\NubisCloudShapeNoise.dds", importedShapeNoiseSrv, !settings.cloudNoise.procedural);
	if (importedShapeNoiseSrv && !IsVolumeTexture(importedShapeNoiseSrv.get())) {
		logger::warn("Ignoring Nubis noise composite because it is not a linear RGBA 3D texture.");
		importedShapeNoiseSrv = nullptr;
	}

	loadDDS(L"Data\\Textures\\PhysicalSky\\NubisVerticalProfile.dds", cloudProfileLutSrv);
	loadDDS(L"Data\\Textures\\PhysicalSky\\NubisVerticalAdjustment.dds", importedAdjustmentLutSrv);
	if (cloudProfileLutSrv && !NdfManager::IsTextureNdf(cloudProfileLutSrv.get(), 2)) {
		logger::warn("Cloud profile DDS must be a linear 2D texture with at least RG channels.");
		cloudProfileLutSrv = nullptr;
	}
	if (importedAdjustmentLutSrv && !NdfManager::IsTextureNdf(importedAdjustmentLutSrv.get(), 3)) {
		logger::warn("Cloud adjustment DDS must be a linear 2D texture with at least RGB channels.");
		importedAdjustmentLutSrv = nullptr;
	}
	cloudProfileGenerated = !cloudProfileLutSrv;
	cloudAdjustmentGenerated = !importedAdjustmentLutSrv;
	CreateProfileFallbacks(cloudProfileLutSrv, importedAdjustmentLutSrv);
	for (const auto* filename : { "top_lut.dds", "bottom_lut.dds" }) {
		const auto path = std::filesystem::path("Data/Textures/PhysicalSky") / filename;
		std::error_code error;
		if (std::filesystem::exists(path, error))
			ndfTexManager.LoadTexture(path);
	}
	baseShapeNoiseSrv = importedShapeNoiseSrv;
	cloudAdjustmentLutSrv = importedAdjustmentLutSrv;
	cloudCrossSectionDirty = true;
}

void PhysicalSky::SetupVolumetricResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	logger::debug("Setting up volumetric cloud resources...");

	debugCubeFaceSrvs.clear();
	texturePreview.ClearShaderCache();
	{
		auto createBounds = [](uint32_t size, const char* name, DXGI_FORMAT format = DXGI_FORMAT_R32G32_FLOAT) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = size;
			desc.MipLevels = desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto texture = eastl::make_unique<Texture2D>(desc, name);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			texture->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			texture->CreateUAV(uav);
			return texture;
		};
		texCloudHeightBounds = createBounds(1, "PhysicalSky::CloudHeightBounds");
		texCloudHeightBoundsTiles = createBounds(16, "PhysicalSky::CloudHeightBoundsTiles", DXGI_FORMAT_R32G32B32A32_FLOAT);
		texCloudWeatherBounds = createBounds(1, "PhysicalSky::CloudWeatherBounds");
	}

	// Tileable sampler (wrap all axes)
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.MaxAnisotropy = 1;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&desc, sampTileable.put()));
	}

	// Volumetric cloud constants
	{
		volCloudCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VolumetricCloudCB>());
		cloudDispatchCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<uint32_t, 4>>(), "PhysicalSky::CloudDispatch");
	}

	// Static low-cloud cross-section shown in the Clouds page.
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = kCloudCrossSectionW;
		desc.Height = kCloudCrossSectionH;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texCloudCrossSection = eastl::make_unique<Texture2D>(desc, "PhysicalSky::CloudCrossSection");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		texCloudCrossSection->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		texCloudCrossSection->CreateUAV(uav);
		cloudCrossSectionCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CloudCrossSectionCB>());
		cloudCrossSectionReady = false;
		cloudCrossSectionDirty = true;
	}

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 6,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texVolCloudAmbientSH = eastl::make_unique<Texture2D>(texDesc, "PhysicalSky::VolumetricCloudAmbientSH");
		texVolCloudAmbientSH->CreateSRV(srvDesc);
		texVolCloudAmbientSH->CreateUAV(uavDesc);
		texDesc.Width = 8;
		texDesc.Height = 8;
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		texCloudAmbientPartial = eastl::make_unique<Texture2D>(texDesc, "PhysicalSky::CloudAmbientPartial");
		texCloudAmbientPartial->CreateSRV(srvDesc);
		texCloudAmbientPartial->CreateUAV(uavDesc);
	}

	// Get main render target dimensions for TR/Lum textures
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);

	auto createCloudTexture = [](uint32_t a_width, uint32_t a_height, DXGI_FORMAT a_format, const char* a_name) {
		D3D11_TEXTURE2D_DESC tex_desc = {
			.Width = a_width,
			.Height = a_height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = a_format,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto texture = eastl::make_unique<Texture2D>(tex_desc, a_name);
		texture->CreateSRV(srv_desc);
		texture->CreateUAV(uav_desc);
		return texture;
	};
	texCloudAmbientLighting = createCloudTexture(512, 7, DXGI_FORMAT_R32G32B32A32_FLOAT, "PhysicalSky::CloudAmbientLighting");
	texCloudAmbientBoundary = createCloudTexture(26, 15, DXGI_FORMAT_R32G32B32A32_FLOAT, "PhysicalSky::CloudAmbientBoundary");
	for (uint32_t i = 0; i < texCloudAmbientLayout.size(); ++i)
		texCloudAmbientLayout[i] = createCloudTexture(5, kCloudAmbientCascadeCount, DXGI_FORMAT_R32G32B32A32_FLOAT, fmt::format("PhysicalSky::CloudAmbientLayout{}", i).c_str());
	{
		auto createVolume = [](uint32_t width, uint32_t height, uint32_t depth, DXGI_FORMAT format, const std::string& name) {
			D3D11_TEXTURE3D_DESC desc = {
				.Width = width,
				.Height = height,
				.Depth = depth,
				.MipLevels = 1,
				.Format = format,
				.Usage = D3D11_USAGE_DEFAULT,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
			};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {
				.Format = format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
				.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
			};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {
				.Format = format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
				.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = depth }
			};
			auto texture = eastl::make_unique<Texture3D>(desc, name.c_str());
			texture->CreateSRV(srv);
			texture->CreateUAV(uav);
			return texture;
		};
		texCloudAmbientDensity = createVolume(kCloudAmbientVolumeWidth, kCloudAmbientVolumeWidth, kCloudAmbientVolumeDepth, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::CloudAmbientDensity");
		texCloudAmbientSweep = createVolume(2 * kCloudAmbientVolumeWidth, 2 * kCloudAmbientVolumeWidth, 26 * kCloudAmbientVolumeWidth, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudAmbientIntervals");
		for (uint32_t cascade = 0; cascade < kCloudAmbientCascadeCount; ++cascade) {
			const uint32_t width = cascade == 0 ? kCloudAmbientVolumeWidth : kCloudAmbientVolumeWidth / 2;
			texCloudAmbientView[cascade] = createVolume(width, width, width / 2, DXGI_FORMAT_R16G16B16A16_FLOAT, fmt::format("PhysicalSky::CloudAmbientView{}", cascade));
			for (uint32_t i = 0; i < 2; ++i)
				texCloudAmbientSource[cascade][i] = createVolume(width, width, width / 2 * 9, DXGI_FORMAT_R16G16B16A16_FLOAT, fmt::format("PhysicalSky::CloudAmbientMoments{}_{}", cascade, i));
		}
		cloudAmbientVolumeCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CloudAmbientVolumeCB>(), "PhysicalSky::CloudAmbientVolumeConstants");
		cloudAmbientValid = false;
	}

	constexpr DXGI_FORMAT historyRadianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const uint32_t lowW = std::max(1u, (mainDesc.Width + 3u) / 4u);
	const uint32_t lowH = std::max(1u, (mainDesc.Height + 3u) / 4u);
	texVolLowTr = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::CloudTraceTr");
	texVolLowLum = createCloudTexture(lowW, lowH, historyRadianceFormat, "PhysicalSky::CloudTraceLum");
	texVolLowAux = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudTraceAux");
	texLightningTrace = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningTrace");
	texLightningMask = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningMask");
	texLightningHistory = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningHistory");
	texLightningChannels = createCloudTexture(mainDesc.Width, mainDesc.Height, historyRadianceFormat, "PhysicalSky::LightningChannels");
	texVolStormMotion = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudStormMotion");
	texStormTiles = createCloudTexture((lowW + 7u) / 8u, (lowH + 7u) / 8u, DXGI_FORMAT_R32_UINT, "PhysicalSky::StormTiles");
	texUpperTiles = createCloudTexture((lowW + 7u) / 8u, (lowH + 7u) / 8u, DXGI_FORMAT_R32_UINT, "PhysicalSky::UpperCloudTiles");
	{
		D3D11_TEXTURE2D_DESC desc = texVolLowAux->desc;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texCloudBoundary = eastl::make_unique<Texture2D>(desc, "PhysicalSky::CloudBoundary");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		texCloudBoundary->CreateSRV(srv);
		D3D11_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = desc.Format;
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		texCloudBoundary->CreateRTV(rtv);
		cloudBoundaryCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CloudBoundaryCB>());

		std::vector<uint32_t> indices;
		indices.reserve(kCloudBoundaryCells * kCloudBoundaryCells * 6u);
		for (uint32_t y = 0; y < kCloudBoundaryCells; ++y) {
			for (uint32_t x = 0; x < kCloudBoundaryCells; ++x) {
				const uint32_t v = y * (kCloudBoundaryCells + 1u) + x;
				const uint32_t next = v + kCloudBoundaryCells + 1u;
				indices.insert(indices.end(), { v, v + 1u, next, v + 1u, next + 1u, next });
			}
		}
		D3D11_BUFFER_DESC indexDesc{};
		indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
		indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA indexData{};
		indexData.pSysMem = indices.data();
		cloudBoundaryIndices = nullptr;
		DX::ThrowIfFailed(device->CreateBuffer(&indexDesc, &indexData, cloudBoundaryIndices.put()));

		D3D11_BLEND_DESC blend{};
		auto& target = blend.RenderTarget[0];
		target.BlendEnable = TRUE;
		target.SrcBlend = target.DestBlend = D3D11_BLEND_ONE;
		target.BlendOp = D3D11_BLEND_OP_MIN;
		target.SrcBlendAlpha = target.DestBlendAlpha = D3D11_BLEND_ONE;
		target.BlendOpAlpha = D3D11_BLEND_OP_MIN;
		target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		cloudBoundaryBlend = nullptr;
		DX::ThrowIfFailed(device->CreateBlendState(&blend, cloudBoundaryBlend.put()));
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode = D3D11_FILL_SOLID;
		raster.CullMode = D3D11_CULL_NONE;
		// The camera basis reverses the boundary mesh winding.
		raster.FrontCounterClockwise = TRUE;
		cloudBoundaryRasterizer = nullptr;
		DX::ThrowIfFailed(device->CreateRasterizerState(&raster, cloudBoundaryRasterizer.put()));
		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
		cloudBoundaryDepth = nullptr;
		DX::ThrowIfFailed(device->CreateDepthStencilState(&depth, cloudBoundaryDepth.put()));
	}
	texUpperSunDepth = createCloudTexture(256, 256, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::UpperSunDepth");
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = kMiddleBoundsSize;
		desc.MipLevels = kMiddleBoundsLevels;
		desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texMiddleBounds = eastl::make_unique<Texture2D>(desc, "PhysicalSky::MiddleCloudBounds");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = kMiddleBoundsLevels;
		texMiddleBounds->CreateSRV(srv);
		for (uint32_t mip = 0; mip < kMiddleBoundsLevels; ++mip) {
			srv.Texture2D.MostDetailedMip = mip;
			srv.Texture2D.MipLevels = 1;
			middleBoundsSrvs[mip] = nullptr;
			DX::ThrowIfFailed(device->CreateShaderResourceView(texMiddleBounds->resource.get(), &srv, middleBoundsSrvs[mip].put()));
			Util::SetResourceName(middleBoundsSrvs[mip].get(), "PhysicalSky::MiddleCloudBounds Mip%u SRV", mip);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uav.Texture2D.MipSlice = mip;
			middleBoundsUavs[mip] = nullptr;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texMiddleBounds->resource.get(), &uav, middleBoundsUavs[mip].put()));
			Util::SetResourceName(middleBoundsUavs[mip].get(), "PhysicalSky::MiddleCloudBounds Mip%u UAV", mip);
		}
		middleBoundsDirty = true;
	}
	texVoxelTraceTr = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::VoxelTraceTr");
	texVoxelTraceLum = createCloudTexture(lowW, lowH, historyRadianceFormat, "PhysicalSky::VoxelTraceLum");
	texVoxelTraceAux = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VoxelTraceAux");
	texVoxelTraceMotion = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VoxelTraceMotion");
	texVoxelTraceLightning = createCloudTexture(lowW, lowH, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::VoxelTraceLightning");
	texVolFilteredTr = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::CloudFilteredTr");
	texVolFilteredLum = createCloudTexture(mainDesc.Width, mainDesc.Height, historyRadianceFormat, "PhysicalSky::CloudFilteredLum");
	texVolFilteredAux = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudFilteredAux");
	texVolTr = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::CloudTr");
	texVolLum = createCloudTexture(mainDesc.Width, mainDesc.Height, historyRadianceFormat, "PhysicalSky::CloudLum");
	texVolAux = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudAux");
	texVolHistoryTr = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::CloudHistoryTr");
	texVolHistoryLum = createCloudTexture(mainDesc.Width, mainDesc.Height, historyRadianceFormat, "PhysicalSky::CloudHistoryLum");
	texVolHistoryAux = createCloudTexture(mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::CloudHistoryAux");

	auto createCube = [](uint32_t size, DXGI_FORMAT format, const char* name) {
		D3D11_TEXTURE2D_DESC desc = {
			.Width = size,
			.Height = size,
			.MipLevels = 1,
			.ArraySize = 6,
			.Format = format,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv = {
			.Format = desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {
			.Format = desc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 6 }
		};
		auto texture = eastl::make_unique<Texture2D>(desc, name);
		texture->CreateSRV(srv);
		texture->CreateUAV(uav);
		return texture;
	};
	texVolCubeTr = createCube(kVolCubeSize, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::VolCubeTr");
	texVolCubeLum = createCube(kVolCubeSize, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeLum");
	texVolCubeAux = createCube(kVolCubeSize, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeAux");
	texVolCubeHistoryTr = createCube(kVolCubeSize, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::VolCubeHistoryTr");
	texVolCubeHistoryLum = createCube(kVolCubeSize, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeHistoryLum");
	texVolCubeHistoryAux = createCube(kVolCubeSize, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeHistoryAux");
	texVolCubeTraceTr = createCube(kVolCubeSize / 4u, DXGI_FORMAT_R16_FLOAT, "PhysicalSky::VolCubeTraceTr");
	texVolCubeTraceLum = createCube(kVolCubeSize / 4u, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeTraceLum");
	texVolCubeTraceAux = createCube(kVolCubeSize / 4u, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeTraceAux");
	texLightningCubeTrace = createCube(kVolCubeSize / 4u, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningCubeTrace");
	texLightningCubeMask = createCube(kVolCubeSize, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningCubeMask");
	texLightningCubeHistory = createCube(kVolCubeSize, DXGI_FORMAT_R16G16_FLOAT, "PhysicalSky::LightningCubeHistory");
	texLightningCubeLum = createCube(kVolCubeSize, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::LightningCubeRadiance");
	texVolCubeStormMotion = createCube(kVolCubeSize / 4u, DXGI_FORMAT_R16G16B16A16_FLOAT, "PhysicalSky::VolCubeStormMotion");
	const float clearTr[4] = { 1.f, 1.f, 1.f, 1.f };
	const float clearLum[4] = {};
	for (auto* texture : { texLightningTrace.get(), texLightningMask.get(), texLightningHistory.get(), texLightningChannels.get(), texLightningCubeTrace.get(), texLightningCubeMask.get(), texLightningCubeHistory.get(), texLightningCubeLum.get() })
		context->ClearUnorderedAccessViewFloat(texture->uav.get(), clearLum);
	lightning.SetupResources();
	context->ClearUnorderedAccessViewFloat(texVolCubeTr->uav.get(), clearTr);
	context->ClearUnorderedAccessViewFloat(texVolCubeLum->uav.get(), clearLum);
	context->ClearUnorderedAccessViewFloat(texVolCubeAux->uav.get(), clearLum);
	volMainHistoryValid = false;

	// Shadow volume 3D texture
	{
		D3D11_TEXTURE3D_DESC tex3d_desc = {
			.Width = kShadowVolW,
			.Height = kShadowVolH,
			.Depth = kShadowVolD,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = tex3d_desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = tex3d_desc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = kShadowVolD }
		};

		texShadowVolume = eastl::make_unique<Texture3D>(tex3d_desc, "PhysicalSky::VolumetricCloudShadowVolume");
		texShadowVolume->CreateSRV(srv_desc);
		texShadowVolume->CreateUAV(uav_desc);
		texShadowVolumeRaw = eastl::make_unique<Texture3D>(tex3d_desc, "PhysicalSky::CloudShadowIntegration");
		texShadowVolumeRaw->CreateSRV(srv_desc);
		texShadowVolumeRaw->CreateUAV(uav_desc);

		const FLOAT shadowVolumeClr[4] = { 1.f, 0.f, 0.f, 0.f };
		context->ClearUnorderedAccessViewFloat(texShadowVolume->uav.get(), shadowVolumeClr);
		context->ClearUnorderedAccessViewFloat(texShadowVolumeRaw->uav.get(), shadowVolumeClr);
		// Metadata row plus endpoint-inclusive vertical mean transmittance profile.
		texCloudShadowMean = createCloudTexture(1, kShadowMeanHeights + 1u, DXGI_FORMAT_R32G32B32A32_FLOAT, "PhysicalSky::CloudShadowMean");
		context->ClearUnorderedAccessViewFloat(texCloudShadowMean->uav.get(), clearLum);
	}

	// Load textures and NDF
	LoadCloudTextures();
	ndfManager.SetupResources();
	localNdfManager.SetupResources();
	cirrusMapManager.SetupResources();
	midCloudMapManager.SetupResources();
	cloudStormManager.SetupResources();
	cloudNoiseGenerator.SetupResources();

	CompileVolumetricShaders();
}

void PhysicalSky::CompileVolumetricShaders()
{
	cloudAmbientValid = false;
	volMainHistoryValid = false;
	logger::debug("Compiling volumetric cloud shaders...");
	cloudStormManager.CompileShaders();

	struct ShaderInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		const char* filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	std::array shaderInfos = {
		ShaderInfo{ &csCloudHeightBounds, "Volumetrics.cs.hlsl", {}, "buildCloudHeightBounds" },
		ShaderInfo{ &csCloudHeightBoundsReduce, "Volumetrics.cs.hlsl", {}, "reduceCloudHeightBounds" },
		ShaderInfo{ &csVolAmbientSH, "Volumetrics.cs.hlsl", {}, "buildCloudAmbientSH" },
		ShaderInfo{ &csCloudAmbientReduce, "Volumetrics.cs.hlsl", {}, "reduceCloudAmbientSH" },
		ShaderInfo{ &csCloudAmbientLighting, "Volumetrics.cs.hlsl", {}, "buildCloudAmbientLighting" },
		ShaderInfo{ &csCloudAmbientDensity, "Volumetrics.cs.hlsl", {}, "buildCloudAmbientDensity" },
		ShaderInfo{ &csCloudAmbientBoundary, "Volumetrics.cs.hlsl", {}, "buildCloudAmbientBoundary" },
		ShaderInfo{ &csCloudAmbientSweep, "Volumetrics.cs.hlsl", {}, "sweepCloudAmbient" },
		ShaderInfo{ &csCloudAmbientSweepFar, "Volumetrics.cs.hlsl", { { "CLOUD_AMBIENT_SWEEP_THREADS", "16" } }, "sweepCloudAmbient" },
		ShaderInfo{ &csCloudAmbientProject, "Volumetrics.cs.hlsl", {}, "projectCloudAmbient" },
		ShaderInfo{ &csCloudAmbientLayout, "Volumetrics.cs.hlsl", {}, "updateCloudAmbientLayout" },
		ShaderInfo{ &csCloudAmbientView, "Volumetrics.cs.hlsl", {}, "resolveCloudAmbientView" },
		ShaderInfo{ &csVolMainView, "Volumetrics.cs.hlsl", { { "CLOUD_AP_SHADOW", "" }, { "CLOUD_MAIN_DISPATCH", "" } } },
		ShaderInfo{ &csUpperSunDepth, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "buildUpperSunDepth" },
		ShaderInfo{ &csUpperView, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "renderUpperView" },
		ShaderInfo{ &csUpperTiles, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "classifyUpperTiles" },
		ShaderInfo{ &csUpperViewTiles, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "renderUpperTiles" },
		ShaderInfo{ &csMiddleBounds, "MidCloudBounds.cs.hlsl", {} },
		ShaderInfo{ &csMiddleBoundsReduce, "MidCloudBounds.cs.hlsl", {}, "reduceBounds" },
		ShaderInfo{ &csStormTiles, "Volumetrics.cs.hlsl", {}, "classifyStormTiles" },
		ShaderInfo{ &csVolMainWithoutStorm, "Volumetrics.cs.hlsl", { { "CLOUD_AP_SHADOW", "" }, { "CLOUD_MAIN_DISPATCH", "" }, { "NO_CLOUD_STORMS", "" } } },
		ShaderInfo{ &csVolMainWeather, "Volumetrics.cs.hlsl", { { "CLOUD_AP_SHADOW", "" }, { "CLOUD_MAIN_DISPATCH", "" }, { "NO_CLOUD_STORMS", "" }, { "NO_LOCAL_NDF", "" } } },
		ShaderInfo{ &csVolMainVolcanic, "Volumetrics.cs.hlsl", { { "CLOUD_AP_SHADOW", "" }, { "CLOUD_MAIN_DISPATCH", "" }, { "VOLCANIC_STORMS_ONLY", "" } } },
		ShaderInfo{ &csVolMainSingleVolcanic, "Volumetrics.cs.hlsl", { { "CLOUD_AP_SHADOW", "" }, { "CLOUD_MAIN_DISPATCH", "" }, { "VOLCANIC_STORMS_ONLY", "" }, { "SINGLE_CLOUD_STORM", "" } } },
		ShaderInfo{ &csVolCubemapVolcanic, "Volumetrics.cs.hlsl", { { "CLOUD_REFLECTION", "" }, { "VOLCANIC_STORMS_ONLY", "" } }, "renderCubemap" },
		ShaderInfo{ &csVolShadowVolcanic, "Volumetrics.cs.hlsl", { { "VOLCANIC_STORMS_ONLY", "" } }, "renderShadowVolume" },
		ShaderInfo{ &csCloudAmbientDensityVolcanic, "Volumetrics.cs.hlsl", { { "VOLCANIC_STORMS_ONLY", "" } }, "buildCloudAmbientDensity" },
		ShaderInfo{ &csVoxelView, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" }, { "CLOUD_VOXEL_VIEW", "" }, { "NO_CLOUD_STORMS", "" } }, "renderVoxelView" },
		ShaderInfo{ &csVoxelViewSingle, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" }, { "CLOUD_VOXEL_SINGLE", "" }, { "CLOUD_VOXEL_VIEW", "" }, { "NO_CLOUD_STORMS", "" } }, "renderVoxelView" },
		ShaderInfo{ &csVoxelLighting, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "buildVoxelLighting" },
		ShaderInfo{ &csVolCubemapVoxel, "Volumetrics.cs.hlsl", { { "CLOUD_REFLECTION", "" }, { "CLOUD_VOXELS", "" }, { "CLOUD_VOXEL_COARSE", "" } }, "renderCubemap" },
		ShaderInfo{ &csVolShadowVoxel, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "renderShadowVolume" },
		ShaderInfo{ &csCloudAmbientDensityVoxel, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "buildCloudAmbientDensity" },
		ShaderInfo{ &csVolFilter, "Volumetrics.cs.hlsl", {}, "filterCloud" },
		ShaderInfo{ &csVolFilterWide, "Volumetrics.cs.hlsl", { { "CLOUD_BLUR_RADIUS", "2" } }, "filterCloud" },
		ShaderInfo{ &csVolReproject, "Volumetrics.cs.hlsl", {}, "reproject" },
		ShaderInfo{ &csVolCubeReproject, "Volumetrics.cs.hlsl", {}, "reprojectCubemap" },
		ShaderInfo{ &csVolShadowVolume, "Volumetrics.cs.hlsl", {}, "renderShadowVolume" },
		ShaderInfo{ &csVolShadowWeather, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "renderShadowVolume" },
		ShaderInfo{ &csVolShadowColumns, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "renderShadowColumns" },
		ShaderInfo{ &csVolShadowResample, "Volumetrics.cs.hlsl", {}, "resampleShadowVolume" },
		ShaderInfo{ &csCloudShadowMean, "Volumetrics.cs.hlsl", {}, "buildCloudShadowMean" },
		ShaderInfo{ &csCloudShadowMeanWeather, "Volumetrics.cs.hlsl", { { "NO_CLOUD_STORMS", "" } }, "buildCloudShadowMean" },
		ShaderInfo{ &csCloudShadowMeanVolcanic, "Volumetrics.cs.hlsl", { { "VOLCANIC_STORMS_ONLY", "" } }, "buildCloudShadowMean" },
		ShaderInfo{ &csCloudShadowMeanVoxel, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "buildCloudShadowMean" },
		ShaderInfo{ &csVolCubemap, "Volumetrics.cs.hlsl", { { "CLOUD_REFLECTION", "" } }, "renderCubemap" },
		ShaderInfo{ &csCloudCrossSection, "CloudCrossSection.cs.hlsl", {} },
		ShaderInfo{ &csLightningQuery, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "findLightningSource" },
		ShaderInfo{ &csLightningComposite, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "compositeLightning" },
		ShaderInfo{ &csLightningCubeComposite, "Volumetrics.cs.hlsl", { { "CLOUD_VOXELS", "" } }, "compositeLightningCube" }
	};

	for (auto& info : shaderInfos) {
		*info.csPtr = nullptr;
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalSky") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.data())))
			info.csPtr->attach(rawPtr);
		else
			logger::warn("PhysicalSky shader unavailable: {}::{}", info.filename, info.entry);
	}
	ndfManager.CompileShaders();
	localNdfManager.CompileShaders();
	cirrusMapManager.CompileShaders();
	midCloudMapManager.CompileShaders();
	middleBoundsDirty = true;
	cloudNoiseGenerator.CompileShaders();
	vsCloudBoundary = nullptr;
	psCloudBoundary = nullptr;
	const auto boundaryPath = std::filesystem::path("Data\\Shaders\\PhysicalSky\\CloudBoundary.hlsl");
	if (auto* shader = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(boundaryPath.c_str(), {}, "vs_5_0", "vertexMain")))
		vsCloudBoundary.attach(shader);
	if (auto* shader = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(boundaryPath.c_str(), {}, "ps_5_0", "pixelMain")))
		psCloudBoundary.attach(shader);
}

void PhysicalSky::RenderCloudCrossSection()
{
	if (!texCloudCrossSection || !csCloudCrossSection || !cloudCrossSectionCB ||
		!ndfManager.texHeight || !ndfManager.texModeling || !cloudProfileLutSrv || !cloudAdjustmentLutSrv ||
		!sampNoise || !sampTr)
		return;

	const auto& low = settings.cloudLayer.low;
	const float2 altitudeKm = low.GetNdfAltitudeRangeKm();
	const float spanX = std::max(low.ndfScale.x, 1.f) * 1000.f;
	const float spanY = std::max(low.ndfScale.y, 1.f) * 1000.f;
	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float2 cameraMeters{ camera.x * Util::Units::GAME_UNIT_TO_M, camera.y * Util::Units::GAME_UNIT_TO_M };

	CloudCrossSectionCB data{};
	data.field = float4(1.f / spanX, 1.f / spanY, altitudeKm.x * 1000.f, altitudeKm.y * 1000.f);
	data.cameraSpan = float4(cameraMeters.x, cameraMeters.y, spanX, spanY);
	data.shape = float4(low.bottomSpreadScale, low.bottomSpreadHeight, low.bottomSoftness, low.bottomSoftnessHeight);
	data.shape2 = float4(low.topExpansionScale, low.densityScale, 0.f, 0.f);
	cloudCrossSectionCB->Update(data);

	auto* context = globals::d3d::context;
	auto* cb = cloudCrossSectionCB->CB();
	ID3D11ShaderResourceView* srvs[] = {
		ndfManager.texHeight->srv.get(),
		ndfManager.texModeling->srv.get(),
		cloudProfileLutSrv.get(),
		cloudAdjustmentLutSrv.get()
	};
	ID3D11SamplerState* samplers[] = { sampNoise.get(), sampTr.get() };
	auto* uav = texCloudCrossSection->uav.get();
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, 2, samplers);
	context->CSSetShaderResources(0, 4, srvs);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(csCloudCrossSection.get(), nullptr, 0);
	globals::profiler->BeginPass("PhysicalSky::CloudCrossSection");
	context->Dispatch((kCloudCrossSectionW + 7u) >> 3, (kCloudCrossSectionH + 7u) >> 3, 1);
	globals::profiler->EndPass();

	ID3D11ShaderResourceView* nullSrvs[4] = {};
	ID3D11UnorderedAccessView* nullUav = nullptr;
	ID3D11Buffer* nullCb = nullptr;
	ID3D11SamplerState* nullSamplers[2] = {};
	context->CSSetShaderResources(0, 4, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
	context->CSSetConstantBuffers(0, 1, &nullCb);
	context->CSSetSamplers(0, 2, nullSamplers);
	context->CSSetShader(nullptr, nullptr, 0);

	cloudCrossSectionCamera = cameraMeters;
	cloudCrossSectionDirty = false;
	cloudCrossSectionReady = true;
}

void PhysicalSky::RenderCloudBoundary(const CloudBoundaryCB& a_data, NdfTextureSet a_textures)
{
	auto* context = globals::d3d::context;
	cloudBoundaryCB->Update(a_data);
	ID3D11ShaderResourceView* nullSrv = nullptr;
	context->CSSetShaderResources(15, 1, &nullSrv);
	const float clear[4] = { std::numeric_limits<float>::max(), 0.f, std::numeric_limits<float>::max(), 0.f };
	context->ClearRenderTargetView(texCloudBoundary->rtv.get(), clear);

	PostProcessingRaster::RasterPass pass(context);
	winrt::com_ptr<ID3D11Buffer> savedIndex, savedCB, savedFrame;
	winrt::com_ptr<ID3D11SamplerState> savedSampler;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> savedResources;
	DXGI_FORMAT savedIndexFormat;
	UINT savedIndexOffset;
	context->IAGetIndexBuffer(savedIndex.put(), &savedIndexFormat, &savedIndexOffset);
	context->VSGetConstantBuffers(0, 1, savedCB.put());
	context->VSGetConstantBuffers(12, 1, savedFrame.put());
	context->VSGetSamplers(0, 1, savedSampler.put());
	for (UINT i = 0; i < savedResources.size(); ++i)
		context->VSGetShaderResources(i, 1, savedResources[i].put());

	pass.SetTargets({ texCloudBoundary->rtv.get() }, a_data.frameDimensions.z, a_data.frameDimensions.w);
	pass.SetShaders(vsCloudBoundary.get(), psCloudBoundary.get());
	context->RSSetState(cloudBoundaryRasterizer.get());
	context->OMSetBlendState(cloudBoundaryBlend.get(), nullptr, 0xFFFFFFFFu);
	context->OMSetDepthStencilState(cloudBoundaryDepth.get(), 0);
	context->IASetIndexBuffer(cloudBoundaryIndices.get(), DXGI_FORMAT_R32_UINT, 0);
	ID3D11Buffer* cb = cloudBoundaryCB->CB();
	ID3D11Buffer* perFrame = *globals::game::perFrame;
	ID3D11SamplerState* sampler = sampTileable.get();
	ID3D11ShaderResourceView* resources[] = { a_textures.height, a_textures.modeling };
	context->VSSetConstantBuffers(0, 1, &cb);
	context->VSSetConstantBuffers(12, 1, &perFrame);
	context->VSSetSamplers(0, 1, &sampler);
	context->VSSetShaderResources(0, 2, resources);
	globals::profiler->BeginPass("PhysicalSky::CloudBoundary");
	context->DrawIndexedInstanced(kCloudBoundaryCells * kCloudBoundaryCells * 6u, 2, 0, 0, 0);
	globals::profiler->EndPass();

	context->IASetIndexBuffer(savedIndex.get(), savedIndexFormat, savedIndexOffset);
	cb = savedCB.get();
	perFrame = savedFrame.get();
	sampler = savedSampler.get();
	context->VSSetConstantBuffers(0, 1, &cb);
	context->VSSetConstantBuffers(12, 1, &perFrame);
	context->VSSetSamplers(0, 1, &sampler);
	for (UINT i = 0; i < savedResources.size(); ++i) {
		auto* srv = savedResources[i].get();
		context->VSSetShaderResources(i, 1, &srv);
	}
}

void CloudStormManager::UpdateLighting(float dropletDiameter)
{
	if (lightingRevision == revision && lightingDiameter == dropletDiameter)
		return;
	const CloudMiePhase phase(dropletDiameter);
	for (auto& instance : active) {
		instance.ambientPhase = phase.AmbientPhase(instance.driftDensity.w);
		instance.msCalibration = CloudMsCoefficients(instance.driftDensity.w, phase);
	}
	lightingRevision = revision;
	lightingDiameter = dropletDiameter;
}

void PhysicalSky::UpdateCloudWind()
{
	auto* ui = globals::game::ui;
	if (!ui || ui->GameIsPaused())
		return;
	const double elapsed = RE::GetSecondsSinceLastFrame();
	if (!std::isfinite(elapsed) || elapsed <= 0.0)
		return;

	const auto& layer = settings.cloudLayer;
	const float2 lowVelocity = CloudVelocity(layer.wind.lowVelocity);
	const float2 highVelocity = CloudVelocity(layer.wind.highVelocity);
	volWind.lowOffset[0] += lowVelocity.x * elapsed;
	volWind.lowOffset[1] += lowVelocity.y * elapsed;
	volWind.highOffset[0] += highVelocity.x * elapsed;
	volWind.highOffset[1] += highVelocity.y * elapsed;
	volWind.phase += CloudUnitParameter(layer.wind.development) * 0.03 * elapsed;
	volWind.stormTime += elapsed;
	const float2 altitude = layer.low.GetNdfAltitudeRangeKm();
	const float span = altitude.y - altitude.x;
	const float heightFraction = span / std::max(layer.cirrus.GetAltitudeKm() - (altitude.x + altitude.y) * 0.5f, span);
	float2 targetShear = (highVelocity - lowVelocity) * (3.f * heightFraction);
	const float shearLength = std::hypot(targetShear.x, targetShear.y);
	if (shearLength > 150.f)
		targetShear *= 150.f / shearLength;
	volWind.shear += (targetShear - volWind.shear) * static_cast<float>(-std::expm1(-elapsed / 30.0));
	volWind.disturbance = std::lerp(volWind.disturbance, CloudUnitParameter(layer.wind.disturbance), static_cast<float>(-std::expm1(-elapsed / 2.0)));
}

void PhysicalSky::RenderVolumetricClouds(VolumetricCloudPass a_pass)
{
	if (a_pass == VolumetricCloudPass::kMainViewAndCubemap) {
		const bool unified = globals::features::viewAtmosphere.IsReady();
		if (unified != volHistoryUnifiedMedium) {
			volMainHistoryValid = false;
			volHistoryUnifiedMedium = unified;
		}
	}
	if (!ShadersOK())
		return;
	if (!baseShapeNoiseSrv || !cloudProfileLutSrv || !cloudAdjustmentLutSrv)
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	float3 cloudLightDir = cbData.sunDir;
	if (auto* shaderManager = globals::game::smState) {
		if (auto* shadowSceneNode = shaderManager->shadowSceneNode[0]) {
			auto shadowSunLight = shadowSceneNode->GetRuntimeData().sunLight;
			if (shadowSunLight && shadowSunLight->light) {
				if (auto* dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSunLight->light.get())) {
					const auto& direction = dirLight->GetWorldDirection();
					cloudLightDir = { -direction.x, -direction.y, -direction.z };
					const float lengthSq = cloudLightDir.x * cloudLightDir.x + cloudLightDir.y * cloudLightDir.y + cloudLightDir.z * cloudLightDir.z;
					if (lengthSq > 1e-6f) {
						const float invLength = 1.0f / std::sqrt(lengthSq);
						cloudLightDir.x *= invLength;
						cloudLightDir.y *= invLength;
						cloudLightDir.z *= invLength;
					} else {
						cloudLightDir = cbData.sunDir;
					}
				}
			}
		}
	}
	// Ground shadow directions may be elevation-clamped or switched to a moon
	// while elevated clouds are still sunlit. Trace their actual solar column.
	if (cbData.volCloudUseSun != 0)
		cloudLightDir = cbData.sunDir;
	const auto& frameBuffer = globals::game::frameBufferCached;
	const auto& resolutionScale = frameBuffer.GetDynamicResolutionParams1();
	const float2 textureDim = { static_cast<float>(texVolTr->desc.Width), static_cast<float>(texVolTr->desc.Height) };
	const float2 frameDim = {
		std::clamp(textureDim.x * resolutionScale.x, 1.0f, textureDim.x),
		std::clamp(textureDim.y * resolutionScale.y, 1.0f, textureDim.y)
	};
	const uint32_t renderW = static_cast<uint32_t>(frameDim.x);
	const uint32_t renderH = static_cast<uint32_t>(frameDim.y);

	auto& low = settings.cloudLayer.low;
	auto& cirrus = settings.cloudLayer.cirrus;
	const auto& middle = settings.cloudLayer.middle;
	auto* middleWeather = middle.enabled ? midCloudMapManager.Weather() : nullptr;
	const auto middleAltitude = middle.GetAltitudeRange();
	const auto bandValue = [](float value, float lo, float hi, float fallback) {
		return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
	};
	const float bandAngle = (bandValue(middle.bands.direction, -360.f, 360.f, 0.f) + 90.f) * (std::numbers::pi_v<float> / 180.f);
	const float bandWavelength = bandValue(middle.bands.wavelength, 500.f, 16000.f, 2000.f) / Util::Units::GAME_UNIT_TO_M;
	const float bandCoverage = bandValue(middle.bands.coverage, 0.f, 1.f, 0.f);
	const float bandThickness = bandValue(middle.bands.thickness, 0.f, 1.f, 0.f);
	const float bandLift = bandValue(middle.bands.displacement, 0.f, .5f, 0.f) * bandValue(middle.thickness, 100.f, 4000.f, 800.f) / Util::Units::GAME_UNIT_TO_M;
	auto& lighting = settings.cloudLayer.lighting;

	low.ndfScale.x = std::clamp(low.ndfScale.x, 1.0f, 50.0f);
	low.ndfScale.y = std::clamp(low.ndfScale.y, 1.0f, 50.0f);

	const auto& previousWind = volMainHistoryValid ? volHistoryWind : volWind;
	auto offset = [](const std::array<double, 2>& value) -> float2 {
		return { static_cast<float>(value[0] / Util::Units::GAME_UNIT_TO_M), static_cast<float>(value[1] / Util::Units::GAME_UNIT_TO_M) };
	};
	auto delta = [](const std::array<double, 2>& current, const std::array<double, 2>& previous) -> float2 {
		return { static_cast<float>((current[0] - previous[0]) / Util::Units::GAME_UNIT_TO_M), static_cast<float>((current[1] - previous[1]) / Util::Units::GAME_UNIT_TO_M) };
	};
	const float2 noiseWindOffset = offset(volWind.lowOffset);
	const float2 windDelta = delta(volWind.lowOffset, previousWind.lowOffset);
	constexpr double phasePeriod = 6.283185307179586;
	const float2 lowAltitudeRange = low.GetNdfAltitudeRangeKm();
	const float lowCloudBaseKm = lowAltitudeRange.x;
	const float lowCloudTopKm = lowAltitudeRange.y;
	const float cloudCurvatureRadius = KilometersToGameUnits(low.GetCurvatureRadiusKm());
	const auto localRect = localNdfManager.GetRect();
	const auto localAltitude = localNdfManager.GetAltitude();
	const float lowCloudTraceBottomKm = localAltitude.y > 0.f ? std::min(lowCloudBaseKm, localAltitude.x * 0.001f) : lowCloudBaseKm;
	const float lowCloudTraceTopKm = localAltitude.y > 0.f ? std::max(lowCloudTopKm, (localAltitude.x + localAltitude.y) * 0.001f) : lowCloudTopKm;
	const auto stormAltitude = cloudStormManager.Altitude();
	const bool stormActive = cloudStormManager.Count() > 0;
	float volumeBottomKm = stormActive ? std::min(lowCloudTraceBottomKm, stormAltitude.x * 0.001f) : lowCloudTraceBottomKm;
	float volumeTopKm = stormActive ? std::max(lowCloudTraceTopKm, stormAltitude.y * 0.001f) : lowCloudTraceTopKm;
	if (middle.enabled) {
		volumeBottomKm = std::min(volumeBottomKm, middleAltitude.x * .001f);
		volumeTopKm = std::max(volumeTopKm, middleAltitude.y * .001f);
	}
	const bool voxelActive = voxelCloudManager.Count() > 0;
	if (voxelActive) {
		const auto altitude = voxelCloudManager.Altitude();
		volumeBottomKm = std::min(volumeBottomKm, altitude.x * .001f);
		volumeTopKm = std::max(volumeTopKm, altitude.y * .001f);
	}

	const uint32_t lowW = (renderW + 3u) / 4u;
	const uint32_t lowH = (renderH + 3u) / 4u;

	const auto cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float2 boundaryExtent = { KilometersToGameUnits(low.ndfScale.x * 2.f), KilometersToGameUnits(low.ndfScale.y * 2.f) };
	const float2 boundarySpacing = boundaryExtent / static_cast<float>(kCloudBoundaryCells);
	const float2 boundaryOrigin = {
		std::floor(cameraPosition.x / boundarySpacing.x) * boundarySpacing.x - boundaryExtent.x * 0.5f,
		std::floor(cameraPosition.y / boundarySpacing.y) * boundarySpacing.y - boundaryExtent.y * 0.5f
	};
	const auto cirrusTextures = cirrusMapManager.GetTextures();
	const bool renderCirrus = cirrus.enabled && bool(cirrusTextures);
	const CloudMiePhase lowCloudPhase(lighting.dropletDiameter);
	auto* ndfVoxelNoise = settings.cloudNoise.useVoxelNoise ? voxelCloudManager.Noise() : nullptr;

	VolumetricCloudCB cloudData = {
		.rayMarchRange = KilometersToGameUnits(std::clamp(settings.rayMarchRange, 1.f, 64.f)),
		.shadowVolumeRange = KilometersToGameUnits(settings.shadowVolumeRange),
		.marchStepScale = std::clamp(settings.marchStepScale, 0.0625f, 4.f),
		.cloudFrameIndex = volFrameIndex,

		.rcpFrameDim = float2(1.0f) / textureDim,
		.activeFrameDim = { static_cast<float>(renderW), static_cast<float>(renderH) },
		.lowFrameDim = { static_cast<float>(lowW), static_cast<float>(lowH) },
		.previousFrameDim = volHistoryFrameDim,

		.dirlightDir = cloudLightDir,
		.bottomZ = cbData.zBottom,
		.previousCamera = volHistoryCamera,
		.planetRadius = cbData.rPlanet,

		.lowestCloudAltitude = KilometersToGameUnits(volumeBottomKm),
		.highestCloudAltitude = KilometersToGameUnits(volumeTopKm),
		.lowCloudBaseAltitude = KilometersToGameUnits(lowCloudBaseKm),
		.lowCloudTopAltitude = KilometersToGameUnits(lowCloudTopKm),

		.lowCloudTraceBottomAltitude = KilometersToGameUnits(lowCloudTraceBottomKm),
		.lowCloudTraceTopAltitude = KilometersToGameUnits(lowCloudTraceTopKm),
		.cloudCurvatureRadius = cloudCurvatureRadius,
		.cloudEvolutionDelta = static_cast<float>((volWind.phase - previousWind.phase) / (phasePeriod * 0.0034834063 * Util::Units::GAME_UNIT_TO_M)),

		.shadowVolumeBottom = KilometersToGameUnits(low.GetShadowBottomKm(volumeBottomKm, settings.shadowVolumeRange, kShadowVolW)),
		.shadowVolumeTop = KilometersToGameUnits(volumeTopKm),
		.historyValid = volMainHistoryValid ? 1u : 0u,
		.cirrusEnabled = renderCirrus ? 1u : 0u,

		.lowNdfFrequency = { Util::Units::GAME_UNIT_TO_KM / low.ndfScale.x, Util::Units::GAME_UNIT_TO_KM / low.ndfScale.y },
		.noiseWindOffset = noiseWindOffset,
		.cloudWindDelta = windDelta,
		.cloudShapeShear = volWind.shear / Util::Units::GAME_UNIT_TO_M,
		.previousShapeShear = previousWind.shear / Util::Units::GAME_UNIT_TO_M,
		.cirrusWindOffset = offset(volWind.highOffset),

		.cirrusWindDelta = delta(volWind.highOffset, previousWind.highOffset),
		.cirrusAltitude = KilometersToGameUnits(cirrus.GetAltitudeKm()),
		.cirrusUvInvSize = 1.f / cirrus.GetUvSize(),

		.cirrusDensityScale = std::max(cirrus.densityScale, 0.f),
		.cirrusLightThickness = std::max(lighting.cirrusLightThickness, 1.f),
		.phaseForwardG = std::clamp(lighting.phaseForwardG, 0.f, 0.95f),
		.phaseBackwardG = std::clamp(lighting.phaseBackwardG, -0.95f, 0.f),

		.lowDensityScale = std::max(low.densityScale, 0.f),
		.coverageBottomPower = 1.f - 0.75f * std::clamp(low.bottomSpreadScale, 0.f, 1.f),
		.coverageHeightRange = std::clamp(low.bottomSpreadHeight, 0.001f, 1.f),
		.bottomDensityPower = 2.f + 4.f * std::clamp(low.bottomSoftness, 0.f, 1.f),

		.bottomDensityWidth = 10.f * std::clamp(low.bottomSoftnessHeight, 0.f, 1.f),
		.topExpansion = std::clamp(low.topExpansionScale, 0.f, 1.f),
		.scatterDensityThreshold = std::max(lighting.scatterDensityThreshold, 0.001f),
		.scatterDensityPower = std::max(lighting.scatterDensityPower, 0.01f),

		.cloudLightingScale = std::max(lighting.brightness, 0.f),
		.sunExtinction = std::max(lighting.sunExtinction, 0.f),
		.viewExtinction = std::max(lighting.viewExtinction, 0.f),
		.ambientStrength = std::max(lighting.ambientStrength, 0.f),

		.msAlbedo = std::clamp(lighting.msAlbedo, 0.f, 0.9999f),
		.msStrength = std::max(lighting.msStrength, 0.f),

		.cloudEvolution = { static_cast<float>(std::fmod(volWind.phase, phasePeriod)), volWind.disturbance,
			static_cast<float>(std::fmod(previousWind.phase, phasePeriod)), previousWind.disturbance },
		.previousViewProj = volHistoryViewProj,

		.ndfBoundaryRect = { boundaryOrigin.x, boundaryOrigin.y, boundaryOrigin.x + boundaryExtent.x, boundaryOrigin.y + boundaryExtent.y },
		.localNdfRect = localRect,
		.localNdfAltitude = localAltitude,
		.localNdfAuxiliary = ndfManager.GetHeightAuxiliaryRange(),
		.lowCloudPhase = lowCloudPhase.coefficients,
		.stormCount = cloudStormManager.Count(),
		.voxelCount = voxelCloudManager.Count(),
		.voxelAltitude = voxelCloudManager.Altitude(),
		.skipShapeNoise = settings.cloudNoise.skipShapeNoise ? 1u : 0u,
		.useVoxelNoise = ndfVoxelNoise ? 1u : 0u,
		.noisePadding = { volMainHistoryValid ? volHistoryCurvatureRadius : cloudCurvatureRadius, cloudWeatherChanged ? 1.f / 32.f : 0.f },
		.middleLayer = { KilometersToGameUnits(middleAltitude.x * .001f), KilometersToGameUnits(middleAltitude.y * .001f),
			middleWeather && std::isfinite(middle.densityScale) ? std::clamp(middle.densityScale, 0.f, 1.f) : 0.f,
			Util::Units::GAME_UNIT_TO_M / (16384.f * (std::isfinite(middle.uvSize) ? std::clamp(middle.uvSize, .01f, 100.f) : 1.f)) },
		.middleShape = { std::isfinite(middle.edgeSoftness) ? std::clamp(middle.edgeSoftness, .01f, .49f) : .1f,
			0.f,
			Util::Units::GAME_UNIT_TO_M / (std::isfinite(middle.detailScale) ? std::clamp(middle.detailScale, 200.f, 4000.f) : 256.f),
			static_cast<float>(std::min(middle.detailMode, 1u)) },
		.middleCells = { std::isfinite(middle.cellFrequency) ? std::clamp(middle.cellFrequency, .25f, 16.f) : 2.f,
			std::isfinite(middle.cellStrength) ? std::clamp(middle.cellStrength, 0.f, 1.f) : .9f,
			std::isfinite(middle.stratusCellStrength) ? std::clamp(middle.stratusCellStrength, 0.f, 1.f) : .1f,
			std::isfinite(middle.warpStrength) ? std::clamp(middle.warpStrength, 0.f, .5f) : .1f },
		.middleBands = { std::cos(bandAngle) / bandWavelength, std::sin(bandAngle) / bandWavelength,
			bandValue(middle.bands.warp, 0.f, 1.f, .25f), bandCoverage },
		.middleBandShape = { bandThickness, bandLift, bandWavelength, bandCoverage + bandThickness + bandLift > 0.f ? 1.f : 0.f },
	};
	cloudStormManager.UpdateLighting(lighting.dropletDiameter);
	cloudStormManager.UpdateMotion(volWind.stormTime, previousWind.stormTime,
		CloudVelocity(settings.cloudLayer.wind.lowVelocity), CloudVelocity(settings.cloudLayer.wind.highVelocity),
		(lowAltitudeRange.x + lowAltitudeRange.y) * 500.f, settings.cloudLayer.cirrus.GetAltitudeKm() * 1000.f);
	cloudData.msTransportExtinction = 1.f - cloudData.msAlbedo * static_cast<float>(lowCloudPhase.MeanCosine());
	cloudData.msBoundaryWidth = std::clamp(lighting.msBoundaryWidth, 1.f, 200.f);
	cloudData.msCalibration = CloudMsCoefficients(cloudData.msAlbedo, lowCloudPhase);
	volCloudCb->Update(cloudData);

	const auto depthTarget = globals::features::viewAtmosphere.CompositeEnabled() && a_pass == VolumetricCloudPass::kMainViewAndCubemap ?
	                             RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN :
	                             RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY;

	// Shared SRVs for both passes
	auto ndfTextures = ndfManager.GetNdf(settings.cloudMap, ndfTexManager);
	if (!ndfTextures)
		return;

	auto* cloudConstants = volCloudCb->CB();
	context->CSSetConstantBuffers(1, 1, &cloudConstants);
	context->CSSetShaderResources(99, 1, &middleWeather);
	// Reuse the mipmapped NDF noises; middle detail is evaluated independently
	// of the coarse weather-map resolution in every upper-cloud density query.
	const auto& middleNoise = ndfManager.GetNoiseInputs();
	context->CSSetShaderResources(101, 3, middleNoise.data() + 1);
	if (voxelActive)
		voxelCloudManager.Bind();
	if (ndfVoxelNoise)
		context->CSSetShaderResources(76, 1, &ndfVoxelNoise);
	std::array<ID3D11ShaderResourceView*, 12> srvs = {
		texTrLut->srv.get(),                                                  // t1
		texApLut->srv.get(),                                                  // t2
		renderer->GetDepthStencilData().depthStencils[depthTarget].depthSRV,  // t3
		baseShapeNoiseSrv.get(),                                              // t4 cloud shape noise
		ndfTextures.height,                                                   // t5 NDF height
		ndfTextures.modeling,                                                 // t6 NDF modeling
		globals::deferred->GetDirectionalShadowMap(),                         // t7
		texMsLut->srv.get(),                                                  // t8
		renderCirrus ? cirrusTextures.weather : nullptr,                      // t9
		renderCirrus ? cirrusTextures.patterns : nullptr,                     // t10
		cloudProfileLutSrv.get(),                                             // t11 vertical profile
		cloudAdjustmentLutSrv.get(),                                          // t12 profile expansion and noise warp
	};
	ID3D11ShaderResourceView* ambientShSrv = texVolCloudAmbientSH ? texVolCloudAmbientSH->srv.get() : nullptr;
	std::array<ID3D11SamplerState*, 3> samplers = { sampTileable.get(), sampTr.get(), sampSv.get() };
	context->CSSetSamplers(2, (uint)samplers.size(), samplers.data());
	if (voxelActive && a_pass == VolumetricCloudPass::kMainViewAndCubemap)
		voxelCloudManager.UpdateLighting(csVoxelLighting.get(), cloudData.dirlightDir);
	if (a_pass == VolumetricCloudPass::kMainViewAndCubemap && texVolCloudAmbientSH) {
		state->BeginPerfEvent("Volumetric Clouds: Ambient SH");
		ID3D11ShaderResourceView* nullAmbientSrv = nullptr;
		context->CSSetShaderResources(13, 1, &nullAmbientSrv);
		auto* groundReflection = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS].SRV;
		context->CSSetShaderResources(39, 1, &groundReflection);
		auto* partialOutput = texCloudAmbientPartial->uav.get();
		context->CSSetShaderResources(1, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(1, 1, &partialOutput, nullptr);
		context->CSSetShader(csVolAmbientSH.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::VolumetricCloudAmbientSH");
		context->Dispatch(8, 1, 1);
		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetUnorderedAccessViews(1, 1, &nullUav, nullptr);
		auto* partialInput = texCloudAmbientPartial->srv.get();
		context->CSSetShaderResources(90, 1, &partialInput);
		auto* ambientShOutput = texVolCloudAmbientSH->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &ambientShOutput, nullptr);
		context->CSSetShader(csCloudAmbientReduce.get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		globals::profiler->EndPass();
		context->CSSetShaderResources(90, 1, &nullAmbientSrv);
		context->CSSetShaderResources(39, 1, &nullAmbientSrv);

		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		if (cloudData.ambientStrength > 0.f && (cloudData.msAlbedo > 0.f || cloudData.stormCount > 0u)) {
			context->CSSetShaderResources(13, 1, &ambientShSrv);
			auto* ambientOutput = texCloudAmbientLighting->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &ambientOutput, nullptr);
			context->CSSetShader(csCloudAmbientLighting.get(), nullptr, 0);
			globals::profiler->BeginPass("PhysicalSky::CloudAmbientLighting");
			context->Dispatch(1, 5, 1);
			globals::profiler->EndPass();
			context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		}
		context->CSSetShader(nullptr, nullptr, 0);
		state->EndPerfEvent();
	}

	std::array<ID3D11ShaderResourceView*, 3> outputSrvs = { texVolTr->srv.get(), texVolLum->srv.get(), texShadowVolume->srv.get() };

	context->CSSetSamplers(2, (uint)samplers.size(), samplers.data());
	const auto localMaps = localNdfManager.GetTextures();
	ID3D11ShaderResourceView* localSrvs[] = { localMaps.height, localMaps.modeling, localNdfManager.GetMaximum(), localNdfManager.GetWeights() };
	context->CSSetShaderResources(32, 4, localSrvs);
	ID3D11ShaderResourceView* stormSrvs[] = { cloudStormManager.Height(), cloudStormManager.Modeling(), cloudStormManager.Data() };
	context->CSSetShaderResources(55, 3, stormSrvs);

	if (a_pass == VolumetricCloudPass::kShadowVolume) {
		// Shadow path: accumulate the cloud extinction column into the 3D shadow
		// volume along the light direction.
		state->BeginPerfEvent("Volumetric Clouds: Shadow Volume");
		std::array<ID3D11UnorderedAccessView*, 1> uavs = { texShadowVolumeRaw->uav.get() };
		ID3D11ShaderResourceView* nullPsShadowSrv = nullptr;
		context->PSSetShaderResources(112, 1, &nullPsShadowSrv);

		context->CSSetShaderResources(1, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		// Dispatch based on dominant light direction component
		const float shadowRangeGu = KilometersToGameUnits(settings.shadowVolumeRange);
		const float shadowThicknessGu = std::max(cloudData.shadowVolumeTop - cloudData.shadowVolumeBottom, 1.f);
		float3 ray_px_dir = { -cloudLightDir.x, -cloudLightDir.y, -cloudLightDir.z };
		ray_px_dir.x *= kShadowVolW / shadowRangeGu;
		ray_px_dir.y *= kShadowVolH / shadowRangeGu;
		ray_px_dir.z *= kShadowVolD / shadowThicknessGu;
		const float dir_max_component = std::max({ std::abs(ray_px_dir.x), std::abs(ray_px_dir.y), std::abs(ray_px_dir.z) });
		const bool horizontal = std::abs(ray_px_dir.x) == dir_max_component || std::abs(ray_px_dir.y) == dir_max_component;
		// Long, nearly horizontal low/storm rays need the parallel scan's latency
		// hiding. Short columns and sparse multi-layer weather favour coherent rays.
		const bool serialColumns = !voxelActive && cloudData.stormCount == 0u && (!horizontal || cloudData.middleLayer.z > 0.f);
		auto* shadowShader = voxelActive                      ? csVolShadowVoxel.get() :
		                     cloudData.stormCount == 0u       ? (serialColumns ? csVolShadowColumns.get() : csVolShadowWeather.get()) :
		                     cloudStormManager.VolcanicOnly() ? csVolShadowVolcanic.get() :
		                                                        csVolShadowVolume.get();
		context->CSSetShader(shadowShader, nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::VolumetricShadowVolume");
		if (serialColumns) {
			const uint32_t columnsY = horizontal ? kShadowVolD : kShadowVolH;
			context->Dispatch((kShadowVolW + 7u) / 8u, (columnsY + 7u) / 8u, 1);
		} else if (horizontal) {
			context->Dispatch(kShadowVolW, kShadowVolD, 1);
		} else {
			constexpr uint32_t columnsPerGroup = kShadowVolW / kShadowVolD;
			static_assert(kShadowVolW % kShadowVolD == 0);
			context->Dispatch(kShadowVolW / columnsPerGroup, kShadowVolH, 1);
		}
		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		auto* rawShadow = texShadowVolumeRaw->srv.get();
		context->CSSetShaderResources(14, 1, &rawShadow);
		auto* filteredShadow = texShadowVolume->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &filteredShadow, nullptr);
		context->CSSetShader(csVolShadowResample.get(), nullptr, 0);
		context->Dispatch((kShadowVolW + 7u) / 8u, (kShadowVolH + 7u) / 8u, (kShadowVolD + 3u) / 4u);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		context->CSSetShaderResources(14, 1, &nullPsShadowSrv);
		globals::profiler->EndPass();

		context->CSSetShaderResources(69, 1, &nullPsShadowSrv);
		context->PSSetShaderResources(69, 1, &nullPsShadowSrv);
		auto* shadowInput = texShadowVolume->srv.get();
		context->CSSetShaderResources(14, 1, &shadowInput);
		auto* meanOutput = texCloudShadowMean->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &meanOutput, nullptr);
		context->CSSetShader(voxelActive ? csCloudShadowMeanVoxel.get() : (cloudData.stormCount == 0u ? csCloudShadowMeanWeather.get() : (cloudStormManager.VolcanicOnly() ? csCloudShadowMeanVolcanic.get() : csCloudShadowMean.get())), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::CloudShadowMean");
		context->Dispatch(1, kShadowMeanHeights, 1);
		globals::profiler->EndPass();
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		context->CSSetShaderResources(14, 1, &nullPsShadowSrv);
		state->EndPerfEvent();
	}

	if (a_pass == VolumetricCloudPass::kMainViewAndCubemap) {
		auto* ambientInput = texCloudAmbientLighting->srv.get();
		context->CSSetShaderResources(40, 1, &ambientInput);
		context->CSSetShaderResources(1, (uint)srvs.size(), srvs.data());
		ID3D11UnorderedAccessView* nullBoundsUav = nullptr;
		ID3D11ShaderResourceView* noMiddleBounds = nullptr;
		context->CSSetShaderResources(105, 1, &noMiddleBounds);
		if (cloudData.middleLayer.z > 0.f && csMiddleBounds && csMiddleBoundsReduce) {
			if (middleBoundsDirty) {
				globals::profiler->BeginPass("PhysicalSky::MiddleCloudBounds");
				auto* output = middleBoundsUavs[0].get();
				context->CSSetUnorderedAccessViews(5, 1, &output, nullptr);
				context->CSSetShader(csMiddleBounds.get(), nullptr, 0);
				context->Dispatch(kMiddleBoundsSize, kMiddleBoundsSize, 1);
				ID3D11UnorderedAccessView* noOutput = nullptr;
				context->CSSetUnorderedAccessViews(5, 1, &noOutput, nullptr);
				context->CSSetShader(csMiddleBoundsReduce.get(), nullptr, 0);
				for (uint32_t mip = 1; mip < kMiddleBoundsLevels; ++mip) {
					auto* input = middleBoundsSrvs[mip - 1].get();
					output = middleBoundsUavs[mip].get();
					context->CSSetShaderResources(105, 1, &input);
					context->CSSetUnorderedAccessViews(5, 1, &output, nullptr);
					const auto groups = ((kMiddleBoundsSize >> mip) + 7u) / 8u;
					context->Dispatch(groups, groups, 1);
					context->CSSetUnorderedAccessViews(5, 1, &noOutput, nullptr);
					context->CSSetShaderResources(105, 1, &noMiddleBounds);
				}
				globals::profiler->EndPass();
				middleBoundsDirty = false;
			}
			auto* input = texMiddleBounds->srv.get();
			context->CSSetShaderResources(105, 1, &input);
		}
		if (cloudData.middleLayer.z > 0.f || renderCirrus) {
			ID3D11ShaderResourceView* noSunInput = nullptr;
			context->CSSetShaderResources(100, 1, &noSunInput);
			auto* output = texUpperSunDepth->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(csUpperSunDepth.get(), nullptr, 0);
			globals::profiler->BeginPass("PhysicalSky::UpperSunDepth");
			context->Dispatch(32, 32, 1);
			globals::profiler->EndPass();
			context->CSSetUnorderedAccessViews(0, 1, &nullBoundsUav, nullptr);
			auto* input = texUpperSunDepth->srv.get();
			context->CSSetShaderResources(100, 1, &input);
		}
		if (!volMainHistoryValid || cloudHeightBoundsDirty) {
			state->BeginPerfEvent("Volumetric Clouds: Height Bounds");
			globals::profiler->BeginPass("PhysicalSky::CloudHeightBounds");
			auto* boundsOutput = texCloudHeightBoundsTiles->uav.get();
			context->CSSetUnorderedAccessViews(6, 1, &boundsOutput, nullptr);
			context->CSSetShader(csCloudHeightBounds.get(), nullptr, 0);
			context->Dispatch(16, 16, 1);
			context->CSSetUnorderedAccessViews(6, 1, &nullBoundsUav, nullptr);
			auto* boundsInput = texCloudHeightBoundsTiles->srv.get();
			context->CSSetShaderResources(16, 1, &boundsInput);
			context->CSSetShaderResources(54, 1, &boundsInput);
			boundsOutput = texCloudHeightBounds->uav.get();
			context->CSSetUnorderedAccessViews(6, 1, &boundsOutput, nullptr);
			auto* weatherBoundsOutput = texCloudWeatherBounds->uav.get();
			context->CSSetUnorderedAccessViews(5, 1, &weatherBoundsOutput, nullptr);
			context->CSSetShader(csCloudHeightBoundsReduce.get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->CSSetUnorderedAccessViews(6, 1, &nullBoundsUav, nullptr);
			context->CSSetUnorderedAccessViews(5, 1, &nullBoundsUav, nullptr);
			cloudHeightBoundsDirty = false;
			globals::profiler->EndPass();
			state->EndPerfEvent();
		}
		auto* heightBounds = texCloudHeightBounds->srv.get();
		context->CSSetShaderResources(16, 1, &heightBounds);
		auto* weatherBounds = texCloudWeatherBounds->srv.get();
		context->CSSetShaderResources(89, 1, &weatherBounds);

		if (cloudData.ambientStrength > 0.f && (cloudData.msAlbedo > 0.f || cloudData.stormCount > 0u)) {
			state->BeginPerfEvent("Volumetric Clouds: Ambient Transport");
			globals::profiler->BeginPass("PhysicalSky::CloudAmbientTransport");
			ID3D11ShaderResourceView* nullSrv = nullptr;
			ID3D11UnorderedAccessView* nullUavs[2] = {};
			ID3D11ShaderResourceView* nullSources[8] = {};
			context->CSSetShaderResources(42, 8, nullSources);
			const bool resetField = !cloudAmbientValid;
			if (resetField) {
				const float clear[4] = {};
				for (auto& layout : texCloudAmbientLayout)
					context->ClearUnorderedAccessViewFloat(layout->uav.get(), clear);
				cloudAmbientIterations.fill(0);
				cloudAmbientUpdateIndex = 0;
				cloudAmbientValid = true;
			}
			static constexpr uint32_t schedule[] = { 0, 1, 0, 2, 0, 3 };
			const uint32_t cascade = schedule[cloudAmbientUpdateIndex++ % std::size(schedule)];
			const uint32_t width = cascade == 0 ? kCloudAmbientVolumeWidth : kCloudAmbientVolumeWidth / 2;
			const uint32_t depth = width / 2;
			CloudAmbientVolumeCB volumeData{
				.phase = lowCloudPhase.AmbientPhase(cloudData.msAlbedo),
				.cascade = cascade,
				.width = width,
				.reset = resetField ? 1u : 0u,
				.iterations = ++cloudAmbientIterations[cascade]
			};
			cloudAmbientVolumeCB->Update(volumeData);
			auto* constants = cloudAmbientVolumeCB->CB();
			context->CSSetConstantBuffers(2, 1, &constants);
			auto* output = texCloudAmbientBoundary->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(csCloudAmbientBoundary.get(), nullptr, 0);
			context->Dispatch(1, 5, 1);
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			auto* boundary = texCloudAmbientBoundary->srv.get();
			context->CSSetShaderResources(47, 1, &boundary);

			output = texCloudAmbientDensity->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(voxelActive ? csCloudAmbientDensityVoxel.get() : (cloudStormManager.VolcanicOnly() && csCloudAmbientDensityVolcanic ? csCloudAmbientDensityVolcanic.get() : csCloudAmbientDensity.get()), nullptr, 0);
			context->Dispatch(width / 4, width / 4, depth / 4);
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			auto* density = texCloudAmbientDensity->srv.get();
			context->CSSetShaderResources(41, 1, &density);
			for (uint32_t i = 0; i < kCloudAmbientCascadeCount; ++i) {
				auto* source = texCloudAmbientSource[i][0]->srv.get();
				context->CSSetShaderResources(i < 3 ? 42 + i : 49, 1, &source);
			}
			auto* previousLayout = texCloudAmbientLayout[0]->srv.get();
			context->CSSetShaderResources(46, 1, &previousLayout);
			context->CSSetShaderResources(45, 1, &nullSrv);
			output = texCloudAmbientSweep->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(cascade == 0 ? csCloudAmbientSweep.get() : csCloudAmbientSweepFar.get(), nullptr, 0);
			context->Dispatch(2 * width, 2 * width, 26);
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			auto* intervals = texCloudAmbientSweep->srv.get();
			context->CSSetShaderResources(45, 1, &intervals);
			output = texCloudAmbientSource[cascade][1]->uav.get();
			context->CSSetUnorderedAccessViews(1, 1, &output, nullptr);
			context->CSSetShader(csCloudAmbientProject.get(), nullptr, 0);
			context->Dispatch(width / 4, width / 4, depth / 4);
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			context->CSSetShaderResources(42, 8, nullSources);
			context->CSSetShaderResources(41, 1, &nullSrv);
			context->CopyResource(texCloudAmbientLayout[1]->resource.get(), texCloudAmbientLayout[0]->resource.get());
			output = texCloudAmbientLayout[1]->uav.get();
			context->CSSetUnorderedAccessViews(1, 1, &output, nullptr);
			context->CSSetShader(csCloudAmbientLayout.get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			texCloudAmbientSource[cascade][0].swap(texCloudAmbientSource[cascade][1]);
			texCloudAmbientLayout[0].swap(texCloudAmbientLayout[1]);
			for (uint32_t i = 0; i < kCloudAmbientCascadeCount; ++i) {
				auto* source = texCloudAmbientSource[i][0]->srv.get();
				context->CSSetShaderResources(i < 3 ? 42 + i : 49, 1, &source);
			}
			auto* layout = texCloudAmbientLayout[0]->srv.get();
			context->CSSetShaderResources(48, 1, &layout);
			context->CSSetShaderResources(47, 1, &boundary);
			ID3D11UnorderedAccessView* viewOutputs[kCloudAmbientCascadeCount] = {};
			ID3D11UnorderedAccessView* nullViewOutputs[kCloudAmbientCascadeCount] = {};
			ID3D11ShaderResourceView* viewInputs[kCloudAmbientCascadeCount] = {};
			context->CSSetShaderResources(50, kCloudAmbientCascadeCount, viewInputs);
			for (uint32_t i = 0; i < kCloudAmbientCascadeCount; ++i) {
				viewOutputs[i] = texCloudAmbientView[i]->uav.get();
				viewInputs[i] = texCloudAmbientView[i]->srv.get();
			}
			context->CSSetUnorderedAccessViews(0, kCloudAmbientCascadeCount, viewOutputs, nullptr);
			context->CSSetShader(csCloudAmbientView.get(), nullptr, 0);
			static_assert(kCloudAmbientVolumeWidth == 32 && kCloudAmbientVolumeDepth == 16 && kCloudAmbientCascadeCount == 4);
			constexpr uint32_t nearViewGroups = (kCloudAmbientVolumeWidth / 4) * (kCloudAmbientVolumeWidth / 4) * (kCloudAmbientVolumeDepth / 4);
			constexpr uint32_t farViewGroups = nearViewGroups / 8;
			context->Dispatch(nearViewGroups + (kCloudAmbientCascadeCount - 1) * farViewGroups, 1, 1);
			context->CSSetUnorderedAccessViews(0, kCloudAmbientCascadeCount, nullViewOutputs, nullptr);
			context->CSSetShaderResources(50, kCloudAmbientCascadeCount, viewInputs);
			globals::profiler->EndPass();
			state->EndPerfEvent();
		} else {
			cloudAmbientValid = false;
		}

		globals::features::viewAtmosphere.Bind(true);
		ID3D11UnorderedAccessView* nullUavs[3] = {};
		ID3D11ShaderResourceView* nullTemporalSrvs[6] = {};
		ID3D11ShaderResourceView* nullOutputs[2] = {};
		context->PSSetShaderResources(110, 2, nullOutputs);
		context->PSSetShaderResources(114, 2, nullOutputs);
		context->PSSetShaderResources(116, 1, nullOutputs);
		context->PSSetShaderResources(113, 1, nullOutputs);
		context->PSSetShaderResources(109, 1, nullOutputs);
		texVolCubeTr.swap(texVolCubeHistoryTr);
		const auto voxelRect = voxelCloudManager.ScreenRect(cloudData.bottomZ, (renderW + 3u) / 4u, (renderH + 3u) / 4u);
		const bool voxelVisible = voxelRect[2] > voxelRect[0] && voxelRect[3] > voxelRect[1] && !state->isMapMenuOpen;
		texVolCubeLum.swap(texVolCubeHistoryLum);
		texVolCubeAux.swap(texVolCubeHistoryAux);
		texLightningCubeMask.swap(texLightningCubeHistory);
		context->CSSetShaderResources(1, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(13, 1, &ambientShSrv);
		lightning.QuerySource(csLightningQuery.get());
		std::array<ID3D11UnorderedAccessView*, 3> uavs = {};
		if (!state->isMapMenuOpen) {
			const CloudBoundaryCB boundaryData = {
				.gridOriginSpacing = { boundaryOrigin.x, boundaryOrigin.y, boundarySpacing.x, boundarySpacing.y },
				.fieldFrequencyWind = { cloudData.lowNdfFrequency.x, cloudData.lowNdfFrequency.y, noiseWindOffset.x, noiseWindOffset.y },
				.shearAltitude = { cloudData.cloudShapeShear.x, cloudData.cloudShapeShear.y, cloudData.lowCloudBaseAltitude, cloudData.lowCloudTopAltitude },
				.evolution = cloudData.cloudEvolution,
				.frameDimensions = { textureDim.x, textureDim.y, static_cast<float>(lowW), static_cast<float>(lowH) },
				.cloudCurvatureRadius = cloudData.cloudCurvatureRadius,
				.bottomZ = cloudData.bottomZ,
				.gridCellCount = kCloudBoundaryCells,
				.cloudFrameIndex = volFrameIndex,
				.localRect = localRect,
				.localAltitude = localAltitude
			};
			RenderCloudBoundary(boundaryData, ndfTextures);
			auto* boundarySrv = texCloudBoundary->srv.get();
			context->CSSetShaderResources(15, 1, &boundarySrv);
			auto& terrainShadows = globals::features::terrainShadows;
			auto& cloudShadows = globals::features::cloudShadows;
			auto* cloudShadowLayer = cloudShadows.loaded ? cloudShadows.texCloudShadowLayers[CloudShadows::kMaxCloudLayers - 1] : nullptr;
			std::array<ID3D11ShaderResourceView*, 2> apShadowSrvs = {
				terrainShadows.IsShadowMapReady() ? terrainShadows.texShadowHeight->srv.get() : nullptr,
				cloudShadowLayer ? cloudShadowLayer->srv.get() : nullptr
			};
			ID3D11ShaderResourceView* directionalShadowLights = globals::deferred->directionalShadowLights ?
			                                                        globals::deferred->directionalShadowLights->srv.get() :
			                                                        nullptr;
			auto* volumeShadow = texShadowVolume->srv.get();
			context->CSSetShaderResources(14, 1, &volumeShadow);
			context->CSSetShaderResources(36, (uint)apShadowSrvs.size(), apShadowSrvs.data());
			context->CSSetShaderResources(98, 1, &directionalShadowLights);
			globals::features::viewAtmosphere.Bind(true);
			texVolTr.swap(texVolHistoryTr);
			texVolLum.swap(texVolHistoryLum);
			texVolAux.swap(texVolHistoryAux);
			texLightningMask.swap(texLightningHistory);
			uavs = { texVolLowTr->uav.get(), texVolLowLum->uav.get(), texVolLowAux->uav.get() };
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			auto* lightningOutput = texLightningTrace->uav.get();
			context->CSSetUnorderedAccessViews(3, 1, &lightningOutput, nullptr);
			auto* stormMotionOutput = texVolStormMotion->uav.get();
			context->CSSetUnorderedAccessViews(6, 1, &stormMotionOutput, nullptr);
			auto stormRect = cloudStormManager.ScreenRect(cloudData.bottomZ, lowW, lowH,
				{ cloudData.lowCloudTraceBottomAltitude, cloudData.lowCloudTraceTopAltitude }, cloudData.cloudCurvatureRadius);
			const bool stormVisible = stormRect[2] > stormRect[0] && stormRect[3] > stormRect[1];
			cloudDispatchCb->Update(stormRect);
			auto* dispatchConstants = cloudDispatchCb->CB();
			context->CSSetConstantBuffers(3, 1, &dispatchConstants);
			globals::profiler->BeginPass("PhysicalSky::VolumetricMainView");
			if (stormVisible) {
				auto* tilesOutput = texStormTiles->uav.get();
				context->CSSetUnorderedAccessViews(7, 1, &tilesOutput, nullptr);
				context->CSSetShader(csStormTiles.get(), nullptr, 0);
				context->Dispatch((lowW + 63u) / 64u, (lowH + 63u) / 64u, 1);
				context->CSSetUnorderedAccessViews(7, 1, nullUavs, nullptr);
				auto* tilesInput = texStormTiles->srv.get();
				context->CSSetShaderResources(88, 1, &tilesInput);
			}
			// Ordinary pixels retain the small, storm-free shader even while a storm
			// is visible. The two dispatches write disjoint pixels of the same outputs.
			context->CSSetShader(cloudData.localNdfAltitude.z <= 0.f ? csVolMainWeather.get() : csVolMainWithoutStorm.get(), nullptr, 0);
			context->Dispatch((lowW + 7u) / 8u, (lowH + 3u) / 4u, 1);
			if (stormVisible) {
				const bool volcanic = cloudStormManager.VolcanicOnly() && csVolMainVolcanic;
				auto* singleConstants = volcanic && csVolMainSingleVolcanic ? cloudStormManager.SingleData() : nullptr;
				winrt::com_ptr<ID3D11Buffer> previousConstants;
				if (singleConstants) {
					context->CSGetConstantBuffers(0, 1, previousConstants.put());
					context->CSSetConstantBuffers(0, 1, &singleConstants);
				}
				context->CSSetShader(singleConstants ? csVolMainSingleVolcanic.get() : (volcanic ? csVolMainVolcanic.get() : csVolMainView.get()), nullptr, 0);
				const uint32_t groupWidth = volcanic ? 16u : 8u;
				const uint32_t groupHeight = volcanic && !singleConstants ? 4u : 8u;
				context->Dispatch((stormRect[2] - stormRect[0] + groupWidth - 1u) / groupWidth,
					(stormRect[3] - stormRect[1] + groupHeight - 1u) / groupHeight, 1);
				if (singleConstants) {
					auto* previous = previousConstants.get();
					context->CSSetConstantBuffers(0, 1, &previous);
				}
			}
			globals::profiler->EndPass();
			context->CSSetShaderResources(88, 1, nullOutputs);
			stormMotionOutput = nullptr;
			context->CSSetUnorderedAccessViews(6, 1, &stormMotionOutput, nullptr);
			dispatchConstants = nullptr;
			context->CSSetConstantBuffers(3, 1, &dispatchConstants);
			context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
			if (voxelVisible) {
				cloudDispatchCb->Update(voxelRect);
				dispatchConstants = cloudDispatchCb->CB();
				context->CSSetConstantBuffers(3, 1, &dispatchConstants);
				// NDF and NVDF sample the same checkerboard phase. Combine their
				// low-resolution integrals before the shared temporal reconstruction.
				ID3D11UnorderedAccessView* nullLightning = nullptr;
				context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
				std::array<ID3D11ShaderResourceView*, 3> ndfInputs = { texVolLowTr->srv.get(), texVolLowLum->srv.get(), texVolLowAux->srv.get() };
				context->CSSetShaderResources(20, (uint)ndfInputs.size(), ndfInputs.data());
				auto* motionInput = texVolStormMotion->srv.get();
				context->CSSetShaderResources(58, 1, &motionInput);
				auto* maskInput = texLightningTrace->srv.get();
				context->CSSetShaderResources(90, 1, &maskInput);
				std::array<ID3D11UnorderedAccessView*, 4> voxelOutputs = { texVoxelTraceTr->uav.get(), texVoxelTraceLum->uav.get(), texVoxelTraceAux->uav.get(), texVoxelTraceLightning->uav.get() };
				context->CSSetUnorderedAccessViews(0, (uint)voxelOutputs.size(), voxelOutputs.data(), nullptr);
				auto* motionOutput = texVoxelTraceMotion->uav.get();
				context->CSSetUnorderedAccessViews(6, 1, &motionOutput, nullptr);
				context->CSSetShader(voxelCloudManager.Count() == 1 && csVoxelViewSingle ? csVoxelViewSingle.get() : csVoxelView.get(), nullptr, 0);
				globals::profiler->BeginPass("PhysicalSky::VoxelMainView");
				// Outside the voxel rectangle the shader copies the NDF sample.
				context->Dispatch((lowW + 7u) / 8u, (lowH + 7u) / 8u, 1);
				globals::profiler->EndPass();
				context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
				context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
				motionOutput = nullptr;
				context->CSSetUnorderedAccessViews(6, 1, &motionOutput, nullptr);
				context->CSSetShaderResources(20, 3, nullTemporalSrvs);
				context->CSSetShaderResources(58, 1, nullOutputs);
				context->CSSetShaderResources(90, 1, nullOutputs);
				texVolLowTr.swap(texVoxelTraceTr);
				texVolLowLum.swap(texVoxelTraceLum);
				texVolLowAux.swap(texVoxelTraceAux);
				texVolStormMotion.swap(texVoxelTraceMotion);
				texLightningTrace.swap(texVoxelTraceLightning);
				dispatchConstants = nullptr;
				context->CSSetConstantBuffers(3, 1, &dispatchConstants);
			}
			if (cloudData.middleLayer.z > 0.f || renderCirrus) {
				// Reuse the free composition targets after the voxel pass. Upper
				// layers retain their own march and share only temporal reconstruction.
				ID3D11UnorderedAccessView* nullLightning = nullptr;
				context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
				std::array<ID3D11ShaderResourceView*, 3> ndfInputs = { texVolLowTr->srv.get(), texVolLowLum->srv.get(), texVolLowAux->srv.get() };
				context->CSSetShaderResources(20, (uint)ndfInputs.size(), ndfInputs.data());
				auto* motionInput = texVolStormMotion->srv.get();
				context->CSSetShaderResources(58, 1, &motionInput);
				auto* maskInput = texLightningTrace->srv.get();
				context->CSSetShaderResources(90, 1, &maskInput);
				std::array<ID3D11UnorderedAccessView*, 4> voxelOutputs = { texVoxelTraceTr->uav.get(), texVoxelTraceLum->uav.get(), texVoxelTraceAux->uav.get(), texVoxelTraceLightning->uav.get() };
				context->CSSetUnorderedAccessViews(0, (uint)voxelOutputs.size(), voxelOutputs.data(), nullptr);
				auto* motionOutput = texVoxelTraceMotion->uav.get();
				context->CSSetUnorderedAccessViews(6, 1, &motionOutput, nullptr);
				globals::profiler->BeginPass("PhysicalSky::UpperCloudMainView");
				// Below the middle layer, opaque foreground can hide whole tiles.
				// Cirrus-only and inside/above-layer views keep the direct dispatch.
				const bool classify = cloudData.middleLayer.z > 0.f &&
				                      cameraPosition.z - cloudData.bottomZ < cloudData.middleLayer.x && csUpperTiles && csUpperViewTiles;
				if (classify) {
					auto* tileOutput = texUpperTiles->uav.get();
					context->CSSetUnorderedAccessViews(4, 1, &tileOutput, nullptr);
					context->CSSetShader(csUpperTiles.get(), nullptr, 0);
					context->Dispatch((lowW + 7u) / 8u, (lowH + 7u) / 8u, 1);
					tileOutput = nullptr;
					context->CSSetUnorderedAccessViews(4, 1, &tileOutput, nullptr);
					auto* tileInput = texUpperTiles->srv.get();
					context->CSSetShaderResources(87, 1, &tileInput);
				}
				context->CSSetShader(classify ? csUpperViewTiles.get() : csUpperView.get(), nullptr, 0);
				// All pixels use the same phase as the low-cloud trace.
				context->Dispatch((lowW + 7u) / 8u, (lowH + 7u) / 8u, 1);
				globals::profiler->EndPass();
				context->CSSetShaderResources(87, 1, nullOutputs);
				context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
				context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
				motionOutput = nullptr;
				context->CSSetUnorderedAccessViews(6, 1, &motionOutput, nullptr);
				context->CSSetShaderResources(20, 3, nullTemporalSrvs);
				context->CSSetShaderResources(58, 1, nullOutputs);
				context->CSSetShaderResources(90, 1, nullOutputs);
				texVolLowTr.swap(texVoxelTraceTr);
				texVolLowLum.swap(texVoxelTraceLum);
				texVolLowAux.swap(texVoxelTraceAux);
				texVolStormMotion.swap(texVoxelTraceMotion);
				texLightningTrace.swap(texVoxelTraceLightning);
				dispatchConstants = nullptr;
				context->CSSetConstantBuffers(3, 1, &dispatchConstants);
			}
			context->CSSetShaderResources(14, 1, nullOutputs);
			context->CSSetShaderResources(36, (uint)apShadowSrvs.size(), nullTemporalSrvs);
			context->CSSetShaderResources(98, 1, nullOutputs);

			std::array<ID3D11ShaderResourceView*, 3> historySrvs = {
				texVolHistoryTr->srv.get(), texVolHistoryLum->srv.get(), texVolHistoryAux->srv.get()
			};
			std::array<ID3D11ShaderResourceView*, 3> traceSrvs = {
				texVolLowTr->srv.get(), texVolLowLum->srv.get(), texVolLowAux->srv.get()
			};
			context->CSSetShaderResources(17, (uint)historySrvs.size(), historySrvs.data());
			ID3D11UnorderedAccessView* nullLightning = nullptr;
			context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
			ID3D11ShaderResourceView* lightningInputs[] = { texLightningTrace->srv.get(), texLightningHistory->srv.get() };
			context->CSSetShaderResources(90, 2, lightningInputs);
			lightningOutput = texLightningMask->uav.get();
			context->CSSetUnorderedAccessViews(3, 1, &lightningOutput, nullptr);
			context->CSSetShaderResources(20, (uint)traceSrvs.size(), traceSrvs.data());
			uavs = { texVolTr->uav.get(), texVolLum->uav.get(), texVolAux->uav.get() };
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			auto* stormMotionInput = texVolStormMotion->srv.get();
			context->CSSetShaderResources(58, 1, &stormMotionInput);
			context->CSSetShader(csVolReproject.get(), nullptr, 0);
			globals::profiler->BeginPass("PhysicalSky::VolumetricReproject");
			context->Dispatch((renderW + 7u) >> 3, (renderH + 7u) >> 3, 1);
			globals::profiler->EndPass();
			context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
			context->CSSetShaderResources(17, (uint)historySrvs.size(), nullTemporalSrvs);
			context->CSSetShaderResources(20, (uint)traceSrvs.size(), nullTemporalSrvs);
			context->CSSetShaderResources(58, 1, nullTemporalSrvs);

			const int blurRadius = std::clamp(settings.cloudBlurRadius, 0, 2);
			if (blurRadius == 0) {
				// Forward draws already hold these output identities. Keep them and
				// the unfiltered history separate without dispatching a blur shader.
				context->CopyResource(texVolFilteredTr->resource.get(), texVolTr->resource.get());
				context->CopyResource(texVolFilteredLum->resource.get(), texVolLum->resource.get());
				context->CopyResource(texVolFilteredAux->resource.get(), texVolAux->resource.get());
			} else {
				std::array<ID3D11ShaderResourceView*, 3> resultSrvs = {
					texVolTr->srv.get(), texVolLum->srv.get(), texVolAux->srv.get()
				};
				context->CSSetShaderResources(29, (uint)resultSrvs.size(), resultSrvs.data());
				std::array<ID3D11UnorderedAccessView*, 3> filteredUavs = {
					texVolFilteredTr->uav.get(), texVolFilteredLum->uav.get(), texVolFilteredAux->uav.get()
				};
				context->CSSetUnorderedAccessViews(3, (uint)filteredUavs.size(), filteredUavs.data(), nullptr);
				context->CSSetShader(blurRadius == 2 ? csVolFilterWide.get() : csVolFilter.get(), nullptr, 0);
				globals::profiler->BeginPass("PhysicalSky::VolumetricFilter");
				context->Dispatch((renderW + 7u) >> 3, (renderH + 7u) >> 3, 1);
				globals::profiler->EndPass();
				context->CSSetUnorderedAccessViews(3, 3, nullUavs, nullptr);
				context->CSSetShaderResources(29, (uint)resultSrvs.size(), nullTemporalSrvs);
			}
		} else {
			constexpr FLOAT clearTr[4] = { 1.f, 1.f, 1.f, 1.f };
			constexpr FLOAT clearLum[4] = {};
			context->ClearUnorderedAccessViewFloat(texVolTr->uav.get(), clearTr);
			context->ClearUnorderedAccessViewFloat(texVolLum->uav.get(), clearLum);
			context->ClearUnorderedAccessViewFloat(texVolAux->uav.get(), clearLum);
			context->ClearUnorderedAccessViewFloat(texVolFilteredTr->uav.get(), clearTr);
			context->ClearUnorderedAccessViewFloat(texVolFilteredLum->uav.get(), clearLum);
			context->ClearUnorderedAccessViewFloat(texVolFilteredAux->uav.get(), clearLum);
		}
		outputSrvs[0] = texVolFilteredTr->srv.get();
		outputSrvs[1] = texVolFilteredLum->srv.get();
		if (!state->isMapMenuOpen && cbData.lightningColorIntensity.w > 0.f) {
			ID3D11ShaderResourceView* inputs[] = { lightning.Segments(), texLightningMask->srv.get() };
			context->CSSetShaderResources(94, 2, inputs);
			ID3D11UnorderedAccessView* outputs[] = { texLightningChannels->uav.get(), texVolFilteredLum->uav.get() };
			context->CSSetUnorderedAccessViews(4, 2, outputs, nullptr);
			context->CSSetShader(csLightningComposite.get(), nullptr, 0);
			globals::profiler->BeginPass("PhysicalSky::LightningChannels");
			context->Dispatch((renderW + 7u) / 8u, (renderH + 7u) / 8u, 1);
			globals::profiler->EndPass();
			context->CSSetUnorderedAccessViews(4, 2, nullUavs, nullptr);
			context->CSSetShaderResources(94, 2, nullTemporalSrvs);
		}

		uavs = { texVolCubeTraceTr->uav.get(), texVolCubeTraceLum->uav.get(), texVolCubeTraceAux->uav.get() };
		auto* cubeLightningOutput = texLightningCubeTrace->uav.get();
		context->CSSetUnorderedAccessViews(3, 1, &cubeLightningOutput, nullptr);
		auto* reflectionBounds = texCloudHeightBoundsTiles->srv.get();
		context->CSSetShaderResources(54, 1, &reflectionBounds);
		auto* stormMotionOutput = texVolCubeStormMotion->uav.get();
		context->CSSetUnorderedAccessViews(7, 1, &stormMotionOutput, nullptr);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(voxelActive ? csVolCubemapVoxel.get() : (cloudStormManager.VolcanicOnly() && csVolCubemapVolcanic ? csVolCubemapVolcanic.get() : csVolCubemap.get()), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::VolumetricCubemap");
		context->Dispatch(kVolCubeSize / 4u, kVolCubeSize / 4u, 6);
		globals::profiler->EndPass();
		context->CSSetShaderResources(54, 1, nullTemporalSrvs);
		stormMotionOutput = nullptr;
		context->CSSetUnorderedAccessViews(7, 1, &stormMotionOutput, nullptr);
		context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
		std::array<ID3D11ShaderResourceView*, 6> cubeSrvs = {
			texVolCubeHistoryTr->srv.get(), texVolCubeHistoryLum->srv.get(), texVolCubeHistoryAux->srv.get(),
			texVolCubeTraceTr->srv.get(), texVolCubeTraceLum->srv.get(), texVolCubeTraceAux->srv.get()
		};
		context->CSSetShaderResources(23, (uint)cubeSrvs.size(), cubeSrvs.data());
		uavs = { texVolCubeTr->uav.get(), texVolCubeLum->uav.get(), texVolCubeAux->uav.get() };
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		auto* stormMotionInput = texVolCubeStormMotion->srv.get();
		context->CSSetShaderResources(59, 1, &stormMotionInput);
		context->CSSetShader(csVolCubeReproject.get(), nullptr, 0);
		ID3D11UnorderedAccessView* nullLightning = nullptr;
		context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
		ID3D11ShaderResourceView* lightningCubeInputs[] = { texLightningCubeTrace->srv.get(), texLightningCubeHistory->srv.get() };
		context->CSSetShaderResources(92, 2, lightningCubeInputs);
		cubeLightningOutput = texLightningCubeMask->uav.get();
		context->CSSetUnorderedAccessViews(3, 1, &cubeLightningOutput, nullptr);
		globals::profiler->BeginPass("PhysicalSky::VolumetricCubeReproject");
		context->Dispatch((kVolCubeSize + 7u) >> 3, (kVolCubeSize + 7u) >> 3, 6);
		globals::profiler->EndPass();
		context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
		context->CSSetShaderResources(23, (uint)cubeSrvs.size(), nullTemporalSrvs);
		context->CSSetShaderResources(59, 1, nullTemporalSrvs);
		context->CSSetUnorderedAccessViews(3, 1, &nullLightning, nullptr);
		if (lightning.HasSource()) {
			auto* mask = texLightningCubeMask->srv.get();
			auto* channel = lightning.Segments();
			context->CSSetShaderResources(94, 1, &channel);
			context->CSSetShaderResources(95, 1, &mask);
			auto* output = texLightningCubeLum->uav.get();
			context->CSSetUnorderedAccessViews(5, 1, &output, nullptr);
			context->CSSetShader(csLightningCubeComposite.get(), nullptr, 0);
			globals::profiler->BeginPass("PhysicalSky::LightningReflections");
			context->Dispatch((kVolCubeSize + 7u) / 8u, (kVolCubeSize + 7u) / 8u, 6);
			globals::profiler->EndPass();
			context->CSSetUnorderedAccessViews(5, 1, &nullLightning, nullptr);
		}
		lightning.CloudFrameComplete(volMainHistoryValid && !state->isMapMenuOpen);
		volMainHistoryValid = true;
		volHistoryWind = volWind;
		cloudStormManager.CommitMotion();
		volHistoryViewProj = globals::game::frameBufferCached.GetCameraViewProj();
		volHistoryCamera = { cameraPosition.x, cameraPosition.y, cameraPosition.z };
		volHistoryCurvatureRadius = cloudCurvatureRadius;
		volHistoryFrameDim = state->isMapMenuOpen ? float2{} : frameDim;
		++volFrameIndex;
	}

	// Cleanup
	{
		ID3D11ShaderResourceView* nullSrvs[86] = {};
		ID3D11UnorderedAccessView* nullUavs[3] = {};
		ID3D11Buffer* nullCb[1] = {};
		ID3D11SamplerState* nullSamplers[3] = {};
		context->CSSetShaderResources(0, ARRAYSIZE(nullSrvs), nullSrvs);
		context->CSSetShaderResources(89, 1, nullSrvs);
		context->CSSetShaderResources(90, 7, nullSrvs);
		context->CSSetShaderResources(99, 1, nullSrvs);
		context->CSSetShaderResources(100, 1, nullSrvs);
		context->CSSetShaderResources(101, 3, nullSrvs);
		context->CSSetShaderResources(105, 1, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
		context->CSSetUnorderedAccessViews(3, 3, nullUavs, nullptr);
		context->CSSetConstantBuffers(1, 1, nullCb);
		context->CSSetConstantBuffers(2, 1, nullCb);
		context->CSSetSamplers(2, 3, nullSamplers);
		context->CSSetShader(nullptr, nullptr, 0);

		// Make results available to any later pixel-shader passes that sample PhysicalSky/Common.hlsli.
		context->PSSetShaderResources(110, (uint)outputSrvs.size(), outputSrvs.data());
		auto* channels = texLightningChannels->srv.get();
		auto* reflection = texLightningCubeLum->srv.get();
		context->PSSetShaderResources(113, 1, &channels);
		context->PSSetShaderResources(109, 1, &reflection);
	}
}

ID3D11ShaderResourceView* PhysicalSky::GetDebugCubeFaceSrv(Texture2D* a_texture)
{
	if (!a_texture || !a_texture->resource)
		return nullptr;

	ID3D11Resource* resource = a_texture->resource.get();
	if (auto it = debugCubeFaceSrvs.find(resource); it != debugCubeFaceSrvs.end())
		return it->second.get();
	if (debugCubeFace < 0 || static_cast<uint32_t>(debugCubeFace) >= a_texture->desc.ArraySize)
		return nullptr;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = a_texture->desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
		.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = static_cast<UINT>(debugCubeFace), .ArraySize = 1 }
	};

	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	if (FAILED(globals::d3d::device->CreateShaderResourceView(resource, &srvDesc, srv.put())))
		return nullptr;

	return debugCubeFaceSrvs.emplace(resource, std::move(srv)).first->second.get();
}

void PhysicalSky::DrawDebugCube(Texture2D* a_texture, const char* a_label, float a_scale)
{
	if (!a_texture || !a_texture->srv)
		return;

	if (auto srv = GetDebugCubeFaceSrv(a_texture)) {
		ImGui::BulletText("%s", a_label);
		ImGui::Image(srv, { a_texture->desc.Width * a_scale, a_texture->desc.Height * a_scale });
	}
}

ID3D11ShaderResourceView* PhysicalSky::GetDebugVolumeSliceSrv(ID3D11ShaderResourceView* a_srv, eastl::unique_ptr<Texture2D>& a_target)
{
	if (!a_srv)
		return nullptr;

	winrt::com_ptr<ID3D11Resource> resource;
	a_srv->GetResource(resource.put());
	winrt::com_ptr<ID3D11Texture3D> texture;
	if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
		return nullptr;

	D3D11_TEXTURE3D_DESC desc;
	texture->GetDesc(&desc);

	if (!a_target || a_target->desc.Width != desc.Width || a_target->desc.Height != desc.Height || a_target->desc.Format != desc.Format) {
		D3D11_TEXTURE2D_DESC sliceDesc = {
			.Width = desc.Width,
			.Height = desc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = desc.Format,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = sliceDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		a_target = eastl::make_unique<Texture2D>(sliceDesc, "PhysicalSky::DebugVolumeSlice");
		a_target->CreateSRV(srvDesc);
	}

	// A 3D subresource covers every depth slice, so the slice is selected by the box.
	const D3D11_BOX box = { 0, 0, 0, desc.Width, desc.Height, 1 };
	globals::d3d::context->CopySubresourceRegion(a_target->resource.get(), 0, 0, 0, 0, texture.get(), 0, &box);

	return a_target->srv.get();
}

void PhysicalSky::DrawDebugVolume(ID3D11ShaderResourceView* a_srv, const char* a_label, eastl::unique_ptr<Texture2D>& a_target, float a_scale)
{
	if (!a_srv)
		return;

	if (auto srv = GetDebugVolumeSliceSrv(a_srv, a_target)) {
		ImGui::BulletText("%s", a_label);
		ImGui::Image(srv, { a_target->desc.Width * a_scale, a_target->desc.Height * a_scale });
	}
}

#define I18N_KEY_PREFIX "feature.physical_sky."

void PhysicalSky::DrawDebugCloudTexture(ID3D11ShaderResourceView* srv, const char* id, const char* label, float scale, bool scalar)
{
	texturePreview.Draw(srv, id, label, scale, scalar);
}

#undef I18N_KEY_PREFIX

#include "CloudNoiseGenerator.h"

#include "I18n/I18n.h"
#include "State.h"
#include "Util.h"
#include "Utils/AsyncShaderSet.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>

namespace
{
	float FiniteClamp(float value, float low, float high, float fallback)
	{
		return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
	}

	CloudNoiseBand Sanitize(CloudNoiseBand band, uint32_t maximumFrequency)
	{
		band.frequency = std::clamp(band.frequency, 1u, maximumFrequency);
		band.octaves = std::clamp(band.octaves, 1u, 6u);
		band.persistence = FiniteClamp(band.persistence, 0.f, 1.f, 0.5f);
		band.exponent = FiniteClamp(band.exponent, 0.1f, 6.f, 1.f);
		band.contrast = FiniteClamp(band.contrast, 0.f, 6.f, 1.f);
		band.bias = FiniteClamp(band.bias, -1.f, 1.f, 0.f);
		band.perlinMix = FiniteClamp(band.perlinMix, 0.f, 1.f, 1.f);
		band.padding = 0.f;
		return band;
	}
}

void CloudNoiseGenerator::SetupResources()
{
	parameters = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<Parameters>());
	shape = nullptr;
	pendingShape = nullptr;
	adjustment = nullptr;
	pendingAdjustment = nullptr;
	generatedSource = nullptr;
	pendingSource = nullptr;
	valid = pending = false;
}

void CloudNoiseGenerator::CompileShaders(Util::AsyncShaderSet& a_shaders)
{
	valid = pending = false;
	const std::array programs = { std::pair{ &shapeProgram, "generateShape" },
		std::pair{ &adjustmentProgram, "generateAdjustment" }, std::pair{ &adjustmentMipProgram, "generateAdjustmentMip" } };
	for (const auto& [program, entry] : programs)
		a_shaders.Add(*program, L"Data\\Shaders\\PhysicalSky\\CloudNoiseGenerate.cs.hlsl", {}, entry);
}

bool CloudNoiseGenerator::Update(const CloudNoiseSettings& settings, ID3D11ShaderResourceView* source)
{
	if (!parameters || !shapeProgram || !adjustmentProgram || !adjustmentMipProgram || !source)
		return false;

	winrt::com_ptr<ID3D11Resource> sourceResource;
	source->GetResource(sourceResource.put());
	const auto sourceTexture = sourceResource.try_as<ID3D11Texture2D>();
	if (!sourceTexture)
		return false;
	D3D11_TEXTURE2D_DESC sourceDesc;
	sourceTexture->GetDesc(&sourceDesc);
	D3D11_SHADER_RESOURCE_VIEW_DESC sourceView;
	source->GetDesc(&sourceView);
	if (sourceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
		return false;
	const auto firstMip = sourceView.Texture2D.MostDetailedMip;
	const auto width = std::max(sourceDesc.Width >> firstMip, 1u);
	const auto height = std::max(sourceDesc.Height >> firstMip, 1u);
	const auto mipCount = std::min(sourceView.Texture2D.MipLevels, sourceDesc.MipLevels - firstMip);
	Parameters data{};
	for (size_t i = 0; i < data.shape.size(); ++i)
		data.shape[i] = Sanitize(settings.shape[i], kShapeSize / 2u);
	for (size_t i = 0; i < data.warp.size(); ++i)
		data.warp[i] = Sanitize(settings.warp[i], std::max(std::min(width, height) / 2u, 1u));
	data.seed = settings.seed;
	data.warpCorrelation = FiniteClamp(settings.warpCorrelation, -1.f, 1.f, -0.35f);
	if (valid && generatedSource.get() == source && std::memcmp(&data, &generated, sizeof(data)) == 0) {
		pending = false;
		return false;
	}
	auto* device = globals::d3d::device;
	auto* context = globals::d3d::context;
	if (!pending || pendingSource.get() != source || std::memcmp(&data, &requested, sizeof(data)) != 0) {
		requested = data;
		pendingSource.copy_from(source);
		nextSlice = 0;
		pending = true;
		if (!pendingShape) {
			D3D11_TEXTURE3D_DESC desc{};
			desc.Width = desc.Height = desc.Depth = kShapeSize;
			desc.MipLevels = 8;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			pendingShape = eastl::make_unique<Texture3D>(desc, "PhysicalSky::GeneratedCloudNoise");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
			srv.Texture3D.MipLevels = desc.MipLevels;
			pendingShape->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uav.Texture3D.WSize = kShapeSize;
			pendingShape->CreateUAV(uav);
		}
		if (!pendingAdjustment || pendingAdjustment->desc.Width != width || pendingAdjustment->desc.Height != height || pendingAdjustment->desc.MipLevels != mipCount) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = mipCount;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			pendingAdjustment = eastl::make_unique<Texture2D>(desc, "PhysicalSky::GeneratedCloudAdjustment");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = mipCount;
			pendingAdjustment->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			pendingAdjustment->CreateUAV(uav);
		}
	}

	globals::profiler->BeginPass("PhysicalSky::CloudNoiseGeneration");
	data.firstSlice = nextSlice;
	parameters->Update(data);
	auto* cb = parameters->CB();
	context->CSSetConstantBuffers(1, 1, &cb);
	auto* output = pendingShape->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	context->CSSetShader(shapeProgram.get(), nullptr, 0);
	context->Dispatch(kShapeSize / 4u, kShapeSize / 4u, kSlicesPerFrame / 4u);
	ID3D11UnorderedAccessView* nullUavs[2] = {};
	ID3D11ShaderResourceView* nullSrvs[2] = {};
	context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
	nextSlice += kSlicesPerFrame;
	const bool complete = nextSlice == kShapeSize;
	if (complete) {
		context->GenerateMips(pendingShape->srv.get());
		context->CSSetShaderResources(0, 1, &source);
		output = pendingAdjustment->uav.get();
		context->CSSetUnorderedAccessViews(1, 1, &output, nullptr);
		context->CSSetShader(adjustmentProgram.get(), nullptr, 0);
		context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
		context->CSSetUnorderedAccessViews(1, 1, nullUavs, nullptr);
		context->CSSetShader(adjustmentMipProgram.get(), nullptr, 0);
		for (uint32_t mip = 1; mip < mipCount; ++mip) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = pendingAdjustment->desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MostDetailedMip = mip - 1u;
			srv.Texture2D.MipLevels = 1;
			winrt::com_ptr<ID3D11ShaderResourceView> previous;
			DX::ThrowIfFailed(device->CreateShaderResourceView(pendingAdjustment->resource.get(), &srv, previous.put()));
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = srv.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uav.Texture2D.MipSlice = mip;
			winrt::com_ptr<ID3D11UnorderedAccessView> target;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(pendingAdjustment->resource.get(), &uav, target.put()));
			data.mip = mip;
			parameters->Update(data);
			auto* input = previous.get();
			output = target.get();
			context->CSSetShaderResources(1, 1, &input);
			context->CSSetUnorderedAccessViews(1, 1, &output, nullptr);
			context->Dispatch((std::max(width >> mip, 1u) + 7u) / 8u, (std::max(height >> mip, 1u) + 7u) / 8u, 1);
			context->CSSetUnorderedAccessViews(1, 1, nullUavs, nullptr);
			context->CSSetShaderResources(1, 1, nullSrvs);
		}
		shape.swap(pendingShape);
		adjustment.swap(pendingAdjustment);
		generated = requested;
		generatedSource = pendingSource;
		valid = true;
		pending = false;
	}
	context->CSSetShaderResources(0, 2, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();
	return complete;
}

#define I18N_KEY_PREFIX "feature.physical_sky."

void CloudNoiseGenerator::DrawSettings(CloudNoiseSettings& settings)
{
	if (!ImGui::TreeNode(T(TKEY("cloud_noise_inputs"), "Cloud Noise Inputs")))
		return;
	ImGui::Checkbox(T(TKEY("procedural_cloud_noise"), "Procedural Noise"), &settings.procedural);
	if (settings.procedural) {
		if (ImGui::Button(T(TKEY("reset_cloud_noise"), "Reset Noise Defaults")))
			settings = {};
		ImGui::InputScalar(T(TKEY("cloud_noise_seed"), "Noise Seed"), ImGuiDataType_U32, &settings.seed);
		const char* labels[] = {
			T(TKEY("cloud_noise_billows"), "R: Perlin-Worley Billows"),
			T(TKEY("cloud_noise_coarse_wisps"), "G: Coarse Wisps"),
			T(TKEY("cloud_noise_near_wisps"), "B: Near Wisps"),
			T(TKEY("cloud_noise_fine_erosion"), "A: Fine Erosion"),
			T(TKEY("cloud_noise_warp_x"), "G: Warp X"),
			T(TKEY("cloud_noise_warp_y"), "B: Warp Y")
		};
		for (uint32_t i = 0; i < 6; ++i) {
			ImGui::PushID(i);
			if (ImGui::TreeNode(labels[i])) {
				auto& band = i < 4 ? settings.shape[i] : settings.warp[i - 4];
				const uint32_t one = 1, maxFrequency = i < 4 ? 64 : 32, maxOctaves = 6;
				ImGui::SliderScalar(T(TKEY("cloud_noise_frequency"), "Cells Per Tile"), ImGuiDataType_U32, &band.frequency, &one, &maxFrequency);
				ImGui::SliderScalar(T(TKEY("cloud_noise_octaves"), "Octaves"), ImGuiDataType_U32, &band.octaves, &one, &maxOctaves);
				ImGui::SliderFloat(T(TKEY("cloud_noise_persistence"), "Detail Weight"), &band.persistence, 0.f, 1.f);
				ImGui::SliderFloat(T(TKEY("cloud_noise_response"), "Response Exponent"), &band.exponent, 0.1f, 6.f);
				ImGui::SliderFloat(T(TKEY("cloud_noise_contrast"), "Contrast"), &band.contrast, 0.f, 6.f);
				ImGui::SliderFloat(T(TKEY("cloud_noise_bias"), "Bias"), &band.bias, -1.f, 1.f);
				if (i == 0)
					ImGui::SliderFloat(T(TKEY("cloud_noise_perlin_mix"), "Perlin Blend"), &band.perlinMix, 0.f, 1.f);
				if (i > 0 && i < 4)
					ImGui::TextUnformatted(T(TKEY("cloud_noise_alligator"), "Alligator Noise"));
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::SliderFloat(T(TKEY("cloud_noise_warp_correlation"), "Warp Correlation"), &settings.warpCorrelation, -1.f, 1.f);
		if (pending)
			ImGui::ProgressBar(static_cast<float>(nextSlice) / kShapeSize, { -1, 0 }, T(TKEY("cloud_noise_generating"), "Generating noise"));
		if (!shapeProgram || !adjustmentProgram || !adjustmentMipProgram)
			ImGui::TextColored({ 1, 0.3f, 0.2f, 1 }, "%s", T(TKEY("cloud_noise_shader_missing"), "Noise generation shaders are unavailable."));
		ImGui::TextWrapped("%s", T(TKEY("cloud_noise_generation_hint"), "Edits generate a new tile over several frames. The complete tile and its mip chain replace the previous noise together."));
		ImGui::TextWrapped("%s", T(TKEY("cloud_noise_bandwidth_hint"), "Higher Cells Per Tile produces smaller features. Octaves finer than the texture can resolve are omitted. Vertical height profiles remain unchanged."));
	} else
		ImGui::TextWrapped("%s", T(TKEY("cloud_noise_dds_hint"), "Uses available DDS inputs. Missing shape or adjustment inputs use procedural noise. Reload Cloud Textures after replacing files."));
	ImGui::TreePop();
}

#undef I18N_KEY_PREFIX

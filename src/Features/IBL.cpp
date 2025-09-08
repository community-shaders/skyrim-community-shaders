#include "IBL.h"

#include "Deferred.h"
#include "DynamicCubemaps.h"
#include "Shadercache.h"
#include "State.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	IBL::Settings,
	EnableDiffuseIBL,
	PreserveFogLuminance,
	UseStaticIBL,
	DiffuseIBLScale,
	DALCAmount,
	IBLSaturation,
	FogAmount,
	DynamicCubemapsAmount)

void IBL::DrawSettings()
{
	ImGui::Checkbox("Enable Diffuse IBL", (bool*)&settings.EnableDiffuseIBL);
	ImGui::SliderFloat("Diffuse IBL Scale", &settings.DiffuseIBLScale, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Diffuse IBL Saturation", &settings.IBLSaturation, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat("DALC Amount", &settings.DALCAmount, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Use Static IBL For Out-of-World Objects", (bool*)&settings.UseStaticIBL);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enables the use of static IBL textures for objects that are not in the world (e.g. inventory items).");
	}
	ImGui::SliderFloat("Fog Mix", &settings.FogAmount, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Preserve Fog Luminance", (bool*)&settings.PreserveFogLuminance);
	ImGui::SliderFloat("Dynamic Cubemaps Amount", &settings.DynamicCubemapsAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Samples from dynamic cubemaps.\n"
			"Requires the Dynamic Cubemaps feature to be enabled.\n"
			"May cause dynamic cubemaps sampling accumulation issues under certain conditions.");
	}
}

void IBL::LoadSettings(json& o_json)
{
	settings = o_json;
}

void IBL::SaveSettings(json& o_json)
{
	o_json = settings;
}

void IBL::RestoreDefaultSettings()
{
	settings = {};
}

void IBL::EarlyPrepass()
{
	if (loaded) {
		auto context = globals::d3d::context;

		// Set PS shader resource
		{
			std::array<ID3D11ShaderResourceView*, 3> srvs = {
				diffuseIBLTexture->srv.get(),
				staticDiffuseIBLTexture->srv.get(),
				staticSpecularIBLTexture->srv.get()
			};
			context->PSSetShaderResources(76, 3, srvs.data());
		}
	}
}

void IBL::Prepass()
{
	auto context = globals::d3d::context;

	auto renderer = globals::game::renderer;
	auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;

	auto& envTexture = dynamicCubemaps.envTexture;
	auto& envReflectionsTexture = dynamicCubemaps.envReflectionsTexture;

	std::array<ID3D11ShaderResourceView*, 3> srvs = {
		reflections.SRV,
		(dynamicCubemaps.loaded && envTexture) ? envTexture->srv.get() : nullptr,
		(dynamicCubemaps.loaded && envReflectionsTexture) ? envReflectionsTexture->srv.get() : nullptr
	};
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { diffuseIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// Unset PS shader resource
	{
		ID3D11ShaderResourceView* srv = nullptr;
		context->PSSetShaderResources(76, 1, &srv);
	}

	// IBL
	{
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
		context->Dispatch(1, 1, 1);
	}

	// Reset
	{
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		samplers.fill(nullptr);

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Set PS shader resource
	{
		ID3D11ShaderResourceView* srv = diffuseIBLTexture->srv.get();
		context->PSSetShaderResources(76, 1, &srv);
	}
}

void IBL::SetupResources()
{
	GetDiffuseIBLCS();

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
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
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		diffuseIBLTexture = new Texture2D(texDesc);
		diffuseIBLTexture->CreateSRV(srvDesc);
		diffuseIBLTexture->CreateUAV(uavDesc);
	}

	auto device = globals::d3d::device;

	logger::debug("Loading static Diffuse IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\DiffuseIBL.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		staticDiffuseIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		staticDiffuseIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticDiffuseIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		staticDiffuseIBLTexture->CreateSRV(srvDesc);
	}

	logger::debug("Loading static Specular IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\SpecIBL.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		staticSpecularIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		staticSpecularIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticSpecularIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 8 }
		};
		staticSpecularIBLTexture->CreateSRV(srvDesc);
	}
}

void IBL::ClearShaderCache()
{
	if (diffuseIBLCS)
		diffuseIBLCS->Release();
	diffuseIBLCS = nullptr;
}

ID3D11ComputeShader* IBL::GetDiffuseIBLCS()
{
	std::vector<std::pair<const char*, const char*>> defines;
	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });
	if (!diffuseIBLCS)
		diffuseIBLCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\IBL\\DiffuseIBLCS.hlsl", defines, "cs_5_0"));
	return diffuseIBLCS;
}
#include "../PhysicalSky.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

#include "../TerrainShadows.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

bool TextureManager::LoadTexture(std::filesystem::path path)
{
	auto device = State::GetSingleton()->device;
	auto context = State::GetSingleton()->context;

	auto path_str = path.string();
	if (!tex_list.contains(path_str))
		tex_list.emplace(path_str, nullptr);

	return SUCCEEDED(DirectX::CreateDDSTextureFromFile(device, context, path.wstring().c_str(), nullptr, tex_list.at(path_str).put()));
}

bool PhysicalSky::HasShaderDefine(RE::BSShader::Type type)
{
	switch (type) {
	case RE::BSShader::Type::Sky:
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::DistantTree:
	case RE::BSShader::Type::Effect:
	case RE::BSShader::Type::Water:
		return true;
		break;
	default:
		return false;
		break;
	}
}

void PhysicalSky::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto device = State::GetSingleton()->device;

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, transmittance_sampler.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sky_view_sampler.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, tileable_sampler.put()));
	}

	logger::debug("Creating structured buffers...");
	{
		phys_sky_sb = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<PhysSkySB>(), 1);
		phys_sky_sb->CreateSRV();
	}

	logger::debug("Creating LUT textures...");
	{
		D3D11_TEXTURE2D_DESC tex2d_desc{
			.Width = s_transmittance_width,
			.Height = s_transmittance_height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		transmittance_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		transmittance_lut->CreateSRV(srv_desc);
		transmittance_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_multiscatter_width;
		tex2d_desc.Height = s_multiscatter_height;

		multiscatter_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		multiscatter_lut->CreateSRV(srv_desc);
		multiscatter_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_sky_view_width;
		tex2d_desc.Height = s_sky_view_height;

		sky_view_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		sky_view_lut->CreateSRV(srv_desc);
		sky_view_lut->CreateUAV(uav_desc);

		D3D11_TEXTURE3D_DESC tex3d_desc{
			.Width = s_aerial_perspective_width,
			.Height = s_aerial_perspective_height,
			.Depth = s_aerial_perspective_depth,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srv_desc.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		uav_desc.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = s_aerial_perspective_depth };

		aerial_perspective_lut = eastl::make_unique<Texture3D>(tex3d_desc);
		aerial_perspective_lut->CreateSRV(srv_desc);
		aerial_perspective_lut->CreateUAV(uav_desc);
	}

	logger::debug("Creating render textures...");
	{
		D3D11_TEXTURE2D_DESC tex_desc;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&tex_desc);
		tex_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		tex_desc.MipLevels = 1;

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = tex_desc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		{
			main_view_tr_tex = eastl::make_unique<Texture2D>(tex_desc);
			main_view_tr_tex->CreateSRV(srv_desc);
			main_view_tr_tex->CreateUAV(uav_desc);

			main_view_lum_tex = eastl::make_unique<Texture2D>(tex_desc);
			main_view_lum_tex->CreateSRV(srv_desc);
			main_view_lum_tex->CreateUAV(uav_desc);
		}

		D3D11_TEXTURE3D_DESC tex3d_desc{
			.Width = s_shadow_volume_size,
			.Height = s_shadow_volume_size,
			.Depth = s_shadow_volume_height,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		srv_desc.Format = uav_desc.Format = tex3d_desc.Format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srv_desc.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		uav_desc.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = tex3d_desc.Depth };

		{
			shadow_volume_tex = eastl::make_unique<Texture3D>(tex3d_desc);
			shadow_volume_tex->CreateSRV(srv_desc);
			shadow_volume_tex->CreateUAV(uav_desc);
		}
	}

	LoadNDFTextures();

	CompileComputeShaders();
}

void PhysicalSky::LoadNDFTextures()
{
	auto device = State::GetSingleton()->device;
	auto context = State::GetSingleton()->context;

	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Textures\\PhysicalSky\\ndf.dds", nullptr, ndf_tex_srv.put());
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Textures\\PhysicalSky\\top_lut.dds", nullptr, cloud_top_lut_srv.put());
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Textures\\PhysicalSky\\bottom_lut.dds", nullptr, cloud_bottom_lut_srv.put());
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Textures\\PhysicalSky\\nubis.dds", nullptr, nubis_noise_srv.put());
}

void PhysicalSky::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string_view entry = "main";
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &transmittance_program, "LUTGen.cs.hlsl", { { "LUTGEN", "0" } } },
		{ &multiscatter_program, "LUTGen.cs.hlsl", { { "LUTGEN", "1" } } },
		{ &sky_view_program, "LUTGen.cs.hlsl", { { "LUTGEN", "2" } } },
		{ &aerial_perspective_program, "LUTGen.cs.hlsl", { { "LUTGEN", "3" } } },
		{ &main_view_program, "Volumetrics.cs.hlsl" },
		{ &shadow_volume_program, "Volumetrics.cs.hlsl", {}, "renderShadowVolume" }
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalSky") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.data())))
			info.csPtr->attach(rawPtr);
	}
}

void PhysicalSky::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&transmittance_program, &multiscatter_program, &sky_view_program, &aerial_perspective_program, &main_view_program, &shadow_volume_program
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void PhysicalSky::Reset()
{
	UpdateBuffer();
}

void PhysicalSky::Prepass()
{
	if (phys_sky_sb_data.enable_sky) {
		GenerateLuts();
		RenderShadowMapMainView();
	} else {
		auto context = State::GetSingleton()->context;
		{
			FLOAT clr[4] = { 1., 1., 1., 1. };
			context->ClearUnorderedAccessViewFloat(main_view_tr_tex->uav.get(), clr);
		}
		{
			FLOAT clr[4] = { 0., 0., 0., 0. };
			context->ClearUnorderedAccessViewFloat(main_view_lum_tex->uav.get(), clr);
		}
	}

	auto context = State::GetSingleton()->context;

	std::array<ID3D11ShaderResourceView*, 8> srvs = {
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		sky_view_lut->srv.get(),
		aerial_perspective_lut->srv.get(),
		nullptr,
		nullptr,
		shadow_volume_tex->srv.get()
	};

	if (phys_sky_sb_data.enable_sky) {
		auto sky = RE::Sky::GetSingleton();
		if (auto masser = sky->masser) {
			RE::NiTexturePtr masser_tex;
			RE::BSShaderManager::GetTexture(masser->stateTextures[current_moon_phases[0]].c_str(), true, masser_tex, false);  // TODO: find the phase
			if (masser_tex)
				srvs.at(5) = reinterpret_cast<RE::NiSourceTexture*>(masser_tex.get())->rendererTexture->resourceView;
		}
		if (auto secunda = sky->secunda) {
			RE::NiTexturePtr secunda_tex;
			RE::BSShaderManager::GetTexture(secunda->stateTextures[current_moon_phases[1]].c_str(), true, secunda_tex, false);
			if (secunda_tex)
				srvs.at(6) = reinterpret_cast<RE::NiSourceTexture*>(secunda_tex.get())->rendererTexture->resourceView;
		}
	}

	context->PSSetShaderResources(100, (uint)srvs.size(), srvs.data());

	if (phys_sky_sb_data.enable_sky) {
		ID3D11SamplerState* samplers[2] = {
			transmittance_sampler.get(),
			sky_view_sampler.get()
		};
		context->PSSetSamplers(3, ARRAYSIZE(samplers), samplers);
	}
}

bool PhysicalSky::GenerateNoise(const std::filesystem::path& filename, uint type, float base_freq, uint octaves, float persistence, float lacunarity, uint seed)
{
	auto context = State::GetSingleton()->context;
	auto device = State::GetSingleton()->device;

	constexpr auto noise_funcs = std::array{
		"Worley",
		"Alligator"
	};
	if ((type >= noise_funcs.size()))
		return false;

	// create texture
	D3D11_TEXTURE3D_DESC tex3d_desc{
		.Width = s_noise_size,
		.Height = s_noise_size,
		.Depth = s_noise_size,
		.MipLevels = std::bit_width(s_noise_size) - 1,
		.Format = DXGI_FORMAT_R16_UNORM,
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
		.CPUAccessFlags = 0,
		.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS
	};
	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
		.Format = tex3d_desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
		.Texture3D = { .MostDetailedMip = 0, .MipLevels = std::bit_width(s_noise_size) - 1 }
	};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
		.Format = tex3d_desc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = s_noise_size }
	};

	auto tex = eastl::make_unique<Texture3D>(tex3d_desc);
	tex->CreateSRV(srv_desc);
	tex->CreateUAV(uav_desc);

	// create shader
	winrt::com_ptr<ID3D11ComputeShader> shader = nullptr;

	auto base_freq_str = std::format("{}", base_freq);
	auto octaves_str = std::format("{}", octaves);
	auto persistence_str = std::format("{}", persistence);
	auto lacunarity_str = std::format("{}", lacunarity);
	auto seed_str = std::format("{}", seed);
	std::vector<std::pair<const char*, const char*>> defines = {
		{ "NOISE_FUNC", noise_funcs.at(type) },
		{ "BASE_FREQ", base_freq_str.c_str() },
		{ "OCTAVES", octaves_str.c_str() },
		{ "PERSISTENCE", persistence_str.c_str() },
		{ "LACUNARITY", lacunarity_str.c_str() },
		{ "SEED", seed_str.c_str() },
	};

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalSky\\Noise.cs.hlsl", defines, "cs_5_0")))
		shader.attach(rawPtr);
	else
		return false;

	// render
	auto uav = tex->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(shader.get(), nullptr, 0);
	context->Dispatch((s_noise_size + 7) >> 3, (s_noise_size + 7) >> 3, s_noise_size);

	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	context->GenerateMips(tex->srv.get());

	// save
	DirectX::ScratchImage image;
	if (FAILED(CaptureTexture(device, context, tex->resource.get(), image)))
		return false;
	return SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, filename.wstring().c_str()));
}

void PhysicalSky::GenerateLuts()
{
	auto state = State::GetSingleton();
	auto context = State::GetSingleton()->context;

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[4] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11Buffer* buffer = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11ClassInstance* instance = nullptr;
		ID3D11SamplerState* samplers[3] = { nullptr };
		UINT numInstances;
	} old, newer;
	context->CSGetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetConstantBuffers(0, 1, &old.buffer);
	context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs);
	context->CSGetSamplers(2, ARRAYSIZE(old.samplers), old.samplers);

	state->BeginPerfEvent("Physical Sky: LUT Generation");

	/* ---- DISPATCH ---- */
	newer.srvs[0] = phys_sky_sb->SRV(0);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);

	newer.samplers[0] = tileable_sampler.get();
	newer.samplers[1] = transmittance_sampler.get();
	newer.samplers[2] = sky_view_sampler.get();
	context->CSSetSamplers(3, ARRAYSIZE(newer.samplers), newer.samplers);

	// -> transmittance
	newer.uavs[0] = transmittance_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShader(transmittance_program.get(), nullptr, 0);
	context->Dispatch(((s_transmittance_width - 1) >> 5) + 1, ((s_transmittance_height - 1) >> 5) + 1, 1);

	// -> multiscatter
	newer.uavs[0] = multiscatter_lut->uav.get();
	newer.srvs[1] = transmittance_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetShader(multiscatter_program.get(), nullptr, 0);
	context->Dispatch(((s_multiscatter_width - 1) >> 5) + 1, ((s_multiscatter_height - 1) >> 5) + 1, 1);

	// -> sky-view
	newer.uavs[0] = sky_view_lut->uav.get();
	newer.srvs[2] = multiscatter_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetShader(sky_view_program.get(), nullptr, 0);
	context->Dispatch(((s_sky_view_width - 1) >> 5) + 1, ((s_sky_view_height - 1) >> 5) + 1, 1);

	// -> aerial perspective
	newer.uavs[0] = aerial_perspective_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShader(aerial_perspective_program.get(), nullptr, 0);
	context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	context->CSSetConstantBuffers(0, 1, &old.buffer);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetSamplers(3, ARRAYSIZE(old.samplers), old.samplers);

	state->EndPerfEvent();
}

void PhysicalSky::RenderShadowMapMainView()
{
	auto state = State::GetSingleton();
	auto& context = state->context;
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto deferred = Deferred::GetSingleton();

	float2 size = Util::ConvertToDynamic(state->screenSize);
	uint resolution[2] = { (uint)size.x, (uint)size.y };

	auto srvs = std::array{
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		aerial_perspective_lut->srv.get(),
		renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV,
		nubis_noise_srv.get(),
		ndf_tex_srv.get(),
		cloud_top_lut_srv.get(),
		cloud_bottom_lut_srv.get(),
	};
	std::array<ID3D11ShaderResourceView*, 4> shadow_srvs = {
		deferred->shadowView,
		deferred->perShadow->srv.get(),
		TerrainShadows::GetSingleton()->IsHeightMapReady() ? TerrainShadows::GetSingleton()->texShadowHeight->srv.get() : nullptr,
		nullptr,
	};
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { shadow_volume_tex->uav.get(), nullptr };
	std::array<ID3D11SamplerState*, 3> samplers = { tileable_sampler.get(), transmittance_sampler.get(), sky_view_sampler.get() };

	context->CSSetSamplers(2, (uint)samplers.size(), samplers.data());

	// shadow volume
	{
		state->BeginPerfEvent("Physical Sky: Shadow Volume");

		float3 ray_px_dir = -phys_sky_sb_data.dirlight_dir;
		ray_px_dir.x *= s_shadow_volume_size / phys_sky_sb_data.shadow_volume_range;
		ray_px_dir.y *= s_shadow_volume_size / phys_sky_sb_data.shadow_volume_range;
		ray_px_dir.z *= s_shadow_volume_height / phys_sky_sb_data.cloud_layer.thickness;
		float dir_max_component = std::max(std::max(abs(ray_px_dir.x), abs(ray_px_dir.y)), abs(ray_px_dir.z));
		uint dispatch_size[2];
		if (abs(ray_px_dir.x) == dir_max_component || abs(ray_px_dir.y) == dir_max_component) {
			dispatch_size[0] = s_shadow_volume_size;
			dispatch_size[1] = s_shadow_volume_height;
		} else {
			dispatch_size[0] = dispatch_size[1] = s_shadow_volume_size;
		}

		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(20, (uint)shadow_srvs.size(), shadow_srvs.data());
		context->CSSetShader(shadow_volume_program.get(), nullptr, 0);
		context->Dispatch(dispatch_size[0], dispatch_size[1], 1);

		state->EndPerfEvent();
	}

	// main view
	{
		state->BeginPerfEvent("Physical Sky: Main View");

		uavs = { main_view_tr_tex->uav.get(), main_view_lum_tex->uav.get() };
		shadow_srvs.at(3) = shadow_volume_tex->srv.get();

		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);  // uav first!
		context->CSSetShaderResources(20, (uint)shadow_srvs.size(), shadow_srvs.data());
		context->CSSetShader(main_view_program.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);

		state->EndPerfEvent();
	}

	// cleanup
	samplers.fill(nullptr);
	srvs.fill(nullptr);
	shadow_srvs.fill(nullptr);
	uavs.fill(nullptr);
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(20, (uint)shadow_srvs.size(), shadow_srvs.data());
	context->CSSetSamplers(2, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}
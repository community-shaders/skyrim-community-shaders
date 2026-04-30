#include "SDFGI.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SDFGI::Settings,
	Enabled,
	NumCascades,
	MinCellSize,
	Energy,
	NormalBias,
	BounceFeedback,
	RayCount,
	FramesToConverge,
	FramesToUpdateLight,
	UseOcclusion,
	YScale,
	ShowDebug)

void SDFGI::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void SDFGI::DrawSettings()
{
	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	ImGui::SeparatorText("General");

	ImGui::Checkbox("Enabled", &settings.Enabled);

	{
		int nc = (int)settings.NumCascades;
		if (ImGui::SliderInt("Cascades", &nc, 1, MAX_CASCADES)) {
			settings.NumCascades = (uint)nc;
			recompileFlag = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Number of SDF cascades. More cascades cover larger areas but use more memory.");
	}

	ImGui::SliderFloat("Min Cell Size", &settings.MinCellSize, 0.5f, 32.f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Size of the smallest cascade cell in world units. Smaller values give finer detail.");

	ImGui::SliderFloat("Energy", &settings.Energy, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat("Normal Bias", &settings.NormalBias, 0.0f, 4.0f, "%.2f");

	ImGui::SeparatorText("Quality");

	{
		int rc = (int)settings.RayCount;
		if (ImGui::SliderInt("Ray Count", &rc, 4, 128))
			settings.RayCount = (uint)rc;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Rays per probe for integration. More rays reduce noise but cost more.");
	}

	ImGui::SliderFloat("Bounce Feedback", &settings.BounceFeedback, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multi-bounce indirect lighting. Higher values produce more bounces but may cause energy blowup.");

	{
		int ftc = (int)settings.FramesToConverge;
		if (ImGui::SliderInt("Frames to Converge", &ftc, 4, 64))
			settings.FramesToConverge = (uint)ftc;
	}

	{
		int ftul = (int)settings.FramesToUpdateLight;
		if (ImGui::SliderInt("Frames to Update Light", &ftul, 1, 16))
			settings.FramesToUpdateLight = (uint)ftul;
	}

	ImGui::Checkbox("Occlusion", &settings.UseOcclusion);

	ImGui::SliderFloat("Y Scale", &settings.YScale, 0.5f, 2.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Y-axis scaling for anisotropic voxelization.");

	ImGui::SeparatorText("Debug");
	ImGui::Checkbox("Show Debug", &settings.ShowDebug);

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		if (giOutput) {
			BUFFER_VIEWER_NODE(giOutput, debugRescale);
		}

		ImGui::TreePop();
	}
}

void SDFGI::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.NumCascades = std::clamp(settings.NumCascades, 1u, (uint)MAX_CASCADES);
	settings.RayCount = std::clamp(settings.RayCount, 4u, 128u);
	recompileFlag = true;
}

void SDFGI::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SDFGI::CreateCascadeResources(uint cascadeIndex)
{
	auto device = globals::d3d::device;
	auto& c = cascades[cascadeIndex];

	auto namePrefix = std::string("SDFGI::Cascade") + std::to_string(cascadeIndex);

	// SDF texture (R8_UNORM 128^3)
	{
		D3D11_TEXTURE3D_DESC desc = {
			.Width = CASCADE_SIZE,
			.Height = CASCADE_SIZE,
			.Depth = CASCADE_SIZE,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R8_UNORM,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.sdfTex = eastl::make_unique<Texture3D>(desc, (namePrefix + "::SDF").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R8_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		c.sdfTex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R8_UNORM,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = CASCADE_SIZE }
		};
		c.sdfTex->CreateUAV(uavDesc);
	}

	// Light texture (R32_UINT 128^3, RGBE encoded)
	{
		D3D11_TEXTURE3D_DESC desc = {
			.Width = CASCADE_SIZE,
			.Height = CASCADE_SIZE,
			.Depth = CASCADE_SIZE,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.lightTex = eastl::make_unique<Texture3D>(desc, (namePrefix + "::Light").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		c.lightTex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = CASCADE_SIZE }
		};
		c.lightTex->CreateUAV(uavDesc);
	}

	// Anisotropic light 0 (RGBA8 128^3)
	{
		D3D11_TEXTURE3D_DESC desc = {
			.Width = CASCADE_SIZE,
			.Height = CASCADE_SIZE,
			.Depth = CASCADE_SIZE,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.lightAniso0Tex = eastl::make_unique<Texture3D>(desc, (namePrefix + "::LightAniso0").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		c.lightAniso0Tex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = CASCADE_SIZE }
		};
		c.lightAniso0Tex->CreateUAV(uavDesc);
	}

	// Anisotropic light 1 (RG8 128^3)
	{
		D3D11_TEXTURE3D_DESC desc = {
			.Width = CASCADE_SIZE,
			.Height = CASCADE_SIZE,
			.Depth = CASCADE_SIZE,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R8G8_UNORM,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.lightAniso1Tex = eastl::make_unique<Texture3D>(desc, (namePrefix + "::LightAniso1").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R8G8_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		c.lightAniso1Tex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R8G8_UNORM,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = CASCADE_SIZE }
		};
		c.lightAniso1Tex->CreateUAV(uavDesc);
	}

	// Solid cell buffer (structured buffer of ProcessVoxel)
	uint solidCellCount = (uint)(CASCADE_SIZE * CASCADE_SIZE * CASCADE_SIZE * 0.25f);
	{
		D3D11_BUFFER_DESC desc = {
			.ByteWidth = solidCellCount * sizeof(ProcessVoxel),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(ProcessVoxel),
		};
		c.solidCellBuffer = eastl::make_unique<Buffer>(desc, nullptr, (namePrefix + "::SolidCells").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = solidCellCount }
		};
		DX::ThrowIfFailed(device->CreateShaderResourceView(c.solidCellBuffer->resource.get(), &srvDesc, c.solidCellSRV.put()));
		Util::SetResourceName(c.solidCellSRV.get(), "%s::SolidCells SRV", namePrefix.c_str());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = solidCellCount }
		};
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(c.solidCellBuffer->resource.get(), &uavDesc, c.solidCellUAV.put()));
		Util::SetResourceName(c.solidCellUAV.get(), "%s::SolidCells UAV", namePrefix.c_str());
	}

	// Dispatch indirect buffer (4 uints: groupX, groupY, groupZ, totalCount)
	{
		D3D11_BUFFER_DESC desc = {
			.ByteWidth = 4 * sizeof(uint),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS,
		};
		uint initData[4] = { 0, 0, 0, 0 };
		D3D11_SUBRESOURCE_DATA init = { .pSysMem = initData };
		c.dispatchBuffer = eastl::make_unique<Buffer>(desc, &init, (namePrefix + "::Dispatch").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = 4 }
		};
		DX::ThrowIfFailed(device->CreateShaderResourceView(c.dispatchBuffer->resource.get(), &srvDesc, c.dispatchSRV.put()));
		Util::SetResourceName(c.dispatchSRV.get(), "%s::Dispatch SRV", namePrefix.c_str());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = 4 }
		};
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(c.dispatchBuffer->resource.get(), &uavDesc, c.dispatchUAV.put()));
		Util::SetResourceName(c.dispatchUAV.get(), "%s::Dispatch UAV", namePrefix.c_str());
	}

	// Probe history (R16G16B16A16_SINT, 2D array)
	{
		uint probeTexW = PROBE_AXIS_SIZE * PROBE_AXIS_SIZE;
		uint probeTexH = PROBE_AXIS_SIZE * SH_SIZE;
		D3D11_TEXTURE2D_DESC desc = {
			.Width = probeTexW,
			.Height = probeTexH,
			.MipLevels = 1,
			.ArraySize = settings.FramesToConverge,
			.Format = DXGI_FORMAT_R16G16B16A16_SINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.probeHistoryTex = eastl::make_unique<Texture2D>(desc, (namePrefix + "::ProbeHistory").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R16G16B16A16_SINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = settings.FramesToConverge }
		};
		c.probeHistoryTex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R16G16B16A16_SINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = settings.FramesToConverge }
		};
		c.probeHistoryTex->CreateUAV(uavDesc);
	}

	// Probe average (R32G32B32A32_SINT, single 2D)
	{
		uint probeTexW = PROBE_AXIS_SIZE * PROBE_AXIS_SIZE;
		uint probeTexH = PROBE_AXIS_SIZE * SH_SIZE;
		D3D11_TEXTURE2D_DESC desc = {
			.Width = probeTexW,
			.Height = probeTexH,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_SINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		c.probeAverageTex = eastl::make_unique<Texture2D>(desc, (namePrefix + "::ProbeAverage").c_str());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R32G32B32A32_SINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		c.probeAverageTex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R32G32B32A32_SINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		c.probeAverageTex->CreateUAV(uavDesc);
	}
}

void SDFGI::CreateSharedResources()
{
	auto makeVoxelTex3D = [](const char* name, DXGI_FORMAT fmt, uint size) {
		D3D11_TEXTURE3D_DESC desc = {
			.Width = size,
			.Height = size,
			.Depth = size,
			.MipLevels = 1,
			.Format = fmt,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		auto tex = eastl::make_unique<Texture3D>(desc, name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = fmt,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		tex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = fmt,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = size }
		};
		tex->CreateUAV(uavDesc);

		return tex;
	};

	renderAlbedo = makeVoxelTex3D("SDFGI::RenderAlbedo", DXGI_FORMAT_R16_UINT, CASCADE_SIZE);
	renderEmission = makeVoxelTex3D("SDFGI::RenderEmission", DXGI_FORMAT_R32_UINT, CASCADE_SIZE);
	renderGeomFacing = makeVoxelTex3D("SDFGI::RenderGeomFacing", DXGI_FORMAT_R32_UINT, CASCADE_SIZE);

	renderSdf[0] = makeVoxelTex3D("SDFGI::RenderSDF[0]", DXGI_FORMAT_R8G8B8A8_UINT, CASCADE_SIZE);
	renderSdf[1] = makeVoxelTex3D("SDFGI::RenderSDF[1]", DXGI_FORMAT_R8G8B8A8_UINT, CASCADE_SIZE);

	uint halfSize = CASCADE_SIZE / 2;
	renderSdfHalf[0] = makeVoxelTex3D("SDFGI::RenderSDFHalf[0]", DXGI_FORMAT_R8G8B8A8_UINT, halfSize);
	renderSdfHalf[1] = makeVoxelTex3D("SDFGI::RenderSDFHalf[1]", DXGI_FORMAT_R8G8B8A8_UINT, halfSize);

	// Lightprobe octahedral data (R32_UINT 2DArray, size = (PROBE_AXIS_SIZE * OCT_SIZE + 2) per cascade)
	{
		uint probeTexSize = PROBE_AXIS_SIZE * (LIGHTPROBE_OCT_SIZE + 2);
		D3D11_TEXTURE2D_DESC desc = {
			.Width = probeTexSize,
			.Height = probeTexSize,
			.MipLevels = 1,
			.ArraySize = settings.NumCascades * 2,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		lightprobeData = eastl::make_unique<Texture2D>(desc, "SDFGI::LightprobeData");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = settings.NumCascades * 2 }
		};
		lightprobeData->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = 0, .ArraySize = settings.NumCascades * 2 }
		};
		lightprobeData->CreateUAV(uavDesc);
	}

	// Light buffer (structured buffer of SDFGILight, SRV only, CPU-writable)
	{
		lightBuffer = eastl::make_unique<StructuredBuffer>(
			StructuredBufferDesc<SDFGILight>(MAX_LIGHTS, true),
			MAX_LIGHTS, "SDFGI::LightBuffer");
		lightBuffer->CreateSRV();
	}

	// Occlusion (R16_UINT 3D, packed RGBA4)
	{
		D3D11_TEXTURE3D_DESC desc = {
			.Width = CASCADE_SIZE * 2,
			.Height = CASCADE_SIZE,
			.Depth = CASCADE_SIZE * settings.NumCascades,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16_UINT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		occlusionTex = eastl::make_unique<Texture3D>(desc, "SDFGI::Occlusion");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R16_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		occlusionTex->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R16_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = CASCADE_SIZE * settings.NumCascades }
		};
		occlusionTex->CreateUAV(uavDesc);
	}

	// Screen-space GI output (R11G11B10_FLOAT)
	{
		auto renderer = globals::game::renderer;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC mainDesc;
		mainTex.texture->GetDesc(&mainDesc);

		D3D11_TEXTURE2D_DESC desc = {
			.Width = mainDesc.Width,
			.Height = mainDesc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R11G11B10_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		};
		giOutput = eastl::make_unique<Texture2D>(desc, "SDFGI::GIOutput");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R11G11B10_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		giOutput->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R11G11B10_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		giOutput->CreateUAV(uavDesc);
	}
}

void SDFGI::SetupResources()
{
	logger::debug("SDFGI: Creating constant buffers...");
	{
		paramsCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SDFGIParams>(), "SDFGI::ParamsCB");

		CascadeData cascadeData[MAX_CASCADES] = {};
		cascadesCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc(sizeof(cascadeData)), "SDFGI::CascadesCB");

		sampleCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SDFGISampleCB>(), "SDFGI::SampleCB");
	}

	logger::debug("SDFGI: Creating cascade resources...");
	{
		cascades.resize(settings.NumCascades);
		float cellSize = settings.MinCellSize;
		for (uint i = 0; i < settings.NumCascades; ++i) {
			cascades[i].cellSize = cellSize;
			cascades[i].allDirty = true;
			CreateCascadeResources(i);
			cellSize *= 2.0f;
		}
	}

	logger::debug("SDFGI: Creating shared resources...");
	CreateSharedResources();

	logger::debug("SDFGI: Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "SDFGI::LinearClampSampler");
	}

	CompileComputeShaders();
}

void SDFGI::ClearShaderCache()
{
	for (auto& cs : preprocessCS)
		cs = nullptr;
	directLightStaticCS = nullptr;
	directLightDynamicCS = nullptr;
	integrateProcessCS = nullptr;
	integrateStoreCS = nullptr;
	integrateScrollCS = nullptr;
	integrateScrollStoreCS = nullptr;
	sampleCS = nullptr;
	debugCS = nullptr;

	CompileComputeShaders();
}

void SDFGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	auto cascadesStr = std::to_string(settings.NumCascades);
	auto probeAxisStr = std::to_string(PROBE_AXIS_SIZE);
	auto cascadeSizeStr = std::to_string(CASCADE_SIZE);
	auto shSizeStr = std::to_string(SH_SIZE);
	auto octSizeStr = std::to_string(LIGHTPROBE_OCT_SIZE);

	std::vector<std::pair<const char*, const char*>> commonDefines = {
		{ "CASCADE_SIZE", cascadeSizeStr.c_str() },
		{ "PROBE_AXIS_SIZE", probeAxisStr.c_str() },
		{ "MAX_CASCADES", cascadesStr.c_str() },
		{ "SH_SIZE", shSizeStr.c_str() },
		{ "LIGHTPROBE_OCT_SIZE", octSizeStr.c_str() },
	};

	auto addCommon = [&](std::vector<std::pair<const char*, const char*>>& defs) {
		for (auto& d : commonDefines)
			defs.push_back(d);
	};

	// Preprocess shaders
	static const char* ppModeNames[] = {
		"MODE_JFA_INIT_HALF",
		"MODE_JFA_PASS",
		"MODE_JFA_OPTIMIZED",
		"MODE_JFA_UPSCALE",
		"MODE_OCCLUSION",
		"MODE_STORE",
		"MODE_SCROLL",
		"MODE_SCROLL_OCCLUSION",
	};

	std::vector<ShaderCompileInfo> shaderInfos;

	for (int i = 0; i < PP_COUNT; ++i) {
		auto defines = std::vector<std::pair<const char*, const char*>>{ { ppModeNames[i], "" } };
		addCommon(defines);
		shaderInfos.push_back({ &preprocessCS[i], "preprocess.cs.hlsl", defines });
	}

	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_STATIC", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &directLightStaticCS, "directLight.cs.hlsl", defs });
	}
	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_DYNAMIC", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &directLightDynamicCS, "directLight.cs.hlsl", defs });
	}

	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_PROCESS", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &integrateProcessCS, "integrate.cs.hlsl", defs });
	}
	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_STORE", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &integrateStoreCS, "integrate.cs.hlsl", defs });
	}
	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_SCROLL", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &integrateScrollCS, "integrate.cs.hlsl", defs });
	}
	{
		auto defs = std::vector<std::pair<const char*, const char*>>{ { "MODE_SCROLL_STORE", "" } };
		addCommon(defs);
		shaderInfos.push_back({ &integrateScrollStoreCS, "integrate.cs.hlsl", defs });
	}

	{
		auto defs = std::vector<std::pair<const char*, const char*>>{};
		addCommon(defs);
		shaderInfos.push_back({ &sampleCS, "sample.cs.hlsl", defs });
	}
	{
		auto defs = std::vector<std::pair<const char*, const char*>>{};
		addCommon(defs);
		shaderInfos.push_back({ &debugCS, "debug.cs.hlsl", defs });
	}

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\SDFGI") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

bool SDFGI::ShadersOK()
{
	if (!sampleCS)
		return false;
	for (auto& cs : preprocessCS)
		if (!cs)
			return false;
	return directLightStaticCS && directLightDynamicCS && integrateProcessCS && integrateStoreCS;
}

ID3D11ShaderResourceView* SDFGI::GetOutputSRV()
{
	return (loaded && settings.Enabled && giOutput) ? giOutput->srv.get() : nullptr;
}

void SDFGI::StubVoxelizeRegion(uint cascade)
{
	auto context = globals::d3d::context;

	// Stub: clear voxelization textures to produce a test scene
	// Generate a procedural sphere at the cascade center
	FLOAT zeros[4] = { 0, 0, 0, 0 };
	UINT uzeros[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(renderAlbedo->uav.get(), uzeros);
	context->ClearUnorderedAccessViewUint(renderEmission->uav.get(), uzeros);
	context->ClearUnorderedAccessViewUint(renderGeomFacing->uav.get(), uzeros);

	// The preprocess shaders will handle the rest using the voxel data.
	// For now, we leave the volumes empty - the JFA will produce an empty SDF,
	// and no solid cells will be generated. This is correct for a stub:
	// the pipeline runs end-to-end but produces no GI output.
	(void)cascade;
}

void SDFGI::StubGatherLights(std::vector<SDFGILight>& lights)
{
	lights.clear();

	auto shaderManager = globals::game::smState;
	if (!shaderManager)
		return;

	auto shadowSceneNode = shaderManager->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (!dirLight)
		return;

	const auto& direction = dirLight->GetWorldDirection();
	auto& lightData = dirLight->GetLightRuntimeData();

	SDFGILight sunLight = {};
	sunLight.type = 0;
	sunLight.direction[0] = -direction.x;
	sunLight.direction[1] = -direction.y;
	sunLight.direction[2] = -direction.z;
	sunLight.color[0] = lightData.diffuse.red;
	sunLight.color[1] = lightData.diffuse.green;
	sunLight.color[2] = lightData.diffuse.blue;
	sunLight.energy = lightData.fade;
	sunLight.hasShadow = 1;

	lights.push_back(sunLight);
}

void SDFGI::UpdateCascades()
{
	auto camPos = globals::game::frameBufferCached.GetCameraPosAdjust();
	float3 cameraPos = { camPos.x, camPos.y, camPos.z };

	for (uint i = 0; i < settings.NumCascades; ++i) {
		auto& c = cascades[i];
		float cs = c.cellSize;

		int newPos[3] = {
			(int)std::floor(cameraPos.x / cs),
			(int)std::floor(cameraPos.y / cs),
			(int)std::floor(cameraPos.z / cs),
		};

		if (c.allDirty) {
			c.position[0] = newPos[0];
			c.position[1] = newPos[1];
			c.position[2] = newPos[2];
			c.dirtyRegions[0] = CASCADE_SIZE;
			c.dirtyRegions[1] = CASCADE_SIZE;
			c.dirtyRegions[2] = CASCADE_SIZE;
		} else {
			int scroll[3] = {
				newPos[0] - c.position[0],
				newPos[1] - c.position[1],
				newPos[2] - c.position[2],
			};

			bool needsScroll = scroll[0] != 0 || scroll[1] != 0 || scroll[2] != 0;
			if (needsScroll) {
				c.dirtyRegions[0] = std::abs(scroll[0]);
				c.dirtyRegions[1] = std::abs(scroll[1]);
				c.dirtyRegions[2] = std::abs(scroll[2]);
				c.position[0] = newPos[0];
				c.position[1] = newPos[1];
				c.position[2] = newPos[2];
			} else {
				c.dirtyRegions[0] = 0;
				c.dirtyRegions[1] = 0;
				c.dirtyRegions[2] = 0;
			}
		}
	}

	// Update cascade UBO
	CascadeData cascadeData[MAX_CASCADES] = {};
	for (uint i = 0; i < settings.NumCascades; ++i) {
		auto& c = cascades[i];
		cascadeData[i].Offset[0] = c.position[0] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		cascadeData[i].Offset[1] = c.position[1] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		cascadeData[i].Offset[2] = c.position[2] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		cascadeData[i].ToCell = 1.0f / c.cellSize;
		cascadeData[i].ProbeWorldOffset[0] = c.position[0] - CASCADE_SIZE / 2;
		cascadeData[i].ProbeWorldOffset[1] = c.position[1] - CASCADE_SIZE / 2;
		cascadeData[i].ProbeWorldOffset[2] = c.position[2] - CASCADE_SIZE / 2;
	}
	cascadesCB->Update(cascadeData, sizeof(cascadeData));

	lastCameraPos = cameraPos;
}

void SDFGI::UpdateParams(uint cascade, int stepSize)
{
	SDFGIParams params = {};
	params.GridSize[0] = params.GridSize[1] = params.GridSize[2] = (float)CASCADE_SIZE;
	params.MaxCascades = settings.NumCascades;
	params.Cascade = cascade;
	params.LightCount = currentLightCount;
	params.ProbeAxisSize = PROBE_AXIS_SIZE;
	params.BounceFeedback = settings.BounceFeedback;
	params.YMult = settings.YScale;
	params.UseOcclusion = settings.UseOcclusion ? 1 : 0;
	params.StepSize = stepSize;
	params.HalfSize = 0;
	params.HistorySize = settings.FramesToConverge;
	params.HistoryIndex = frameCount % settings.FramesToConverge;
	params.RayCount = settings.RayCount;
	params.RayBias = 1.1f;
	params.SkyEnergy = 1.0f;
	params.SkyColor[0] = 0.5f;
	params.SkyColor[1] = 0.6f;
	params.SkyColor[2] = 0.8f;

	auto& c = cascades[cascade];
	params.Scroll[0] = c.dirtyRegions[0];
	params.Scroll[1] = c.dirtyRegions[1];
	params.Scroll[2] = c.dirtyRegions[2];
	params.ProbeOffset[0] = c.position[0];
	params.ProbeOffset[1] = c.position[1];
	params.ProbeOffset[2] = c.position[2];

	params.ProcessOffset = frameCount % settings.FramesToUpdateLight;
	params.ProcessIncrement = settings.FramesToUpdateLight;

	paramsCB->Update(params);
}

void SDFGI::DispatchPreprocess(uint cascade)
{
	auto context = globals::d3d::context;
	auto& c = cascades[cascade];

	UpdateParams(cascade);

	// Clear dispatch buffer
	UINT zeros[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(c.dispatchUAV.get(), zeros);

	uint halfSize = CASCADE_SIZE / 2;

	// JFA Init Half
	{
		ID3D11ShaderResourceView* srvs[] = { renderAlbedo->srv.get() };
		ID3D11UnorderedAccessView* uavs[] = { renderSdfHalf[0]->uav.get() };
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(preprocessCS[PP_JFA_INIT_HALF].get(), nullptr, 0);
		context->Dispatch(halfSize / 4, halfSize / 4, halfSize / 4);
	}

	// JFA passes (half-res) - ping pong
	{
		int src = 0;
		int step = (int)halfSize / 2;
		while (step >= 1) {
			int dst = 1 - src;

			SDFGIParams stepParams = {};
			stepParams.GridSize[0] = stepParams.GridSize[1] = stepParams.GridSize[2] = (float)CASCADE_SIZE;
			stepParams.MaxCascades = settings.NumCascades;
			stepParams.Cascade = cascade;
			stepParams.StepSize = step;
			stepParams.HalfSize = 1;
			paramsCB->Update(stepParams);

			ID3D11ShaderResourceView* nullSRV = nullptr;
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 1, &nullSRV);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

			ID3D11ShaderResourceView* srvs[] = { renderSdfHalf[src]->srv.get() };
			ID3D11UnorderedAccessView* uavs[] = { renderSdfHalf[dst]->uav.get() };
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

			auto shader = (step >= 8) ? preprocessCS[PP_JFA_PASS].get() : preprocessCS[PP_JFA_OPTIMIZED].get();
			context->CSSetShader(shader, nullptr, 0);

			if (step >= 8)
				context->Dispatch(halfSize / 4, halfSize / 4, halfSize / 4);
			else
				context->Dispatch(halfSize / 8, halfSize / 8, halfSize / 8);

			src = dst;
			step /= 2;
		}

		// Upscale to full resolution
		{
			ID3D11ShaderResourceView* nullSRV = nullptr;
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 1, &nullSRV);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

			ID3D11ShaderResourceView* srvs[] = { renderSdfHalf[src]->srv.get(), renderAlbedo->srv.get() };
			ID3D11UnorderedAccessView* uavs[] = { renderSdf[0]->uav.get() };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(preprocessCS[PP_JFA_UPSCALE].get(), nullptr, 0);
			context->Dispatch(CASCADE_SIZE / 4, CASCADE_SIZE / 4, CASCADE_SIZE / 4);
		}

		// Final full-res JFA pass
		{
			ID3D11ShaderResourceView* nullSRVs[2] = {};
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 2, nullSRVs);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

			SDFGIParams finalParams = {};
			finalParams.GridSize[0] = finalParams.GridSize[1] = finalParams.GridSize[2] = (float)CASCADE_SIZE;
			finalParams.MaxCascades = settings.NumCascades;
			finalParams.Cascade = cascade;
			finalParams.StepSize = 1;
			finalParams.HalfSize = 0;
			paramsCB->Update(finalParams);

			ID3D11ShaderResourceView* srvs[] = { renderSdf[0]->srv.get() };
			ID3D11UnorderedAccessView* uavs[] = { renderSdf[1]->uav.get() };
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(preprocessCS[PP_JFA_OPTIMIZED].get(), nullptr, 0);
			context->Dispatch(CASCADE_SIZE / 8, CASCADE_SIZE / 8, CASCADE_SIZE / 8);
		}
	}

	// Occlusion
	if (settings.UseOcclusion) {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		ID3D11ShaderResourceView* srvs[] = { renderAlbedo->srv.get(), renderGeomFacing->srv.get() };
		ID3D11UnorderedAccessView* uavs[] = { occlusionTex->uav.get() };
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(preprocessCS[PP_OCCLUSION].get(), nullptr, 0);
		context->Dispatch(CASCADE_SIZE / 4, CASCADE_SIZE / 4, CASCADE_SIZE / 4);
	}

	// Store (compacts solid cells, writes SDF)
	{
		ID3D11ShaderResourceView* nullSRVs[2] = {};
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 2, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		UpdateParams(cascade);

		ID3D11ShaderResourceView* srvs[] = {
			renderSdf[1]->srv.get(),
			renderAlbedo->srv.get(),
			renderEmission->srv.get(),
			renderGeomFacing->srv.get()
		};
		ID3D11UnorderedAccessView* uavs[] = {
			c.sdfTex->uav.get(),
			c.solidCellUAV.get(),
			c.dispatchUAV.get()
		};
		context->CSSetShaderResources(0, 4, srvs);
		context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
		context->CSSetShader(preprocessCS[PP_STORE].get(), nullptr, 0);
		context->Dispatch(CASCADE_SIZE / 4, CASCADE_SIZE / 4, CASCADE_SIZE / 4);
	}

	// Clean up
	{
		ID3D11ShaderResourceView* nullSRVs[4] = {};
		ID3D11UnorderedAccessView* nullUAVs[3] = {};
		context->CSSetShaderResources(0, 4, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
	}
}

void SDFGI::DispatchDirectLight(uint cascade)
{
	auto context = globals::d3d::context;
	auto& c = cascades[cascade];

	UpdateParams(cascade);

	// Bind all cascade SDF textures as SRVs
	std::array<ID3D11ShaderResourceView*, 13> srvs = {};
	for (uint i = 0; i < settings.NumCascades && i < MAX_CASCADES; ++i)
		srvs[i] = cascades[i].sdfTex->srv.get();
	srvs[8] = c.solidCellSRV.get();
	srvs[9] = c.dispatchSRV.get();
	srvs[10] = lightprobeData ? lightprobeData->srv.get() : nullptr;
	srvs[11] = occlusionTex ? occlusionTex->srv.get() : nullptr;
	srvs[12] = lightBuffer ? lightBuffer->SRV() : nullptr;

	ID3D11UnorderedAccessView* uavs[] = {
		c.lightTex->uav.get(),
		c.lightAniso0Tex->uav.get(),
		c.lightAniso1Tex->uav.get()
	};

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	// Use indirect dispatch based on solid cell count
	context->CSSetShader(directLightStaticCS.get(), nullptr, 0);
	context->DispatchIndirect(c.dispatchBuffer->resource.get(), 0);

	// Clean up
	srvs.fill(nullptr);
	ID3D11UnorderedAccessView* nullUAVs[3] = {};
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
}

void SDFGI::DispatchIntegrate(uint cascade)
{
	auto context = globals::d3d::context;
	auto& c = cascades[cascade];

	UpdateParams(cascade);

	// Bind cascade SDF + light textures
	std::array<ID3D11ShaderResourceView*, 16> srvs = {};
	for (uint i = 0; i < settings.NumCascades && i < MAX_CASCADES; ++i) {
		srvs[i] = cascades[i].sdfTex->srv.get();
		srvs[8 + i] = cascades[i].lightTex->srv.get();
	}

	ID3D11UnorderedAccessView* uavs[] = {
		c.probeHistoryTex->uav.get(),
		c.probeAverageTex->uav.get(),
		lightprobeData->uav.get()
	};

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	// Process mode: ray-march and accumulate SH
	context->CSSetShader(integrateProcessCS.get(), nullptr, 0);
	uint probeGroups = (PROBE_AXIS_SIZE * PROBE_AXIS_SIZE * PROBE_AXIS_SIZE + 63) / 64;
	context->Dispatch(probeGroups, 1, 1);

	// Store mode: convert SH to octahedral
	context->CSSetShader(integrateStoreCS.get(), nullptr, 0);
	uint octGroups = (PROBE_AXIS_SIZE * PROBE_AXIS_SIZE * LIGHTPROBE_OCT_SIZE + 7) / 8;
	context->Dispatch(octGroups, (PROBE_AXIS_SIZE * LIGHTPROBE_OCT_SIZE + 7) / 8, 1);

	// Clean up
	srvs.fill(nullptr);
	ID3D11UnorderedAccessView* nullUAVs[3] = {};
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
}

void SDFGI::DispatchSample()
{
	auto context = globals::d3d::context;

	// Update sample constant buffer
	SDFGISampleCB sampleData = {};
	sampleData.GridSize[0] = sampleData.GridSize[1] = sampleData.GridSize[2] = (float)CASCADE_SIZE;
	sampleData.MaxCascades = settings.NumCascades;
	sampleData.UseOcclusion = settings.UseOcclusion ? 1 : 0;
	sampleData.ProbeAxisSize = PROBE_AXIS_SIZE;
	sampleData.ProbeToUVW = 1.0f / (float)(PROBE_AXIS_SIZE * (LIGHTPROBE_OCT_SIZE + 2));
	sampleData.NormalBias = settings.NormalBias;
	sampleData.Energy = settings.Energy;
	sampleData.YMult = settings.YScale;

	float probeTexSize = (float)(PROBE_AXIS_SIZE * (LIGHTPROBE_OCT_SIZE + 2));
	sampleData.LightprobeTexPixelSize[0] = 1.0f / probeTexSize;
	sampleData.LightprobeTexPixelSize[1] = 1.0f / probeTexSize;
	sampleData.LightprobeTexPixelSize[2] = 0;
	sampleData.LightprobeUVOffset[0] = 1.0f / probeTexSize;
	sampleData.LightprobeUVOffset[1] = 1.0f / probeTexSize;
	sampleData.LightprobeUVOffset[2] = 0;

	for (uint i = 0; i < settings.NumCascades; ++i) {
		auto& c = cascades[i];
		sampleData.Cascades[i].Offset[0] = c.position[0] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		sampleData.Cascades[i].Offset[1] = c.position[1] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		sampleData.Cascades[i].Offset[2] = c.position[2] * c.cellSize - (CASCADE_SIZE / 2) * c.cellSize;
		sampleData.Cascades[i].ToCell = 1.0f / c.cellSize;
	}

	sampleCB->Update(sampleData);

	// Bind resources
	auto cb = sampleCB->CB();
	context->CSSetConstantBuffers(2, 1, &cb);

	auto renderer = globals::game::renderer;
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];

	ID3D11ShaderResourceView* srvs[] = {
		Util::GetCurrentSceneDepthSRV(),
		normalRoughness.SRV,
		lightprobeData->srv.get(),
		occlusionTex ? occlusionTex->srv.get() : nullptr,
	};

	ID3D11UnorderedAccessView* uavs[] = { giOutput->uav.get() };

	context->CSSetShaderResources(0, 4, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	{
		auto sampler = linearClampSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
	}

	context->CSSetShader(sampleCS.get(), nullptr, 0);

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	// Clean up
	ID3D11ShaderResourceView* nullSRVs[4] = {};
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 4, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void SDFGI::DrawSDFGI()
{
	if (!(settings.Enabled && ShadersOK()))
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SDFGI");

	auto context = globals::d3d::context;

	if (recompileFlag)
		ClearShaderCache();

	// Bind shared constant buffers
	auto paramsBuf = paramsCB->CB();
	auto cascadesBuf = cascadesCB->CB();
	context->CSSetConstantBuffers(0, 1, &paramsBuf);
	context->CSSetConstantBuffers(1, 1, &cascadesBuf);
	{
		auto sampler = linearClampSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
	}

	// Phase 1: Update cascade positions
	UpdateCascades();

	// Gather and upload lights
	{
		std::vector<SDFGILight> lights;
		StubGatherLights(lights);
		currentLightCount = (uint)std::min(lights.size(), (size_t)MAX_LIGHTS);
		if (currentLightCount > 0)
			lightBuffer->Update(lights.data(), currentLightCount * sizeof(SDFGILight));
	}

	// Phase 2-5: Per cascade processing
	for (uint i = 0; i < settings.NumCascades; ++i) {
		auto& c = cascades[i];
		bool isDirty = c.allDirty || c.dirtyRegions[0] > 0 || c.dirtyRegions[1] > 0 || c.dirtyRegions[2] > 0;

		if (isDirty) {
			StubVoxelizeRegion(i);
			DispatchPreprocess(i);
			DispatchDirectLight(i);
		}

		DispatchIntegrate(i);
		c.allDirty = false;
	}

	// Phase 6: Screen-space sampling
	DispatchSample();

	// Clean up shared CB bindings
	ID3D11Buffer* nullBufs[2] = {};
	context->CSSetConstantBuffers(0, 2, nullBufs);

	frameCount++;
}

#include "Raytracing.h"

#include "Globals.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	Enabled)

////////////////////////////////////////////////////////////////////////////////////

void Raytracing::RestoreDefaultSettings()
{
	settings = {};
}

void Raytracing::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Raytracing::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Raytracing::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
}

void Raytracing::CreateD3D12Device(ID3D11Device* device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter)
{
	Hooks::InstallD3D11Hooks(device);

	// Set Device
	DX::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(immediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create device
	DX::ThrowIfFailed(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device)));

	// Check hardware raytracing tier
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
		if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
			logger::info("[Raytracing] Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
		else
			logger::warn("[Raytracing] Hardware ray tracing not supported.");
	}

	// Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	DX::ThrowIfFailed(commandQueue->SetName(L"Command Queue"));

	DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	DX::ThrowIfFailed(commandAllocator->SetName(L"Command Allocator"));

	DX::ThrowIfFailed(d3d12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));
	DX::ThrowIfFailed(commandList->SetName(L"Command List"));

	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	// Create Interop
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	InitializeCERaytracing(d3d12Device.get(), commandQueue.get());
}

void Raytracing::Load()
{
	creationEngineRaytracing = eastl::make_unique<CreationEngineRaytracing>();
}

void Raytracing::PostPostLoad()
{
	Hooks::Install();

	RE::GetINISetting("bReflectLODLand:Water")->data.b = false;
	RE::GetINISetting("bReflectLODObjects:Water")->data.b = false;
	RE::GetINISetting("bReflectLODTrees:Water")->data.b = false;
	RE::GetINISetting("bReflectSky:Water")->data.b = true;
}

void Raytracing::InitializeCERaytracing(ID3D12Device5* device, ID3D12CommandQueue* commandQueue2)
{
	if (initialized)
		return;

	bool result = creationEngineRaytracing->Initialize(device, commandQueue2);

	if (!result) {
		settings.Enabled = false;
		forcedDisabled = true;

		logger::error("[Raytracing] Failed to initialize Creation Engine ray tracing.");
	} else {
		initialized = true;
		logger::info("[Raytracing] Successfully initialized Creation Engine ray tracing.");
	}	
}

void Raytracing::Main_RenderPlayerView_Before() const
{
	if (!settings.Enabled)
		return;

	creationEngineRaytracing->SetScreenSize(static_cast<uint32_t>(globals::state->screenSize.x), static_cast<uint32_t>(globals::state->screenSize.y));

	auto eye = Util::GetCameraData(0);
	float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

	creationEngineRaytracing->UpdateFrameBuffer(
		globals::game::frameBufferCached.GetCameraViewInverse().Transpose(),
		globals::game::frameBufferCached.GetCameraProjInverse().Transpose(),
		Util::GetCameraData(),
		float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y),
		float3(globals::game::frameBufferCached.GetCameraPosAdjust()));

	creationEngineRaytracing->Execute();
}

void Raytracing::DeferredPasses() const
{
	if (!settings.Enabled)
		return;

	creationEngineRaytracing->WaitExecution();
}

void Raytracing::SetupResources()
{
	creationEngineRaytracing->SetupResources();

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		cheeseCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CbData>());
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			             D3D11_BIND_UNORDERED_ACCESS |
			             D3D11_BIND_RENDER_TARGET,
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

		// When you want to align with the main texture format
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		srvDesc.Format = uavDesc.Format = texDesc.Format;

		{
			cheeseTex = eastl::make_unique<Texture2D>(texDesc);
			cheeseTex->CreateSRV(srvDesc);
			cheeseTex->CreateUAV(uavDesc);
		}
	}

	logger::debug("Creating samplers...");
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, cheeseSampler.put()));
	}

	CompileShaders();
}

void Raytracing::ClearShaderCache()
{
	cheeseCs = nullptr;  // This is actually optional
	CompileShaders();
}

void Raytracing::CompileShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\nonexistent.cs.hlsl", { { "SOME_MACRO", "0" } }, "cs_5_0")); rawPtr)
		cheeseCs.attach(rawPtr);
}
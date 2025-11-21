#include "RaytracedGI.h"
#include "InverseSquareLighting.h"

#include "Globals.h"
#include "State.h"
#include "RaytracedGI/ShaderUtils.h"
#include "ShaderCache.h"

#include <filesystem>
#include <shlobj.h>
#include <windows.h>
#include "Deferred.h"

#ifdef DLSS_RR
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RaytracedGI::Settings,
	Enabled,
	Denoiser,
	Bounces,
	SamplesPerPixel,
	Roughness,
	Metalness,
	Diffuse,
	Specular,
	Emissive,
	Directional,
	Point,
	PointFade,
	GammaToLinear,
	DLSSRRQualityMode,
	DebugOutput,
	EnablePIXCapture,
	EnableDebugDevice)
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RaytracedGI::Settings,
	Enabled,
	Denoiser,
	Bounces,
	SamplesPerPixel,
	Roughness,
	Metalness,
	Diffuse,
	Specular,
	Emissive,
	Directional,
	Point,
	PointFade,
	GammaToLinear,
	DebugOutput,
	EnablePIXCapture,
	EnableDebugDevice)
#endif


////////////////////////////////////////////////////////////////////////////////////

void RaytracedGI::RestoreDefaultSettings()
{
	settings = {};
}

void RaytracedGI::LoadSettings(json& o_json)
{
	settings = o_json;
}

void RaytracedGI::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DrawFloat2(const char* label, float2& v, float min = 0.0f, float max = 1.0f)
{
	float floats[2] = { v.x, v.y };
	if (ImGui::SliderFloat2(label, floats, min, max)) {
		v = { floats[0], floats[1] };
		v.Clamp({ min, min }, { max, max });
	}
}

void RaytracedGI::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Ray-Traced Global Illumination.");
	}

	// Denoiser
	{
		int denoiser = static_cast<int32_t>(settings.Denoiser);
		ImGui::TextUnformatted("Denoiser");

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(25, 0));

		for (auto& [value, name] : magic_enum::enum_entries<Denoiser>()) {
			ImGui::SameLine();
			ImGui::RadioButton(name.data(), &denoiser, static_cast<int32_t>(value));
		}

		settings.Denoiser = static_cast<Denoiser>(denoiser);
	}

	if (ImGui::SliderInt("Bounces", &settings.Bounces, 1, 32))
		settings.Bounces = std::clamp(settings.Bounces, 1, 32);

	if (ImGui::SliderInt("Samples Per Pixel", &settings.SamplesPerPixel, 1, 32))
		settings.SamplesPerPixel = std::clamp(settings.SamplesPerPixel, 1, 32);

	/*if (ImGui::SliderInt("Bounces", &settings.Bounces, 1, 32))
		settings.Bounces = std::clamp(settings.Bounces, 1, 32);*/

	DrawFloat2("Roughness", settings.Roughness);
	DrawFloat2("Metalness", settings.Metalness);

	if (ImGui::DragFloat("Diffuse Strength", &settings.Diffuse, 0.001f))
		settings.Diffuse = std::max(0.0f, settings.Diffuse);

	if (ImGui::DragFloat("Specular Strength", &settings.Specular, 0.001f))
		settings.Specular = std::max(0.0f, settings.Specular);

	if (ImGui::DragFloat("Emissive Strength", &settings.Emissive, 0.001f))
		settings.Emissive = std::max(0.0f, settings.Emissive);

	if (ImGui::DragFloat("Effect Strength", &settings.Effect, 0.001f))
		settings.Effect = std::max(0.0f, settings.Effect);

	if (ImGui::DragFloat("Sky Strength", &settings.Sky, 0.001f))
		settings.Sky = std::max(0.0f, settings.Sky);

	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNodeEx("Direct Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			if(ImGui::DragFloat("Directional Strength", &settings.Directional, 0.001f))
				settings.Directional = std::max(0.0f, settings.Directional);

			if (ImGui::DragFloat("Point Strength", &settings.Point, 0.001f))
				settings.Point = std::max(0.0f, settings.Point);

			ImGui::Checkbox("Point Fade", &settings.PointFade);
			ImGui::Checkbox("Gamma To Linear", &settings.GammaToLinear);

			ImGui::Checkbox("Raytraced Shadows", &settings.RaytracedShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Replaces directional light shadowmaps.\n");
			}
		}
	}
	/*if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Number of clipmaps to use\n");
	}*/

	// Debug display mode
	if (ImGui::BeginCombo("Debug Output", magic_enum::enum_name(settings.DebugOutput).data())) {
		for (auto& value : magic_enum::enum_values<DebugOutput>())
		{
			bool isSelected = (settings.DebugOutput == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				settings.DebugOutput = value;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
	}

#ifdef SHARC
	ImGui::DragFloat("SHARC Scale", &settings.SHARCScale, 1.0f, 0.1f, 100.0f);
	settings.SHARCScale = std::clamp(settings.SHARCScale, 0.1f, 100.0f);
#endif

#ifdef DLSS_RR
	if (ImGui::BeginCombo("DLSS RR Quality Mode", magic_enum::enum_name(settings.DLSSRRQualityMode).data())) {
		for (auto& value : magic_enum::enum_values<DLSSRRQuality>()) {
			bool isSelected = (settings.DLSSRRQualityMode == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				settings.DLSSRRQualityMode = value;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
	}
#endif

	ImGui::Checkbox("Enabled PIX Capture", &settings.EnablePIXCapture);
	ImGui::Checkbox("Enabled Debug Device", &settings.EnableDebugDevice);

	if (settings.EnablePIXCapture)
	{
		if (ImGui::Button("Create PIX Capture")) {
			pixCapture = true;
			pixCaptureStarted = false;
		}
	}

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Meshes (vertex, index and BLAS buffers): {}", meshes.size()).c_str());
		ImGui::Text(std::format("Shared Textures: {}", sharedTextures.size()).c_str());

		ImGui::Text(std::format("Instances: {}, Unculled: {}, Culled: {}", instances.size(), instanceData.size(), instances.size() - instanceData.size()).c_str());

		ImGui::Text(std::format("Lights: {}", lights.size()).c_str());

		if (settings.RaytracedShadows) {
			ImGui::Text(std::format("Unculled Shadow Instances: {}", blasShadowInstances.size()).c_str());
		}
		ImGui::TreePop();
	}

	ImGui::Image(shadowMaskTexture->srv, { 960.f, 540.f });

	//ImGui::Image(skyHemisphere->srv, { 64.0f, 64.0f });

	/*
	D3D11_TEXTURE2D_DESC desc;
	mainTexture->resource11->GetDesc(&desc);
	ImGui::Image(mainTexture->srv, { desc.Width * 0.5f, desc.Height * 0.5f });*/
}

void RaytracedGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	commonHeap = eastl::make_unique<DX12::DescriptorHeap<HeapSlot::Slot, HeapType::Type>>(
		d3d12Device.get(), 
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, HeapSlot::NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	/*computeHeap = eastl::make_unique<DX12::DescriptorHeap<ComputeHeapSlot::Slot, ComputeHeapType::Type>>(
		d3d12Device.get(),
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ComputeHeapSlot::NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));*/

	computeHeapShadows = eastl::make_unique<DX12::DescriptorHeap<ComputeHeapShadowsSlot::Slot, ComputeHeapShadowsType::Type>>(
		d3d12Device.get(),
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ComputeHeapShadowsSlot::NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);

	// Depth
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		depthTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(depthTexture->resource->SetName(L"Depth texture"));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, commonHeap->CPUHandle(HeapSlot::Depth));
		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, computeHeapShadows->CPUHandle(ComputeHeapShadowsSlot::Depth));	
	}

	// Shadow mask
	{
		auto shadowMask = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];
		D3D11_TEXTURE2D_DESC shadowMaskDesc;
		shadowMask.texture->GetDesc(&shadowMaskDesc);

		logger::info("[RT] Shadowmask Format: {}", magic_enum::enum_name(shadowMaskDesc.Format));

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = shadowMaskDesc.Format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		shadowMaskTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(shadowMaskTexture->resource->SetName(L"Shadow Mask"));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = texDesc.Format;

		d3d12Device->CreateUnorderedAccessView(shadowMaskTexture->resource.get(), nullptr, &uavDesc, computeHeapShadows->CPUHandle(ComputeHeapShadowsSlot::ShadowMask));	
	}

	// UAVs
	{	
		// u0 - Final texture
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			mainTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(mainTexture->resource->SetName(L"Main Texture"));

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Format = texDesc.Format;

			d3d12Device->CreateUnorderedAccessView(mainTexture->resource.get(), nullptr, &uavDesc, commonHeap->CPUHandle(HeapSlot::Main));
		}

		// Motion vector
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			motionVectorsTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(motionVectorsTexture->resource->SetName(L"Motion Vectors Texture"));
		}

		// u1 - Output texture
		{
			outputTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			outputTexture->SetName(L"Output texture");

			outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			outputTexture->CreateUAV(commonHeap->CPUHandle(HeapSlot::Output));
			//outputTexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::DiffuseGI));
		}

		// u2 - Reflectance texture
		{				
			reflectanceTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			reflectanceTexture->SetName(L"Reflectance Texture");

			reflectanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			reflectanceTexture->CreateUAV(commonHeap->CPUHandle(HeapSlot::Reflectance));
			//reflectanceTexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::SpecularGI));	
		}

		// u3 - Specular Hit Distance texture
		{
			specularHitDistanceTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			specularHitDistanceTexture->SetName(L"Specular Hit Distance Texture");

			reflectanceTexture->CreateUAV(commonHeap->CPUHandle(HeapSlot::SpecularHitDist));
			//specularHitDistanceTexture->CreateUAV(computeHeap->CPUHandle(ComputeHeapSlot::SpecHitDist));
		}

		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

			normalRoughnessTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(normalRoughnessTexture->resource->SetName(L"Normal Roughness Texture"));

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

			d3d12Device->CreateShaderResourceView(normalRoughnessTexture->resource.get(), &srvDesc, commonHeap->CPUHandle(HeapSlot::NormalRoughness));
		}
	}

	// t3 - Light buffer
	{
		lightBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Light>>(d3d12Device.get(), MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->resource->SetName(L"Light Buffer"));

		lightBuffer->CreateSRV(commonHeap->CPUHandle(HeapSlot::Lights));
	}

	// t4 - Instance buffer
	{
		instanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Instance>>(d3d12Device.get(), MAX_INSTANCES);
		DX::ThrowIfFailed(instanceBuffer->resource->SetName(L"Instance Buffer"));

		instanceBuffer->CreateSRV(commonHeap->CPUHandle(HeapSlot::Instances));
	}

	// Create instance buffer for BLAS
	{
		blasInstanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>>(d3d12Device.get(), MAX_INSTANCES);
		DX::ThrowIfFailed(blasInstanceBuffer->resource->SetName(L"BLAS Instance Buffer"));
	}

	// Create shadow instance buffer for BLAS
	{
		blasShadowInstanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>>(d3d12Device.get(), MAX_INSTANCES);
		DX::ThrowIfFailed(blasShadowInstanceBuffer->resource->SetName(L"BLAS Instance Buffer"));
	}

	logger::debug("Creating constant buffer...");
	{
		frameBuffer = eastl::make_unique<DX12::StructuredBufferUpload<FrameBuffer>>(d3d12Device.get(), 1);
		frameBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(frameBuffer->resource->SetName(L"Frame Buffer"));

		shadowsCB = eastl::make_unique<DX12::StructuredBufferUpload<ShadowsCB>>(d3d12Device.get(), 1);
		shadowsCB->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(shadowsCB->resource->SetName(L"Shadows Constant Buffer"));
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, samplerState.put()));
	}

	// Sky Hemisphere
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = SKY_CUBEMAP_SIZE * 2;
		texDesc.Height = SKY_CUBEMAP_SIZE * 2;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		skyHemisphere = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(skyHemisphere->resource->SetName(L"Sky Hemisphere"));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		d3d12Device->CreateShaderResourceView(skyHemisphere->resource.get(), &srvDesc, commonHeap->CPUHandle(HeapSlot::SkyHemisphere));
	}

	// Sky cubemap
	{
		/*auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		D3D11_TEXTURE2D_DESC texDesc{};

		reflections.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.ArraySize = 1;
		
		for (UINT i = 0; i < 6; ++i) {
			srvDesc.Texture2DArray.FirstArraySlice = i;
			DX::ThrowIfFailed(device->CreateShaderResourceView(reflections.texture, &srvDesc, skyCubemapSRV[i].put()));
		}*/

		/*D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = SKY_CUBEMAP_SIZE;
		texDesc.Height = SKY_CUBEMAP_SIZE;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 6;
		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT; //DXGI_FORMAT_R16G16B16A16_FLOAT;  // DXGI_FORMAT_R11G11B10_FLOAT
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texDesc.CPUAccessFlags = 0;
		texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, skyCubemap.put()));*/

		/*winrt::com_ptr<IDXGIResource1> dxgiResource;
		DX::ThrowIfFailed(skyCubemap->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));

		// Share with DX12
		DX::ThrowIfFailed(d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(skyCubemapDX12.put())));
		CloseHandle(sharedHandle);*/

		// Create SRV (just to Debug)
		/*D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MipLevels = 1;

		DX::ThrowIfFailed(device->CreateShaderResourceView(skyCubemap.get(), &srvDesc, skyCubemapSRV[0].put()));*/

		/*D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.ArraySize = 1;
		
		for (UINT i = 0; i < 6; i++) {
			srvDesc.Texture2DArray.FirstArraySlice = i;
			DX::ThrowIfFailed(device->CreateShaderResourceView(skyCubemap.get(), &srvDesc, skyCubemapSRV[i].put()));
		}

		// Create RTV
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.Texture2DArray.MipSlice = 0;
		rtvDesc.Texture2DArray.ArraySize = 1;
		
		for (UINT i = 0; i < 6; i++) {
			rtvDesc.Texture2DArray.FirstArraySlice = i;
			DX::ThrowIfFailed(device->CreateRenderTargetView(skyCubemap.get(), &rtvDesc, skyCubemapRTV[i].put()));
		}

		// Create Depth
		D3D11_TEXTURE2D_DESC depthDesc = {};
		depthDesc.Width = SKY_CUBEMAP_SIZE;
		depthDesc.Height = SKY_CUBEMAP_SIZE;
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 6;
		depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;

		DX::ThrowIfFailed(device->CreateTexture2D(&depthDesc, nullptr, skyCubemapDepth.put()));

		// Create SRV		
		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
		depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;*/
		/*depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthSrvDesc.Texture2D.MostDetailedMip = 0;
		depthSrvDesc.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(skyCubemapDepth.get(), &depthSrvDesc, skyCubemapDepthSRV.put()));*/
		/*depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		depthSrvDesc.Texture2DArray.MipLevels = 1;
		depthSrvDesc.Texture2DArray.ArraySize = 1;

		for (UINT i = 0; i < 6; i++) {
			depthSrvDesc.Texture2DArray.FirstArraySlice = i;
			DX::ThrowIfFailed(device->CreateShaderResourceView(skyCubemapDepth.get(), &depthSrvDesc, skyCubemapDepthSRV[i].put()));
		}

		// Create DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;*/
		/*dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(device->CreateDepthStencilView(skyCubemapDepth.get(), &dsvDesc, skyCubemapDSV.put()));*/
		/*dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.MipSlice = 0;
		dsvDesc.Texture2DArray.ArraySize = 1;

		for (UINT i = 0; i < 6; i++) {
			dsvDesc.Texture2DArray.FirstArraySlice = i;
			DX::ThrowIfFailed(device->CreateDepthStencilView(skyCubemapDepth.get(), &dsvDesc, skyCubemapDSV[i].put()));
		}

		skyCubemapViewport.TopLeftX = 0.0f;
		skyCubemapViewport.TopLeftY = 0.0f;
		skyCubemapViewport.Width = SKY_CUBEMAP_SIZE * 1.0f;
		skyCubemapViewport.Height = SKY_CUBEMAP_SIZE * 1.0f;
		skyCubemapViewport.MinDepth = 0.0f;
		skyCubemapViewport.MaxDepth = 1.0f;

		skyPerGeometryCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SkyPerGeometry>());*/
	}

	logger::debug("Creating irradiance cache buffer...");
	{
		irradianceCacheBuffer = eastl::make_unique<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>>(d3d12Device.get(), MAX_IRRADIANCE_ENTRIES);
	}

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (fenceEvent == nullptr) {
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

#if defined(DLSS_RR)
	InitRR();
#endif

	CompileShaders();
}

#ifdef DLSS_RR
void RaytracedGI::InitRR()
{
	std::wstring interposerPath = L"Data\\Shaders\\Upscaling\\Streamline\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_RR };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D12;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;
	//sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		logger::info("[Streamline] Successfully initialized Streamline");
	}

	slSetD3DDevice((void*)d3d12Device.get());

	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetOptimalSettings", (void*&)slDLSSDGetOptimalSettings);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetState", (void*&)slDLSSDGetState);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", (void*&)slDLSSDSetOptions);
}

int32_t RaytracedGI::GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
float RaytracedGI::Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void RaytracedGI::GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

float2 RaytracedGI::GetInputResolutionScaleRR(uint32_t outputWidth, uint32_t outputHeight)
{
	sl::DLSSDOptions dlssdOptions{};
	dlssdOptions.mode = GetDLSSMode();
	dlssdOptions.outputWidth = outputWidth;
	dlssdOptions.outputHeight = outputHeight;

	logger::debug("[DLSS RR] Getting input resolution scale for output {}x{} and quality mode {}", outputWidth, outputHeight, magic_enum::enum_name(dlssdOptions.mode));

	sl::DLSSDOptimalSettings optimalSettings{};
	sl::Result result = slDLSSDGetOptimalSettings(dlssdOptions, optimalSettings);
	if (result != sl::Result::eOk) {
		logger::critical("[Streamline] Failed to get DLSS RR optimal settings, error code: {}", (int)result);
		return { 1.0f, 1.0f };
	}

	float scaleX;
	float scaleY;

	if (globals::game::ui->GameIsPaused()) {
		// Calculate scale as ratio of minimum render resolution to output resolution
		scaleX = (float)optimalSettings.renderWidthMin / (float)outputWidth;
		scaleY = (float)optimalSettings.renderHeightMin / (float)outputHeight;
	} else {
		// Calculate scale as ratio of optimal render resolution to output resolution
		scaleX = (float)optimalSettings.optimalRenderWidth / (float)outputWidth;
		scaleY = (float)optimalSettings.optimalRenderHeight / (float)outputHeight;
	}

	// Return separate X and Y scales for more precision
	return { scaleX, scaleY };
}

sl::DLSSMode RaytracedGI::GetDLSSMode()
{
	switch (settings.DLSSRRQualityMode) {
	case DLSSRRQuality::MaxPerformance:
		return sl::DLSSMode::eMaxPerformance;
		break;
	case DLSSRRQuality::MaxQuality:
		return sl::DLSSMode::eMaxQuality;
		break;
	default:
		return sl::DLSSMode::eBalanced;
		break;
	}
}

void RaytracedGI::SetDLSSRROptions()
{
	sl::DLSSDOptions dlssdOptions{};

	dlssdOptions.mode = GetDLSSMode();

	auto worldToCameraView = globals::game::frameBufferCached.GetCameraView().Transpose();
	auto cameraViewToWorld = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	auto state = globals::state;

	dlssdOptions.outputWidth = (uint)state->screenSize.x;
	dlssdOptions.outputHeight = (uint)state->screenSize.y;
	dlssdOptions.colorBuffersHDR = sl::Boolean::eTrue;
	dlssdOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	dlssdOptions.alphaUpscalingEnabled = sl::Boolean::eFalse;

	dlssdOptions.worldToCameraView = sl::float4x4{
		sl::float4{ worldToCameraView._11, worldToCameraView._12, worldToCameraView._13, worldToCameraView._14 },
		sl::float4{ worldToCameraView._21, worldToCameraView._22, worldToCameraView._23, worldToCameraView._24 },
		sl::float4{ worldToCameraView._31, worldToCameraView._32, worldToCameraView._33, worldToCameraView._34 },
		sl::float4{ worldToCameraView._41, worldToCameraView._42, worldToCameraView._43, worldToCameraView._44 }
	};
	dlssdOptions.cameraViewToWorld = sl::float4x4{
		sl::float4{ cameraViewToWorld._11, cameraViewToWorld._12, cameraViewToWorld._13, cameraViewToWorld._14 },
		sl::float4{ cameraViewToWorld._21, cameraViewToWorld._22, cameraViewToWorld._23, cameraViewToWorld._24 },
		sl::float4{ cameraViewToWorld._31, cameraViewToWorld._32, cameraViewToWorld._33, cameraViewToWorld._34 },
		sl::float4{ cameraViewToWorld._41, cameraViewToWorld._42, cameraViewToWorld._43, cameraViewToWorld._44 }
	};

	auto preset = sl::DLSSDPreset::ePresetE;  // sl::DLSSDPreset::ePresetD

	dlssdOptions.dlaaPreset = preset;
	dlssdOptions.qualityPreset = preset;
	dlssdOptions.balancedPreset = preset;
	dlssdOptions.performancePreset = preset;
	dlssdOptions.ultraPerformancePreset = preset;

	if (SL_FAILED(result, slDLSSDSetOptions(slViewportHandle, dlssdOptions))) {
		logger::critical("[DLSS RR] Could not set DLSS RR options");
		return;
	}
}

void RaytracedGI::CheckFrameConstants()
{
	if (frameChecker.IsNewFrame()) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);

		auto state = globals::state;

		sl::Constants slConstants = {};

		if (globals::game::isVR) {
			slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
		} else {
			slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
		}

		slConstants.cameraFOV = Util::GetVerticalFOVRad();
		slConstants.cameraNear = *globals::game::cameraNear;
		slConstants.cameraFar = *globals::game::cameraFar;

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
		slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
		slConstants.depthInverted = sl::Boolean::eFalse;

		recalculateCameraMatrices(slConstants);

		auto screenSize = state->screenSize;

		auto screenWidth = static_cast<int>(screenSize.x);
		auto screenHeight = static_cast<int>(screenSize.y);

		float2 resolutionScaleBase = GetInputResolutionScaleRR((uint32_t)screenSize.x, (uint32_t)screenSize.y);
		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase.x);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase.y);

		float2 resolutionScale = { 1.0f, 1.0f };

		// Use precise scale if the integer conversion doesn't change the dimensions
		if (renderWidth == screenWidth && renderHeight == screenHeight) {
			// For DLAA and other 1:1 modes, ensure exactly 1.0
			resolutionScale.x = 1.0f;
			resolutionScale.y = 1.0f;
		} else {
			resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
			resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);
		}

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { (globals::game::isVR ? 0.5f : 1.0f), 1 };
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, slViewportHandle))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}
#endif

LPCWSTR StringViewToLPCWSTR(std::string_view sv)
{
	std::string str(sv);

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);

	std::wstring wstr(size_needed, 0);

	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);

	return wstr.c_str();
}

void RaytracedGI::ShareRT(ID3D11Texture2D* pTexture2D, const HeapSlot::Slot& target, const ComputeHeapShadowsSlot::Slot& cTarget, ID3D12Resource** ppResource)
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture2D->GetDesc(&desc);

	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(pTexture2D->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle)); // DXGI_SHARED_RESOURCE_WRITE

	DX::ThrowIfFailed(d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(ppResource)));
	CloseHandle(sharedHandle);

	/*const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(*ppResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
	commandList->ResourceBarrier(1, &barrier);*/

	// Create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	if (target != HeapSlot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, commonHeap->CPUHandle(target));

	if (cTarget != ComputeHeapShadowsSlot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, computeHeapShadows->CPUHandle(cTarget));
}

void RaytracedGI::SetupSharedRT()
{
	const auto& rendererRD = globals::game::renderer->GetRuntimeData();

	ShareRT(rendererRD.renderTargets[ALBEDO].texture, HeapSlot::Albedo, ComputeHeapShadowsSlot::None, albedoTexture.put());
	ShareRT(rendererRD.renderTargets[REFLECTANCE].texture, HeapSlot::None, ComputeHeapShadowsSlot::None, gbufferReflectanceTexture.put());
	//ShareRT(rendererRD.renderTargets[NORMALROUGHNESS].texture, HeapSlot::NormalRoughness, ComputeHeapSlot::None, normalRoughnessTexture.put());
	ShareRT(rendererRD.renderTargets[MASKS2].texture, HeapSlot::GNMD, ComputeHeapShadowsSlot::None, GNMDTexture.put());  // GNMD

	DX::ThrowIfFailed(albedoTexture->SetName(L"Shared Albedo Texture"));
	DX::ThrowIfFailed(gbufferReflectanceTexture->SetName(L"Shared Reflectance Texture"));
	//DX::ThrowIfFailed(normalRoughnessTexture->SetName(L"Shared NormalRoughness Texture"));
	DX::ThrowIfFailed(GNMDTexture->SetName(L"Shared GNMD Texture"));
}

bool IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

eastl::vector<LightLimitFix::LightData> RaytracedGI::GetPointLights()
{
	eastl::vector<LightLimitFix::LightData> lightsData{};

	auto accumulator = *globals::game::currentAccumulator.get();
	const auto activeShadowSceneNode = accumulator->GetRuntimeData().activeShadowSceneNode;

	//auto& isl = globals::features::inverseSquareLighting;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) 
	{
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightLimitFix::LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

					/*if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;

						if (settings.PointFade)
							light.color *= runtimeData.fade;
					}*/

					light.radius = runtimeData.radius.x;

					if (settings.PointFade)
						light.color *= runtimeData.fade;

					//light.color *= bsLight->lodDimmer;

					if (!IsGlobalLight(bsLight)) {
						light.lightFlags.set(LightLimitFix::LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(shadowLightIndex, shadowLight);
						light.shadowMaskIndex = shadowLightIndex;
						light.lightFlags.set(LightLimitFix::LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						auto worldPos = niLight->world.translate;

						light.positionWS[0].data = float3(worldPos.x, worldPos.y, worldPos.z);

						if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	const auto& activeLights = activeShadowSceneNode->GetRuntimeData().activeLights;
	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = activeShadowSceneNode->GetRuntimeData().activeShadowLights;
	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	return lightsData;
}

float3 RaytracedGI::GammaToLinear(float3 color)
{
	if (settings.GammaToLinear) {
		float3 colorAbs = DirectX::XMVectorAbs(color);
		return float3(pow(colorAbs.x, 1.6f), pow(colorAbs.y, 1.6f), pow(colorAbs.z, 1.6f));
	} else {
		return color;
	}
}

void RaytracedGI::UpdateLights() 
{
	if (!renderingWorld || lightsUpdated)
		return;

	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& directionNi = dirLight->GetWorldDirection();
		auto direction = float3(directionNi.x, directionNi.y, directionNi.z);
		direction.Normalize();

		auto diffuse = dirLight->GetLightRuntimeData().diffuse;

		frameBufferData.Directional.Vector = -direction;
		frameBufferData.Directional.Color = GammaToLinear(float3(diffuse.red, diffuse.green, diffuse.blue)) * settings.Directional;  // * ( Util::IsInterior() ? 0.0f : 1.0f );
	}

	// Point lights
	{
		lights.clear();
		lights.reserve(MAX_LIGHTS);

		for (auto data : GetPointLights()) {
			if (lights.size() >= MAX_LIGHTS)
				break;

			lights.emplace_back(data.positionWS[0].data, data.radius, GammaToLinear(data.color) * settings.Point, 0);
		}

		lightBuffer->UpdateList(lights.data(), lights.size());
	}

	lightsUpdated = true;
}

DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
{
	DirectX::XMMATRIX temp;

	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][0],
										   m.entry[1][0],
										   m.entry[2][0],
										   0.0f),
		scale);

	temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][1],
										   m.entry[1][1],
										   m.entry[2][1],
										   0.0f),
		scale);

	temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][2],
										   m.entry[1][2],
										   m.entry[2][2],
										   0.0f),
		scale);

	temp.r[3] = DirectX::XMVectorSet(
		Transform.translate.x,
		Transform.translate.y,
		Transform.translate.z,
		1.0f);

	return temp;
}

void RaytracedGI::CopyDepth()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->CSSetShader(copyDepthCS.get(), nullptr, 0);

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN]; // kPOST_ZPREPASS_COPY
	context->CSSetShaderResources(0, 1, &depth.depthSRV);

	context->CSSetUnorderedAccessViews(0, 1, &depthTexture->uav, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
}

void RaytracedGI::ConvertNormalGlossiness()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->CSSetShader(convertNormalGlossCS.get(), nullptr, 0);

	ID3D11Buffer* cb[1] = { *globals::game::perFrame.get() };
	context->CSSetConstantBuffers(12, 1, cb);

	auto srv = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS].SRV;
	context->CSSetShaderResources(0, 1, &srv);

	auto uav = normalRoughnessTexture->uav;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
}

void RaytracedGI::SkyCubeToHemi()
{
	auto context = globals::d3d::context;

	context->CSSetShader(cubeToHemiCS.get(), nullptr, 0);

	auto reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];
	context->CSSetShaderResources(0, 1, &reflections.SRV);

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	context->CSSetUnorderedAccessViews(0, 1, &skyHemisphere->uav, nullptr);

	float hemiResolution = SKY_CUBEMAP_SIZE * 2.0f;
	uint dispatch = (uint)std::ceil(hemiResolution / 8.0f);

	context->Dispatch(dispatch, dispatch, 1);
}

void RaytracedGI::Main_RenderWorld(bool a1)
{
	if (Active()) {
		renderingWorld = true;
		lightsUpdated = false;

		SkyCubeToHemi();

		if (!addedAllInstances)
		{
			AddUpdateAllInstances();
			addedAllInstances = true;
		}
	}

	Hooks::Main_RenderWorld::func(a1);

	if (Active()) {
		renderingWorld = false;
	}
}

template <typename T>
auto GetFlags(uint32_t value) {
	const auto& entries = magic_enum::enum_entries<T>();

	std::string flags;

	for (const auto& [flag, name] : entries) {
		if ((value & static_cast<uint32_t>(flag)) != 0) {
			flags += fmt::format("{} ", name);
		}
	}

	return flags;
};

template <typename T>
auto GetFlags(uint64_t value)
{
	const auto& entries = magic_enum::enum_entries<T>();

	std::string flags;

	for (const auto& [flag, name] : entries) {
		if ((value & static_cast<uint64_t>(flag)) != 0) {
			flags += fmt::format("{} ", name);
		}
	}

	return flags;
};

RE::BSFadeNode* FindBSFadeNode(RE::NiNode* a_niNode)
{
	if (auto fadeNode = a_niNode->AsFadeNode()) {
		return fadeNode;
	}
	return a_niNode->parent ? FindBSFadeNode(a_niNode->parent) : nullptr;
}

template <typename T>
void RaytracedGI::MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res)
 {
	auto desc = BASIC_BUFFER_DESC;
	desc.Width = sizeof(T) * data.size();

	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));

	void* ptr;
	DX::ThrowIfFailed(res->Map(0, nullptr, &ptr));
	memcpy(ptr, data.data(), desc.Width);
	res->Unmap(0, nullptr);
}

inline std::wstring ToWide(const std::string& str)
{
	if (str.empty())
		return std::wstring();

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), &wstr[0], size_needed);
	return wstr;
}

void RaytracedGI::AddInstance(RE::BSTriShape* pTriShape)
{
	//std::lock_guard lock{ renderMutex };

	if (pTriShape->worldBound.radius == 0.0f)
		return;

	auto& geometryRuntimeData = pTriShape->GetGeometryRuntimeData();

	RE::BSGraphics::TriShape* rendererData = geometryRuntimeData.rendererData;

	if (!rendererData) {
		//logger::warn(fmt::runtime("[RT] AddInstance - {} has no rendererData, niSkinInstance: [0x{:x}]"), pTriShape->name, reinterpret_cast<uintptr_t>(niSkinInstance));
		return;
	}

	/*RE::NiSkinInstance* niSkinInstance = geometryRuntimeData.skinInstance.get();
	if (!rendererData && niSkinInstance) {
		if (auto skinPartition = niSkinInstance->skinPartition.get(); skinPartition)
			for (uint32_t i = 0; i < skinPartition->numPartitions; i++) {
				auto& partition = skinPartition->partitions[i];
				rendererData = partition.buffData;
			}
	}*/

	// Our beloved key, this could mess things up if the engine uses the same vertexBuffer for another, but based on Brixelizer it doesn't...
	auto vertexBufferDX11 = (ID3D11Buffer*)rendererData->vertexBuffer;

	auto meshIt = meshes.find(vertexBufferDX11);

	// Mesh doesn't exist yet
	if (meshIt == meshes.end()) 
	{
		//logger::warn(fmt::runtime("[RT] AddInstance - {}, Child Index {}: Parent [0x{:x}] - Name: {}"), pTriShape->name, pTriShape->parentIndex, reinterpret_cast<uintptr_t>(pTriShape->parent), pTriShape->parent->name);

		const auto triShapeRuntime = pTriShape->GetTrishapeRuntimeData();
		uint vertexCount = triShapeRuntime.vertexCount;
		uint triangleCount = triShapeRuntime.triangleCount;

		std::wstring geometryNameW = ToWide(pTriShape->name.c_str());

		eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Vertex>>(d3d12Device.get(), vertexCount);
		eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> triangleBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Triangle>>(d3d12Device.get(), triangleCount);
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;

		// Create vertex buffer
		{
			auto vertexDesc = rendererData->vertexDesc;

			auto vertexFlags = vertexDesc.GetFlags();
			uint32_t stride = vertexDesc.GetSize();

			uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
			uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
			uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
			uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);

			eastl::vector<Vertex> vertices(vertexCount);

			for (uint32_t i = 0; i < vertexCount; i++) {
				uint8_t* vtx = rendererData->rawVertexData + i * stride;

				Vertex vertexData{};

				if (vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX) {
					const float* pos = reinterpret_cast<const float*>(vtx + posOffset);
					vertexData.Position.x = pos[0];
					vertexData.Position.y = pos[1];
					vertexData.Position.z = pos[2];
					//vertexData.Position.w = pos[3];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
					const uint16_t* texcoord = reinterpret_cast<const uint16_t*>(vtx + uvOffset);

					vertexData.Texcoord0[0] = texcoord[0];
					vertexData.Texcoord0[1] = texcoord[1];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL) {
					const uint8_t* norm = reinterpret_cast<const uint8_t*>(vtx + normOffset);

					vertexData.Normal[0] = norm[0];
					vertexData.Normal[1] = norm[1];
					vertexData.Normal[2] = norm[2];
					vertexData.Normal[3] = norm[3];
				}

				if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
					const uint8_t* col = reinterpret_cast<const uint8_t*>(vtx + colorOffset);

					vertexData.Color[0] = col[0];
					vertexData.Color[1] = col[1];
					vertexData.Color[2] = col[2];
					vertexData.Color[3] = col[3];
				}

				vertices[i] = eastl::move(vertexData);
			}

			vertexBuffer->UpdateList(vertices.data(), vertices.size());
			DX::ThrowIfFailed(vertexBuffer->resource->SetName(std::format(L"Vertex Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));

			vertexBuffer->Upload(commandList.get());
		}

		// Create indices buffer
		{
			const uint16_t* indexes = rendererData->rawIndexData;

			eastl::vector<Triangle> triangles(triangleCount);

			for (uint32_t t = 0; t < triangleCount; ++t) {
				uint32_t i = t * 3;

				triangles[t] = Triangle(
					static_cast<uint32_t>(indexes[i]),
					static_cast<uint32_t>(indexes[i + 1]),
					static_cast<uint32_t>(indexes[i + 2]));
			}

			triangleBuffer->UpdateList(triangles.data(), triangles.size());
			DX::ThrowIfFailed(triangleBuffer->resource->SetName(std::format(L"Index Buffer [{}] - {}", meshes.size(), geometryNameW).c_str()));

			triangleBuffer->Upload(commandList.get());
		}

		using State = RE::BSGeometry::States;
		using Feature = RE::BSShaderMaterial::Feature;

		Feature feature = Feature::kNone;
		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* effectTexture = nullptr;
		float4 effectColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		int effectType = 0;

		float4 texCoordOffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };
		float4 texCoord1OffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };

		// Register textures buffer
		{
			//auto geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

			auto effect = geometryRuntimeData.properties[State::kEffect].get();

			if (effect) {
				//auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);

				//logger::warn(fmt::runtime("[RT] AddInstance - {}, ShaderPropertyFlags: {}"), pTriShape->name, GetFlags<RE::BSShaderProperty::EShaderPropertyFlag>(shaderProperty->flags.underlying()));

				auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);

				if (lightingShader) {
					effectColor = {
						lightingShader->emissiveColor->red,
						lightingShader->emissiveColor->green,
						lightingShader->emissiveColor->blue,
						lightingShader->emissiveMult
					};

					/*auto shaderPropertyFlags = lightingShader->flags;
					auto effectData = lightingShader->effectData;
					auto textureClampMode = effectData->textureClampMode;*/

					if (auto material = lightingShader->material) {
						texCoordOffsetScale = {
							material->texCoordOffset[0].x, material->texCoordOffset[0].y,
							material->texCoordScale[0].x, material->texCoordScale[0].y
						};

						/*texCoord1OffsetScale = {
							material->texCoordOffset[1].x, material->texCoordOffset[1].y,
							material->texCoordScale[1].x, material->texCoordScale[1].y
						};*/

						feature = material->GetFeature();

						auto lightingBaseMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(material);

						if (lightingBaseMaterial) {
							auto niDiffuseTexture = lightingBaseMaterial->diffuseTexture;
							auto bsDiffuseTexture = niDiffuseTexture->rendererTexture;

							if (auto it = sharedTextures.find(bsDiffuseTexture->texture); it != sharedTextures.end()) {
								diffuseTexture = it->second.get();
							} else {
								logger::warn(fmt::runtime("[RT] Diffuse texture not found for {}"), pTriShape->name.c_str());
							}

							if (feature == Feature::kGlowMap) {
								const auto& lightingGlowMaterial = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material);

								if (lightingGlowMaterial) {
									if (const auto& niGlowTexture = lightingGlowMaterial->glowTexture; niGlowTexture) {
										if (const auto& bsGlowTexture = niGlowTexture->rendererTexture; bsGlowTexture) {
											if (auto it = sharedTextures.find(bsGlowTexture->texture); it != sharedTextures.end()) {
												effectTexture = it->second.get();
											}
										}
									}
								}

								if (!effectTexture)
									logger::warn(fmt::runtime("[RT] Glow texture not found for {}"), pTriShape->name.c_str());
							}
						}
					}
				}

				auto effectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect);

				if (effectShader) {
					//auto effectData = effectShader->effectData;
					//effectData->baseTexture;
					//effectData->paletteTexture;
					//logger::info("[RT] AddInstance - Effect Shader {}", pTriShape->name);
					
					if (auto material = effectShader->material) {
						auto effectMaterial = static_cast<RE::BSEffectShaderMaterial*>(material);

						if (effectMaterial) {
							effectType = 1;
							effectColor = { effectMaterial->baseColor.red, effectMaterial->baseColor.green, effectMaterial->baseColor.blue, effectMaterial->baseColorScale };

							if (auto niSourceTexture = effectMaterial->sourceTexture) {
								if (auto bsSourceTexture = niSourceTexture->rendererTexture) {
									if (auto it = sharedTextures.find(bsSourceTexture->texture); it != sharedTextures.end()) {
										diffuseTexture = it->second.get();
									} else {
										logger::warn(fmt::runtime("[RT] Source texture not found for {}"), pTriShape->name.c_str());
									}
								}
							}

							if (auto niGreyscaleTexture = effectMaterial->greyscaleTexture) {
								if (auto bsGreyscaleTexture = niGreyscaleTexture->rendererTexture) {
									if (auto it = sharedTextures.find(bsGreyscaleTexture->texture); it != sharedTextures.end()) {
										effectTexture = it->second.get();
									} else {
										logger::warn(fmt::runtime("[RT] Greyscale texture not found for {}"), pTriShape->name.c_str());
									}
								}
							}
						}
					}
				}
			}
		}

		// Create BLAS
		{
			blasBuffer.attach(MakeBLAS(vertexBuffer->resource.get(), vertexCount, triangleBuffer->resource.get(), triangleCount * 3));
		}

		uint registerIndex = registers.allocate();

		//logger::info("[RT] Register {} for mesh {}", registerIndex, meshes.size());

		// SRVs
		{
			// Vertex structured buffer
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = static_cast<int32_t>(vertexCount);
			vbDesc.Buffer.StructureByteStride = sizeof(Vertex);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(vertexBuffer->resource.get(), &vbDesc, commonHeap->CPUHandle(HeapSlot::Vertices, registerIndex));

			// Index/Triangle (Structured buffer)
			D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
			ibDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			ibDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ibDesc.Format = DXGI_FORMAT_UNKNOWN;
			ibDesc.Buffer.FirstElement = 0;
			ibDesc.Buffer.NumElements = static_cast<int32_t>(triangleCount);
			ibDesc.Buffer.StructureByteStride = sizeof(Triangle);
			ibDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			d3d12Device->CreateShaderResourceView(triangleBuffer->resource.get(), &ibDesc, commonHeap->CPUHandle(HeapSlot::Triangles, registerIndex));

			// Diffuse Texture
			if (diffuseTexture) {
				D3D12_RESOURCE_DESC texResDesc = diffuseTexture->GetDesc();

				D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
				texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				texSrvDesc.Format = texResDesc.Format;
				texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				texSrvDesc.Texture2D.MostDetailedMip = 0;
				texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
				texSrvDesc.Texture2D.PlaneSlice = 0;
				texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				d3d12Device->CreateShaderResourceView(diffuseTexture, &texSrvDesc, commonHeap->CPUHandle(HeapSlot::DiffuseTextures, registerIndex));
			}

			// Glow Texture
			if (effectTexture) {
				D3D12_RESOURCE_DESC texResDesc = effectTexture->GetDesc();

				D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
				texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				texSrvDesc.Format = texResDesc.Format;
				texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				texSrvDesc.Texture2D.MostDetailedMip = 0;
				texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
				texSrvDesc.Texture2D.PlaneSlice = 0;
				texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				d3d12Device->CreateShaderResourceView(effectTexture, &texSrvDesc, commonHeap->CPUHandle(HeapSlot::GlowTextures, registerIndex));
			}		
		}

		// Emplace mesh
		meshes.emplace(
			vertexBufferDX11,
			MeshData(
				registerIndex,
				vertexCount,
				triangleCount,
				false,
				eastl::move(vertexBuffer),
				eastl::move(triangleBuffer),
				std::move(blasBuffer),
				MaterialData(feature, texCoordOffsetScale, diffuseTexture, effectTexture, nullptr, effectColor, effectType),
				{ pTriShape }));

	} else {
		meshIt->second.instances.push_back(pTriShape);
	}

	instances.emplace(
		pTriShape,
		InstanceData(
			vertexBufferDX11,
			GetXMFromNiTransform(pTriShape->world)));
}

eastl::vector<size_t> RaytracedGI::GatherInstanceLights(RE::BSTriShape* pBSTriShape)
{
	eastl::vector<size_t> instanceLights;

	float3 center = Float3(pBSTriShape->worldBound.center);
	float radius = pBSTriShape->worldBound.radius;

	for (size_t i = 0; i < lights.size(); i++) {
		const Light& light = lights[i];

		if ((center - light.Vector).Length() <= radius + light.Range)
			instanceLights.push_back(i);
	}

	return instanceLights;
}

void RaytracedGI::AddUpdateInstance(RE::BSTriShape* pTriShape)
{
	std::lock_guard lock{ meshMutex };

	auto effect = pTriShape->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
	auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);

	if (!ValidTriShape(pTriShape) && (!shaderProperty || (shaderProperty && shaderProperty->flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor))))
		return;

	//logger::info("[RT] {} [0x{:x}] [0x{:x}]", pTriShape->name.c_str(), reinterpret_cast<uintptr_t>(pTriShape), reinterpret_cast<uintptr_t>(pTriShape->GetGeometryRuntimeData().rendererData->vertexBuffer));

	if (const auto it = instances.find(pTriShape); it != instances.end()) {
		InstanceData& instance = it->second;

		instance.transform = GetXMFromNiTransform(pTriShape->world);	
	} else {
		AddInstance(pTriShape);	
	}
}

void RaytracedGI::VertexBufferReleased(ID3D11Buffer* pBuffer)
{
	std::lock_guard lock{ meshMutex };

	if (const auto it = meshes.find(pBuffer); it != meshes.end()) {
		MeshData& meshData = it->second;

		logger::info("[RT] VertexBufferReleased - Instances: {}", meshData.instances.size());

		for (auto& instance : meshData.instances)
			instances.erase(instance);

		registers.free(meshData.registerIndex);

		meshes.erase(it);
	/*} else {
		logger::info("[RT] VertexBufferReleased - Not Found [0x{:x}]", reinterpret_cast<uintptr_t>(pBuffer));*/
	}
}

bool RaytracedGI::ValidTriShape(RE::BSTriShape* pTriShape)
{
	if (pTriShape->GetFlags().all(RE::NiAVObject::Flag::kRenderUse)) {
		if (auto fadeNode = FindBSFadeNode((RE::NiNode*)pTriShape)) {
			if (auto extraData = fadeNode->GetExtraData("BSX")) {
				auto bsxFlags = (RE::BSXFlags*)extraData;

				if (static_cast<uint32_t>(bsxFlags->value) & (uint32_t)RE::BSXFlags::Flag::kEditorMarker)
					return false;
			}
		} else {  // Else it crashes on Block stuff when reading indexes
			return false;
		}
	} else {
		return false;
	}

	return true;
}

void RaytracedGI::AddUpdateAllInstances()
{
	static auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];

	RE::BSVisit::TraverseScenegraphGeometries(shadowSceneNode, [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
		if (RE::BSTriShape* pTriShape = geometry->AsTriShape())
			AddUpdateInstance(pTriShape);

		return RE::BSVisit::BSVisitControl::kContinue;
	});
}

RE::NiCamera* FindNiCamera(RE::NiAVObject* object)
{
	if (auto* camera = skyrim_cast<RE::NiCamera*>(object))
		return camera;

	auto* node = object->AsNode();
	if (!node)
		return nullptr;

	for (auto child : node->GetChildren()) {
		if (child) {
			if (auto* res = FindNiCamera(child.get()))
				return res;
		}
	}
	return nullptr;
}

void RaytracedGI::UpdateInstances()
{
	std::lock_guard lock{ meshMutex };

	instanceData.clear();
	instanceData.reserve(instances.size());

	blasInstances.clear();
	blasInstances.reserve(instances.size());

	auto* playerCamera = RE::PlayerCamera::GetSingleton();
	auto* tesCamera = playerCamera->currentState->camera;

	RE::NiCamera* camera = FindNiCamera(tesCamera->cameraRoot.get());

	auto eye = Util::GetAverageEyePosition();

	for (auto& [pTriShape, data] : instances) {

		MeshData& meshData = meshes[data.meshKey];
		auto worldBound = pTriShape->worldBound;

		float worldBoundRadius= worldBound.radius;
		float distanceToBounds = Util::Units::GameUnitsToMeters(eye.GetDistance(worldBound.center) - worldBoundRadius);

		auto cullOutOfView = meshData.material.feature != RE::BSShaderMaterial::Feature::kGlowMap && meshData.material.shaderType == 0;

		if ((cullOutOfView && Util::Units::GameUnitsToMeters(worldBoundRadius) < 1.0f) || distanceToBounds > 100.0f) {
			if (!RE::NiCamera::BoundInFrustum(worldBound, camera))
				continue;
		}

		blasInstances.push_back({ 
			.InstanceID = static_cast<uint>(blasInstances.size()),
			.InstanceMask = 1,
			.AccelerationStructure = meshData.blasBuffer->GetGPUVirtualAddress()
		});

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&blasInstances.back().Transform);
		XMStoreFloat3x4(ptr, data.transform);

		const auto& material = meshData.material;

		instanceData.emplace_back(
			static_cast<uint>(meshData.registerIndex), 
			LightData(GatherInstanceLights(pTriShape)), 
			Material(material.texCoordOffsetScale, material.effectColor, static_cast<float>(material.shaderType))
		);
	}
	
	blasInstanceBuffer->UpdateList(blasInstances.data(), blasInstances.size());
	blasInstanceBuffer->Upload(commandList.get());

	instanceBuffer->UpdateList(instanceData.data(), instanceData.size());
	instanceBuffer->Upload(commandList.get());
}

void RaytracedGI::UpdateShadowInstances()
{
	std::lock_guard lock{ meshMutex };

	if (!shadowLight)
		return;

	blasShadowInstances.clear();
	blasShadowInstances.reserve(instances.size());

	//RE::NiCamera* camera = shadowLight->GetShadowDirectionalLightRuntimeData().cullingCamera.get();

	for (auto& [pTriShape, data] : instances) {
		MeshData& meshData = meshes[data.meshKey];

		//if (!RE::NiCamera::BoundInFrustum(pTriShape->worldBound, camera))
		//	continue;

		blasShadowInstances.push_back({ .InstanceID = static_cast<uint>(blasShadowInstances.size()),
			.InstanceMask = 1,
			.AccelerationStructure = meshData.blasBuffer->GetGPUVirtualAddress() });

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&blasShadowInstances.back().Transform);
		XMStoreFloat3x4(ptr, data.transform);
	}

	blasShadowInstanceBuffer->UpdateList(blasShadowInstances.data(), blasShadowInstances.size());
	blasShadowInstanceBuffer->Upload(commandList.get());
}

void RaytracedGI::BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
{
	const auto shader = pass->shader;
	const auto shaderType = shader->shaderType.get();

	if (shaderType != RE::BSShader::Type::Lighting)
		return;

	const auto geometry = pass->geometry;

	const auto triShape = geometry->AsTriShape();

	if (triShape == nullptr)
		return;

	const auto& type = geometry->GetType().get();
	const auto typeName = magic_enum::enum_name(type);

	const auto& flagsUnd = geometry->GetFlags().underlying();
	const auto flagsStr = GetFlags<RE::NiAVObject::Flag>(flagsUnd);

	// NiAVObject::Flag
	logger::info(fmt::runtime("Geometry [0x{:x}]: {}"), reinterpret_cast<uintptr_t>(geometry), geometry->name.c_str());
	logger::info(fmt::runtime("Flags: {}"), flagsStr);

	logger::info(
		fmt::runtime("Pass: [0x{:x}], Alpha Test: {}, Render Flags: [0x{:x}], TriShape: [0x{:x}], Type: {}"),
		reinterpret_cast<uintptr_t>(pass),
		alphaTest,
		renderFlags,
		reinterpret_cast<uintptr_t>(triShape),
		typeName);

	if (technique)
		return;

	//int32_t technique = 0x3F & (vertexDescriptor >> 24);
	//logger::info(fmt::runtime("LightingShaderTechniques: {}"), GetFlags<SIE::ShaderCache::LightingShaderTechniques>(technique));
}

void RaytracedGI::BSTriShape_UpdateWorldData(RE::BSTriShape* oThis, RE::NiUpdateData* pData)
{
	if (Active() && pData->flags.any(RE::NiUpdateData::Flag::kDirty)) {
		RE::NiPoint3 pointA = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);

		RE::NiPoint3 pointB = oThis->world * RE::NiPoint3{ 1.0f, 1.0f, 1.0f };

		if (pointA.GetDistance(pointB) > 0.1f) {
			//AddUpdateInstance(oThis);
		}		
	} else {
		Hooks::BSTriShape_UpdateWorldData::func(oThis, pData);
	}
}

void RaytracedGI::BSShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, [[maybe_unused]] RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active() || !renderingWorld)
		return;

	UpdateLights();

	if (auto triShape = pPass->geometry->AsTriShape())
		AddUpdateInstance(triShape);
	/*else
		logger::warn("TriShape not available for {}, type: {}", pPass->geometry->name, magic_enum::enum_name(pPass->geometry->GetType().get()));*/
}

void RaytracedGI::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	/*auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(skyCubemapRTV[side].get(), black);
	context->ClearDepthStencilView(skyCubemapDSV[side].get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);*/
}


void RaytracedGI::BSSkyShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, [[maybe_unused]] RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	/*if (!Active() || !renderingCubemap)
		return;

	auto context = globals::d3d::context;

	using enum SIE::ShaderCache::SkyShaderTechniques;
	const auto technique = static_cast<SIE::ShaderCache::SkyShaderTechniques>(globals::state->currentPixelDescriptor);
	bool clouds = (technique == Clouds || technique == CloudsLerp || technique == CloudsFade);

	if (technique != Sky)
		return;

	if (clouds)
		return;*/

	//ID3D11Buffer* prevCB = nullptr;

	/*if (!clouds) {
		auto shadowState = globals::game::shadowState;
		GET_INSTANCE_MEMBER(currentVertexShader, shadowState)
		auto constantBuffers = currentVertexShader->constantBuffers;
		auto perGeometryCB = constantBuffers[PER_GEOMETRY_IDX];

		memcpy(&skyPerGeometryCBData, perGeometryCB.data, sizeof(SkyPerGeometry));

		skyPerGeometryCBData.WorldViewProj._34 *= 2.0f;

		logger::info("[RT] BSSkyShader::SetupGeometry {} - WorldViewProj: {}", pPass->geometry->name.c_str(), skyPerGeometryCBData.WorldViewProj);

		skyPerGeometryCB->Update(skyPerGeometryCBData);

		context->VSGetConstantBuffers(PER_GEOMETRY_IDX, 1, &prevCB);

		auto skyPerGeomCB = skyPerGeometryCB->CB();
		context->VSSetConstantBuffers(PER_GEOMETRY_IDX, 1, &skyPerGeomCB);
	}*/

	/*auto reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	auto pGeometry = pPass->geometry;
	auto geomRuntimeDta = pGeometry->GetGeometryRuntimeData();

	auto pTriShape = pGeometry->AsTriShape();
	auto triShapeRuntimeData = pTriShape->GetTrishapeRuntimeData();

	uint numViewports = 1;
	D3D11_VIEWPORT prevViewport = {};
	context->RSGetViewports(&numViewports, &prevViewport);

	ID3D11RenderTargetView* prevRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
	ID3D11DepthStencilView* prevDSV = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRTVs, &prevDSV);

	int side = -1;

	for (int i = 0; i < 6; ++i)
		if (prevRTVs && prevRTVs[0] == reflections.cubeSideRTV[i]) {
			side = i;
			break;
		}

	if (side != -1) {
		CheckResourcesSide(side);

		context->RSSetViewports(1, &skyCubemapViewport);

		auto rtv = skyCubemapRTV[side].get();
		context->OMSetRenderTargets(1, &rtv, clouds ? nullptr : skyCubemapDSV[side].get());

		if (clouds) {
			auto depthSRV = skyCubemapDepthSRV[side].get();
			context->PSSetShaderResources(17, 1, &depthSRV);
		}

		context->DrawIndexed(triShapeRuntimeData.triangleCount * 3, 0, 0);

		context->RSSetViewports(1, &prevViewport);

		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRTVs, prevDSV);
	}

	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		if (prevRTVs[i]) {
			prevRTVs[i]->Release();
		}
	}

	if (prevDSV)
		prevDSV->Release();*/

	/*if (!clouds)
		context->VSSetConstantBuffers(PER_GEOMETRY_IDX, 1, &prevCB);

	if (prevCB)
		prevCB->Release();*/
}

/*void RaytracedGI::BSSkyShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, [[maybe_unused]] RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active() || !renderingWorld)
		return;

	auto context = globals::d3d::context;

	auto pGeometry = pPass->geometry;
	auto geomRuntimeDta = pGeometry->GetGeometryRuntimeData();

	auto pTriShape = pGeometry->AsTriShape();
	auto triShapeRuntimeData = pTriShape->GetTrishapeRuntimeData();
	
	ID3D11Buffer* prevCB = nullptr;
	context->VSGetConstantBuffers(PER_GEOMETRY_IDX, 1, &prevCB);
	
	uint numViewports = 1;
	D3D11_VIEWPORT prevViewport = {};
	context->RSGetViewports(&numViewports, &prevViewport);

	ID3D11RenderTargetView* prevRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
	ID3D11DepthStencilView* prevDSV = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRTVs, &prevDSV);

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(currentVertexShader, shadowState)
	auto constantBuffers = currentVertexShader->constantBuffers;
	auto perGeometryCB = constantBuffers[PER_GEOMETRY_IDX];

	memcpy(&skyPerGeometryCBData, perGeometryCB.data, sizeof(SkyPerGeometry));
	skyPerGeometryCBData.EyePosition = { 0.0f, 0.0f, 0.0f };

	
	logger::info("[RT] BSSkyShader::SetupGeometry - Camera ViewMatrix: {}", globals::game::frameBufferCached.GetCameraView());

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	logger::info("[RT] BSSkyShader::SetupGeometry - Camera ViewMatrix (Inverse Transposed): {}", viewMatrix);

	float3 cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	float3 cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	float3 cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	logger::info("[RT] BSSkyShader::SetupGeometry - Camera Right: {}, Camera Up: {}, Camera Forward: {}", cameraRight, cameraUp, cameraFwd);

	logger::info("[RT] BSSkyShader::SetupGeometry - Camera ViewProj: {}", globals::game::frameBufferCached.GetCameraViewProj());

	logger::info("[RT] BSSkyShader::SetupGeometry - World: {}", skyPerGeometryCBData.World);
	logger::info("[RT] BSSkyShader::SetupGeometry - WorldViewProj: {}", skyPerGeometryCBData.WorldViewProj);
	logger::info("[RT] BSSkyShader::SetupGeometry - EyePosition: {}", skyPerGeometryCBData.EyePosition);

	auto skyPerGeomCB = skyPerGeometryCB->CB();
	context->VSSetConstantBuffers(PER_GEOMETRY_IDX, 1, &skyPerGeomCB);

	context->RSSetViewports(1, &skyCubemapViewport);

	for (int i = 0; i < 6; i++) {
		skyPerGeometryCBData.WorldViewProj = skyCubemapWorldViewProj[i];
		skyPerGeometryCB->Update(skyPerGeometryCBData);


		auto rtv = skyCubemapRTV[i].get();
		context->OMSetRenderTargets(1, &rtv, skyCubemapDSV[i].get());

		context->DrawIndexed(triShapeRuntimeData.triangleCount * 3, 0, 0);
	}

	context->VSSetConstantBuffers(PER_GEOMETRY_IDX, 1, &prevCB);

	prevCB->Release();

	context->RSSetViewports(1, &prevViewport);

	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRTVs, prevDSV);

	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		if (prevRTVs[i]) {
			prevRTVs[i]->Release();
		}
	}

	if (prevDSV) {
		prevDSV->Release();
	}
}*/

void RaytracedGI::DrawRTGI()
{
	//std::lock_guard lock{ renderMutex };

	if (!d3d11Context) {
		logger::error("d3d11Context is nullptr");
	}

	if (!d3d11Fence) {
		logger::error("d3d11Fence is nullptr");
	}

	auto rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto main = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kMAIN];

	d3d11Context->CopyResource(mainTexture->resource11, main.texture);
	d3d11Context->CopyResource(motionVectorsTexture->resource11, rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].texture);

	if (!settings.RaytracedShadows)
		CopyDepth();

	ConvertNormalGlossiness();

	// Wait for D3D11 to finish
	{
		//d3d11Context->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
		d3d11Context->Flush();
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;
	}

	// New frame, reset
	/*{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	// Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
	}*/

	/*if (pixCapture) {
		pixCaptureStarted = true;
		ga->BeginCapture();
	}*/

	UpdateInstances();

	// Upload buffers
	lightBuffer->Upload(commandList.get());

#ifdef DLSS_RR
	if (settings.Denoiser == Denoiser::DLSSRR) {
		SetDLSSRROptions();
		CheckFrameConstants();
	}
#endif

	// Update framebuffer
	{
		frameBufferData.ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameBufferData.ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();

		float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameBufferData.Position = float3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
		frameBufferData.FrameCount = globals::state->frameCount;

		frameBufferData.CameraData = Util::GetCameraData();

		auto eye = Util::GetCameraData(0);
		float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
		float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

		frameBufferData.NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

		frameBufferData.Roughness = settings.Roughness;
		frameBufferData.Metalness = settings.Metalness;

		frameBufferData.Diffuse = settings.Diffuse;
		frameBufferData.Specular = settings.Specular;
		frameBufferData.Emissive = settings.Emissive;
		frameBufferData.Effect = settings.Effect;
		frameBufferData.Sky = settings.Sky;

#ifdef SHARC
		frameBufferData.SHARCScale = settings.SHARCScale / Util::Units::GAME_UNIT_TO_M;
#endif

		frameBuffer->Update(&frameBufferData, sizeof(FrameBuffer));
		frameBuffer->Upload(commandList.get());
		frameBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	if (tlas == nullptr && tlasScratch == nullptr)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
			.NumDescs = MAX_INSTANCES,
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.InstanceDescs = blasInstanceBuffer->resource->GetGPUVirtualAddress()
		};

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
		d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

		auto desc = BASIC_BUFFER_DESC;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		// TLAS
		{	
			desc.Width = prebuildInfo.ResultDataMaxSizeInBytes * TLAS_BUFFER_SIZE_MULT;
			DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&tlas)));
			DX::ThrowIfFailed(tlas->SetName(L"TLAS"));

			// SRV
			D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
			tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
			tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, commonHeap->CPUHandle(HeapSlot::TLAS));
			d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, computeHeapShadows->CPUHandle(ComputeHeapShadowsSlot::TLAS));
		}

		// TLAS scratch (used for rebuilding)
		desc.Width = std::max(prebuildInfo.ScratchDataSizeInBytes * TLAS_BUFFER_SIZE_MULT, 8ULL);
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&tlasScratch)));
		DX::ThrowIfFailed(tlasScratch->SetName(L"TLAS scratch"));

		// TLAS update scratch
		/*desc.Width = std::max(prebuildInfo.UpdateScratchDataSizeInBytes * TLAS_BUFFER_SIZE_MULT, 8ULL);  // WARP bug workaround: use 8 if the required size was reported as less
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
		DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch"));*/
	}

	// Build/Rebuild TLAS
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
			.NumDescs = static_cast<uint>(instanceData.size()),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.InstanceDescs = blasInstanceBuffer->resource->GetGPUVirtualAddress()
		};

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
			.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
			.Inputs = inputs,
			.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress() 
		};

		commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
		commandList->ResourceBarrier(1, &asBarrier);
	}

	{
		// Raytracing
		{
			commandList->SetPipelineState1(pipelineRT.get());
			commandList->SetComputeRootSignature(rootSignature.get());

			auto commonHeapPtr = commonHeap->Heap();
			commandList->SetDescriptorHeaps(1, &commonHeapPtr);

			// Parameter 0: UAV table
			commandList->SetComputeRootDescriptorTable(0, commonHeap->TableGPUHandle(HeapType::UAV));

			// Parameter 1: Fixed SRVs
			commandList->SetComputeRootDescriptorTable(1, commonHeap->TableGPUHandle(HeapType::SRV));

			// Parameter 2: Vertex buffers
			commandList->SetComputeRootDescriptorTable(2, commonHeap->TableGPUHandle(HeapType::VertexBuffer));

			// Parameter 3: Triangle buffers
			commandList->SetComputeRootDescriptorTable(3, commonHeap->TableGPUHandle(HeapType::TriangleBuffer));

			// Parameter 4: Diffuse Textures
			commandList->SetComputeRootDescriptorTable(4, commonHeap->TableGPUHandle(HeapType::DiffuseTextures));

			// Parameter 5: Glow Textures
			commandList->SetComputeRootDescriptorTable(5, commonHeap->TableGPUHandle(HeapType::GlowTextures));

			// Parameter 6: Constant buffer
			commandList->SetComputeRootConstantBufferView(6, frameBuffer->resource->GetGPUVirtualAddress());

			auto finalTexDesc = mainTexture->resource->GetDesc();
			
			D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
			dispatchDesc.Width = static_cast<uint>(finalTexDesc.Width);
			dispatchDesc.Height = finalTexDesc.Height;
			dispatchDesc.Depth = 1;

			shaderBindingTable->FillDispatchShaderBindingTable(dispatchDesc, shaderBindingTableBuffer->resource->GetGPUVirtualAddress());

			commandList->DispatchRays(&dispatchDesc);

			CD3DX12_RESOURCE_BARRIER rtUAVBarrier[3] = {
				CD3DX12_RESOURCE_BARRIER::UAV(outputTexture->resource.get()),
				CD3DX12_RESOURCE_BARRIER::UAV(reflectanceTexture->resource.get()),
				CD3DX12_RESOURCE_BARRIER::UAV(specularHitDistanceTexture->resource.get())
			};

			commandList->ResourceBarrier(_countof(rtUAVBarrier), rtUAVBarrier);
		}

		if (settings.DebugOutput == DebugOutput::None) {
#ifdef DLSS_RR
			if (settings.Denoiser == Denoiser::DLSSRR) {
				{
					auto screenSize = globals::state->screenSize;
					auto renderSize = Util::ConvertToDynamic(screenSize);

					sl::Extent inputExtent{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
					sl::Extent outputExtent{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

					sl::Resource colorIn = { sl::ResourceType::eTex2d, outputTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
					sl::Resource colorOut = { sl::ResourceType::eTex2d, mainTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };  // This probably will break, we'll see...
					sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON };
					sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture->resource.get(), 0 };
					sl::Resource diffuseAlbedo = { sl::ResourceType::eTex2d, albedoTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource specularAlbedo = { sl::ResourceType::eTex2d, gbufferReflectanceTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource normalRoughness = { sl::ResourceType::eTex2d, normalRoughnessTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource specHitDistance = { sl::ResourceType::eTex2d, specularHitDistanceTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON };

					sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent };
					sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };
					sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag diffuseAlbedoTag = sl::ResourceTag{ &diffuseAlbedo, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specularAlbedoTag = sl::ResourceTag{ &specularAlbedo, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag normalRoughnessTag = sl::ResourceTag{ &normalRoughness, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specHitDistanceTag = sl::ResourceTag{ &specHitDistance, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };

					sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, diffuseAlbedoTag, specularAlbedoTag, normalRoughnessTag, specHitDistanceTag };
					if (SL_FAILED(result, slSetTag(slViewportHandle, resourceTags, _countof(resourceTags), commandList.get()))) {
						logger::error("[DLSS RR] Failed to set DLSS RR tags, error: {}", magic_enum::enum_name(result));
						return;
					}
				}

				const sl::BaseStructure* inputs[] = { &slViewportHandle };

				if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, inputs, _countof(inputs), commandList.get()))) {
					logger::error("[DLSS RR] Failed to evaluate DLSS RR feature, error: {}", magic_enum::enum_name(result));
				}
			} else {
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), outputTexture->resource.get());
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);		
			}
#endif
		} else {
			if (settings.DebugOutput == DebugOutput::Output) {
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), outputTexture->resource.get());
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			} else if (settings.DebugOutput == DebugOutput::Reflectance) {
				reflectanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), reflectanceTexture->resource.get());
				reflectanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			} else if (settings.DebugOutput == DebugOutput::SpecularHitDistance) {
				specularHitDistanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), specularHitDistanceTexture->resource.get());
				specularHitDistanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}

		}

		DX::ThrowIfFailed(commandList->Close());

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish
	{
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		// Wait until GPU is done with previous frame
		if (d3d12Fence->GetCompletedValue() < fenceValue) {
			DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
		}

		/*if (pixCapture && pixCaptureStarted) {
			ga->EndCapture();
			pixCapture = false;
			pixCaptureStarted = false;
		}*/

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	//New frame, reset
	{
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
	}

	/*if (pixCapture) {
		pixCaptureStarted = true;
		ga->BeginCapture();
	}*/

	//Release scratch buffers
	{
		asScratchBuffers.clear();
		rtGeometryDescs.clear();
		rtASDescs.clear();
	}

	d3d11Context->CopyResource(main.texture, mainTexture->resource11);

	// Clear specular for now, just so I can see the results better 
	/*{
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		d3d11Context->ClearRenderTargetView(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
	}*/
}

void RaytracedGI::RenderShadows()
{
	//logger::info("[RT] RenderShadows - ShadowLight [0x{:x}], TLAS [0x{:x}]", reinterpret_cast<uintptr_t>(shadowLight), reinterpret_cast<uintptr_t>(tlas.get()));

	if (!shadowLight || tlas == nullptr)
		return;

	auto rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto shadowMask = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];

	CopyDepth();

	// Tell DX11 to finish and wait
	//d3d11Context->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
	d3d11Context->Flush();
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	if (pixCapture) {
		pixCaptureStarted = true;
		ga->BeginCapture();
	}

	// Do DX12 work...
	{
		{
			shadowsCBData.CameraData = Util::GetCameraData();
			shadowsCBData.Size = globals::state->screenSize;

			auto eye = Util::GetCameraData(0);
			float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
			float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));
			shadowsCBData.NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

			shadowsCBData.ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

			float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
			shadowsCBData.Position = float3(cameraPosition.x, cameraPosition.y, cameraPosition.z);

			auto direction = Float3(shadowLight->GetShadowDirectionalLightRuntimeData().lightDirection);
			direction.Normalize();
			shadowsCBData.Direction = -direction;

			shadowsCB->Update(&shadowsCBData, sizeof(ShadowsCB));
			shadowsCB->Upload(commandList.get());
			shadowsCB->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		}

		// Build/Rebuild TLAS
		/*{
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
				.NumDescs = static_cast<uint>(blasShadowInstances.size()),
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = blasShadowInstanceBuffer->resource->GetGPUVirtualAddress()
			};

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
				.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
				.Inputs = inputs,
				.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress()
			};

			commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

			const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
			commandList->ResourceBarrier(1, &asBarrier);
		}*/

		commandList->SetPipelineState(pipelineCSShadows.get());
		commandList->SetComputeRootSignature(rootSignatureCSShadows.get());

		auto computeHeapPtr = computeHeapShadows->Heap();
		commandList->SetDescriptorHeaps(1, &computeHeapPtr);

		// UAV table
		commandList->SetComputeRootDescriptorTable(0, computeHeapShadows->TableGPUHandle(ComputeHeapShadowsType::UAV));

		// SRV table
		commandList->SetComputeRootDescriptorTable(1, computeHeapShadows->TableGPUHandle(ComputeHeapShadowsType::SRV));

		// Constant buffer
		commandList->SetComputeRootConstantBufferView(2, shadowsCB->resource->GetGPUVirtualAddress());

		CD3DX12_RESOURCE_BARRIER ctuBarrier[1] = {
			CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		commandList->ResourceBarrier(_countof(ctuBarrier), ctuBarrier);

		auto dispatchCount = Util::GetScreenDispatchCount();
		commandList->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		CD3DX12_RESOURCE_BARRIER utcBarrier[1] = {
			CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
		};
		commandList->ResourceBarrier(_countof(utcBarrier), utcBarrier);

		DX::ThrowIfFailed(commandList->Close());

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish and signal DX11
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

	// Wait for GPU
	if (d3d12Fence->GetCompletedValue() < fenceValue) {
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
	}

	if (pixCapture && pixCaptureStarted) {
		ga->EndCapture();
		pixCapture = false;
		pixCaptureStarted = false;
	}

	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	// Reset for next command list usage
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	d3d11Context->CopyResource(shadowMask.texture, shadowMaskTexture->resource11);
}

ID3D12Resource* RaytracedGI::MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* sratchSize, UINT64* updateScratchSize)
{
	auto makeBuffer = [&](UINT64 size, auto initialState) {
		auto desc = BASIC_BUFFER_DESC;
		desc.Width = size;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ID3D12Resource* buffer;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer)));
		return buffer;
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	if (sratchSize)
		*sratchSize = prebuildInfo.ScratchDataSizeInBytes;

	if (updateScratchSize)
		*updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

	winrt::com_ptr<ID3D12Resource> scratch = nullptr;
	scratch.attach(makeBuffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	auto buildDesc = eastl::make_unique<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>(
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC{
			.DestAccelerationStructureData = as->GetGPUVirtualAddress(),
			.Inputs = inputs,
			.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() 
		}
	);

	commandList->BuildRaytracingAccelerationStructure(buildDesc.get(), 0, nullptr);

	rtASDescs.push_back(eastl::move(buildDesc));

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(as);
	commandList->ResourceBarrier(1, &asBarrier);	

	asScratchBuffers.push_back(std::move(scratch));

	return as;
}

ID3D12Resource* RaytracedGI::MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices)
{
	eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs({ D3D12_RAYTRACING_GEOMETRY_DESC{
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {
			.Transform3x4 = 0,
			.IndexFormat = DXGI_FORMAT_R32_UINT,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = indices,
			.VertexCount = vertices,
			.IndexBuffer = indexBuffer->GetGPUVirtualAddress(),
			.VertexBuffer = {
				.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
				.StrideInBytes = sizeof(Vertex) 
			} 
		}	  
	 }});

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	rtGeometryDescs.push_back(eastl::move(geometryDescs));

	return MakeAccelerationStructure(inputs);
}

ID3D12Resource* RaytracedGI::MakeTLAS(ID3D12Resource* localInstances, UINT numInstances, UINT64* scratchSize, UINT64* updateScratchSize)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE, // | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = numInstances,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = localInstances->GetGPUVirtualAddress()
	};

	return MakeAccelerationStructure(inputs, scratchSize, updateScratchSize);
}

void RaytracedGI::Flush()
{
	static UINT64 value = 1;
	commandQueue->Signal(d3d12Fence.get(), value);
	d3d12Fence->SetEventOnCompletion(value++, nullptr);
}


void RaytracedGI::PostPostLoad()
{
	Hooks::Install();
	Initialize();

	//MenuOpenCloseEventHandler::Register();
	//TESLoadGameEventHandler::Register();
}

static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath)) {
		if (directory_entry.is_directory()) {
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str()) {
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty()) {
		// TODO: Error, no PIX installation found
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

void DumpDredBreadcrumbs(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1& breadcrumbsOutput)
{
	const D3D12_AUTO_BREADCRUMB_NODE1* pNode = breadcrumbsOutput.pHeadAutoBreadcrumbNode;

	while (pNode) {
		const UINT32 completedOps = *pNode->pLastBreadcrumbValue;
		const UINT32 totalOps = pNode->BreadcrumbCount;

		logger::error("[RT] Command List: {}", pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "<unnamed>");
		logger::error("[RT] Queue: {}", pNode->pCommandQueueDebugNameA ? pNode->pCommandQueueDebugNameA : "<unnamed>");
		logger::error("[RT] Completed Ops: {} / {}", completedOps, totalOps);

		if (pNode->pCommandHistory && totalOps > 0) {
			// Last executed command
			UINT32 lastIndex = (completedOps > 0) ? completedOps - 1 : 0;
			auto lastOp = pNode->pCommandHistory[lastIndex];
			logger::error("[RT] Last Executed Command: {}", magic_enum::enum_name(lastOp));

			// Next (likely faulting) command
			if (completedOps < totalOps) {
				auto nextOp = pNode->pCommandHistory[completedOps];
				logger::error("[RT] Next (Likely Faulting) Command: {}", magic_enum::enum_name(nextOp));
			}
		}

		logger::error("");  // empty line for readability
		pNode = pNode->pNext;
	}
}

void RaytracedGI::DeviceRemovedHandler()
{
	// 1. Device removed reason
	HRESULT reason = d3d12Device->GetDeviceRemovedReason();
	logger::error("[RT] ============================================================");
	logger::error("[RT] DEVICE REMOVED! HRESULT = 0x{:08X}", reason);

	winrt::com_ptr<ID3D12DeviceRemovedExtendedData1> dred;
	if (FAILED(d3d12Device->QueryInterface(IID_PPV_ARGS(&dred)))) {
		logger::error("[RT] DRED not available on this device.");
		return;
	}

	// ---------------------------------------------------------------------
	// 2. Auto Breadcrumbs
	// ---------------------------------------------------------------------
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bcOutput = {};
	if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&bcOutput)) && bcOutput.pHeadAutoBreadcrumbNode) {
		DumpDredBreadcrumbs(bcOutput);
	} else {
		logger::error("[RT] No breadcrumbs available.");
	}
}

void RaytracedGI::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			LoadLibrary(GetLatestWinPixGpuCapturerPath().c_str());
		}
	}

	logger::info("[RTGI] Creating D3D12 device");

	// Set Device
	DX::ThrowIfFailed(ppDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(pImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	// Create debug device
	if (!settings.EnablePIXCapture && settings.EnableDebugDevice)
	{
		winrt::com_ptr<ID3D12Debug6> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(TRUE);
		}

		winrt::com_ptr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}

	if (settings.EnablePIXCapture) {
		DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
	}

	// Create Device
	{
		DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device)));

		// Check hardware raytracing tier
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
			if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
				if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
					logger::info("Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
				else
					logger::warn("Hardware ray tracing not supported.");
			}		
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));

		DX::ThrowIfFailed(commandQueue->SetName(L"Command Queue"));
		DX::ThrowIfFailed(commandAllocator->SetName(L"Command Allocator"));
		DX::ThrowIfFailed(commandList->SetName(L"Command List"));

		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
		//DX::ThrowIfFailed(commandList->Close());
	}

	// Create Interop
	{
		HANDLE sharedFenceHandle;
		DX::ThrowIfFailed(d3d12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
		DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
		DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
		CloseHandle(sharedFenceHandle);
	}

	if (settings.EnableDebugDevice)
	{
		HANDLE disconnectEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(UINT64_MAX, disconnectEvent));

		std::thread([this, disconnectEvent]() {
			WaitForSingleObject(disconnectEvent, INFINITE);
			DeviceRemovedHandler();
		}).detach();
	}
}

void RaytracedGI::CreateRootSignature()
{
	// UAV range
	commonHeap->CreateTable(
		HeapType::UAV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 
		{  
			{ HeapSlot::Output, 1 }, 
			{ HeapSlot::Reflectance, 1 }, 
			{ HeapSlot::SpecularHitDist, 1 }
		});

	// Fixed SRV ranges (NormalRoughness + GNMD + Scene + Lights + Index map)
	commonHeap->CreateTable(
		HeapType::SRV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::Main, 1 },
			{ HeapSlot::Depth, 1 },
			{ HeapSlot::Albedo, 1 },
			{ HeapSlot::NormalRoughness, 1 },
			{ HeapSlot::GNMD, 1 },
			{ HeapSlot::TLAS, 1 },
			{ HeapSlot::SkyHemisphere, 1 },
			{ HeapSlot::Lights, 1 },
			{ HeapSlot::Instances, 1 }		
		});


	// Vertex buffers (unbounded)
	commonHeap->CreateTable(
		HeapType::VertexBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::Vertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE }
		});
	

	// Index buffers (unbounded)
	commonHeap->CreateTable(
		HeapType::TriangleBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
		{ 
			{ HeapSlot::Triangles, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});
	

	// Diffuse Textures (unbounded)
	commonHeap->CreateTable(
		HeapType::DiffuseTextures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ HeapSlot::DiffuseTextures, UINT_MAX, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE }	
		});

	// Glow Textures (unbounded)
	commonHeap->CreateTable(
		HeapType::GlowTextures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ HeapSlot::GlowTextures, UINT_MAX, 4, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});


	auto rootParameters = commonHeap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0); 
	rootParameters.push_back(constantRootParam);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);  // register s0

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			logger::error("[RT] D3DX12SerializeVersionedRootSignature {}", (char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	DX::ThrowIfFailed(rootSignature->SetName(L"RT Root Signature"));
}

void RaytracedGI::CreateComputeRootSignature()
{
	// UAV range
	/*computeHeap->CreateTable(
		ComputeHeapType::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ 
			{ ComputeHeapSlot::Final, 1 },
			{ ComputeHeapSlot::DiffuseGI, 1 },
			{ ComputeHeapSlot::SpecularGI, 1 },
			{ ComputeHeapSlot::SpecHitDist, 1 },
			{ ComputeHeapSlot::Depth, 1 }
		});

	// SRV range
	computeHeap->CreateTable(
		ComputeHeapType::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ ComputeHeapSlot::GNMD, 1 },
			{ ComputeHeapSlot::Albedo, 1 },
			{ ComputeHeapSlot::Reflectance, 1 }
		});

	auto rootParameters = computeHeap->GetRootParameters();

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig;
	winrt::com_ptr<ID3DBlob> errorBlob;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignatureCS.put())));
	DX::ThrowIfFailed(rootSignatureCS->SetName(L"Compute Root Signature"));*/
}

void RaytracedGI::CreateComputeRootSignatureShadows()
{
	// UAV range
	computeHeapShadows->CreateTable(
		ComputeHeapShadowsType::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ 
			{ ComputeHeapShadowsSlot::ShadowMask, 1 } 
		});

	// SRV
	computeHeapShadows->CreateTable(
		ComputeHeapShadowsType::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{	
			{ ComputeHeapShadowsSlot::Depth, 1 },
			{ ComputeHeapShadowsSlot::TLAS, 1 }
		});

	auto rootParameters = computeHeapShadows->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0);
	rootParameters.push_back(constantRootParam);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> serializedRootSig;
	winrt::com_ptr<ID3DBlob> errorBlob;

	DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(rootSignatureCSShadows.put())));
	DX::ThrowIfFailed(rootSignatureCSShadows->SetName(L"Compute Root Signature - Shadows"));
}

void RaytracedGI::Initialize()
{
	
}


void RaytracedGI::ClearShaderCache()
{
	copyDepthCS = nullptr;  // This is actually optional
	CompileShaders();
}

void RaytracedGI::CompileShaders()
{
	if (!rootSignature) {
		CreateRootSignature();
		CompileRaytracingShaders();
	}

	/*if (!rootSignatureCS) {
		CreateComputeRootSignature();
	}*/

	if (!rootSignatureCSShadows) {
		CreateComputeRootSignatureShadows();
		CompileDX12ComputeShaders();
	}

	CompileComputeShaders();
}

void RaytracedGI::CompileRaytracingShaders()
{
	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"Data/Shaders/RaytracedGI/Raytracing/RayGeneration.hlsl");

	winrt::com_ptr<IDxcBlob> diffuseMissBlob, diffuseClosestHitBlob;
	ShaderUtils::CompileShader(diffuseMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl");
	ShaderUtils::CompileShader(diffuseClosestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl");

	winrt::com_ptr<IDxcBlob> shadowMissBlob;
	ShaderUtils::CompileShader(shadowMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/ShadowMiss.hlsl");

	/*winrt::com_ptr<IDxcBlob> specularMissBlob, specularClosestHitBlob;
	ShaderUtils::CompileShader(specularMissBlob, L"Data/Shaders/RaytracedGI/Raytracing/Miss.hlsl", { { L"SPECULAR", nullptr } });
	ShaderUtils::CompileShader(specularClosestHitBlob, L"Data/Shaders/RaytracedGI/Raytracing/ClosestHit.hlsl", { { L"SPECULAR", nullptr } });*/
	
	DX12::RTPipelineBuilder pipelineBuilder;

	// Init pipeline
	{
		// Libraries
		pipelineBuilder.AddRayGenLib(rayGenBlob.get(), L"RayGeneration");

		pipelineBuilder.AddMissLib(diffuseMissBlob.get(), L"IndirectMiss");
		pipelineBuilder.AddMissLib(shadowMissBlob.get(), L"ShadowMiss");
		//pipelineBuilder.AddMissLib(specularMissBlob.get(), L"SpecularMiss");

		pipelineBuilder.AddHitLib(diffuseClosestHitBlob.get(), L"IndirectClosestHit");
		//pipelineBuilder.AddHitLib(specularClosestHitBlob.get(), L"SpecularClosestHit");

		// Hit groups
		pipelineBuilder.AddHitGroup(L"IndirectHitGroup", L"IndirectClosestHit");
		pipelineBuilder.AddHitGroup(L"ShadowHitGroup");
		//pipelineBuilder.AddHitGroup(L"SpecularHitGroup", L"SpecularClosestHit");

		// Shader + pipeline config
		pipelineBuilder.AddShaderConfig(20, 8);
		pipelineBuilder.AddGlobalRootSignature(rootSignature.get());
		pipelineBuilder.AddPipelineConfig(6);

		auto desc = pipelineBuilder.MakeStateObjectDesc();
		HRESULT hr = d3d12Device->CreateStateObject(desc, IID_PPV_ARGS(&pipelineRT));

		if (FAILED(hr)) {
			logger::error("CreateStateObject failed: {}", hr);
		}

		DX::ThrowIfFailed(hr);

		DX::ThrowIfFailed(pipelineRT->SetName(L"RT Pipeline"));
	}

	// Init shader tables
	{
		winrt::com_ptr<ID3D12StateObjectProperties> props;
		pipelineRT->QueryInterface(props.put());

		shaderBindingTable = eastl::make_unique<DX12::ShaderBindingTable>(pipelineBuilder.CreateShaderBindingTable(props.get()));

		auto shaderBindingTableSize = shaderBindingTable->GetTotalSize();
		logger::info("[RT] Shader Binding Table size: {}", shaderBindingTableSize);

		shaderBindingTableBuffer = eastl::make_unique<DX12::ResourceUpload>(d3d12Device.get(), shaderBindingTableSize);

		std::vector<uint8_t> shaderBindingTableCPU(shaderBindingTableSize);
		shaderBindingTable->Build(shaderBindingTableCPU.data());

		shaderBindingTable->LogShaderBindingTable(shaderBindingTableBuffer->resource->GetGPUVirtualAddress());

		shaderBindingTableBuffer->Update(shaderBindingTableCPU.data(), shaderBindingTableSize);
		shaderBindingTableBuffer->Upload(commandList.get());
		shaderBindingTableBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
}

void RaytracedGI::CompileDX12ComputeShaders()
{
	winrt::com_ptr<IDxcBlob> shaderBlob;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/RaytracedGI/RTShadowsCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = rootSignatureCSShadows.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(d3d12Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineCSShadows.put())));
	DX::ThrowIfFailed(pipelineCSShadows->SetName(L"Compute Pipeline - Shadows"));

	/*{
		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/RaytracedGI/CompositeCS.hlsl", {}, L"cs_6_3");

		D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
		computeDesc.pRootSignature = rootSignatureCS.get();
		computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

		DX::ThrowIfFailed(d3d12Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineCS.put())));
		DX::ThrowIfFailed(pipelineCS->SetName(L"Compute Pipeline"));
	}*/
}

void RaytracedGI::CompileComputeShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\RaytracedGI\\CopyDepthCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		copyDepthCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\RaytracedGI\\CubeToHemiCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		cubeToHemiCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\RaytracedGI\\ConvertNormalGlossCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		convertNormalGlossCS.attach(rawPtr);
}

RE::BSEventNotifyControl RaytracedGI::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a loadscreen
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		//auto& rtgi = globals::features::raytracedGI;

		logger::info("MenuOpenCloseEventHandler::ProcessEvent - Opening: {}", a_event->opening);

		if (a_event->opening) {			
			//rtgi.meshesCreated = false;		
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl RaytracedGI::TESLoadGameEventHandler::ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*)
{
	logger::info("TESLoadGameEventHandler::ProcessEvent {}", reinterpret_cast<intptr_t>(a_event));

	return RE::BSEventNotifyControl::kContinue;
}
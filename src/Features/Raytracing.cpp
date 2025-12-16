#include "Raytracing.h"
#include "InverseSquareLighting.h"
#include "TerrainBlending.h"

#include "Globals.h"
#include "State.h"
#include "Raytracing/ShaderUtils.h"
#include "ShaderCache.h"

#include <filesystem>
#include <shlobj.h>
#include <windows.h>
#include "Deferred.h"

#include "Utils/PerfUtils.h"

#include "Menu.h"

#include "Features/CloudShadows.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/HairSpecular.h"
#include "Features/WetnessEffects.h"

#ifdef DLSS_RR
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	Enabled,
	GlobalIllumination,
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
	RaytracedShadows,
	PathTracing,
	CullShadows,
	RecompressTextures,
	DLSSRRQualityMode,
	PerformanceOverlay,
	DebugOutput,
	EnablePIXCapture,
	PIXCaptureLocation,
	EnableDebugDevice)
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	Enabled,
	GlobalIllumination,
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
	RaytracedShadows,
	PathTracing,
	CullShadows,
	RecompressTextures,
	PerformanceOverlay,
	DebugOutput,
	EnablePIXCapture,
	PIXCaptureLocation,
	EnableDebugDevice)
#endif


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

void DrawFloat2(const char* label, float2& v, float min = 0.0f, float max = 1.0f)
{
	float floats[2] = { v.x, v.y };
	if (ImGui::SliderFloat2(label, floats, min, max)) {
		v = { floats[0], floats[1] };
		v.Clamp({ min, min }, { max, max });
	}
}

void Raytracing::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Ray-Traced Global Illumination.");
	}

	ImGui::Checkbox("Global Illumination", &settings.GlobalIllumination);

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

			ImGui::Checkbox("Cull Shadows", &settings.CullShadows);

			ImGui::TreePop();
		}
	}

	ImGui::Checkbox("Path Tracing", &settings.PathTracing);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Experimental Path Tracing mode.\n");
	}

	ImGui::Checkbox("Recompress Textures", &settings.RecompressTextures);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Some texture formats cannot be shared between APIs, enabling this option ensures they'll be recompressed in a lower quality yet compatible format.\n");
	}

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
	ImGui::Checkbox("SHaRC", &settings.SHaRC);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enables Spatially Hashed Radiance Cache, a technique aimed at improving signal quality and performance in the context of path tracing.");
	}

	ImGui::DragFloat("SHaRC Scale", &settings.SHaRCScale, 0.001f, 0.1f, 10.0f);
	settings.SHaRCScale = std::clamp(settings.SHaRCScale, 0.1f, 10.0f);
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

	ImGui::Checkbox("Enable PIX Capture", &settings.EnablePIXCapture);

	if (settings.EnablePIXCapture)
	{
		if (ImGui::TreeNodeEx("Pix Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
			//Pix Capture Location
			{
				int pixCapLocation = static_cast<int32_t>(settings.PIXCaptureLocation);
				ImGui::TextUnformatted("PIX Capture");

				ImGui::SameLine();
				ImGui::Dummy(ImVec2(25, 0));

				for (auto& [value, name] : magic_enum::enum_entries<PIXCaptureLocation>()) {
					ImGui::SameLine();
					ImGui::RadioButton(name.data(), &pixCapLocation, static_cast<int32_t>(value));
				}

				settings.PIXCaptureLocation = static_cast<PIXCaptureLocation>(pixCapLocation);
			}

			if (ImGui::Button("Single Frame Capture")) {
				pixCapture = true;
				pixCaptureStarted = false;
			}

			if (ImGui::Button("Start MultiFrame Capture")) {
				pixCapture = true;
				pixCaptureStarted = false;
				pixMultiFrame = true;
			}

			if (pixCapture && pixCaptureStarted && pixMultiFrame && ImGui::Button("End MultiFrame Capture")) {
				pixMultiFrame = false;
			}

			if (ImGui::Button("Start TRD Capture")) {
				pixCapture = true;
				pixCaptureStarted = false;
				pixTDR = true;
			}

			ImGui::TreePop();
		}
	}

	ImGui::Checkbox("Enabled Debug Device", &settings.EnableDebugDevice);

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Lights: {}", lights.size()).c_str());

		ImGui::Text(std::format("Used Textures: {}, Shared: {}", textureRegisters.UsedCount(), sharedTextures.size()).c_str());
		ImGui::Text(std::format("Used Shapes: {}", shapeRegisters.UsedCount()).c_str());
		ImGui::Text(std::format("Models: {}", models.size()).c_str());

		auto instanceCount = instances.size();
		ImGui::Text(std::format("Instances: {}", instanceCount).c_str());

		if (settings.GlobalIllumination) {
			auto blasInstancesCount = blasInstances.size();
			ImGui::Text(std::format("GI Unculled: {}, Culled: {}", blasInstancesCount, instanceCount - blasInstancesCount).c_str());
		}

		if (settings.RaytracedShadows) {
			auto blasInstancesCount = blasShadowInstances.size();
			ImGui::Text(std::format("Shadow Unculled: {}, Culled: {}", blasInstancesCount, instanceCount - blasInstancesCount).c_str());
		}

		ImGui::TreePop();
	}

	//ImGui::Image(skyHemisphere->srv, { 64.0f, 64.0f });
}

void Raytracing::DrawOverlay()
{
	auto* menu = Menu::GetSingleton();

	if (!globals::state || !menu)
		return;

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!menu->IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	ImGui::Begin("Raytracing Overlay", NULL, windowFlags);

	auto DrawRow = [](const char* label, size_t instances, float ms, [[maybe_unused]] double frameTime = 0.0f) {
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::Text(label);

		ImGui::TableNextColumn();
		ImGui::Text("%d", instances);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", ms);
	};
	
	if (ImGui::BeginTable("Effects", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		if (settings.RaytracedShadows)
			DrawRow("Shadows", blasShadowInstances.size(), shadowsTime);

		// GI/PT
		DrawRow(settings.PathTracing ? "Path Tracing" : "Global Illumination", blasInstances.size(), mainTime);

		// Denoiser
		//DrawRow(settings.PathTracing ? "Denoiser", blasInstances.size(), 0);

		ImGui::EndTable();
	}

	ImGui::End();
}

void Raytracing::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	skinningHeap = eastl::make_unique<DX12::DescriptorHeap<SkinningHeap>>(
		d3d12Device.get(),
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SkinningHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	giHeap = eastl::make_unique<DX12::DescriptorHeap<GIHeap>>(
		d3d12Device.get(), 
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GIHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	shadowHeap = eastl::make_unique<DX12::DescriptorHeap<ShadowsHeap>>(
		d3d12Device.get(),
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ShadowsHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	// Setup default textures (this is a bit wordy...)
	{
		uint8_t white[] = { 255u, 255u, 255u, 255u };
		uint8_t normal[] = { 128u, 128u, 255u, 255u };
		uint8_t black[] = { 0u, 0u, 0u, 0u };
		uint8_t rmaos[] = { 128u, 0u, 255u, 10u };

		defaultWhiteTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultNormalTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultBlackTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultRMAOSTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());

		defaultWhiteTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultNormalTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultBlackTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultRMAOSTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);

		defaultWhiteTexture->UpdateAndUpload(commandList.get(), white);
		defaultNormalTexture->UpdateAndUpload(commandList.get(), normal);
		defaultBlackTexture->UpdateAndUpload(commandList.get(), black);
		defaultRMAOSTexture->UpdateAndUpload(commandList.get(), rmaos);
	}

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

		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::Depth));
		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::Depth));	
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

		d3d12Device->CreateUnorderedAccessView(shadowMaskTexture->resource.get(), nullptr, &uavDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::ShadowMask));	
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

			d3d12Device->CreateUnorderedAccessView(mainTexture->resource.get(), nullptr, &uavDesc, giHeap->CPUHandle(GIHeap::Slot::Main));
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

			outputTexture->CreateUAV(giHeap->CPUHandle(GIHeap::Slot::Output));
		}

		// u2 - Reflectance texture
		{				
			reflectanceTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			reflectanceTexture->SetName(L"Reflectance Texture");

			reflectanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			reflectanceTexture->CreateUAV(giHeap->CPUHandle(GIHeap::Slot::Reflectance));
		}

		// u3 - Specular Hit Distance texture
		{
			specularHitDistanceTexture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), mainDesc.Width, mainDesc.Height, DXGI_FORMAT_R16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			specularHitDistanceTexture->SetName(L"Specular Hit Distance Texture");

			specularHitDistanceTexture->CreateUAV(giHeap->CPUHandle(GIHeap::Slot::SpecularHitDist));
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

			d3d12Device->CreateShaderResourceView(normalRoughnessTexture->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::NormalRoughness));
		}
	}

	// t3 - Light buffer
	{
		lightBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Light>>(d3d12Device.get(), MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->resource->SetName(L"Light Buffer"));

		lightBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Lights));
	}

	// t4 - Material buffer
	{
		materialBuffer = eastl::make_unique<DX12::StructuredBufferUpload<MaterialData>>(d3d12Device.get(), MAX_MATERIALS);
		DX::ThrowIfFailed(materialBuffer->resource->SetName(L"Material Buffer"));

		materialBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Materials));
	}

	// t5 - Instance buffer
	{
		instanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<InstanceData>>(d3d12Device.get(), MAX_INSTANCES);
		DX::ThrowIfFailed(instanceBuffer->resource->SetName(L"Instance Buffer"));

		instanceBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Instances));
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
		frameBuffer = eastl::make_unique<DX12::StructuredBufferUpload<GIFrameData>>(d3d12Device.get(), 1);
		frameBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(frameBuffer->resource->SetName(L"Frame Buffer"));

		frameBufferData = eastl::make_unique<GIFrameData>();

		shadowsCB = eastl::make_unique<DX12::StructuredBufferUpload<ShadowsFrameData>>(d3d12Device.get(), 1);
		shadowsCB->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(shadowsCB->resource->SetName(L"Shadows Constant Buffer"));

		shadowsCBData = eastl::make_unique<ShadowsFrameData>();
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

		d3d12Device->CreateShaderResourceView(skyHemisphere->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::SkyHemisphere));
	}

	// Skinning
	{
		vertexUpdateBuffer = eastl::make_unique<DX12::StructuredBufferUpload<VertexUpdateData>>(d3d12Device.get(), MAX_MODELS);
		DX::ThrowIfFailed(vertexUpdateBuffer->resource->SetName(L"Vertex Update Buffer"));

		vertexUpdateBuffer->CreateSRV(skinningHeap->CPUHandle(SkinningHeap::Slot::UpdateData));
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
void Raytracing::InitRR()
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

int32_t Raytracing::GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
float Raytracing::Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void Raytracing::GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

float2 Raytracing::GetInputResolutionScaleRR(uint32_t outputWidth, uint32_t outputHeight)
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

sl::DLSSMode Raytracing::GetDLSSMode()
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

void Raytracing::SetDLSSRROptions()
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

void Raytracing::CheckFrameConstants()
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

void Raytracing::ShareRT(ID3D11Texture2D* pTexture2D, const GIHeap::Slot& target, const ShadowsHeap::Slot& cTarget, ID3D12Resource** ppResource) const
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

	if (target != GIHeap::Slot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, giHeap->CPUHandle(target));

	if (cTarget != ShadowsHeap::Slot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, shadowHeap->CPUHandle(cTarget));
}

void Raytracing::SetupSharedRT()
{
	const auto& rendererRD = globals::game::renderer->GetRuntimeData();

	ShareRT(rendererRD.renderTargets[ALBEDO].texture, GIHeap::Slot::Albedo, ShadowsHeap::Slot::None, albedoTexture.put());
	ShareRT(rendererRD.renderTargets[REFLECTANCE].texture, GIHeap::Slot::None, ShadowsHeap::Slot::None, gbufferReflectanceTexture.put());
	//ShareRT(rendererRD.renderTargets[NORMALROUGHNESS].texture, HeapSlot::NormalRoughness, ComputeHeapSlot::None, normalRoughnessTexture.put());
	ShareRT(rendererRD.renderTargets[MASKS2].texture, GIHeap::Slot::GNMD, ShadowsHeap::Slot::None, GNMDTexture.put());  // GNMD

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

eastl::vector<LightLimitFix::LightData> Raytracing::GetPointLights()
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

float3 Raytracing::GammaToLinear(float3 color)
{
	if (settings.GammaToLinear) {
		float3 colorAbs = DirectX::XMVectorAbs(color);
		return float3(pow(colorAbs.x, 1.6f), pow(colorAbs.y, 1.6f), pow(colorAbs.z, 1.6f));
	} else {
		return color;
	}
}

void Raytracing::UpdateLights() 
{
	if (!renderingWorld || lightsUpdated)
		return;

	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto direction = Float3(dirLight->GetWorldDirection());
		direction.Normalize();

		auto& diffuse = dirLight->GetLightRuntimeData().diffuse;

		frameBufferData->Directional.Vector = -direction;
		frameBufferData->Directional.Color = GammaToLinear(float3(diffuse.red, diffuse.green, diffuse.blue)) * settings.Directional;  // * ( Util::IsInterior() ? 0.0f : 1.0f );
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

		if (!lights.empty())
			lightBuffer->UpdateList(lights.data(), lights.size());
	}

	lightsUpdated = true;
}

static DirectX::XMFLOAT3X4 GetXMF3X4FromNiTransform(const RE::NiTransform& Transform)
{
	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	return { 
		m.entry[0][0] * scale, m.entry[1][0] * scale, m.entry[2][0] * scale,
		m.entry[0][1] * scale, m.entry[1][1] * scale, m.entry[2][1] * scale,
		m.entry[0][2] * scale, m.entry[1][2] * scale, m.entry[2][2] * scale,
		Transform.translate.x, Transform.translate.y, Transform.translate.z
	};
}

void Raytracing::CopyDepth()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& tb = globals::features::terrainBlending;

	if (tb.loaded) {
		context->CopyResource(depthTexture->resource11, tb.blendedDepthTexture->resource.get());
	} 
	else
	{
		auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];  // kMAIN kPOST_ZPREPASS_COPY

		context->CSSetShader(copyDepthCS.get(), nullptr, 0);

		context->CSSetShaderResources(0, 1, &depth.depthSRV);

		context->CSSetUnorderedAccessViews(0, 1, &depthTexture->uav, nullptr);

		auto dispatchCount = Util::GetScreenDispatchCount();
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		context->CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
	}
}

void Raytracing::ConvertNormalGlossiness()
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

	//context->CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
}

void Raytracing::SkyCubeToHemi()
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

	//context->CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
}

void Raytracing::Main_RenderWorld(bool a1)
{
	if (Active()) {
		renderingWorld = true;
		lightsUpdated = false;

		SkyCubeToHemi();
	}

	Hooks::Main_RenderWorld::func(a1);

	if (Active()) {
		renderingWorld = false;
	}
}

RE::BSFadeNode* FindBSFadeNode(RE::NiNode* a_niNode)
{
	if (auto fadeNode = a_niNode->AsFadeNode()) {
		return fadeNode;
	}
	return a_niNode->parent ? FindBSFadeNode(a_niNode->parent) : nullptr;
}

template <typename T>
void Raytracing::MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res)
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

void Raytracing::CommitModel(Model& model)
{
	auto& shapes = model.shapes;
	auto meshCount = shapes.size();

	eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(meshCount);

	auto flags = Flags::None;

	for (auto i = 0; i < meshCount; i++) {
		auto& shape = shapes[i];

		flags |= shape->flags;

		geometryDescs[i] = {
			.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
			.Flags = shape->flags & Flags::Alpha ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
			.Triangles = {
				.Transform3x4 = 0,
				.IndexFormat = DXGI_FORMAT_R16_UINT,
				.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
				.IndexCount = shape->triangleCount * 3,
				.VertexCount = shape->vertexCount,
				.IndexBuffer = shape->triangleBuffer->resource->GetGPUVirtualAddress(),
				.VertexBuffer = {
					.StartAddress = shape->vertexBuffer->resource->GetGPUVirtualAddress(),
					.StrideInBytes = sizeof(Vertex) 
				} 
			} 
		};
	}

	bool updatable = (flags & Flags::Skinned) || (flags & Flags::Dynamic);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | (updatable ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION),
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	D3D12_RESOURCE_DESC desc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = prebuildInfo.ScratchDataSizeInBytes,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	};

	winrt::com_ptr<ID3D12Resource> scratch = nullptr;
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&scratch)));

	desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&model.blasBuffer)));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = model.blasBuffer->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() 
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(model.blasBuffer.get());
	commandList->ResourceBarrier(1, &asBarrier);

	if (updatable)
		model.blasScratchBuffer = std::move(scratch);
	else
		tempGPUData.emplace_back(std::move(scratch), fenceValue);
}

void Raytracing::UpdateModelBLAS(Model& model)
{
	auto& shapes = model.shapes;
	auto shapeCount = shapes.size();

	eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(shapeCount);


	for (auto i = 0; i < shapeCount; i++) {
		auto& shape = shapes[i];

		geometryDescs[i] = {
			.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
			.Flags = shape->flags & Flags::Alpha ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
			.Triangles = {
				.Transform3x4 = 0,
				.IndexFormat = DXGI_FORMAT_R16_UINT,
				.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
				.IndexCount = shape->triangleCount * 3,
				.VertexCount = shape->vertexCount,
				.IndexBuffer = shape->triangleBuffer->resource->GetGPUVirtualAddress(),
				.VertexBuffer = {
					.StartAddress = shape->vertexBuffer->resource->GetGPUVirtualAddress(),
					.StrideInBytes = sizeof(Vertex) 
				} 
			}
		};
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};


	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = model.blasBuffer->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.SourceAccelerationStructureData = model.blasBuffer->GetGPUVirtualAddress(),
		.ScratchAccelerationStructureData = model.blasScratchBuffer->GetGPUVirtualAddress()
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
}

void Raytracing::CreateModel(const char* path, RE::NiNode* pRoot)
{
	if (!pRoot) {
		logger::error("[RT] CreateModel \"{}\" - nullptr root", path);
		return;
	}

	logger::info("[RT] CreateModel \"{}\"", typeid(*pRoot).name());

	if (!path) {
		logger::debug("[RT] CreateModel \"{}\" - Invalid Path", pRoot->name);
		return;
	}

	if (strlen(path) == 0) {
		logger::debug("[RT] CreateModel \"{}\" - Empty Path", pRoot->name);
		return;
	}

	const auto* bsxFlags = pRoot->GetExtraData<RE::BSXFlags>("BSX");

	if (bsxFlags) {
		if (static_cast<int32_t>(bsxFlags->value) & static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker))
			return;
		
		//logger::info("[RT] CreateModel - BSX Flags [0x{:x}]: {}", bsxFlags->value, GetFlagsString<RE::BSXFlags::Flag>(static_cast<uint32_t>(bsxFlags->value)));
	}

	// We only need one buffer per model
	if (models.find(path) != models.end()) {
		AddInstance(pRoot, path);
		return;
	}

	//std::lock_guard lock{ renderMutex };

	logger::debug("[RT] CreateModel - Path: {}, NiNode [0x{:X}]: {}", path, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	auto rootWorldInverse = pRoot->world.Invert();

	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	RE::BSVisit::TraverseScenegraphGeometries(pRoot, [&](RE::BSGeometry* pGeometry) -> RE::BSVisit::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape)) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", magic_enum::enum_name(geometryType.get()), name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		// Early workaround since Land cause(ed?)s DX12 Device removal (why?)
		if (strcmp(name, "Land") == 0) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Is Land");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		const auto& geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

		auto& effect = geometryRuntimeData.properties[RE::BSGeometry::States::kEffect];

		auto* lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect.get());

		// Only lighting shader for now
		if (!lightingShader) {
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool skinned = lightingShader->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);

		auto& geomFlags = pGeometry->GetFlags();

		if (geomFlags.any(RE::NiAVObject::Flag::kHidden) && !skinned) {
			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Is Hidden");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto flags = Flags::None;

		if (geometryType.all(RE::BSGeometry::Type::kDynamicTriShape))
			flags |= Flags::Dynamic;

		auto localToRoot = GetXMFromNiTransform(rootWorldInverse * pGeometry->world);
		
		if (auto* triShapeRD = geometryRuntimeData.rendererData) { // Non-Skinned
			auto* pTriShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

			const auto& triShapeRuntime = pTriShape->GetTrishapeRuntimeData();

			auto meshData = eastl::make_unique<Shape>(shapeRegisters.Allocate(), pGeometry, flags);

			meshData->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0, localToRoot);
			meshData->BuildMaterial(geometryRuntimeData, name);
			meshData->CreateBuffers(ToWide(name));

			shapes.push_back(eastl::move(meshData));
		} else if (auto* skinInstance = (RE::BSDismemberSkinInstance*)geometryRuntimeData.skinInstance.get()) {  // Skinned
			/*static REL::Relocation<const RE::NiRTTI*> bsDismemberedSkinInstanceRTTI{ RE::BSDismemberSkinInstance::Ni_RTTI };
			bool isDismembered = skinInstance->GetRTTI()->IsKindOf(bsDismemberedSkinInstanceRTTI.get());

			if (isDismembered)
				logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Is dismembered");*/
			
			auto& skinPartition = skinInstance->skinPartition;
	
			if (!skinPartition) {
				logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Invalid SkinPartition");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Partitions: {}, VertexCount: {}, Unk24: [0x{:X}]", skinPartition->numPartitions, skinPartition->vertexCount, skinPartition->unk24);

			for (auto& partition : skinPartition->partitions) {
				auto meshData = eastl::make_unique<Shape>(shapeRegisters.Allocate(), pGeometry, flags | Flags::Skinned);

				meshData->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex, localToRoot);
				meshData->BuildMaterial(geometryRuntimeData, name);
				meshData->CreateBuffers(ToWide(name));

				shapes.push_back(eastl::move(meshData));
			}

			/*auto* rootParent = skinInstance->rootParent;
			auto* bones = skinInstance->bones;
			auto* boneWorldTransforms = skinInstance->boneWorldTransforms;

			auto& numMatrices = skinInstance->numMatrices;
			auto* boneMatrices = skinInstance->boneMatrices;*/
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	if (auto shapeCount = shapes.size(); shapeCount > 0) {
		eastl::string modelKey = path;

		auto model = Model(shapes);

		// Models with these flags cannot be instanced directly
		if ((model.GetFlags() & Flags::Dynamic) || (model.GetFlags() & Flags::Skinned))
			modelKey.append(std::format("_{:8X}", reinterpret_cast<uintptr_t>(pRoot)).c_str());

		auto [it, emplaced] = models.emplace(modelKey, eastl::move(model));

		if (emplaced) {
			CommitModel(it->second);
			AddInstance(pRoot, modelKey);

			logger::debug("[RT] CreateModel - Commited {} TriShapes", shapeCount);
		} else {
			logger::warn("[RT] CreateModel - Emplace failed for {} TriShapes", shapeCount);
		}
	} else {
		logger::debug("[RT] CreateModel - No TriShapes to commit");
	}
}

void Raytracing::RemoveInstance(RE::NiNode* pRoot, bool releaseModel)
{
	if (auto instanceIt = instances.find(pRoot); instanceIt != instances.end())
	{
		auto& instance = instanceIt->second;

		if (auto modelIt = models.find(instance.filename); modelIt != models.end()) {
			auto& model = modelIt->second;

			// If this is the last Instance of the model, remove it
			if (model.Release() && releaseModel) {
				logger::debug("[RT] RemoveInstance - No refs, erasing from collection");
				models.erase(modelIt);
			}
		}

		logger::debug("[RT] RemoveInstance - {}", pRoot->name);

		instances.erase(instanceIt);
	}
}

eastl::shared_ptr<Allocation> Raytracing::GetTextureRegister(ID3D11Texture2D* dx11Texture, eastl::shared_ptr<Allocation> defaultTexture)
{
	// Texture already placed in heap, return allocation
	if (auto refIt = textures.find(dx11Texture); refIt != textures.end()) {
		return refIt->second.registerIndex;
	} 

	// Search for texture in shared map
	if (auto sharedIt = sharedTextures.find(dx11Texture); sharedIt != sharedTextures.end()) {
		// Texture not in heap, so create SRV at next available heap slot
		auto dx12Texture = sharedIt->second.get();

		D3D12_RESOURCE_DESC texResDesc = dx12Texture->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
		texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		texSrvDesc.Format = texResDesc.Format;
		texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		texSrvDesc.Texture2D.MostDetailedMip = 0;
		texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
		texSrvDesc.Texture2D.PlaneSlice = 0;
		texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		auto [it, emplaced] = textures.emplace(dx11Texture, TextureReference(dx12Texture, { textureRegisters.Allocate(), AllocationDeleter() }));

		d3d12Device->CreateShaderResourceView(dx12Texture, &texSrvDesc, giHeap->CPUHandle(GIHeap::Slot::Textures, it->second.registerIndex->GetIndex()));

		return it->second.registerIndex;
	}

	logger::debug("[RT] GetTextureRegister - Source texture not found");

	return defaultTexture;
}

void Raytracing::AddInstance(RE::NiNode* pNiNode, eastl::string path)
{
	logger::debug("[RT] AddInstance - {}, Path: {}", pNiNode->name, path);

	//std::lock_guard lock{ geometryMutex };

	if (auto instanceIt = instances.find(pNiNode); instanceIt == instances.end()) {
		if (auto modelIt = models.find(path); modelIt != models.end()) {
			
			auto [it, emplaced] = instances.try_emplace(pNiNode, Instance(path));

			if (emplaced)
				modelIt->second.AddRef();
		}
	}
}

void Raytracing::UpdateDynamicSkinning(ID3D12GraphicsCommandList4* pCommandList) {
	if (vertexUpdate.empty())
		return;

	auto updateCount = vertexUpdate.size();

	eastl::vector<VertexUpdateData> vertexUpdateData;
	vertexUpdateData.reserve(updateCount);

	// Reset vertices (having another buffer and just reading from it in shaders might be better)
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			vertexUpdateData.emplace_back(item.registerIndex, item.flags, item.vertexCount, 0);

			if (item.flags & Flags::Skinned) {
				barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_COPY_DEST));
			}
		}

		if (!barriers.empty()) {
			pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());

			for (auto& item : vertexUpdate) {
				if (item.flags & Flags::Skinned) {
					pCommandList->CopyResource(item.vertexBuffer->resource.get(), item.vertexBuffer->uploadBuffer.get());				
				}
			}
		}
	}

	vertexUpdateBuffer->UpdateList(vertexUpdateData.data(), vertexUpdateData.size());
	vertexUpdateBuffer->Upload(pCommandList);

	pCommandList->SetPipelineState(skinningPipeline.get());
	pCommandList->SetComputeRootSignature(skinningRS.get());

	auto computeHeapPtr = skinningHeap->Heap();
	pCommandList->SetDescriptorHeaps(1, &computeHeapPtr);

	pCommandList->SetComputeRootDescriptorTable(0, skinningHeap->TableGPUHandle(SkinningHeap::Table::UAV));

	pCommandList->SetComputeRootDescriptorTable(1, skinningHeap->TableGPUHandle(SkinningHeap::Table::SRV));
	
	pCommandList->SetComputeRootDescriptorTable(2, skinningHeap->TableGPUHandle(SkinningHeap::Table::DynamicBuffer));

	pCommandList->SetComputeRootDescriptorTable(3, skinningHeap->TableGPUHandle(SkinningHeap::Table::SkinningBuffer));

	// Constant buffer
	//pCommandList->SetComputeRootConstantBufferView(2, shadowsCB->resource->GetGPUVirtualAddress());

	// Transition to Unordered Access
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		}

		pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());
	}

	// Dispatch our GPU vertex update
	/*auto dispatchCount = static_cast<uint32_t>(ceil(updateCount / 16.0f));
	pCommandList->Dispatch(dispatchCount, 1, 1);*/

	// Transition back to non-pixel shader resource
	{
		eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.reserve(updateCount);

		for (auto& item : vertexUpdate) {
			barriers.push_back(item.vertexBuffer->GetTransitionBarrier(true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}

		pCommandList->ResourceBarrier((uint32_t)barriers.size(), barriers.data());
	}

	auto blasUpdateCount = (uint)modelUpdate.size();

	eastl::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
	uavBarriers.reserve(blasUpdateCount);

	for (auto& path : modelUpdate) {
		if (auto modelIt = models.find(path); modelIt != models.end()) {
			auto& model = modelIt->second;

			UpdateModelBLAS(model);

			uavBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(model.blasBuffer.get()));
		}
	}

	commandList->ResourceBarrier(blasUpdateCount, uavBarriers.data());

	vertexUpdate.clear();
	modelUpdate.clear();
}

eastl::vector<size_t> Raytracing::GatherInstanceLights(RE::NiNode* pNiNode)
{
	eastl::vector<size_t> instanceLights;

	float3 center = Float3(pNiNode->worldBound.center);
	float radius = pNiNode->worldBound.radius;

	for (size_t i = 0; i < lights.size(); i++) {
		const Light& light = lights[i];

		if ((center - light.Vector).Length() <= radius + light.Range)
			instanceLights.push_back(i);
	}

	return instanceLights;
}

static RE::NiCamera* FindNiCamera(RE::NiAVObject* object)
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

void Raytracing::AddInstances()
{
	/*RE::BSVisit::TraverseScenegraphObjects(pRoot, [&](RE::NiAVObject* pObject) -> RE::BSVisit::BSVisitControl {

		return RE::BSVisit::BSVisitControl::kContinue;
	});*/
}

void Raytracing::ClearInstances()
{
	instances.clear();
}

void Raytracing::UpdateInstances()
{
	//std::lock_guard lock{ geometryMutex };

	instanceBufferData.clear();
	instanceBufferData.reserve(instances.size());

	blasInstances.clear();
	blasInstances.reserve(instances.size());

	auto* playerCamera = RE::PlayerCamera::GetSingleton();
	auto* tesCamera = playerCamera->currentState->camera;

	RE::NiCamera* camera = FindNiCamera(tesCamera->cameraRoot.get());

	auto eye = Util::GetAverageEyePosition();

	for (auto& [pNiNode, instance] : instances) {
		if (blasInstances.size() > MAX_INSTANCES)
			break;

		auto it = models.find(instance.filename);

		// Model was erased but not the instance
		if (it == models.end())
			continue;

		auto& model = it->second;

		auto worldBound = pNiNode->worldBound;

		float worldBoundRadius = worldBound.radius;
		float distanceToBounds = Util::Units::GameUnitsToMeters(eye.GetDistance(worldBound.center) - worldBoundRadius);

		// TODO: Fix culling exclusion for emissive models
		// Both may glow, if the object is a light source and behind the camera culling it will darken the visible scene (a better filter instead of shader types might be using their pixelDescriptor)
		//auto cullOutOfView = !geometryData.HasFeature(RE::BSShaderMaterial::Feature::kGlowMap) && !geometryData.HasShaderType(RE::BSShader::Type::Effect);
		
		auto cullOutOfView = true;

		// We'll cull small models or very distant ones (that are outside the player view)
		if ((cullOutOfView && Util::Units::GameUnitsToMeters(worldBoundRadius) < 1.0f) || distanceToBounds > 100.0f) {
			//if (!RE::NiCamera::BoundInFrustum(worldBound, camera))
			if (!camera->NodeInFrustum(pNiNode))
				continue;
		}

		if (!instance.Update(pNiNode, { it->first, model }))
			return;

		auto firstShapeIndex = model.shapes[0]->registerIndex->GetIndex();

		//logger::info("[RT] UpdateInstances - InstanceID: {}", firstShapeIndex);

		D3D12_RAYTRACING_INSTANCE_DESC blasInstance = {
			.InstanceID = firstShapeIndex,
			.InstanceMask = 1,
			.AccelerationStructure = model.blasBuffer->GetGPUVirtualAddress()
		};

		// Copy transform matrix from Instance to DX12 BLAS instance
		memcpy(blasInstance.Transform, instance.transform.m, sizeof(blasInstance.Transform));

		blasInstances.push_back(blasInstance);

		instanceBufferData.emplace_back(
			firstShapeIndex, 
			LightData(GatherInstanceLights(pNiNode))
		);
	}
	
	blasInstanceBuffer->UpdateList(blasInstances.data(), std::min(blasInstances.size(), (size_t)MAX_INSTANCES));
	blasInstanceBuffer->Upload(commandList.get());

	instanceBuffer->UpdateList(instanceBufferData.data(), std::min(instanceBufferData.size(), (size_t)MAX_INSTANCES));
	instanceBuffer->Upload(commandList.get());

	materialBuffer->Upload(commandList.get());
}

auto GetFrustumCorners2(const RE::NiFrustum& frustum)
{
	eastl::array<float3, 8> corners;

	// Near plane
	corners[0] = { frustum.fLeft, frustum.fTop, frustum.fNear };      // near top-left
	corners[1] = { frustum.fRight, frustum.fTop, frustum.fNear };     // near top-right
	corners[2] = { frustum.fRight, frustum.fBottom, frustum.fNear };  // near bottom-right
	corners[3] = { frustum.fLeft, frustum.fBottom, frustum.fNear };   // near bottom-left

	// Far plane
	float scale = frustum.fFar / frustum.fNear;
	corners[4] = { frustum.fLeft * scale, frustum.fTop * scale, frustum.fFar };
	corners[5] = { frustum.fRight * scale, frustum.fTop * scale, frustum.fFar };
	corners[6] = { frustum.fRight * scale, frustum.fBottom * scale, frustum.fFar };
	corners[7] = { frustum.fLeft * scale, frustum.fBottom * scale, frustum.fFar };

	return corners;
}

auto GetFrustumCorners(const RE::NiFrustum& frustum)
{
	eastl::array<float3, 8> corners;

	float scale = frustum.fFar / frustum.fNear;

	// Near plane (Y = forward)
	corners[0] = { frustum.fLeft, frustum.fNear, frustum.fTop };      // near top-left
	corners[1] = { frustum.fRight, frustum.fNear, frustum.fTop };     // near top-right
	corners[2] = { frustum.fRight, frustum.fNear, frustum.fBottom };  // near bottom-right
	corners[3] = { frustum.fLeft, frustum.fNear, frustum.fBottom };   // near bottom-left

	// Far plane
	corners[4] = { frustum.fLeft * scale, frustum.fFar, frustum.fTop * scale };
	corners[5] = { frustum.fRight * scale, frustum.fFar, frustum.fTop * scale };
	corners[6] = { frustum.fRight * scale, frustum.fFar, frustum.fBottom * scale };
	corners[7] = { frustum.fLeft * scale, frustum.fFar, frustum.fBottom * scale };

	return corners;
}

void ComputeFrustumAABB(eastl::array<float3, 8> corners, float3& bbMin, float3& bbMax, DirectX::XMMATRIX* transform = nullptr)
{
	if (transform)
		for (int i = 0; i < 8; i++) {
			corners[i]  = float3::Transform(corners[i], *transform);
		}

	// Initialize AABB
	bbMin = corners[0];
	bbMax = corners[0];

	// Compute min/max for X, Y, Z
	for (int i = 1; i < 8; i++) {
		bbMin.x = std::min(bbMin.x, corners[i].x);
		bbMin.y = std::min(bbMin.y, corners[i].y);
		bbMin.z = std::min(bbMin.z, corners[i].z);

		bbMax.x = std::max(bbMax.x, corners[i].x);
		bbMax.y = std::max(bbMax.y, corners[i].y);
		bbMax.z = std::max(bbMax.z, corners[i].z);
	}
}

bool SphereCastAABB(const float3& sphereCenter, float sphereRadius, const float3& dir, float maxDistance, const float3& bbMin, const float3& bbMax, float* hitDistance = nullptr)
{
	auto SphereCastAxis = [](float origin, float dir, float min, float max, float& tmin, float& tmax) -> bool {
		if (std::abs(dir) < 1e-6f) {
			if (origin < min || origin > max)
				return false;

			return true;
		}

		float ood = 1.0f / dir;
		float t1 = (min - origin) * ood;
		float t2 = (max - origin) * ood;

		if (t1 > t2)
			std::swap(t1, t2);

		tmin = std::max(tmin, t1);
		tmax = std::min(tmax, t2);

		return tmin <= tmax;
	};

	// Expand AABB by sphere radius
	float3 min = bbMin - float3(sphereRadius, sphereRadius, sphereRadius);
	float3 max = bbMax + float3(sphereRadius, sphereRadius, sphereRadius);

	float tmin = 0.0f;
	float tmax = maxDistance;

	if (!SphereCastAxis(sphereCenter.x, dir.x, min.x, max.x, tmin, tmax))
		return false;

	if (!SphereCastAxis(sphereCenter.y, dir.y, min.y, max.y, tmin, tmax))
		return false;

	if (!SphereCastAxis(sphereCenter.z, dir.z, min.z, max.z, tmin, tmax))
		return false;

	if (hitDistance)
		*hitDistance = tmin;

	return true;
}

auto GetPlanes(float3 corners[8])
{
	eastl::array<DirectX::SimpleMath::Plane, 6> planes;

	// Near plane
	planes[0] = Plane(corners[0], corners[1], corners[2]);

	// Far plane
	planes[1] = Plane(corners[5], corners[4], corners[7]);

	// Left plane
	planes[2] = Plane(corners[4], corners[0], corners[3]);

	// Right plane
	planes[3] = Plane(corners[1], corners[5], corners[6]);

	// Bottom plane
	planes[4] = Plane(corners[0], corners[4], corners[5]);

	// Top plane
	planes[5] = Plane(corners[3], corners[7], corners[6]);

	return planes;
}

bool SphereCastFrustum(const float3& sphereCenter, float radius, const float3& dir, const std::array<Plane, 6>& planes, float maxDistance = FLT_MAX, float* hitDistance = nullptr)
{
	float tmin = 0.0f;
	float tmax = maxDistance;

	for (const auto& plane : planes) {
		float nDotDir = plane.DotNormal(dir);
		float dist = plane.DotCoordinate(sphereCenter);

		if (std::abs(nDotDir) < 1e-6f) {
			// Ray is parallel to plane
			if (dist < -radius)  // sphere completely outside
				return false;
			else
				continue;  // sphere may be intersecting
		}

		float t1 = (-radius - dist) / nDotDir;
		float t2 = (radius - dist) / nDotDir;
		if (t1 > t2)
			std::swap(t1, t2);

		tmin = std::max(tmin, t1);
		tmax = std::min(tmax, t2);

		if (tmin > tmax)  // no intersection along this ray
			return false;
	}

	if (hitDistance)
		*hitDistance = tmin;

	return true;
}

void Raytracing::UpdateShadowInstances()
{
	//std::lock_guard lock{ geometryMutex };

	blasShadowInstances.clear();
	blasShadowInstances.reserve(instances.size());

	DirectX::XMMATRIX transformInverse;
	float3 bbMin, bbMax;
	float3 localLightDirection;

	if (settings.CullShadows) {
		auto* playerCamera = RE::PlayerCamera::GetSingleton();
		auto* tesCamera = playerCamera->currentState->camera;

		RE::NiCamera* camera = FindNiCamera(tesCamera->cameraRoot.get());

		auto transform = GetXMFromNiTransform(camera->world);
		transformInverse = DirectX::XMMatrixInverse(nullptr, transform);

		RE::NiFrustum frustrum = camera->GetRuntimeData2().viewFrustum;

		auto frustumCorners = GetFrustumCorners(frustrum);

		ComputeFrustumAABB(frustumCorners, bbMin, bbMax);  // In local (camera) space

		logger::trace("[RT] UpdateShadowInstances - Min: {}, Max: {}", bbMin, bbMax);

		localLightDirection = float3::TransformNormal(float3(shadowsCBData->Direction), transformInverse);
	}

	for (auto& [pNiNode, instance] : instances) {
		if (blasShadowInstances.size() > MAX_INSTANCES)
			break;

		if (settings.CullShadows) {
			auto worldBound = pNiNode->worldBound;
			float3 localCenter = float3::Transform(Float3(worldBound.center), transformInverse);

			logger::trace("[RT] UpdateShadowInstances - Local Center: {}, Radius: {}", localCenter, worldBound.radius);

			if (!SphereCastAABB(localCenter, worldBound.radius, localLightDirection, FLT_MAX, bbMin, bbMax))
				continue;
		}

		auto it = models.find(instance.filename);

		// Model was erased but not the instance
		if (it == models.end())
			continue;

		auto& model = it->second;

		if (!instance.Update(pNiNode, { it->first, model }))
			return;

		D3D12_RAYTRACING_INSTANCE_DESC blasShadowInstance = { 
			.InstanceID = static_cast<uint>(blasShadowInstances.size()),
			.InstanceMask = 1,
			.AccelerationStructure = model.blasBuffer->GetGPUVirtualAddress() 
		};

		memcpy(blasShadowInstance.Transform, instance.transform.m, sizeof(blasShadowInstance.Transform));

		blasShadowInstances.push_back(blasShadowInstance);
	}

	blasShadowInstanceBuffer->UpdateList(blasShadowInstances.data(), std::min(blasShadowInstances.size(), (size_t)MAX_INSTANCES));
	blasShadowInstanceBuffer->Upload(commandList.get());
}

void Raytracing::ReleaseTempGPUData()
{
	while (!tempGPUData.empty() && tempGPUData.front().fenceValue <= fenceValue) {
		tempGPUData.pop_front();
	}
}

void Raytracing::BSShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, [[maybe_unused]] RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active() || !renderingWorld)
		return;

	UpdateLights();
}

void Raytracing::BuildTLAS()
{
	if (tlas)
		return;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = MAX_INSTANCES,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = blasInstanceBuffer->resource->GetGPUVirtualAddress()
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	//logger::info("[RT] Build TLAS - Instances: {}, ResultDataMaxSizeInBytes: {}, ScratchDataSizeInBytes: {}", MAX_INSTANCES, prebuildInfo.ResultDataMaxSizeInBytes, prebuildInfo.ScratchDataSizeInBytes);

	auto desc = BASIC_BUFFER_DESC;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// TLAS
	{
		desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&tlas)));
		DX::ThrowIfFailed(tlas->SetName(L"TLAS"));

		// SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::TLAS));
		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, giHeap->CPUHandle(GIHeap::Slot::TLAS));
	}

	// TLAS scratch (used for rebuilding)
	desc.Width = std::max(prebuildInfo.ScratchDataSizeInBytes, 8ULL);
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&tlasScratch)));
	DX::ThrowIfFailed(tlasScratch->SetName(L"TLAS scratch"));

	// TLAS update scratch
	/*desc.Width = std::max(prebuildInfo.UpdateScratchDataSizeInBytes * TLAS_BUFFER_SIZE_MULT, 8ULL);  // WARP bug workaround: use 8 if the required size was reported as less
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
	DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch"));*/
}

void Raytracing::RebuildTLAS(ID3D12GraphicsCommandList4* pCommandList, size_t numDescs, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = static_cast<uint>(numDescs),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = instanceDescs
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	//logger::info("[RT] Rebuild TLAS - Instances: {}, ResultDataMaxSizeInBytes: {}, ScratchDataSizeInBytes: {}", numDescs, prebuildInfo.ResultDataMaxSizeInBytes, prebuildInfo.ScratchDataSizeInBytes);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress()
	};

	pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
	pCommandList->ResourceBarrier(1, &asBarrier);
}

void Raytracing::DrawRTGI()
{
	//std::lock_guard lock{ renderMutex };

	if (!d3d11Context) {
		logger::error("d3d11Context is nullptr");
	}

	if (!d3d11Fence) {
		logger::error("d3d11Fence is nullptr");
	}

	auto& rendererRuntimeData = globals::game::renderer->GetRuntimeData();
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

	auto startTime = Util::GetNowSecs();

	if (pixCapture && (!pixCaptureStarted || pixTDR) && settings.PIXCaptureLocation == PIXCaptureLocation::GlobalIllumination) {
		pixCaptureStarted = true;

		/*if (pixMultiFrame) {
			PIXGpuCaptureNextFrames(L"I:/Temp/Pix/TDRCap.pix", 60);
		} else {*/
			//PIXBeginCapture(PIX_CAPTURE_GPU, PIXCaptureParameters
			ga->BeginCapture();
		//}
	}
	
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
		frameBufferData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameBufferData->ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();

		float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameBufferData->Position = float3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
		frameBufferData->FrameCount = globals::state->frameCount;

		frameBufferData->CameraData = Util::GetCameraData();

		auto eye = Util::GetCameraData(0);
		float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
		float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

		frameBufferData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

		frameBufferData->Roughness = settings.Roughness;
		frameBufferData->Metalness = settings.Metalness;

		frameBufferData->Diffuse = settings.Diffuse;
		frameBufferData->Specular = settings.Specular;
		frameBufferData->Emissive = settings.Emissive;
		frameBufferData->Effect = settings.Effect;
		frameBufferData->Sky = settings.Sky;

#ifdef SHARC
		frameBufferData->SHaRCScale = settings.SHaRCScale / Util::Units::GAME_UNIT_TO_M;
#endif

		// Update Features
		{
			auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();

			frameBufferData->Features.ExtendedMaterial = *reinterpret_cast<CPMSettings*>(&globals::features::extendedMaterials.settings);
			frameBufferData->Features.WetnessEffects = *reinterpret_cast<WetnessEffectsSettings*>(&wetnessEffect);
			frameBufferData->Features.CloudShadows = *reinterpret_cast<CloudShadowsSettings*>(&globals::features::cloudShadows.settings);
			frameBufferData->Features.HairSpecular = *reinterpret_cast<HairSpecularSettings*>(&globals::features::hairSpecular.settings);
			frameBufferData->Features.ExtendedTranslucency = *reinterpret_cast<ExtendedTranslucencySettings*>(&globals::features::extendedTranslucency.settings);
		}

		frameBuffer->Update(frameBufferData.get(), sizeof(GIFrameData));
		frameBuffer->Upload(commandList.get());
		frameBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	BuildTLAS();
	RebuildTLAS(commandList.get(), blasInstances.size(), blasInstanceBuffer->resource->GetGPUVirtualAddress());

	{
		// Raytracing
		{
			commandList->SetPipelineState1(pipelineRT.get());
			commandList->SetComputeRootSignature(rootSignature.get());

			auto commonHeapPtr = giHeap->Heap();
			commandList->SetDescriptorHeaps(1, &commonHeapPtr);

			// Parameter 0: UAV table
			commandList->SetComputeRootDescriptorTable(0, giHeap->TableGPUHandle(GIHeap::Table::UAV));

			// Parameter 1: Fixed SRVs
			commandList->SetComputeRootDescriptorTable(1, giHeap->TableGPUHandle(GIHeap::Table::SRV));

			// Parameter 2: Vertex buffers
			commandList->SetComputeRootDescriptorTable(2, giHeap->TableGPUHandle(GIHeap::Table::VertexBuffer));

			// Parameter 3: Triangle buffers
			commandList->SetComputeRootDescriptorTable(3, giHeap->TableGPUHandle(GIHeap::Table::TriangleBuffer));

			// Parameter 4: Textures
			commandList->SetComputeRootDescriptorTable(4, giHeap->TableGPUHandle(GIHeap::Table::Textures));

			// Parameter 5: Constant buffer
			commandList->SetComputeRootConstantBufferView(5, frameBuffer->resource->GetGPUVirtualAddress());

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
			} else if (settings.DebugOutput == DebugOutput::NormalRoughnessGbuffer) {

				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(normalRoughnessTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), normalRoughnessTexture->resource.get());

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(normalRoughnessTexture->resource.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			} else if (settings.DebugOutput == DebugOutput::GeometryNormalMetalness) {
				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(GNMDTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), GNMDTexture.get());

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(GNMDTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			} else if (settings.DebugOutput == DebugOutput::ReflectanceGBuffer) {

				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(gbufferReflectanceTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), gbufferReflectanceTexture.get());

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(gbufferReflectanceTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			}
		}

		DX::ThrowIfFailed(commandList->Close());

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

	// Wait until GPU is done with previous frame
	if (d3d12Fence->GetCompletedValue() < fenceValue) {
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
	}

	mainTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);

	if (pixCapture && pixCaptureStarted && !pixTDR && settings.PIXCaptureLocation == PIXCaptureLocation::GlobalIllumination) {
		ga->EndCapture();
		pixCapture = pixMultiFrame;
		pixCaptureStarted = false;
	}

	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	//New frame, reset
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	/*if (pixCapture) {
		pixCaptureStarted = true;
		ga->BeginCapture();
	}*/

	ReleaseTempGPUData();

	d3d11Context->CopyResource(main.texture, mainTexture->resource11);

	// Clear specular if Path Tracing is enabled
	if (settings.PathTracing)
	{
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		d3d11Context->ClearRenderTargetView(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
	}
}

void Raytracing::UpdateShadowsFrameBuffer()
{
	shadowsCBData->CameraData = Util::GetCameraData();

	auto eye = Util::GetCameraData(0);
	float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));
	shadowsCBData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	shadowsCBData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	shadowsCBData->Position = float4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f);

	if (shadowLight) {
		auto direction = Float3(-shadowLight->GetShadowDirectionalLightRuntimeData().lightDirection);
		direction.Normalize();
		shadowsCBData->Direction = float4(direction.x, direction.y, direction.z, 0.0f);
	}

	shadowsCB->Update(shadowsCBData.get(), sizeof(ShadowsFrameData));
}

void Raytracing::RenderShadows()
{

	//logger::info("[RT] RenderShadows - ShadowLight [0x{:x}], TLAS [0x{:x}]", reinterpret_cast<uintptr_t>(shadowLight), reinterpret_cast<uintptr_t>(tlas.get()));

	if (!shadowLight)
		return;

	if (!shadowFrameChecker.IsNewFrame())
		return;

	//std::lock_guard lock{ renderMutex };

	auto rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto shadowMask = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];

	CopyDepth();

	// Tell DX11 to finish and wait
	//d3d11Context->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
	d3d11Context->Flush();
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	auto startTime = Util::GetNowSecs();

	if (pixCapture && (!pixCaptureStarted || pixTDR) && settings.PIXCaptureLocation == PIXCaptureLocation::Shadows) {
		pixCaptureStarted = true;

		/*if (pixMultiFrame) {
			PIXGpuCaptureNextFrames(L"I:/Temp/Pix/TDRCap.pix", 60);
		} else {*/
			//PIXBeginCapture(PIX_CAPTURE_GPU, PIXCaptureParameters
			ga->BeginCapture();
		//}
	}

	// Do DX12 work...
	UpdateShadowInstances();

	//UpdateDynamicSkinning(commandList.get());

	shadowsCB->Upload(commandList.get());
	shadowsCB->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	BuildTLAS();
	RebuildTLAS(commandList.get(), blasShadowInstances.size(), blasShadowInstanceBuffer->resource->GetGPUVirtualAddress());

	commandList->SetPipelineState1(shadowPipeline.get());
	commandList->SetComputeRootSignature(shadowRS.get());

	auto computeHeapPtr = shadowHeap->Heap();
	commandList->SetDescriptorHeaps(1, &computeHeapPtr);

	// UAV table
	commandList->SetComputeRootDescriptorTable(0, shadowHeap->TableGPUHandle(ShadowsHeap::Table::UAV));

	// SRV table
	commandList->SetComputeRootDescriptorTable(1, shadowHeap->TableGPUHandle(ShadowsHeap::Table::SRV));

	// Constant buffer
	commandList->SetComputeRootConstantBufferView(2, shadowsCB->resource->GetGPUVirtualAddress());

	CD3DX12_RESOURCE_BARRIER ctuBarrier[1] = {
		CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	commandList->ResourceBarrier(_countof(ctuBarrier), ctuBarrier);

	// Dispatch
	auto shadowMaskDesc = shadowMaskTexture->resource->GetDesc();

	D3D12_GPU_VIRTUAL_ADDRESS sbtAddr = shadowSBTBuffer->resource->GetGPUVirtualAddress();

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
		.RayGenerationShaderRecord = {
			.StartAddress = sbtAddr,
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
		},
		.MissShaderTable = { 
			.StartAddress = sbtAddr + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
		},
		.HitGroupTable = { 
			.StartAddress = sbtAddr + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES 
		},
		.Width = static_cast<UINT>(shadowMaskDesc.Width),
		.Height = shadowMaskDesc.Height,
		.Depth = 1
	};

	commandList->DispatchRays(&dispatchDesc);

	CD3DX12_RESOURCE_BARRIER utcBarrier[1] = {
		CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
	};
	commandList->ResourceBarrier(_countof(utcBarrier), utcBarrier);

	DX::ThrowIfFailed(commandList->Close());

	ID3D12CommandList* commandListPtr = commandList.get();
	commandQueue->ExecuteCommandLists(1, &commandListPtr);

	// Wait for D3D12 to finish and signal DX11
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

	// Wait for GPU
	if (d3d12Fence->GetCompletedValue() < fenceValue) {
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
	}

	shadowsTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);

	if (pixCapture && pixCaptureStarted && !pixTDR && settings.PIXCaptureLocation == PIXCaptureLocation::Shadows) {
		ga->EndCapture();
		pixCapture = pixMultiFrame; // Do not stop capture when doing multiframe
		pixCaptureStarted = false;
	}

	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	// Reset for next command list usage
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	ReleaseTempGPUData();

	d3d11Context->CopyResource(shadowMask.texture, shadowMaskTexture->resource11);
}

void Raytracing::PostPostLoad()
{
	Hooks::Install();
	Initialize();
	//RE::BSTSingletonExplicit<RE::BSModelDB::BSModelProcessor>;

	//auto* g_TESProcessor = REL::Relocation<RE::BSModelDB::BSModelProcessor*>(0x01F5D910).get();
	//(g_TESProcessor) = new RTProcessor(g_TESProcessor);

	//MenuOpenCloseEventHandler::Register();
	//TESLoadGameEventHandler::Register();

	TESObjectLoadedEventHandler::Register();
}

/*void Raytracing::RTProcessor::PostCreate(const RE::BSModelDB::DBTraits::ArgsType& a_args, const char* modelName, RE::NiPointer<RE::NiNode>& a_root, std::uint32_t& typeOut)
{
	m_oldProcessor->PostCreate(a_args, modelName, a_root, typeOut);
	logger::error("[RT] RTProcessor::PostCreate - ModelName: {}", modelName);
}*/

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

void Raytracing::DeviceRemovedHandler()
{
	if (settings.EnablePIXCapture) {
		ga->EndCapture();
		pixCapture = false;
		pixCaptureStarted = false;
		pixMultiFrame = false;
		pixTDR = false;
	}

	if (settings.EnableDebugDevice) {
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
}

void Raytracing::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			auto pixGPUCapturerPath = GetLatestWinPixGpuCapturerPath();

			if (pixGPUCapturerPath.empty()) {
				logger::warn("[RT] PIX capture is enabled but binaries where not found.");
			} else
			{
				LoadLibrary(pixGPUCapturerPath.c_str());
			}
		}
	}

	logger::info("[RT] Creating D3D12 device");

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
					logger::info("[RT] Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
				else
					logger::warn("[RT] Hardware ray tracing not supported.");
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

	if (settings.EnableDebugDevice || settings.EnablePIXCapture)
	{
		HANDLE disconnectEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(UINT64_MAX, disconnectEvent));

		std::thread([this, disconnectEvent]() {
			WaitForSingleObject(disconnectEvent, INFINITE);
			DeviceRemovedHandler();
		}).detach();
	}
}

void Raytracing::CreateRootSignature()
{
	// UAV range
	giHeap->CreateTable(
		GIHeap::Table::UAV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 
		{  
			{ GIHeap::Slot::Output, 1 }, 
			{ GIHeap::Slot::Reflectance, 1 }, 
			{ GIHeap::Slot::SpecularHitDist, 1 }
		});

	// Fixed SRV ranges (NormalRoughness + GNMD + Scene + Lights + Index map)
	giHeap->CreateTable(
		GIHeap::Table::SRV, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ GIHeap::Slot::Main, 1 },
			{ GIHeap::Slot::Depth, 1 },
			{ GIHeap::Slot::Albedo, 1 },
			{ GIHeap::Slot::NormalRoughness, 1 },
			{ GIHeap::Slot::GNMD, 1 },
			{ GIHeap::Slot::TLAS, 1 },
			{ GIHeap::Slot::SkyHemisphere, 1 },
			{ GIHeap::Slot::Lights, 1 },
			{ GIHeap::Slot::Materials, 1 },	
			{ GIHeap::Slot::Instances, 1 }		
		});


	// Vertex buffers (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::VertexBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{
			{ GIHeap::Slot::Vertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE }
		});
	

	// Triangle buffers (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::TriangleBuffer, 
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
		{ 
			{ GIHeap::Slot::Triangles, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});

	// Textures (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::Textures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ GIHeap::Slot::Textures, UINT_MAX, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});


	auto rootParameters = giHeap->GetRootParameters();

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

void Raytracing::CreateShadowsRootSignature()
{
	// UAV range
	shadowHeap->CreateTable(
		ShadowsHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ 
			{ ShadowsHeap::Slot::ShadowMask, 1 } 
		});

	// SRV
	shadowHeap->CreateTable(
		ShadowsHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{	
			{ ShadowsHeap::Slot::Depth, 1 },
			{ ShadowsHeap::Slot::TLAS, 1 }
		});

	auto rootParameters = shadowHeap->GetRootParameters();

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

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			logger::error("[RT] D3DX12SerializeVersionedRootSignature {}", (char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&shadowRS)));
	DX::ThrowIfFailed(shadowRS->SetName(L"Shadow Root Signature"));
}

void Raytracing::CreateSkinningRootSignature()
{
	skinningHeap->CreateTable(
		SkinningHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ 
			{ SkinningHeap::Slot::Output, UINT_MAX, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});

	skinningHeap->CreateTable(
		SkinningHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ SkinningHeap::Slot::LocalToRoot, 1 },
			{ SkinningHeap::Slot::UpdateData, 1 },
			{ SkinningHeap::Slot::BoneMatrices, 1 }
		});

	skinningHeap->CreateTable(
		SkinningHeap::Table::DynamicBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ SkinningHeap::Slot::DynamicVertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});

	skinningHeap->CreateTable(
		SkinningHeap::Table::SkinningBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ 
			{ SkinningHeap::Slot::SkinningData, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } 
		});

	auto rootParameters = skinningHeap->GetRootParameters();

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
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(skinningRS.put())));
	DX::ThrowIfFailed(skinningRS->SetName(L"Compute Root Signature - Skinning"));
}

void Raytracing::Initialize()
{
	
}


void Raytracing::ClearShaderCache()
{
	copyDepthCS = nullptr;  // This is actually optional
	CompileShaders();
}

void Raytracing::CompileShaders()
{
	if (!skinningRS) {
		CreateSkinningRootSignature();
		CompileSkinningShaders();
	}

	if (!rootSignature) {
		CreateRootSignature();
		CompileRTGIShaders();
	}

	if (!shadowRS) {
		CreateShadowsRootSignature();
		CompileRTShadowsShaders();
	}

	CompileComputeShaders();
}

void Raytracing::CompileSkinningShaders()
{
	winrt::com_ptr<IDxcBlob> shaderBlob;
	ShaderUtils::CompileShader(shaderBlob, L"Data/Shaders/Raytracing/SkinningCS.hlsl", {}, L"cs_6_5");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = skinningRS.get();
	computeDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

	DX::ThrowIfFailed(d3d12Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(skinningPipeline.put())));
	DX::ThrowIfFailed(skinningPipeline->SetName(L"Compute Pipeline - Vertex Update"));
}

void Raytracing::CompileRTGIShaders()
{
	const auto modePath = settings.PathTracing ? L"PT" : L"GI";

	const int bounces = settings.PathTracing ? settings.Bounces + 1 : settings.Bounces;

	const auto bouncesWStr = std::to_wstring(bounces);
	const auto samplesWStr = std::to_wstring(settings.SamplesPerPixel);

	eastl::vector<DxcDefine> defines = { 
		{ L"MAX_DEPTH", bouncesWStr.c_str() },
		{ L"SAMPLES", samplesWStr.c_str() },
	};

#ifdef SHARC
	if (settings.SHaRC) {
		defines.emplace_back(L"SHARC");
	}
#endif

	if (settings.PathTracing) {
		defines.emplace_back(L"PATH_TRACING");
	}

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, std::format(L"Data/Shaders/Raytracing/{}/RayGeneration.hlsl", modePath).c_str(), defines);

	winrt::com_ptr<IDxcBlob> missBlob, closestHitBlob, anyHitBlob;
	ShaderUtils::CompileShader(missBlob, L"Data/Shaders/Raytracing/GI/Miss.hlsl", defines);
	ShaderUtils::CompileShader(closestHitBlob, L"Data/Shaders/Raytracing/GI/ClosestHit.hlsl", defines);
	ShaderUtils::CompileShader(anyHitBlob, L"Data/Shaders/Raytracing/GI/AnyHit.hlsl", defines);

	winrt::com_ptr<IDxcBlob> shadowMissBlob, shadowAnyHitBlob;
	ShaderUtils::CompileShader(shadowMissBlob, L"Data/Shaders/Raytracing/GI/ShadowMiss.hlsl");
	ShaderUtils::CompileShader(shadowAnyHitBlob, L"Data/Shaders/Raytracing/GI/ShadowAnyHit.hlsl");

	DX12::RTPipelineBuilder pipelineBuilder;

	// Init pipeline
	{
		// Libraries
		pipelineBuilder.AddRayGenLib(rayGenBlob.get(), L"RayGeneration");

		pipelineBuilder.AddMissLib(missBlob.get(), L"IndirectMiss");
		pipelineBuilder.AddMissLib(shadowMissBlob.get(), L"ShadowMiss");

		pipelineBuilder.AddHitLib(closestHitBlob.get(), L"IndirectClosestHit");

		pipelineBuilder.AddAnyHitLib(anyHitBlob.get(), L"IndirectAnyHit");
		pipelineBuilder.AddAnyHitLib(shadowAnyHitBlob.get(), L"ShadowAnyHit");

		// Hit groups
		pipelineBuilder.AddHitGroup(L"IndirectHitGroup", L"IndirectClosestHit", L"IndirectAnyHit");
		pipelineBuilder.AddHitGroup(L"ShadowHitGroup", L"", L"ShadowAnyHit");

		// Shader + pipeline config
		pipelineBuilder.AddShaderConfig(20, 8);
		pipelineBuilder.AddGlobalRootSignature(rootSignature.get());
		pipelineBuilder.AddPipelineConfig(settings.Bounces + (settings.PathTracing ? 2 : 1)); // Max recursion depth

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
		logger::debug("[RT] GI SBT size: {}", shaderBindingTableSize);

		shaderBindingTableBuffer = eastl::make_unique<DX12::ResourceUpload>(d3d12Device.get(), shaderBindingTableSize);
		shaderBindingTableBuffer->SetName(L"GI SBT");

		std::vector<uint8_t> shaderBindingTableCPU(shaderBindingTableSize);
		shaderBindingTable->Build(shaderBindingTableCPU.data());

		shaderBindingTable->LogShaderBindingTable(shaderBindingTableBuffer->resource->GetGPUVirtualAddress());

		shaderBindingTableBuffer->Update(shaderBindingTableCPU.data(), shaderBindingTableSize);
		shaderBindingTableBuffer->Upload(commandList.get());
		shaderBindingTableBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
}

void Raytracing::CompileRTShadowsShaders()
{
	winrt::com_ptr<IDxcBlob> shadowsRTBlob;
	ShaderUtils::CompileShader(shadowsRTBlob, L"Data/Shaders/Raytracing/ShadowsRT.hlsl");

	// Init pipeline
	{
		D3D12_DXIL_LIBRARY_DESC lib = {
			.DXILLibrary = { 
				.pShaderBytecode = shadowsRTBlob->GetBufferPointer(),
				.BytecodeLength = shadowsRTBlob->GetBufferSize() 
			}
		};

		D3D12_HIT_GROUP_DESC hitGroup = { 
			.HitGroupExport = L"HitGroup",
			.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
			.ClosestHitShaderImport = L"ClosestHit" 
		};

		D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
			.MaxPayloadSizeInBytes = 4,
			.MaxAttributeSizeInBytes = 8,
		};

		D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { shadowRS.get() };

		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 2 };

		D3D12_STATE_SUBOBJECT subobjects[] = {
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroup },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg }
		};
		D3D12_STATE_OBJECT_DESC desc = { .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
			.NumSubobjects = std::size(subobjects),
			.pSubobjects = subobjects };

		HRESULT hr = d3d12Device->CreateStateObject(&desc, IID_PPV_ARGS(&shadowPipeline));

		if (FAILED(hr)) {
			logger::error("CreateStateObject failed: {}", hr);
		}

		DX::ThrowIfFailed(hr);

		DX::ThrowIfFailed(shadowPipeline->SetName(L"Shadow Pipeline"));
	}

	// Init shader tables
	{
		winrt::com_ptr<ID3D12StateObjectProperties> props;
		shadowPipeline->QueryInterface(props.put());

		size_t shaderBindingTableSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 3;

		shadowSBTBuffer = eastl::make_unique<DX12::ResourceUpload>(d3d12Device.get(), shaderBindingTableSize);
		shadowSBTBuffer->SetName(L"Shadows SBT");

		std::vector<uint8_t> shaderBindingTableCPU(shaderBindingTableSize);

		void* data = shaderBindingTableCPU.data();
		auto writeId = [&](const wchar_t* name) {
			void* id = props->GetShaderIdentifier(name);
			memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			data = static_cast<char*>(data) +
				   D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		};
	
		writeId(L"RayGeneration");
		writeId(L"Miss");
		writeId(L"HitGroup");

		shadowSBTBuffer->Update(shaderBindingTableCPU.data(), shaderBindingTableSize);
		shadowSBTBuffer->Upload(commandList.get());
		shadowSBTBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_GENERIC_READ);
	}
}

void Raytracing::CompileComputeShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CopyDepthCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		copyDepthCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CubeToHemiCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		cubeToHemiCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ConvertNormalGlossCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		convertNormalGlossCS.attach(rawPtr);
}

RaytracingFD::FeatureData Raytracing::GetCommonBufferData()
{
	RaytracingFD::FeatureData featureData{
		.InteriorDirectional = settings.GlobalIllumination ? 0.0f : 1.0f,
		.Ambient = settings.GlobalIllumination ? 0.0f : 1.0f,
		.EnvMap = settings.GlobalIllumination ? 0.0f : 1.0f,
		.Albedo = settings.GlobalIllumination
	};

	return featureData;
}

RE::BSEventNotifyControl Raytracing::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a loadscreen
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		//auto& rtgi = globals::features::raytracing;

		logger::debug("MenuOpenCloseEventHandler::ProcessEvent - Opening: {}", a_event->opening);

		if (a_event->opening) {			
			auto& rt = globals::features::raytracing;
			rt.ClearInstances();	
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::TESLoadGameEventHandler::ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*)
{
	logger::debug("TESLoadGameEventHandler::ProcessEvent {}", reinterpret_cast<intptr_t>(a_event));

	auto& rt = globals::features::raytracing;
	rt.AddInstances();

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::TESObjectLoadedEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	auto* eventRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->formID);

	//if (eventRef->formType.none(RE::FormType::NPC, RE::FormType::LeveledNPC, RE::FormType::ActorCharacter))
	if (eventRef->formType.none(RE::FormType::ActorCharacter))
		return RE::BSEventNotifyControl::kContinue;

	/*if (eventRef->data.objectReference->formType.none(RE::FormType::NPC))
		return RE::BSEventNotifyControl::kContinue;*/

	auto* actor = eventRef->As<RE::Actor>();

	if (!actor)
		return RE::BSEventNotifyControl::kContinue;

	/*if (actor) {
		logger::info("[RT] TESObjectLoadedEventHandler - Actor: {}", actor->GetName());

		auto* actorBase = actor->GetActorBase();
		if (actorBase)
			logger::info("[RT] TESObjectLoadedEventHandler - ActorBase: {}", actorBase->GetFullName());
	}*/

	auto* pNiAVObject = eventRef->Get3D();

	if (!pNiAVObject)
		return RE::BSEventNotifyControl::kContinue;

	globals::features::raytracing.CreateModel(actor->GetName(), netimmerse_cast<RE::NiNode*>(pNiAVObject));

	return RE::BSEventNotifyControl::kContinue;
}
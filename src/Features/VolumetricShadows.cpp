#include "VolumetricShadows.h"

#include "State.h"
#include "Utils/D3D.h"

void VolumetricShadows::SetupResources()
{
	auto device = globals::d3d::device;

	// Create samplers
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));
		Util::SetResourceName(linearSampler, "VolumetricShadows::LinearSampler");
	}

	// Compile compute shaders
	std::vector<std::pair<const char*, const char*>> defines;
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP0", nullptr });
	downsampleShadowMip0CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP1", nullptr });
	downsampleShadowMip1CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));

	defines.clear();
	defines.push_back({ "BLUR_HORIZONTAL", nullptr });
	blurShadowHorizontalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "BLUR_VERTICAL", nullptr });
	blurShadowVerticalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
}

void VolumetricShadows::ClearShaderCache()
{
	if (downsampleShadowMip0CS) {
		downsampleShadowMip0CS->Release();
		downsampleShadowMip0CS = nullptr;
	}
	if (downsampleShadowMip1CS) {
		downsampleShadowMip1CS->Release();
		downsampleShadowMip1CS = nullptr;
	}
	if (blurShadowHorizontalCS) {
		blurShadowHorizontalCS->Release();
		blurShadowHorizontalCS = nullptr;
	}
	if (blurShadowVerticalCS) {
		blurShadowVerticalCS->Release();
		blurShadowVerticalCS = nullptr;
	}

	std::vector<std::pair<const char*, const char*>> defines;
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP0", nullptr });
	downsampleShadowMip0CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP1", nullptr });
	downsampleShadowMip1CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));

	defines.clear();
	defines.push_back({ "BLUR_HORIZONTAL", nullptr });
	blurShadowHorizontalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "BLUR_VERTICAL", nullptr });
	blurShadowVerticalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
}

void VolumetricShadows::CopyShadowLightData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VolumetricShadows::CopyShadowLightData");

	auto context = globals::d3d::context;

	{
		context->PSGetShaderResources(4, 1, &shadowView);

		// Downsample shadow texture array to fixed 512x512 (mip1: 256x256)
		if (shadowView) {
			constexpr uint32_t SHADOW_COPY_SIZE = 512;

			// Lazily create fixed-size output textures
			if (!shadowCopyTexture) {
				shadowCopyWidth = SHADOW_COPY_SIZE;
				shadowCopyHeight = SHADOW_COPY_SIZE;

				D3D11_TEXTURE2D_DESC copyDesc{};
				copyDesc.Width = SHADOW_COPY_SIZE;
				copyDesc.Height = SHADOW_COPY_SIZE;
				copyDesc.MipLevels = 2;
				copyDesc.ArraySize = 1;
				copyDesc.Format = DXGI_FORMAT_R16G16_UNORM;
				copyDesc.SampleDesc.Count = 1;
				copyDesc.SampleDesc.Quality = 0;
				copyDesc.Usage = D3D11_USAGE_DEFAULT;
				copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
				copyDesc.MiscFlags = 0;

				auto device = globals::d3d::device;
				DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowCopyTexture));
				Util::SetResourceName(shadowCopyTexture, "VolumetricShadows::ShadowCopy");

				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = copyDesc.Format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 2;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopySRV));
				Util::SetResourceName(shadowCopySRV, "VolumetricShadows::ShadowCopy SRV");

				// Create mip-specific SRVs for blur passes
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip0SRV));
				Util::SetResourceName(shadowCopyMip0SRV, "VolumetricShadows::ShadowCopy SRV mip0");

				srvDesc.Texture2D.MostDetailedMip = 1;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip1SRV));
				Util::SetResourceName(shadowCopyMip1SRV, "VolumetricShadows::ShadowCopy SRV mip1");

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format = copyDesc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip0UAV));
				Util::SetResourceName(shadowCopyMip0UAV, "VolumetricShadows::ShadowCopy UAV mip0");

				uavDesc.Texture2D.MipSlice = 1;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip1UAV));
				Util::SetResourceName(shadowCopyMip1UAV, "VolumetricShadows::ShadowCopy UAV mip1");

				// Create temporary texture for blur intermediate result
				DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowBlurTempTexture));
				Util::SetResourceName(shadowBlurTempTexture, "VolumetricShadows::ShadowBlurTemp");

				// Create mip-specific SRVs for blur temp texture
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip0SRV));
				Util::SetResourceName(shadowBlurTempMip0SRV, "VolumetricShadows::ShadowBlurTemp SRV mip0");

				srvDesc.Texture2D.MostDetailedMip = 1;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip1SRV));
				Util::SetResourceName(shadowBlurTempMip1SRV, "VolumetricShadows::ShadowBlurTemp SRV mip1");

				uavDesc.Texture2D.MipSlice = 0;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip0UAV));
				Util::SetResourceName(shadowBlurTempMip0UAV, "VolumetricShadows::ShadowBlurTemp UAV mip0");

				uavDesc.Texture2D.MipSlice = 1;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip1UAV));
				Util::SetResourceName(shadowBlurTempMip1UAV, "VolumetricShadows::ShadowBlurTemp UAV mip1");
			}

			// Get input dimensions for dispatch sizing
			ID3D11Resource* shadowResource = nullptr;
			shadowView->GetResource(&shadowResource);

			if (shadowResource) {
				ID3D11Texture2D* shadowTexture = nullptr;
				shadowResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&shadowTexture));

				if (shadowTexture) {
					D3D11_TEXTURE2D_DESC srcDesc;
					shadowTexture->GetDesc(&srcDesc);

					// Dispatch downsample compute shader
					auto renderer = globals::game::renderer;
					auto& esramDepthStencil = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM];

					ID3D11ShaderResourceView* csSrvs[2]{ shadowView, esramDepthStencil.depthSRV };
					context->CSSetShaderResources(0, 2, csSrvs);

					context->CSSetSamplers(0, 1, &linearSampler);

					// Dispatch covers full input: each thread gathers 2x2, 8 threads per group
					auto dispatchSize = srcDesc.Width / 16;

					// Mip 0 (cascade 1)
					ID3D11UnorderedAccessView* csUavs[1]{ shadowCopyMip0UAV };
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip0CS, nullptr, 0);
					context->Dispatch(dispatchSize, dispatchSize, 1);

					// Mip 1 (cascade 0)
					csUavs[0] = shadowCopyMip1UAV;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip1CS, nullptr, 0);
					context->Dispatch(dispatchSize, dispatchSize, 1);

					// Unbind SRVs before blur passes
					csSrvs[0] = nullptr;
					csSrvs[1] = nullptr;
					context->CSSetShaderResources(0, 2, csSrvs);
					csUavs[0] = nullptr;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

					constexpr uint32_t mip0Size = SHADOW_COPY_SIZE;
					constexpr uint32_t mip1Size = SHADOW_COPY_SIZE / 2;

					// 11x11 separable blur for Mip 0
					{
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip0 -> shadowBlurTemp mip0
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip0SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip0UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS, nullptr, 0);
						context->Dispatch((mip0Size + GROUP_SIZE - 1) / GROUP_SIZE, mip0Size, 1);

						// Unbind for next pass
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

						// Vertical pass: shadowBlurTemp mip0 -> shadowCopy mip0
						blurSrvs[0] = shadowBlurTempMip0SRV;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowCopyMip0UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowVerticalCS, nullptr, 0);
						context->Dispatch(mip0Size, (mip0Size + GROUP_SIZE - 1) / GROUP_SIZE, 1);

						// Unbind
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					}

					// 11x11 separable blur for Mip 1
					{
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip1 -> shadowBlurTemp mip1
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip1SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip1UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS, nullptr, 0);
						context->Dispatch((mip1Size + GROUP_SIZE - 1) / GROUP_SIZE, mip1Size, 1);

						// Unbind for next pass
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

						// Vertical pass: shadowBlurTemp mip1 -> shadowCopy mip1
						blurSrvs[0] = shadowBlurTempMip1SRV;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowCopyMip1UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowVerticalCS, nullptr, 0);
						context->Dispatch(mip1Size, (mip1Size + GROUP_SIZE - 1) / GROUP_SIZE, 1);

						// Unbind
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					}

					// Cleanup CS state
					ID3D11SamplerState* nullSampler = nullptr;
					context->CSSetSamplers(0, 1, &nullSampler);
					context->CSSetShader(nullptr, nullptr, 0);

					shadowTexture->Release();
				}
				shadowResource->Release();
			}
		}

		ID3D11ShaderResourceView* srv = shadowView ? (shadowCopySRV ? shadowCopySRV : shadowView) : nullptr;
		context->PSSetShaderResources(18, 1, &srv);

		if (shadowView)
			shadowView->Release();
		shadowView = nullptr;
	}
}

void VolumetricShadows::DrawSettings()
{
	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		auto DisplayRT = [&](const char* label, ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv) {
			if (srv && tex) {
				D3D11_TEXTURE2D_DESC desc;
				tex->GetDesc(&desc);
				char buf[128];
				snprintf(buf, sizeof(buf), "%s (%ux%u)", label, desc.Width, desc.Height);
				if (ImGui::TreeNode(buf)) {
					ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
					ImGui::TreePop();
				}
			}
		};

		DisplayRT("VSM Cascade 0", shadowCopyTexture, shadowCopyMip0SRV);
		DisplayRT("VSM Cascade 1", shadowCopyTexture, shadowCopyMip1SRV);

		ImGui::TreePop();
	}
}

void VolumetricShadows::LoadSettings(json&)
{
	// No settings currently
}

void VolumetricShadows::SaveSettings(json&)
{
	// No settings currently
}

void VolumetricShadows::RestoreDefaultSettings()
{
	// No settings currently
}

struct CreateDepthStencil_VolumetricLighting
{
	static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		RE::BSGraphics::DepthStencilTargetProperties properties = *a_properties;
		a_properties->height = 1024;
		a_properties->width = 1024;
		func(This, a_target, &properties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void VolumetricShadows::PostPostLoad()
{
	stl::write_thunk_call<CreateDepthStencil_VolumetricLighting>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x9DC, 0x9DC, 0xC60));
}

bool VolumetricShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

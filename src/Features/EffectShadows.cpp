#include "EffectShadows.h"

#include "State.h"

void EffectShadows::SetupResources()
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
	}

	// Create shadow data buffer
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = 1;

		sbDesc.StructureByteStride = sizeof(PerGeometry);
		sbDesc.ByteWidth = sizeof(PerGeometry) * numElements;
		perShadow = new Buffer(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		perShadow->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		perShadow->CreateUAV(uavDesc);
	}

	// Compile compute shaders
	copyShadowCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\EffectShadows\\CopyShadowDataCS.hlsl", {}, "cs_5_0"));

	std::vector<std::pair<const char*, const char*>> defines;
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP0", nullptr });
	downsampleShadowMip0CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\EffectShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "DOWNSAMPLE_SHADOW_MIP1", nullptr });
	downsampleShadowMip1CS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\EffectShadows\\DownsampleShadowCS.hlsl", defines, "cs_5_0"));

	defines.clear();
	defines.push_back({ "BLUR_HORIZONTAL", nullptr });
	blurShadowHorizontalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\EffectShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
	defines.clear();
	defines.push_back({ "BLUR_VERTICAL", nullptr });
	blurShadowVerticalCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\EffectShadows\\BlurShadowCS.hlsl", defines, "cs_5_0"));
}

void EffectShadows::ClearShaderCache()
{
}

void EffectShadows::CopyShadowData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowData");

	auto context = globals::d3d::context;

	ID3D11UnorderedAccessView* uavs[1]{ perShadow->uav.get() };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11Buffer* buffers[3];
	context->PSGetConstantBuffers(0, 3, buffers);
	context->PSGetConstantBuffers(12, 1, buffers + 1);

	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(copyShadowCS, nullptr, 0);

	context->Dispatch(1, 1, 1);

	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	std::fill(buffers, buffers + ARRAYSIZE(buffers), nullptr);
	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(nullptr, nullptr, 0);

	{
		context->PSGetShaderResources(4, 1, &shadowView);

		// Downsample shadow texture array to 8x smaller resolution
		if (shadowView) {
			ID3D11Resource* shadowResource = nullptr;
			shadowView->GetResource(&shadowResource);

			if (shadowResource) {
				ID3D11Texture2D* shadowTexture = nullptr;
				shadowResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&shadowTexture));

				if (shadowTexture) {
					D3D11_TEXTURE2D_DESC srcDesc;
					shadowTexture->GetDesc(&srcDesc);

					uint32_t newWidth = srcDesc.Width / 8;
					uint32_t newHeight = srcDesc.Height / 8;

					// Lazily create or recreate downscaled texture if dimensions changed
					if (!shadowCopyTexture || shadowCopyWidth != newWidth || shadowCopyHeight != newHeight) {
						if (shadowCopySRV) {
							shadowCopySRV->Release();
							shadowCopySRV = nullptr;
						}
						if (shadowCopyMip0SRV) {
							shadowCopyMip0SRV->Release();
							shadowCopyMip0SRV = nullptr;
						}
						if (shadowCopyMip1SRV) {
							shadowCopyMip1SRV->Release();
							shadowCopyMip1SRV = nullptr;
						}
						if (shadowCopyMip0UAV) {
							shadowCopyMip0UAV->Release();
							shadowCopyMip0UAV = nullptr;
						}
						if (shadowCopyMip1UAV) {
							shadowCopyMip1UAV->Release();
							shadowCopyMip1UAV = nullptr;
						}
						if (shadowCopyTexture) {
							shadowCopyTexture->Release();
							shadowCopyTexture = nullptr;
						}

						// Release blur temp texture resources
						if (shadowBlurTempMip0SRV) {
							shadowBlurTempMip0SRV->Release();
							shadowBlurTempMip0SRV = nullptr;
						}
						if (shadowBlurTempMip1SRV) {
							shadowBlurTempMip1SRV->Release();
							shadowBlurTempMip1SRV = nullptr;
						}
						if (shadowBlurTempMip0UAV) {
							shadowBlurTempMip0UAV->Release();
							shadowBlurTempMip0UAV = nullptr;
						}
						if (shadowBlurTempMip1UAV) {
							shadowBlurTempMip1UAV->Release();
							shadowBlurTempMip1UAV = nullptr;
						}
						if (shadowBlurTempTexture) {
							shadowBlurTempTexture->Release();
							shadowBlurTempTexture = nullptr;
						}

						shadowCopyWidth = newWidth;
						shadowCopyHeight = newHeight;

						D3D11_TEXTURE2D_DESC copyDesc{};
						copyDesc.Width = newWidth;
						copyDesc.Height = newHeight;
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

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
						srvDesc.Format = copyDesc.Format;
						srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
						srvDesc.Texture2D.MostDetailedMip = 0;
						srvDesc.Texture2D.MipLevels = 2;
						DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopySRV));

						// Create mip-specific SRVs for blur passes
						srvDesc.Texture2D.MostDetailedMip = 0;
						srvDesc.Texture2D.MipLevels = 1;
						DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip0SRV));

						srvDesc.Texture2D.MostDetailedMip = 1;
						srvDesc.Texture2D.MipLevels = 1;
						DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip1SRV));

						D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
						uavDesc.Format = copyDesc.Format;
						uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
						uavDesc.Texture2D.MipSlice = 0;
						DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip0UAV));

						uavDesc.Texture2D.MipSlice = 1;
						DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip1UAV));

						// Create temporary texture for blur intermediate result
						DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowBlurTempTexture));

						// Create mip-specific SRVs for blur temp texture
						srvDesc.Texture2D.MostDetailedMip = 0;
						srvDesc.Texture2D.MipLevels = 1;
						DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip0SRV));

						srvDesc.Texture2D.MostDetailedMip = 1;
						srvDesc.Texture2D.MipLevels = 1;
						DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip1SRV));

						uavDesc.Texture2D.MipSlice = 0;
						DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip0UAV));

						uavDesc.Texture2D.MipSlice = 1;
						DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip1UAV));
					}

					// Dispatch downsample compute shader
					ID3D11ShaderResourceView* csSrvs[1]{ shadowView };
					context->CSSetShaderResources(0, 1, csSrvs);

					context->CSSetSamplers(0, 1, &linearSampler);

					auto shadowFullSize = newWidth * 4;

					// Mip 0 with second cascade
					ID3D11UnorderedAccessView* csUavs[1]{ shadowCopyMip0UAV };
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip0CS, nullptr, 0);
					context->Dispatch((shadowFullSize + 7) >> 3, (shadowFullSize + 7) >> 3, 1);

					// Mip 1 with first cascade
					csUavs[0] = shadowCopyMip1UAV;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip1CS, nullptr, 0);
					context->Dispatch((shadowFullSize + 7) >> 3, (shadowFullSize + 7) >> 3, 1);

					// Unbind shadow view before blur passes
					csSrvs[0] = nullptr;
					context->CSSetShaderResources(0, 1, csSrvs);
					csUavs[0] = nullptr;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

					// 11x11 separable blur for Mip 0
					{
						uint32_t mip0Width = newWidth;
						uint32_t mip0Height = newHeight;
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip0 -> shadowBlurTemp mip0
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip0SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip0UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS, nullptr, 0);
						context->Dispatch((mip0Width + GROUP_SIZE - 1) / GROUP_SIZE, mip0Height, 1);

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
						context->Dispatch(mip0Width, (mip0Height + GROUP_SIZE - 1) / GROUP_SIZE, 1);

						// Unbind
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					}

					// 11x11 separable blur for Mip 1
					{
						uint32_t mip1Width = newWidth / 2;
						uint32_t mip1Height = newHeight / 2;
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip1 -> shadowBlurTemp mip1
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip1SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip1UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS, nullptr, 0);
						context->Dispatch((mip1Width + GROUP_SIZE - 1) / GROUP_SIZE, mip1Height, 1);

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
						context->Dispatch(mip1Width, (mip1Height + GROUP_SIZE - 1) / GROUP_SIZE, 1);

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

		ID3D11ShaderResourceView* srvs[2]{
			shadowCopySRV ? shadowCopySRV : shadowView,
			perShadow->srv.get(),
		};

		context->PSSetShaderResources(18, ARRAYSIZE(srvs), srvs);

		// Release COM object to prevent memory leak
		if (shadowView)
			shadowView->Release();
	}
}

void EffectShadows::LoadSettings(json&)
{
	// No settings currently
}

void EffectShadows::SaveSettings(json&)
{
	// No settings currently
}

void EffectShadows::RestoreDefaultSettings()
{
	// No settings currently
}

bool EffectShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

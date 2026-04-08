#include "ENBLocalTonemapping.h"

#include "../SettingManager.h"
#include "../TextureManager.h"
#include "Utils/D3D.h"
#include "State.h"

void ENBLocalTonemapping::Initialize()
{
	CreateShaders();
}

void ENBLocalTonemapping::CreateShaders()
{
	const wchar_t* shaderPath = L"Data\\Shaders\\LocalTonemapping.hlsl";

	luminanceCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_Luminance"));
	exposureWeightCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_ExposureWeight"));
	blendCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_Blend"));
	blendLaplacianCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_BlendLaplacian"));
	finalCombineCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_FinalCombine"));
	mipmapCS.attach((ID3D11ComputeShader*)Util::CompileShader(shaderPath, {}, "cs_5_0", "CS_GenerateMip"));

	auto device = globals::d3d::device;
	auto createCB = [&](winrt::com_ptr<ID3D11Buffer>& cb, uint32_t size) {
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = (size + 15) & ~15;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, cb.put()));
	};

	createCB(luminanceCB, sizeof(LuminanceArgs));
	createCB(weightCB, sizeof(ExposureWeightArgs));
	createCB(blendCB, sizeof(BlendArgs));
	createCB(laplacianCB, sizeof(BlendLaplacianArgs));
	createCB(finalCombineCB, sizeof(FinalCombineArgs));
	createCB(mipmapCB, sizeof(MipmapArgs));
}

void ENBLocalTonemapping::CreateTextures(uint32_t width, uint32_t height)
{
	if (width == m_width && height == m_height && m_mipCount > 0)
		return;

	m_width = width;
	m_height = height;
	m_mipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

	auto device = globals::d3d::device;

	auto createPyramid = [&](TexturePyramid& pyramid, DXGI_FORMAT format, const char* name) {
		(void)name;
		pyramid.textures.clear();
		pyramid.srvs.clear();
		pyramid.uavs.clear();
		pyramid.width = width;
		pyramid.height = height;
		pyramid.mips = m_mipCount;

		uint32_t w = width;
		uint32_t h = height;

		for (uint32_t i = 0; i < m_mipCount; i++) {
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = w;
			desc.Height = h;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			winrt::com_ptr<ID3D11Texture2D> tex;
			DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, tex.put()));
			pyramid.textures.push_back(tex);

			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			DX::ThrowIfFailed(device->CreateShaderResourceView(tex.get(), nullptr, srv.put()));
			pyramid.srvs.push_back(srv);

			winrt::com_ptr<ID3D11UnorderedAccessView> uav;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(tex.get(), nullptr, uav.put()));
			pyramid.uavs.push_back(uav);

			w = std::max(1u, w / 2);
			h = std::max(1u, h / 2);
		}
	};

	createPyramid(mipsLuminance, DXGI_FORMAT_R16G16B16A16_FLOAT, "LocalTonemapping::Luminance");
	createPyramid(mipsWeights, DXGI_FORMAT_R16G16B16A16_FLOAT, "LocalTonemapping::Weights");
	createPyramid(mipsAssemble, DXGI_FORMAT_R16_FLOAT, "LocalTonemapping::Assemble");
}

void ENBLocalTonemapping::Execute()
{
	auto& settingManager = SettingManager::GetSingleton();
	if (!settingManager.GetValue<bool>("EnableLocalTonemapping", "EFFECT"))
		return;

	auto context = globals::d3d::context;

	auto& textureManager = TextureManager::GetSingleton();
	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");

	D3D11_TEXTURE2D_DESC desc;
	textureSDRTemp->texture->GetDesc(&desc);

	CreateTextures(desc.Width, desc.Height);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto updateCB = [&](winrt::com_ptr<ID3D11Buffer>& cb, const void* data, uint32_t size) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			memcpy(mapped.pData, data, size);
			context->Unmap(cb.get(), 0);
		}
	};

	// 1. Luminance Pass
	{
		LuminanceArgs args{};
		args.exposure = settingManager.GetInterpolatedTimeOfDayValue("Exposure", "LOCALTONEMAPPING");
		args.shadows = std::pow(2.0f, settingManager.GetInterpolatedTimeOfDayValue("Shadows", "LOCALTONEMAPPING"));
		args.highlights = std::pow(2.0f, -settingManager.GetInterpolatedTimeOfDayValue("Highlights", "LOCALTONEMAPPING"));
		args.useLegacyACES = false;
		updateCB(luminanceCB, &args, sizeof(args));

		context->CSSetShader(luminanceCS.get(), nullptr, 0);
		
		ID3D11Buffer* cbArray[] = { luminanceCB.get() };
		context->CSSetConstantBuffers(0, 1, cbArray);
		
		ID3D11ShaderResourceView* srvArray[] = { textureSDRTemp->srv.get() };
		context->CSSetShaderResources(0, 1, srvArray);
		
		ID3D11UnorderedAccessView* uavArray[] = { mipsLuminance.uavs[0].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);

		context->Dispatch((desc.Width + 15) / 16, (desc.Height + 15) / 16, 1);
		
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
	}

	// 2. Weight Pass
	{
		ExposureWeightArgs args{};
		args.sigmaSq = exposurePreferenceSigma;
		args.offset = exposurePreferenceOffset;
		updateCB(weightCB, &args, sizeof(args));

		context->CSSetShader(exposureWeightCS.get(), nullptr, 0);
		
		ID3D11Buffer* cbArray[] = { weightCB.get() };
		context->CSSetConstantBuffers(0, 1, cbArray);
		
		ID3D11ShaderResourceView* srvArray[] = { mipsLuminance.srvs[0].get() };
		context->CSSetShaderResources(0, 1, srvArray);
		
		ID3D11UnorderedAccessView* uavArray[] = { mipsWeights.uavs[0].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
		
		context->Dispatch((desc.Width + 15) / 16, (desc.Height + 15) / 16, 1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// 3. Generate Mips
	auto generateMips = [&](TexturePyramid& pyramid) {
		context->CSSetShader(mipmapCS.get(), nullptr, 0);
		ID3D11SamplerState* samplers[] = { textureManager.GetLinearSampler() };
		context->CSSetSamplers(0, 1, samplers);

		for (uint32_t i = 1; i < m_mipCount; i++) {
			uint32_t w = std::max(1u, pyramid.width >> i);
			uint32_t h = std::max(1u, pyramid.height >> i);
			
			MipmapArgs args{};
			args.resolution[0] = w;
			args.resolution[1] = h;
			args.texelSize[0] = 1.0f / (pyramid.width >> (i-1));
			args.texelSize[1] = 1.0f / (pyramid.height >> (i-1));
			updateCB(mipmapCB, &args, sizeof(args));

			ID3D11Buffer* cbArray[] = { mipmapCB.get() };
			context->CSSetConstantBuffers(0, 1, cbArray);
			
			ID3D11ShaderResourceView* srvArray[] = { pyramid.srvs[i-1].get() };
			context->CSSetShaderResources(0, 1, srvArray);
			
			ID3D11UnorderedAccessView* uavArray[] = { pyramid.uavs[i].get() };
			context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
			
			context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
			
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		}
	};
	generateMips(mipsLuminance);
	generateMips(mipsWeights);

	// 4. Blend Pass
	uint32_t startMip = std::min((uint32_t)mip, m_mipCount - 1);
	{
		uint32_t w = std::max(1u, m_width >> startMip);
		uint32_t h = std::max(1u, m_height >> startMip);
		
		BlendArgs args{};
		updateCB(blendCB, &args, sizeof(args));

		context->CSSetShader(blendCS.get(), nullptr, 0);
		
		ID3D11Buffer* cbArray[] = { blendCB.get() };
		context->CSSetConstantBuffers(0, 1, cbArray);
		
		ID3D11ShaderResourceView* srvs[] = { mipsLuminance.srvs[startMip].get(), mipsWeights.srvs[startMip].get() };
		context->CSSetShaderResources(0, 2, srvs);
		
		ID3D11UnorderedAccessView* uavArray[] = { mipsAssemble.uavs[startMip].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
		
		context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// 5. Laplacian Blend
	uint32_t endMip = std::min((uint32_t)displayMip, m_mipCount - 1);
	for (int i = (int)startMip; i > (int)endMip; i--) {
		uint32_t w = std::max(1u, m_width >> (i - 1));
		uint32_t h = std::max(1u, m_height >> (i - 1));

		BlendLaplacianArgs args{};
		args.resolution[0] = w;
		args.resolution[1] = h;
		args.boostLocalContrast = boostLocalContrast;
		updateCB(laplacianCB, &args, sizeof(args));

		context->CSSetShader(blendLaplacianCS.get(), nullptr, 0);
		
		ID3D11Buffer* cbArray[] = { laplacianCB.get() };
		context->CSSetConstantBuffers(0, 1, cbArray);
		
		ID3D11ShaderResourceView* srvs[] = { 
			mipsLuminance.srvs[i-1].get(), 
			mipsLuminance.srvs[i].get(), 
			mipsWeights.srvs[i-1].get(), 
			mipsAssemble.srvs[i].get() 
		};
		context->CSSetShaderResources(0, 4, srvs);
		
		ID3D11UnorderedAccessView* uavArray[] = { mipsAssemble.uavs[i-1].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
		
		ID3D11SamplerState* samplers[] = { textureManager.GetLinearSampler() };
		context->CSSetSamplers(0, 1, samplers);

		context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
		
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// 6. Final Combine
	{
		FinalCombineArgs args{};
		uint32_t mw = std::max(1u, m_width >> endMip);
		uint32_t mh = std::max(1u, m_height >> endMip);
		args.mipPixelSize[0] = 1.0f / mw;
		args.mipPixelSize[1] = 1.0f / mh;
		args.mipPixelSize[2] = (float)mw;
		args.mipPixelSize[3] = (float)mh;
		args.resolution[0] = desc.Width;
		args.resolution[1] = desc.Height;
		args.exposure = settingManager.GetInterpolatedTimeOfDayValue("Exposure", "LOCALTONEMAPPING");
		args.finalizeWithACES = finalizeWithACES;
		args.performSRGBConversion = false;
		args.useLegacyACES = false;
		updateCB(finalCombineCB, &args, sizeof(args));

		context->CSSetShader(finalCombineCS.get(), nullptr, 0);
		
		ID3D11Buffer* cbArray[] = { finalCombineCB.get() };
		context->CSSetConstantBuffers(0, 1, cbArray);
		
		ID3D11ShaderResourceView* srvs[] = { 
			mipsLuminance.srvs[0].get(), 
			mipsWeights.srvs[0].get(), 
			mipsLuminance.srvs[endMip].get(), 
			mipsAssemble.srvs[endMip].get(),
			nullptr,
			nullptr
		};
		
		context->CSSetShaderResources(0, 6, srvs);
		
		ID3D11UnorderedAccessView* uavArray[] = { textureSDRTemp->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
		
		ID3D11SamplerState* samplers[] = { textureManager.GetLinearSampler() };
		context->CSSetSamplers(0, 1, samplers);

		context->Dispatch((desc.Width + 15) / 16, (desc.Height + 15) / 16, 1);
	}

	// Clean up
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	ID3D11ShaderResourceView* nullSRVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, 6, nullSRVs);
}

void ENBLocalTonemapping::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();
	mip = (int)settingManager.GetValue<float>("Mip", "LOCALTONEMAPPING");
	displayMip = (int)settingManager.GetValue<float>("DisplayMip", "LOCALTONEMAPPING");
	boostLocalContrast = settingManager.GetValue<bool>("BoostLocalContrast", "LOCALTONEMAPPING");
	exposurePreferenceSigma = settingManager.GetValue<float>("ExposurePreferenceSigma", "LOCALTONEMAPPING");
	exposurePreferenceOffset = settingManager.GetValue<float>("ExposurePreferenceOffset", "LOCALTONEMAPPING");
}

void ENBLocalTonemapping::RenderImGui() {}
bool ENBLocalTonemapping::Apply() { return true; }
bool ENBLocalTonemapping::Load() { return true; }
void ENBLocalTonemapping::Save() {}

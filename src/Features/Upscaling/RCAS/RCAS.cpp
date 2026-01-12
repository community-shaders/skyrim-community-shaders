#include "RCAS.h"

#include "../../../Deferred.h"
#include "../../../State.h"
#include "../../../Util.h"

struct RCASConfig
{
	float sharpness;
	float3 pad;
};

RCAS::~RCAS()
{
	delete rcasConfigCB;
	rcasConfigCB = nullptr;
}

void RCAS::Initialize()
{
	if (rcasConfigCB)
		return;

	logger::info("[RCAS] Creating resources");
	CreateComputeShader();
	rcasConfigCB = new ConstantBuffer(ConstantBufferDesc<RCASConfig>());
}

void RCAS::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	rcasComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
}

void RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	if (!rcasComputeShader) {
		logger::warn("[RCAS] Compute shader not compiled");
		return;
	}

	state->BeginPerfEvent("RCAS Sharpening");

	uint32_t screenWidth = (uint32_t)state->screenSize.x;
	uint32_t screenHeight = (uint32_t)state->screenSize.y;

	RCASConfig config{};
	config.sharpness = sharpness;

	rcasConfigCB->Update(config);
	auto bufferArray = rcasConfigCB->CB();

	context->CSSetShader(rcasComputeShader.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &bufferArray);

	ID3D11ShaderResourceView* srvs[] = { inputSRV };
	context->CSSetShaderResources(0, 1, srvs);

	ID3D11UnorderedAccessView* uavs[] = { outputUAV };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	uint32_t dispatchX = (screenWidth + 7) / 8;
	uint32_t dispatchY = (screenHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}

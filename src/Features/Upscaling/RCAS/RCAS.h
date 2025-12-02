#pragma once

#include "../../../Buffer.h"
#include "../../../State.h"

#include <d3d11_4.h>
#include <winrt/base.h>

class RCAS
{
public:
	RCAS() = default;

	void Initialize();
	void ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness = 0.15f);

private:
	void CreateComputeShader();

	winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;
	ConstantBuffer* rcasConfigCB = nullptr;
};

#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"

struct Weather : Feature
{
public:
	static Weather* GetSingleton()
	{
		static Weather singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Weather"; }
	virtual inline std::string GetShortName() override { return "Weather"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WEATHER"; }

	Texture2D* diffuseIBLTexture = nullptr;
	ID3D11ComputeShader* diffuseIBLCS = nullptr;

	void Bind();

	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	ID3D11ComputeShader* GetDiffuseIBLCS();
	void LerpWeather(RE::TESWeather*, RE::TESWeather*, float);
};

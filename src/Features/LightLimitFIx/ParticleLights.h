#pragma once

class ParticleLights
{
public:
	static ParticleLights* GetSingleton()
	{
		static ParticleLights singleton;
		return &singleton;
	}

	struct Config
	{
		bool cull = false;
		RE::NiColor colorMult{ 1.0f, 1.0f, 1.0f };
		float radiusMult = 1.0f;
		float saturationMult = 1.0f;
	};

	struct GradientConfig
	{
		RE::NiColor color;
	};

	ankerl::unordered_dense::map<std::string, Config> particleLightConfigs;
	ankerl::unordered_dense::map<std::string, GradientConfig> particleLightGradientConfigs;

	void GetConfigs();
};
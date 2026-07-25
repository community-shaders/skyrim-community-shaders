#pragma once

#include <cstdint>

namespace Effect11PL
{
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

	struct ConfigStore
	{
		ankerl::unordered_dense::map<std::string, Config> configs;
		ankerl::unordered_dense::map<std::string, GradientConfig> gradientConfigs;
		std::uint64_t configVersion = 0;

		void Load();
	};
}

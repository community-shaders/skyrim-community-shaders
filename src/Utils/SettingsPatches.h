#pragma once

#include <string>
#include <vector>

class Effect;

namespace Util
{
	namespace SettingsPatches
	{
		struct Patch
		{
			std::string variable;
			std::string value;
		};

		struct Entry
		{
			std::string file;
			std::vector<Patch> patches;
		};

		void Load();
		void Apply(Effect& effect);
	}
}

#pragma once

#include <string>
#include <vector>

namespace Util
{
	namespace ShaderPatches
	{
		struct Replacement
		{
			std::string find;
			std::string replace;
		};

		struct Entry
		{
			std::string file;
			std::vector<Replacement> replacements;
		};

		void Load();
		bool Apply(const char* filename, std::vector<char>& buffer);
		bool Apply(const char* filename, std::string& content);
	}
}

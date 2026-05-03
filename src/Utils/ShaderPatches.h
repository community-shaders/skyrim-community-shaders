#pragma once

#include <filesystem>
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

		struct UIDefineInfo
		{
			std::string defineName;
			std::string displayName;
			std::string group;
			std::string type;
			std::string value;
			std::string widget;
			std::string list;
			int intMin = 0;
			int intMax = 100;
			float floatMin = 0.0f;
			float floatMax = 1.0f;
			float floatStep = 0.01f;
			int ordering = 0;
		};

		void Load();
		bool Apply(const char* filename, std::vector<char>& buffer);
		bool Apply(const char* filename, std::string& content);

		std::string DecodeENBSource(const std::string& content);
		void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, const std::string& iniPath = "", const std::string& iniSection = "");

		const std::vector<UIDefineInfo>& GetLastUIDefines();

		void ConvertFxGroups(std::string& content);
	}
}

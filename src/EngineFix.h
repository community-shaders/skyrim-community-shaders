#pragma once

struct EngineFix
{
	virtual ~EngineFix() = default;

	virtual std::string GetName() = 0;

	virtual void Install() {}

	static void InstallOnPostPostLoadFixes();
	static void InstallOnDataLoadedFixes();

private:
	static const std::vector<EngineFix*>& GetOnPostPostLoadFixesList();
	static const std::vector<EngineFix*>& GetOnDataLoadedFixesList();
	static void InstallFixes(const std::vector<EngineFix*>& fixes);
};

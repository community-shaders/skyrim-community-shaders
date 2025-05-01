#pragma once

struct BugFix
{
	virtual ~BugFix() = default;

	virtual std::string GetName() = 0;

	virtual void Install() {}

	static void InstallBugFixes();

private:
	static const std::vector<BugFix*>& GetBugFixList();
};

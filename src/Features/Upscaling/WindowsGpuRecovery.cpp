#include "WindowsGpuRecovery.h"

#include <Windows.h>
#include <string>

void WindowsGpuRecovery::Request()
{
	wchar_t systemDir[MAX_PATH]{};
	if (!GetSystemDirectoryW(systemDir, MAX_PATH))
		return;
	std::wstring command = L"\"" + std::wstring(systemDir) +
		L"\\schtasks.exe\" /Run /TN \"CommunityShaders GPU Recovery\"";
	STARTUPINFOW startup{ sizeof(startup) };
	PROCESS_INFORMATION process{};
	if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		logger::critical("[DLSSG-Watchdog] requested forced TDR through scheduled recovery task");
	} else {
		logger::critical("[DLSSG-Watchdog] recovery task launch failed (Win32 error {}); run community_shaders_recover_hung_gpu.ps1 manually",
			GetLastError());
	}
}

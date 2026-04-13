// dear imgui: Platform Backend for Windows (standard windows API for 32-bits AND 64-bits applications)
// Ported from OpenAnimationReplacer for Community Shaders

#include "imgui_impl_win32.h"
#include <imgui.h>
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <dwmapi.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

// Using XInput for gamepad (will load DLL dynamically)
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#	include <xinput.h>
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
#endif

struct ImGui_ImplWin32_Data
{
	HWND hWnd;
	HWND MouseHwnd;
	int MouseTrackedArea;
	int MouseButtonsDown;
	INT64 Time;
	INT64 TicksPerSecond;
	ImGuiMouseCursor LastMouseCursor;

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
	bool HasGamepad;
	bool WantUpdateHasGamepad;
	HMODULE XInputDLL;
	PFN_XInputGetCapabilities XInputGetCapabilities;
	PFN_XInputGetState XInputGetState;
#endif

	ImGui_ImplWin32_Data()
	{
		memset((void*)this, 0, sizeof(*this));
	}
};

static ImGui_ImplWin32_Data* ImGui_ImplWin32_GetBackendData()
{
	return ImGui::GetCurrentContext() ? (ImGui_ImplWin32_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static bool ImGui_ImplWin32_InitEx(void* hwnd, bool platform_has_own_dc)
{
	ImGuiIO& io = ImGui::GetIO();
	IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

	INT64 perf_frequency, perf_counter;
	if (!::QueryPerformanceFrequency((LARGE_INTEGER*)&perf_frequency))
		return false;
	if (!::QueryPerformanceCounter((LARGE_INTEGER*)&perf_counter))
		return false;

	ImGui_ImplWin32_Data* bd = IM_NEW(ImGui_ImplWin32_Data)();
	io.BackendPlatformUserData = (void*)bd;
	io.BackendPlatformName = "imgui_impl_win32";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

	bd->hWnd = (HWND)hwnd;
	bd->TicksPerSecond = perf_frequency;
	bd->Time = perf_counter;
	bd->LastMouseCursor = ImGuiMouseCursor_COUNT;

	ImGui::GetMainViewport()->PlatformHandleRaw = (void*)hwnd;
	IM_UNUSED(platform_has_own_dc);

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
	bd->WantUpdateHasGamepad = true;
	const char* xinput_dll_names[] = {
		"xinput1_4.dll",
		"xinput1_3.dll",
		"xinput9_1_0.dll",
		"xinput1_2.dll",
		"xinput1_1.dll"
	};
	for (int n = 0; n < IM_ARRAYSIZE(xinput_dll_names); n++)
		if (HMODULE dll = ::LoadLibraryA(xinput_dll_names[n])) {
			bd->XInputDLL = dll;
			bd->XInputGetCapabilities = (PFN_XInputGetCapabilities)::GetProcAddress(dll, "XInputGetCapabilities");
			bd->XInputGetState = (PFN_XInputGetState)::GetProcAddress(dll, "XInputGetState");
			break;
		}
#endif

	return true;
}

IMGUI_IMPL_API bool ImGui_ImplWin32_Init(void* hwnd)
{
	return ImGui_ImplWin32_InitEx(hwnd, false);
}

IMGUI_IMPL_API bool ImGui_ImplWin32_InitForOpenGL(void* hwnd)
{
	return ImGui_ImplWin32_InitEx(hwnd, true);
}

void ImGui_ImplWin32_Shutdown()
{
	ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
	IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
	ImGuiIO& io = ImGui::GetIO();

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
	if (bd->XInputDLL)
		::FreeLibrary(bd->XInputDLL);
#endif

	io.BackendPlatformName = nullptr;
	io.BackendPlatformUserData = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);
	IM_DELETE(bd);
}

static bool ImGui_ImplWin32_UpdateMouseCursor()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
		return false;

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
		::SetCursor(nullptr);
	} else {
		LPTSTR win32_cursor = IDC_ARROW;
		switch (imgui_cursor) {
		case ImGuiMouseCursor_Arrow:
			win32_cursor = IDC_ARROW;
			break;
		case ImGuiMouseCursor_TextInput:
			win32_cursor = IDC_IBEAM;
			break;
		case ImGuiMouseCursor_ResizeAll:
			win32_cursor = IDC_SIZEALL;
			break;
		case ImGuiMouseCursor_ResizeEW:
			win32_cursor = IDC_SIZEWE;
			break;
		case ImGuiMouseCursor_ResizeNS:
			win32_cursor = IDC_SIZENS;
			break;
		case ImGuiMouseCursor_ResizeNESW:
			win32_cursor = IDC_SIZENESW;
			break;
		case ImGuiMouseCursor_ResizeNWSE:
			win32_cursor = IDC_SIZENWSE;
			break;
		case ImGuiMouseCursor_Hand:
			win32_cursor = IDC_HAND;
			break;
		case ImGuiMouseCursor_NotAllowed:
			win32_cursor = IDC_NO;
			break;
		}
		::SetCursor(::LoadCursor(nullptr, win32_cursor));
	}
	return true;
}

static void ImGui_ImplWin32_UpdateMouseData()
{
	ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
	ImGuiIO& io = ImGui::GetIO();
	IM_ASSERT(bd->hWnd != 0);

	HWND focused_window = ::GetForegroundWindow();
	const bool is_app_focused = (focused_window == bd->hWnd);
	if (is_app_focused) {
		if (io.WantSetMousePos) {
			POINT pos = { (int)io.MousePos.x, (int)io.MousePos.y };
			if (::ClientToScreen(bd->hWnd, &pos))
				::SetCursorPos(pos.x, pos.y);
		}

		if (!io.WantSetMousePos && bd->MouseTrackedArea == 0) {
			POINT pos;
			if (::GetCursorPos(&pos) && ::ScreenToClient(bd->hWnd, &pos)) {
				auto userData = static_cast<OAR_ImGuiUserData*>(io.UserData);
				io.AddMousePosEvent((float)pos.x * userData->screenScaleRatio.x, (float)pos.y * userData->screenScaleRatio.y);
			}
		}
	}
}

// Gamepad navigation mapping
static void ImGui_ImplWin32_UpdateGamepads()
{
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();

	if (bd->WantUpdateHasGamepad) {
		XINPUT_CAPABILITIES caps = {};
		bd->HasGamepad = bd->XInputGetCapabilities ? (bd->XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) : false;
		bd->WantUpdateHasGamepad = false;
	}

	io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
	XINPUT_STATE xinput_state;
	XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;
	if (!bd->HasGamepad || bd->XInputGetState == nullptr || bd->XInputGetState(0, &xinput_state) != ERROR_SUCCESS)
		return;
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

#	define IM_SATURATE(V) (V < 0.0f ? 0.0f : V > 1.0f ? 1.0f : \
														 V)
#	define MAP_BUTTON(KEY_NO, BUTTON_ENUM)                                \
		{                                                                  \
			io.AddKeyEvent(KEY_NO, (gamepad.wButtons & BUTTON_ENUM) != 0); \
		}
#	define MAP_ANALOG(KEY_NO, VALUE, V0, V1)                          \
		{                                                              \
			float vn = (float)(VALUE - V0) / (float)(V1 - V0);         \
			io.AddKeyAnalogEvent(KEY_NO, vn > 0.10f, IM_SATURATE(vn)); \
		}
	MAP_BUTTON(ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START);
	MAP_BUTTON(ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK);
	MAP_BUTTON(ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X);
	MAP_BUTTON(ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B);
	MAP_BUTTON(ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y);
	MAP_BUTTON(ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A);
	MAP_BUTTON(ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
	MAP_BUTTON(ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
	MAP_BUTTON(ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP);
	MAP_BUTTON(ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN);
	MAP_BUTTON(ImGuiKey_GamepadL1, XINPUT_GAMEPAD_LEFT_SHOULDER);
	MAP_BUTTON(ImGuiKey_GamepadR1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
	MAP_ANALOG(ImGuiKey_GamepadL2, gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
	MAP_ANALOG(ImGuiKey_GamepadR2, gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
	MAP_BUTTON(ImGuiKey_GamepadL3, XINPUT_GAMEPAD_LEFT_THUMB);
	MAP_BUTTON(ImGuiKey_GamepadR3, XINPUT_GAMEPAD_RIGHT_THUMB);
	MAP_ANALOG(ImGuiKey_GamepadLStickLeft, gamepad.sThumbLX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
	MAP_ANALOG(ImGuiKey_GamepadLStickRight, gamepad.sThumbLX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
	MAP_ANALOG(ImGuiKey_GamepadLStickUp, gamepad.sThumbLY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
	MAP_ANALOG(ImGuiKey_GamepadLStickDown, gamepad.sThumbLY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
	MAP_ANALOG(ImGuiKey_GamepadRStickLeft, gamepad.sThumbRX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
	MAP_ANALOG(ImGuiKey_GamepadRStickRight, gamepad.sThumbRX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
	MAP_ANALOG(ImGuiKey_GamepadRStickUp, gamepad.sThumbRY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
	MAP_ANALOG(ImGuiKey_GamepadRStickDown, gamepad.sThumbRY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
#	undef MAP_BUTTON
#	undef MAP_ANALOG
#endif
}

void ImGui_ImplWin32_NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
	IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplWin32_Init()?");

	// Setup time step
	INT64 current_time = 0;
	::QueryPerformanceCounter((LARGE_INTEGER*)&current_time);
	io.DeltaTime = (float)(current_time - bd->Time) / bd->TicksPerSecond;
	bd->Time = current_time;

	// Update OS mouse position
	ImGui_ImplWin32_UpdateMouseData();

	// Update OS mouse cursor with the cursor requested by imgui
	ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
	if (bd->LastMouseCursor != mouse_cursor) {
		bd->LastMouseCursor = mouse_cursor;
		ImGui_ImplWin32_UpdateMouseCursor();
	}

	// Update game controllers (if enabled and available)
	ImGui_ImplWin32_UpdateGamepads();
}

// DPI-related helpers

static BOOL _IsWindowsVersionOrGreater(WORD major, WORD minor, WORD)
{
	typedef LONG(WINAPI * PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*, ULONG, ULONGLONG);
	static PFN_RtlVerifyVersionInfo RtlVerifyVersionInfoFn = nullptr;
	if (RtlVerifyVersionInfoFn == nullptr)
		if (HMODULE ntdllModule = ::GetModuleHandleA("ntdll.dll"))
			RtlVerifyVersionInfoFn = (PFN_RtlVerifyVersionInfo)GetProcAddress(ntdllModule, "RtlVerifyVersionInfo");
	if (RtlVerifyVersionInfoFn == nullptr)
		return FALSE;

	RTL_OSVERSIONINFOEXW versionInfo = {};
	ULONGLONG conditionMask = 0;
	versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
	versionInfo.dwMajorVersion = major;
	versionInfo.dwMinorVersion = minor;
	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
	return (RtlVerifyVersionInfoFn(&versionInfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask) == 0) ? TRUE : FALSE;
}

#define _IsWindowsVistaOrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0600), LOBYTE(0x0600), 0)
#define _IsWindows8OrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0602), LOBYTE(0x0602), 0)
#define _IsWindows8Point1OrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0603), LOBYTE(0x0603), 0)
#define _IsWindows10OrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0A00), LOBYTE(0x0A00), 0)

#ifndef DPI_ENUMS_DECLARED
typedef enum
{
	PROCESS_DPI_UNAWARE = 0,
	PROCESS_SYSTEM_DPI_AWARE = 1,
	PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;
typedef enum
{
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#	define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE (DPI_AWARENESS_CONTEXT) - 3
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#	define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (DPI_AWARENESS_CONTEXT) - 4
#endif
typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);
typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
typedef DPI_AWARENESS_CONTEXT(WINAPI* PFN_SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

void ImGui_ImplWin32_EnableDpiAwareness()
{
	if (_IsWindows10OrGreater()) {
		static HINSTANCE user32_dll = ::LoadLibraryA("user32.dll");
		if (PFN_SetThreadDpiAwarenessContext SetThreadDpiAwarenessContextFn = (PFN_SetThreadDpiAwarenessContext)::GetProcAddress(user32_dll, "SetThreadDpiAwarenessContext")) {
			SetThreadDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			return;
		}
	}
	if (_IsWindows8Point1OrGreater()) {
		static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll");
		if (PFN_SetProcessDpiAwareness SetProcessDpiAwarenessFn = (PFN_SetProcessDpiAwareness)::GetProcAddress(shcore_dll, "SetProcessDpiAwareness")) {
			SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
			return;
		}
	}
#if _WIN32_WINNT >= 0x0600
	::SetProcessDPIAware();
#endif
}

#if defined(_MSC_VER) && !defined(NOGDI)
#	pragma comment(lib, "gdi32")
#endif

float ImGui_ImplWin32_GetDpiScaleForMonitor(void* monitor)
{
	UINT xdpi = 96, ydpi = 96;
	if (_IsWindows8Point1OrGreater()) {
		static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll");
		static PFN_GetDpiForMonitor GetDpiForMonitorFn = nullptr;
		if (GetDpiForMonitorFn == nullptr && shcore_dll != nullptr)
			GetDpiForMonitorFn = (PFN_GetDpiForMonitor)::GetProcAddress(shcore_dll, "GetDpiForMonitor");
		if (GetDpiForMonitorFn != nullptr) {
			GetDpiForMonitorFn((HMONITOR)monitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi);
			IM_ASSERT(xdpi == ydpi);
			return xdpi / 96.0f;
		}
	}
#ifndef NOGDI
	const HDC dc = ::GetDC(nullptr);
	xdpi = ::GetDeviceCaps(dc, LOGPIXELSX);
	ydpi = ::GetDeviceCaps(dc, LOGPIXELSY);
	IM_ASSERT(xdpi == ydpi);
	::ReleaseDC(nullptr, dc);
#endif
	return xdpi / 96.0f;
}

float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd)
{
	HMONITOR monitor = ::MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONEAREST);
	return ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
}

#if defined(_MSC_VER)
#	pragma comment(lib, "dwmapi")
#endif

void ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd)
{
	if (!_IsWindowsVistaOrGreater())
		return;

	BOOL composition;
	if (FAILED(::DwmIsCompositionEnabled(&composition)) || !composition)
		return;

	BOOL opaque;
	DWORD color;
	if (_IsWindows8OrGreater() || (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque)) && !opaque)) {
		HRGN region = ::CreateRectRgn(0, 0, -1, -1);
		DWM_BLURBEHIND bb = {};
		bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
		bb.hRgnBlur = region;
		bb.fEnable = TRUE;
		::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
		::DeleteObject(region);
	} else {
		DWM_BLURBEHIND bb = {};
		bb.dwFlags = DWM_BB_ENABLE;
		::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
	}
}

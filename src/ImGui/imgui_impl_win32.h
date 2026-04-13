// dear imgui: Platform Backend for Windows (standard windows API for 32-bits AND 64-bits applications)
// Ported from OpenAnimationReplacer for Community Shaders
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

#pragma once
#include <imgui.h>

IMGUI_IMPL_API bool ImGui_ImplWin32_Init(void* hwnd);
IMGUI_IMPL_API bool ImGui_ImplWin32_InitForOpenGL(void* hwnd);
IMGUI_IMPL_API void ImGui_ImplWin32_Shutdown();
IMGUI_IMPL_API void ImGui_ImplWin32_NewFrame();

// DPI-related helpers (optional)
IMGUI_IMPL_API void ImGui_ImplWin32_EnableDpiAwareness();
IMGUI_IMPL_API float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd);
IMGUI_IMPL_API float ImGui_ImplWin32_GetDpiScaleForMonitor(void* monitor);

// Transparency related helpers (optional) [experimental]
IMGUI_IMPL_API void ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd);

struct OAR_ImGuiUserData
{
	struct
	{
		float x = 1.f;
		float y = 1.f;
	} screenScaleRatio;
};

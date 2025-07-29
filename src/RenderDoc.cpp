#include "RenderDoc.h"

constexpr const wchar_t* RENDERDOC_DLL_PATH = L"Data\\SKSE\\Plugins\\Renderdoc\\renderdoc.dll";
constexpr const char* CAPTURE_PATH_TEMPLATE = ".\\Data\\SKSE\\Plugins\\CommunityShaders\\Captures\\";

bool RenderDoc::Initialize()
{
	// Try to load the RenderDoc DLL
	renderDocModule = LoadLibraryW(RENDERDOC_DLL_PATH);
	if (!renderDocModule) {
		logger::debug("[RenderDoc] renderdoc.dll not found");
		return false;
	}

	// Get the API function pointer
	auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(renderDocModule, "RENDERDOC_GetAPI");
	if (!RENDERDOC_GetAPI) {
		logger::warn("[RenderDoc] Failed to get RENDERDOC_GetAPI function");
		FreeLibrary(renderDocModule);
		renderDocModule = nullptr;
		return false;
	}

	// Get the API interface
	int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&renderDocApi);
	if (ret != 1 || !renderDocApi) {
		logger::warn("[RenderDoc] Failed to get API interface");
		FreeLibrary(renderDocModule);
		renderDocModule = nullptr;
		renderDocApi = nullptr;
		return false;
	}

	renderDocApi->SetCaptureFilePathTemplate(CAPTURE_PATH_TEMPLATE);

	try {
		std::filesystem::create_directories(CAPTURE_PATH_TEMPLATE);
	} catch (const std::exception& e) {
		logger::warn("[RenderDoc] Failed to create capture directory: {}", e.what());
	}

	renderDocApi->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);

	logger::info("[RenderDoc] Successfully initialized");
	return true;
}

void RenderDoc::TriggerCapture()
{
	if (!IsAvailable()) {
		logger::warn("[RenderDoc] Cannot trigger capture - RenderDoc not available");
		return;
	}

	if (IsCapturing()) {
		logger::warn("[RenderDoc] Capture already in progress");
		return;
	}

	renderDocApi->TriggerCapture();
	logger::info("[RenderDoc] Frame capture triggered");
}

bool RenderDoc::IsCapturing() const
{
	if (!IsAvailable()) {
		return false;
	}

	return renderDocApi->IsFrameCapturing() != 0;
}

std::string RenderDoc::GetCapturePath(uint32_t a_index)
{
	if (!IsAvailable()) {
		return "";
	}

	char path[1024] = {};
	uint32_t pathLength = sizeof(path);
	uint64_t timestamp;

	renderDocApi->GetCapture(a_index, path, &pathLength, &timestamp);

	if (pathLength == 0 || pathLength >= sizeof(path)) {
		return "invalid path";
	}

	return std::string{ path };
}

uint32_t RenderDoc::GetNumCaptures() const
{
	if (!IsAvailable()) {
		return 0;
	}

	return renderDocApi->GetNumCaptures();
}

uint32_t RenderDoc::CalculateCapturesDiskUsage()
{
	try {
		auto path = std::filesystem::path{ ".\\Data\\SKSE\\Plugins\\CommunityShaders\\Captures\\" };

		uintmax_t totalBytes = 0;

		if (std::filesystem::exists(path)) {
			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.is_regular_file()) {
					totalBytes += entry.file_size();
				}
			}
		}

		return static_cast<uint32_t>(totalBytes / (1024 * 1024));  // Convert to MB
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Failed to calculate disk usage: {}", e.what());
		return 0;
	}
}

void RenderDoc::ClearFrameCaptures()
{
	try {
		auto path = std::filesystem::path{ ".\\Data\\SKSE\\Plugins\\CommunityShaders\\Captures\\" };

		if (std::filesystem::exists(path)) {
			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.is_regular_file()) {
					std::filesystem::remove(entry.path());
					logger::info("[RenderDoc] Deleted: {}", entry.path().string());
				}
			}
		}
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Failed to clear frame captures: {}", e.what());
	}
}
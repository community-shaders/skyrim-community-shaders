#include <dbghelp.h>

#include "Deferred.h"
#include "Features/RenderDoc.h"
#include "Features/Upscaling.h"
#include "FrameAnnotations.h"
#include "Globals.h"
#include "Hooks.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "ShaderCache.h"
#include "State.h"


#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

// Diagnostic crash capture. The frame-generation interop can take the process down during a
// load with no Windows Error Reporting record and no .dmp, which leaves nothing to analyse.
// A vectored handler runs before any SEH frame swallows the fault, so it sees the exception
// even when something downstream would otherwise turn it into a silent exit.
static LONG CALLBACK CommunityShadersCrashDump(EXCEPTION_POINTERS* a_exception)
{
	if (!a_exception || !a_exception->ExceptionRecord)
		return EXCEPTION_CONTINUE_SEARCH;

	const DWORD code = a_exception->ExceptionRecord->ExceptionCode;
	// Ignore the benign ones the game raises constantly (C++ EH, debugger notifications).
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
		code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
		code != 0xC0000409 /* fast-fail / stack cookie */)
		return EXCEPTION_CONTINUE_SEARCH;

	static std::atomic<bool> written{ false };
	if (written.exchange(true))
		return EXCEPTION_CONTINUE_SEARCH;

	logger::critical("[CRASH] exception {:#x} at {} - writing CommunityShaders.dmp", code,
		fmt::ptr(a_exception->ExceptionRecord->ExceptionAddress));
	spdlog::default_logger()->flush();

	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::filesystem::path dump{ path };
	dump = dump.parent_path() / L"CommunityShaders.dmp";

	const HANDLE file = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION info{};
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = a_exception;
		info.ClientPointers = FALSE;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
			static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
			&info, nullptr, nullptr);
		CloseHandle(file);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	AddVectoredExceptionHandler(1, &CommunityShadersCrashDump);
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);
	return Load();
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
		{
			if (errors.empty()) {
				Deferred::Hooks::Install();
				Hooks::Install();
				EngineFix::InstallOnPostPostLoadFixes();
				FrameAnnotations::OnPostPostLoad();

				auto shaderCache = globals::shaderCache;

				// Run feature PostPostLoad() first so features can disable themselves if needed
				Feature::ForEachLoadedFeature("PostPostLoad", [](Feature* feature) { feature->PostPostLoad(); });

				// Register scene settings event handler (Interior Only transitions)
				SceneSettingsManager::MenuOpenCloseEventHandler::Register();

				// Now validate disk cache after features have had a chance to modify their state
				shaderCache->ValidateDiskCache();

				if (shaderCache->UseFileWatcher())
					shaderCache->StartFileWatcher();
			}

			break;
		}
	case SKSE::MessagingInterface::kDataLoaded:
		{
			for (auto it = errors.begin(); it != errors.end(); ++it) {
				auto& errorMessage = *it;
				RE::DebugMessageBox(std::format("Community Shaders\n{}, will disable all hooks and features", errorMessage).c_str());
			}

			if (errors.empty()) {
				globals::OnDataLoaded();
				EngineFix::InstallOnDataLoadedFixes();
				FrameAnnotations::OnDataLoaded();

				auto shaderCache = globals::shaderCache;
				shaderCache->menuLoaded = true;

				while (shaderCache->IsCompiling() && !shaderCache->backgroundCompilation && !globals::game::quitGame) {
					std::this_thread::sleep_for(100ms);
				}

				if (globals::game::quitGame) {
					logger::info("Game was closed, skipping feature DataLoaded methods");
					break;
				}

				if (shaderCache->IsDiskCache()) {
					shaderCache->WriteDiskCacheInfo();
				}

				Feature::ForEachLoadedFeature("DataLoaded", [](Feature* feature) { feature->DataLoaded(); });
			}

			break;
		}
	case SKSE::MessagingInterface::kPostLoadGame:
		{
			if (errors.empty())
				Feature::ForEachLoadedFeature("GameLoaded", [](Feature* feature) { feature->GameLoaded(); });
			break;
		}
	}
}

bool Load()
{
	auto privateProfileRedirectorVersion = Util::GetDllVersion(L"Data/SKSE/Plugins/PrivateProfileRedirector.dll");
	if (privateProfileRedirectorVersion.has_value() && privateProfileRedirectorVersion.value().compare(REL::Version(0, 6, 2)) == std::strong_ordering::less) {
		stl::report_and_fail("Old version of PrivateProfileRedirector detected, 0.6.2+ required if using it."sv);
	}

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", MessageHandler);

	globals::OnInit();
	globals::ReInit();

	auto state = globals::state;

	// Initialize i18n system (loads English fallback and discovers available locales)
	I18n::GetSingleton()->Init();

	state->Load();
	state->LoadTheme();  // Load theme settings from SettingsTheme.json

	// Initialize theme system - create default themes and discover existing ones
	globals::menu->CreateDefaultThemes();  // Creates JSON files if they don't exist
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->DiscoverThemes();  // Discover all available themes

	auto log = spdlog::default_logger();
	log->set_level(state->GetLogLevel());

	const std::array incompatibleDLLs = {
		L"Data/SKSE/Plugins/ShaderTools.dll",
		L"Data/SKSE/Plugins/SSEShaderTools.dll",
		L"Data/SKSE/Plugins/SkyrimUpscaler.dll",
		L"Data/SKSE/Plugins/EVLaS.dll",
		L"Data/SKSE/Plugins/AELAS.dll",
		L"Data/SKSE/Plugins/SSEReShadeHelper.dll",
		L"Data/SKSE/Plugins/TAASharpen.dll",
		L"Data/SKSE/Plugins/NVIDIA_Reflex.dll",
		L"Data/SKSE/Plugins/MARA.dll",
		L"Data/SKSE/Plugins/NativeWaterLightStabilizer.dll",
	    L"Data/SKSE/Plugins/DynamicWetness.dll"
	};

	for (const auto dll : incompatibleDLLs) {
		if (LoadLibrary(dll)) {
			auto errorMessage = std::format("Incompatible DLL {} detected", stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
			logger::error("{}", errorMessage);
			errors.push_back(errorMessage);
		}
	}

	auto pushMissingDllError = [&](std::string_view dllName) {
		auto errorMessage = std::format("Required DLL {} was missing", dllName);
		logger::error("{}", errorMessage);
		errors.push_back(errorMessage);
	};

	if (!LoadLibrary(L"Data/SKSE/Plugins/EngineFixes.dll")) {
		pushMissingDllError(stl::utf16_to_utf8(L"Data/SKSE/Plugins/EngineFixes.dll").value_or("<unicode conversion error>"s));
	}

	// Empty RequiredDLLs array, if necessary we can add a dll here in the future without needing to modify the plugin loading logic.
	const std::array<LPCWSTR, 0> requiredDLLs{};

	for (const auto dll : requiredDLLs) {
		if (!LoadLibrary(dll)) {
			pushMissingDllError(stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
		}
	}

	if (errors.empty()) {
		// RenderDoc patches the D3D imports, so load it before capturing the IAT originals.
		globals::features::renderDoc.Load();
		Hooks::InstallEarlyHooks();
		logger::info("Calling feature Load methods");
		Feature::ForEachLoadedFeature("Load", [](Feature* feature) { feature->Load(); });
	}

	return true;
}

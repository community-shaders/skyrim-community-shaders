#include "OverlayRenderer.h"
#include "BackgroundBlur.h"
#include "HomePageRenderer.h"
#include "ThemeManager.h"

#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <unordered_map>
#include <unordered_set>

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/Effects11/Editor/Effects11Editor.h"
#include "Features/Effects11/EffectManager.h"
#include "Features/RenderDoc.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/CursorLoader.h"
#include "Menu/FontAtlasState.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/Notifications.h"
#include "Utils/UISurface.h"

#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
namespace
{
	std::unordered_map<ImGuiID, float> s_windowOverlapAlpha;

	constexpr ImGuiWindowFlags SKIP_WINDOW_FLAGS = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
	constexpr const char* MAIN_WINDOW_PREFIX = "Community Shaders";

	bool IsMainWindow(ImGuiWindow* win) { return win->Name && strncmp(win->Name, MAIN_WINDOW_PREFIX, strlen(MAIN_WINDOW_PREFIX)) == 0; }

	/**
	 * @brief Keeps a status card from covering the menu: when the main menu window is displayed behind the card, the
	 * card is moved just behind it. Z-order follows the window wherever it is moved or undocked, unlike a fixed position.
	 */
	void KeepBehindMainWindow(ImGuiWindow* card)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		ImGuiWindow* menu = nullptr;
		int cardIndex = -1;
		int menuIndex = -1;
		for (int i = 0; i < g.Windows.Size; i++) {
			ImGuiWindow* win = g.Windows[i];
			if (win == card)
				cardIndex = i;
			else if (!menu && !win->ParentWindow && win->WasActive && IsMainWindow(win)) {
				menu = win;
				menuIndex = i;
			}
		}
		if (menu && cardIndex > menuIndex)
			ImGui::BringWindowToDisplayBehind(card, menu);
	}

	/**
	 * @brief Begins the top-left status window in the toast card style: themed WindowBg at OVERLAP_MIN_ALPHA,
	 * no frame, with the accent border and left stripe drawn by EndOverlayCard.
	 * @return False when the window is collapsed or clipped; EndOverlayCard must still be called.
	 */
	bool BeginOverlayCard(const ImVec2& position)
	{
		using Constants = ThemeManager::Constants;
		const float scale = Util::GetUIScale();
		ImGui::SetNextWindowPos(position);
		ImGui::SetNextWindowBgAlpha(0.0f);  // The surface is drawn by EndOverlayCard
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().FrameRounding * Constants::TOAST_ROUNDING_SCALE);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
			ImVec2((Constants::TOAST_PADDING_X + Constants::TOAST_STRIPE_WIDTH) * scale, Constants::TOAST_PADDING_Y * scale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		const bool visible = ImGui::Begin("ShaderCompilationInfo", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		KeepBehindMainWindow(ImGui::GetCurrentWindow());
		if (visible) {
			// Surface backing, drawn before the content. The card auto-sizes, so it uses last frame's rect.
			Util::Surface::Options surface;
			surface.rounding = ImGui::GetStyle().WindowRounding;
			const ImVec2 p0 = ImGui::GetWindowPos();
			const ImVec2 size = ImGui::GetWindowSize();
			Util::Surface::Draw(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + size.x, p0.y + size.y),
				Util::Surface::Role::OverlayCard, surface);
		}
		return visible;
	}

	/** @brief Draws the card's thin accent border and left stripe, then ends the window begun by BeginOverlayCard. */
	void EndOverlayCard(const ImVec4& accentColor)
	{
		using Constants = ThemeManager::Constants;
		const float scale = Util::GetUIScale();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 p0 = ImGui::GetWindowPos();
		const ImVec2 size = ImGui::GetWindowSize();
		const ImVec2 p1(p0.x + size.x, p0.y + size.y);
		const float rounding = ImGui::GetStyle().WindowRounding;
		const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(accentColor.x, accentColor.y, accentColor.z, Constants::TOAST_ACCENT_ALPHA));
		Util::Surface::DrawAccentFrame(draw, p0, p1, accent, rounding, Constants::TOAST_BORDER_THICKNESS * scale, Constants::TOAST_STRIPE_WIDTH * scale);
		ImGui::End();
		ImGui::PopStyleVar(3);
	}

	/** @brief Effects 11 effects that failed to compile, or 0 while the effect manager is not up. */
	uint32_t GetFailedEffectCount()
	{
		auto& effectManager = EffectManager::GetSingleton();
		return effectManager.IsInitialized() ? effectManager.GetFailedEffectCount() : 0;
	}

	void DrawShaderCompilationFailures(uint64_t failed, const Menu::ThemeSettings& themeSettings)
	{
		if (failed) {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s",
				I18n::GetSingleton()->Format("overlay.shaders_failed", { { "count", std::to_string(failed) } },
										"ERROR: {count} shaders failed to compile. Check installation and CommunityShaders.log")
					.c_str());

			if (FeatureIssues::HasPotentialShaderModifyingFeatures()) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("overlay.modified_features", "Features that may have modified shaders detected. Check Feature Issues in the Menu."));
			}
		}
	}

	void DrawEffects11Errors(const Menu::ThemeSettings& themeSettings)
	{
		const uint32_t effectFailed = GetFailedEffectCount();
		if (effectFailed) {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s",
				I18n::GetSingleton()->Format("overlay.effects_failed", { { "count", std::to_string(effectFailed) } },
										"ERROR: {count} effect(s) failed to compile")
					.c_str());
			for (const auto& err : EffectManager::GetSingleton().GetAllErrors())
				ImGui::TextColored(themeSettings.StatusPalette.Error, "  %s", err.c_str());
		}
	}

	bool IsVisibleRootWindow(ImGuiWindow* win)
	{
		if (!win || !win->WasActive || win->Hidden)
			return false;
		return !(win->ParentWindow && !win->DockIsActive) && !(win->Flags & SKIP_WINDOW_FLAGS);
	}

	// Patches DrawList background vertices for windows involved in overlap.
	void PatchOverlappingWindowBackgrounds()
	{
		auto* ctx = ImGui::GetCurrentContext();
		if (!ctx)
			return;

		using C = ThemeManager::Constants;
		const float dt = ImGui::GetIO().DeltaTime;

		struct WinInfo
		{
			ImGuiWindow* win;
			ImRect rect;
		};
		std::vector<WinInfo> windows;
		for (int i = 0; i < ctx->Windows.Size; i++) {
			auto* win = ctx->Windows[i];
			if (IsVisibleRootWindow(win))
				windows.push_back({ win, win->Rect() });
		}

		std::unordered_set<ImGuiID> overlapping;
		for (size_t i = 0; i < windows.size(); i++)
			for (size_t j = i + 1; j < windows.size(); j++)
				if (windows[i].rect.Overlaps(windows[j].rect)) {
					auto* a = windows[i].win;
					auto* b = windows[j].win;
					// Main CS window never dims; other windows yield to it
					if (IsMainWindow(a))
						overlapping.insert(b->ID);
					else if (IsMainWindow(b))
						overlapping.insert(a->ID);
					else
						overlapping.insert(a->FocusOrder > b->FocusOrder ? a->ID : b->ID);
				}

		const ImU32 bgRGB = ImGui::GetColorU32(ImGuiCol_WindowBg) & ~IM_COL32_A_MASK;

		for (auto& [win, rect] : windows) {
			const float target = overlapping.count(win->ID) ? C::OVERLAP_MIN_ALPHA : 0.0f;
			float& alpha = s_windowOverlapAlpha[win->ID];
			const float speed = (target > alpha) ? C::OVERLAP_FADEIN_SPEED : C::OVERLAP_FADEOUT_SPEED;
			alpha += (target - alpha) * (std::min)(1.0f, dt * speed);

			if (alpha < C::OVERLAP_ALPHA_EPSILON) {
				alpha = 0.0f;
				continue;
			}

			auto* dl = win->DrawList;
			if (!dl || dl->VtxBuffer.Size == 0)
				continue;

			// Clamp background rect vertex alpha (contiguous bgRGB block at start of DrawList)
			const ImU32 minA = static_cast<ImU32>(alpha * 255.0f);
			for (int v = 0; v < dl->VtxBuffer.Size; v++) {
				auto& vtx = dl->VtxBuffer[v];
				if ((vtx.col & ~IM_COL32_A_MASK) != bgRGB)
					break;
				ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
				if (a > 0 && a < minA)
					vtx.col = bgRGB | (minA << IM_COL32_A_SHIFT);
			}
		}

		// Prune stale entries
		for (auto it = s_windowOverlapAlpha.begin(); it != s_windowOverlapAlpha.end();)
			it->second < C::OVERLAP_ALPHA_EPSILON ? it = s_windowOverlapAlpha.erase(it) : ++it;
	}

	/** @brief Blur strength this frame: the editor's fullscreen blur follows its widgets, other windows follow the menu fade. */
	float GetBlurFade()
	{
		auto* editorWindow = EditorWindow::GetSingleton();
		if (editorWindow->open)
			return editorWindow->IsViewportActive() ? editorWindow->widgetsFade : 1.0f;

		const float menuFade = Menu::GetSingleton()->openFade;
		return menuFade > 0.0f ? menuFade : 1.0f;
	}
}  // namespace

void OverlayRenderer::RenderOverlay(
	Menu& menu,
	const std::function<void()>& processInputEventQueue,
	const std::function<void()>& drawSettings,
	const std::function<const char*(std::vector<InputCombo>)>& keyIdToString,
	float& cachedFontSize,
	float currentFontSize)
{
	// Always sync, including frames that skip drawing, so closing the last CS UI restores vanity.
	menu.SyncVanityCamera();

	processInputEventQueue();

	// ImGui only takes game input while a CS window owns it. Otherwise status overlays
	// (compile progress, performance overlay) would react to gameplay clicks and keys, and
	// keys held while flying the editor camera would leak into the editor's widgets.
	auto& io = ImGui::GetIO();
	const bool acceptsInput = !menu.IsPreviewFlying() && menu.ShouldSwallowInput();
	io.SetAppAcceptingEvents(acceptsInput);
	if (!acceptsInput) {
		io.ClearEventsQueue();
		io.ClearInputKeys();
		io.ClearInputMouse();
		io.WantSetMousePos = false;
		ImGui::ClearActiveID();
	}

	// Before the skip check so the editor's close edge is never missed.
	auto* editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->open && !EditorWindow::CanBeOpen()) {
		editorWindow->open = false;
		if (editorWindow->IsInPreviewMode())
			editorWindow->ExitPreviewMode();
	}
	editorWindow->UpdateOpenState();
	menu.UpdateOpenFade(io.DeltaTime);

	if (ShouldSkipRendering()) {
		io.ClearInputKeys();
		io.ClearEventsQueue();
		s_windowOverlapAlpha.clear();
		EditorWindow::GetSingleton()->UpdateOpenState();
		return;
	}

	HandleFontReload(menu, cachedFontSize, currentFontSize);
	InitializeImGuiFrame(menu);

	RenderShaderCompilationStatus(keyIdToString);
	RenderShaderBlockingStatus();

	// The Effects 11 editor and the Community Shaders menu are exclusive; the CS Editor hosts it.
	auto& effects11Editor = Effects11Editor::GetSingleton();
	if (effects11Editor.IsOpen() && menu.IsEnabled)
		effects11Editor.Close(false);

	if (editorWindow->open) {
		bool flying = editorWindow->IsPreviewFlying();
		io.MouseDrawCursor = !flying;
		if (flying)
			io.MousePos = { -FLT_MAX, -FLT_MAX };  // prevent hover/tooltips during active flying
		editorWindow->Draw();
	} else if (effects11Editor.IsOpen()) {
		ImGui::GetIO().MouseDrawCursor = true;
		effects11Editor.Draw();
	} else if (menu.IsEnabled || menu.openFade > 0.0f || HomePageRenderer::ShouldShowFirstTimeSetup()) {
		ImGui::GetIO().MouseDrawCursor = menu.IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup();
		if (menu.IsEnabled || menu.openFade > 0.0f) {
			const auto& context = *ImGui::GetCurrentContext();
			const bool editedBefore = context.ActiveIdHasBeenEditedThisFrame;
			drawSettings();
			// Only frames that edited something pay for the check; search boxes edit too, so it compares.
			if (!editedBefore && context.ActiveIdHasBeenEditedThisFrame)
				globals::state->CheckCoreSettingsChanged();
		}
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}

	// Enabling a Baseline pack can happen from either the menu or the CS editor, and the answer outlives the page.
	if (editorWindow->open || menu.IsEnabled)
		UnifiedPresetCatalog::GetSingleton().DrawBaselineOverlapPrompt();

	// The editor draws the notifications in its own frame; while it is closed they still need a host,
	// e.g. the "cannot open in a loading screen" warning raised by the toggle hotkey.
	if (!editorWindow->open)
		Util::Notifications::Draw();

	RenderFeatureOverlays();
	RenderFirstTimeSetupOverlay();
	HandleABTesting();
	PatchOverlappingWindowBackgrounds();
	FinalizeImGuiFrame();
}

bool OverlayRenderer::ShouldSkipRendering()
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	auto* renderDoc = RenderDoc::GetSingleton();

	const uint32_t effectFailed = GetFailedEffectCount();

	return !(shaderCache->IsCompiling() ||
			 Menu::GetSingleton()->IsEnabled ||
			 Menu::GetSingleton()->openFade > 0.0f ||
			 HomePageRenderer::ShouldShowFirstTimeSetup() ||
			 EditorWindow::GetSingleton()->open ||
			 Util::Notifications::HasAny() ||
			 Effects11Editor::GetSingleton().IsOpen() ||
			 abTestingManager->IsEnabled() ||
			 (failed && !hide) ||
			 effectFailed ||
			 globals::features::performanceOverlay.settings.ShowInOverlay ||
			 globals::features::performanceOverlay.settings.ShowWeatherPicker ||
			 renderDoc->IsAvailable());
}

void OverlayRenderer::HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize)
{
	bool fontSizeChanged = std::abs(cachedFontSize - currentFontSize) > ThemeManager::Constants::FONT_CACHE_EPSILON;
	std::string desiredSignature = menu.BuildFontSignature(currentFontSize);
	bool signatureChanged = desiredSignature != MenuFonts::GetAtlasState().cachedFontSignature;

	if (fontSizeChanged || signatureChanged) {
		if (!ThemeManager::ReloadFont(menu, cachedFontSize)) {
			logger::warn("OverlayRenderer::HandleFontReload() - Font reload failed");
		}
	}
}

void OverlayRenderer::InitializeImGuiFrame(Menu& menu)
{
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	const float displayW = static_cast<float>(desc.BufferDesc.Width);
	const float displayH = static_cast<float>(desc.BufferDesc.Height);
	Util::UpdateImGuiInput(desc.OutputWindow, displayW, displayH);

	ImGui::NewFrame();

	// Detect display size change (cross-session via ini handler, mid-session via member)
	const float2 currentDisplaySize{ displayW, displayH };
	if (menu.lastDisplaySize.x > 0.f && menu.lastDisplaySize != currentDisplaySize) {
		logger::info("Display size changed: {}x{} -> {}x{}, resetting window layout",
			menu.lastDisplaySize.x, menu.lastDisplaySize.y, currentDisplaySize.x, currentDisplaySize.y);
		menu.resetLayout = true;
		EditorWindow::GetSingleton()->resetLayout = true;
		Effects11Editor::GetSingleton().RequestLayoutReset();
	}
	menu.lastDisplaySize = currentDisplaySize;

	ThemeManager::SetupImGuiStyle(menu);
}

void OverlayRenderer::RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>& keyIdToString)
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;

	uint64_t totalShaders = shaderCache->GetTotalTasks();
	uint64_t compiledShaders = shaderCache->GetCompletedTasks();

	auto state = globals::state;
	auto& themeSettings = Menu::GetSingleton()->GetTheme();
	auto* renderDoc = RenderDoc::GetSingleton();
	bool renderDocAvailable = renderDoc->IsAvailable();
	const auto renderDocInformation = renderDoc->GetOverlayWarningMessage();

	const std::unordered_map<std::string, std::string> statsArg{ { "stats", shaderCache->GetShaderStatsString(!state->IsDeveloperMode()) } };
	const auto progressTitle = shaderCache->backgroundCompilation ?
	                               I18n::GetSingleton()->Format("overlay.compiling_shaders_background", statsArg, "Background Compiling Shaders: {stats}") :
	                               I18n::GetSingleton()->Format("overlay.compiling_shaders", statsArg, "Compiling Shaders: {stats}");
	auto percent = (float)compiledShaders / (float)totalShaders;
	auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", compiledShaders, totalShaders, 100 * percent);

	const uint32_t effectFailed = GetFailedEffectCount();
	const bool showFailures = (failed && !hide) || effectFailed;
	const bool compiling = shaderCache->IsCompiling();
	if (!compiling && !showFailures && !renderDocAvailable)
		return;

	// Card accent: errors win, then the RenderDoc warning, else plain progress
	const ImVec4 accent = showFailures       ? themeSettings.StatusPalette.Error :
	                      renderDocAvailable ? themeSettings.StatusPalette.Warning :
	                                           themeSettings.StatusPalette.InfoColor;

	if (!BeginOverlayCard(ImVec2(pos, pos))) {
		EndOverlayCard(accent);
		return;
	}

	if (compiling) {
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());
		if (state->IsDeveloperMode()) {
			int32_t threadLimit = shaderCache->backgroundCompilation ? shaderCache->backgroundCompilationThreadCount : shaderCache->compilationThreadCount;
			int compilationRunning = (int)shaderCache->compilationPool.get_tasks_running();
			int heavyInFlight = shaderCache->GetHeavyTasksInFlight();
			int heavyLimit = static_cast<int>(Util::GetPerformanceCoreCount());
			uint64_t slow = shaderCache->GetSlowTasks();
			uint64_t verySlow = shaderCache->GetVerySlowTasks();
			ImGui::Text(T("overlay.compile_threads", "Threads: %d / %d limit | Heavy: %d / %d P-cores | %d workers"),
				compilationRunning,
				threadLimit,
				heavyInFlight,
				heavyLimit,
				(int)shaderCache->compilationPool.get_thread_count());
			if (slow > 0) {
				ImGui::Text(T("overlay.slow_shaders", "Slow shaders: %llu (very slow: %llu)"), slow, verySlow);
			}
		}
		if (!shaderCache->backgroundCompilation && shaderCache->menuLoaded) {
			const auto skipShadersText = I18n::GetSingleton()->Format("overlay.skip_compilation",
				{ { "key", keyIdToString(Menu::GetSingleton()->GetSettings().SkipCompilationKey) } },
				"Press {key} to proceed without completing shader compilation.");
			ImGui::TextUnformatted(skipShadersText.c_str());
			ImGui::TextUnformatted(T("overlay.uncompiled_warning", "WARNING: Uncompiled shaders will have visual errors or cause stuttering when loading."));
		}
	}

	if (showFailures) {
		DrawShaderCompilationFailures(hide ? 0 : failed, themeSettings);
		DrawEffects11Errors(themeSettings);
	}

	if (renderDocAvailable)
		ImGui::TextColored(themeSettings.StatusPalette.Warning, "%s", renderDocInformation.c_str());

	EndOverlayCard(accent);
}

void OverlayRenderer::RenderFeatureOverlays()
{
	// load overlays
	for (Feature* feat : Feature::GetFeatureList()) {
		if (feat && feat->loaded) {
			if (auto* overlay = dynamic_cast<OverlayFeature*>(feat)) {
				overlay->DrawOverlay();
			}
		}
	}
}

void OverlayRenderer::HandleABTesting()
{
	// A/B Testing management
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->Update();

	// Always update test data during TEST phase, regardless of overlay visibility
	if (abTestingManager->IsEnabled()) {
		globals::features::performanceOverlay.UpdateAllShaderTestData();

		// Add A/B test aggregator data collection here
		auto& overlay = globals::features::performanceOverlay;
		auto [mainRows, summaryRows] = overlay.BuildDrawCallRows();
		std::vector<DrawCallRow> allRows = mainRows;
		allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());

		// Update the A/B test aggregator with current frame data
		abTestingManager->GetAggregator().OnFrame(allRows);
	}

	// Draw A/B testing overlay
	abTestingManager->DrawOverlayUI();
}

void OverlayRenderer::FinalizeImGuiFrame()
{
	if (auto* menu = Menu::GetSingleton();
		menu && menu->GetSettings().Theme.UseCustomCursor && Util::CursorLoader::GetLoadedCount() > 0) {
		Util::CursorLoader::DrawCustomCursor(*menu);
	}

	ImGui::Render();

	BackgroundBlur::SetFade(GetBlurFade());

	// Apply background blur behind ImGui windows before rendering them
	BackgroundBlur::RenderBackgroundBlur();

	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void OverlayRenderer::RenderFirstTimeSetupOverlay()
{
	if (HomePageRenderer::ShouldShowFirstTimeSetup()) {
		HomePageRenderer::RenderFirstTimeSetupDialog();
	}
}

void OverlayRenderer::RenderShaderBlockingStatus()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!state->IsDeveloperMode() || shaderCache->blockedKey.empty()) {
		return;
	}

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;

	// Stack below shader compilation window if visible
	float yPos = pos;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			yPos = shaderWin->Pos.y + shaderWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	// Also stack below water cache overlay if visible
	if (auto* waterWin = ImGui::FindWindowByName("UWCacheCreationInfo")) {
		if (waterWin->Active && waterWin->Pos.y + waterWin->Size.y > yPos) {
			yPos = waterWin->Pos.y + waterWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	ImGui::SetNextWindowPos(ImVec2(pos, yPos));
	if (!ImGui::Begin("ShaderBlockingInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	Util::Text::Error(T("overlay.shader_blocking_active", "Shader Blocking Active"));
	ImGui::Text(T("overlay.blocked_shader", "Blocked: %s"), shaderCache->blockedKey.c_str());

	// Try to get more details from active shaders
	auto activeShaders = shaderCache->GetActiveShaders();

	// Find the index of the blocked shader in the active list (or show N/A if not found)
	size_t blockedIndex = 0;
	bool foundBlocked = false;
	for (size_t i = 0; i < activeShaders.size(); ++i) {
		if (activeShaders[i].key == shaderCache->blockedKey) {
			blockedIndex = i + 1;  // 1-based indexing for display
			foundBlocked = true;
			break;
		}
	}

	if (foundBlocked) {
		ImGui::Text(T("overlay.blocked_index", "Index: %zu/%zu"), blockedIndex, activeShaders.size());
	} else {
		ImGui::Text(T("overlay.blocked_index_unknown", "Index: N/A (%zu active)"), activeShaders.size());
	}

	for (const auto& shader : activeShaders) {
		if (shader.key == shaderCache->blockedKey) {
			ImGui::Text(T("overlay.blocked_details", "Type: %s | Class: %s | Descriptor: 0x%X"),
				magic_enum::enum_name(shader.shaderType).data(),
				magic_enum::enum_name(shader.shaderClass).data(),
				shader.descriptor);
			break;
		}
	}

	ImGui::End();
}

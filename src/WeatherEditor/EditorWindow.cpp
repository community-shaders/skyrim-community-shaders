#include "EditorWindow.h"

#include "State.h"
#include "Features/WeatherEditor.h"
#include "imgui_internal.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings, recordMarkers, markedRecords, autoApplyChanges)

bool ContainsStringIgnoreCase(const std::string_view a_string, const std::string_view a_substring)
{
	const auto it = std::ranges::search(a_string, a_substring, [](const char a_a, const char a_b) {
		return std::tolower(a_a) == std::tolower(a_b);
	}).begin();
	return it != a_string.end();
}

void TextUnformattedDisabled(const char* a_text, const char* a_textEnd = nullptr)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextUnformatted(a_text, a_textEnd);
	ImGui::PopStyleColor();
}

void AddTooltip(const char* a_desc, ImGuiHoveredFlags a_flags = ImGuiHoveredFlags_DelayNormal)
{
	if (ImGui::IsItemHovered(a_flags)) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
		if (ImGui::BeginTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
			ImGui::TextUnformatted(a_desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopStyleVar();
	}
}

inline void HelpMarker(const char* a_desc)
{
	ImGui::AlignTextToFramePadding();
	TextUnformattedDisabled("(?)");
	AddTooltip(a_desc, ImGuiHoveredFlags_DelayShort);
}

void EditorWindow::ShowObjectsWindow()
{
	ImGui::Begin("Object List");

	// Static variable to track the selected category
	static std::string selectedCategory = "Lighting Template";

	// Static variable for filtering objects
	static char filterBuffer[256] = "";

	// Create a table with two columns
	if (ImGui::BeginTable("ObjectTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		// Set up column widths
		ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthStretch, 0.3f);  // 30% width
		ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthStretch, 0.7f);     // 70% width

		ImGui::TableNextRow();

		// Left column: Categories
		ImGui::TableSetColumnIndex(0);

		// Begin a table for the categories list
		if (ImGui::BeginTable("CategoriesTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Categories");  // Label for the table

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			// List of categories
			const char* categories[] = { "Lighting Template", "Weather", "WorldSpace" };
			for (int i = 0; i < IM_ARRAYSIZE(categories); ++i) {
				// Highlight the selected category
				if (ImGui::Selectable(categories[i], selectedCategory == categories[i])) {
					selectedCategory = categories[i];  // Update selected category
				}
			}

			ImGui::EndTable();
		}

		// Right column: Objects
		ImGui::TableSetColumnIndex(1);

		ImGui::InputTextWithHint("##ObjectFilter", "Filter...", filterBuffer, sizeof(filterBuffer));

		ImGui::SameLine();
		HelpMarker("Type a part of an object name to filter the list.");

		// Create a table for the right column with "Name" and "ID" headers
		if (ImGui::BeginTable("DetailsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Form ID", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);  // Added File column
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableHeadersRow();

			// Display objects based on the selected category
			auto& widgets = selectedCategory == "Weather"    ? weatherWidgets :
			                selectedCategory == "WorldSpace" ? worldSpaceWidgets :
			                                                   lightingTemplateWidgets;

			// Filtered display of widgets
			for (int i = 0; i < widgets.size(); ++i) {
				if (!ContainsStringIgnoreCase(widgets[i]->GetEditorID(), filterBuffer))
					continue;  // Skip widgets that don't match the filter

				auto editorLabel = widgets[i]->GetEditorID();
				auto markedRecord = settings.markedRecords.find(editorLabel);
				ImGui::TableNextRow();

				// Set background colour
				if (markedRecord != settings.markedRecords.end()) {
					auto& color = settings.recordMarkers[markedRecord->second];
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(color));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(color));
				} else {
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGuiCol_TableRowBg);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGuiCol_TableRowBgAlt);
				}

				ImGui::TableSetColumnIndex(0);

				// Editor ID column
				if (ImGui::Selectable(editorLabel.c_str(), widgets[i]->IsOpen(), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					if (ImGui::IsMouseDoubleClicked(0)) {
						widgets[i]->SetOpen(true);
					}
				}

				// Opens a context menu on right click to mark records by color
				if (ImGui::BeginPopupContextItem(std::format("widget_context_menu##{}", widgets[i]->GetFormID()).c_str(), ImGuiPopupFlags_MouseButtonRight)) {
					auto& markedRecords = settings.markedRecords;

					for (auto& recordMarker : settings.recordMarkers) {
						if (ImGui::MenuItem(recordMarker.first.c_str())) {
							settings.markedRecords[editorLabel] = recordMarker.first;
							Save();
						}
					}

					if (ImGui::MenuItem("Remove")) {
						markedRecords.erase(editorLabel);
						Save();
					}

					ImGui::EndPopup();
				}

				// Form ID column
				ImGui::TableNextColumn();
				ImGui::Text(widgets[i]->GetFormID().c_str());

				// File column
				ImGui::TableNextColumn();
				ImGui::Text(widgets[i]->GetFilename().c_str());

				// Status column
				ImGui::TableNextColumn();

				// Re-check if the record exists after potential removal
				markedRecord = settings.markedRecords.find(editorLabel);
				if (markedRecord != settings.markedRecords.end()) {
					ImGui::Text("%s", markedRecord->second.c_str());
				}
			}

			ImGui::EndTable();
		}

		ImGui::EndTable();
	}

	// End the window
	ImGui::End();
}

void EditorWindow::ShowViewportWindow()
{
	ImGui::Begin("Viewport");

	// Top bar
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar)
		ImGui::SliderFloat("##ViewportSlider", &calendar->gameHour->value, 0.0f, 23.99f, "Time: %.2f");

	// The size of the image in ImGui																														   // Get the available space in the current window
	ImVec2 availableSpace = ImGui::GetContentRegionAvail();

	// Calculate aspect ratio of the image
	float aspectRatio = ImGui::GetIO().DisplaySize.x / ImGui::GetIO().DisplaySize.y;

	// Determine the size to fit while preserving the aspect ratio
	ImVec2 imageSize;
	if (availableSpace.x / availableSpace.y < aspectRatio) {
		// Fit width
		imageSize.x = availableSpace.x;
		imageSize.y = availableSpace.x / aspectRatio;
	} else {
		// Fit height
		imageSize.y = availableSpace.y;
		imageSize.x = availableSpace.y * aspectRatio;
	}

	ImGui::Image((void*)tempTexture->srv.get(), imageSize);

	ImGui::End();
}

void EditorWindow::ShowWidgetWindow()
{
	for (int i = 0; i < (int)weatherWidgets.size(); i++) {
		auto widget = weatherWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)worldSpaceWidgets.size(); i++) {
		auto widget = worldSpaceWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)lightingTemplateWidgets.size(); i++) {
		auto widget = lightingTemplateWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}
}

void EditorWindow::RenderUI()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	auto& context = globals::d3d::context;

	context->ClearRenderTargetView(framebuffer.RTV, (float*)&ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);

	// Increase background opacity for all editor windows
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);

	// Check for Escape key to close editor
	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		open = false;
	}

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Save All Open Widgets", "Ctrl+S")) {
				SaveAll();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Close All Weather Widgets")) {
				for (auto* widget : weatherWidgets) widget->SetOpen(false);
			}
			if (ImGui::MenuItem("Close All WorldSpace Widgets")) {
				for (auto* widget : worldSpaceWidgets) widget->SetOpen(false);
			}
			if (ImGui::MenuItem("Close All Lighting Widgets")) {
				for (auto* widget : lightingTemplateWidgets) widget->SetOpen(false);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Settings")) {
			if (ImGui::MenuItem("Editor Preferences")) {
				showSettingsWindow = !showSettingsWindow;
			}
			ImGui::Separator();
			if (ImGui::Checkbox("Auto-Apply Changes", &settings.autoApplyChanges)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Automatically apply weather changes to the game as you edit");
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window")) {
			ImGui::Text("Open Widgets:");
			ImGui::Separator();
			
			int openCount = 0;
			for (auto* widget : weatherWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("Weather: {}", widget->GetEditorID()).c_str())) {
						// Focus window (ImGui will bring to front when clicked)
					}
				}
			}
			for (auto* widget : worldSpaceWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("WorldSpace: {}", widget->GetEditorID()).c_str())) {
						// Focus window
					}
				}
			}
			for (auto* widget : lightingTemplateWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("Lighting: {}", widget->GetEditorID()).c_str())) {
						// Focus window
					}
				}
			}
			
			if (openCount == 0) {
				ImGui::TextDisabled("No widgets open");
			}
			
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			ImGui::Text("Weather Editor");
			ImGui::Separator();
			ImGui::BulletText("Double-click objects to edit");
			ImGui::BulletText("Right-click to mark status");
			ImGui::BulletText("Auto-Apply updates game live");
			ImGui::BulletText("Lock weather to prevent changes");
			ImGui::BulletText("Changes save to JSON files");
			ImGui::Separator();
			ImGui::Text("Total Objects:");
			ImGui::BulletText("Weathers: %d", (int)weatherWidgets.size());
			ImGui::BulletText("WorldSpaces: %d", (int)worldSpaceWidgets.size());
			ImGui::BulletText("Lighting: %d", (int)lightingTemplateWidgets.size());
			ImGui::EndMenu();
		}
		
		// Weather lock indicator
		if (weatherLockActive && lockedWeather) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
			const char* weatherName = lockedWeather->GetFormEditorID();
			ImGui::Text(" [LOCKED: %s]", weatherName ? weatherName : "Unknown");
			ImGui::PopStyleColor();
		}
		
		// Time pause indicator
		if (timePaused) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
			ImGui::Text(" [TIME PAUSED]");
			ImGui::PopStyleColor();
		}
		
		// Close button on the right side
		float menuBarHeight = ImGui::GetFrameHeight();
		ImGui::SameLine(ImGui::GetWindowWidth() - menuBarHeight - 10.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
		if (ImGui::Button("X", ImVec2(menuBarHeight, menuBarHeight))) {
			open = false;
		}
		ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Close Weather Editor (Esc)");
		}
		
		ImGui::EndMainMenuBar();
	}

	auto width = ImGui::GetIO().DisplaySize.x;
	auto height = ImGui::GetIO().DisplaySize.y;
	auto viewportWidth = width * 0.5f;                // Make the viewport take up 50% of the width
	auto sideWidth = (width - viewportWidth) / 2.0f;  // Divide the remaining width equally between the side windows
	ImGui::SetNextWindowSize(ImVec2(sideWidth, ImGui::GetIO().DisplaySize.y * 0.75f), ImGuiCond_FirstUseEver);
	ShowObjectsWindow();

	ImGui::SetNextWindowSize(ImVec2(viewportWidth, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver);
	ShowViewportWindow();

	auto settingsWindowHeight = height * 0.25f;
	auto settingsWindowWidth = width * 0.25f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(settingsWindowWidth, settingsWindowHeight), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::SetNextWindowPos({ (width / 2.0f) - (settingsWindowWidth / 2.0f), (height / 2.0f) - (settingsWindowHeight / 2.0f) }, ImGuiCond_Appearing);
	if (showSettingsWindow) {
		ShowSettingsWindow();
	}

	ShowWidgetWindow();

	// Pop the alpha style var
	ImGui::PopStyleVar();
}

void EditorWindow::SetupResources()
{
	auto dataHandler = RE::TESDataHandler::GetSingleton();
	auto& weatherArray = dataHandler->GetFormArray<RE::TESWeather>();

	Load();

	for (auto weather : weatherArray) {
		auto widget = new WeatherWidget(weather);
		widget->Load();
		weatherWidgets.push_back(widget);
	}

	auto& worldSpaceArray = dataHandler->GetFormArray<RE::TESWorldSpace>();

	for (auto worldSpace : worldSpaceArray) {
		auto widget = new WorldSpaceWidget(worldSpace);
		widget->Load();
		worldSpaceWidgets.push_back(widget);
	}

	auto& lightingTemplateArray = dataHandler->GetFormArray<RE::BGSLightingTemplate>();

	for (auto lightingTemplate : lightingTemplateArray) {
		auto widget = new LightingTemplateWidget(lightingTemplate);
		widget->Load();
		lightingTemplateWidgets.push_back(widget);
	}
}

void EditorWindow::Draw()
{
	// Track editor open state for vanity camera management
	static bool wasOpen = false;
	
	if (open && !wasOpen) {
		// Editor just opened - disable vanity camera
		DisableVanityCamera();
	} else if (!open && wasOpen) {
		// Editor just closed - restore vanity camera
		RestoreVanityCamera();
	}
	
	wasOpen = open;

	// Re-enforce weather lock if active (handles time changes)
	if (weatherLockActive && lockedWeather) {
		auto sky = RE::Sky::GetSingleton();
		if (sky && sky->currentWeather != lockedWeather) {
			sky->ForceWeather(lockedWeather, false);
		}
	}

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

	ID3D11Resource* resource;
	framebuffer.SRV->GetResource(&resource);

	if (!tempTexture) {
		D3D11_TEXTURE2D_DESC texDesc{};
		((ID3D11Texture2D*)resource)->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		framebuffer.SRV->GetDesc(&srvDesc);

		tempTexture = new Texture2D(texDesc);
		tempTexture->CreateSRV(srvDesc);
	}

	auto& context = globals::d3d::context;

	context->CopyResource(tempTexture->resource.get(), resource);

	RenderUI();
}

void EditorWindow::SaveAll()
{
	for (auto weather : weatherWidgets) {
		if (weather->IsOpen())
			weather->Save();
	}

	for (auto worldspace : worldSpaceWidgets) {
		if (worldspace->IsOpen())
			worldspace->Save();
	}

	for (auto lightingTemplate : lightingTemplateWidgets) {
		if (lightingTemplate->IsOpen())
			lightingTemplate->Save();
	}

	Save();
}

void EditorWindow::SaveSettings()
{
	j = settings;
}

void EditorWindow::LoadSettings()
{
	if (!j.empty())
		settings = j;
}

void EditorWindow::ShowSettingsWindow()
{
	ImGui::Begin("Settings", &showSettingsWindow);

	// Static variable to track the selected category
	static std::string selectedOption = "Preferences";

	// Create a table with two columns
	if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		// Set up column widths
		ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, 0.3f);     // 30% width
		ImGui::TableSetupColumn("##Settings", ImGuiTableColumnFlags_WidthStretch, 0.7f);  // 70% width

		ImGui::TableNextRow();

		// Left column: Options
		ImGui::TableSetColumnIndex(0);
		// List of options
		const char* options[] = { "Preferences" };
		for (int i = 0; i < IM_ARRAYSIZE(options); ++i) {
			if (ImGui::Selectable(options[i], selectedOption == options[i])) {
				selectedOption = options[i];  // Update selected option
			}
		}

		// Right column: Option settings
		ImGui::TableSetColumnIndex(1);

		// Create a table for the right column.
		if (selectedOption == "Preferences") {
			if (ImGui::BeginTable("PreferencesTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthStretch);

				auto& recordMarkers = settings.recordMarkers;

				for (auto& recordMarker : recordMarkers) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text(recordMarker.first.c_str());

					ImGui::TableSetColumnIndex(1);
					if (ImGui::ColorEdit4(std::format("Color##{}", recordMarker.first).c_str(), (float*)&recordMarker.second)) {
						Save();
					};
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				if (recordMarkers.size() < maxRecordMarkers && ImGui::Selectable("Add new marker")) {
					recordMarkers.insert({ std::string("New marker##new{}", recordMarkers.size()), { 0, 0, 0, 255 } });
					Save();
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

void EditorWindow::Save()
{
	SaveSettings();
	const std::string filePath = Util::PathHelpers::GetCommunityShaderPath().string();
	const std::string file = std::format("{}\\{}.json", filePath, settingsFilename);

	std::ofstream settingsFile(file);

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open settings file: {}", file);
		return;
	}

	if (settingsFile.fail()) {
		logger::warn("Unable to create settings file: {}", file);
		settingsFile.close();
		return;
	}

	logger::info("Saving settings file: {}", file);

	try {
		settingsFile << j.dump(1);

		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
}

void EditorWindow::Load()
{
	std::string filePath = std::format("{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), settingsFilename);

	std::ifstream settingsFile(filePath);

	if (!std::filesystem::exists(filePath)) {
		// Does not have any settings so just return.
		return;
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to load settings file: {}", filePath);
		return;
	}

	try {
		j << settingsFile;
		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
	LoadSettings();
}

void EditorWindow::LockWeather(RE::TESWeather* weather)
{
	if (!weather) return;

	auto sky = RE::Sky::GetSingleton();
	if (!sky) return;

	// Force the weather to be active
	sky->ForceWeather(weather, false);

	lockedWeather = weather;
	weatherLockActive = true;

	logger::info("Weather locked: {}", weather->GetFormEditorID() ? weather->GetFormEditorID() : "Unknown");
}

void EditorWindow::UnlockWeather()
{
	if (!weatherLockActive) return;

	auto sky = RE::Sky::GetSingleton();
	if (sky) {
		// Release weather override to allow natural progression
		sky->ReleaseWeatherOverride();
	}

	logger::info("Weather unlocked: {}", lockedWeather && lockedWeather->GetFormEditorID() ? lockedWeather->GetFormEditorID() : "Unknown");

	lockedWeather = nullptr;
	weatherLockActive = false;
}

void EditorWindow::PauseTime()
{
	if (timePaused) return;

	auto calendar = RE::Calendar::GetSingleton();
	if (calendar && calendar->timeScale) {
		savedTimeScale = calendar->timeScale->value;
		calendar->timeScale->value = 0.0f;
		timePaused = true;
		logger::info("Time paused (saved timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResumeTime()
{
	if (!timePaused) return;

	auto calendar = RE::Calendar::GetSingleton();
	if (calendar && calendar->timeScale) {
		calendar->timeScale->value = savedTimeScale;
		timePaused = false;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void EditorWindow::DisableVanityCamera()
{
	if (vanityCameraDisabled) return;

	auto setting = RE::GetINISetting("fAutoVanityModeDelay:Camera");
	if (setting) {
		savedVanityCameraDelay = setting->GetFloat();
		setting->data.f = 10000.0f;
		vanityCameraDisabled = true;
		logger::info("Vanity camera disabled (saved delay: {})", savedVanityCameraDelay);
	}
}

void EditorWindow::RestoreVanityCamera()
{
	if (!vanityCameraDisabled) return;

	auto setting = RE::GetINISetting("fAutoVanityModeDelay:Camera");
	if (setting) {
		setting->data.f = savedVanityCameraDelay;
		vanityCameraDisabled = false;
		logger::info("Vanity camera restored (delay: {})", savedVanityCameraDelay);
	}
}

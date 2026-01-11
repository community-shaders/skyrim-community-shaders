#include "UI.h"

#include "FileSystem.h"
#include "Menu.h"
#include "Menu/IconLoader.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <DirectXTex.h>
#include <d3d11.h>
#include <dinput.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>

#include "../Feature.h"
#include "../Globals.h"
#include "../Menu.h"
#include "FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stb_image.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Util
{
	HoverTooltipWrapper::HoverTooltipWrapper()
	{
		hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
		if (hovered) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		}
	}

	HoverTooltipWrapper::~HoverTooltipWrapper()
	{
		if (hovered) {
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	DisableGuard::DisableGuard(bool disable) :
		disable(disable)
	{
		if (disable)
			ImGui::BeginDisabled();
	}
	DisableGuard::~DisableGuard()
	{
		if (disable)
			ImGui::EndDisabled();
	}

	bool PercentageSlider(const char* label, float* data, float lb, float ub, const char* format)
	{
		float percentageData = (*data) * 1e2f;
		bool retval = ImGui::SliderFloat(label, &percentageData, lb, ub, format);
		(*data) = percentageData * 1e-2f;
		return retval;
	}

	ImVec2 GetNativeViewportSizeScaled(float scale)
	{
		const auto Size = ImGui::GetMainViewport()->Size;
		return { Size.x * scale, Size.y * scale };
	}

	bool InitializeMenuIcons(Menu* menu)
	{
		return IconLoader::InitializeMenuIcons(menu);
	}

	// Text rendering helpers
	ImVec2 DrawSharpText(const char* text, bool alignToPixelGrid, float scale)
	{
		ImVec2 startPos = ImGui::GetCursorPos();

		if (alignToPixelGrid) {
			// Get current position
			ImVec2 pos = ImGui::GetCursorPos();

			// Align to pixel grid for sharper rendering
			pos.x = std::round(pos.x);
			pos.y = std::round(pos.y);

			// Set aligned position
			ImGui::SetCursorPos(pos);
		}
		// Apply scale if needed
		if (scale != 1.0f) {
			ImGui::SetWindowFontScale(scale);
		}

		// Use Text instead of TextUnformatted for better rendering
		ImGui::Text("%s", text);
		// Restore original scale if needed
		if (scale != 1.0f)
			ImGui::SetWindowFontScale(1.0f);

		// Calculate and return the rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	ImVec2 DrawAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale, ImU32 logoTint)
	{
		// Save current cursor position
		ImVec2 startPos = ImGui::GetCursorPos();

		// Calculate scaled text height
		float fontHeight = ImGui::GetFontSize() * textScale;
		float logoHeight = logoSize.y;

		// Calculate vertical offset to center align logo with text
		float verticalOffset = (fontHeight - logoHeight) * 0.5f;

		// Position cursor for logo with vertical alignment
		ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + verticalOffset));

		// Render logo using draw list with tint color support
		ImVec2 logoPos = ImGui::GetCursorScreenPos();
		ImVec2 logoMin = logoPos;
		ImVec2 logoMax = ImVec2(logoPos.x + logoSize.x, logoPos.y + logoSize.y);
		ImGui::GetWindowDrawList()->AddImage(logoTexture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), logoTint);

		// Advance cursor past logo
		ImGui::Dummy(logoSize);
		ImGui::SameLine();

		// Add consistent spacing between logo and text
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);

		// Reset cursor for text with proper vertical alignment
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), startPos.y));
		// Use windowed font scale for sharper text
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::SetWindowFontScale(textScale);

		// Render text aligned to pixel grid for sharpness
		ImGui::Text("%s", text);
		// Restore style
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		// Calculate and return the total rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	float GetCenterOffsetForContent(float contentWidth)
	{
		// Get full window width for true centering
		float fullWindowWidth = ImGui::GetWindowWidth();
		float windowPaddingX = ImGui::GetStyle().WindowPadding.x;
		float availableFullWidth = fullWindowWidth - (windowPaddingX * 2.0f);

		// Calculate center position
		float centerOffset = (availableFullWidth - contentWidth) * 0.5f;

		// Adjust for current cursor position
		float currentX = ImGui::GetCursorPosX();
		float targetX = windowPaddingX + centerOffset;
		float offset = targetX - currentX;

		return offset > 0.0f ? offset : 0.0f;
	}

	// StyledButtonWrapper implementation
	StyledButtonWrapper::StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor) :
		m_pushedStyles(0)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
		m_pushedStyles = 3;
	}

	StyledButtonWrapper::~StyledButtonWrapper()
	{
		if (m_pushedStyles > 0) {
			ImGui::PopStyleColor(m_pushedStyles);
		}
	}

	// SectionWrapper implementation
	SectionWrapper::SectionWrapper(const char* title, const char* description, const ImVec4& titleColor, bool isVisible) :
		m_shouldDraw(isVisible),
		m_treeNodeOpened(false)
	{
		if (!m_shouldDraw) {
			return;
		}

		ImGui::TextColored(titleColor, "%s", title);
		ImGui::Spacing();

		if (description && strlen(description) > 0) {
			ImGui::TextWrapped("%s", description);
			ImGui::Spacing();
		}
	}

	SectionWrapper::~SectionWrapper()
	{
		if (m_shouldDraw) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
	}

	SectionWrapper::operator bool() const
	{
		return m_shouldDraw;
	}

	bool DrawCategoryHeader(const char* categoryName, bool& isExpanded, int categoryCount)
	{
		// Get the appropriate icon for this category
		ID3D11ShaderResourceView* categoryIcon = nullptr;
		auto& menu = Menu::GetSingleton()->uiIcons;

		if (strcmp(categoryName, "Characters") == 0) {
			categoryIcon = menu.characters.texture;
		} else if (strcmp(categoryName, "Display") == 0) {
			categoryIcon = menu.display.texture;
		} else if (strcmp(categoryName, "Grass") == 0) {
			categoryIcon = menu.grass.texture;
		} else if (strcmp(categoryName, "Lighting") == 0) {
			categoryIcon = menu.lighting.texture;
		} else if (strcmp(categoryName, "Sky") == 0) {
			categoryIcon = menu.sky.texture;
		} else if (strcmp(categoryName, "Landscape & Textures") == 0) {
			categoryIcon = menu.landscape.texture;
		} else if (strcmp(categoryName, "Water") == 0) {
			categoryIcon = menu.water.texture;
		} else if (strcmp(categoryName, "Debug") == 0) {
			categoryIcon = menu.debug.texture;
		} else if (strcmp(categoryName, "Materials") == 0) {
			categoryIcon = menu.materials.texture;
		} else if (strcmp(categoryName, "Post-Processing") == 0) {
			categoryIcon = menu.postProcessing.texture;
		}

		// Add categoryCount to categoryName
		std::string displayName = std::format("{} ({})", categoryName, categoryCount);

		// Draw category header with custom styling
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;

		// Calculate icon size based on current font size to match text scaling
		// This ensures icons scale consistently with text when the font scale changes
		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * 1.2f;     // 20% larger than font height
		const float iconSpacing = currentFontSize * 0.3f;  // 30% of font height for spacing
		ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());

		// Calculate total content width (icon + spacing + text)
		float contentWidth = textSize.x;
		if (categoryIcon) {
			contentWidth += iconSize + iconSpacing;
		}

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - contentWidth - 20.0f) * 0.5f;  // 20px for padding

		// Create selectable area for the entire header
		ImGui::PushID(displayName.c_str());
		bool hovered = false;
		bool clicked = false;

		// Invisible button for hover detection and clicking
		ImGui::SetCursorScreenPos(pos);
		if (ImGui::InvisibleButton("##CategoryHeader", ImVec2(availableWidth, textSize.y + 4.0f))) {
			clicked = true;
		}
		hovered = ImGui::IsItemHovered();

		// Draw the lines and text using Menu theme colors
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& palette = themeSettings.Palette;

		// Use theme text color
		ImVec4 color = palette.Text;

		// If minimized, apply reduced alpha
		if (!isExpanded) {
			color.w *= 0.7f;  // 70% alpha when minimized
		}
		// If hovered, slightly dim the color
		if (hovered) {
			color.w *= 0.8f;  // 80% alpha when hovered
		}
		ImU32 headerColor = ImGui::GetColorU32(color);  // Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + contentWidth + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);
		}

		// Draw icon and text
		float currentX = pos.x + lineLength + 10.0f;

		// Draw icon if available
		if (categoryIcon) {
			ImVec2 iconPos = ImVec2(currentX, pos.y + (textSize.y - iconSize) * 0.5f + 2.0f);
			ImVec2 iconMax = ImVec2(iconPos.x + iconSize, iconPos.y + iconSize);

			// Apply the same color tint as the text
			ImU32 iconTint = headerColor;
			drawList->AddImage(categoryIcon, iconPos, iconMax, ImVec2(0, 0), ImVec2(1, 1), iconTint);

			currentX += iconSize + iconSpacing;
		}

		// Center text
		ImVec2 textPos = ImVec2(currentX, pos.y + 2.0f);
		drawList->AddText(textPos, headerColor, displayName.c_str());

		// Handle click to toggle expansion
		if (clicked) {
			isExpanded = !isExpanded;
		}

		ImGui::PopID();

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		return clicked;
	}

	bool DrawSectionHeader(const char* sectionName, bool useWhiteText, bool isCollapsible, bool* isExpanded)
	{
		bool stateChanged = false;

		// Use Menu theme colors for consistent styling
		auto& theme = globals::menu->GetTheme().FeatureHeading;
		auto& palette = globals::menu->GetTheme().Palette;
		// When useWhiteText is true, use the theme's text color instead of hardcoded white
		ImVec4 color = useWhiteText ? palette.Text : theme.ColorDefault;

		ImU32 headerColor = ImGui::GetColorU32(color);

		if (isCollapsible && isExpanded) {
			// Use collapsible header similar to DrawCategoryHeader
			ImGui::PushID(sectionName);

			ImGui::PushStyleColor(ImGuiCol_Text, headerColor);

			if (ImGui::CollapsingHeader(sectionName, ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = true;
			} else {
				if (*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = false;
			}

			ImGui::PopStyleColor();
			ImGui::PopID();
		} else {
			// Non-collapsible header - use custom styled header similar to CategoryHeader
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 pos = ImGui::GetCursorScreenPos();
			float availableWidth = ImGui::GetContentRegionAvail().x;
			ImVec2 textSize = ImGui::CalcTextSize(sectionName);

			// Calculate line positions
			float lineY = pos.y + textSize.y * 0.5f;
			float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

			// Left line
			if (lineLength > 0) {
				drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);
			}

			// Right line
			float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
			if (rightLineStart < pos.x + availableWidth) {
				drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);
			}

			// Center text
			ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
			drawList->AddText(textPos, headerColor, sectionName);

			// Move cursor to next line
			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		}

		return stateChanged;
	}

	// ColorCodedValueConfig static helper implementations
	ColorCodedValueConfig ColorCodedValueConfig::HighIsBad(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },    // Very low - gray
			{ med, theme.InfoColor },  // Low - blue
			{ high, theme.Warning },   // Medium - orange
			{ FLT_MAX, theme.Error }   // High - red (bad)
		};
		return config;
	}

	ColorCodedValueConfig ColorCodedValueConfig::HighIsGood(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },          // Very low - gray
			{ med, theme.InfoColor },        // Low - blue
			{ high, theme.Warning },         // Medium - orange
			{ FLT_MAX, theme.SuccessColor }  // High - green (good)
		};
		return config;
	}

	void DrawColorCodedValue(
		const std::string& label,
		float valueToCheck,
		const std::string& valueStr,
		const ColorCodedValueConfig& config,
		bool useBullet)
	{
		// Display label
		if (useBullet) {
			ImGui::BulletText("%s", label.c_str());
		} else {
			ImGui::Text("%s", label.c_str());
		}
		if (config.sameLine) {
			ImGui::SameLine();
		}

		// Determine color based on thresholds
		ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // Default white
		for (const auto& tc : config.thresholds) {
			if (valueToCheck < tc.threshold) {
				valueColor = tc.color;
				break;
			}
		}

		// Display colored value (arbitrary string)
		ImGui::TextColored(valueColor, "%s", valueStr.c_str());

		// Add tooltip if provided
		if (config.tooltipText) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", config.tooltipText);
			}
		}
	}

	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors)
	{
		for (size_t i = 0; i < lines.size(); ++i) {
			const char* lineCStr = lines[i].c_str();
			if (!colors.empty() && i < colors.size()) {
				// Use provided color for this line
				ImGui::TextColored(colors[i], "%s", lineCStr);
			} else {
				// Use default color
				ImGui::Text("%s", lineCStr);
			}
		}
	}

	void DrawColoredMultiLineTooltip(const ColoredTextLines& lines)
	{
		for (const auto& line : lines) {
			ImGui::TextColored(line.color, "%s", line.text.c_str());
		}
	}

	void SortTableRowsByColumn(std::vector<std::vector<std::string>>& rows, size_t column, bool ascending)
	{
		std::sort(rows.begin(), rows.end(), [column, ascending](const auto& a, const auto& b) {
			if (column >= a.size() || column >= b.size())
				return false;
			return ascending ? (a[column] < b[column]) : (a[column] > b[column]);
		});
	}

	bool VersionStringLess(const std::string& a, const std::string& b, bool ascending)
	{
		auto split = [](const std::string& s) {
			std::vector<int> parts;
			size_t start = 0, end = 0;
			while ((end = s.find('.', start)) != std::string::npos) {
				try {
					parts.push_back(std::stoi(s.substr(start, end - start)));
				} catch (...) {
					parts.push_back(0);
				}
				start = end + 1;
			}
			if (start < s.size()) {
				try {
					parts.push_back(std::stoi(s.substr(start)));
				} catch (...) {
					parts.push_back(0);
				}
			}
			return parts;
		};
		auto va = split(a), vb = split(b);
		for (size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
			int ai = i < va.size() ? va[i] : 0;
			int bi = i < vb.size() ? vb[i] : 0;
			if (ai != bi)
				return ascending ? (ai < bi) : (ai > bi);
		}
		return false;
	}

	bool VersionSortComparator(const std::string& a, const std::string& b, bool asc)
	{
		return VersionStringLess(a, b, asc);
	}

	bool StringSortComparator(const std::string& a, const std::string& b, bool ascending)
	{
		return ascending ? (a < b) : (b < a);
	}

	void RenderTextWithHighlights(const std::string& text, const std::string& searchTerm, ImVec4 highlightColor)
	{
		if (searchTerm.empty()) {
			ImGui::TextUnformatted(text.c_str());
			return;
		}

		std::string lowerText = text;
		std::string lowerSearch = searchTerm;
		std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
		std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

		size_t pos = 0;
		size_t lastPos = 0;

		while ((pos = lowerText.find(lowerSearch, lastPos)) != std::string::npos) {
			// Render text before highlight
			if (pos > lastPos) {
				ImGui::TextUnformatted(text.substr(lastPos, pos - lastPos).c_str());
				ImGui::SameLine(0, 0);
			}

			// Render highlighted text
			ImGui::PushStyleColor(ImGuiCol_Text, highlightColor);
			ImGui::TextUnformatted(text.substr(pos, searchTerm.length()).c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0, 0);

			lastPos = pos + searchTerm.length();
		}

		// Render remaining text
		if (lastPos < text.length()) {
			ImGui::TextUnformatted(text.substr(lastPos).c_str());
		}
	}

	ImVec4 GetThresholdColor(float value, float good, float warn, ImVec4 goodColor, ImVec4 warnColor, ImVec4 badColor)
	{
		if (value < good)
			return goodColor;
		else if (value < warn)
			return warnColor;
		else
			return badColor;
	}

	bool FeatureMatchesSearch(Feature* feat, const std::string& searchQuery)
	{
		if (searchQuery.empty())
			return true;

		// Get both short name and display name
		std::string shortName = feat->GetShortName();
		std::string displayName = feat->GetName();
		std::string query = searchQuery;

		// Convert all to lowercase for case-insensitive search
		std::transform(shortName.begin(), shortName.end(), shortName.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
		std::transform(displayName.begin(), displayName.end(), displayName.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
		std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

		// Search in both short name and display name
		return shortName.find(query) != std::string::npos ||
		       displayName.find(query) != std::string::npos;
	}

	bool StringMatchesSearch(const std::string& text, const std::string& searchQuery)
	{
		if (searchQuery.empty())
			return true;

		std::string lowerText = text;
		std::string lowerQuery = searchQuery;

		// Convert all to lowercase for case-insensitive search
		std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
		std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

		return lowerText.find(lowerQuery) != std::string::npos;
	}

	void DrawSearchIcon(const ImVec2& position, float size, float alpha)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		ImVec2 center = ImVec2(position.x + size * 0.46f, position.y + size * 0.5f);
		float radius = size * 0.3f;

		// Use themed text color with reduced alpha for search icon
		auto& theme = globals::menu->GetTheme().Palette;
		ImVec4 iconColor = theme.Text;
		iconColor.w *= alpha;  // Apply alpha multiplier for subtler appearance
		ImU32 placeholderColor = ImGui::GetColorU32(iconColor);

		// Draw circle
		drawList->AddCircle(center, radius, placeholderColor, 12, 2.2f);

		// Draw handle
		ImVec2 handleStart = ImVec2(center.x + radius * 0.81f, center.y + radius * 0.81f);
		ImVec2 handleEnd = ImVec2(handleStart.x + size * 0.29f, handleStart.y + size * 0.29f);
		drawList->AddLine(handleStart, handleEnd, placeholderColor, 2.1f);
	}

	void DrawFeatureSearchBar(std::string& searchString, float availableWidth)
	{
		ImGui::PushID("FeatureSearchBar");

		float iconSize = 20.0f;
		float iconSpace = iconSize + 14.0f;

		// Get the current cursor position and available width
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		if (availableWidth <= 0.0f) {
			availableWidth = ImGui::GetContentRegionAvail().x;
		}
		float frameHeight = ImGui::GetFrameHeight();

		// Custom style - always transparent background to avoid click blocking
		ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		ImVec4 bgColorActive = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
		// Use theme text color instead of hardcoded color
		auto& palette = globals::menu->GetTheme().Palette;
		ImVec4 textColor = palette.Text;

		ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgColorActive);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, 6.0f));

		// Draw the input field
		ImGui::SetNextItemWidth(availableWidth);
		char buffer[256];
		strncpy_s(buffer, searchString.c_str(), sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';

		if (ImGui::InputTextWithHint("##feature_search", "Search Features...", buffer, sizeof(buffer))) {
			searchString = buffer;
		}

		// Draw search icon using the reusable function
		ImVec2 iconPos = ImVec2(cursorPos.x + 8.0f, cursorPos.y + (frameHeight - iconSize) * 0.5f);
		DrawSearchIcon(iconPos, iconSize, 0.7f);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	void ShowSortedStringTableStrings(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<std::string>>& rows,
		size_t sortColumn,
		bool ascending,
		const std::vector<TableSortFunc>& customSorts,
		TableCellRenderFunc cellRender)
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable;
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags)) {
			for (const auto& header : headers)
				ImGui::TableSetupColumn(header.c_str());
			ImGui::TableHeadersRow();

			// Determine sorting
			int sortCol = static_cast<int>(sortColumn);
			bool sortAsc = ascending;
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsCount > 0) {
					sortCol = sortSpecs->Specs->ColumnIndex;
					sortAsc = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
				}
			}

			// Make a copy if sorting is needed
			std::vector<std::vector<std::string>> sortedRows = rows;
			if (sortCol >= 0 && static_cast<size_t>(sortCol) < headers.size()) {
				// Fallback to default string sort if no custom sort is provided
				auto cmp = (sortCol < static_cast<int>(customSorts.size()) && customSorts[sortCol]) ? customSorts[sortCol] : StringSortComparator;
				std::sort(sortedRows.begin(), sortedRows.end(), [sortCol, sortAsc, &cmp](const std::vector<std::string>& a, const std::vector<std::string>& b) {
					const std::string& aVal = (sortCol < a.size()) ? a[sortCol] : std::string();
					const std::string& bVal = (sortCol < b.size()) ? b[sortCol] : std::string();
					return cmp(aVal, bVal, sortAsc);
				});
			}

			// Render rows
			for (size_t rowIdx = 0; rowIdx < sortedRows.size(); ++rowIdx) {
				const auto& row = sortedRows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						const std::string& value = (col < row.size()) ? row[col] : std::string();
						cellRender(static_cast<int>(rowIdx), static_cast<int>(col), value);
					} else {
						if (col < row.size())
							ImGui::TextUnformatted(row[col].c_str());
					}
				}
			}
			ImGui::EndTable();
		}
	}

	// Theme-aware color accessor functions
	namespace Colors
	{
		ImVec4 GetTimerGood()
		{
			return globals::menu->GetTheme().StatusPalette.SuccessColor;
		}

		ImVec4 GetTimerWarning()
		{
			return globals::menu->GetTheme().StatusPalette.Warning;
		}

		ImVec4 GetTimerCritical()
		{
			return globals::menu->GetTheme().StatusPalette.Error;
		}

		ImVec4 GetDefault()
		{
			return globals::menu->GetTheme().Palette.Text;
		}

		ImVec4 GetSuccess()
		{
			return globals::menu->GetTheme().StatusPalette.SuccessColor;
		}

		ImVec4 GetWarning()
		{
			return globals::menu->GetTheme().StatusPalette.Warning;
		}

		ImVec4 GetError()
		{
			return globals::menu->GetTheme().StatusPalette.Error;
		}

		ImVec4 GetInfo()
		{
			return globals::menu->GetTheme().StatusPalette.InfoColor;
		}

		ImVec4 GetDisabled()
		{
			return globals::menu->GetTheme().StatusPalette.Disable;
		}
	}

	namespace Input
	{
#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)

		ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey)
		{
			switch (vkKey) {
			case VK_TAB:
				return ImGuiKey_Tab;
			case VK_LEFT:
				return ImGuiKey_LeftArrow;
			case VK_RIGHT:
				return ImGuiKey_RightArrow;
			case VK_UP:
				return ImGuiKey_UpArrow;
			case VK_DOWN:
				return ImGuiKey_DownArrow;
			case VK_PRIOR:
				return ImGuiKey_PageUp;
			case VK_NEXT:
				return ImGuiKey_PageDown;
			case VK_HOME:
				return ImGuiKey_Home;
			case VK_END:
				return ImGuiKey_End;
			case VK_INSERT:
				return ImGuiKey_Insert;
			case VK_DELETE:
				return ImGuiKey_Delete;
			case VK_BACK:
				return ImGuiKey_Backspace;
			case VK_SPACE:
				return ImGuiKey_Space;
			case VK_RETURN:
				return ImGuiKey_Enter;
			case VK_ESCAPE:
				return ImGuiKey_Escape;
			case VK_OEM_7:
				return ImGuiKey_Apostrophe;
			case VK_OEM_COMMA:
				return ImGuiKey_Comma;
			case VK_OEM_MINUS:
				return ImGuiKey_Minus;
			case VK_OEM_PERIOD:
				return ImGuiKey_Period;
			case VK_OEM_2:
				return ImGuiKey_Slash;
			case VK_OEM_1:
				return ImGuiKey_Semicolon;
			case VK_OEM_PLUS:
				return ImGuiKey_Equal;
			case VK_OEM_4:
				return ImGuiKey_LeftBracket;
			case VK_OEM_5:
				return ImGuiKey_Backslash;
			case VK_OEM_6:
				return ImGuiKey_RightBracket;
			case VK_OEM_3:
				return ImGuiKey_GraveAccent;
			case VK_CAPITAL:
				return ImGuiKey_CapsLock;
			case VK_SCROLL:
				return ImGuiKey_ScrollLock;
			case VK_NUMLOCK:
				return ImGuiKey_NumLock;
			case VK_SNAPSHOT:
				return ImGuiKey_PrintScreen;
			case VK_PAUSE:
				return ImGuiKey_Pause;
			case VK_NUMPAD0:
				return ImGuiKey_Keypad0;
			case VK_NUMPAD1:
				return ImGuiKey_Keypad1;
			case VK_NUMPAD2:
				return ImGuiKey_Keypad2;
			case VK_NUMPAD3:
				return ImGuiKey_Keypad3;
			case VK_NUMPAD4:
				return ImGuiKey_Keypad4;
			case VK_NUMPAD5:
				return ImGuiKey_Keypad5;
			case VK_NUMPAD6:
				return ImGuiKey_Keypad6;
			case VK_NUMPAD7:
				return ImGuiKey_Keypad7;
			case VK_NUMPAD8:
				return ImGuiKey_Keypad8;
			case VK_NUMPAD9:
				return ImGuiKey_Keypad9;
			case VK_DECIMAL:
				return ImGuiKey_KeypadDecimal;
			case VK_DIVIDE:
				return ImGuiKey_KeypadDivide;
			case VK_MULTIPLY:
				return ImGuiKey_KeypadMultiply;
			case VK_SUBTRACT:
				return ImGuiKey_KeypadSubtract;
			case VK_ADD:
				return ImGuiKey_KeypadAdd;
			case IM_VK_KEYPAD_ENTER:
				return ImGuiKey_KeypadEnter;
			case VK_LSHIFT:
				return ImGuiKey_LeftShift;
			case VK_LCONTROL:
				return ImGuiKey_LeftCtrl;
			case VK_LMENU:
				return ImGuiKey_LeftAlt;
			case VK_LWIN:
				return ImGuiKey_LeftSuper;
			case VK_RSHIFT:
				return ImGuiKey_RightShift;
			case VK_RCONTROL:
				return ImGuiKey_RightCtrl;
			case VK_RMENU:
				return ImGuiKey_RightAlt;
			case VK_RWIN:
				return ImGuiKey_RightSuper;
			case VK_APPS:
				return ImGuiKey_Menu;
			case '0':
				return ImGuiKey_0;
			case '1':
				return ImGuiKey_1;
			case '2':
				return ImGuiKey_2;
			case '3':
				return ImGuiKey_3;
			case '4':
				return ImGuiKey_4;
			case '5':
				return ImGuiKey_5;
			case '6':
				return ImGuiKey_6;
			case '7':
				return ImGuiKey_7;
			case '8':
				return ImGuiKey_8;
			case '9':
				return ImGuiKey_9;
			case 'A':
				return ImGuiKey_A;
			case 'B':
				return ImGuiKey_B;
			case 'C':
				return ImGuiKey_C;
			case 'D':
				return ImGuiKey_D;
			case 'E':
				return ImGuiKey_E;
			case 'F':
				return ImGuiKey_F;
			case 'G':
				return ImGuiKey_G;
			case 'H':
				return ImGuiKey_H;
			case 'I':
				return ImGuiKey_I;
			case 'J':
				return ImGuiKey_J;
			case 'K':
				return ImGuiKey_K;
			case 'L':
				return ImGuiKey_L;
			case 'M':
				return ImGuiKey_M;
			case 'N':
				return ImGuiKey_N;
			case 'O':
				return ImGuiKey_O;
			case 'P':
				return ImGuiKey_P;
			case 'Q':
				return ImGuiKey_Q;
			case 'R':
				return ImGuiKey_R;
			case 'S':
				return ImGuiKey_S;
			case 'T':
				return ImGuiKey_T;
			case 'U':
				return ImGuiKey_U;
			case 'V':
				return ImGuiKey_V;
			case 'W':
				return ImGuiKey_W;
			case 'X':
				return ImGuiKey_X;
			case 'Y':
				return ImGuiKey_Y;
			case 'Z':
				return ImGuiKey_Z;
			case VK_F1:
				return ImGuiKey_F1;
			case VK_F2:
				return ImGuiKey_F2;
			case VK_F3:
				return ImGuiKey_F3;
			case VK_F4:
				return ImGuiKey_F4;
			case VK_F5:
				return ImGuiKey_F5;
			case VK_F6:
				return ImGuiKey_F6;
			case VK_F7:
				return ImGuiKey_F7;
			case VK_F8:
				return ImGuiKey_F8;
			case VK_F9:
				return ImGuiKey_F9;
			case VK_F10:
				return ImGuiKey_F10;
			case VK_F11:
				return ImGuiKey_F11;
			case VK_F12:
				return ImGuiKey_F12;
			default:
				return ImGuiKey_None;
			};
		}

		uint32_t DIKToVK(uint32_t dikKey)
		{
			switch (dikKey) {
			case DIK_LEFTARROW:
				return VK_LEFT;
			case DIK_RIGHTARROW:
				return VK_RIGHT;
			case DIK_UPARROW:
				return VK_UP;
			case DIK_DOWNARROW:
				return VK_DOWN;
			case DIK_DELETE:
				return VK_DELETE;
			case DIK_END:
				return VK_END;
			case DIK_HOME:
				return VK_HOME;  // pos1
			case DIK_PRIOR:
				return VK_PRIOR;  // page up
			case DIK_NEXT:
				return VK_NEXT;  // page down
			case DIK_INSERT:
				return VK_INSERT;
			case DIK_NUMPAD0:
				return VK_NUMPAD0;
			case DIK_NUMPAD1:
				return VK_NUMPAD1;
			case DIK_NUMPAD2:
				return VK_NUMPAD2;
			case DIK_NUMPAD3:
				return VK_NUMPAD3;
			case DIK_NUMPAD4:
				return VK_NUMPAD4;
			case DIK_NUMPAD5:
				return VK_NUMPAD5;
			case DIK_NUMPAD6:
				return VK_NUMPAD6;
			case DIK_NUMPAD7:
				return VK_NUMPAD7;
			case DIK_NUMPAD8:
				return VK_NUMPAD8;
			case DIK_NUMPAD9:
				return VK_NUMPAD9;
			case DIK_DECIMAL:
				return VK_DECIMAL;
			case DIK_NUMPADENTER:
				return IM_VK_KEYPAD_ENTER;
			case DIK_RMENU:
				return VK_RMENU;  // right alt
			case DIK_RCONTROL:
				return VK_RCONTROL;  // right control
			case DIK_LWIN:
				return VK_LWIN;  // left win
			case DIK_RWIN:
				return VK_RWIN;  // right win
			case DIK_APPS:
				return VK_APPS;
			default:
				return dikKey;
			}
		}

		const char* KeyIdToString(uint32_t key)
		{
			if (key >= 256)
				return "";

			static const char* keyboard_keys_international[256] = {
				"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
				"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
				"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
				"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
				"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
				"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
				"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
				"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
				"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
				"Num Lock", "Scroll Lock", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
				"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "OEM ;", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM /",
				"OEM ~", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "", "", "", "", "", "OEM [", "OEM \\", "OEM ]", "OEM '", "OEM 8",
				"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
			};

			return keyboard_keys_international[key];
		}
	}  // namespace Input

	bool ButtonWithFlash(const char* label, const ImVec2& size, int flashDurationMs)
	{
		static std::unordered_map<std::string, std::chrono::steady_clock::time_point> flashTimers;
		static std::mutex flashTimersMutex;

		std::string buttonId = std::string(label);
		auto now = std::chrono::steady_clock::now();

		// Check if this button has active flash (thread-safe)
		bool hasActiveFlash = false;
		{
			std::lock_guard<std::mutex> lock(flashTimersMutex);
			auto it = flashTimers.find(buttonId);
			if (it != flashTimers.end()) {
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
				if (elapsed.count() < flashDurationMs) {
					hasActiveFlash = true;
				} else {
					// Flash expired, remove it
					flashTimers.erase(it);
				}
			}
		}

		// Style the button with flash effect if active.
		bool styleChanged = false;
		if (hasActiveFlash) {
			// Use subtle white overlay similar to action icon hover effect
			ImVec4 normalButton = ImGui::GetStyleColorVec4(ImGuiCol_Button);
			ImVec4 flashColor = ImVec4(
				normalButton.x + 0.2f,  // Brighten slightly
				normalButton.y + 0.2f,
				normalButton.z + 0.2f,
				normalButton.w);
			ImVec4 flashHovered = ImVec4(flashColor.x * 1.1f, flashColor.y * 1.1f, flashColor.z * 1.1f, flashColor.w);
			ImVec4 flashActive = ImVec4(flashColor.x * 0.9f, flashColor.y * 0.9f, flashColor.z * 0.9f, flashColor.w);

			ImGui::PushStyleColor(ImGuiCol_Button, flashColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, flashHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, flashActive);
			styleChanged = true;
		}

		bool clicked = ImGui::Button(label, size);

		if (styleChanged) {
			ImGui::PopStyleColor(3);
		}

		// If clicked, start the flash timer (thread-safe)
		if (clicked) {
			std::lock_guard<std::mutex> lock(flashTimersMutex);
			flashTimers[buttonId] = now;
		}

		return clicked;
	}

	bool LoadDDSTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size)
	{
		if (!device || !out_srv) {
			logger::warn("LoadDDSTextureFromFile: Invalid parameters");
			return false;
		}

		*out_srv = nullptr;

		// Try to load from BSA using Skyrim's resource system
		RE::BSResourceNiBinaryStream bsaStream(filename);
		if (!bsaStream.good()) {
			logger::warn("LoadDDSTextureFromFile: Failed to open resource: {}", filename);
			return false;
		}

		// Read entire DDS file into memory
		std::vector<uint8_t> ddsData;
		auto size = bsaStream.stream->totalSize;
		if (size == 0) {
			logger::warn("LoadDDSTextureFromFile: Resource has zero size: {}", filename);
			return false;
		}

		ddsData.resize(size);
		bsaStream.read(reinterpret_cast<char*>(ddsData.data()), size);

		// Load DDS from memory
		DirectX::ScratchImage image;
		try {
			DX::ThrowIfFailed(DirectX::LoadFromDDSMemory(
				ddsData.data(),
				ddsData.size(),
				DirectX::DDS_FLAGS_NONE,
				nullptr,
				image));
		} catch (const DX::com_exception& e) {
			logger::warn("LoadDDSTextureFromFile: Failed to load DDS data from {}: {}", filename, e.what());
			return false;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(DirectX::CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::warn("LoadDDSTextureFromFile: Failed to create texture: {}", e.what());
			return false;
		}

		ID3D11Texture2D* pTexture = reinterpret_cast<ID3D11Texture2D*>(pResource);
		D3D11_TEXTURE2D_DESC desc;
		pTexture->GetDesc(&desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = desc.MipLevels }
		};

		HRESULT hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		pTexture->Release();

		if (FAILED(hr) || !*out_srv) {
			logger::warn("LoadDDSTextureFromFile: Failed to create SRV, HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			return false;
		}

		out_size = ImVec2((float)desc.Width, (float)desc.Height);
		logger::debug("LoadDDSTextureFromFile: Successfully loaded {} ({}x{})", filename, desc.Width, desc.Height);
		return true;
	}

	bool FeatureToggle(const char* label, bool* enabled, const ImVec2& size)
	{
		if (!enabled)
			return false;

		// Calculate appropriate size if not specified - make it smaller
		ImVec2 toggleSize = size;
		if (toggleSize.x <= 0) {
			toggleSize.x = ImGui::GetFrameHeight() * 1.6f;  // Smaller 1.6:1 aspect ratio
		}
		if (toggleSize.y <= 0) {
			toggleSize.y = ImGui::GetFrameHeight() * 0.8f;  // Smaller height
		}

		// Get theme colors for better integration
		auto& style = ImGui::GetStyle();
		auto& colors = style.Colors;

		// Use theme header colors instead of bright green/red
		ImVec4 toggleBg = *enabled ?
		                      colors[ImGuiCol_Header] :  // Use header color when enabled
		                      colors[ImGuiCol_FrameBg];  // Use frame background when disabled

		ImVec4 toggleBgHovered = *enabled ?
		                             colors[ImGuiCol_HeaderHovered] :  // Use header hovered when enabled
		                             colors[ImGuiCol_FrameBgHovered];  // Use frame hovered when disabled

		ImVec4 toggleBgActive = *enabled ?
		                            colors[ImGuiCol_HeaderActive] :  // Use header active when enabled
		                            colors[ImGuiCol_FrameBgActive];  // Use frame active when disabled

		// Apply toggle styling with border
		ImGui::PushStyleColor(ImGuiCol_Button, toggleBg);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toggleBgHovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, toggleBgActive);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, toggleSize.y * 0.5f);  // Round ends
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);               // Larger border

		// Create unique ID for the toggle
		ImGui::PushID(label);

		// Draw the toggle button
		bool clicked = ImGui::Button("", toggleSize);

		// Draw the toggle knob
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 buttonMin = ImGui::GetItemRectMin();
		ImVec2 buttonMax = ImGui::GetItemRectMax();

		// Calculate knob position and size
		float knobRadius = (toggleSize.y - 4.0f) * 0.5f;
		float knobPadding = 2.0f;
		float knobTravel = toggleSize.x - (knobRadius * 2.0f) - (knobPadding * 2.0f);
		float knobX = *enabled ?
		                  buttonMin.x + knobPadding + knobRadius + knobTravel :
		                  buttonMin.x + knobPadding + knobRadius;
		float knobY = buttonMin.y + toggleSize.y * 0.5f;

		// Draw knob
		ImU32 knobColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, knobColor);

		ImGui::PopID();
		ImGui::PopStyleVar(2);  // Pop both FrameRounding and FrameBorderSize
		ImGui::PopStyleColor(3);

		// Handle toggle action
		if (clicked) {
			*enabled = !*enabled;
		}

		return clicked;
	}

}  // namespace Util

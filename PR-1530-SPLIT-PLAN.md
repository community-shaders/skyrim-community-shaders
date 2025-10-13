# PR #1530 Split Plan - UI Flair, Better Themes & Settings, Font Support

**Original PR:** https://github.com/doodlum/skyrim-community-shaders/pull/1530  
**Total Changes:** ~6,800 lines across 117 files  
**Split Strategy:** 5 logical, sequential PRs  
**Author:** @davo0411  
**Date:** October 13, 2025

---

## Executive Summary

This document outlines a comprehensive plan to split the massive PR #1530 into 5 manageable, logically-grouped pull requests. The original PR introduces:

- **Theme System:** Hot-swappable JSON themes with 9 presets
- **Font System:** Role-based font architecture with 15+ font families
- **Settings UI Overhaul:** Reorganized settings into Themes/Fonts/Styling/Colors tabs
- **Background Blur:** GPU-accelerated Gaussian blur effect
- **UI Enhancements:** Flashing buttons, compact toggles, visual polish
- **Performance Overlay:** Improved layout with inline A/B test display

**Key Principle:** Each PR is independently testable, builds on previous PRs, and preserves the original design intent.

---

## Dependency Chain & Merge Order

**CRITICAL:** PRs MUST be merged in this exact order:

```
PR #1: Core Infrastructure (Utils & Theme JSON)
  ↓ (Provides PathHelpers and theme loading)
PR #2: Font System Infrastructure & Assets
  ↓ (Provides font discovery and role system)
PR #3: Settings UI Overhaul (Themes/Fonts/Styling Tabs)
  ↓ (Provides UI for theme/font customization)
PR #4: UI Candy (Blur, Buttons, Visual Polish)
  ↓ (Adds visual enhancements)
PR #5: Performance Overlay & Miscellaneous
```

---

## PR #1: Core Infrastructure - Utilities, PathHelpers & Theme JSON System

**Branch Name:** `feature/theme-system-infrastructure`  
**Title:** `feat(ui): Add theme system infrastructure with PathHelpers and JSON support`  
**Estimated Lines:** ~1,200  
**Goal:** Establish foundational utilities and theme loading/saving infrastructure without UI changes

### Files to Include

#### Core Utility Functions (6 files)
- ✅ `src/Utils/FileSystem.h` - Add PathHelpers namespace
- ✅ `src/Utils/FileSystem.cpp` - Implement all PathHelper functions
- ✅ `src/State.h` - Add THEME config mode enum
- ✅ `src/State.cpp` - Add LoadTheme/SaveTheme functions
- ✅ `src/SettingsOverrideManager.h` - Update to use PathHelpers
- ✅ `src/SettingsOverrideManager.cpp` - Replace hardcoded paths with PathHelpers

#### ThemeManager Core - JSON System Only (2 files)
- ✅ `src/Menu/ThemeManager.h` - **PARTIAL:**
  - ✅ Include: ThemeInfo struct
  - ✅ Include: JSON loading/saving function declarations
  - ✅ Include: Theme discovery function declarations
  - ✅ Include: ValidateThemeData(), CreateDefaultThemeFiles(), ForceApplyDefaultTheme()
  - ✅ Include: SetupImGuiStyle() for applying themes to ImGui
  - ❌ **EXCLUDE:** All blur shader code (static members, InitializeBlurShaders, RenderBackgroundBlur, CleanupBlurResources)

- ✅ `src/Menu/ThemeManager.cpp` - **PARTIAL:**
  - ✅ Include: All theme JSON parsing/saving logic
  - ✅ Include: DiscoverThemes(), GetThemeNames(), GetThemes()
  - ✅ Include: LoadTheme(), SaveTheme()
  - ✅ Include: ValidateThemeData()
  - ✅ Include: CreateDefaultThemeFiles(), ForceApplyDefaultTheme()
  - ✅ Include: SetupImGuiStyle() (applies theme to ImGui style)
  - ✅ Include: ResolveFontSize()
  - ❌ **EXCLUDE:** All blur shader initialization, rendering, and cleanup code (lines ~1000-1600 in original)

#### Theme JSON Files (9 files)
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/Default.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/Amber.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/DragonBlood.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/DwemerBronze.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/Forest.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/HighContrast.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/Light.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/NordicFrost.json`
- ✅ `package/SKSE/Plugins/CommunityShaders/Themes/Ocean.json`

#### Menu Integration - Basic (2 files)
- ✅ `src/Menu.h` - **PARTIAL:**
  - ✅ Include: ThemeSettings struct definition
  - ✅ Include: LoadTheme/SaveTheme/DiscoverThemes/LoadThemePreset declarations
  - ✅ Include: SelectedThemePreset in Settings struct
  - ❌ **EXCLUDE:** Font-related members (cachedFontName, cachedFontSize, loadedFontRoles, pendingFontReload)
  - ❌ **EXCLUDE:** FontRole enum, FontRoleDescriptor, FontRoleSettings

- ✅ `src/Menu.cpp` - **PARTIAL:**
  - ✅ Include: LoadTheme/SaveTheme/DiscoverThemes/LoadThemePreset implementations
  - ✅ Include: Basic theme application in Init()
  - ✅ Include: SaveTheme() call in existing save logic
  - ❌ **EXCLUDE:** ReloadFont(), BuildFontSignature(), font validation, font reload triggers

#### Plugin Initialization (1 file)
- ✅ `src/XSEPlugin.cpp` - **Add:**
  - ✅ CreateDefaultThemes() call at startup
  - ✅ DiscoverThemes() call
  - ✅ LoadTheme() call

### What This PR Achieves
- ✅ Theme JSON loading/saving system fully functional
- ✅ All 9 default themes available and loadable
- ✅ PathHelpers consolidate all path construction (reduces code duplication)
- ✅ Settings infrastructure can load/save themes
- ✅ Theme switching works programmatically (via code)
- ❌ **NO UI CHANGES** - Users can't select themes in menu yet (that's PR #3)
- ✅ Foundation ready for font system (PR #2)

### Testing Checklist
- [ ] Build compiles without errors
- [ ] All 9 default theme JSON files created on first run
- [ ] Themes load from JSON correctly
- [ ] Theme validation catches malformed JSON
- [ ] PathHelpers return correct paths for all locations
- [ ] Theme switching works programmatically
- [ ] ImGui style updates when theme loaded

---

## PR #2: Font System Infrastructure & Font Assets

**Branch Name:** `feature/font-system-and-assets`  
**Title:** `feat(ui): Add role-based font system with discovery, validation, and 15+ font families`  
**Estimated Lines:** ~1,800 + 72 font files  
**Goal:** Complete font discovery, validation, role-based system, and all font assets

### Files to Include

#### Font System Core (2 files)
- ✅ `src/Menu/Fonts.h` - **COMPLETE FILE:**
  - Font role definitions
  - FontRoleSettings struct
  - NormalizeFontRoles() declaration
  - GetDefaultRole() declaration
  - BuildFontSignature() declaration
  - Catalog structs (FamilyInfo, StyleInfo, Catalog)
  - DiscoverFonts(), DiscoverFontCatalog() declarations
  - ValidateFont() declaration
  - FormatFontDisplayName() declaration
  - Path validation helpers

- ✅ `src/Menu/Fonts.cpp` - **COMPLETE FILE (~600 lines):**
  - NormalizeFontRoles() implementation
  - GetDefaultRole() implementation
  - BuildFontSignature() implementation
  - DiscoverFonts() implementation
  - DiscoverFontCatalog() implementation (recursive font discovery)
  - ValidateFont() implementation (with path security checks)
  - FormatFontDisplayName() implementation
  - IsPathWithinDirectory() security helper

#### Menu Font Integration (2 files)
- ✅ `src/Menu.h` - **ADD:**
  - FontRole enum (Body, Heading, Subheading, Subtitle, Caption, Monospace)
  - FontRoleDescriptor struct
  - FontRoleDescriptors static array (6 roles with display names, keys, default scales)
  - FontRoleSettings struct in ThemeSettings
  - Font caching members:
    - `std::string cachedFontName`
    - `float cachedFontSize`
    - `std::array<std::string, 6> cachedFontFilesByRole`
    - `std::string cachedFontSignature`
    - `std::array<ImFont*, 6> loadedFontRoles`
    - `bool pendingFontReload`
  - BuildFontSignature() declaration
  - CreateDefaultThemes() declaration

- ✅ `src/Menu.cpp` - **ADD:**
  - ReloadFont() implementation (full font atlas rebuilding logic)
  - BuildFontSignature() implementation
  - Font validation in Draw() (signature comparison)
  - Font reload trigger logic (pendingFontReload flag)
  - CreateDefaultThemes() implementation
  - Font initialization in Init()

#### ThemeManager Font Support (2 files)
- ✅ `src/Menu/ThemeManager.h` - **ADD:**
  - ReloadFont() declaration
  - ResolveFontRole() declaration

- ✅ `src/Menu/ThemeManager.cpp` - **ADD:**
  - ReloadFont() full implementation (~250 lines)
    - Font atlas clearing
    - Per-role font loading with fallbacks
    - Font atlas building
    - Device object recreation
    - Error handling with emergency fallback to default font
  - ResolveFontRole() implementation (maps role enum to font settings)
  - Font size resolution with scale factors

#### Font RAII Wrapper (2 files)
- ✅ `src/Utils/UI.h` - **ADD:**
  - FontRoleScope class declaration
  - Constructor/destructor for RAII font switching

- ✅ `src/Utils/UI.cpp` - **ADD:**
  - FontRoleScope implementation
  - PushFont/PopFont logic

#### Feature Integration - Font Role Usage (5 files)
Apply FontRoleScope throughout existing UI code:

- ✅ `src/Menu/MenuHeaderRenderer.cpp`
  - Use FontRoleScope(FontRole::Heading) for menu headers

- ✅ `src/Menu/FeatureListRenderer.cpp`
  - Use FontRoleScope(FontRole::Heading) for feature section headers
  - Use FontRoleScope(FontRole::Body) for feature settings
  - Use FontRoleScope(FontRole::Subtitle) for descriptions

- ✅ `src/Menu/HomePageRenderer.cpp`
  - Use FontRoleScope where appropriate

- ✅ `src/Menu/OverlayRenderer.cpp`
  - Use FontRoleScope for overlay text

- ✅ `src/Features/VR.cpp`
  - Use FontRoleScope for VR-specific UI

#### All Font Files & Licenses (72 files)

**Directory Structure:**
```
package/Interface/CommunityShaders/Fonts/
├── Bitter/
│   ├── Bitter-Light.ttf
│   ├── Bitter-Regular.ttf
│   ├── Bitter-SemiBold.ttf
│   └── OFL.txt
├── CrimsonPro/
│   ├── CrimsonPro-Light.ttf
│   ├── CrimsonPro-Regular.ttf
│   ├── CrimsonPro-SemiBold.ttf
│   └── OFL.txt
├── Crimson_Pro/ (duplicate - can be removed if desired)
│   ├── CrimsonPro-Light.ttf
│   ├── CrimsonPro-Regular.ttf
│   └── CrimsonPro-SemiBold.ttf
├── IBMPlexMono/
│   ├── IBMPlexMono-Light.ttf
│   ├── IBMPlexMono-Regular.ttf
│   ├── IBMPlexMono-SemiBold.ttf
│   └── OFL.txt
├── IBMPlexSans/
│   ├── IBMPlexSans-Light.ttf
│   ├── IBMPlexSans-Regular.ttf
│   ├── IBMPlexSans-SemiBold.ttf
│   ├── IBMPlexSans_Condensed-Light.ttf
│   ├── IBMPlexSans_Condensed-Regular.ttf
│   ├── IBMPlexSans_Condensed-SemiBold.ttf
│   └── OFL.txt
├── IBMPlexSerif/
│   ├── IBMPlexSerif-Light.ttf
│   ├── IBMPlexSerif-Regular.ttf
│   ├── IBMPlexSerif-SemiBold.ttf
│   └── OFL.txt
├── IBM_Plex_Sans/ (duplicate - can be removed if desired)
│   ├── IBMPlexSans-Light.ttf
│   ├── IBMPlexSans-Regular.ttf
│   ├── IBMPlexSans-SemiBold.ttf
│   ├── IBMPlexSans_Condensed-Light.ttf
│   ├── IBMPlexSans_Condensed-Regular.ttf
│   ├── IBMPlexSans_Condensed-SemiBold.ttf
│   ├── IBMPlexSans_SemiCondensed-Light.ttf
│   ├── IBMPlexSans_SemiCondensed-Regular.ttf
│   └── IBMPlexSans_SemiCondensed-SemiBold.ttf
├── Inter/
│   ├── Inter_24pt-Light.ttf
│   ├── Inter_24pt-Regular.ttf
│   ├── Inter_24pt-SemiBold.ttf
│   └── OFL.txt
├── Jost/ (moved from root Fonts directory)
│   ├── Jost-Light.ttf
│   ├── Jost-Regular.ttf
│   ├── Jost-SemiBold.ttf
│   └── OFL.txt
├── Merriweather/
│   ├── Merriweather_24pt-Light.ttf
│   ├── Merriweather_24pt-Regular.ttf
│   ├── Merriweather_24pt-SemiBold.ttf
│   ├── Merriweather_24pt_SemiCondensed-Light.ttf
│   ├── Merriweather_24pt_SemiCondensed-Regular.ttf
│   ├── Merriweather_24pt_SemiCondensed-SemiBold.ttf
│   └── OFL.txt
├── Rajdhani/
│   ├── Rajdhani-Light.ttf
│   ├── Rajdhani-Regular.ttf
│   └── OFL.txt
├── Roboto/
│   ├── Roboto-Bold.ttf
│   ├── Roboto-Regular.ttf
│   ├── Roboto-SemiBold.ttf
│   ├── Roboto-Thin.ttf
│   ├── Roboto_Condensed-Light.ttf
│   ├── Roboto_Condensed-Regular.ttf
│   ├── Roboto_Condensed-SemiBold.ttf
│   └── OFL.txt
├── RobotoSlab/
│   ├── RobotoSlab-Light.ttf
│   ├── RobotoSlab-Regular.ttf
│   └── RobotoSlab-SemiBold.ttf
├── Rubik/
│   ├── Rubik-Light.ttf
│   ├── Rubik-Regular.ttf
│   ├── Rubik-SemiBold.ttf
│   └── OFL.txt
├── Sanguis/
│   └── Sanguis.ttf
├── Sovngarde/
│   ├── SovngardeBold.ttf
│   └── SovngardeLight.ttf
└── WorkSans/
    ├── WorkSans-Light.ttf
    ├── WorkSans-Regular.ttf
    ├── WorkSans-SemiBold.ttf
    └── OFL.txt
```

**Total:** 72 files (60 .ttf fonts + 12 OFL.txt licenses)

### What This PR Achieves
- ✅ Complete font discovery and catalog system
- ✅ Role-based font architecture (6 semantic roles: Body, Heading, Subheading, Subtitle, Caption, Monospace)
- ✅ Font validation with security (path traversal protection via IsPathWithinDirectory)
- ✅ All 15+ font families included with proper licenses
- ✅ Font atlas rebuilding without crashes (safe ImGui_ImplDX11 integration)
- ✅ RAII-based font scope guards for easy font switching
- ✅ Fonts work with existing themes from PR #1
- ✅ Font caching and signature-based change detection
- ❌ **Still minimal UI changes** - Fonts work but no UI to select them yet (that's PR #3)

### Testing Checklist
- [ ] Build compiles without errors
- [ ] All fonts discovered in catalog
- [ ] Font validation catches invalid/dangerous paths
- [ ] Font roles apply correctly per context (headers use heading font, body uses body font)
- [ ] Font atlas rebuilds without crashes when switching fonts
- [ ] Font changes persist across restarts
- [ ] FontRoleScope RAII wrapper works (push/pop without leaks)
- [ ] Emergency fallback to default font works if user font fails

---

## PR #3: Settings UI Overhaul - Themes, Fonts, Styling Tabs

**Branch Name:** `feature/settings-ui-overhaul`  
**Title:** `feat(ui): Reorganize settings into Themes/Fonts/Styling/Colors tabs with modern controls`  
**Estimated Lines:** ~1,600  
**Goal:** Modernize settings UI with new tab structure, theme/font selectors, and styling controls

### Files to Include

#### Settings Tab Renderer Updates (2 files)
- ✅ `src/Menu/SettingsTabRenderer.h` - **UPDATE:**
  - Add RenderThemesTab() declaration
  - Add RenderFontsTab() declaration
  - Add RenderStylingTab() declaration
  - Add RenderColorsTab() declaration
  - Update RenderInterfaceTab() to use sub-tabs

- ✅ `src/Menu/SettingsTabRenderer.cpp` - **MAJOR REWRITE (~580 new lines):**
  
  **RenderThemesTab() Implementation:**
  - Theme preset selector dropdown (displays all discovered themes)
  - "Create New Theme" button
  - "Save Current Theme" button with name input
  - "Delete Theme" button with confirmation
  - Theme metadata display (DisplayName, Author, Version, Description)
  - Theme preview (show colors before applying)
  - "Apply Theme" button
  - "Refresh Themes" button
  
  **RenderFontsTab() Implementation:**
  - Global font scale slider (-2.0 to +2.0 exponent)
  - Per-role font configuration (6 sections, one per role):
    - Font family dropdown (from discovered catalog)
    - Font style dropdown (Regular, Light, SemiBold, etc.)
    - Font size scale slider (0.5 to 2.0)
    - Live preview text showing selected font
  - "Apply Fonts" button
  - "Reset to Theme Defaults" button
  
  **RenderStylingTab() Implementation:**
  - **Border Settings:**
    - Window border size slider (0-5px)
    - Frame border size slider (0-3px)
    - Child border size slider (0-3px)
    - Popup border size slider (0-3px)
  - **Spacing Settings:**
    - Window padding XY sliders (0-20px)
    - Frame padding XY sliders (0-20px)
    - Item spacing XY sliders (0-20px)
    - Item inner spacing XY sliders (0-20px)
  - **Rounding Settings:**
    - Window rounding slider (0-20px)
    - Frame rounding slider (0-12px)
    - Child rounding slider (0-12px)
    - Popup rounding slider (0-12px)
    - Scrollbar rounding slider (0-12px)
  - **Scrollbar Settings:**
    - Scrollbar size slider (10-30px)
    - Scrollbar opacity sliders:
      - Background opacity (0-1.0)
      - Thumb opacity (0-1.0)
      - Thumb hovered opacity (0-1.0)
      - Thumb active opacity (0-1.0)
  - **Global Settings:**
    - Global UI scale slider (-2.0 to +2.0 exponent)
    - Tooltip hover delay (0-5 seconds)
  - "Apply Styling" button
  
  **RenderColorsTab() Implementation:**
  - **Mode Toggle:**
    - "Simple Palette" mode (6 key colors)
    - "Advanced Palette" mode (all ImGui colors)
    - "Full Palette" mode (raw 55-color array editing)
  - **Simple Palette Editor:**
    - Background color picker
    - Text color picker
    - Window border color picker
    - Frame border color picker
    - Separator color picker
    - Resize grip color picker
  - **Status Palette Editor:**
    - Disable color
    - Error color
    - Warning color
    - RestartNeeded color
    - CurrentHotkey color
    - SuccessColor
    - InfoColor
  - **Feature Heading Editor:**
    - ColorDefault
    - ColorHovered
    - MinimizedFactor slider
  - **Advanced Palette Editor:**
    - All 55 ImGui colors with pickers
  - **Contrast Indicators:**
    - Show contrast ratio for text vs background
    - Warn if contrast too low (accessibility)
  - "Apply Colors" button
  - "Reset to Theme Defaults" button

#### UI Helper Functions (2 files)
- ✅ `src/Utils/UI.h` - **ADD:**
  - ColorUtils namespace:
    - `float CalculateLuminance(const ImVec4& color)`
    - `float CalculateContrast(const ImVec4& color1, const ImVec4& color2)`
    - `ImVec4 GetContrastAwareTextColor(const ImVec4& bgColor)`
  - `bool ContrastAwareSelectable(const char* label, bool selected, const ImVec4& bgColor, ImGuiSelectableFlags flags = 0)`
  - `bool RestoreToDefaultButton(const char* id, const char* tooltip = "Restore to Default")`

- ✅ `src/Utils/UI.cpp` - **ADD (~150 lines):**
  - ColorUtils::CalculateLuminance() implementation (relative luminance formula)
  - ColorUtils::CalculateContrast() implementation (WCAG contrast ratio)
  - ColorUtils::GetContrastAwareTextColor() implementation (returns white/black based on contrast)
  - ContrastAwareSelectable() implementation (adjusts text color for readability)
  - RestoreToDefaultButton() implementation (small icon button with hover tooltip)

#### Menu State Updates (2 files)
- ✅ `src/Menu.h` - **UPDATE:**
  - Add ScrollbarOpacity struct in ThemeSettings (if not already in PR #1)
  - Add any UI state flags for new tabs (e.g., selectedColorMode, showAdvancedPalette)

- ✅ `src/Menu.cpp` - **UPDATE:**
  - Integrate new settings tabs into main render loop
  - Connect SaveTheme() to "Save Current Theme" button
  - Connect LoadThemePreset() to theme selector
  - Add validation for theme name input

### What This PR Achieves
- ✅ Complete settings UI reorganization (old "UI Options" → new "Themes/Fonts/Styling/Colors")
- ✅ **Themes Tab:** Select, create, save, delete themes with preview
- ✅ **Fonts Tab:** Per-role font configuration with live previews
- ✅ **Styling Tab:** All spacing, rounding, border, scrollbar controls in one place
- ✅ **Colors Tab:** Simple/advanced/full palette editing modes
- ✅ Contrast-aware UI elements for accessibility
- ✅ Restore-to-default buttons for easy reset
- ✅ **Fully functional theme and font customization UI**
- ✅ User can now select and customize everything introduced in PR #1 and #2

### Testing Checklist
- [ ] Themes tab loads all discovered themes
- [ ] Theme selection applies theme correctly
- [ ] Theme creation prompts for name and saves correctly
- [ ] Theme deletion works with confirmation dialog
- [ ] Theme preview shows colors before applying
- [ ] Fonts tab displays all font families and styles
- [ ] Font selection updates live preview text
- [ ] Per-role font configuration works independently
- [ ] Styling controls modify ImGui style correctly
- [ ] Scrollbar opacity controls work
- [ ] Colors tab palette editing works in all 3 modes
- [ ] Contrast indicators show correct values
- [ ] Restore-to-default buttons reset to theme defaults
- [ ] All settings persist across restarts

---

## PR #4: UI Candy - Background Blur, Flashing Buttons, Visual Enhancements

**Branch Name:** `feature/ui-visual-enhancements`  
**Title:** `feat(ui): Add background blur, flashing buttons, compact toggles, and visual polish`  
**Estimated Lines:** ~1,400 + 2 HLSL shaders  
**Goal:** Add visual polish and eye-candy features for modern UI feel

### Files to Include

#### Background Blur Shaders (2 files)
- ✅ `src/Features/Menu/Shaders/GaussianBlur_Horizontal.hlsl` - **COMPLETE (113 lines)**
  - Horizontal Gaussian blur pass
  - Configurable sample count (default 13)
  - Sub-pixel jitter for quality
  - Weight normalization
  - Full documentation in shader comments

- ✅ `src/Features/Menu/Shaders/GaussianBlur_Vertical.hlsl` - **COMPLETE (121 lines)**
  - Vertical Gaussian blur pass
  - Matches horizontal pass parameters
  - Gamma correction
  - Full documentation in shader comments

#### Background Blur System (2 files)
- ✅ `src/Menu/ThemeManager.h` - **ADD:**
  
  **Blur Shader Static Members:**
  ```cpp
  static inline winrt::com_ptr<ID3D11VertexShader> blurVertexShader;
  static inline winrt::com_ptr<ID3D11PixelShader> blurHorizontalPixelShader;
  static inline winrt::com_ptr<ID3D11PixelShader> blurVerticalPixelShader;
  static inline winrt::com_ptr<ID3D11Buffer> blurConstantBuffer;
  static inline winrt::com_ptr<ID3D11Buffer> blurVertexBuffer;
  static inline winrt::com_ptr<ID3D11Buffer> blurIndexBuffer;
  static inline winrt::com_ptr<ID3D11Texture2D> blurTexture1;
  static inline winrt::com_ptr<ID3D11Texture2D> blurTexture2;
  static inline winrt::com_ptr<ID3D11ShaderResourceView> blurSRV1;
  static inline winrt::com_ptr<ID3D11ShaderResourceView> blurSRV2;
  static inline winrt::com_ptr<ID3D11RenderTargetView> blurRTV1;
  static inline winrt::com_ptr<ID3D11RenderTargetView> blurRTV2;
  static inline winrt::com_ptr<ID3D11BlendState> blurBlendState;
  static inline winrt::com_ptr<ID3D11SamplerState> blurSamplerState;
  static inline float currentBlurIntensity = 0.0f;
  ```
  
  **Blur Function Declarations:**
  - `bool InitializeBlurShaders()`
  - `void CleanupBlurResources()`
  - `void RenderBackgroundBlur(ID3D11RenderTargetView* targetRTV, const ImVec2& menuPos, const ImVec2& menuSize, float intensity)`
  - `void RecreateBlurTextures(uint32_t width, uint32_t height)`

- ✅ `src/Menu/ThemeManager.cpp` - **ADD (~600 lines):**
  
  **InitializeBlurShaders() Implementation:**
  - Compile vertex shader from embedded HLSL
  - Compile horizontal blur pixel shader
  - Compile vertical blur pixel shader
  - Create constant buffer for blur parameters
  - Create vertex/index buffers for fullscreen triangle
  - Create blend state for alpha compositing
  - Create sampler state for linear filtering
  - Error handling for all D3D11 resource creation
  
  **CompileBlurShader() Helper:**
  - Read shader source from file
  - Compile HLSL to bytecode
  - Return compiled shader blob
  
  **RenderBackgroundBlur() Implementation (~250 lines):**
  - **Validation:**
    - Check if blur enabled in theme
    - Check if shaders initialized
    - Check if blur textures exist
  - **Capture Source:**
    - Copy current render target to blur source texture
  - **First Pass (Horizontal Blur):**
    - Set blur texture 1 as render target
    - Bind horizontal pixel shader
    - Set constant buffer with horizontal parameters
    - Draw fullscreen triangle
  - **Second Pass (Vertical Blur):**
    - Set blur texture 2 as render target
    - Bind vertical pixel shader
    - Set constant buffer with vertical parameters
    - Draw fullscreen triangle
  - **Final Composition:**
    - Restore original render target
    - Set up scissor rect for menu area only
    - Create rasterizer state with scissor enabled
    - Set blend state for proper alpha compositing
    - Bind final blurred texture
    - Draw fullscreen triangle with menu scissor rect
  - **State Restoration:**
    - Restore original rasterizer state
    - Restore original blend state
    - Clear shader resources
  
  **RecreateBlurTextures() Implementation:**
  - Release old textures if they exist
  - Create new textures matching viewport size
  - Create shader resource views
  - Create render target views
  
  **CleanupBlurResources() Implementation:**
  - Release all blur shaders
  - Release all blur textures and views
  - Release constant/vertex/index buffers
  - Release blend and sampler states
  - Reset currentBlurIntensity

#### Blur Integration (2 files)
- ✅ `src/Menu.cpp` - **UPDATE:**
  - Call `ThemeManager::InitializeBlurShaders()` in Init()
  - Call `ThemeManager::RenderBackgroundBlur(targetRTV, menuPos, menuSize, settings.BackgroundBlur)` before `ImGui::Render()`
  - Call `ThemeManager::CleanupBlurResources()` in destructor
  - Handle blur intensity changes from theme settings

- ✅ `src/Menu/OverlayRenderer.cpp` - **UPDATE:**
  - Render blur for overlay if enabled in theme
  - Use same RenderBackgroundBlur() call with overlay rect

#### Flashing Button System (2 files)
- ✅ `src/Utils/UI.h` - **ADD:**
  ```cpp
  /**
   * Renders a button with optional flashing animation
   * @param label Button label
   * @param shouldFlash If true, button will flash for flashDuration seconds
   * @param flashDuration How long to flash (default 2.0s)
   * @return True if button clicked
   */
  bool ButtonWithFlash(const char* label, bool shouldFlash = false, float flashDuration = 2.0f);
  ```

- ✅ `src/Utils/UI.cpp` - **ADD (~80 lines):**
  - Static map to track flash state per button ID
  - Flash timer and animation logic
  - Color interpolation for flash effect (lerp between normal and highlight color)
  - Automatic flash expiration after duration
  - ImGui button rendering with animated colors

#### Compact Feature Toggle (2 files)
- ✅ `src/Utils/UI.h` - **ADD:**
  ```cpp
  /**
   * Renders a compact on/off toggle switch
   * @param label Toggle label
   * @param value Pointer to bool value
   * @return True if value changed
   */
  bool FeatureToggle(const char* label, bool* value);
  ```

- ✅ `src/Utils/UI.cpp` - **ADD (~50 lines):**
  - Compact toggle switch rendering (like modern iOS/Android toggles)
  - Smooth animation between on/off states
  - Distinct visual feedback (color change, slide animation)
  - Smaller footprint than ImGui::Checkbox

#### Feature UI Updates (2 files)
- ✅ `src/Menu/FeatureListRenderer.cpp` - **UPDATE:**
  - Replace `ImGui::Checkbox()` with `Util::FeatureToggle()` for feature enable/disable
  - Use `Util::ButtonWithFlash()` for action buttons (e.g., "Apply Settings")
  - Apply compact layouts where appropriate

- ✅ `src/FeatureIssues.cpp` - **UPDATE:**
  - Use `ImGui::GetStyle().FrameBorderSize` for button borders (consistency with theme)
  - Remove hardcoded border values

### Known Issues to Fix (from CodeRabbit AI Review)
- ⚠️ **CRITICAL:** `GaussianBlur_Vertical.hlsl` has duplicate `cbuffer BlurBuffer` declaration (lines 33-47) - **MUST FIX**
- ⚠️ **MAJOR:** Final composition pass in RenderBackgroundBlur applies extra blur - set `sampleCount=1` for pass-through
- ⚠️ **MAJOR:** InitializeBlurShaders() uses static local flags that survive CleanupBlurResources() - move to file-scope or derive from ComPtr state

### What This PR Achieves
- ✅ GPU-accelerated background blur behind menu (two-pass separable Gaussian blur)
- ✅ Blur intensity configurable per theme (0.0-1.0)
- ✅ Smooth, performance-friendly blur implementation (~1-2ms at 1080p)
- ✅ Flashing buttons for visual feedback on important actions
- ✅ Compact feature toggles (modern on/off switches)
- ✅ Polished, modern UI feel
- ✅ All visual enhancements work with existing themes

### Testing Checklist
- [ ] Build compiles without errors
- [ ] Blur shaders compile successfully (check for HLSL errors)
- [ ] Background blur renders correctly behind menu
- [ ] Blur intensity adjustable via theme settings (0.0 = no blur, 1.0 = full blur)
- [ ] Blur performance acceptable (< 2ms at 1080p)
- [ ] Blur doesn't cause rendering artifacts or crashes
- [ ] Flashing buttons animate smoothly
- [ ] Flash animation expires after duration
- [ ] Feature toggles render correctly
- [ ] Feature toggles respond to clicks
- [ ] Compact toggles save space vs old checkboxes

---

## PR #5: Performance Overlay & Miscellaneous Updates

**Branch Name:** `feature/performance-overlay-improvements`  
**Title:** `feat(ui): Improve performance overlay with always-visible sections and inline A/B tests`  
**Estimated Lines:** ~600  
**Goal:** Performance overlay UX improvements and remaining small changes

### Files to Include

#### Performance Overlay Changes (2 files)
- ✅ `src/Features/PerformanceOverlay.h` - **UPDATE:**
  - Remove `collapsibleSections` flag (no longer needed)
  - Update DrawABTestSection() signature (remove collapsing parameter)
  - Update function signatures as needed

- ✅ `src/Features/PerformanceOverlay.cpp` - **MAJOR REWRITE (~240 lines changed):**
  
  **Key Changes:**
  - Remove collapsing behavior entirely
  - Always show all sections (no ImGui::CollapsingHeader)
  - Inline A/B test settings display:
    - Show "Settings A" and "Settings B" side-by-side
    - Display current values inline (no separate window)
  - Inline settings diff view:
    - Show differences between A and B inline
    - Color-code changed values (red = different, green = same)
  - Improved section layout:
    - Better spacing and alignment
    - Clearer section headers
  - Font role usage:
    - Use FontRoleScope(FontRole::Heading) for section headers
    - Use FontRoleScope(FontRole::Body) for values

#### Upscaling Updates (5 files)
Minor font-related or cleanup changes:

- ✅ `src/Features/Upscaling.cpp` - **Minor changes** (if any)
- ✅ `src/Features/Upscaling/FidelityFX.h` - **Minor changes** (if any)
- ✅ `src/Features/Upscaling/FidelityFX.cpp` - **Minor changes** (if any)
- ✅ `src/Features/Upscaling/Streamline.h` - **Minor changes** (if any)
- ✅ `src/Features/Upscaling/Streamline.cpp` - **Minor changes** (if any)

#### Shader Updates (1 file)
- ✅ `package/Shaders/Lighting.hlsl` - **Single line fix** (check git diff for exact change)

### What This PR Achieves
- ✅ Improved performance overlay UX
- ✅ Always-visible sections (no more collapsing)
- ✅ Inline A/B test settings display (easier comparison)
- ✅ Inline settings diff view (color-coded differences)
- ✅ Better layout and spacing
- ✅ Font roles applied throughout
- ✅ Any remaining small polish items or bug fixes

### Testing Checklist
- [ ] Performance overlay displays correctly
- [ ] All sections always visible (no collapsing)
- [ ] A/B test settings display inline
- [ ] Settings diff shows correct differences
- [ ] Color coding works (red for differences, green for same)
- [ ] Layout is clean and readable
- [ ] Font roles apply correctly

---

## Cross-PR Concerns & Best Practices

### Rebase Strategy
After each PR merges to `dev`, subsequent PRs MUST be rebased:

```bash
# After PR #1 merges
git checkout feature/font-system-and-assets
git rebase origin/dev
git push --force-with-lease

# After PR #2 merges
git checkout feature/settings-ui-overhaul
git rebase origin/dev
git push --force-with-lease

# And so on...
```

### Conflict Resolution
Expected conflicts and how to resolve:

1. **Menu.h/Menu.cpp conflicts:** Always take "both" changes, as each PR adds non-overlapping members
2. **ThemeManager.h/cpp conflicts:** PR #1 adds JSON system, PR #4 adds blur system - merge both sections
3. **UI.h/cpp conflicts:** Each PR adds different helpers - merge all

### Testing Between PRs
**CRITICAL:** After each PR merges, test the combined state:

```bash
# After PR #1
./BuildRelease.bat ALL
# Test: Themes load, JSON parsing works

# After PR #2
./BuildRelease.bat ALL
# Test: Fonts load, role system works, themes + fonts together

# After PR #3
./BuildRelease.bat ALL
# Test: Full UI works, theme/font selection, all settings

# After PR #4
./BuildRelease.bat ALL
# Test: Blur works, buttons flash, everything together

# After PR #5
./BuildRelease.bat ALL
# Test: Complete feature set, no regressions
```

### Code Review Focus Areas

**PR #1 Review:**
- PathHelpers correctness
- Theme JSON schema validation
- No UI changes yet (pure infrastructure)

**PR #2 Review:**
- Font discovery security (path traversal protection)
- Font atlas rebuilding safety (no crashes)
- Font fallback logic

**PR #3 Review:**
- UI usability and layout
- Contrast calculations accuracy
- Settings persistence

**PR #4 Review:**
- Blur shader correctness (HLSL)
- Blur performance (GPU profiling)
- D3D11 resource management (no leaks)

**PR #5 Review:**
- Performance overlay clarity
- Layout improvements

---

## Summary Statistics

### Overall Split
| Metric | Original PR | Split Total | Difference |
|--------|-------------|-------------|------------|
| Total Lines Changed | ~6,800 | ~6,600 | -200 (cleanup) |
| Source Files | 28 | 28 | 0 |
| Asset Files | 84 | 84 | 0 |
| Pull Requests | 1 | 5 | +4 |
| Avg Lines Per PR | ~6,800 | ~1,320 | -80% |

### PR Breakdown
| PR # | Title | Lines | Files | Complexity |
|------|-------|-------|-------|------------|
| 1 | Infrastructure | ~1,200 | 21 | Medium |
| 2 | Font System | ~1,800 | 87 | High |
| 3 | Settings UI | ~1,600 | 9 | High |
| 4 | Visual Polish | ~1,400 | 11 | Medium |
| 5 | Perf Overlay | ~600 | 8 | Low |

### Review Time Estimates
| PR # | Estimated Review Time | Complexity |
|------|-----------------------|------------|
| 1 | 30-45 minutes | Infrastructure setup, JSON parsing |
| 2 | 60-90 minutes | Font system logic, security validation |
| 3 | 45-60 minutes | UI layout and usability |
| 4 | 30-45 minutes | Shader code, D3D11 resource management |
| 5 | 15-30 minutes | Straightforward UI improvements |
| **Total** | **3-4.5 hours** | vs. original **8+ hours** |

---

## Potential Risks & Mitigations

### Risk: Merge Conflicts
**Likelihood:** High  
**Impact:** Medium  
**Mitigation:** 
- Rebase each PR after previous merges
- Use clear commit messages
- Test combined state after each merge

### Risk: Font Loading Crashes
**Likelihood:** Low  
**Impact:** High  
**Mitigation:**
- PR #2 includes comprehensive error handling
- Emergency fallback to default font
- Font validation before loading

### Risk: Blur Performance
**Likelihood:** Medium  
**Impact:** Medium  
**Mitigation:**
- Blur is optional (per theme)
- Two-pass separable algorithm (optimal)
- Texture size matches viewport (not full screen)

### Risk: Settings Not Persisting
**Likelihood:** Low  
**Impact:** Medium  
**Mitigation:**
- PR #1 establishes save/load infrastructure
- JSON schema validation
- Comprehensive testing in PR #3

---

## Credits & Attribution

**Original Work:** @davo0411  
**PR Analysis:** GitHub Copilot  
**Code Review:** CodeRabbit AI  
**Split Plan:** GitHub Copilot  

**Special Thanks:**
- Christian Ofenberg (Unrimp) for Gaussian blur shader reference (MIT License)
- Font authors for SIL Open Font License fonts
- Community Shaders team for original codebase

---

## Appendix: File Change Matrix

| File | PR #1 | PR #2 | PR #3 | PR #4 | PR #5 |
|------|-------|-------|-------|-------|-------|
| `src/Menu/ThemeManager.h` | ✅ JSON | ✅ Font | - | ✅ Blur | - |
| `src/Menu/ThemeManager.cpp` | ✅ JSON | ✅ Font | - | ✅ Blur | - |
| `src/Menu.h` | ✅ Basic | ✅ Font | ✅ UI | - | - |
| `src/Menu.cpp` | ✅ Basic | ✅ Font | ✅ UI | ✅ Blur | - |
| `src/Utils/FileSystem.h` | ✅ Path | - | - | - | - |
| `src/Utils/FileSystem.cpp` | ✅ Path | - | - | - | - |
| `src/Utils/UI.h` | - | ✅ Font | ✅ Color | ✅ Flash | - |
| `src/Utils/UI.cpp` | - | ✅ Font | ✅ Color | ✅ Flash | - |
| `src/Menu/Fonts.h` | - | ✅ Full | - | - | - |
| `src/Menu/Fonts.cpp` | - | ✅ Full | - | - | - |
| `src/Menu/SettingsTabRenderer.h` | - | - | ✅ Full | - | - |
| `src/Menu/SettingsTabRenderer.cpp` | - | - | ✅ Full | - | - |
| `src/Menu/FeatureListRenderer.cpp` | - | ✅ Font | - | ✅ Toggle | - |
| `src/Menu/MenuHeaderRenderer.cpp` | - | ✅ Font | - | - | - |
| `src/Menu/HomePageRenderer.cpp` | - | ✅ Font | - | - | - |
| `src/Menu/OverlayRenderer.cpp` | - | ✅ Font | - | ✅ Blur | - |
| `src/Features/VR.cpp` | - | ✅ Font | - | - | - |
| `src/Features/PerformanceOverlay.h` | - | - | - | - | ✅ Full |
| `src/Features/PerformanceOverlay.cpp` | - | - | - | - | ✅ Full |
| `src/FeatureIssues.cpp` | - | - | - | ✅ Border | - |
| Themes/*.json (9 files) | ✅ All | - | - | - | - |
| Fonts/*.ttf (60+ files) | - | ✅ All | - | - | - |
| Shaders/GaussianBlur*.hlsl (2) | - | - | - | ✅ Both | - |

---

## Final Notes

This split plan has been carefully designed to:

1. **Minimize review burden** - Each PR is digestible in under 90 minutes
2. **Preserve functionality** - Every feature works exactly as designed in original PR
3. **Enable incremental testing** - Each PR is independently testable
4. **Respect dependencies** - Clear merge order prevents integration issues
5. **Facilitate rollback** - If issues found, can revert individual PRs

**Recommended merge timeline:** 1-2 PRs per week, allowing time for thorough review and testing between merges.

**Questions?** Ping @davo0411 or @doodlum

---

**Document Version:** 1.0  
**Last Updated:** October 13, 2025  
**Status:** Ready for Implementation

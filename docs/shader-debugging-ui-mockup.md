# Shader Debugging UI Mockup

This document describes the visual layout of the new Shader Debugging section.

## Location

The Shader Debugging section appears in the Advanced Settings tab, between "Replace Original Shaders" and the Developer Testing section. It is only visible when Developer Mode is enabled (log level set to debug or trace).

## Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Shader Debugging  ▼                                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  [When shader is blocked - Orange header:]                              │
│  Shader Blocking Active                                                 │
│  ──────────────────────────────────────────────────────────────────────│
│  Blocked Shader:                                                        │
│    Lighting_Pixel_Lighting_0x12345678_DEFERRED=1                       │
│  Descriptors Blocked: 3                                                 │
│  Type: Lighting                                                         │
│  Class: Pixel                                                           │
│  Descriptor: 0x12345678                                                 │
│  Cache Path: Data/ShaderCache/Lighting/12345678.pso                    │
│                                                                          │
│  [ Stop Blocking ]                                                      │
│  ──────────────────────────────────────────────────────────────────────│
│                                                                          │
│  Active Shaders (Used Recently)                        [?]              │
│  Total Active: 42                                                       │
│                                                                          │
│  Filter: [____________________]  [?]                                    │
│  Sort By: [Key ▼]                                                       │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Type      │ Class │ Descriptor │ Draw Calls │ Key                │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │ Lighting  │ P     │ 0x12345678 │ 156        │ [Block] Light...   │  │
│  │ Effect    │ P     │ 0xABCDEF01 │ 89         │ [Block] Effec...   │  │
│  │ Water     │ V     │ 0x98765432 │ 45         │ [Block] Water...   │  │
│  │ Lighting  │ C     │ 0x11111111 │ 23         │ [Block] Light...   │  │
│  │ ...                                                                │  │
│  │                                                                    │  │
│  │  (Scrollable - 300px height)                                      │  │
│  │                                                                    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Tip: Use PAGEUP/PAGEDOWN keys to quickly cycle through active         │
│  shaders. Blocked shaders will use vanilla rendering instead of        │
│  Community Shaders.                                                     │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Interactive Elements

### 1. Blocked Shader Section (appears only when blocking is active)

-   **Stop Blocking Button**: Clears the current blocking state
-   **Orange Text**: Indicates blocking is active
-   **Auto-populated Details**: Pulled from active shader info when available

### 2. Filter Input

-   **Text Input**: Type to filter shader keys
-   **Case-sensitive**: Exact substring matching
-   **Live Update**: Table updates as you type
-   **Tooltip**: "Filter shaders by key substring (case-sensitive)"

### 3. Sort Dropdown

-   **Options**:
    -   Key (alphabetical)
    -   Draw Calls (descending)
    -   Type (grouped by shader type)
-   **Default**: Key
-   **Persistent**: Selection maintained during session

### 4. Active Shaders Table

-   **Fixed Header**: Column headers stay visible when scrolling
-   **Resizable Columns**: Drag column separators to adjust width
-   **Row Highlighting**:
    -   Blocked shader: Orange text
    -   Hover: Standard ImGui hover color
-   **Tooltips**: Hover over any row's Key column for full details:
    ```
    Type: Lighting
    Class: Pixel
    Descriptor: 0x12345678
    Draw Calls: 156
    Key: Lighting_Pixel_Lighting_0x12345678_DEFERRED=1
    Cache Path: Data/ShaderCache/Lighting/12345678.pso
    ```

### 5. Block/Unblock Buttons

-   **Per-shader**: Each row has its own button
-   **Dynamic Label**: Shows "Unblock" for currently blocked shader
-   **Immediate Action**: Clicking applies instantly
-   **Visual Feedback**: Blocked shader row turns orange after clicking

### 6. Keyboard Shortcuts

-   **PAGEUP**: Cycle to next shader in active list (forward)
-   **PAGEDOWN**: Cycle to previous shader in active list (backward)
-   **ESC**: Close menu (does not clear blocking)

## Visual Style

-   **Colors**:

    -   Blocked shader indicator: Orange (1.0, 0.5, 0.0, 1.0)
    -   Regular text: Default ImGui text color
    -   Headers: Default ImGui header color
    -   Borders: Default ImGui border color

-   **Fonts**: Uses standard Community Shaders UI font

-   **Spacing**: Follows ImGui default spacing with manual separators

## Empty State

When no shaders are active (e.g., in main menu):

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Shader Debugging  ▼                                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Active Shaders (Used Recently)                        [?]              │
│  Total Active: 0                                                        │
│                                                                          │
│  Filter: [____________________]  [?]                                    │
│  Sort By: [Key ▼]                                                       │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Type      │ Class │ Descriptor │ Draw Calls │ Key                │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │                                                                    │  │
│  │                     (No active shaders)                            │  │
│  │                                                                    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Tip: Active shaders will appear here when you are in-game and         │
│  rendering. PAGEUP/PAGEDOWN will fall back to cycling through all      │
│  cached shaders.                                                        │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Responsive Behavior

-   **Column Widths**:

    -   Type: 80px fixed
    -   Class: 60px fixed
    -   Descriptor: 80px fixed
    -   Draw Calls: 80px fixed
    -   Key: Stretches to fill remaining space

-   **Scrolling**: Vertical scroll bar appears when > ~10 shaders

-   **Filtering**: Table shrinks to show only matching results

-   **Sorting**: Entire table re-orders without scrolling

## Performance Considerations

-   **Render Cost**: Minimal - only visible in developer mode
-   **Update Frequency**: Once per frame for draw call counters
-   **Memory**: ~100 bytes per active shader (typically 10-50 shaders)
-   **Sorting**: O(n log n) where n is typically < 100

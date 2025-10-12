# Shader Debugging Guide

## Overview

The Shader Debugging feature provides tools for developers to identify and debug problematic shaders in Community Shaders. This feature is only available when Developer Mode is enabled (log level set to trace or debug).

## Features

### 1. Active Shader Tracking

The system automatically tracks shaders that are used in recent frames:
- **Draw Call Statistics**: Number of draw calls per shader per frame
- **Activity Tracking**: Shaders used within the last ~1 second are tracked
- **Automatic Cleanup**: Inactive shaders are removed from the tracking list

### 2. Shader Blocking

Block specific shaders to compare Community Shaders rendering with vanilla:
- **Manual Blocking**: Click "Block" next to any shader in the Active Shaders list
- **Keyboard Navigation**: Use PAGEUP/PAGEDOWN to cycle through active shaders
- **Visual Feedback**: Blocked shaders are highlighted in orange in the UI

When a shader is blocked:
- Community Shaders version is disabled for that shader
- Vanilla Skyrim rendering is used instead
- All matching descriptors for that shader are blocked
- Information is logged to the console

### 3. UI Features

#### Shader Debugging Section (Advanced Settings)

Located in the Advanced settings menu when Developer Mode is enabled:

**Blocked Shader Information:**
- Shader key and type
- Class (Vertex/Pixel/Compute)
- Descriptor hex value
- Cache file path
- Number of descriptors blocked

**Active Shaders Table:**
- **Filter**: Search for specific shaders by key substring
- **Sort By**: Sort by Key, Draw Calls, or Type
- **Columns**:
  - Type: Shader type (Lighting, Effect, Water, etc.)
  - Class: V (Vertex), P (Pixel), C (Compute)
  - Descriptor: Hex descriptor value
  - Draw Calls: Number of draw calls this frame
  - Key: Full shader identifier
- **Actions**: Block/Unblock buttons per shader
- **Tooltips**: Hover over shader key for full details including cache path

## Usage Examples

### Debugging Visual Artifacts

1. Enable Developer Mode (set log level to "debug" or "trace")
2. Navigate to Advanced > Shader Debugging
3. Look for shaders with high draw call counts in the Active Shaders table
4. Click "Block" on suspected shaders to compare with vanilla rendering
5. If the artifact disappears, you've found the problematic shader

### Finding Specific Shader Issues

1. Use the Filter field to search for specific shader types or keywords
2. Sort by Draw Calls to find the most frequently used shaders
3. Use PAGEUP/PAGEDOWN to quickly cycle through shaders
4. Check the log file for detailed blocking information

### Comparing Performance

1. Note the draw call counts for different shader types
2. Block high-impact shaders temporarily
3. Observe performance differences
4. Use this information to identify optimization opportunities

## Technical Details

### Shader Key Format

Shader keys follow this format:
```
{Type}_{Class}_{FXPFilename}_{Descriptor}_{Defines}
```

Example:
```
Lighting_Pixel_Lighting_0x12345678_DEFERRED=1
```

### Cache Paths

Blocked shader information includes the disk cache path:
```
Data/ShaderCache/{FXPFilename}/{Descriptor}.pso
```

### Frame Tracking

- Shader activity is reset at the start of each frame
- Draw call counters are accumulated throughout the frame
- Shaders not used for 1 second are removed from the active list
- Tracking only occurs when Developer Mode is enabled

## Keyboard Shortcuts

- **PAGEUP**: Block next shader in active list
- **PAGEDOWN**: Block previous shader in active list
- **ESC**: Close menu (does not unblock shaders)

## Tips

1. **Use Filtering**: With thousands of potential shaders, filtering by type or keyword helps narrow down issues
2. **Sort by Draw Calls**: High draw call shaders have more impact and are good candidates for investigation
3. **Check the Log**: Blocked shaders are logged with their full details for reference
4. **Active Shaders Only**: The system now prioritizes recently-used shaders, making PAGEUP/PAGEDOWN more practical
5. **One at a Time**: Block one shader at a time to isolate the exact source of issues

## Known Limitations

- Shader tracking adds minimal overhead but is only enabled in Developer Mode
- Only shaders loaded during the current session are tracked
- Disk-cached shaders may need a cache clear to be fully recompiled after unblocking
- Compute shaders require a game restart to fully reload after cache changes

## Related Settings

- **Log Level**: Must be "debug" or "trace" to enable Developer Mode
- **Frame Annotations**: Provides additional performance tracking when enabled
- **Dump Shaders**: Outputs shader source for detailed analysis
- **File Watcher**: Auto-recompiles shaders when HLSL files change during development

# Community Shaders - Hot-Swappable Theme System

This directory contains JSON theme files that can be hot-swapped at runtime without requiring code changes or restarts.

## How It Works

The theme system automatically discovers `.json` files in this directory and makes them available in the Community Shaders menu. Simply:

1. Create or edit a `.json` theme file in this directory
2. Click "Refresh Themes" in the Colors tab of the Community Shaders menu
3. Select your theme from the dropdown

## Theme File Format

Theme files use JSON format and should follow this structure:

```json
{
	"DisplayName": "My Custom Theme",
	"Description": "A beautiful custom theme",
	"Version": "1.0.0",
	"Author": "Your Name",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.1, 0.1, 0.1, 0.95],
			"Text": [1.0, 1.0, 1.0, 1.0],
			"Border": [0.5, 0.5, 0.5, 0.8]
		}
	}
}
```

### Required Fields

- `Theme`: The main theme object containing all visual settings
- `Theme.UseSimplePalette`: Set to `true` for simple 3-color themes, `false` for full ImGui color palette control

### Optional Metadata

- `DisplayName`: Human-readable name shown in the dropdown (defaults to filename)
- `Description`: Brief description shown in the UI
- `Version`: Theme version number
- `Author`: Theme creator name

### Color Format

Colors are specified as arrays of 4 floating-point values: `[red, green, blue, alpha]`
- Values range from 0.0 to 1.0
- Alpha (transparency) typically ranges from 0.8 to 1.0 for UI elements

## Simple vs Full Palette

### Simple Palette (`UseSimplePalette: true`)

Uses only 3 colors for a clean, consistent look:
- `Background`: Main UI background color
- `Text`: Primary text color
- `Border`: Border and accent color

### Full Palette (`UseSimplePalette: false`)

Allows complete control over all ImGui colors. See existing theme files for examples of the full color array structure.

## Tips for Creating Themes

1. **Start Simple**: Begin with `UseSimplePalette: true` and the 3-color system
2. **Test Contrast**: Ensure good readability between text and background colors
3. **Consider Alpha**: Use appropriate transparency for backgrounds (0.9-0.95 recommended)
4. **Backup Originals**: Keep copies of default themes before modifying
5. **Use Descriptive Names**: Choose clear, descriptive filenames and display names

## Example Themes Included

- **Default**: Classic dark theme
- **Light**: Clean light mode for daytime use
- **Ocean**: Cool blue oceanic tones
- **Forest**: Natural green forest theme
- **Mystic**: Purple magical theme
- **Amber**: Warm candlelight theme
- **HighContrast**: Accessibility-focused high contrast
- **DragonBlood**: Dark red dragon-inspired theme
- **NordicFrost**: Cool Nordic blue-white theme
- **DwemerBronze**: Ancient bronze Dwemer technology theme

## Hot-Swapping Features

- **Runtime Discovery**: New themes are discovered immediately with "Refresh Themes"
- **No Restart Required**: Themes apply instantly when selected
- **Live Editing**: Edit theme files and refresh to see changes immediately
- **Fallback Safety**: Invalid themes are safely ignored
- **File Size Limits**: Theme files are limited to 1MB for performance
- **Error Handling**: Malformed JSON files are logged but don't crash the system

## Sharing Themes

Theme files are completely portable and can be shared between users. Simply copy `.json` files to this directory and refresh to make them available.

## Technical Notes

- Theme discovery is performed on-demand for performance
- Files are validated for basic JSON structure and required fields
- Theme loading uses the same robust error handling as the settings override system
- Maximum of 100 theme files can be loaded (prevent performance issues)
- File modification times are tracked for change detection
# VR Menu Feature

The VR Menu feature allows you to display the Community Shaders menu directly in your VR headset as a SteamVR overlay, eliminating the need to take off your headset to configure settings.

## Features

- **Native VR Integration**: Uses SteamVR's overlay system for seamless integration
- **Full Menu Functionality**: All menu features work in VR just like on desktop
- **Configurable Appearance**: Adjust size, position, and visual quality
- **Auto-show/Hide**: Automatically shows/hides with the menu toggle key
- **Dashboard Access**: Optional dashboard integration for easy access

## Requirements

- Skyrim VR
- SteamVR running
- Community Shaders mod installed

## Setup

1. **Enable VR Menu**: In the Community Shaders menu, go to General → Interface → VR Menu section
2. **Check "Enable VR Menu Overlay"** to activate the feature
3. **Configure Settings**:
   - **Show in Dashboard**: Makes the menu accessible from SteamVR dashboard
   - **Auto-show on Toggle**: Automatically shows VR overlay when menu toggle key is pressed
   - **Overlay Size**: Adjust width, height, and distance (in meters)
   - **Visual Options**: Enable curved overlay and high-quality rendering
   - **Position**: Fine-tune the overlay position relative to center

## Usage

### Basic Operation
- Press your menu toggle key (default: Insert) to show/hide the VR menu overlay
- The overlay will appear in front of you in VR space
- Use VR controllers or mouse to interact with the menu
- All menu functionality works the same as desktop version

### Accessing from Dashboard
If "Show in Dashboard" is enabled:
1. Open SteamVR dashboard
2. Look for "Community Shaders VR Menu" in the applications list
3. Click to open the menu overlay

### Positioning
- **Distance**: How far the menu appears from your face (0.5-3.0 meters)
- **Size**: Width and height of the overlay in VR space
- **Position Offset**: Fine-tune X/Y position relative to center
- **Curved Overlay**: Applies curvature for more immersive experience

## Troubleshooting

### Menu Not Appearing
1. Ensure SteamVR is running
2. Check that "Enable VR Menu Overlay" is checked in settings
3. Verify you're in Skyrim VR (not regular Skyrim)
4. Check the CommunityShaders.log for error messages

### Performance Issues
1. Disable "High Quality Rendering" if experiencing frame drops
2. Reduce overlay size (width/height) for better performance
3. Increase distance to reduce rendering load

### Input Issues
1. Ensure "Show in Dashboard" is enabled for controller input
2. Try using mouse input if controller input isn't working
3. Check SteamVR controller settings

## Technical Details

The VR Menu feature:
- Creates a separate ImGui context for VR rendering
- Uses D3D11 render targets to capture menu content
- Submits textures to SteamVR overlay system
- Handles VR-specific input events
- Maintains separate state from desktop menu

## Configuration

Settings are saved in the Community Shaders configuration and persist between game sessions. The VR menu settings are stored alongside other menu preferences.

## Support

If you encounter issues with the VR menu:
1. Check the CommunityShaders.log for detailed error messages
2. Ensure SteamVR is up to date
3. Try disabling and re-enabling the VR menu feature
4. Report issues on the Community Shaders GitHub page with log files attached 
# Detect if Windows Graphics Tools are installed
# Graphics Tools include the D3D12 Debug Layer (d3d12SDKLayers.dll) required
# for D3D12 debugging and development

if(WIN32)
    message(STATUS "Checking for Windows Graphics Tools...")

    # Get Windows directory (works on any drive)
    # Try multiple environment variables for maximum compatibility
    if(DEFINED ENV{SystemRoot})
        set(WINDOWS_DIR "$ENV{SystemRoot}")
    elseif(DEFINED ENV{WINDIR})
        set(WINDOWS_DIR "$ENV{WINDIR}")
    else()
        # Fallback to C: drive if environment variables not set
        set(WINDOWS_DIR "C:/Windows")
    endif()

    # Check for D3D12 SDK Layers DLL (primary indicator)
    set(D3D12_SDK_LAYERS_DLL_64 "${WINDOWS_DIR}/System32/d3d12SDKLayers.dll")

    if(EXISTS "${D3D12_SDK_LAYERS_DLL_64}")
        message(STATUS "Graphics Tools detected: ${D3D12_SDK_LAYERS_DLL_64}")
        set(GRAPHICS_TOOLS_INSTALLED TRUE CACHE BOOL "Windows Graphics Tools are installed")
    else()
        message(WARNING "Graphics Tools NOT detected!")
        message(WARNING "")
        message(WARNING "The D3D12 Debug Layer DLL was not found at:")
        message(WARNING "  ${D3D12_SDK_LAYERS_DLL_64}")
        message(WARNING "")
        message(WARNING "This will cause shader tests (ShaderTestFramework) to fail with:")
        message(WARNING "  DXGI_ERROR_SDK_COMPONENT_MISSING (0x887A002D)")
        message(WARNING "")
        message(WARNING "TO FIX:")
        message(WARNING "  1. Open Windows Settings -> Apps -> Optional Features")
        message(WARNING "  2. Click 'Add a feature'")
        message(WARNING "  3. Search for 'Graphics Tools'")
        message(WARNING "  4. Install it")
        message(WARNING "  5. Reboot your computer")
        message(WARNING "  6. Re-run CMake")
        message(WARNING "")
        message(WARNING "Or run this PowerShell command as Administrator:")
        message(WARNING "  Enable-WindowsOptionalFeature -Online -FeatureName GraphicsTools -All")
        message(WARNING "")

        set(GRAPHICS_TOOLS_INSTALLED FALSE CACHE BOOL "Windows Graphics Tools are NOT installed")

        # Optional: Automatically open the Optional Features dialog
        option(AUTO_OPEN_OPTIONAL_FEATURES "Automatically open Windows Optional Features dialog if Graphics Tools missing" OFF)

        if(AUTO_OPEN_OPTIONAL_FEATURES)
            message(STATUS "Opening Windows Optional Features dialog...")
            # ms-settings: URI scheme to open Windows Settings
            execute_process(
                COMMAND cmd /c start ms-settings:optionalfeatures
                ERROR_QUIET
            )
            message(STATUS "Please install 'Graphics Tools' from the dialog and reboot.")
        else()
            message(STATUS "")
            message(STATUS "Tip: Add -DAUTO_OPEN_OPTIONAL_FEATURES=ON to automatically open the")
            message(STATUS "     Windows Optional Features dialog next time.")
        endif()
    endif()

    # Export for use in other CMake files
    set(GRAPHICS_TOOLS_INSTALLED ${GRAPHICS_TOOLS_INSTALLED} PARENT_SCOPE)

else()
    # Non-Windows platforms don't need Graphics Tools
    set(GRAPHICS_TOOLS_INSTALLED TRUE CACHE BOOL "Graphics Tools check not needed on non-Windows")
    message(STATUS "Graphics Tools check skipped (non-Windows platform)")
endif()

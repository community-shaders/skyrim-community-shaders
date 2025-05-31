#pragma once

#include <d3d11.h>
#include <imgui.h>

// Forward declarations
class Menu;

namespace UIIconLoader
{
    // Load a texture from a file path
    bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size);
    
    // Initialize the icons for the Menu class
    bool InitializeMenuIcons(Menu* menu);
}

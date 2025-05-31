// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
// https://github.com/microsoft/fluentui-system-icons

#include "UIIconLoader.h"
#include "Globals.h"
#include "Menu.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace UIIconLoader
{bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size)
    {
        // Load from disk into a raw RGBA buffer
        int image_width = 0;
        int image_height = 0;
        int channels_in_file;
        unsigned char* image_data = stbi_load(filename, &image_width, &image_height, &channels_in_file, 4);
        if (image_data == NULL)
            return false;

        // Create texture
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = image_width;
        desc.Height = image_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D* pTexture = NULL;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = image_data;
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        globals::d3d::device->CreateTexture2D(&desc, &subResource, &pTexture);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        globals::d3d::device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
        pTexture->Release();
        
        out_size = ImVec2((float)image_width, (float)image_height);
        stbi_image_free(image_data);

        return true;
    }    bool InitializeMenuIcons(Menu* menu)
    {
        if (!menu) {
            return false;
        }
        
        bool success = true;
          // Define path to icons
        std::string basePath = "Data\\Interface\\CommunityShaders\\Icons\\";
        
    // Load all required icons
        success &= LoadTextureFromFile((basePath + "save-settings.png").c_str(), &menu->uiIcons.saveSettings.texture, menu->uiIcons.saveSettings.size);
        success &= LoadTextureFromFile((basePath + "load-settings.png").c_str(), &menu->uiIcons.loadSettings.texture, menu->uiIcons.loadSettings.size);
        success &= LoadTextureFromFile((basePath + "clear-cache.png").c_str(), &menu->uiIcons.clearCache.texture, menu->uiIcons.clearCache.size);
        success &= LoadTextureFromFile((basePath + "clear-disk.png").c_str(), &menu->uiIcons.clearDiskCache.texture, menu->uiIcons.clearDiskCache.size);
        success &= LoadTextureFromFile((basePath + "cs-logo.png").c_str(), &menu->uiIcons.logo.texture, menu->uiIcons.logo.size);
        
        return success;
    }
}

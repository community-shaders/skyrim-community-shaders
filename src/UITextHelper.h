#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace UITextHelper
{
    // Helper function to render sharper text by ensuring pixel-perfect alignment
    inline void RenderSharpText(const char* text, bool alignToPixelGrid = true, float scale = 1.0f)
    {

        if (alignToPixelGrid)
        {
            // Get current position
            ImVec2 pos = ImGui::GetCursorPos();
            
            // Align to pixel grid for sharper rendering
            pos.x = std::round(pos.x);
            pos.y = std::round(pos.y);
            
            // Set aligned position
            ImGui::SetCursorPos(pos);
        }

        // Apply scale if needed
        if (scale != 1.0f)
            ImGui::SetWindowFontScale(scale);
            
        // Use Text instead of TextUnformatted for better rendering
        ImGui::Text("%s", text);
        
        // Restore scale if needed
        if (scale != 1.0f)
            ImGui::SetWindowFontScale(1.0f);
    }
    
    // Helper function to render aligned text and logo
    inline void RenderAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale = 1.5f)
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
        
        // Render logo
        ImGui::Image(logoTexture, logoSize);
        ImGui::SameLine();
        
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
    }
}

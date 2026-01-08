#include "HiZOcclusion.h"

#include "Features/Upscaling.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#include <RE/N/NiBound.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    HiZOcclusion::Settings,
    enableHiZViewer,
    hizViewerMip,
    hizViewerScale,
    enableHiZCulling,
    conservativeBias,
    showCullingStats,
    debugMode,
    enableBoundsViewer,
    boundsMaxObjects,
    showVisTestPassed,
    showVisInsideBounds,
    showVisInvalidRadius,
    showCulledFrustum,
    showCulledNoEarlyOut
)

HiZOcclusion::~HiZOcclusion()
{
    // Release Hi-Z pyramid resources
    if (hiZSRV) { hiZSRV->Release(); hiZSRV = nullptr; }
    for (auto* v : hiZSRVsPerMip) { if (v) v->Release(); }
    hiZSRVsPerMip.clear();
    for (auto* u : hiZUAVs) { if (u) u->Release(); }
    hiZUAVs.clear();
    if (hiZTexture) { hiZTexture->Release(); hiZTexture = nullptr; }
    
    // Release compute shaders
    if (hiZBuildLevel0CS) { hiZBuildLevel0CS->Release(); hiZBuildLevel0CS = nullptr; }
    if (hiZDownsampleCS) { hiZDownsampleCS->Release(); hiZDownsampleCS = nullptr; }
    if (hiZTestCS) { hiZTestCS->Release(); hiZTestCS = nullptr; }
    
    // Release GPU culling resources
    if (geometryBoundsSRV) { geometryBoundsSRV->Release(); geometryBoundsSRV = nullptr; }
    if (geometryBoundsBuffer) { geometryBoundsBuffer->Release(); geometryBoundsBuffer = nullptr; }
    if (visibilityResultsUAV) { visibilityResultsUAV->Release(); visibilityResultsUAV = nullptr; }
    if (visibilityResultsBuffer) { visibilityResultsBuffer->Release(); visibilityResultsBuffer = nullptr; }
    if (hiZTestParamsBuffer) { hiZTestParamsBuffer->Release(); hiZTestParamsBuffer = nullptr; }
    if (hiZSampler) { hiZSampler->Release(); hiZSampler = nullptr; }
    
    // Release async readback staging buffers
    for (int i = 0; i < AsyncReadbackState::BUFFER_COUNT; ++i) {
        if (readbackState.stagingBuffers[i]) {
            readbackState.stagingBuffers[i]->Release();
            readbackState.stagingBuffers[i] = nullptr;
        }
        if (readbackState.completionQueries[i]) {
            readbackState.completionQueries[i]->Release();
            readbackState.completionQueries[i] = nullptr;
        }
    }
    
    // Release debug buffers
    ReleaseDebugBuffer();
    
    // Release bounds overlay resources
    ReleaseBoundsOverlayResources();
    
    // Release readback buffers
    if (debugReadbackBuffer) { debugReadbackBuffer->Release(); debugReadbackBuffer = nullptr; }
    if (visibilityReadbackBuffer) { visibilityReadbackBuffer->Release(); visibilityReadbackBuffer = nullptr; }

    // Release GPU timestamp queries
    for (uint32_t i = 0; i < GPU_TIMING_BUFFER_COUNT; ++i) {
        if (gpuTimingQueries[i].disjointQuery) { gpuTimingQueries[i].disjointQuery->Release(); gpuTimingQueries[i].disjointQuery = nullptr; }
        if (gpuTimingQueries[i].beginTimestamp) { gpuTimingQueries[i].beginTimestamp->Release(); gpuTimingQueries[i].beginTimestamp = nullptr; }
        if (gpuTimingQueries[i].endTimestamp) { gpuTimingQueries[i].endTimestamp->Release(); gpuTimingQueries[i].endTimestamp = nullptr; }
        gpuTimingQueries[i].pending = false;
    }
}

bool HiZOcclusion::SetupBoundsOverlayResources(uint32_t width, uint32_t height) {
    auto device = globals::d3d::device;
    if (!device || width == 0 || height == 0) return false;

    // Release old
    ReleaseBoundsOverlayResources();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device->CreateTexture2D(&td, nullptr, &boundsOverlayTex))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srd{};
    srd.Format = td.Format;
    srd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srd.Texture2D.MostDetailedMip = 0;
    srd.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(boundsOverlayTex, &srd, &boundsOverlaySRV))) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format = td.Format;
    uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavd.Texture2D.MipSlice = 0;
    if (FAILED(device->CreateUnorderedAccessView(boundsOverlayTex, &uavd, &boundsOverlayUAV))) return false;

    boundsOverlayW = width;
    boundsOverlayH = height;
    return true;
}

void HiZOcclusion::ReleaseBoundsOverlayResources() {
    if (boundsOverlayUAV) { boundsOverlayUAV->Release(); boundsOverlayUAV = nullptr; }
    if (boundsOverlaySRV) { boundsOverlaySRV->Release(); boundsOverlaySRV = nullptr; }
    if (boundsOverlayTex) { boundsOverlayTex->Release(); boundsOverlayTex = nullptr; }
    boundsOverlayW = boundsOverlayH = 0;
}

void HiZOcclusion::ClearBoundsOverlay() {
    if (!boundsOverlayUAV) return;
    auto ctx = globals::d3d::context;
    static const float clearColor[4] = {0.f,0.f,0.f,0.f};
    ctx->ClearUnorderedAccessViewFloat(boundsOverlayUAV, clearColor);
}

void HiZOcclusion::DrawSettings()
{
    if (ImGui::TreeNodeEx("Hi-Z Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show viewer window", &settings.enableHiZViewer);
        if (auto _tt = Util::HoverTooltipWrapper()) {
            Util::DrawMultiLineTooltip({
                "Displays the selected Hi-Z mip level in a separate window.",
                "Useful to verify pyramid contents and downsampling correctness."
            });
        }

        // Clamp mip selection to available range when resources exist
        uint32_t maxMip = hiZMipCount > 0 ? (hiZMipCount - 1) : 0;
        if (settings.hizViewerMip > maxMip) settings.hizViewerMip = maxMip;

        ImGui::SliderInt("Mip", reinterpret_cast<int*>(&settings.hizViewerMip), 0, static_cast<int>(maxMip));
        if (auto _tt = Util::HoverTooltipWrapper()) {
            Util::DrawMultiLineTooltip({
                "Mip 0 is full resolution. Higher mips are progressively smaller.",
                "Range is based on the currently built Hi-Z mip count."
            });
        }
        ImGui::SliderFloat("Scale", &settings.hizViewerScale, 0.1f, 4.0f, "%.2fx");
        if (auto _tt = Util::HoverTooltipWrapper()) {
            Util::DrawMultiLineTooltip({
                "Visual scale applied to the displayed texture.",
                "Use to enlarge small mip levels."
            });
        }

        if (ImGui::TreeNodeEx("Bounds Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Bounds Overlay", &settings.enableBoundsViewer);
            if (auto _tt = Util::HoverTooltipWrapper()) {
                Util::DrawMultiLineTooltip({
                    "Shows colored outlines around tested geometry based on their culling status.",
                    "Enable individual colors below to filter what is displayed."
                });
            }
            
            ImGui::SliderInt("Max Objects", reinterpret_cast<int*>(&settings.boundsMaxObjects), 8, 2048);
            if (auto _tt = Util::HoverTooltipWrapper()) {
                Util::DrawMultiLineTooltip({
                    "Maximum number of objects to draw outlines for per frame.",
                    "Higher values may impact performance."
                });
            }
            
            if (settings.enableBoundsViewer) {
                ImGui::Separator();
                ImGui::Text("Color Filters:");
                
                ImGui::Checkbox("Green (Visible: Test Passed)", &settings.showVisTestPassed);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), ": %u", stats.visTestPassed);
                if (auto _tt = Util::HoverTooltipWrapper()) {
                    ImGui::SetTooltip("Geometry that passed a valid depth test");
                }
                
                ImGui::Checkbox("Teal (Visible: Camera Inside Bounds)", &settings.showVisInsideBounds);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.f, 1.f, 0.5f, 1.f), ": %u", stats.visInsideBounds);
                if (auto _tt = Util::HoverTooltipWrapper()) {
                    ImGui::SetTooltip("Geometry whose bounds clip the camera");
                }
                
                ImGui::Checkbox("Cyan (Visible: Invalid Radius)", &settings.showVisInvalidRadius);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.f, 1.f, 1.f, 1.f), ": %u", stats.visInvalidRadius);
                if (auto _tt = Util::HoverTooltipWrapper()) {
                    ImGui::SetTooltip("Geometry with a radius of 0 or less");
                }
                
                ImGui::Checkbox("Magenta (Culled: Frustum)", &settings.showCulledFrustum);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.f, 1.f, 1.f), ": %u", stats.culledFrustum);
                if (auto _tt = Util::HoverTooltipWrapper()) {
                    ImGui::SetTooltip("Geometry with no valid boundary points on screen");
                }
                
                ImGui::Checkbox("Red (Culled: No Early Out)", &settings.showCulledNoEarlyOut);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), ": %u", stats.culledNoEarlyOut);
                if (auto _tt = Util::HoverTooltipWrapper()) {
                    ImGui::SetTooltip("Geometry that failed all tests and were culled");
                }
            }
            ImGui::TreePop();
        }

        // Status line
        ImGui::Separator();
        ImGui::Text("Frame: %u", globals::state ? globals::state->frameCount : 0);
		ImGui::Text("Status: %s", HiZStatusToString(status));
		if (!statusMessage.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  > %s", statusMessage.c_str());
		}
        ImGui::Text("Geometry from frame %u: %u", globals::state->frameCount - 1, stats.geometryListSize);
        ImGui::Text("Total tested: %u", stats.totalTested);
        ImGui::Text("Culled: %u", stats.culledFrustum + stats.culledNoEarlyOut);
        ImGui::Text("Visible: %u", stats.visTestPassed + stats.visInsideBounds + stats.visInvalidRadius);
        ImGui::Text("Unknown/Default: %u", stats.defaultValue);
        
        // Async readback status
        ImGui::Text("Occluded set size: %zu", occludedGeometry.size());
        if (stats.staleFrameCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Stale results: %u frames (from frame %u)", 
                              stats.staleFrameCount, stats.lastResultFrame);
        } else {
            ImGui::Text("Results: Fresh (frame %u)", stats.lastResultFrame);
        }
        
        // Display profiling durations in micro seconds
        ImGui::Text("GPU time: %.2f us", stats.gpuCullingTimeMs * 1000);
        ImGui::Text("Copy results time: %.2f us", stats.copyTimeMs * 1000);
        ImGui::Text("Map time: %.2f us", stats.mapTimeMs * 1000);
        ImGui::Text("Copy data time: %.2f us", stats.copyDataTimeMs * 1000);
        ImGui::Text("Unmap time: %.2f us", stats.unmapTimeMs * 1000);
        ImGui::Text("CPU Readback time: %.2f us", stats.readbackTimeMs * 1000);

        ImGui::TreePop();
    }
    
    // Hi-Z Culling Settings
    if (ImGui::TreeNodeEx("Hi-Z Culling", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Hi-Z Culling", &settings.enableHiZCulling);
        ImGui::SameLine();
        if (auto _tt = Util::HoverTooltipWrapper()) {
            ImGui::SetTooltip("Enable Hi-Z Culling system");
        }
        ImGui::SliderFloat("Conservative Bias", &settings.conservativeBias, 0.0f, 1.0f, "%.4f");
        ImGui::SameLine();
        if (auto _tt = Util::HoverTooltipWrapper()) {
            ImGui::SetTooltip("Conservative bias for Hi-Z Culling. \nHigher values are more conservative, lower values are more aggressive.");
        }
        ImGui::TreePop();
    }

    // Separate viewer window (persists while settings are open)
    if (settings.enableHiZViewer) {
        if (ImGui::Begin("Hi-Z Pyramid Viewer", &settings.enableHiZViewer)) {
            if (hiZTexture) {
                // Compute selected mip dimensions
                uint32_t mip = settings.hizViewerMip;
                uint32_t w = std::max(1u, hiZWidth  >> mip);
                uint32_t h = std::max(1u, hiZHeight >> mip);
                ImVec2 size = ImVec2(w * settings.hizViewerScale, h * settings.hizViewerScale);

                // Draw the texture
                ImGui::Text("Mip %u  (%ux%u)", mip, w, h);
                if (mip < hiZSRVsPerMip.size() && hiZSRVsPerMip[mip]) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(hiZSRVsPerMip[mip]), size);
                    
                    // Debug info
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("SRV: %p", hiZSRVsPerMip[mip]);
                        ImGui::Text("Scale: %.2fx", settings.hizViewerScale);
                        ImGui::Text("Display size: %.0fx%.0f", size.x, size.y);
                        ImGui::EndTooltip();
                    }
                } else {
                    ImGui::Text("SRV not available (mip=%u, size=%zu)", mip, hiZSRVsPerMip.size());
                    if (mip < hiZSRVsPerMip.size()) {
                        ImGui::Text("SRV pointer for mip %u: %p", mip, hiZSRVsPerMip[mip]);
                    }
                    ImGui::Text("Texture: %p, Main SRV: %p", hiZTexture, hiZSRV);
                }
            }
            else {
                ImGui::Text("Hi-Z pyramid unavailable.");
            }
        }
        ImGui::End();
    }
}

void HiZOcclusion::DrawOverlay()
{
    // Draw bounds overlay full-screen when enabled
    // This is called every frame regardless of menu state
    if (settings.enableBoundsViewer && boundsOverlaySRV) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 p0 = vp->Pos;
        ImVec2 p1 = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    
        // Draw overlay image full-screen
        // Tint alpha controls opacity (e.g., 0x80 = 50%)
        ImU32 tint = IM_COL32(255, 255, 255, 192);
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(boundsOverlaySRV),
            p0, p1, ImVec2(0, 0), ImVec2(1, 1), tint);
    
        // Optional: red border around whole screen
        ImGui::GetBackgroundDrawList()->AddRect(p0, p1, IM_COL32(255, 0, 0, 255));
    }
}

bool HiZOcclusion::IsOverlayVisible() const
{
    // Overlay is visible when bounds viewer is enabled and resource exists
    return settings.enableBoundsViewer && boundsOverlaySRV != nullptr;
}

void HiZOcclusion::LoadSettings(json& o_json)
{
    settings = o_json;
}

void HiZOcclusion::SaveSettings(json& o_json)
{
    o_json = settings;
}

void HiZOcclusion::RestoreDefaultSettings()
{
    settings = {};
}

// Preserve Feature base-class contract
void HiZOcclusion::SetupResources()
{
    InitShaders();
}

void HiZOcclusion::InitShaders()
{  
    // Ensure we have a valid device before attempting shader compilation
    auto device = globals::d3d::device;
    if (!device) {
        status = HiZStatus::Error;
        statusMessage = "No D3D device available";
        logger::error("{}", statusMessage);
        return;
    }
    
    status = HiZStatus::CompilingShaders;
    statusMessage.clear();
    
    // Build defines for VR support
    std::vector<std::pair<const char*, const char*>> shaderDefines;
    if (REL::Module::IsVR()) {
        shaderDefines.push_back({ "VR", nullptr });
        shaderDefines.push_back({ "FRAMEBUFFER", nullptr });
    }
    
    // Compile Hi-Z build shaders with error handling
    if (!hiZBuildLevel0CS) {
        try {
            hiZBuildLevel0CS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\HiZOcclusion\\HiZBuildLevel0CS.hlsl", shaderDefines, "cs_5_0");
            if (!hiZBuildLevel0CS) { 
                status = HiZStatus::Error;
                statusMessage = "Failed to compile HiZBuildLevel0CS";
                logger::error("{}", statusMessage);
                return;
            }
        } catch (const std::exception& e) {
            status = HiZStatus::Error;
            statusMessage = std::string("HiZBuildLevel0CS compilation exception: ") + e.what();
            logger::error("{}", statusMessage);
            return;
        }
    }
    
    if (!hiZDownsampleCS) {
        try {
            hiZDownsampleCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\HiZOcclusion\\HiZDownsampleCS.hlsl", shaderDefines, "cs_5_0");
            if (!hiZDownsampleCS) { 
                status = HiZStatus::Error;
                statusMessage = "Failed to compile HiZDownsampleCS";
                logger::error("{}", statusMessage);
                return;
            }
        } catch (const std::exception& e) {
            status = HiZStatus::Error;
            statusMessage = std::string("HiZDownsampleCS compilation exception: ") + e.what();
            logger::error("{}", statusMessage);
            return;
        }
    }
    
    if (!hiZTestCS) {
        try {
            hiZTestCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\HiZOcclusion\\HiZTestCS.hlsl", shaderDefines, "cs_5_0");
            if (!hiZTestCS) { 
                status = HiZStatus::Error;
                statusMessage = "Failed to compile HiZTestCS";
                logger::error("{}", statusMessage);
                return;
            }
        } catch (const std::exception& e) {
            status = HiZStatus::Error;
            statusMessage = std::string("HiZTestCS compilation exception: ") + e.what();
            logger::error("{}", statusMessage);
            return;
        }
    }
    
    resourcesSetup = true;
    status = HiZStatus::ShadersReady;
    statusMessage.clear();
    
    // Skip resource validation for a few frames after shader compilation
    // to avoid crashes during device state transitions
    skipValidationThisFrame = true;
}

void HiZOcclusion::ClearShaderCache()
{
    if (hiZBuildLevel0CS) { hiZBuildLevel0CS->Release(); hiZBuildLevel0CS = nullptr; }
    if (hiZDownsampleCS) { hiZDownsampleCS->Release(); hiZDownsampleCS = nullptr; }
    if (hiZTestCS) { hiZTestCS->Release(); hiZTestCS = nullptr; }
}

void HiZOcclusion::Reset()
{
    if (!settings.enableHiZCulling) {
        if (wasEnabled) {
            // Uncull all hidden geometries
            if (!unCullNextFrame.empty()) {
                for (auto& geometry : unCullNextFrame) {
                    if (geometry) {
                        geometry->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
                    }
                }
                unCullNextFrame.clear();
            }
            // Empty pending geometry list (with mutex protection)
            {
                std::lock_guard<std::mutex> lock(pendingGeometryMutex);
                if (!pendingGeometry.empty()) {
                    pendingGeometry.clear();
                }
                pendingGeometrySet.clear();
            }
            // Release and clear all resources
            ReleaseBoundsOverlayResources();
            ReleaseDebugBuffer();
            UnbindD3DResources();
            // Reset stats
            stats.frameIndex = 0;
            stats.totalTested = 0;
            stats.geometryListSize = 0;
            stats.visTestPassed = 0;
            stats.visInsideBounds = 0;
            stats.visInvalidRadius = 0;
            stats.defaultValue = 0;
            stats.culledFrustum = 0;
            stats.culledNoEarlyOut = 0;
            stats.resourceSetupDurationMS = 0.0f;
            stats.recreateDurationMS = 0.0f;
            wasEnabled = false;
        }
    } else {
        if (!wasEnabled) {
            wasEnabled = true;
        }
    }
}

void HiZOcclusion::EarlyPrepass()
{
    if (settings.debugMode) {
        logger::debug("Frame {} EarlyPrepass - {} hidden geometries queued for re-test", 
                     globals::state->frameCount, unCullNextFrame.size());
    }
}

void HiZOcclusion::Prepass()
{

    if (!settings.enableHiZCulling) {
        return;
    }
    // Stats are reset each frame during active culling
    
    // Reset culling stats for new frame - always reset to avoid accumulation
    stats.frameIndex = currentFrame;
    stats.totalTested = 0;
    stats.resourceSetupDurationMS = 0.0f;
    stats.recreateDurationMS = 0.0f;
    stats.visTestPassed = 0;
    stats.visInsideBounds = 0;
    stats.visInvalidRadius = 0;
    stats.defaultValue = 0;
    stats.culledFrustum = 0;
    stats.culledNoEarlyOut = 0;

    if (settings.enableBoundsViewer && boundsOverlayUAV) {
        overlayUpdatedThisFrame = false;
    }

    if (!resourcesSetup) {
        auto start = std::chrono::high_resolution_clock::now();
        InitShaders();
        auto end = std::chrono::high_resolution_clock::now();
        const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();
        stats.resourceSetupDurationMS = static_cast<float>(durationMs);
    }
    
    // Early return if shader compilation failed
    if (!resourcesSetup) {
        status = HiZStatus::Error;
        statusMessage = "Shader compilation failed";
        return;
    }

    status = HiZStatus::ResourcesReady;
    statusMessage.clear();

    if (!InitHiZResources()) {
        logger::error("HiZOcclusion::EarlyPrepass - failed to initialize Hi-Z resources");
        status = HiZStatus::Error;
        statusMessage = "Resource initialization failed";
        return;
    }
    
    // Setup GPU culling resources if not already done
    if (!geometryBoundsBuffer || !hiZTestParamsBuffer || !hiZSampler || !visibilityResultsBuffer) {
        if (!SetupGPUCullingResources()) {
            logger::error("HiZOcclusion::EarlyPrepass - failed to setup GPU culling resources");
            status = HiZStatus::Error;
            statusMessage = "GPU culling resource setup failed";
            return;
        }
    }

    // Lock mutex to safely access pendingGeometry and pendingGeometrySet
    // This prevents race conditions with BSBatchRenderer_RenderPassImmediately hook
    std::lock_guard<std::mutex> lock(pendingGeometryMutex);

    // Update geometry list size stat now that we have the lock
    stats.geometryListSize = static_cast<uint32_t>(pendingGeometry.size());

    // Reserve vector capacity
    if (pendingGeometry.capacity() < 16384) {
        pendingGeometry.reserve(16384);
        geometryBounds.reserve(16384);
    }

    if (!unCullNextFrame.empty()) {
        // Re-add previously hidden geometry for continuous testing
        for (auto& geo : unCullNextFrame) {
            auto* rawGeo = geo.get();
            if (rawGeo && !pendingGeometrySet.contains(rawGeo)) {
                //geo->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
                pendingGeometry.push_back(geo);
                pendingGeometrySet.insert(rawGeo);
            }
        }
    }

    if (readbackState.numPendingReads > 0 || !pendingGeometry.empty()) {
        ExecuteVisibilityTests();
    }

    // Only process visibility tests if we have geometry from previous frame
    if (!pendingGeometry.empty()) {
        // Clean up resources that we are finished with
        pendingGeometry.clear();
        pendingGeometrySet.clear();
        geometryBounds.clear();
    } else {
        logger::debug("Frame {} - No pending geometry to process in Prepass", globals::state->frameCount);
    }

    if (settings.enableBoundsViewer && boundsOverlayUAV && !overlayUpdatedThisFrame) {
        ClearBoundsOverlay();
    }

    UnbindD3DResources();

    status = HiZStatus::Running;
    statusMessage.clear();
};

bool HiZOcclusion::InitHiZResources()
{
    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;

    // Ensure resources are ready
    if (!renderer) {
		logger::error("Renderer not ready");
		status = HiZStatus::Error;
		statusMessage = "Renderer not available";
        return false;
    }

    // Get previous frame depth dimensions
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    if (!depth.depthSRV) {
		logger::error("no depth texture SRV");
		status = HiZStatus::Error;
		statusMessage = "Depth texture SRV unavailable";
        return false;
    }
    
    // Log depth buffer properties
    D3D11_TEXTURE2D_DESC depthTexDesc{};
    depth.texture->GetDesc(&depthTexDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSRVDesc{};
    depth.depthSRV->GetDesc(&depthSRVDesc);

    // Check if depth format is compatible
	if (depthSRVDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && 
		depthSRVDesc.Format != DXGI_FORMAT_R32_FLOAT &&
		depthSRVDesc.Format != DXGI_FORMAT_R16_UNORM) {
		logger::warn("Unexpected depth format: {}", static_cast<int>(depthSRVDesc.Format));
	}

    D3D11_TEXTURE2D_DESC depthDesc{};
    depth.texture->GetDesc(&depthDesc);

	if (!depthDesc.Width || !depthDesc.Height) {
		logger::error("depth texture has invalid dimensions");
		status = HiZStatus::Error;
		statusMessage = "Invalid depth texture dimensions";
		return false;
	}

    uint32_t desiredW;
    uint32_t desiredH;

    if (globals::features::upscaling.loaded && !((Upscaling::UpscaleMethod)globals::features::upscaling.settings.upscaleMethod == Upscaling::UpscaleMethod::kNONE)) {
        uint32_t displayW = static_cast<uint32_t>(globals::state->screenSize.x);
        uint32_t displayH = static_cast<uint32_t>(globals::state->screenSize.y);
        desiredW = static_cast<uint32_t>(displayW * globals::features::upscaling.dynamicResolutionWidthRatio);
        desiredH = static_cast<uint32_t>(displayH * globals::features::upscaling.dynamicResolutionHeightRatio);
        // Ensure dimensions are at least 1
        desiredW = std::max(1u, desiredW);
        desiredH = std::max(1u, desiredH);
    } else {
        desiredW = depthDesc.Width;
        desiredH = depthDesc.Height;
    }

    // Build Hi-Z pyramid from previous frame depth buffer
    // Ensure Hi-Z texture exists and matches current depth dimensions
    auto device = globals::d3d::device;
    if (!device) {
        logger::error("no D3D device");
        status = HiZStatus::Error;
        statusMessage = "D3D device unavailable";
        return false;
    }

    // Detailed diagnostics for resource recreation
    const bool textureNull = (hiZTexture == nullptr);
    const bool widthMismatch = (hiZWidth != desiredW);
    const bool heightMismatch = (hiZHeight != desiredH);
    const bool needRecreate = textureNull || widthMismatch || heightMismatch;
    
    if (needRecreate) {
        auto startRecreateTimer = std::chrono::high_resolution_clock::now();

        // Compute mip count for the new texture
        uint32_t w = desiredW;
        uint32_t h = desiredH;
        uint32_t newMipCount = 1;
        while (w > 1 || h > 1) { w = std::max(1u, w >> 1); h = std::max(1u, h >> 1); ++newMipCount; }

        // Create new resources into temporaries
        ID3D11Texture2D* newTexture = nullptr;
        ID3D11ShaderResourceView* newSRV = nullptr;
        std::vector<ID3D11ShaderResourceView*> newSRVsPerMip;
        std::vector<ID3D11UnorderedAccessView*> newUAVs;
        newSRVsPerMip.reserve(newMipCount);
        newUAVs.reserve(newMipCount);

        D3D11_TEXTURE2D_DESC tdesc{};
        tdesc.Width = desiredW;
        tdesc.Height = desiredH;
        tdesc.MipLevels = newMipCount;
        tdesc.ArraySize = 1;
        tdesc.Format = DXGI_FORMAT_R32_FLOAT;
        tdesc.SampleDesc.Count = 1;
        tdesc.Usage = D3D11_USAGE_DEFAULT;
        tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        HRESULT hr = device->CreateTexture2D(&tdesc, nullptr, &newTexture);
        if (FAILED(hr) || !newTexture) {
            status = HiZStatus::Error;
            statusMessage = "Failed to create Hi-Z texture";
            logger::error("{}", statusMessage);
            if (newTexture) newTexture->Release();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sdesc{};
        sdesc.Format = DXGI_FORMAT_R32_FLOAT;
        sdesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sdesc.Texture2D.MostDetailedMip = 0;
        sdesc.Texture2D.MipLevels = newMipCount;
        HRESULT srvResult = device->CreateShaderResourceView(newTexture, &sdesc, &newSRV);
        if (FAILED(srvResult) || !newSRV) {
            status = HiZStatus::Error;
            statusMessage = "Failed to create Hi-Z SRV";
            logger::error("{}", statusMessage);
            if (newSRV) newSRV->Release();
            if (newTexture) newTexture->Release();
            return false;
        }

        bool perMipOk = true;
        for (uint32_t i = 0; i < newMipCount; ++i) {
            ID3D11ShaderResourceView* srvMip = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC sM{};
            sM.Format = DXGI_FORMAT_R32_FLOAT;
            sM.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sM.Texture2D.MostDetailedMip = i;
            sM.Texture2D.MipLevels = 1;
            HRESULT srvMipResult = device->CreateShaderResourceView(newTexture, &sM, &srvMip);
            if (FAILED(srvMipResult) || !srvMip) { perMipOk = false; }
            else { newSRVsPerMip.push_back(srvMip); }

            ID3D11UnorderedAccessView* uavMip = nullptr;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uM{};
            uM.Format = DXGI_FORMAT_R32_FLOAT;
            uM.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uM.Texture2D.MipSlice = i;
            HRESULT uavMipResult = device->CreateUnorderedAccessView(newTexture, &uM, &uavMip);
            if (FAILED(uavMipResult) || !uavMip) { perMipOk = false; }
            else { newUAVs.push_back(uavMip); }

            if (!perMipOk) break;
        }

        if (!perMipOk || newSRVsPerMip.size() != newMipCount || newUAVs.size() != newMipCount) {
            status = HiZStatus::Error;
            statusMessage = "Failed to create per-mip views";
            logger::error("{}", statusMessage);
            for (auto* v : newSRVsPerMip) { if (v) v->Release(); }
            for (auto* u : newUAVs) { if (u) u->Release(); }
            if (newSRV) newSRV->Release();
            if (newTexture) newTexture->Release();
            return false;
        }

        // Success: release old and swap in new resources
        if (hiZSRV) { hiZSRV->Release(); hiZSRV = nullptr; }
        for (auto* v : hiZSRVsPerMip) { if (v) v->Release(); }
        hiZSRVsPerMip.clear();
        for (auto* u : hiZUAVs) { if (u) u->Release(); }
        hiZUAVs.clear();
        if (hiZTexture) { hiZTexture->Release(); hiZTexture = nullptr; }

        hiZTexture = newTexture;
        hiZSRV = newSRV;
        hiZSRVsPerMip = std::move(newSRVsPerMip);
        hiZUAVs = std::move(newUAVs);
        hiZWidth = desiredW;
        hiZHeight = desiredH;
        hiZMipCount = newMipCount;
        resourceCreationFrame = globals::state->frameCount;
        resourcesValid = true;

        // Validate all SRVs are non-null
        // Note: D3D11 COM calls return HRESULTs, they don't throw C++ exceptions,
        // so we rely on null checks rather than try-catch blocks.
        bool allSRVsValid = true;
        for (uint32_t i = 0; i < hiZMipCount; ++i) {
            if (!hiZSRVsPerMip[i]) {
                logger::error("SRV for mip {} is null!", i);
                allSRVsValid = false;
            }
        }
        
        // If validation failed, mark resources as needing recreation
        if (!allSRVsValid) {
            logger::warn("SRV validation failed - resources may need recreation on next frame");
            status = HiZStatus::ValidationFailed;
            statusMessage = "SRV validation failed, will retry";
            resourcesValid = false;
            // Skip validation on next few frames to prevent repeated crashes
            skipValidationThisFrame = true;
        }
        auto endRecreateTimer = std::chrono::high_resolution_clock::now();
        const double recreateDuration = std::chrono::duration<double, std::milli>(endRecreateTimer - startRecreateTimer).count();
        stats.recreateDurationMS = static_cast<float>(recreateDuration);
    }

    // Build level 0 from depth with safety checks
    {
        // Verify all required resources are valid before proceeding
        if (!hiZBuildLevel0CS) {
            logger::error("hiZBuildLevel0CS is null - cannot build Hi-Z pyramid");
            status = HiZStatus::Error;
            statusMessage = "Build shader unavailable";
            return false;
        }
        
        if (hiZUAVs.empty() || !hiZUAVs[0]) {
            logger::error("hiZUAVs[0] is null - cannot build Hi-Z pyramid");
            status = HiZStatus::Error;
            statusMessage = "UAV[0] unavailable";
            return false;
        }
        
        ID3D11ShaderResourceView* srvs[1] = { depth.depthSRV };
        context->CSSetShaderResources(0, 1, srvs);
        ID3D11UnorderedAccessView* uavs[1] = { hiZUAVs[0] };
        context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        context->CSSetShader(hiZBuildLevel0CS, nullptr, 0);

		const uint32_t tgX = 16, tgY = 16;
		uint32_t groupsX = (hiZWidth + tgX - 1) / tgX;
		uint32_t groupsY = (hiZHeight + tgY - 1) / tgY;
		context->Dispatch(groupsX, groupsY, 1);

		// Unbind (must pass arrays, not raw nullptr, when Count > 0)
		ID3D11UnorderedAccessView* nullUAVs_lvl0[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs_lvl0, nullptr);
		ID3D11ShaderResourceView* nullSRVs_lvl0[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRVs_lvl0);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Downsample pyramid with max reduction
	uint32_t srcW = desiredW;
	uint32_t srcH = desiredH;
	for (uint32_t mip = 0; mip + 1 < hiZMipCount; ++mip) {
		uint32_t dstW = std::max(1u, srcW >> 1);
		uint32_t dstH = std::max(1u, srcH >> 1);

		// Safety checks for each mip level
		if (mip >= hiZSRVsPerMip.size() || !hiZSRVsPerMip[mip]) {
			logger::error("hiZSRVsPerMip[{}] is null - aborting pyramid build", mip);
			status = HiZStatus::Error;
			statusMessage = "Null SRV at mip " + std::to_string(mip);
			break;
		}
		
		if (mip + 1 >= hiZUAVs.size() || !hiZUAVs[mip + 1]) {
			logger::error("hiZUAVs[{}] is null - aborting pyramid build", mip + 1);
			status = HiZStatus::Error;
			statusMessage = "Null UAV at mip " + std::to_string(mip + 1);
			break;
		}
		
		if (!hiZDownsampleCS) {
			logger::error("hiZDownsampleCS is null - aborting pyramid build");
			status = HiZStatus::Error;
			statusMessage = "Downsample shader unavailable";
			break;
		}

		ID3D11ShaderResourceView* srvIn[1] = { hiZSRVsPerMip[mip] };
		context->CSSetShaderResources(0, 1, srvIn);
		ID3D11UnorderedAccessView* uavOut[1] = { hiZUAVs[mip + 1] };
		context->CSSetUnorderedAccessViews(0, 1, uavOut, nullptr);
		context->CSSetShader(hiZDownsampleCS, nullptr, 0);

		const uint32_t tgX = 16, tgY = 16;
		uint32_t groupsX = (dstW + tgX - 1) / tgX;
		uint32_t groupsY = (dstH + tgY - 1) / tgY;
		context->Dispatch(groupsX, groupsY, 1);

		// Unbind for next level (must pass arrays, not raw nullptr)
		ID3D11UnorderedAccessView* nullUAVs_ds[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs_ds, nullptr);
		ID3D11ShaderResourceView* nullSRVs_ds[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRVs_ds);
		context->CSSetShader(nullptr, nullptr, 0);

		srcW = dstW; srcH = dstH;
    } // End Hi-Z build timing

    return true;
}

bool HiZOcclusion::SetupGPUCullingResources()
{
    auto device = globals::d3d::device;
    if (!device) return false;
    
    // Create geometry bounds buffer (input)
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = maxGeometryCount * sizeof(DirectX::XMFLOAT4);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.StructureByteStride = sizeof(DirectX::XMFLOAT4);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, &geometryBoundsBuffer);
    if (FAILED(hr)) {
        logger::error("Failed to create geometry bounds buffer");
        return false;
    }
    
    // Create SRV for geometry bounds
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = maxGeometryCount;
    
    HRESULT srvCreateResult = device->CreateShaderResourceView(geometryBoundsBuffer, &srvDesc, &geometryBoundsSRV);
    if (FAILED(srvCreateResult)) {
        logger::error("Failed to create geometry bounds SRV");
        return false;
    }
    
    // Create visibility results buffer (output, batch) as float2 per element (objectDepth, sceneDepth)
    bufferDesc.ByteWidth = maxGeometryCount * sizeof(OcclusionResult);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.CPUAccessFlags = 0;  // No CPU access for UAV buffers
    bufferDesc.StructureByteStride = sizeof(OcclusionResult);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
    HRESULT visibilityBufferResult = device->CreateBuffer(&bufferDesc, nullptr, &visibilityResultsBuffer);
    if (FAILED(visibilityBufferResult)) {
        logger::error("Failed to create visibility results buffer");
        return false;
    }
    
    // Create UAV for visibility results (batch)
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = maxGeometryCount;
    
    HRESULT visibilityUAVResult = device->CreateUnorderedAccessView(visibilityResultsBuffer, &uavDesc, &visibilityResultsUAV);
    if (FAILED(visibilityUAVResult)) {
        logger::error("Failed to create visibility results UAV");
        return false;
    }
    
    // Create constant buffer for Hi-Z test parameters
    bufferDesc.ByteWidth = sizeof(HiZSettings);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.StructureByteStride = 0;
    bufferDesc.MiscFlags = 0;
    
    HRESULT paramsBufferResult = device->CreateBuffer(&bufferDesc, nullptr, &hiZTestParamsBuffer);
    if (FAILED(paramsBufferResult)) {
        logger::error("Failed to create Hi-Z test params buffer");
        return false;
    }
    
    // Create triple-buffered staging buffers for async readback
    D3D11_BUFFER_DESC readbackDesc = {};
    readbackDesc.ByteWidth = maxGeometryCount * sizeof(OcclusionResult);
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.BindFlags = 0;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    readbackDesc.StructureByteStride = 0;
    readbackDesc.MiscFlags = 0;

    for (int i = 0; i < AsyncReadbackState::BUFFER_COUNT; ++i) {
        HRESULT rbhr = device->CreateBuffer(&readbackDesc, nullptr, &readbackState.stagingBuffers[i]);
        if (FAILED(rbhr)) {
            logger::error("Failed to create visibility readback buffer {}", i);
            // Clean up any buffers we created
            for (int j = 0; j < i; ++j) {
                if (readbackState.stagingBuffers[j]) {
                    readbackState.stagingBuffers[j]->Release();
                    readbackState.stagingBuffers[j] = nullptr;
                }
                if (readbackState.completionQueries[j]) {
                    readbackState.completionQueries[j]->Release();
                    readbackState.completionQueries[j] = nullptr;
                }
            }
            return false;
        }
        
        // Create event query for precise GPU completion detection
        D3D11_QUERY_DESC queryDesc = {};
        queryDesc.Query = D3D11_QUERY_EVENT;
        queryDesc.MiscFlags = 0;
        HRESULT queryHr = device->CreateQuery(&queryDesc, &readbackState.completionQueries[i]);
        if (FAILED(queryHr)) {
            logger::warn("Failed to create completion query {} - falling back to polling", i);
            readbackState.completionQueries[i] = nullptr;
            // Non-fatal: will fall back to DO_NOT_WAIT polling if query creation fails
        }
        
        readbackState.hasPendingRead[i] = false;
        readbackState.pendingFrameIndex[i] = 0;
    }
    readbackState.writeIndex = 0;
    readbackState.readIndex = 0;
    readbackState.numPendingReads = 0;

    // Create sampler for Hi-Z sampling
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT sr = device->CreateSamplerState(&sampDesc, &hiZSampler);
    if (FAILED(sr)) {
        logger::error("Failed to create Hi-Z sampler");
        return false;
    }

    // Create GPU timestamp queries for accurate timing (triple-buffered)
    for (uint32_t i = 0; i < GPU_TIMING_BUFFER_COUNT; ++i) {
        D3D11_QUERY_DESC timestampDesc = {};
        timestampDesc.Query = D3D11_QUERY_TIMESTAMP;
        timestampDesc.MiscFlags = 0;

        D3D11_QUERY_DESC disjointDesc = {};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        disjointDesc.MiscFlags = 0;

        if (FAILED(device->CreateQuery(&disjointDesc, &gpuTimingQueries[i].disjointQuery)) ||
            FAILED(device->CreateQuery(&timestampDesc, &gpuTimingQueries[i].beginTimestamp)) ||
            FAILED(device->CreateQuery(&timestampDesc, &gpuTimingQueries[i].endTimestamp))) {
            logger::warn("Failed to create GPU timestamp queries for timing slot {}", i);
            // Non-fatal: fall back to CPU timing
        }
        gpuTimingQueries[i].pending = false;
    }
    gpuTimingWriteIndex = 0;
    gpuTimingReadIndex = 0;

    // Debug output buffers
    if (settings.debugMode || settings.enableBoundsViewer) {
        // Only create debug buffer when actually debugging
        if (!debugResultsBuffer) {
            CreateDebugBuffer();
        }
    } else {
        // Release debug buffer when not needed
        ReleaseDebugBuffer();
    }
    return true;
}

void HiZOcclusion::CreateDebugBuffer()
{
    auto device = globals::d3d::device;
    if (!device) return;
    
    const uint32_t debugElementCount = maxGeometryCount;
    D3D11_BUFFER_DESC dbgDesc = {};
    dbgDesc.ByteWidth = debugElementCount * sizeof(HiZOcclusion::DebugData);
    dbgDesc.Usage = D3D11_USAGE_DEFAULT;
    dbgDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    dbgDesc.CPUAccessFlags = 0;
    dbgDesc.StructureByteStride = sizeof(HiZOcclusion::DebugData);
    dbgDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
    HRESULT dbr = device->CreateBuffer(&dbgDesc, nullptr, &debugResultsBuffer);
    if (FAILED(dbr)) {
        logger::warn("Failed to create debugResultsBuffer");
        return;
    }
    
    D3D11_UNORDERED_ACCESS_VIEW_DESC dbgUavDesc = {};
    dbgUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    dbgUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    dbgUavDesc.Buffer.NumElements = debugElementCount;
    device->CreateUnorderedAccessView(debugResultsBuffer, &dbgUavDesc, &debugResultsUAV);
    
    // Create double-buffered staging buffers for debug readback
    D3D11_BUFFER_DESC dbgReadback = {};
    dbgReadback.ByteWidth = dbgDesc.ByteWidth;
    dbgReadback.Usage = D3D11_USAGE_STAGING;
    dbgReadback.BindFlags = 0;
    dbgReadback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    device->CreateBuffer(&dbgReadback, nullptr, &debugReadbackBuffer);
}

void HiZOcclusion::ReleaseDebugBuffer()
{
    if (debugResultsBuffer) {
        debugResultsBuffer->Release();
        debugResultsBuffer = nullptr;
    }
    if (debugResultsUAV) {
        debugResultsUAV->Release();
        debugResultsUAV = nullptr;
    }
    if (debugReadbackBuffer) {
        debugReadbackBuffer->Release();
        debugReadbackBuffer = nullptr;
    }
}

void HiZOcclusion::ExecuteVisibilityTests()
{
    auto context = globals::d3d::context;
    auto device = globals::d3d::device;
    if (!context || !device) {
        logger::warn("ExecuteVisibilityTests: D3D context or device not initialized");
        return;
    }
    
    // Track whether we got fresh results this frame
    bool gotNewResults = false;
    
    // Try to read results from any pending staging buffers (multi-buffered approach)
    if (readbackState.numPendingReads > 0) {
        auto readStart = std::chrono::high_resolution_clock::now();
        
        // Try to read from all pending buffers (oldest first)
        uint32_t attemptsToRead = readbackState.numPendingReads;
        for (uint32_t attempt = 0; attempt < attemptsToRead && readbackState.numPendingReads > 0; ++attempt) {
            uint32_t bufferIdx = readbackState.readIndex;
            
            if (readbackState.hasPendingRead[bufferIdx]) {
                // Check if GPU has completed using event query (if available)
                bool gpuComplete = false;
                
                if (readbackState.completionQueries[bufferIdx]) {
                    // Use query for precise completion detection
                    BOOL queryData = FALSE;
                    HRESULT queryHr = context->GetData(readbackState.completionQueries[bufferIdx], 
                                                        &queryData, sizeof(BOOL), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    gpuComplete = (queryHr == S_OK && queryData);
                    
                    if (!gpuComplete && settings.debugMode && attempt == 0) {
                        logger::debug("Frame {} - GPU work not yet complete for buffer {} (query)",
                                    globals::state->frameCount, bufferIdx);
                    }
                } else {
                    // Fallback to polling with DO_NOT_WAIT if query not available
                    gpuComplete = false;  // Will attempt Map() below
                }
                
                if (gpuComplete || !readbackState.completionQueries[bufferIdx]) {
                    auto mapStart = std::chrono::high_resolution_clock::now();
                    
                    UINT mapFlags = readbackState.completionQueries[bufferIdx] ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT;
                    HRESULT hr = context->Map(readbackState.stagingBuffers[bufferIdx], 0, 
                        D3D11_MAP_READ, mapFlags, 
                        &readbackState.mappedData[bufferIdx]);
                    
                    auto mapEnd = std::chrono::high_resolution_clock::now();
                    stats.mapTimeMs = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(mapEnd - mapStart).count());
                
                if (SUCCEEDED(hr)) {
                    auto copyStart = std::chrono::high_resolution_clock::now();
                    
                    // Successfully mapped - process results using the geometry snapshot from this buffer
                    ProcessVisibilityResults(bufferIdx);
                    gotNewResults = true;
                    
                    auto copyEnd = std::chrono::high_resolution_clock::now();
                    stats.copyDataTimeMs = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(copyEnd - copyStart).count());
                    
                    auto unmapStart = std::chrono::high_resolution_clock::now();
                    context->Unmap(readbackState.stagingBuffers[bufferIdx], 0);
                    auto unmapEnd = std::chrono::high_resolution_clock::now();
                    stats.unmapTimeMs = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(unmapEnd - unmapStart).count());
                    
                    // Mark buffer as free
                    readbackState.hasPendingRead[bufferIdx] = false;
                    readbackState.numPendingReads--;
                                        
                    // Advance read index for next frame
                    readbackState.readIndex = (readbackState.readIndex + 1) % AsyncReadbackState::BUFFER_COUNT;
                    break;  // Successfully processed one buffer, don't read more this frame
                } else {
                    // GPU not done yet - try next buffer in ring
                    readbackState.readIndex = (readbackState.readIndex + 1) % AsyncReadbackState::BUFFER_COUNT;
                    if (settings.debugMode && attempt == 0) {
                        logger::debug("Buffer {} not ready (frame {}), will retry next frame",
                                     bufferIdx, readbackState.pendingFrameIndex[bufferIdx]);
                    }
                }
            } else {
                // This buffer has no pending read, advance
                readbackState.readIndex = (readbackState.readIndex + 1) % AsyncReadbackState::BUFFER_COUNT;
            }
        }
        
        auto readEnd = std::chrono::high_resolution_clock::now();
        stats.readbackTimeMs = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(readEnd - readStart).count());
    }
    
    // Track stale frame count when no new results were obtained
    // The occludedGeometry set persists from the previous frame automatically
    if (!gotNewResults && stats.lastResultFrame > 0) {
        stats.staleFrameCount = globals::state->frameCount - stats.lastResultFrame;
        if (settings.debugMode && stats.staleFrameCount > 0) {
            logger::debug("Frame {} - Using cached occlusion results from frame {} ({} frames stale, {} geometry occluded)",
                         globals::state->frameCount, stats.lastResultFrame, stats.staleFrameCount, occludedGeometry.size());
        }
    }

    // Dispatch new test if we have geometry to process
    if (!pendingGeometry.empty()) {
        // Create the worldBound array, filtering out nullptrs and invalid radii
        geometryBounds.clear();
        pendingGeometrySnapshot.clear();
        geometryBounds.reserve(pendingGeometry.size());
        pendingGeometrySnapshot.reserve(pendingGeometry.size());
        
        const uint32_t cappedCount = std::min<uint32_t>(static_cast<uint32_t>(pendingGeometry.size()), maxGeometryCount);
        uint32_t processed = 0;
        
        for (auto& geometry : pendingGeometry) {
            if (!geometry || processed >= cappedCount) {
                continue;
            }
            
            auto& worldBound = geometry->worldBound;
            if (worldBound.radius <= 0.0f) {
                continue;
            }
            
            geometryBounds.emplace_back(worldBound.center.x, worldBound.center.y, worldBound.center.z, worldBound.radius);
            pendingGeometrySnapshot.push_back(geometry);
            ++processed;
        }
        
        numGeometry = processed;
        
        if (numGeometry == 0) {
            return;
        }
        
        logger::debug("ExecuteVisibilityTests: Processing {} geometry objects from frame {}", numGeometry, globals::state->frameCount - 1);

        // Execute HiZ Tests for this frame
        DispatchComputeShader();

        // Check if we have a free staging buffer
        if (readbackState.numPendingReads >= AsyncReadbackState::BUFFER_COUNT) {
            logger::warn("All {} staging buffers are full! GPU readback is severely delayed. Skipping oldest buffer.",
                        AsyncReadbackState::BUFFER_COUNT);
            // Force-free the oldest buffer (readIndex points to it)
            uint32_t oldestIdx = readbackState.readIndex;
            if (readbackState.hasPendingRead[oldestIdx]) {
                readbackState.hasPendingRead[oldestIdx] = false;
                readbackState.numPendingReads--;
                readbackState.readIndex = (readbackState.readIndex + 1) % AsyncReadbackState::BUFFER_COUNT;
            }
        }

        // Copy current frame results to next available staging buffer
        uint32_t writeIdx = readbackState.writeIndex;
        
        auto copyStart = std::chrono::high_resolution_clock::now();
        context->CopyResource(readbackState.stagingBuffers[writeIdx], visibilityResultsBuffer);
        
        // Issue event query to detect when GPU copy completes
        if (readbackState.completionQueries[writeIdx]) {
            context->End(readbackState.completionQueries[writeIdx]);
        }
        
        auto copyEnd = std::chrono::high_resolution_clock::now();
        stats.copyTimeMs = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(copyEnd - copyStart).count());

        // Store geometry snapshot WITH this buffer so results match when read back
        readbackState.geometrySnapshots[writeIdx] = pendingGeometrySnapshot;
        readbackState.geometryCount[writeIdx] = numGeometry;
        
        // Mark buffer as pending
        readbackState.hasPendingRead[writeIdx] = true;
        readbackState.pendingFrameIndex[writeIdx] = globals::state->frameCount;
        readbackState.numPendingReads++;
        
        // Advance write index for next frame
        readbackState.writeIndex = (readbackState.writeIndex + 1) % AsyncReadbackState::BUFFER_COUNT;

        // Update statistics
        stats.frameIndex = globals::state->frameCount;
    }
}

void HiZOcclusion::UnbindD3DResources()
{
    auto context = globals::d3d::context;
    
    // Local null arrays for unbinding - avoids per-instance member overhead
    ID3D11Buffer* nullCBs[1] = { nullptr };
    ID3D11SamplerState* nullSamplers[1] = { nullptr };
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    
    context->CSSetShaderResources(0, 2, nullSRVs); // t0 and t1
    context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    context->CSSetShader(nullptr, nullptr, 0);
    context->CSSetSamplers(0, 1, nullSamplers);
    context->CSSetConstantBuffers(0, 1, nullCBs);
}

void HiZOcclusion::DispatchComputeShader() 
{
    auto* context = globals::d3d::context;
    auto* device = globals::d3d::device;
    auto* renderer = globals::game::renderer;
    if (!context || !device || !renderer) {
        logger::warn("DispatchComputeShader: missing D3D context/device/renderer");
        return;
    }

    if (!hiZTestCS || !geometryBoundsBuffer || !visibilityResultsBuffer || !hiZTestParamsBuffer || !hiZSRV || !hiZSampler) {
        logger::warn("DispatchComputeShader: required resources not ready");
        return;
    }

    // geometryBounds and pendingGeometrySnapshot are already populated by ExecuteVisibilityTests()
    if (numGeometry == 0 || geometryBounds.empty()) {
        return;
    }

    // Update the buffer for GPU
    context->UpdateSubresource(geometryBoundsBuffer, 0, nullptr, geometryBounds.data(), 0, 0);
    
    // Update constant buffer with camera parameters
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HiZSettings params{};
        params.hiZParams = DirectX::XMFLOAT4(static_cast<float>(hiZMipCount), settings.conservativeBias, static_cast<float>(numGeometry), static_cast<float>(settings.debugMode));
        // Use average eye position in VR for more accurate occlusion testing
        // This ensures objects visible to either eye are not incorrectly culled
        auto eyePos = REL::Module::IsVR() ? Util::GetAverageEyePosition() : Util::GetEyePosition(0);
        params.cameraWorldPos = DirectX::XMFLOAT3(eyePos.x, eyePos.y, eyePos.z);
        params.overlaySettings = DirectX::XMFLOAT4(
            settings.enableBoundsViewer ? 1.0f : 0.0f,
            static_cast<float>(settings.boundsMaxObjects),
            0.0f, 0.0f);
        
        // Pack color toggles into float4 (7 bits used)
        float toggleBits = 0.0f;
        if (settings.showVisTestPassed) toggleBits += 1.0f;       // bit 0
        if (settings.showVisInsideBounds) toggleBits += 2.0f;      // bit 1
        if (settings.showVisInvalidRadius) toggleBits += 4.0f;       // bit 2
        if (settings.showCulledFrustum) toggleBits += 8.0f;       // bit 3
        if (settings.showCulledNoEarlyOut) toggleBits += 16.0f;  // bit 4
        params.overlayColorToggles = DirectX::XMFLOAT4(toggleBits, 0.0f, 0.0f, 0.0f);

        D3D11_TEXTURE2D_DESC texDesc{};
        renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&texDesc);

        params.bufferDim = { (float)texDesc.Width, (float)texDesc.Height };
        params.bufferDimInv = { 1.0f / params.bufferDim.x, 1.0f / params.bufferDim.y };

        if (SUCCEEDED(context->Map(hiZTestParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &params, sizeof(HiZSettings));
            context->Unmap(hiZTestParamsBuffer, 0);
        } else {
            logger::warn("ExecuteVisibilityTests: failed to map hiZTestParamsBuffer");
            return;
        }
    }
    
    // Bind resources and dispatch Hi-Z test compute shader for batch processing
    {
        if (settings.enableBoundsViewer) {
            if (!boundsOverlayTex || boundsOverlayW != hiZWidth || boundsOverlayH != hiZHeight) {
                ReleaseBoundsOverlayResources();
                SetupBoundsOverlayResources(hiZWidth, hiZHeight);
            }
            ClearBoundsOverlay();
        }

        const bool overlayEnabled = settings.enableBoundsViewer && (boundsOverlayUAV != nullptr);

        UINT uavCount = overlayEnabled ? 3u : 2u;
        ID3D11UnorderedAccessView* uavs[3] = {
            visibilityResultsUAV,
            (settings.debugMode || settings.enableBoundsViewer) ? debugResultsUAV : nullptr,
            overlayEnabled ? boundsOverlayUAV : nullptr
        };
        context->CSSetUnorderedAccessViews(0, uavCount, uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { hiZSRV, geometryBoundsSRV };
        context->CSSetShaderResources(0, 2, srvs);
        context->CSSetConstantBuffers(0, 1, &hiZTestParamsBuffer);

        if (hiZSampler) {
            context->CSSetSamplers(0, 1, &hiZSampler);
        }
        context->CSSetShader(hiZTestCS, nullptr, 0);

        // Dispatch for batch processing with GPU timestamp profiling
        {
            const uint32_t threadGroupSize = 256;
            uint32_t numGroups = (numGeometry + threadGroupSize - 1) / threadGroupSize;

            // Try to read completed GPU timing from previous frames (async readback)
            auto& readQuery = gpuTimingQueries[gpuTimingReadIndex];
            if (readQuery.pending && readQuery.disjointQuery) {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
                HRESULT disjointResult = context->GetData(readQuery.disjointQuery, &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (disjointResult == S_OK) {
                    UINT64 beginTimestamp = 0, endTimestamp = 0;
                    HRESULT beginResult = context->GetData(readQuery.beginTimestamp, &beginTimestamp, sizeof(beginTimestamp), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    HRESULT endResult = context->GetData(readQuery.endTimestamp, &endTimestamp, sizeof(endTimestamp), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    
                    if (beginResult == S_OK && endResult == S_OK && !disjointData.Disjoint && disjointData.Frequency > 0) {
                        // Calculate GPU time in milliseconds
                        double gpuTimeMs = static_cast<double>(endTimestamp - beginTimestamp) / static_cast<double>(disjointData.Frequency) * 1000.0;
                        stats.gpuCullingTimeMs = static_cast<float>(gpuTimeMs);
                    }
                    readQuery.pending = false;
                    gpuTimingReadIndex = (gpuTimingReadIndex + 1) % GPU_TIMING_BUFFER_COUNT;
                }
            }

            // Start GPU timing for this frame's dispatch
            auto& writeQuery = gpuTimingQueries[gpuTimingWriteIndex];
            bool useGpuTiming = writeQuery.disjointQuery && writeQuery.beginTimestamp && writeQuery.endTimestamp && !writeQuery.pending;
            
            if (useGpuTiming) {
                context->Begin(writeQuery.disjointQuery);
                context->End(writeQuery.beginTimestamp);
            }

            context->Dispatch(numGroups, 1, 1);

            if (useGpuTiming) {
                context->End(writeQuery.endTimestamp);
                context->End(writeQuery.disjointQuery);
                writeQuery.pending = true;
                gpuTimingWriteIndex = (gpuTimingWriteIndex + 1) % GPU_TIMING_BUFFER_COUNT;
            }

            if (settings.enableBoundsViewer && boundsOverlayUAV) {
                overlayUpdatedThisFrame = true;
            }
        }

        // Unbind resources (must pass arrays of nulls)
        ID3D11ShaderResourceView* nullSRVs_tests[2] = { nullptr, nullptr };
        context->CSSetShaderResources(0, 2, nullSRVs_tests);
        ID3D11UnorderedAccessView* nullUAVs_tests[2] = { nullptr, nullptr };
        context->CSSetUnorderedAccessViews(0, 2, nullUAVs_tests, nullptr);
        ID3D11SamplerState* nullSamplers_tests[1] = { nullptr };
        context->CSSetSamplers(0, 1, nullSamplers_tests);
        context->CSSetShader(nullptr, nullptr, 0);
    }
}

void HiZOcclusion::ProcessVisibilityResults(uint32_t bufferIndex) {

    unCullNextFrame.clear();
    
    
    // Track when we received fresh results
    stats.lastResultFrame = globals::state->frameCount;
    stats.staleFrameCount = 0;

    // Read from the correct triple-buffered staging buffer
    const HiZOcclusion::OcclusionResult* visibilityData = static_cast<const HiZOcclusion::OcclusionResult*>(readbackState.mappedData[bufferIndex].pData);
    
    // Use the geometry snapshot that was stored with this buffer
    const auto& geometrySnapshot = readbackState.geometrySnapshots[bufferIndex];
    const uint32_t geometryCount = readbackState.geometryCount[bufferIndex];

    for (uint32_t i = 0; i < geometryCount && i < geometrySnapshot.size(); ++i) {
        if (!geometrySnapshot[i]) continue;
        stats.totalTested++;
        
        auto& geo = geometrySnapshot[i];
        const auto& testResults = visibilityData[i];

        switch (testResults.result) {
            case static_cast<uint32_t>(-3): {// Not culled: Test passed
                MarkGeometryVisible(geo.get());
                stats.visTestPassed++;
                break;
            }
            case static_cast<uint32_t>(-2): { // Not culled: Inside bounds
                MarkGeometryVisible(geo.get());
                stats.visInsideBounds++;
                break;
            }
            case static_cast<uint32_t>(-1): { // Not culled: Invalid Radius
                MarkGeometryVisible(geo.get());
                stats.visInvalidRadius++;
                break;
            }
            case 0u: { // default value
                stats.defaultValue++;
                break;
            }
            case 1u: { // Culled: Frustum
                stats.culledFrustum++;
                MarkGeometryOccluded(geo.get());
                unCullNextFrame.push_back(geo);
                break;
            }
            case 2u: { // Culled: No early out
                stats.culledNoEarlyOut++;
                MarkGeometryOccluded(geo.get());
                unCullNextFrame.push_back(geo);
                break;
            }
            default: {
                break;
            }
        }
    }
}

bool HiZOcclusion::IsGeometryOccluded(RE::BSGeometry* geometry) const
{
    if (!geometry) {
        return false;
    }
    return occludedGeometry.find(geometry) != occludedGeometry.end();
}

void HiZOcclusion::MarkGeometryOccluded(RE::BSGeometry* geometry)
{
    if (!geometry) {
        return;
    }
    occludedGeometry.insert(geometry);
}

void HiZOcclusion::MarkGeometryVisible(RE::BSGeometry* geometry)
{
    if (!geometry) {
        return;
    }
    occludedGeometry.erase(geometry);
}

void HiZOcclusion::ClearOcclusionState()
{
    occludedGeometry.clear();
}
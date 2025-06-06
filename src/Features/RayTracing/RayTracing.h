#pragma once

#include <d3d12.h>
#include <DXRHelpers>

/* Reference Documenation:
    1. https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
    2. Source: I made it up.
*/

/* Ray Tracing Pipeline:
    1. Acceleration Structures:
        - BLAS: per-geometry (triangle meshes or AABB primitives)
        - TLAS: instances of BLAS with transforms
    2. Shader Tables & Records:
        - Shader Table: array of Shader Records
        - Shader Record: shader ID + local root arguments
    3. Shader Types:
        - RayGen: generate rays into the scene
        - Miss: handle rays with no hit (sky/background)
        - HitGroup: closestHit, anyHit, intersection shaders
    4. Dispatch:
        - Use DispatchRays() binding AS and shader tables
*/

struct RayTracing : Feature {
    static RayTracing* GetSingleton() {
        static RayTracing singleton;
        return &singleton;
    }

    virtual inline std::string GetName() override { return "Ray Tracing"; }
    virtual inline std::string GetShortName() override { return "RayTracing"; }
    virtual inline std::string_view GetShaderDefineName() override { return "RAY_TRACING"; }
    bool HasShaderDefine(RE::BSShader::Type) override { return true; };

    virtual inline std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;

    virtual void SetupResources() override;
    virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

    struct Settings {
		uint Enabled = false;
		uint pad0[3];
	};
    Settings settings;

    bool enabledAtBoot = false;
    virtual bool SupportsVR() override { return false; }; // VR support later

    // Acceleration structures
    nv_helpers_dx12::BottomLevelASGenerator m_blasGenerator;
    nv_helpers_dx12::TopLevelASGenerator m_tlasGenerator;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_blasBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_tlasBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_scratchBuffer;

    // Pipeline
    std::unique_ptr<nv_helpers_dx12::RayTracingPipelineGenerator> m_pipelineGenerator;
    Microsoft::WRL::ComPtr<ID3D12StateObject> m_rtStateObject;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rayGenSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_hitSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_missSignature;

    // Shader tables
    struct ShaderTables {
        Microsoft::WRL::ComPtr<ID3D12Resource> rayGen;
        Microsoft::WRL::ComPtr<ID3D12Resource> miss;
        Microsoft::WRL::ComPtr<ID3D12Resource> hitGroup;
        
        UINT rayGenStride = 0;
        UINT missStride = 0;
        UINT hitGroupStride = 0;
    };
    ShaderTables m_shaderTables;

    // Shader identifiers
    struct ShaderIDs {
        void* rayGen = nullptr;
        void* miss = nullptr;
        void* closestHit = nullptr;
        void* anyHit = nullptr;
    };
    ShaderIDs m_shaderIDs;

    // Pipeline methods
    void CreateRayTracingPipeline();
    void CreateShaderBindingTables();

    // Shader resources
    Microsoft::WRL::ComPtr<IDxcBlob> m_rayGenLibrary;
    Microsoft::WRL::ComPtr<IDxcBlob> m_missLibrary;
    Microsoft::WRL::ComPtr<IDxcBlob> m_hitLibrary;
}
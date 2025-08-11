#pragma once

struct CloudShadows;
struct DynamicCubemaps;
struct ExtendedMaterials;
struct GrassCollision;
struct GrassLighting;
struct HairSpecular;
struct IBL;
struct LightLimitFix;
struct LODBlending;
struct InteriorSun;
struct InverseSquareLighting;
struct ScreenSpaceGI;
struct ScreenSpaceShadows;
struct Skylighting;
struct TerrainVariation;
struct SkySync;
struct SubsurfaceScattering;
struct TerrainBlending;
struct TerrainHelper;
struct TerrainShadows;
struct VolumetricLighting;
struct VR;
struct WaterEffects;
struct WeatherPicker;
struct PerformanceOverlay;
struct WetnessEffects;
struct ExtendedTranslucency;
struct Upscaling;

class ParticleLights;

class State;
class Deferred;
struct TruePBR;
class Menu;
class Streamline;
class DX12SwapChain;
class FidelityFX;
class XeSS;

namespace SIE
{
	class ShaderCache;
}

namespace globals
{
	namespace d3d
	{
		extern ID3D11Device* device;
		extern ID3D11DeviceContext* context;
		extern IDXGISwapChain* swapChain;
	}

	namespace features
	{
		extern CloudShadows cloudShadows;
		extern DynamicCubemaps dynamicCubemaps;
		extern ExtendedMaterials extendedMaterials;
		extern GrassCollision grassCollision;
		extern GrassLighting grassLighting;
		extern HairSpecular hairSpecular;
		extern IBL ibl;
		extern LightLimitFix lightLimitFix;
		extern LODBlending lodBlending;
		extern InteriorSun interiorSun;
		extern InverseSquareLighting inverseSquareLighting;
		extern ScreenSpaceGI screenSpaceGI;
		extern ScreenSpaceShadows screenSpaceShadows;
		extern Skylighting skylighting;
		extern TerrainVariation terrainVariation;
		extern SkySync skySync;
		extern SubsurfaceScattering subsurfaceScattering;
		extern TerrainBlending terrainBlending;
		extern TerrainHelper terrainHelper;
		extern TerrainShadows terrainShadows;
		extern VolumetricLighting volumetricLighting;
		extern VR vr;
		extern WaterEffects waterEffects;
		extern WeatherPicker weatherPicker;
		extern PerformanceOverlay performanceOverlay;
		extern WetnessEffects wetnessEffects;
		extern ExtendedTranslucency extendedTranslucency;

		namespace llf
		{
			extern ParticleLights particleLights;
		}
	}

	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	struct FrameBufferVR
	{
		// Must match HLSL VR layout exactly - packoffsets c0 to c86
		Matrix CameraView[2];                        // packoffset(c0) - 8 registers
		Matrix CameraProj[2];                        // packoffset(c8) - 8 registers
		Matrix CameraViewProj[2];                    // packoffset(c16) - 8 registers
		Matrix CameraViewProjUnjittered[2];          // packoffset(c24) - 8 registers
		Matrix CameraPreviousViewProjUnjittered[2];  // packoffset(c32) - 8 registers
		Matrix CameraProjUnjittered[2];              // packoffset(c40) - 8 registers
		Matrix CameraProjUnjitteredInverse[2];       // packoffset(c48) - 8 registers
		Matrix CameraViewInverse[2];                 // packoffset(c56) - 8 registers
		Matrix CameraViewProjInverse[2];             // packoffset(c64) - 8 registers
		Matrix CameraProjInverse[2];                 // packoffset(c72) - 8 registers
		float4 CameraPosAdjust[2];                   // packoffset(c80) - 2 registers
		float4 CameraPreviousPosAdjust[2];           // packoffset(c82) - 2 registers
		float4 FrameParams;                          // packoffset(c84) - 1 register
		float4 DynamicResolutionParams1;             // packoffset(c85) - 1 register
		float4 DynamicResolutionParams2;             // packoffset(c86) - 1 register
	};

	union FrameBufferCache
	{
		FrameBuffer nonVR;
		FrameBufferVR vr;

		// Helper functions for VR-agnostic access to eye 0 (or single eye)
		const Matrix& GetCameraView(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraView[eyeIndex] : nonVR.CameraView;
		}
		const Matrix& GetCameraProj(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProj[eyeIndex] : nonVR.CameraProj;
		}
		const Matrix& GetCameraViewProj(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProj[eyeIndex] : nonVR.CameraViewProj;
		}
		const Matrix& GetCameraViewProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProjUnjittered[eyeIndex] : nonVR.CameraViewProjUnjittered;
		}
		const Matrix& GetCameraPreviousViewProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPreviousViewProjUnjittered[eyeIndex] : nonVR.CameraPreviousViewProjUnjittered;
		}
		const Matrix& GetCameraProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjUnjittered[eyeIndex] : nonVR.CameraProjUnjittered;
		}
		const Matrix& GetCameraProjUnjitteredInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjUnjitteredInverse[eyeIndex] : nonVR.CameraProjUnjitteredInverse;
		}
		const Matrix& GetCameraViewInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewInverse[eyeIndex] : nonVR.CameraViewInverse;
		}
		const Matrix& GetCameraViewProjInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProjInverse[eyeIndex] : nonVR.CameraViewProjInverse;
		}
		const Matrix& GetCameraProjInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjInverse[eyeIndex] : nonVR.CameraProjInverse;
		}
		const float4& GetCameraPosAdjust(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPosAdjust[eyeIndex] : nonVR.CameraPosAdjust;
		}
		const float4& GetCameraPreviousPosAdjust(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPreviousPosAdjust[eyeIndex] : nonVR.CameraPreviousPosAdjust;
		}
		const float4& GetFrameParams() const
		{
			return REL::Module::IsVR() ? vr.FrameParams : nonVR.FrameParams;
		}
		const float4& GetDynamicResolutionParams1() const
		{
			return REL::Module::IsVR() ? vr.DynamicResolutionParams1 : nonVR.DynamicResolutionParams1;
		}
		const float4& GetDynamicResolutionParams2() const
		{
			return REL::Module::IsVR() ? vr.DynamicResolutionParams2 : nonVR.DynamicResolutionParams2;
		}
	};

	namespace game
	{
		extern RE::BSGraphics::RendererShadowState* shadowState;
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern RE::BSShaderManager::State* smState;
		extern RE::TES* tes;
		extern bool isVR;
		extern RE::MemoryManager* memoryManager;
		extern RE::INISettingCollection* iniSettingCollection;
		extern RE::INIPrefSettingCollection* iniPrefSettingCollection;
		extern RE::GameSettingCollection* gameSettingCollection;
		extern float* cameraNear;
		extern float* cameraFar;
		extern float* deltaTime;
		extern RE::BSUtilityShader* utilityShader;
		extern RE::Sky* sky;
		extern RE::UI* ui;

		extern RE::BSGraphics::PixelShader** currentPixelShader;
		extern RE::BSGraphics::VertexShader** currentVertexShader;
		extern REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags;

		extern RE::Setting* bEnableLandFade;
		extern RE::Setting* bShadowsOnGrass;
		extern RE::Setting* shadowMaskQuarter;
		extern REL::Relocation<ID3D11Buffer**> perFrame;
		extern REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		extern D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer;
		extern FrameBufferCache frameBufferCached;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
	}

	extern State* state;
	extern Deferred* deferred;
	extern TruePBR* truePBR;
	extern Menu* menu;
	extern SIE::ShaderCache* shaderCache;
	extern Streamline* streamline;
	extern Upscaling* upscaling;
	extern DX12SwapChain* dx12SwapChain;
	extern FidelityFX* fidelityFX;
	extern XeSS* xess;

	void OnInit();
	void ReInit();
	void OnDataLoaded();
	void InstallD3DHooks(ID3D11DeviceContext* a_context);
}
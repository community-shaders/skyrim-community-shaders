#include "ReverseZ.h"

#include "State.h"
#include "Utils/D3D.h"
#include "Globals.h"
#include <mutex>

struct DepthStates
{
	ID3D11DepthStencilState* a[6][40];

	static DepthStates* GetSingleton()
	{
		static auto depthStates = reinterpret_cast<DepthStates*>(REL::RelocationID(524747, 524747).address());
		return depthStates;
	}
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ReverseZ::Settings,
	EnableReverseZ)

void ReverseZ::DrawSettings()
{
}

void ReverseZ::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ReverseZ::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ReverseZ::RestoreDefaultSettings()
{
	settings = {};
}

struct alignas(16) ViewData
{
	Vector4 viewUp;                           // 00
	Vector4 viewRight;                        // 10
	Vector4 viewForward;                      // 20
	Matrix viewMat;                           // 30
	Matrix projMat;                           // 70
	Matrix viewProjMat;                       // B0
	Matrix unknownMat1;                       // F0 - all 0?
	Matrix viewProjMatrixUnjittered;          // 130
	Matrix previousViewProjMatrixUnjittered;  // 170
	Matrix projMatrixUnjittered;              // 1B0
	Matrix unknownMat2;                       // 1F0 - all 0?
	float viewPort[4];                        // 230 - NiRect<float> { left = 0, right = 1, top = 1, bottom = 0 }
	RE::NiPoint2 viewDepthRange;              // 240
	char _pad0[0x8];                          // 248
};
static_assert(sizeof(ViewData) == 0x250);
static_assert(offsetof(ViewData, viewUp) == 0);
static_assert(offsetof(ViewData, viewRight) == 0x10);
static_assert(offsetof(ViewData, viewForward) == 0x20);
static_assert(offsetof(ViewData, viewMat) == 0x30);
static_assert(offsetof(ViewData, projMat) == 0x70);
static_assert(offsetof(ViewData, viewProjMat) == 0xb0);
static_assert(offsetof(ViewData, unknownMat1) == 0xf0);
static_assert(offsetof(ViewData, viewProjMatrixUnjittered) == 0x130);
static_assert(offsetof(ViewData, previousViewProjMatrixUnjittered) == 0x170);
static_assert(offsetof(ViewData, projMatrixUnjittered) == 0x1b0);
static_assert(offsetof(ViewData, unknownMat2) == 0x1f0);
static_assert(offsetof(ViewData, viewPort) == 0x230);
static_assert(offsetof(ViewData, viewDepthRange) == 0x240);

struct CAMERASTATE_RUNTIME_DATA
{
	ViewData camViewData;           /* 08 VR is BSTArray, Each array has 2 elements (one for each eye?) */
	RE::NiPoint3 posAdjust;         /* 20 */
	RE::NiPoint3 currentPosAdjust;  /* 38 */
	RE::NiPoint3 previousPosAdjust; /* 50 */
};

struct UpdateGPUCameraData_Finalise
{
	static void thunk(RE::BSGraphics::State* a1, ViewData* a2, RE::NiCamera* a3, char a4)
	{		
		func(a1, a2, a3, a4);

		if (globals::features::reverseZ.settings.EnableReverseZ) {
			// One-time swap of depth state operators for reverse Z
			static std::once_flag depthStatesSwapped;
			std::call_once(depthStatesSwapped, []() {
				SwapDepthStates();
			});

			a2->projMat = a2->projMat.Transpose();
			a2->projMatrixUnjittered = a2->projMatrixUnjittered.Transpose();
			a2->viewMat = a2->viewMat.Transpose();

			// Apply reverse Z to projection matrices
			ApplyReverseZToMatrix(a2->projMat);
			ApplyReverseZToMatrix(a2->projMatrixUnjittered);

			// Recalculate view-projection matrices
			a2->viewProjMat = a2->viewMat * a2->projMat;
			a2->viewProjMatrixUnjittered = a2->viewMat * a2->projMatrixUnjittered;

			// Update depth range for reverse Z
			a2->viewDepthRange.x = 1.0f;
			a2->viewDepthRange.y = 0.0f;

			a2->projMat = a2->projMat.Transpose();
			a2->projMatrixUnjittered = a2->projMatrixUnjittered.Transpose();
			a2->viewProjMat = a2->viewProjMat.Transpose();
			a2->viewProjMatrixUnjittered = a2->viewProjMatrixUnjittered.Transpose();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
	
private:
	static void SwapDepthStates()
	{
		auto depthStates = DepthStates::GetSingleton();
		auto device = globals::d3d::device;
		
		logger::info("Swapping DepthStates for reverse Z");
		
		// DepthStencilDepthMode enum:
		// kDisabled = 0
		// kTest = 1  
		// kWrite = 2
		// kTestWrite = 3
		// kTestEqual = 4
		// kTestGreaterEqual = 5
		// kTestGreater = 6
		
		// Create new depth stencil states with swapped comparison operators
		// First array index represents DepthStencilDepthMode enum values
		for (int depthMode = 0; depthMode < 7; depthMode++) {
			for (int depthFunc = 0; depthFunc < 40; depthFunc++) {
				if (depthStates->a[depthMode][depthFunc]) {
					// Create depth stencil state description
					D3D11_DEPTH_STENCIL_DESC desc = {};
					
					// Map depthMode to appropriate depth state configuration
					switch (depthMode) {
						case 0: // kDisabled
							desc.DepthEnable = FALSE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
							desc.DepthFunc = D3D11_COMPARISON_ALWAYS; // No change needed
							break;
							
						case 1: // kTest
							desc.DepthEnable = TRUE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
							desc.DepthFunc = D3D11_COMPARISON_GREATER; // LESS -> GREATER
							break;
							
						case 2: // kWrite  
							desc.DepthEnable = FALSE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
							desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
							break;
							
						case 3: // kTestWrite
							desc.DepthEnable = TRUE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
							desc.DepthFunc = D3D11_COMPARISON_GREATER; // LESS -> GREATER
							break;
							
						case 4: // kTestEqual
							desc.DepthEnable = TRUE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
							desc.DepthFunc = D3D11_COMPARISON_EQUAL; // No change
							break;
							
						case 5: // kTestGreaterEqual
							desc.DepthEnable = TRUE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
							desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; // GREATER_EQUAL -> LESS_EQUAL
							break;
							
						case 6: // kTestGreater  
							desc.DepthEnable = TRUE;
							desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
							desc.DepthFunc = D3D11_COMPARISON_LESS; // GREATER -> LESS
							break;
							
						default:
							continue; // Skip unknown modes
					}
					
					desc.StencilEnable = FALSE;
					
					// Create new depth stencil state with swapped comparison
					ID3D11DepthStencilState* newState = nullptr;
					HRESULT hr = device->CreateDepthStencilState(&desc, &newState);
					
					if (SUCCEEDED(hr)) {
						// Replace the original state (don't release - game manages these)
						depthStates->a[depthMode][depthFunc] = newState;
					}
				}
			}
		}
		
		logger::info("DepthStates swap completed");
	}

	static void ApplyReverseZToMatrix(Matrix& projMatrix)
	{
		// https://github.com/pr0bability/fnv-lod-flicker-fix/blob/master/LODFlickerFix/main.cpp
		// Convert to DirectX matrix for easier manipulation
		auto& mat = static_cast<DirectX::XMFLOAT4X4&>(projMatrix);

		float cameraNear = *globals::game::cameraNear;
		float cameraFar = *globals::game::cameraFar;
		
		float fInvFmN = 1.0f / (cameraFar - cameraNear);
		mat._33 = -(cameraNear * fInvFmN);
		mat._43 = cameraNear * cameraFar * fInvFmN;
	}
};

void ReverseZ::PostPostLoad()
{
	stl::detour_thunk<UpdateGPUCameraData_Finalise>(REL::RelocationID(75711, 75711));

	logger::info("ReverseZ hooks installed");
}

bool ReverseZ::HasShaderDefine(RE::BSShader::Type)
{
	return settings.EnableReverseZ;
}
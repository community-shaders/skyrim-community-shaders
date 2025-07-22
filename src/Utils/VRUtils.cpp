#include "VRUtils.h"
#include "Features/VR.h"  // For ButtonCombo and ControllerDevice definitions
#include "RE/B/BSOpenVR.h"
#include "UI.h"
#include <imgui.h>

namespace Util
{
	void DrawButtonCombo(const std::vector<ButtonCombo>& combo, bool showControllerLabels)
	{
		bool anyDrawn = false;
		for (size_t i = 0; i < combo.size(); ++i) {
			if (combo[i].GetKey() == 0)
				continue;
			if (i > 0) {
				ImGui::SameLine();
				ImGui::Text("+");
				ImGui::SameLine();
			}
			ImVec4 color;
			switch (combo[i].GetDevice()) {
			case ControllerDevice::Primary:
				color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
				break;
			case ControllerDevice::Secondary:
				color = ImVec4(0.0f, 0.6f, 1.0f, 1.0f);
				break;
			case ControllerDevice::Both:
				color = ImVec4(0.5f, 0.0f, 0.5f, 1.0f);
				break;
			default:
				color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
				break;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", RE::GetOpenVRButtonName(combo[i].GetKey()));
			ImGui::PopStyleColor();
			anyDrawn = true;
			if (showControllerLabels) {
				ImGui::SameLine();
				const char* label = "";
				switch (combo[i].GetDevice()) {
				case ControllerDevice::Primary:
					label = "(Primary)";
					break;
				case ControllerDevice::Secondary:
					label = "(Secondary)";
					break;
				case ControllerDevice::Both:
					label = "(Both)";
					break;
				default:
					break;
				}
				ImGui::TextDisabled("%s", label);
				if (i < combo.size() - 1)
					ImGui::SameLine();
			}
		}
		if (anyDrawn) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				Util::DrawColoredMultiLineTooltip({ { "Color coding:", ImVec4(1, 1, 1, 1) },
					{ "Green = Primary controller", ImVec4(0.0f, 1.0f, 0.0f, 1.0f) },
					{ "Blue = Secondary controller", ImVec4(0.0f, 0.6f, 1.0f, 1.0f) },
					{ "Purple = Both controllers", ImVec4(0.5f, 0.0f, 0.5f, 1.0f) } });
			}
		}
	}

	vr::HmdMatrix34_t ComputeOverlayTransformFromHMD(float distance, float offsetX, float offsetY, float offsetZ)
	{
		vr::HmdMatrix34_t transform = {};
		RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
		if (openvr) {
			auto* system = openvr->vrSystem;
			if (system) {
				vr::TrackedDevicePose_t hmdPose;
				system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);
				if (hmdPose.bPoseIsValid) {
					transform = hmdPose.mDeviceToAbsoluteTracking;
					// Move forward by distance (Z axis in HMD space)
					transform.m[0][3] += transform.m[0][2] * (-distance);
					transform.m[1][3] += transform.m[1][2] * (-distance);
					transform.m[2][3] += transform.m[2][2] * (-distance);
					// Apply HMD overlay offsets (in HMD local space)
					transform.m[0][3] += transform.m[0][0] * offsetX + transform.m[0][1] * offsetY + transform.m[0][2] * offsetZ;
					transform.m[1][3] += transform.m[1][0] * offsetX + transform.m[1][1] * offsetY + transform.m[1][2] * offsetZ;
					transform.m[2][3] += transform.m[2][0] * offsetX + transform.m[2][1] * offsetY + transform.m[2][2] * offsetZ;
				}
			}
		}
		return transform;
	}

	void VROverlayResources::Create(ID3D11Device* device, const char* key, const char* name, int width, int height)
	{
		Destroy();  // Clean up any existing resources

		// Create D3D resources
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
			logger::error("Failed to create overlay texture");
			return;
		}

		if (FAILED(device->CreateRenderTargetView(texture, nullptr, &rtv))) {
			logger::error("Failed to create overlay RTV");
			texture->Release();
			texture = nullptr;
			return;
		}

		// Create OpenVR overlay
		vr::EVROverlayError err = vr::VROverlay()->CreateOverlay(key, name, &handle);
		if (err != vr::VROverlayError_None) {
			logger::error("Failed to create overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			Destroy();
			return;
		}
	}

	void VROverlayResources::Destroy()
	{
		if (rtv) {
			rtv->Release();
			rtv = nullptr;
		}
		if (texture) {
			texture->Release();
			texture = nullptr;
		}
		if (handle != vr::k_ulOverlayHandleInvalid) {
			vr::VROverlay()->DestroyOverlay(handle);
			handle = vr::k_ulOverlayHandleInvalid;
		}
	}

	void VROverlayResources::SetInputFlags(vr::IVROverlay* overlay)
	{
		if (!overlay || handle == vr::k_ulOverlayHandleInvalid)
			return;

		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_VisibleInDashboard, true);
	}

	bool VROverlayResources::SubmitTexture(vr::IVROverlay* overlay)
	{
		if (!overlay || !texture || handle == vr::k_ulOverlayHandleInvalid)
			return false;

		vr::Texture_t tex = { texture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
		vr::EVROverlayError err = overlay->SetOverlayTexture(handle, &tex);
		if (err != vr::VROverlayError_None) {
			logger::error("SetOverlayTexture failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			return false;
		}
		return true;
	}
}
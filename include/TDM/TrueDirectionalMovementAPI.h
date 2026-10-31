#pragma once
#include <functional>
#include <stdint.h>

/*
* For modders: Copy this file into your own project if you wish to use this API
*/
	namespace TDM_API
{
	constexpr const auto TDMPluginName = "TrueDirectionalMovement";

	using PluginHandle = SKSE::PluginHandle;
	using ActorHandle = RE::ActorHandle;

	// Available True Directional Movement interface versions
	enum class InterfaceVersion : uint8_t
	{
		V1,
		V2
	};

	// Error types that may be returned by the True Directional Movement API
	enum class APIResult : uint8_t
	{
		// Your API call was successful
		OK,

		// You tried to release a resource that was not allocated to you
		// Do not attempt to manipulate the requested resource if you receive this response
		NotOwner,

		// True Directional Movement currently must keep control of this resource for proper functionality
		// Do not attempt to manipulate the requested resource if you receive this response
		MustKeep,

		// You have already been given control of this resource
		AlreadyGiven,

		// Another mod has been given control of this resource at the present time
		// Do not attempt to manipulate the requested resource if you receive this response
		AlreadyTaken,

		// You sent a command on a thread that could cause a data race were it to be processed
		// Do not attempt to manipulate the requested resource if you receive this response
		BadThread,
	};

	// True Directional Movement's modder interface
	class IVTDM1
	{
	public:
		/// <summary>
		/// Get the thread ID True Directional Movement is running in.
		/// You may compare this with the result of GetCurrentThreadId() to help determine
		/// if you are using the correct thread.
		/// </summary>
		/// <returns>TID</returns>
		[[nodiscard]] virtual unsigned long GetTDMThreadId() const noexcept = 0;

		/// <summary>
		/// Get the current state (enabled / disabled) of directional movement.
		/// </summary>
		/// <returns>The current state (true / false) of directional movement</returns>
		[[nodiscard]] virtual bool GetDirectionalMovementState() noexcept = 0;

		/// <summary>
		/// Get the current state (enabled / disabled) of target lock.
		/// </summary>
		/// <returns>The current state (true / false) of target lock</returns>
		[[nodiscard]] virtual bool GetTargetLockState() noexcept = 0;

		/// <summary>
		/// Get the actor handle of the currently locked target.
		/// </summary>
		/// <returns>The actor handle of the currently locked target</returns>
		[[nodiscard]] virtual ActorHandle GetCurrentTarget() noexcept = 0;

		/// <summary>
		/// Request the plugin to forcibly disable directional movement.
		/// If granted, directional movement will be disabled for the duration of your control.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <returns>OK, MustKeep, AlreadyGiven, AlreadyTaken</returns>
		[[nodiscard]] virtual APIResult RequestDisableDirectionalMovement(PluginHandle a_myPluginHandle) noexcept = 0;

		/// <summary>
		/// Request the plugin to forcibly disable headtracking.
		/// If granted, this mod's headtracking will be disabled for the duration of your control.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <returns>OK, MustKeep, AlreadyGiven, AlreadyTaken</returns>
		[[nodiscard]] virtual APIResult RequestDisableHeadtracking(PluginHandle a_myPluginHandle) noexcept = 0;

		/// <summary>
		/// Returns the current owner of the forced disable of directional movement.
		/// </summary>
		/// <returns>Handle or kPluginHandle_Invalid if no one currently owns the resource</returns>
		virtual PluginHandle GetDisableDirectionalMovementOwner() const noexcept = 0;

		/// <summary>
		/// Returns the current owner of the forced disable of headtracking.
		/// </summary>
		/// <returns>Handle or kPluginHandle_Invalid if no one currently owns the resource</returns>
		virtual PluginHandle GetDisableHeadtrackingOwner() const noexcept = 0;

		/// <summary>
		/// Release your forced disable of directional movement.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <returns>OK, NotOwner</returns>
		virtual APIResult ReleaseDisableDirectionalMovement(PluginHandle a_myPluginHandle) noexcept = 0;

		/// <summary>
		/// Release your forced disable of headtracking.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <returns>OK, NotOwner</returns>
		virtual APIResult ReleaseDisableHeadtracking(PluginHandle a_myPluginHandle) noexcept = 0;
	};

	class IVTDM2 : public IVTDM1
	{
	public:
		/// <summary>
		/// Request the control over the player character's yaw.
		/// If granted, you may use the SetPlayerYaw function and TDM will not adjust the yaw for the duration of your control.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <param name="a_yawRotationSpeedMultiplier">The speed multiplier to use when smoothly rotating to the desired yaw. 0 is instant.</param>
		/// <returns>OK, MustKeep, AlreadyGiven, AlreadyTaken</returns>
		[[nodiscard]] virtual APIResult RequestYawControl(PluginHandle a_myPluginHandle, float a_yawRotationSpeedMultiplier) noexcept = 0;

		/// <summary>
		/// Tries to set the player character's desired yaw. Will only do so if granted control.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <param name="a_desiredYaw">The desired yaw</param>
		/// <returns>OK, NotOwner</returns>
		virtual APIResult SetPlayerYaw(PluginHandle a_myPluginHandle, float a_desiredYaw) noexcept = 0;

		/// <summary>
		/// Release your control over the player character's yaw.
		/// </summary>
		/// <param name="a_myPluginHandle">Your assigned plugin handle</param>
		/// <returns>OK, NotOwner</returns>
		virtual APIResult ReleaseYawControl(PluginHandle a_myPluginHandle) noexcept = 0;
	};

	typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);

	/// <summary>
	/// Request the True Directional Movement API interface.
	/// Recommended: Send your request during or after SKSEMessagingInterface::kMessage_PostLoad to make sure the dll has already been loaded
	/// </summary>
	/// <param name="a_interfaceVersion">The interface version to request</param>
	/// <returns>The pointer to the API singleton, or nullptr if request failed</returns>
	[[nodiscard]] inline void* RequestPluginAPI(const InterfaceVersion a_interfaceVersion = InterfaceVersion::V2)
	{
		auto pluginHandle = GetModuleHandleA("TrueDirectionalMovement.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)GetProcAddress(pluginHandle, "RequestPluginAPI");
		if (requestAPIFunction) {
			return requestAPIFunction(a_interfaceVersion);
		}
		return nullptr;
	}
}

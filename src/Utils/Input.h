#pragma once
#include <format>
#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * @brief Identifies the type of input device for input mapping
 *
 * Used to distinguish between VR controllers, keyboard, mouse, and gamepads.
 */
enum class InputDeviceType
{
	Primary = 0,    ///< VR: The dominant hand controller (right for right-handed, left for left-handed)
	Secondary = 1,  ///< VR: The non-dominant hand controller
	Both = 2,       ///< VR: Both controllers simultaneously
	Keyboard = 3,   ///< Keyboard input
	Mouse = 4,      ///< Mouse input
	Gamepad = 5     ///< Gamepad/Controller input (non-VR)
};

/**
 * @brief Converts an InputDeviceType enum value to a human-readable string
 * @param device The input device to convert
 * @return String representation of the device
 */
constexpr const char* ToString(InputDeviceType device)
{
	switch (device) {
	case InputDeviceType::Primary:
		return "Primary";
	case InputDeviceType::Secondary:
		return "Secondary";
	case InputDeviceType::Both:
		return "Both";
	case InputDeviceType::Keyboard:
		return "Keyboard";
	case InputDeviceType::Mouse:
		return "Mouse";
	case InputDeviceType::Gamepad:
		return "Gamepad";
	default:
		return "Unknown";
	}
}

/**
 * @brief Validates if an InputDeviceType enum value is within valid range
 * @param device The input device to validate
 * @return true if the device value is valid, false otherwise
 */
constexpr bool IsValidDevice(InputDeviceType device)
{
	return device >= InputDeviceType::Primary && device <= InputDeviceType::Gamepad;
}

/**
 * @brief Represents a combination of input device and button/key
 *
 * This structure efficiently encodes both the target device and the specific button/key
 * into a single 32-bit value for performance and JSON serialization compatibility.
 * The upper 16 bits store the device type, lower 16 bits store the key code.
 *
 * Can represent:
 * - VR Controller button presses
 * - Keyboard key presses
 * - Mouse button clicks
 */
struct InputCombo
{
private:
	uint32_t deviceAndKey;  // device in upper bits, key in lower bits

public:
	/**
	 * @brief Constructs an InputCombo with the specified device and key
	 * @param device The target input device
	 * @param key The button/key code (must fit in 16 bits, values > 0xFFFF will be truncated)
	 */
	InputCombo(InputDeviceType device, uint32_t key) :
		deviceAndKey((static_cast<uint32_t>(device) << 16) | (key & 0xFFFF))
	{
	}

	// VR helper methods
	static InputCombo Primary(uint32_t key) { return InputCombo(InputDeviceType::Primary, key); }
	static InputCombo Secondary(uint32_t key) { return InputCombo(InputDeviceType::Secondary, key); }
	static InputCombo Both(uint32_t key) { return InputCombo(InputDeviceType::Both, key); }

	// Desktop helper methods
	static InputCombo Keyboard(uint32_t key) { return InputCombo(InputDeviceType::Keyboard, key); }
	static InputCombo Mouse(uint32_t key) { return InputCombo(InputDeviceType::Mouse, key); }
	static InputCombo Gamepad(uint32_t key) { return InputCombo(InputDeviceType::Gamepad, key); }

	/**
	 * @brief Gets the input device from this combo
	 * @return The target input device
	 */
	InputDeviceType GetDevice() const { return static_cast<InputDeviceType>(deviceAndKey >> 16); }

	/**
	 * @brief Gets the button/key code from this combo
	 * @return The button/key code (16-bit value)
	 */
	uint32_t GetKey() const { return deviceAndKey & 0xFFFF; }

	/**
	 * @brief Validates if this InputCombo has valid device and key values
	 * @return true if both device and key are valid, false otherwise
	 */
	bool IsValid() const
	{
		return IsValidDevice(GetDevice()) && GetKey() != 0;
	}

	/**
	 * @brief Equality comparison operator for container usage
	 * @param other The InputCombo to compare with
	 * @return true if both combos represent the same device and key
	 */
	bool operator==(const InputCombo& other) const
	{
		return deviceAndKey == other.deviceAndKey;
	}

	/**
	 * @brief Less-than comparison operator for ordered container usage
	 * @param other The InputCombo to compare with
	 * @return true if this combo sorts before the other
	 */
	bool operator<(const InputCombo& other) const
	{
		return deviceAndKey < other.deviceAndKey;
	}

	/**
	 * @brief Default constructor for JSON serialization compatibility
	 */
	InputCombo() :
		deviceAndKey(0) {}

	/**
	 * @brief JSON serialization support - converts InputCombo to JSON
	 * @param j Output JSON object
	 * @param combo InputCombo to serialize
	 */
	friend void to_json(nlohmann::json& j, const InputCombo& combo)
	{
		j = combo.deviceAndKey;
	}

	/**
	 * @brief JSON deserialization support - creates InputCombo from JSON
	 * @param j Input JSON object
	 * @param combo InputCombo to populate
	 */
	friend void from_json(const nlohmann::json& j, InputCombo& combo)
	{
		combo.deviceAndKey = j.get<uint32_t>();
	}

	/**
	 * @brief JSON serialization support for vectors of InputCombo
	 *
	 * When serializing a vector of combos, check if they are all simple keyboard inputs.
	 * If so, we could serialize as a list of integers (key codes) for better readability.
	 */

	/**
	 * @brief Static helper to get a formatted string for a VR combo
	 */
	static std::string GetVRString(const std::vector<InputCombo>& combo)
	{
		std::string result;
		for (size_t i = 0; i < combo.size(); ++i) {
			if (i > 0)
				result += " + ";
			const auto& input = combo[i];

			if (input.GetDevice() == InputDeviceType::Keyboard) {
				result += std::format("Key:{:X}", input.GetKey());
			} else {
				// VR Button mapping
				// Based on BSOpenVRControllerDevice::Keys enum values
				// 2 = Grip, 7 = Trigger, 32 = Touchpad, 33 = Stick, 1 = BY, 31 = XA
				// These are standard OpenVR / SkyrimVR constants
				switch (input.GetKey()) {
				case 2:
					result += "Grip";
					break;
				case 7:
					result += "Trigger";
					break;
				case 32:
					result += "Touchpad";
					break;
				case 33:
					result += "Stick";
					break;
				case 1:
					result += "B/Y";
					break;
				case 31:
					result += "A/X";
					break;
				case 9:
					result += "Menu";
					break;
				case 34:
					result += "Shoulder";
					break;
				default:
					result += std::format("Btn:{:d}", input.GetKey());
					break;
				}

				// Append device info if mixed or specific
				switch (input.GetDevice()) {
				case InputDeviceType::Primary:
					result += "(Pri)";
					break;
				case InputDeviceType::Secondary:
					result += "(Sec)";
					break;
				case InputDeviceType::Both:
					result += "(Both)";
					break;
				default:
					break;
				}
			}
		}

		if (result.empty()) {
			return "None";
		}

		return result;
	}

	/**
	 * @brief Wrapper for std::vector<InputCombo> to provide custom JSON serialization.
	 *
	 * Serialization rules for backward compatibility:
	 * - Single keyboard key (no modifiers): saves as plain uint32_t key code
	 * - Single VR/controller input: saves as plain uint32_t packed value
	 * - Multiple keys (combo): saves as array of uint32_t values
	 * - Empty: saves as 0 (unbound)
	 *
	 * This allows users who don't use combo keys to maintain compatibility
	 * with older versions of the software.
	 */
	struct ComboList
	{
		static void to_json(nlohmann::json& j, const std::vector<InputCombo>& combos)
		{
			if (combos.empty()) {
				// Empty/unbound - save as 0 for backward compatibility
				j = 0;
				return;
			}

			// Single input (no combo) - save as single uint32_t for backward compatibility
			if (combos.size() == 1) {
				if (combos[0].GetDevice() == InputDeviceType::Keyboard) {
					// Single keyboard key - save as plain key code
					j = combos[0].GetKey();
				} else {
					// Single VR/controller input - save as packed value
					j = combos[0].deviceAndKey;
				}
				return;
			}

			// Multiple inputs (combo) - save as array
			bool allKeyboardOrMouse = true;
			for (const auto& c : combos) {
				if (c.GetDevice() != InputDeviceType::Keyboard && c.GetDevice() != InputDeviceType::Mouse) {
					allKeyboardOrMouse = false;
					break;
				}
			}

			if (allKeyboardOrMouse) {
				// For keyboard-only combos, serialize as simple key codes for readability
				std::vector<uint32_t> keyCodes;
				keyCodes.reserve(combos.size());
				for (const auto& c : combos) {
					if (c.GetDevice() == InputDeviceType::Keyboard) {
						keyCodes.push_back(c.GetKey());
					} else {
						keyCodes.push_back(c.deviceAndKey);
					}
				}
				j = keyCodes;
			} else {
				// For VR or mixed inputs, use the packed format
				std::vector<uint32_t> packedValues;
				packedValues.reserve(combos.size());
				for (const auto& c : combos) {
					packedValues.push_back(c.deviceAndKey);
				}
				j = packedValues;
			}
		}

		static void from_json(const nlohmann::json& j, std::vector<InputCombo>& combos)
		{
			combos.clear();

			if (j.is_array()) {
				// Array format - multiple inputs in combo
				for (const auto& item : j) {
					if (item.is_number_integer()) {
						uint32_t val = item.get<uint32_t>();
						parseAndAdd(val, combos);
					}
				}
			} else if (j.is_number_integer()) {
				// Single integer format (backward compatibility)
				uint32_t val = j.get<uint32_t>();
				parseAndAdd(val, combos);
			}
			// Other types (null, string, etc.) leave combos empty
		}

	private:
		static void parseAndAdd(uint32_t val, std::vector<InputCombo>& combos)
		{
			if (val == 0) {
				// 0 means unbound, don't add anything
				return;
			}

			if (val < 0x10000) {
				// Simple key code - assume keyboard input
				combos.push_back(InputCombo::Keyboard(val));
			} else {
				// Packed InputCombo with device type in upper bits
				InputCombo c;
				c.deviceAndKey = val;
				combos.push_back(c);
			}
		}
	};
};

// ADL-discoverable JSON serialization for std::vector<InputCombo>
// These are picked up automatically by nlohmann_json when serializing/deserializing
inline void to_json(nlohmann::json& j, const std::vector<InputCombo>& combos)
{
	InputCombo::ComboList::to_json(j, combos);
}

inline void from_json(const nlohmann::json& j, std::vector<InputCombo>& combos)
{
	InputCombo::ComboList::from_json(j, combos);
}

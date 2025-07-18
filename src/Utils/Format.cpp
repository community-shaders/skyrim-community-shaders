#include "Format.h"
#include "Globals.h"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version)
	{
		const auto& v = version.string(".");
		return v.substr(0, v.find_last_of("."));
	}

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines)
	{
		std::string result;
		for (const auto& def : defines) {
			if (def.first != nullptr) {
				result += def.first;
				if (def.second != nullptr && !std::string(def.second).empty()) {
					result += "=";
					result += def.second;
				}
				result += ' ';
			} else {
				break;
			}
		}
		return result;
	}

	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines)
	{
		std::string result;
		for (const auto& def : defines) {
			if (def.Name != nullptr) {
				result += def.Name;
				if (def.Definition != nullptr && !std::string(def.Definition).empty()) {
					result += "=";
					result += def.Definition;
				}
				result += ' ';
			} else {
				break;
			}
		}
		return result;
	}

	std::string FixFilePath(const std::string& a_path)
	{
		std::string lowerFilePath = a_path;

		// Replace all backslashes with forward slashes
		std::replace(lowerFilePath.begin(), lowerFilePath.end(), '\\', '/');

		// Remove consecutive forward slashes
		std::string::iterator newEnd = std::unique(lowerFilePath.begin(), lowerFilePath.end(),
			[](char a, char b) { return a == '/' && b == '/'; });
		lowerFilePath.erase(newEnd, lowerFilePath.end());

		// Convert all characters to lowercase
		std::transform(lowerFilePath.begin(), lowerFilePath.end(), lowerFilePath.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		return lowerFilePath;
	}

	std::string WStringToString(const std::wstring& wideString)
	{
		std::string result;
		std::transform(wideString.begin(), wideString.end(), std::back_inserter(result), [](wchar_t c) {
			return (char)c;
		});
		return result;
	}

	std::string FormatMilliseconds(float ms)
	{
		if (std::abs(ms) < 1e-4f)
			return "0 ms";
		std::ostringstream oss;
		if (ms < 0.1f)
			oss << std::fixed << std::setprecision(3) << ms << " ms";
		else
			oss << std::fixed << std::setprecision(2) << ms << " ms";
		return oss.str();
	}

	std::string FormatMicroseconds(float us)
	{
		if (std::abs(us) < 1e-4f)
			return "0 us";
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(2) << us << " us";
		return oss.str();
	}

	std::string FormatPercent(float percent)
	{
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(1) << percent << "%";
		return oss.str();
	}

	std::string TimeAgoString(std::chrono::steady_clock::time_point last)
	{
		using namespace std::chrono;
		auto now = steady_clock::now();
		auto diff = duration_cast<seconds>(now - last).count();
		if (diff < 60)
			return std::to_string(diff) + "s";
		if (diff < 3600)
			return std::to_string(diff / 60) + "m";
		return std::to_string(diff / 3600) + "h";
	}

	std::string TimeAgoStringQPC(const LARGE_INTEGER& lastTime, const LARGE_INTEGER& frequency)
	{
		if (lastTime.QuadPart == 0) {
			return "0s";
		}

		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);

		// Calculate elapsed seconds
		int64_t elapsedTicks = currentTime.QuadPart - lastTime.QuadPart;
		if (elapsedTicks < 0) {
			return "0s";  // Handle case where clock went backwards
		}

		int64_t elapsedSeconds = elapsedTicks / frequency.QuadPart;

		// Format the same way as TimeAgoString
		if (elapsedSeconds < 60) {
			return std::to_string(elapsedSeconds) + "s";
		} else if (elapsedSeconds < 3600) {
			return std::to_string(elapsedSeconds / 60) + "m";
		} else {
			return std::to_string(elapsedSeconds / 3600) + "h";
		}
	}

	std::string FormatDeltaWithPercent(float a, float b, float threshold)
	{
		float delta = b - a;
		float percentDelta = 0.0f;
		if (a < b && a > 0.0f) {
			percentDelta = 100.0f * (b - a) / a;
		} else if (b < a && b > 0.0f) {
			percentDelta = 100.0f * (a - b) / b;
		}
		std::string percentStr = (percentDelta >= threshold) ? std::format(" ({:+.1f}%)", (b < a ? -percentDelta : percentDelta)) : "";
		return (delta > 0.0f ? "+" : "") + FormatMilliseconds(delta) + percentStr;
	}

	float CalculatePercentage(float part, float total, float defaultValue)
	{
		return (total > 0.0f) ? (part / total * 100.0f) : defaultValue;
	}

	float CalculateCostPerCall(float frameTime, float drawCalls)
	{
		return (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
	}

	float CalculateOtherFrameTime(float totalFrameTime, float measuredSum)
	{
		return totalFrameTime - measuredSum;
	}
}  // namespace Util

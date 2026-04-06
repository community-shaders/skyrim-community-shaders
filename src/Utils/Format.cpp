#include "Format.h"
#include "Globals.h"
#include <algorithm>
#include <cctype>
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

	std::string FormatFileSize(uint64_t bytes)
	{
		if (bytes >= 1024 * 1024) {
			char buffer[32];
			sprintf_s(buffer, "%.1f MB", static_cast<float>(bytes) / (1024 * 1024));
			return buffer;
		} else {
			char buffer[32];
			sprintf_s(buffer, "%.1f KB", static_cast<float>(bytes) / 1024);
			return buffer;
		}
	}

	std::string FormatTimeAgo(std::filesystem::file_time_type fileTime)
	{
		try {
			// Convert filesystem time to system time correctly
			// std::filesystem::file_time_type uses Windows FILETIME epoch (1601-01-01)
			// std::chrono::system_clock uses Unix epoch (1970-01-01)
			// Difference is 11644473600 seconds (number of seconds between 1601-01-01 and 1970-01-01)
			constexpr int64_t WINDOWS_TO_UNIX_EPOCH_SECONDS = 11644473600LL;
			auto fileDuration = fileTime.time_since_epoch();
			auto systemDuration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
				fileDuration - std::chrono::seconds(WINDOWS_TO_UNIX_EPOCH_SECONDS));
			auto systemTime = std::chrono::system_clock::time_point(systemDuration);
			auto fileTimeT = std::chrono::system_clock::to_time_t(systemTime);
			auto nowT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

			// Check if file time is in the future
			if (fileTimeT > nowT) {
				return "Future";
			}

			// Calculate duration in seconds
			auto seconds = static_cast<int64_t>(nowT - fileTimeT);

			// Format based on time difference
			if (seconds < 60) {
				return std::to_string(seconds) + "s ago";
			} else if (seconds < 3600) {
				return std::to_string(seconds / 60) + "m ago";
			} else if (seconds < 86400) {
				return std::to_string(seconds / 3600) + "h ago";
			} else {
				return std::to_string(seconds / 86400) + "d ago";
			}
		} catch (const std::exception&) {
			return "Unknown";
		}
	}

	std::string FormatDuration(double ms)
	{
		// Validate input: handle negative, NaN, and infinite values
		if (!std::isfinite(ms) || ms < 0.0) {
			return "00:00:00";
		}

		// Use int64_t to avoid overflow on long durations (>596 hours with int)
		int64_t total_s = static_cast<int64_t>(ms) / 1000;
		int64_t hours = total_s / 3600;
		int64_t minutes = (total_s % 3600) / 60;
		int64_t seconds = total_s % 60;
		return fmt::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
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
		char buffer[64];
		if (percentDelta >= threshold) {
			sprintf_s(buffer, " (+%.1f%%)", (b < a ? -percentDelta : percentDelta));
		} else {
			buffer[0] = '\0';
		}
		return (delta > 0.0f ? "+" : "") + FormatMilliseconds(delta) + buffer;
	}

	std::string FormatDeltaWithPercent(float delta)
	{
		// Format as percentage with sign
		char buffer[32];
		if (delta >= 0.0f) {
			std::snprintf(buffer, sizeof(buffer), "+%.1f%%", delta);
		} else {
			std::snprintf(buffer, sizeof(buffer), "%.1f%%", delta);
		}
		return buffer;
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

	bool IEquals(std::string_view a, std::string_view b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
										   [](char ca, char cb) {
											   return std::tolower(static_cast<unsigned char>(ca)) == std::tolower(static_cast<unsigned char>(cb));
										   });
	}

	std::string GetShaderDefinesSuffix(const std::string& definesStr)
	{
		if (definesStr.empty())
			return {};
		uint32_t h = 2166136261u;  // FNV-1a 32-bit offset basis
		for (unsigned char c : definesStr) {
			h ^= c;
			h *= 16777619u;  // FNV prime
		}
		return std::format("_{:08X}", h);
	}
}  // namespace Util

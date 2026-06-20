// Based on LegitProfiler by Raikiri (https://github.com/Raikiri/LegitProfiler)
// MIT License - modified to remove glm dependency, using ImVec2 directly.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace legit
{
	namespace Colors
	{
#define RGBA_LE(col) (((col & 0xff000000) >> (3 * 8)) + ((col & 0x00ff0000) >> (1 * 8)) + ((col & 0x0000ff00) << (1 * 8)) + ((col & 0x000000ff) << (3 * 8)))
		const static uint32_t turqoise = RGBA_LE(0x1abc9cffu);
		const static uint32_t greenSea = RGBA_LE(0x16a085ffu);
		const static uint32_t emerald = RGBA_LE(0x2ecc71ffu);
		const static uint32_t nephritis = RGBA_LE(0x27ae60ffu);
		const static uint32_t peterRiver = RGBA_LE(0x3498dbffu);
		const static uint32_t belizeHole = RGBA_LE(0x2980b9ffu);
		const static uint32_t amethyst = RGBA_LE(0x9b59b6ffu);
		const static uint32_t wisteria = RGBA_LE(0x8e44adffu);
		const static uint32_t sunFlower = RGBA_LE(0xf1c40fffu);
		const static uint32_t orange = RGBA_LE(0xf39c12ffu);
		const static uint32_t carrot = RGBA_LE(0xe67e22ffu);
		const static uint32_t pumpkin = RGBA_LE(0xd35400ffu);
		const static uint32_t alizarin = RGBA_LE(0xe74c3cffu);
		const static uint32_t pomegranate = RGBA_LE(0xc0392bffu);
		const static uint32_t clouds = RGBA_LE(0xecf0f1ffu);
		const static uint32_t silver = RGBA_LE(0xbdc3c7ffu);
		const static uint32_t imguiText = RGBA_LE(0xF2F5FAFFu);
#undef RGBA_LE
	}

	/** @brief A single profiling task with timing, name, and display colour. */
	struct ProfilerTask
	{
		double startTime;
		double endTime;
		std::string name;
		uint32_t color;

		/** @brief Get the duration of this task in seconds. */
		double GetLength() { return endTime - startTime; }
	};
}

namespace ImGuiUtils
{
	/**
	 * @brief ImGui widget that renders a scrolling profiler graph with per-frame task bars and a legend.
	 *
	 * Maintains a ring buffer of frame data and renders a stacked bar chart showing per-task
	 * timing alongside a sorted legend of the most significant tasks.
	 */
	class ProfilerGraph
	{
	public:
		int frameWidth;
		int frameSpacing;
		bool useColoredLegendText;

		/**
		 * @brief Construct a profiler graph with a fixed ring buffer size.
		 * @param framesCount Number of frames to keep in the ring buffer.
		 */
		ProfilerGraph(size_t framesCount)
		{
			frames.resize(framesCount);
			for (auto& frame : frames)
				frame.tasks.reserve(100);
			frameWidth = 3;
			frameSpacing = 1;
			useColoredLegendText = false;
		}

		/**
		 * @brief Load task data for the current frame into the ring buffer.
		 *
		 * Adjacent tasks with the same name and colour are merged. Statistics
		 * are rebuilt after insertion.
		 *
		 * @param tasks Array of profiler tasks for this frame.
		 * @param count Number of tasks in the array.
		 */
		void LoadFrameData(const legit::ProfilerTask* tasks, size_t count)
		{
			auto& currFrame = frames[currFrameIndex];
			currFrame.tasks.resize(0);
			currFrame.totalTime = 0.0f;
			for (size_t taskIndex = 0; taskIndex < count; taskIndex++) {
				if (taskIndex == 0)
					currFrame.tasks.push_back(tasks[taskIndex]);
				else {
					if (tasks[taskIndex - 1].color != tasks[taskIndex].color || tasks[taskIndex - 1].name != tasks[taskIndex].name)
						currFrame.tasks.push_back(tasks[taskIndex]);
					else
						currFrame.tasks.back().endTime = tasks[taskIndex].endTime;
				}
				currFrame.totalTime += float(tasks[taskIndex].endTime - tasks[taskIndex].startTime);
			}
			currFrame.taskStatsIndex.resize(currFrame.tasks.size());

			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				auto& task = currFrame.tasks[taskIndex];
				auto it = taskNameToStatsIndex.find(task.name);
				if (it == taskNameToStatsIndex.end()) {
					taskNameToStatsIndex[task.name] = taskStats.size();
					TaskStats taskStat;
					taskStat.maxTime = -1.0;
					taskStats.push_back(taskStat);
				}
				currFrame.taskStatsIndex[taskIndex] = taskNameToStatsIndex[task.name];
			}
			{
				float recentMax = 0.0f;
				size_t lookback = std::min(frames.size(), size_t(120));
				for (size_t i = 0; i < lookback; i++) {
					size_t idx = (currFrameIndex + frames.size() - 1 - i) % frames.size();
					recentMax = std::max(recentMax, frames[idx].totalTime);
				}
				if (peakFrameTime <= 0.0f)
					peakFrameTime = recentMax;
				else {
					float rate = (recentMax < peakFrameTime) ? 0.02f : 0.01f;
					peakFrameTime += (recentMax - peakFrameTime) * rate;
				}
			}
			currFrameIndex = (currFrameIndex + 1) % frames.size();
			RebuildTaskStats(currFrameIndex, 300);
		}

		/**
		 * @brief Get the total task time for a frame at the given offset from the current frame.
		 * @param frameIndexOffset Offset backwards from the current frame index.
		 * @return Total accumulated task time for that frame.
		 */
		float GetTotalTaskTime(int frameIndexOffset)
		{
			return frames[GetCurrFrameIndex(frameIndexOffset)].totalTime;
		}

		/** @brief Get the smoothed peak frame time across recent frames. */
		float GetPeakFrameTime() const { return peakFrameTime; }

		/**
		 * @brief Render the profiler graph and optional legend into the current ImGui window.
		 * @param graphWidth Width of the graph area in pixels.
		 * @param legendWidth Width of the legend area (0 to hide legend).
		 * @param height Height of the entire widget in pixels.
		 * @param frameIndexOffset Offset backwards from the current frame to display.
		 * @param maxFrameTime Time value corresponding to the full graph height.
		 * @param uiScale DPI scale factor for rendering.
		 */
		void RenderTimings(float graphWidth, float legendWidth, float height, int frameIndexOffset, float maxFrameTime, float uiScale = 1.0f)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 widgetPos = ImGui::GetCursorScreenPos();
			RenderGraph(drawList, widgetPos, ImVec2(graphWidth, height), frameIndexOffset, maxFrameTime, uiScale);
			if (legendWidth > 0.0f)
				RenderLegend(drawList, ImVec2(widgetPos.x + graphWidth, widgetPos.y), ImVec2(legendWidth, height), frameIndexOffset, maxFrameTime, uiScale);
			ImGui::Dummy(ImVec2(graphWidth + legendWidth, height));
		}

	private:
		size_t GetCurrFrameIndex(size_t frameIndexOffset)
		{
			return (currFrameIndex - frameIndexOffset - 1 + 2 * frames.size()) % frames.size();
		}

		void RebuildTaskStats(size_t endFrame, size_t framesCount)
		{
			for (auto& taskStat : taskStats) {
				taskStat.maxTime = -1.0f;
				taskStat.priorityOrder = size_t(-1);
				taskStat.onScreenIndex = size_t(-1);
			}

			for (size_t frameNumber = 0; frameNumber < framesCount; frameNumber++) {
				size_t frameIndex = (endFrame - 1 - frameNumber + frames.size()) % frames.size();
				auto& frame = frames[frameIndex];
				for (size_t taskIndex = 0; taskIndex < frame.tasks.size(); taskIndex++) {
					auto& task = frame.tasks[taskIndex];
					auto& stats = taskStats[frame.taskStatsIndex[taskIndex]];
					stats.maxTime = std::max(stats.maxTime, task.endTime - task.startTime);
				}
			}
			std::vector<size_t> statPriorities;
			statPriorities.resize(taskStats.size());
			for (size_t statIndex = 0; statIndex < taskStats.size(); statIndex++)
				statPriorities[statIndex] = statIndex;

			std::sort(statPriorities.begin(), statPriorities.end(), [this](size_t left, size_t right) { return taskStats[left].maxTime > taskStats[right].maxTime; });
			for (size_t statNumber = 0; statNumber < taskStats.size(); statNumber++) {
				size_t statIndex = statPriorities[statNumber];
				taskStats[statIndex].priorityOrder = statNumber;
			}
		}

		void RenderGraph(ImDrawList* drawList, ImVec2 graphPos, ImVec2 graphSize, size_t frameIndexOffset, float maxFrameTime, float uiScale)
		{
			Rect(drawList, graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), 0xffffffff, false);
			const float scaledFrameWidth = std::max(1.0f, static_cast<float>(frameWidth) * uiScale);
			const float scaledFrameSpacing = std::max(1.0f, static_cast<float>(frameSpacing) * uiScale);
			const float heightThreshold = uiScale;

			for (size_t frameNumber = 0; frameNumber < frames.size(); frameNumber++) {
				size_t frameIndex = GetCurrFrameIndex(frameIndexOffset + frameNumber);

				ImVec2 framePos = ImVec2(graphPos.x + graphSize.x - uiScale - scaledFrameWidth - (scaledFrameWidth + scaledFrameSpacing) * float(frameNumber), graphPos.y + graphSize.y - uiScale);
				if (framePos.x < graphPos.x + uiScale)
					break;
				ImVec2 taskPos = framePos;
				auto& frame = frames[frameIndex];
				for (const auto& task : frame.tasks) {
					float taskStartHeight = (float(task.startTime) / maxFrameTime) * graphSize.y;
					float taskEndHeight = (float(task.endTime) / maxFrameTime) * graphSize.y;
					if (std::abs(taskEndHeight - taskStartHeight) > heightThreshold)
						Rect(drawList, ImVec2(taskPos.x, taskPos.y - taskStartHeight), ImVec2(taskPos.x + scaledFrameWidth, taskPos.y - taskEndHeight), task.color, true);
				}
			}
		}

		void RenderLegend(ImDrawList* drawList, ImVec2 legendPos, ImVec2 legendSize, size_t frameIndexOffset, float maxFrameTime, float uiScale)
		{
			float markerLeftRectMargin = 3.0f * uiScale;
			float markerLeftRectWidth = 5.0f * uiScale;
			float markerMidWidth = 30.0f * uiScale;
			float markerRightRectWidth = 10.0f * uiScale;
			float markerRigthRectMargin = 3.0f * uiScale;
			float markerRightRectHeight = 10.0f * uiScale;
			float markerRightRectSpacing = 4.0f * uiScale;
			float nameOffset = 30.0f * uiScale;
			ImVec2 textMargin = ImVec2(5.0f * uiScale, -3.0f * uiScale);

			auto& currFrame = frames[GetCurrFrameIndex(frameIndexOffset)];
			size_t maxTasksCount = size_t(legendSize.y / (markerRightRectHeight + markerRightRectSpacing));

			for (auto& taskStat : taskStats)
				taskStat.onScreenIndex = size_t(-1);

			size_t tasksToShow = std::min<size_t>(taskStats.size(), maxTasksCount);
			size_t tasksShownCount = 0;
			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				auto& task = currFrame.tasks[taskIndex];
				auto& stat = taskStats[currFrame.taskStatsIndex[taskIndex]];

				if (stat.priorityOrder >= tasksToShow)
					continue;

				if (stat.onScreenIndex == size_t(-1))
					stat.onScreenIndex = tasksShownCount++;
				else
					continue;

				float taskStartHeight = (float(task.startTime) / maxFrameTime) * legendSize.y;
				float taskEndHeight = (float(task.endTime) / maxFrameTime) * legendSize.y;

				ImVec2 markerLeftRectMin = ImVec2(legendPos.x + markerLeftRectMargin, legendPos.y + legendSize.y);
				ImVec2 markerLeftRectMax = ImVec2(markerLeftRectMin.x + markerLeftRectWidth, markerLeftRectMin.y);
				markerLeftRectMin.y -= taskStartHeight;
				markerLeftRectMax.y -= taskEndHeight;

				ImVec2 markerRightRectMin = ImVec2(legendPos.x + markerLeftRectMargin + markerLeftRectWidth + markerMidWidth, legendPos.y + legendSize.y - markerRigthRectMargin - (markerRightRectHeight + markerRightRectSpacing) * float(stat.onScreenIndex));
				ImVec2 markerRightRectMax = ImVec2(markerRightRectMin.x + markerRightRectWidth, markerRightRectMin.y - markerRightRectHeight);
				RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.color);

				uint32_t textColor = useColoredLegendText ? task.color : legit::Colors::imguiText;

				float taskTimeMs = float(task.endTime - task.startTime);
				std::ostringstream timeText;
				timeText.precision(2);
				timeText << std::fixed << std::string("[") << (taskTimeMs * 1000.0f);

				Text(drawList, ImVec2(markerRightRectMax.x + textMargin.x, markerRightRectMax.y + textMargin.y), textColor, timeText.str().c_str());
				Text(drawList, ImVec2(markerRightRectMax.x + textMargin.x + nameOffset, markerRightRectMax.y + textMargin.y), textColor, (std::string("ms] ") + task.name).c_str());
			}
		}

		static void Rect(ImDrawList* drawList, ImVec2 minPoint, ImVec2 maxPoint, uint32_t col, bool filled = true)
		{
			if (filled)
				drawList->AddRectFilled(minPoint, maxPoint, col);
			else
				drawList->AddRect(minPoint, maxPoint, col);
		}

		static void Text(ImDrawList* drawList, ImVec2 point, uint32_t col, const char* text)
		{
			drawList->AddText(point, col, text);
		}

		static void RenderTaskMarker(ImDrawList* drawList, ImVec2 leftMinPoint, ImVec2 leftMaxPoint, ImVec2 rightMinPoint, ImVec2 rightMaxPoint, uint32_t col)
		{
			Rect(drawList, leftMinPoint, leftMaxPoint, col, true);
			Rect(drawList, rightMinPoint, rightMaxPoint, col, true);
			std::array<ImVec2, 4> points = {
				ImVec2(leftMaxPoint.x, leftMinPoint.y),
				ImVec2(leftMaxPoint.x, leftMaxPoint.y),
				ImVec2(rightMinPoint.x, rightMaxPoint.y),
				ImVec2(rightMinPoint.x, rightMinPoint.y)
			};
			drawList->AddConvexPolyFilled(points.data(), int(points.size()), col);
		}

		struct FrameData
		{
			std::vector<legit::ProfilerTask> tasks;
			std::vector<size_t> taskStatsIndex;
			float totalTime;
		};

		struct TaskStats
		{
			double maxTime;
			size_t priorityOrder;
			size_t onScreenIndex;
		};

		std::vector<TaskStats> taskStats;
		std::map<std::string, size_t> taskNameToStatsIndex;
		std::vector<FrameData> frames;
		size_t currFrameIndex = 0;
		float peakFrameTime = 0.0f;
	};
}

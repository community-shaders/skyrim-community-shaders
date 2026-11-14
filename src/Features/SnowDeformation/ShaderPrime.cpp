#include "Features/SnowDeformation.h"

#include <functional>

#include "Globals.h"

void SnowDeformation::DataLoaded()
{
	int expected = 0;
	if (!snowPrimeState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
		return;
	snowPrimeThread = std::thread([this] {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		RunShaderPrime();
		snowPrimeState.store(2, std::memory_order_release);
	});
}

SnowDeformation::~SnowDeformation()
{
	if (snowPrimeThread.joinable())
		snowPrimeThread.join();
}

void SnowDeformation::RunShaderPrime()
{
	const auto start = std::chrono::steady_clock::now();

	const std::function<void()> groundSteps[] = {
		[&] { GetShellPSPrepassEarly(); },
		[&] { GetShellPSPrepassAttr(false); },
		[&] { GetShellPSEarlyResolve(); },
		[&] { GetShellPSDeferred(); },
		[&] { GetSnowCS(kNoiseHashCS); },
		[&] { GetShellFillVS(); },
		[&] { GetShellFillPS(); },
		[&] { GetShellFillPSClaim(); },
		[&] { GetSnowCS(kShellTerrainCutCS); },
		[&] { GetSnowCS(kDepthSyncCS); },
		[&] { GetSnowCS(kSssSyncCS); },
		[&] { GetSnowCS(kSssRestoreCS); },
		[&] { GetSnowCS(kTerrainFineCS); },
		[&] { GetSnowCS(kExclusionFieldCS); },
		[&] { GetSnowCS(kBermFieldTiledCS); },
		[&] { GetSnowCS(kUntouchedTilesCS); },
		[&] { GetSnowCS(kDeformationRingCS); },
		[&] { GetSnowCS(kTrackRebaseCS); },
		[&] { GetSnowCS(kDeformationEvolveCS); },
		[&] { GetSnowCS(kDeformationStampCS); },
		[&] { GetSnowCS(kDeformationStampAllCS); },
		[&] { GetSnowCS(kDeformationScanEvolveCS); },
		[&] { GetSnowCS(kDeformationScanSnapshotCS); },
		[&] { GetSnowCS(kDeformationSnapshotCS); },
		[&] { GetSnowCS(kDeformationSnapshotFullCS); },
		[&] { GetSnowCS(kDeformationScanBermCS); },
		[&] { GetSnowCS(kUndulationFieldCS); },
		[&] { GetSnowCS(kWindowFillCS); },
	};
	const std::function<void()> objectSteps[] = {
		[&] { EnsureHeightRasterShaders(); },
	};
	const std::function<void()> effectSteps[] = {
		[] {},
	};

	auto runGroup = [&](const std::function<void()>* a_steps, size_t a_count, int a_phase, const char* a_name) {
		for (size_t i = 0; i < a_count; i++) {
			if (globals::game::quitGame)
				return false;
			a_steps[i]();
		}
		snowPrimePhase.store(a_phase, std::memory_order_release);
		logger::info("[SNOW DEFORMATION] shader prime: {} ready at {:.1f} s", a_name,
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
		return true;
	};

	if (!runGroup(groundSteps, std::size(groundSteps), 1, "ground (snow + computes)"))
		return;
	if (!runGroup(objectSteps, std::size(objectSteps), 2, "object snow"))
		return;
	if (!runGroup(effectSteps, std::size(effectSteps), 3, "effects"))
		return;

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	logger::info("[SNOW DEFORMATION] shader prime done in {:.1f} s", seconds);
}


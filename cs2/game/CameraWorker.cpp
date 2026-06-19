#include "CameraWorker.h"

#include "Game.h"
#include "AppState.h"
#include "../utils/ProcessManager.h"
#include "../utils/Logger.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <windows.h>

namespace CameraWorker
{

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
static std::atomic<bool> s_running{ false };
static std::thread       s_thread;

static std::mutex        s_matrixMutex;
static float             s_latestMatrix[4][4]{};
static std::atomic<bool> s_hasFreshData{ false };

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------

// Validate a view matrix: a real CS2 view matrix has many non-zero entries
// and a non-trivial magnitude. All-zero (DMA cache stale / not in game) or
// near-zero garbage fails this check.
static bool IsLikelyViewMatrix(const float m[4][4])
{
	int   nonZero = 0;
	float absSum  = 0.0f;

	for (int i = 0; i < 16; i++)
	{
		float v = ((const float*)m)[i];

		if (v != 0.0f) nonZero++;
		absSum += std::abs(v);
	}

	return nonZero >= 6 && absSum > 1.0f;
}

// Coarse sleep + spin-wait for sub-millisecond precision.
// std::this_thread::sleep_for/sleep_until has ~1ms granularity on Windows,
// so we sleep until just before the target and spin the rest.
static void PreciseSleepUs(int64_t us)
{
	auto target = std::chrono::steady_clock::now() + std::chrono::microseconds(us);

	std::this_thread::sleep_until(target - std::chrono::microseconds(900));

	while (std::chrono::steady_clock::now() < target)
		_mm_pause();
}

// Pin this thread to a core counted back from the last logical core.
// backFromLastCore = 0 → last core, 1 → second-to-last, etc.
// Also bumps priority so the 500Hz loop is not starved.
static void SetWorkerThreadAffinity(int backFromLastCore)
{
	int cpuCount = (int)std::thread::hardware_concurrency();

	if (cpuCount <= 0) return;

	int targetCore = cpuCount - 1 - backFromLastCore;

	if (targetCore < 0) targetCore = 0;

	DWORD_PTR mask = (DWORD_PTR)1 << targetCore;

	SetThreadAffinityMask(GetCurrentThread(), mask);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}

// ---------------------------------------------------------------------
//  Worker loop — 500Hz matrix reader
// ---------------------------------------------------------------------
static void WorkerLoop()
{
	LOG_INFO("CameraWorker", "Worker thread started (500Hz)");

	// Pin to second-to-last core (DataThread takes the last core).
	SetWorkerThreadAffinity(1);

	constexpr int64_t kIntervalUs = 2000; // 500Hz = 2ms

	while (s_running.load(std::memory_order_acquire))
	{
		if (globalVars::gameState.load() == AppState::EXITING)
			break;

		// Only read when the game is live — outside of RUNNING the matrix
		// address is stale and reads would just rack up DMA failures.
		if (globalVars::gameState.load() == AppState::RUNNING)
		{
			float freshMatrix[4][4];

			if (ProcessMgr.ReadMemory(gGame.GetMatrixAddress(), freshMatrix, 64))
			{
				if (IsLikelyViewMatrix(freshMatrix))
				{
					{
						std::lock_guard<std::mutex> lock(s_matrixMutex);
						memcpy(s_latestMatrix, freshMatrix, sizeof(s_latestMatrix));
					}
					s_hasFreshData.store(true, std::memory_order_release);
				}
				// Failed validation: keep the previous matrix, do not overwrite.
			}
			// Read failure: keep the previous matrix, do not overwrite.
		}

		PreciseSleepUs(kIntervalUs);
	}

	LOG_INFO("CameraWorker", "Worker thread exiting");
}

// ---------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------
void Start()
{
	if (s_running.load(std::memory_order_relaxed)) return;

	s_running.store(true, std::memory_order_release);
	s_hasFreshData.store(false, std::memory_order_relaxed);
	s_thread = std::thread(WorkerLoop);
}

void Stop()
{
	if (!s_running.load(std::memory_order_relaxed)) return;

	s_running.store(false, std::memory_order_release);

	if (s_thread.joinable())
		s_thread.join();
}

void GetLatestMatrix(float out[4][4])
{
	std::lock_guard<std::mutex> lock(s_matrixMutex);
	memcpy(out, s_latestMatrix, sizeof(s_latestMatrix));
}

bool HasFreshData()
{
	return s_hasFreshData.load(std::memory_order_acquire);
}

} // namespace CameraWorker

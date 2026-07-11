#include "Threads.h"

#include "../render/GrenadeHelper.h"
#include "MenuConfig.h"
#include "../config/ConfigSaver.h"
#include "../config/Language.h"
#include "../utils/Logger.h"
#include "../utils/DmaHealth.h"
#include "../utils/StageTimer.h"
#include "intervals.h"
#include "SceneReset.h"
#include "WeaponLookup.h"

#include <winnt.h>
#include <windows.h>
#include <immintrin.h>
#include <thread>
#include <cmath>
#include <set>
#include <chrono>
#include <unordered_map>
#include <algorithm>

// DMA health tracker (DMA health state machine)
DmaHealth::DmaHealthTracker g_dmaHealth;

// ----------------------------------------------------------------------------
// 运行时 VT/IOMMU 拒绝识别（P3 Task 5）
// ----------------------------------------------------------------------------
// 当 DMA 读取持续失败且多轮 Full refresh 无效时，判定为疑似 VT/IOMMU 拒绝。
// 触发后停止 Probe/Repair/Full 重试循环（避免 CPU 空转），弹出一次警告。
namespace VtIommuGuard {
	// Full refresh 轮数计数器：每次触发 Full refresh 递增，读取成功重置为 0。
	// 当 >= 3 时判定为疑似 VT/IOMMU 拒绝。
	inline std::atomic<int> g_fullRefreshRounds{0};
	// 防止重复弹窗
	inline std::atomic<bool> g_warned{false};
	// 停止 DMA 重试循环标志
	inline std::atomic<bool> g_recoveryHalted{false};

	inline void RecordFullRefresh() {
		int rounds = g_fullRefreshRounds.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (rounds >= 3 && !g_warned.exchange(true, std::memory_order_acq_rel)) {
			g_recoveryHalted.store(true, std::memory_order_release);
			std::string msg = lang.dma_error_vtiommu;
			globalVars::g_dmaFailReason = msg;
			LOG_FATAL("Data", "[CRITICAL] Suspected VT/IOMMU denial: {} Full refresh rounds failed. Halting DMA recovery.", rounds);
			// 在 DataThread 中直接弹窗（VT/IOMMU 拒绝时 DataThread 已无法工作）
			MessageBoxA(NULL, msg.c_str(), "CS2-DMA DMA Error", MB_OK | MB_ICONERROR);
		}
	}

	inline void RecordReadSuccess() {
		int prev = g_fullRefreshRounds.exchange(0, std::memory_order_acq_rel);
		if (prev > 0) {
			g_recoveryHalted.store(false, std::memory_order_release);
			g_warned.store(false, std::memory_order_release);
		}
	}
}

// Stage timing counters (stage timer)
std::atomic<int64_t> g_stageMatrixUs{0};
std::atomic<int64_t> g_stageLocalUs{0};
std::atomic<int64_t> g_stageEntitiesUs{0};
std::atomic<int64_t> g_stageScatterUs{0};
std::atomic<int64_t> g_stageWeaponUs{0};
std::atomic<int64_t> g_stageBombUs{0};
std::atomic<int64_t> g_stageProjectileUs{0};

// =====================================================================
//  ConnectionThread 闁?manages game process lifecycle
//
//  States:
//    SEARCHING_GAME    闁?try Attach("cs2.exe") every 1s
//    INITIALIZING_GAME 闁?call InitAddress(), transition to RUNNING
//    RUNNING           闁?periodically check process alive
// =====================================================================

VOID ConnectionThread()
{
	int searchAttempts = 0;

	while (true)
	{
		AppState state = globalVars::gameState.load();

		switch (state)
		{
		case AppState::SEARCHING_GAME:
		{
			searchAttempts++;
			LOG_TRACE("Connection", "Attach attempt #{}", searchAttempts);
			StatusCode status = ProcessMgr.Attach("cs2.exe");
			LOG_TRACE("Connection", "Attach returned {}", (int)status);
			if (status == SUCCEED) {
				LOG_INFO("Connection", "Found cs2.exe (PID: {}) after {} attempts", ProcessMgr.ProcessID, searchAttempts);
				searchAttempts = 0;
				globalVars::gameState.store(AppState::INITIALIZING_GAME);
			} else {
				Sleep(3000);
			}
			break;
		}
		case AppState::INITIALIZING_GAME:
		{
			static int initFailCount = 0;
			LOG_DEBUG("Connection", "Initializing game addresses...");
			if (gGame.InitAddress()) {
				// Post-init validation: verify data is actually accessible
				RequestDmaRefresh(DmaRefreshTier::Full);
				Sleep(500);
				DWORD64 testCtrl = 0;
				if (ProcessMgr.ReadMemory(gGame.GetLocalControllerAddress(), testCtrl) && testCtrl != 0) {
					LOG_INFO("Connection", "Game addresses initialized and validated (ctrl=0x{:X})", testCtrl);
					initFailCount = 0;
					globalVars::gameState.store(AppState::RUNNING);
				} else {
					// Validation failed: client.dll resolved but data not readable yet
					// (module loaded but code section still decrypting). Keep retrying;
					// do NOT force RUNNING with stale data.
					initFailCount++;
					LOG_WARNING("Connection", "Validation failed (ctrl=0x{:X}), retry {} (client.dll not decrypted yet)",
						testCtrl, initFailCount);
					if ((initFailCount % 6) == 0) {
						LOG_WARNING("Connection", "Stuck on init for {} attempts, hard-resetting DMA...", initFailCount);
						ProcessMgr.CloseDMA();
						if (ProcessMgr.InitDMA()) {
							VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
							if (ProcessMgr.Attach("cs2.exe") != StatusCode::SUCCEED) {
								LOG_ERROR("Connection", "Re-attach failed after hard reset");
							}
							g_dmaHealth.NotifyReconnectSuccess();
						} else {
							LOG_ERROR("Connection", "DMA re-init failed after hard reset");
						}
					}
					Sleep(3000);
				}
			} else {
				// InitAddress failed: client.dll and/or engine2.dll not resolved yet
				// (game still loading). Do NOT Detach; stay in INITIALIZING_GAME and retry.
				initFailCount++;
				LOG_DEBUG("Connection", "InitAddress failed (modules not loaded yet), retry {}", initFailCount);
				if ((initFailCount % 6) == 0) {
					LOG_WARNING("Connection", "Stuck on init for {} attempts, hard-resetting DMA...", initFailCount);
					ProcessMgr.CloseDMA();
					if (ProcessMgr.InitDMA()) {
						VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
						if (ProcessMgr.Attach("cs2.exe") != StatusCode::SUCCEED) {
							LOG_ERROR("Connection", "Re-attach failed after hard reset");
						}
						g_dmaHealth.NotifyReconnectSuccess();
					} else {
						LOG_ERROR("Connection", "DMA re-init failed after hard reset");
					}
				}
				Sleep(3000);
			}
			break;
		}
		case AppState::WAITING_DECRYPT:
		{
			LOG_DEBUG("Connection", "Waiting for client.dll decryption...");
			Sleep(3000);
			// HasReadableModule does sync VMMDLL_OPT_REFRESH_ALL before probing,
			// which is required when game loads after DMA software starts
			// (async RequestDmaRefresh leaves VMMDLL_Map_GetModuleFromNameU with stale cache).
			if (ProcessMgr.HasReadableModule(ProcessMgr.ProcessID, "client.dll")) {
				LOG_INFO("Connection", "client.dll accessible, resuming initialization");
				globalVars::gameState.store(AppState::INITIALIZING_GAME);
			} else {
				LOG_WARNING("Connection", "client.dll still not accessible, keep waiting...");
			}
			break;
		}
		case AppState::RUNNING:
		{
			LOG_TRACE("Connection", "Checking process alive...");
			if (!ProcessMgr.IsProcessAlive()) {
				LOG_WARNING("Connection", "Game process lost, searching again...");
				ProcessMgr.Detach();
				Cheats::PublishSnapshot(GameSnapshot{});
				globalVars::gameState.store(AppState::SEARCHING_GAME);
			} else {
				Sleep(2000);
			}
			break;
		}
		default:
			Sleep(100);
			break;
		}
	}
}

// ---------- Thread tuning helpers (P4 Task 13) ----------

// Pin the calling thread to a core counted back from the last logical core
// and bump its priority. backFromLastCore = 0 闁?last core, 1 闁?second-to-last.
// Isolates hot DMA/worker threads from the render thread.
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

// Sub-millisecond sleep: coarse sleep_until (~1ms granularity on Windows)
// followed by a short spin using _mm_pause to hit the target precisely.
static void PreciseSleepUs(int64_t us)
{
	auto target = std::chrono::steady_clock::now() + std::chrono::microseconds(us);

	std::this_thread::sleep_until(target - std::chrono::microseconds(900));

	while (std::chrono::steady_clock::now() < target)
		_mm_pause();
}

// ---------- Validation helpers ----------

static bool IsValidPos(const Vec3& pos)
{
	return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)
		&& std::abs(pos.x) < 50000.f && std::abs(pos.y) < 50000.f && std::abs(pos.z) < 50000.f;
}

static bool IsValidHealth(int hp)
{
	return hp > 0 && hp <= 100;
}

// =====================================================================
//  DataThread 闁?optimized data pipeline
//
//  Optimizations:
//    - Feature-gated scatter: only read fields needed by active menu features
//    - Entity caching: reuse controller data across frames, re-discover every N frames
//    - Dead field removal: spottedMask/aimPunch/shotsFired/fFlags/teamID removed from scatter
//    - Scattered entity discovery: 64 sequential reads 闁?1 scatter batch
//    - On-demand reading: skip entire pipeline when no features enabled
// =====================================================================

VOID DataThread()
{
	// Pin to the last core and raise priority so the DMA pipeline is not
	// starved by the render thread (P4 Task 13).
	SetWorkerThreadAffinity(0);

	// --- Entity cache: persists across frames, avoids re-reading controller data ---
	struct CachedEntity {
		DWORD64 controllerAddr;
		DWORD64 pawnAddr;
		DWORD64 sceneNodeAddr;
		DWORD64 boneArrayAddr;
		CEntity entity;
	};
	constexpr int MAX_ENTITIES = 128;
	std::vector<CachedEntity> entityCache;
	entityCache.reserve(MAX_ENTITIES);

	CEntity localPlayer;
	DWORD64 localPawnAddrCached = 0;
	float matrix[4][4]{};
	int localPlayerIndex = -1;

	// Scatter buffers 闁?only fields actually consumed by render
	struct ScatterBuf {
		BoneJointData bones[CBone::NUM_BONES]{};
		Vec3 pos;
		int health;
		int armor;
		Vec2 viewAngle;
		Vec3 cameraPos;
		float flashDuration;
	};
	static ScatterBuf scatterBuf[MAX_ENTITIES];
	ScatterBuf localBuf{};

	// Per-slot core-field read failure tracking (P3 Task 8).
	// Static so state persists across frames; DataThread is single-threaded.
	std::unordered_map<DWORD64, int> s_coreReadFailStreak;
	std::unordered_map<DWORD64, int64_t> s_coreReadFailSinceUs;
	constexpr int CORE_READ_EVICT_THRESHOLD = 96;
	std::unordered_map<DWORD64, int64_t> s_pawnStaleSinceUs;
	constexpr int64_t PAWN_STALE_GRACE_US = 250000; // 250ms
	std::unordered_map<DWORD64, int64_t> s_boneArrayStaleSinceUs;
	constexpr int64_t BONE_ARRAY_STALE_GRACE_US = 250000; // 250ms

	// Core stale hold: 250ms grace period for invalid core data (DMA jitter tolerance)
	std::unordered_map<DWORD64, int64_t> s_coreStaleSinceUs;
	constexpr int64_t CORE_STALE_HOLD_US = 250000;       // 250ms core stale hold
	constexpr int64_t CORE_STALE_HOLD_AFTER_RESET_US = 1000000; // 1s core stale hold after scene reset
	int64_t s_lastSceneResetUs = 0;                       // timestamp of last scene reset (Task 6)
	// Zero pawn grace: 250ms grace period for transient pawn address == 0 (Task 5)
	std::unordered_map<DWORD64, int64_t> s_zeroPawnSinceUs;
	constexpr int64_t ZERO_PAWN_GRACE_US = 250000;        // 250ms zero pawn grace
	// Hierarchy missing hold: 2.8s grace period for pawn address resolution failure
	std::unordered_map<DWORD64, int64_t> s_hierarchyMissingSinceUs;
	// Partial core detection: detect multi-player core data anomaly, trigger DMA refresh
	int s_partialCoreStreak = 0;
	int64_t s_partialCoreSinceUs = 0;
	int s_partialCoreConfirmedStreak = 0;
	// Zero controller detection: graded DMA refresh when all controllers are 0
	int s_zeroControllerStreak = 0;
	// Population watchdog: graded recovery when entityCache is empty
	int64_t s_zeroPlayerSinceUs = 0;
	bool s_popWatchProbeDone = false;
	bool s_popWatchRepairDone = false;
	bool s_popWatchFullDone = false;
	// Scene reset flag: extends hierarchy missing hold to 4.2s
	bool s_sceneResetRecently = false;
	int64_t s_sceneResetAtUs = 0;
	constexpr int64_t HIERARCHY_MISSING_HOLD_US = 2800000;       // 2.8s hierarchy missing hold
	constexpr int64_t HIERARCHY_MISSING_HOLD_RESET_US = 4200000; // 4.2s after scene reset
	// Controller missing hold: 500ms grace period for transient 0 controller address (DMA jitter)
	std::unordered_map<int, int64_t> s_controllerMissingSinceUs;
	DWORD64 s_lastControllerAddrBySlot[64] = {0};
	constexpr int64_t CONTROLLER_MISSING_HOLD_US = 500000; // 500ms controller missing hold
	constexpr int PARTIAL_CORE_STREAK_THRESHOLD = 60;          // 60 consecutive frames
	constexpr int PARTIAL_CORE_STREAK_RESET_THRESHOLD = 24;    // 24 frames after scene reset
	constexpr int64_t PARTIAL_CORE_TIME_US = 1200000;          // 1.2s
	constexpr int64_t PARTIAL_CORE_TIME_RESET_US = 550000;     // 0.55s after scene reset
	constexpr int CORE_MISSING_TOLERANCE = 1;                   // tolerance for sane core count
	constexpr int64_t SCENE_RESET_RECENT_WINDOW_US = 5000000;    // 5s window for "recent" scene reset
	// Death confirm: require 80ms of health<=0 before confirming death
	std::unordered_map<DWORD64, int> s_deathConfirmCount;
	constexpr int64_t DEATH_CONFIRM_US = 80000;           // 80ms death confirm
	constexpr int DEATH_CONFIRM_THRESHOLD = 8;            // 8 frames (80ms / 10ms)

	// Tiered update frequency 闁?microsecond intervals (P2)
	auto nowUs = []() -> int64_t {
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	};

	int64_t lastDiscoveryUs = 0;
		int64_t lastControllerRefreshUs = 0;
		int64_t lastWeaponUpdateUs = 0;
		int64_t lastWrExtraUs = 0;
		int64_t lastWrSlowUs = 0;
		int64_t lastProjectileScanUs = 0;
		int64_t lastPeriodicRefreshUs = 0;
		int64_t lastPlayerStatusAuxUs = 0;

	int frameCounter = 0; // retained for logging only

	// Consecutive read failure counter 闁?triggers tiered DMA refresh (P3 Task 9)
	// 100闁愁偅澧緍obe, 300闁愁偅澹奺pair, 500闁愁偅澧痷ll (+ InitAddress + reset)
	int consecutiveFailCount = 0;

	// Stale data counter 闁?matrix all-zero means DMA cache may be out of date
	int staleDataCount = 0;
	constexpr int STALE_DATA_THRESHOLD = 2000; // ~2s of all-zero matrix 闁?refresh DMA cache

	while (true)
	{
		try {
			PreciseSleepUs(1000);

			if (globalVars::gameState.load() != AppState::RUNNING) {
				Sleep(100);
				continue;
			}

			// VT/IOMMU 拒绝检测：若已判定为疑似 VT/IOMMU 拒绝，停止 DMA 重试循环
			// （避免 CPU 空转），等待用户处理后重启程序
			if (VtIommuGuard::g_recoveryHalted.load(std::memory_order_acquire)) {
				Sleep(1000);
				continue;
			}

			frameCounter++;

			int64_t now = nowUs(); // frame timestamp, reused throughout this iteration

			// Controller refresh is shared by local player refresh and entity discovery.
			// Compute once per frame so both paths see the same decision.
			bool controllerRefreshDue = (now - lastControllerRefreshUs) >= intervals::kControllerRefreshUs;
			if (controllerRefreshDue) lastControllerRefreshUs = now;

			// --- Compute feature needs from MenuConfig ---
			bool anyESPDraw = MenuConfig::ShowBoxESP || MenuConfig::ShowBoneESP ||
			                  MenuConfig::ShowHealthBar || MenuConfig::ShowWeaponESP ||
			                  MenuConfig::ShowPlayerName || MenuConfig::ShowDistance ||
			                  MenuConfig::ShowEyeRay || MenuConfig::ShowLineToEnemy ||
			                  MenuConfig::ShowHeadDot || MenuConfig::ShowArmorBar ||
			                  MenuConfig::ShowSoundESP ||
			                  MenuConfig::ShowFootstepESP ||
			                  MenuConfig::ShowPlayerFlags ||
			                  MenuConfig::ShowWeaponAmmo || MenuConfig::ShowWeaponIcon ||
			                  MenuConfig::ShowBombTimer || MenuConfig::ShowWorldProjectileTimers ||
			                  MenuConfig::ShowWorldItems || MenuConfig::ShowHealthText ||
			                  MenuConfig::ShowArmorText;
			bool needEntityPipeline = anyESPDraw || MenuConfig::ShowWebRadar || MenuConfig::ShowSpectatorList;
			bool anyFeature = needEntityPipeline || MenuConfig::ShowProjectileESP || MenuConfig::ShowPerfMonitor;

			LOG_TRACE("Data", "frame={} anyESP={} entityPipe={} anyFeat={}", frameCounter, anyESPDraw, needEntityPipeline, anyFeature);

			if (!anyFeature) {
				LOG_TRACE("Data", "No features enabled, sleeping");
				Sleep(50);
				GameSnapshot cleared = Cheats::GetSnapshot();
				cleared.Entities.clear();
				Cheats::PublishSnapshot(cleared);
				continue;
			}

			// Get2DBox uses head bone 闁?bones required for any ESP drawing
			bool needBones = anyESPDraw;
			bool needViewAngle = MenuConfig::ShowEyeRay || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled;
			bool needCameraPos = MenuConfig::ShowEyeRay || MenuConfig::VPKVisibilityCheck;
			bool needWeapon = MenuConfig::ShowWeaponESP || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled || MenuConfig::ShowWeaponAmmo || MenuConfig::ShowWeaponIcon;

			// ------- 1. Read matrix -------
			{
				StageTimer timer(g_stageMatrixUs);
		if (!ProcessMgr.ReadMemory(gGame.GetMatrixAddress(), matrix, 64)) {
			LOG_TRACE("Data", "ReadMemory matrix FAILED (addr=0x{:X})", gGame.GetMatrixAddress());
			consecutiveFailCount++;
			if (consecutiveFailCount >= 500) {
				LOG_WARNING("Data", "{} consecutive read failures, requesting Full DMA refresh", consecutiveFailCount);
				RequestDmaRefresh(DmaRefreshTier::Full);
				gGame.InitAddress();
				SceneReset::BumpSceneReset();
				consecutiveFailCount = 0;
				entityCache.clear();
				s_coreReadFailStreak.clear();
				s_coreReadFailSinceUs.clear();
				s_pawnStaleSinceUs.clear();
				s_boneArrayStaleSinceUs.clear();
				s_coreStaleSinceUs.clear();
				s_deathConfirmCount.clear();
				s_hierarchyMissingSinceUs.clear();
				s_controllerMissingSinceUs.clear();
				s_zeroPawnSinceUs.clear();
				memset(s_lastControllerAddrBySlot, 0, sizeof(s_lastControllerAddrBySlot));
				s_partialCoreStreak = 0;
				s_partialCoreSinceUs = 0;
				s_partialCoreConfirmedStreak = 0;
				s_zeroControllerStreak = 0;
				s_zeroPlayerSinceUs = 0;
				s_popWatchProbeDone = false;
				s_popWatchRepairDone = false;
				s_popWatchFullDone = false;
				s_sceneResetRecently = true;
				s_sceneResetAtUs = now;
				s_lastSceneResetUs = now;
			} else if (consecutiveFailCount >= 300) {
				RequestDmaRefresh(DmaRefreshTier::Repair);
			} else if (consecutiveFailCount >= 100) {
				RequestDmaRefresh(DmaRefreshTier::Probe);
			}
			g_dmaHealth.RecordFailure();
			continue;
		}
		consecutiveFailCount = 0;
		g_dmaHealth.RecordSuccess();
		VtIommuGuard::RecordReadSuccess();

			// Check for stale matrix (all zeros = not in game or DMA cache stale)
			bool matrixAllZero = true;
			for (int i = 0; i < 16; i++) {
				if (((float*)matrix)[i] != 0.0f) { matrixAllZero = false; break; }
			}
			if (matrixAllZero) {
			staleDataCount++;
			if (staleDataCount >= STALE_DATA_THRESHOLD) {
			LOG_WARNING("Data", "Matrix all-zero for {} frames, refreshing DMA cache", staleDataCount);
			RequestDmaRefresh(DmaRefreshTier::Full);
			gGame.UpdateEntityListEntry();
			SceneReset::BumpSceneReset();
			staleDataCount = 0;
			entityCache.clear();
			s_coreReadFailStreak.clear();
			s_coreReadFailSinceUs.clear();
			s_pawnStaleSinceUs.clear();
			s_boneArrayStaleSinceUs.clear();
			s_coreStaleSinceUs.clear();
			s_deathConfirmCount.clear();
			s_hierarchyMissingSinceUs.clear();
			s_controllerMissingSinceUs.clear();
			s_zeroPawnSinceUs.clear();
			memset(s_lastControllerAddrBySlot, 0, sizeof(s_lastControllerAddrBySlot));
			s_partialCoreStreak = 0;
			s_partialCoreSinceUs = 0;
			s_partialCoreConfirmedStreak = 0;
			s_zeroControllerStreak = 0;
			s_zeroPlayerSinceUs = 0;
			s_popWatchProbeDone = false;
			s_popWatchRepairDone = false;
			s_popWatchFullDone = false;
			s_sceneResetRecently = true;
			s_sceneResetAtUs = now;
			s_lastSceneResetUs = now;
		}
			continue;
		}
			staleDataCount = 0;

			memcpy(gGame.View.Matrix, matrix, 64);
			}

			// ------- 2. Read local player addresses -------
			DWORD64 localControllerAddr = 0;
			DWORD64 localPawnAddr = 0;
			{
				StageTimer timer(g_stageLocalUs);
			if (!ProcessMgr.ReadMemory(gGame.GetLocalControllerAddress(), localControllerAddr)) {
				LOG_TRACE("Data", "ReadMemory localController FAILED");
				continue;
			}
			if (!ProcessMgr.ReadMemory(gGame.GetLocalPawnAddress(), localPawnAddr)) {
				LOG_TRACE("Data", "ReadMemory localPawn FAILED");
				continue;
			}

			LOG_TRACE("Data", "Local: ctrl=0x{:X} pawn=0x{:X}", localControllerAddr, localPawnAddr);

			// Detect match entry: pawn 0闁愁偅濮n-zero means player just entered a match
	if (localPawnAddr != 0 && localPawnAddrCached == 0) {
		LOG_INFO("Data", "Player entered match (pawn 0闁?x{:X}), refreshing DMA cache", localPawnAddr);
		RequestDmaRefresh(DmaRefreshTier::Full);
		gGame.UpdateEntityListEntry();
		SceneReset::BumpSceneReset();
		// Reset entity cache to discard stale menu/loading data so the next
		// discovery frame rebuilds the player list from a clean state.
		entityCache.clear();
		s_coreReadFailStreak.clear();
		s_coreReadFailSinceUs.clear();
		s_pawnStaleSinceUs.clear();
		s_boneArrayStaleSinceUs.clear();
		s_coreStaleSinceUs.clear();
		s_deathConfirmCount.clear();
		s_hierarchyMissingSinceUs.clear();
		s_controllerMissingSinceUs.clear();
		s_zeroPawnSinceUs.clear();
		memset(s_lastControllerAddrBySlot, 0, sizeof(s_lastControllerAddrBySlot));
		s_partialCoreStreak = 0;
		s_partialCoreSinceUs = 0;
		s_partialCoreConfirmedStreak = 0;
		s_zeroControllerStreak = 0;
		s_zeroPlayerSinceUs = 0;
		s_popWatchProbeDone = false;
		s_popWatchRepairDone = false;
		s_popWatchFullDone = false;
		s_sceneResetRecently = true;
		s_sceneResetAtUs = now;
	}

		// Local player: full controller read only on address change or periodic refresh
		bool localChanged = (localPawnAddr != localPawnAddrCached);
		if (localChanged || controllerRefreshDue) {
			LOG_TRACE("Data", "Local player refresh (changed={}, periodic={})", localChanged, controllerRefreshDue);
				CEntity newLocal;
				if (!newLocal.UpdateController(localControllerAddr)) {
					LOG_DEBUG("Data", "Local UpdateController FAILED (addr=0x{:X})", localControllerAddr);
					localPawnAddrCached = localPawnAddr; // prevent infinite retry
					continue;
				}
				if (localPawnAddr == 0 || !newLocal.InitPawnAddress(localPawnAddr)) {
					LOG_DEBUG("Data", "Local pawn invalid (addr=0x{:X}), marking dead", localPawnAddr);
					// Player dead or pawn invalid 闁?mark health 0, keep processing
					localPlayer.Pawn.Health = 0;
					localPawnAddrCached = localPawnAddr;
				} else {
					// Carry over WR extra data to avoid flicker
					newLocal.Controller.Money = localPlayer.Controller.Money;
					newLocal.Controller.Color = localPlayer.Controller.Color;
					newLocal.Pawn.HasHelmet = localPlayer.Pawn.HasHelmet;
					newLocal.Pawn.HasDefuser = localPlayer.Pawn.HasDefuser;
					newLocal.Pawn.ModelName = localPlayer.Pawn.ModelName;
					newLocal.Pawn.WeaponName = localPlayer.Pawn.WeaponName;
					newLocal.Pawn.WeaponList = localPlayer.Pawn.WeaponList;
					localPlayer = newLocal;
					localPawnAddrCached = localPawnAddr;
				}
			}
			}

			// ------- 3-7. Entity pipeline (skip when only projectile ESP is on) -------
			if (needEntityPipeline) {

			bool isDiscoveryFrame = ((now - lastDiscoveryUs) >= intervals::kDiscoveryUs) || entityCache.empty();
		if (isDiscoveryFrame) lastDiscoveryUs = now;

			if (isDiscoveryFrame) {
				StageTimer timer(g_stageEntitiesUs);
				// Clear scene reset flag after successful discovery window
				if (s_sceneResetRecently && now - s_sceneResetAtUs > SCENE_RESET_RECENT_WINDOW_US) {
					s_sceneResetRecently = false;
				}
				LOG_TRACE("Data", "--- Discovery frame (cache_size={}) ---", entityCache.size());
			// Refresh DMA cache only when cache is empty (just entered match)
			// or when we detect stale data. Not every frame 闁?too expensive.
			if (entityCache.empty()) {
				RequestDmaRefresh(DmaRefreshTier::Full);
			}

				// Refresh EntityListEntry every discovery frame 闁?the pointer can change
				// between SlowUpdateThread's 10s intervals (round restarts, player joins, etc.)
				gGame.UpdateEntityListEntry();
				DWORD64 listEntry = gGame.GetEntityListEntry();
				if (listEntry == 0) {
					LOG_TRACE("Data", "EntityListEntry is null, skipping");
					continue;
				}

				// CS2 player controllers occupy entity list slots 1-64 (maxPlayers=64).
			// Fixed scan of 64 slots covers all players. The previous adaptive
			// shrinking (based on entityCache.size()) caused a self-reinforcing
			// miss loop: incomplete scan 闁?small cache 闁?small hint 闁?small range.
			constexpr int PLAYER_CONTROLLER_SLOTS = 64;
			int scanCount = PLAYER_CONTROLLER_SLOTS;

				// Scatter-read entity addresses at once
				DWORD64 entityAddresses[MAX_ENTITIES]{};
				{
					VMMDLL_SCATTER_HANDLE addrHandle = ProcessMgr.CreateScatterHandle();
					if (!addrHandle) continue;
					for (int i = 0; i < scanCount; i++)
						ProcessMgr.AddScatterReadRequest(addrHandle, listEntry + (i + 1) * 0x70, &entityAddresses[i], sizeof(DWORD64));
					ProcessMgr.ExecuteReadScatter(addrHandle);
					VMMDLL_Scatter_CloseHandle(addrHandle);
				}

				// Fallback: if Phase 0 scatter returned all zeros, DMA cache is stale
				{
					bool allAddrZero = true;
					for (int i = 0; i < scanCount; i++) {
						if (entityAddresses[i] != 0) { allAddrZero = false; break; }
					}
					if (allAddrZero) {
				LOG_DEBUG("Data", "Phase 0 scatter all-zero, refreshing DMA cache");
				RequestDmaRefresh(DmaRefreshTier::Full);
				SceneReset::BumpSceneReset();
					VMMDLL_SCATTER_HANDLE addrHandle = ProcessMgr.CreateScatterHandle();
					if (addrHandle) {
						for (int i = 0; i < scanCount; i++)
							ProcessMgr.AddScatterReadRequest(addrHandle, listEntry + (i + 1) * 0x70, &entityAddresses[i], sizeof(DWORD64));
						ProcessMgr.ExecuteReadScatter(addrHandle);
						VMMDLL_Scatter_CloseHandle(addrHandle);
					}
				}
				}

				bool controllerRefresh = controllerRefreshDue;
				std::vector<CachedEntity> newCache;
				newCache.reserve(MAX_ENTITIES);
				localPlayerIndex = -1;

				// Build address闁愁偅濮昻dex map for O(1) lookups into old cache
				std::unordered_map<DWORD64, size_t> oldCacheMap;
				oldCacheMap.reserve(entityCache.size() * 2);
				for (size_t ci = 0; ci < entityCache.size(); ci++)
					oldCacheMap[entityCache[ci].controllerAddr] = ci;

				// Identify entities needing refresh vs cached
				struct { int health; int armor; int isAlive; int teamID; DWORD pawn; char name[MAX_PATH]; } ctrlBuf[MAX_ENTITIES]{};
				int refreshSlots[MAX_ENTITIES]{};
				int refreshCount = 0;

				for (int i = 0; i < scanCount; i++) {
				DWORD64 entityAddr = entityAddresses[i];
				if (entityAddr == 0) {
					// Controller missing hold: retain old entity within 500ms grace
					// period to tolerate transient 0 returns from DMA jitter.
					DWORD64 lastAddr = s_lastControllerAddrBySlot[i];
					if (lastAddr != 0) {
						auto oldIt = oldCacheMap.find(lastAddr);
						if (oldIt != oldCacheMap.end()) {
							auto missingIt = s_controllerMissingSinceUs.find(i);
							if (missingIt == s_controllerMissingSinceUs.end()) {
								s_controllerMissingSinceUs[i] = now;
								newCache.push_back(entityCache[oldIt->second]);
								continue;
							} else if (now - missingIt->second < CONTROLLER_MISSING_HOLD_US) {
								newCache.push_back(entityCache[oldIt->second]);
								continue;
							} else {
								s_controllerMissingSinceUs.erase(missingIt);
								s_lastControllerAddrBySlot[i] = 0;
							}
						}
					}
					continue;
				}
				// Address present: clear missing state and update slot memory
				s_controllerMissingSinceUs.erase(i);
				s_lastControllerAddrBySlot[i] = entityAddr;
				if (entityAddr == localControllerAddr) {
					localPlayerIndex = i;
					continue;
				}

				auto it = oldCacheMap.find(entityAddr);
				if (it != oldCacheMap.end() && !controllerRefresh) {
					newCache.push_back(entityCache[it->second]);
				} else {
					refreshSlots[refreshCount++] = i;
				}
			}

				// Phase 1: Scatter-read controller fields (batched to avoid scatter page limit)
				constexpr int CTRL_SCATTER_BATCH = 8;
				if (refreshCount > 0) {
					for (int batchStart = 0; batchStart < refreshCount; batchStart += CTRL_SCATTER_BATCH) {
						int batchEnd = (batchStart + CTRL_SCATTER_BATCH < refreshCount) ? batchStart + CTRL_SCATTER_BATCH : refreshCount;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int r = batchStart; r < batchEnd; r++) {
							int i = refreshSlots[r];
							DWORD64 addr = entityAddresses[i];
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::Health, &ctrlBuf[i].health, sizeof(int));
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::Armor, &ctrlBuf[i].armor, sizeof(int));
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::IsAlive, &ctrlBuf[i].isAlive, sizeof(int));
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::TeamID, &ctrlBuf[i].teamID, sizeof(int));
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::PlayerPawn, &ctrlBuf[i].pawn, sizeof(DWORD));
							ProcessMgr.AddScatterReadRequest(h, addr + Offset::iszPlayerName, ctrlBuf[i].name, MAX_PATH);
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Phase 2: Resolve pawn list sub-entries for alive entities
					// Need the first-level entity list pointer (not the 2nd-level listEntry)
					// to calculate which sub-list each pawn handle belongs to
					DWORD64 entityPawnListEntry = 0;
				if (!ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityPawnListEntry) || entityPawnListEntry == 0) {
				// DMA cache may be stale, refresh and retry once
				RequestDmaRefresh(DmaRefreshTier::Full);
				SceneReset::BumpSceneReset();
				ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityPawnListEntry);
			}

					// DIAG: log Phase 1 scatter results
				if (refreshCount > 0 && (now - lastPeriodicRefreshUs) >= intervals::kPeriodicRefreshUs) {
					lastPeriodicRefreshUs = now;
						int diagAlive = 0, diagDead = 0, diagZero = 0;
						for (int r = 0; r < refreshCount; r++) {
							int i = refreshSlots[r];
							if (ctrlBuf[i].isAlive == 1 && ctrlBuf[i].pawn != 0) diagAlive++;
							else if (ctrlBuf[i].isAlive == 0 && ctrlBuf[i].pawn == 0 && ctrlBuf[i].teamID == 0) diagZero++;
							else diagDead++;
						}
						LOG_DEBUG("Data", "DIAG refresh={}: alive={} dead={} allZero={} (first3 below)",
							refreshCount, diagAlive, diagDead, diagZero);
						int diagCount = (refreshCount < 3) ? refreshCount : 3;
						for (int d = 0; d < diagCount; d++) {
							int di = refreshSlots[d];
							LOG_DEBUG("Data", "DIAG ctrl[{}] addr=0x{:X} alive={} pawn=0x{:X} health={} name='{}'",
								di, entityAddresses[di], ctrlBuf[di].isAlive, ctrlBuf[di].pawn, ctrlBuf[di].health, ctrlBuf[di].name);
						}
						LOG_DEBUG("Data", "DIAG entityPawnListEntry=0x{:X}", entityPawnListEntry);
					}

					DWORD64 subListEntries[MAX_ENTITIES]{};
					int aliveSlots[MAX_ENTITIES]{};
					int aliveCount = 0;

					if (entityPawnListEntry != 0) {
					VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
					if (h) {
						for (int r = 0; r < refreshCount; r++) {
							int i = refreshSlots[r];
							// Resolve pawn for both alive AND dead players.
							// Dead players still need pawn address for WebRadar position updates.
							if (ctrlBuf[i].pawn == 0) continue;
							aliveSlots[aliveCount++] = i;
							ProcessMgr.AddScatterReadRequest(h, entityPawnListEntry + 0x10 + 8 * ((ctrlBuf[i].pawn & 0x7FFF) >> 9), &subListEntries[i], sizeof(DWORD64));
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}
				}

					// Phase 3: Final pawn addresses
					DWORD64 pawnAddresses[MAX_ENTITIES]{};
					{
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (h) {
							for (int a = 0; a < aliveCount; a++) {
								int i = aliveSlots[a];
								if (subListEntries[i] == 0) continue;
								ProcessMgr.AddScatterReadRequest(h, subListEntries[i] + 0x70 * (ctrlBuf[i].pawn & 0x1FF), &pawnAddresses[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}
					}

					// Phase 4+5: GameSceneNode 闁?BoneArray (batched, 1 page per entity)
					constexpr int DISC_BATCH = 6;
					DWORD64 sceneNodes[MAX_ENTITIES]{};
					DWORD64 boneArrays[MAX_ENTITIES]{};
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int a = 0; a < aliveCount; a++) {
						int i = aliveSlots[a];
						if (pawnAddresses[i] == 0) continue;
						if (ctrlBuf[i].isAlive != 1) continue;  // dead players don't need sceneNode
						if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
						ProcessMgr.AddScatterReadRequest(h, pawnAddresses[i] + Offset::GameSceneNode, &sceneNodes[i], sizeof(DWORD64));
							if (++bc >= DISC_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int a = 0; a < aliveCount; a++) {
						int i = aliveSlots[a];
						if (sceneNodes[i] == 0) continue;
						if (ctrlBuf[i].isAlive != 1) continue;  // dead players don't need boneArray
						if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
						ProcessMgr.AddScatterReadRequest(h, sceneNodes[i] + Offset::BoneArray, &boneArrays[i], sizeof(DWORD64));
							if (++bc >= DISC_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Build CachedEntity from scatter results
					for (int a = 0; a < aliveCount; a++) {
						int i = aliveSlots[a];
						DWORD64 addr = entityAddresses[i];
						if (pawnAddresses[i] == 0) {
						// Zero pawn grace: 250ms grace for transient pawn address == 0 (Task 5)
						auto zeroIt = s_zeroPawnSinceUs.find(addr);
						if (zeroIt == s_zeroPawnSinceUs.end()) {
							s_zeroPawnSinceUs[addr] = now;
							zeroIt = s_zeroPawnSinceUs.find(addr);
						}
						bool zeroPawnGraceActive = (now - zeroIt->second < ZERO_PAWN_GRACE_US);

						// Hierarchy missing hold: retain old cache addresses within grace period
						auto oldIt = oldCacheMap.find(addr);
						if (oldIt != oldCacheMap.end()) {
							const auto& old = entityCache[oldIt->second];
							if (old.pawnAddr != 0) {
								// Check grace period (skip hierarchy missing hold while zero pawn grace active)
								if (!zeroPawnGraceActive) {
									int64_t holdUs = s_sceneResetRecently ? HIERARCHY_MISSING_HOLD_RESET_US : HIERARCHY_MISSING_HOLD_US;
									auto missingIt = s_hierarchyMissingSinceUs.find(addr);
									if (missingIt == s_hierarchyMissingSinceUs.end()) {
										// First failure: record timestamp, retain old cache
										s_hierarchyMissingSinceUs[addr] = now;
										// Fall through to build CachedEntity using old addresses
									} else if (now - missingIt->second < holdUs) {
										// Within grace period: retain old cache
										// Fall through to build CachedEntity using old addresses
									} else {
										// Grace period expired: drop entity
										s_hierarchyMissingSinceUs.erase(missingIt);
										continue;
									}
								}

									// Build CachedEntity using old addresses (fallback to old pawnAddr/sceneNodeAddr/boneArrayAddr)
									CEntity ent;
									ent.Controller.Address = entityAddresses[i];
									ent.Controller.Health = ctrlBuf[i].health;
									ent.Controller.Armor = ctrlBuf[i].armor;
									ent.Controller.AliveStatus = ctrlBuf[i].isAlive;
									ent.Controller.TeamID = ctrlBuf[i].teamID;
									ent.Controller.Pawn = ctrlBuf[i].pawn;
									if (memchr(ctrlBuf[i].name, 0, MAX_PATH) && strlen(ctrlBuf[i].name) > 0)
										ent.Controller.PlayerName = ctrlBuf[i].name;
									else
										ent.Controller.PlayerName = "Name_None";

									ent.Pawn.Address = old.pawnAddr;
									ent.Pawn.BoneData.BoneArrayAddress = old.boneArrayAddr;

									CachedEntity ce;
									ce.controllerAddr = entityAddresses[i];
									ce.pawnAddr = old.pawnAddr;
									ce.sceneNodeAddr = old.sceneNodeAddr;
									ce.boneArrayAddr = old.boneArrayAddr;
									ce.entity = ent;

									// Carry over WR extra data from previous cache
									ce.entity.Controller.Money = old.entity.Controller.Money;
									ce.entity.Controller.Color = old.entity.Controller.Color;
									ce.entity.Pawn.HasHelmet = old.entity.Pawn.HasHelmet;
									ce.entity.Pawn.HasDefuser = old.entity.Pawn.HasDefuser;
									ce.entity.Pawn.ModelName = old.entity.Pawn.ModelName;
									ce.entity.Pawn.WeaponName = old.entity.Pawn.WeaponName;
									ce.entity.Pawn.WeaponList = old.entity.Pawn.WeaponList;
									// Carry over last valid position/health for rendering during grace period
									ce.entity.Pawn.Pos = old.entity.Pawn.Pos;
									ce.entity.Pawn.PrevPos = old.entity.Pawn.PrevPos;
									ce.entity.Pawn.Health = old.entity.Pawn.Health;
									ce.entity.Pawn.ScreenPosValid = old.entity.Pawn.ScreenPosValid;

									newCache.push_back(ce);
									continue;
								}
							}
							continue;
					}
					s_hierarchyMissingSinceUs.erase(addr);
					s_zeroPawnSinceUs.erase(addr);  // pawn resolved: clear zero pawn grace (Task 5)

					DWORD64 effectiveBoneArray = boneArrays[i];
					bool isDeadPlayer = (ctrlBuf[i].isAlive != 1);
					if (effectiveBoneArray == 0 && !isDeadPlayer) {
						// boneArray cache fallback (alive players only)
						auto oldBoneIt = oldCacheMap.find(addr);
						if (oldBoneIt != oldCacheMap.end()) {
							DWORD64 oldBoneArr = entityCache[oldBoneIt->second].boneArrayAddr;
							if (oldBoneArr != 0) {
								auto staleIt = s_boneArrayStaleSinceUs.find(addr);
								if (staleIt == s_boneArrayStaleSinceUs.end()) {
									s_boneArrayStaleSinceUs[addr] = now;
									effectiveBoneArray = oldBoneArr;
								} else if (now - staleIt->second < BONE_ARRAY_STALE_GRACE_US) {
									effectiveBoneArray = oldBoneArr;
								} else {
									LOG_DEBUG("Data", "boneArray stale > 250ms for 0x{:X}, dropping bones", addr);
								}
							}
						}
						if (effectiveBoneArray == 0)
							continue;
					} else if (effectiveBoneArray != 0) {
						s_boneArrayStaleSinceUs.erase(addr);
					}
					// Dead players: effectiveBoneArray stays 0 (no bones needed)

						CEntity ent;
						ent.Controller.Address = entityAddresses[i];
						ent.Controller.Health = ctrlBuf[i].health;
						ent.Controller.Armor = ctrlBuf[i].armor;
						ent.Controller.AliveStatus = ctrlBuf[i].isAlive;
						ent.Controller.TeamID = ctrlBuf[i].teamID;
						ent.Controller.Pawn = ctrlBuf[i].pawn;
						if (memchr(ctrlBuf[i].name, 0, MAX_PATH) && strlen(ctrlBuf[i].name) > 0)
							ent.Controller.PlayerName = ctrlBuf[i].name;
						else
							ent.Controller.PlayerName = "Name_None";

						ent.Pawn.Address = pawnAddresses[i];
						ent.Pawn.BoneData.BoneArrayAddress = effectiveBoneArray;

						CachedEntity ce;
						ce.controllerAddr = entityAddresses[i];
						ce.pawnAddr = pawnAddresses[i];
						ce.sceneNodeAddr = sceneNodes[i];
						ce.boneArrayAddr = effectiveBoneArray;
						ce.entity = ent;

						// Carry over WR extra data from previous cache to avoid flicker
						auto oldIt = oldCacheMap.find(entityAddresses[i]);
						if (oldIt != oldCacheMap.end()) {
							const auto& old = entityCache[oldIt->second];
							ce.entity.Controller.Money = old.entity.Controller.Money;
							ce.entity.Controller.Color = old.entity.Controller.Color;
							ce.entity.Pawn.HasHelmet = old.entity.Pawn.HasHelmet;
							ce.entity.Pawn.HasDefuser = old.entity.Pawn.HasDefuser;
							ce.entity.Pawn.ModelName = old.entity.Pawn.ModelName;
							ce.entity.Pawn.WeaponName = old.entity.Pawn.WeaponName;
							ce.entity.Pawn.WeaponList = old.entity.Pawn.WeaponList;
							// Dead players: carry over last valid position for WebRadar.
							// Without this, dead players get default Pos=(0,0,0) and are
							// skipped by WebRadar serialization (position==0 filter).
							if (isDeadPlayer) {
								ce.entity.Pawn.Pos = old.entity.Pawn.Pos;
								ce.entity.Pawn.PrevPos = old.entity.Pawn.PrevPos;
								ce.entity.Pawn.Health = 0;
							}
						}

						newCache.push_back(ce);
					}

					// Retain dead entities from previous cache for WebRadar continuity.
					// Without this, C4 killing everyone 闁?empty cache 闁?empty m_players 闁?"waiting for data".
					// Build set of addresses already in newCache for O(1) lookup
					std::unordered_map<DWORD64, bool> newCacheAddrs;
					for (const auto& nc : newCache)
						newCacheAddrs[nc.controllerAddr] = true;

					for (int r = 0; r < refreshCount; r++) {
						int i = refreshSlots[r];
						DWORD64 addr = entityAddresses[i];
						if (newCacheAddrs.count(addr)) continue;
						auto oldIt2 = oldCacheMap.find(addr);
						if (oldIt2 != oldCacheMap.end()) {
							CachedEntity copy = entityCache[oldIt2->second];
							copy.entity.Pawn.Health = 0;
							copy.entity.Controller.Health = 0;
							newCache.push_back(copy);
						}
					}
				}

				LOG_DEBUG("Data", "Discovery done: cache={} refresh={}", (int)newCache.size(), refreshCount);

			// Zero controller detection: graded DMA refresh
		{
			int validControllers = 0;
			for (int r = 0; r < refreshCount; r++) {
				int i = refreshSlots[r];
				// Valid controller = has non-zero data (not all-zero garbage).
				// Dead players (isAlive=0 but pawn/teamID non-zero) are valid;
				// only all-zero (isAlive=0, pawn=0, teamID=0) indicates DMA read failure.
				if (!(ctrlBuf[i].isAlive == 0 && ctrlBuf[i].pawn == 0 && ctrlBuf[i].teamID == 0))
					validControllers++;
			}

				if (validControllers == 0 && refreshCount > 0) {
					s_zeroControllerStreak++;
					if (s_zeroControllerStreak == 4) {
						LOG_WARNING("Data", "Zero controllers for 4 frames, requesting Probe DMA refresh");
						RequestDmaRefresh(DmaRefreshTier::Probe);
					} else if (s_zeroControllerStreak == 16) {
						LOG_WARNING("Data", "Zero controllers for 16 frames, requesting Repair DMA refresh");
						RequestDmaRefresh(DmaRefreshTier::Repair);
					} else if (s_zeroControllerStreak == 36) {
						LOG_WARNING("Data", "Zero controllers for 36 frames, requesting Full DMA refresh");
						RequestDmaRefresh(DmaRefreshTier::Full);
					}
				} else {
					s_zeroControllerStreak = 0;
				}
			}

			entityCache = std::move(newCache);
		}

			// ------- 4. Scatter read dynamic fields (2-pass: refresh bone addresses every frame) -------
			{
				StageTimer timer(g_stageScatterUs);
				int count = (int)entityCache.size();
				if (count > MAX_ENTITIES) count = MAX_ENTITIES;

				// --- Pass 1: pos, health, viewAngle, cameraPos + fresh BoneArray pointer ---
				// Pawn offsets span 3 pages: health@0x354(page0), pos@0x1588(page1), eyeAngles@0x3DD0(page3)
				// Plus sceneNode BoneArray@0x1D0 = 4 unique pages per entity. Batch=2 闁?8 pages.
				DWORD64 freshBoneArrays[MAX_ENTITIES]{};
				{
					for (int i = 0; i < count; i++) {
						DWORD64 addr = entityCache[i].controllerAddr;
						auto staleIt = s_pawnStaleSinceUs.find(addr);
						if (staleIt != s_pawnStaleSinceUs.end() && (now - staleIt->second) < PAWN_STALE_GRACE_US)
							continue; // 閻庨€涚矙濡炬椽寮甸悢宄版暥濞ｅ洦绻勯弳鈧☉鎾筹攻椤愬ジ寮垫径瀣珡闁轰胶澧楀畵?
						memset(&scatterBuf[i], 0, sizeof(ScatterBuf));
					}
					memset(&localBuf, 0, sizeof(ScatterBuf));

					constexpr int PASS1_BATCH = 2; // pawn spans 3 pages (0x354,0x1588,0x3DD0) + sceneNode = 4 pages/entity
				for (int batchStart = 0; batchStart < count; batchStart += PASS1_BATCH) {
					int batchEnd = (batchStart + PASS1_BATCH < count) ? batchStart + PASS1_BATCH : count;
					VMMDLL_SCATTER_HANDLE handle = ProcessMgr.CreateScatterHandle();
					if (!handle) continue;
					for (int i = batchStart; i < batchEnd; i++) {
						auto& ce = entityCache[i];
						auto& buf = scatterBuf[i];
						DWORD64 pawn = ce.pawnAddr;
						// 位置从 GameSceneNode::m_vecAbsOrigin 读取（与骨骼数据同源），
						// 避免 m_vOldOrigin 在玩家状态切换时卡住导致位置持续异常。
						// sceneNode 为 0 时 fallback 到 pawn::m_vOldOrigin。
						DWORD64 posAddr = (ce.sceneNodeAddr != 0) ? ce.sceneNodeAddr + Offset::vecAbsOrigin : pawn + Offset::Pos;
						ProcessMgr.AddScatterReadRequest(handle, posAddr, &buf.pos, sizeof(Vec3));
						ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::CurrentHealth, &buf.health, sizeof(int));
						ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::PawnArmor, &buf.armor, sizeof(int));
						ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::flFlashDuration, &buf.flashDuration, sizeof(float));
						if (needBones && ce.sceneNodeAddr != 0)
							ProcessMgr.AddScatterReadRequest(handle, ce.sceneNodeAddr + Offset::BoneArray, &freshBoneArrays[i], sizeof(DWORD64));
						if (needViewAngle)
							ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::angEyeAngles, &buf.viewAngle, sizeof(Vec2));
						if (needCameraPos)
							ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::vecLastClipCameraPos, &buf.cameraPos, sizeof(Vec3));
					}
					bool batchOk = ProcessMgr.ExecuteReadScatter(handle);
					VMMDLL_Scatter_CloseHandle(handle);

					// Per-slot fault tolerance (P3 Task 8): on batch failure, retry each
					// slot individually so a single bad pawn doesn't poison the batch.
					if (batchOk) {
						for (int i = batchStart; i < batchEnd; i++) {
							s_coreReadFailStreak.erase(entityCache[i].controllerAddr);
							s_pawnStaleSinceUs.erase(entityCache[i].controllerAddr);
						}
					} else {
						for (int i = batchStart; i < batchEnd; i++) {
							auto& ce = entityCache[i];
							auto& buf = scatterBuf[i];
							DWORD64 pawn = ce.pawnAddr;
							VMMDLL_SCATTER_HANDLE slotHandle = ProcessMgr.CreateScatterHandle();
							if (!slotHandle) {
								if (s_coreReadFailStreak.find(ce.controllerAddr) == s_coreReadFailStreak.end())
									s_coreReadFailSinceUs[ce.controllerAddr] = now;
								s_coreReadFailStreak[ce.controllerAddr]++;
								if (s_pawnStaleSinceUs.find(ce.controllerAddr) == s_pawnStaleSinceUs.end())
									s_pawnStaleSinceUs[ce.controllerAddr] = now;
								continue;
							}
							DWORD64 posAddr = (ce.sceneNodeAddr != 0) ? ce.sceneNodeAddr + Offset::vecAbsOrigin : pawn + Offset::Pos;
							ProcessMgr.AddScatterReadRequest(slotHandle, posAddr, &buf.pos, sizeof(Vec3));
							ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::CurrentHealth, &buf.health, sizeof(int));
							ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::PawnArmor, &buf.armor, sizeof(int));
							ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::flFlashDuration, &buf.flashDuration, sizeof(float));
							if (needBones && ce.sceneNodeAddr != 0)
								ProcessMgr.AddScatterReadRequest(slotHandle, ce.sceneNodeAddr + Offset::BoneArray, &freshBoneArrays[i], sizeof(DWORD64));
							if (needViewAngle)
								ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::angEyeAngles, &buf.viewAngle, sizeof(Vec2));
							if (needCameraPos)
								ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::vecLastClipCameraPos, &buf.cameraPos, sizeof(Vec3));
							bool slotOk = ProcessMgr.ExecuteReadScatter(slotHandle);
							VMMDLL_Scatter_CloseHandle(slotHandle);
							if (slotOk) {
								s_coreReadFailStreak.erase(ce.controllerAddr);
								s_pawnStaleSinceUs.erase(ce.controllerAddr);
							} else {
								if (s_coreReadFailStreak.find(ce.controllerAddr) == s_coreReadFailStreak.end())
									s_coreReadFailSinceUs[ce.controllerAddr] = now;
								s_coreReadFailStreak[ce.controllerAddr]++;
								if (s_pawnStaleSinceUs.find(ce.controllerAddr) == s_pawnStaleSinceUs.end())
									s_pawnStaleSinceUs[ce.controllerAddr] = now;
							}
						}
					}
				}

				// Per-slot eviction: persistent failure 闁?drop pawn, rediscover next discovery frame
				for (int i = 0; i < count; i++) {
					DWORD64 addr = entityCache[i].controllerAddr;
					auto failIt = s_coreReadFailStreak.find(addr);
					if (failIt == s_coreReadFailStreak.end() || failIt->second < CORE_READ_EVICT_THRESHOLD)
						continue;
					auto staleIt = s_pawnStaleSinceUs.find(addr);
					int64_t staleAge = (staleIt != s_pawnStaleSinceUs.end()) ? (now - staleIt->second) : 0;
					if (staleAge < PAWN_STALE_GRACE_US)
						continue; // 閻庨€涚矙濡炬椽寮甸悢宄版暥濞戞挸绉归埞宥夋焻?
					LOG_WARNING("Data", "Evicting entity slot {} (pawn=0x{:X}) after {} core read failures (stale {}ms)",
						i, entityCache[i].pawnAddr, failIt->second, staleAge / 1000);
					entityCache[i].pawnAddr = 0;
					s_coreReadFailStreak.erase(addr);
					s_coreReadFailSinceUs.erase(addr);
					s_pawnStaleSinceUs.erase(addr);
				}

					// Local player dynamic fields (1 page, separate to avoid skip when count==0)
					{
						VMMDLL_SCATTER_HANDLE handle = ProcessMgr.CreateScatterHandle();
						if (handle) {
							DWORD64 lp = localPlayer.Pawn.Address;
							// 位置从 GameSceneNode::m_vecAbsOrigin 读取（与骨骼数据同源），
							// 避免 m_vOldOrigin 在玩家状态切换时卡住导致位置持续异常。
							// sceneNode 为 0 时 fallback 到 pawn::m_vOldOrigin。
							DWORD64 lpPosAddr = (localPlayer.Pawn.BoneData.SceneNodeAddress != 0) ? localPlayer.Pawn.BoneData.SceneNodeAddress + Offset::vecAbsOrigin : lp + Offset::Pos;
							ProcessMgr.AddScatterReadRequest(handle, lpPosAddr, &localBuf.pos, sizeof(Vec3));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::CurrentHealth, &localBuf.health, sizeof(int));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::PawnArmor, &localBuf.armor, sizeof(int));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::flFlashDuration, &localBuf.flashDuration, sizeof(float));
							if (needViewAngle) {
								// Use global dwViewAngles for local player (matches reference KevqDMA).
								// m_angEyeAngles on local pawn can be stale/wrong during spectator mode.
								DWORD64 clientBase = gGame.GetClientDLLAddress();
								if (Offset::ViewAngles && clientBase)
									ProcessMgr.AddScatterReadRequest(handle, clientBase + Offset::ViewAngles, &localBuf.viewAngle, sizeof(Vec2));
								else
									ProcessMgr.AddScatterReadRequest(handle, lp + Offset::angEyeAngles, &localBuf.viewAngle, sizeof(Vec2));
							}
							ProcessMgr.ExecuteReadScatter(handle);
							VMMDLL_Scatter_CloseHandle(handle);
						}
					}
				}

				// Update bone array addresses from fresh reads
				for (int i = 0; i < count; i++) {
					if (freshBoneArrays[i] != 0) {
						entityCache[i].boneArrayAddr = freshBoneArrays[i];
						entityCache[i].entity.Pawn.BoneData.BoneArrayAddress = freshBoneArrays[i];
					}
				}

				// --- Pass 2: bone data from fresh addresses ---
				// VMMDLL scatter silently drops reads when too many unique pages
				// are batched in one ExecuteRead (~8 page limit observed).
				// Split bone reads into batches to stay within the limit.
				constexpr int BONE_SCATTER_BATCH = 6;
				if (needBones) {
					for (int batchStart = 0; batchStart < count; batchStart += BONE_SCATTER_BATCH) {
						int batchEnd = (batchStart + BONE_SCATTER_BATCH < count) ? batchStart + BONE_SCATTER_BATCH : count;
						VMMDLL_SCATTER_HANDLE handle = ProcessMgr.CreateScatterHandle();
						if (!handle) continue;
						for (int i = batchStart; i < batchEnd; i++) {
							if (entityCache[i].boneArrayAddr != 0)
								ProcessMgr.AddScatterReadRequest(handle, entityCache[i].boneArrayAddr, scatterBuf[i].bones, CBone::NUM_BONES * sizeof(BoneJointData));
						}
						ProcessMgr.ExecuteReadScatter(handle);
						VMMDLL_Scatter_CloseHandle(handle);
					}
				}

				// ------- 5. Apply scatter results (world coords only) -------
				// W2S moved to render thread: each render frame reads a fresh ViewMatrix
				// so ESP tracks view rotation at display refresh rate, not DMA update rate.
				LOG_TRACE("Data", "Applying scatter results for {} entities", count);
			for (int i = 0; i < count; i++)
			{
				auto& ce = entityCache[i];
				auto& buf = scatterBuf[i];
				DWORD64 ctrlAddr = ce.controllerAddr;

				if (!IsValidPos(buf.pos) || !IsValidHealth(buf.health)) {
					// Core stale hold: 250ms grace (1s after scene reset) for invalid core data (Task 6)
					int64_t coreStaleHoldUs = (s_lastSceneResetUs != 0 && now - s_lastSceneResetUs < CORE_STALE_HOLD_AFTER_RESET_US)
						? CORE_STALE_HOLD_AFTER_RESET_US : CORE_STALE_HOLD_US;
					auto it = s_coreStaleSinceUs.find(ctrlAddr);
					if (it == s_coreStaleSinceUs.end()) {
						// First failure: record timestamp, retain last valid data
						s_coreStaleSinceUs[ctrlAddr] = now;
						// Don't clear health/pos - retain last valid data for rendering
					} else if (now - it->second < coreStaleHoldUs) {
						// Within grace period: retain last valid data, continue rendering
						// Don't clear health/pos
					} else {
						// Grace period expired: mark as dead/hidden
						ce.entity.Pawn.Health = 0;
						ce.entity.Pawn.ScreenPosValid = false;
						s_coreStaleSinceUs.erase(it);
						s_deathConfirmCount.erase(ctrlAddr);
					}
				} else {
				// Core data valid: clear stale state
				s_coreStaleSinceUs.erase(ctrlAddr);

				// Dead player protection: if player is already dead and stays dead,
				// retain death position for WebRadar. Don't update pos/health/bones.
				// This prevents dead player position from drifting due to DMA read jitter
				// or stale pawn data after death.
				if (ce.entity.Pawn.Health <= 0 && buf.health <= 0) {
					// Player is dead and stays dead: skip position/health update
					continue;
				}

				// Death confirm: require 80ms of health<=0 before confirming death
				if (buf.health <= 0 && ce.entity.Pawn.Health > 0) {
						// Health dropped to 0: start/increment death confirm count
						int& confirmCount = s_deathConfirmCount[ctrlAddr];
						confirmCount++;
						if (confirmCount >= DEATH_CONFIRM_THRESHOLD) {
							// Confirmed dead
							ce.entity.Pawn.PrevPos = ce.entity.Pawn.Pos;
							ce.entity.Pawn.Health = 0;
							ce.entity.Pawn.ScreenPosValid = false;
							ce.entity.Pawn.BoneData.BonePosCount = 0;
							s_deathConfirmCount.erase(ctrlAddr);
						}
						// During confirm period: retain last valid data (don't update health/pos)
					} else {
						// Normal: health > 0 or already dead
						if (buf.health > 0) {
							s_deathConfirmCount.erase(ctrlAddr);
						}
						// Apply normal data (existing logic)
						ce.entity.Pawn.PrevPos = ce.entity.Pawn.Pos;
						ce.entity.Pawn.Pos = buf.pos;
						ce.entity.Pawn.Health = buf.health;
						ce.entity.Pawn.Armor = (buf.armor >= 0 && buf.armor <= 100) ? buf.armor : 0;
						ce.entity.Pawn.FlashDuration = buf.flashDuration;
						ce.entity.Pawn.ScreenPosValid = true; // render thread will refine via W2S

						if (needViewAngle)
							ce.entity.Pawn.ViewAngle = buf.viewAngle;
						if (needCameraPos)
							ce.entity.Pawn.CameraPos = buf.cameraPos;

						// Store bone world positions (W2S done in render thread)
						if (needBones) {
							ce.entity.Pawn.BoneData.BonePosCount = 0;
							for (int j = 0; j < CBone::NUM_BONES; j++)
							{
								const Vec3& bonePos = buf.bones[j].Pos;
								bool valid = std::isfinite(bonePos.x) && std::isfinite(bonePos.y) && std::isfinite(bonePos.z);
								ce.entity.Pawn.BoneData.BonePosList[j] = { bonePos, {0,0}, valid };
								ce.entity.Pawn.BoneData.BonePosCount++;
							}
						}
					}
				}
			}

			// Partial core detection: detect multi-player core data anomaly
			// Note: health=0 is a valid death state, not a read failure.
			// Only health<0 or health>100 indicates corrupt data.
			{
				int pawnCount = 0;
				int saneCoreCount = 0;
				for (int i = 0; i < count; i++) {
					auto& ce = entityCache[i];
					auto& buf = scatterBuf[i];
					if (ce.pawnAddr == 0) continue;
					pawnCount++;
					if (IsValidPos(buf.pos) && buf.health >= 0 && buf.health <= 100)
						saneCoreCount++;
				}

				if (pawnCount >= 2 && saneCoreCount + CORE_MISSING_TOLERANCE < pawnCount) {
					// Partial core detected
					if (s_partialCoreStreak == 0) {
						s_partialCoreSinceUs = now;
					}
					s_partialCoreStreak++;

					int streakThreshold = s_sceneResetRecently ? PARTIAL_CORE_STREAK_RESET_THRESHOLD : PARTIAL_CORE_STREAK_THRESHOLD;
					int64_t timeThreshold = s_sceneResetRecently ? PARTIAL_CORE_TIME_RESET_US : PARTIAL_CORE_TIME_US;

					if (s_partialCoreStreak >= streakThreshold && now - s_partialCoreSinceUs >= timeThreshold) {
						LOG_WARNING("Data", "Partial core detected: {}/{} sane, requesting DMA Repair refresh (streak={})", saneCoreCount, pawnCount, s_partialCoreStreak);
						RequestDmaRefresh(DmaRefreshTier::Repair);
						s_partialCoreStreak = 0;
						s_partialCoreConfirmedStreak = 0;
					}
				} else {
					// All sane or single-player anomaly: reset
					s_partialCoreStreak = 0;
					s_partialCoreConfirmedStreak = 0;
				}
			}

				// Apply local player scatter results
			if (IsValidPos(localBuf.pos)) {
				localPlayer.Pawn.PrevPos = localPlayer.Pawn.Pos;
				localPlayer.Pawn.Pos = localBuf.pos;
				localPlayer.Pawn.Health = localBuf.health;
				localPlayer.Pawn.Armor = (localBuf.armor >= 0 && localBuf.armor <= 100) ? localBuf.armor : 0;
				localPlayer.Pawn.FlashDuration = localBuf.flashDuration;
				if (needViewAngle)
				localPlayer.Pawn.ViewAngle = localBuf.viewAngle;
			}

			// ------- 5b. Extended tactical fields (scoped/defusing/velocity/ping, ~100ms) -------
			if ((now - lastPlayerStatusAuxUs) >= intervals::kPlayerStatusAuxUs) {
				lastPlayerStatusAuxUs = now;
				constexpr int STATUS_BATCH = 4;
				uint8_t scopedBuf[MAX_ENTITIES]{};
				uint8_t defusingBuf[MAX_ENTITIES]{};
				Vec3 velocityBuf[MAX_ENTITIES]{};
			DWORD shotsBuf[MAX_ENTITIES]{};
				static DWORD lastShotsFired[MAX_ENTITIES];
				int fFlagsBuf[MAX_ENTITIES]{};
				uint8_t walkingBuf[MAX_ENTITIES]{};
				uint8_t localScoped = 0, localDefusing = 0;
				int localFFlags = 0; uint8_t localWalking = 0;
				Vec3 localVelocity{};

				for (int batchStart = 0; batchStart < count; batchStart += STATUS_BATCH) {
					int batchEnd = (batchStart + STATUS_BATCH < count) ? batchStart + STATUS_BATCH : count;
					VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
					if (!h) continue;
					for (int i = batchStart; i < batchEnd; i++) {
						auto& ce = entityCache[i];
						if (ce.pawnAddr == 0 || ce.entity.Pawn.Health <= 0) continue;
						if (Offset::bIsScoped)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::bIsScoped, &scopedBuf[i], sizeof(uint8_t));
						if (Offset::bIsDefusing)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::bIsDefusing, &defusingBuf[i], sizeof(uint8_t));
						if (Offset::vecVelocity)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::vecVelocity, &velocityBuf[i], sizeof(Vec3));
						if (MenuConfig::ShowFootstepESP && Offset::fFlags)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::fFlags, &fFlagsBuf[i], sizeof(int));
						if (MenuConfig::ShowFootstepESP && Offset::bIsWalking)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::bIsWalking, &walkingBuf[i], sizeof(uint8_t));
						if (MenuConfig::ShowSoundESP && Offset::iShotsFired)
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::iShotsFired, &shotsBuf[i], sizeof(DWORD));
				}
				if (batchStart == 0 && localPlayer.Pawn.Address != 0 && localPlayer.Pawn.Health > 0) {
						if (Offset::bIsScoped)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::bIsScoped, &localScoped, sizeof(uint8_t));
						if (Offset::bIsDefusing)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::bIsDefusing, &localDefusing, sizeof(uint8_t));
						if (Offset::vecVelocity)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::vecVelocity, &localVelocity, sizeof(Vec3));
						if (MenuConfig::ShowFootstepESP && Offset::fFlags)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::fFlags, &localFFlags, sizeof(int));
						if (MenuConfig::ShowFootstepESP && Offset::bIsWalking)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::bIsWalking, &localWalking, sizeof(uint8_t));
					}
					ProcessMgr.ExecuteReadScatter(h);
					VMMDLL_Scatter_CloseHandle(h);
				}

				for (int i = 0; i < count; i++) {
					auto& ce = entityCache[i];
					if (ce.pawnAddr == 0 || ce.entity.Pawn.Health <= 0) continue;
					if (Offset::bIsScoped)
						ce.entity.Pawn.Scoped = scopedBuf[i] != 0;
					if (Offset::bIsDefusing)
						ce.entity.Pawn.Defusing = defusingBuf[i] != 0;
					if (Offset::vecVelocity)
						ce.entity.Pawn.Velocity = velocityBuf[i];
					if (MenuConfig::ShowFootstepESP && Offset::fFlags)
						ce.entity.Pawn.fFlags = fFlagsBuf[i];
					if (MenuConfig::ShowFootstepESP && Offset::bIsWalking)
						ce.entity.Pawn.IsWalking = walkingBuf[i] != 0;
					// Task 10: Sound ESP 闁?fire ripple when ShotsFired increases.
					if (MenuConfig::ShowSoundESP && Offset::iShotsFired) {
						if (shotsBuf[i] > lastShotsFired[i]) {
							uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now().time_since_epoch()).count();
							ce.entity.Pawn.SoundUntilMs = nowMs + 420;
						}
						lastShotsFired[i] = shotsBuf[i];
					}
				}
				if (localPlayer.Pawn.Address != 0 && localPlayer.Pawn.Health > 0) {
					if (Offset::bIsScoped)
						localPlayer.Pawn.Scoped = localScoped != 0;
					if (Offset::bIsDefusing)
						localPlayer.Pawn.Defusing = localDefusing != 0;
					if (Offset::vecVelocity)
						localPlayer.Pawn.Velocity = localVelocity;
					if (MenuConfig::ShowFootstepESP && Offset::fFlags)
						localPlayer.Pawn.fFlags = localFFlags;
					if (MenuConfig::ShowFootstepESP && Offset::bIsWalking)
						localPlayer.Pawn.IsWalking = localWalking != 0;
				}
			}
		}

			// ------- 6. Weapon names (low frequency, only if feature needs it) -------
		if (needWeapon) {
			StageTimer timer(g_stageWeaponUs);
			if ((now - lastWeaponUpdateUs) >= intervals::kWeaponUpdateUs) {
				lastWeaponUpdateUs = now;
				// --- Active weapon names via scatter batch read ---
					// Collect all player slots (local + alive entities)
					constexpr int MAX_WPN_SLOTS = MAX_ENTITIES + 1;
				DWORD64 wpnPawnAddrs[MAX_WPN_SLOTS]{};
				std::string* wpnNameOut[MAX_WPN_SLOTS]{};
				PlayerPawn* wpnPawns[MAX_WPN_SLOTS]{};
				int wpnSlotCount = 0;
				if (localPlayer.Pawn.Address != 0) {
					wpnPawnAddrs[wpnSlotCount] = localPlayer.Pawn.Address;
					wpnNameOut[wpnSlotCount] = &localPlayer.Pawn.WeaponName;
					wpnPawns[wpnSlotCount] = &localPlayer.Pawn;
					wpnSlotCount++;
				}
				for (auto& ce : entityCache) {
					if (ce.pawnAddr != 0 && ce.entity.Pawn.Health > 0 && wpnSlotCount < MAX_WPN_SLOTS) {
						wpnPawnAddrs[wpnSlotCount] = ce.pawnAddr;
						wpnNameOut[wpnSlotCount] = &ce.entity.Pawn.WeaponName;
						wpnPawns[wpnSlotCount] = &ce.entity.Pawn;
						wpnSlotCount++;
					}
				}
					for (int i = 0; i < wpnSlotCount; i++)
						*wpnNameOut[i] = "Weapon_None";

					if (wpnSlotCount > 0) {
						constexpr int WPN_BATCH = 6;
						DWORD64 wsPtrs[MAX_WPN_SLOTS]{};
						DWORD activeHandles[MAX_WPN_SLOTS]{};
						DWORD64 subLists[MAX_WPN_SLOTS]{};
						DWORD64 weaponAddrs[MAX_WPN_SLOTS]{};
						DWORD64 m_pEntities[MAX_WPN_SLOTS]{};
						DWORD64 nameAddrs[MAX_WPN_SLOTS]{};
						int ammoBuf[MAX_WPN_SLOTS]{};
						uint16_t itemDefIds[MAX_WPN_SLOTS]{};  // item definition index (reliable weapon name lookup)
						for (int i = 0; i < MAX_WPN_SLOTS; i++) ammoBuf[i] = -1;
						char nameBufs[MAX_WPN_SLOTS][64];
						memset(nameBufs, 0, sizeof(nameBufs));

						// Pass 1: read WeaponServices pointers
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++)
								ProcessMgr.AddScatterReadRequest(h, wpnPawnAddrs[i] + Offset::WeaponServices, &wsPtrs[i], sizeof(DWORD64));
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Pass 2: read ActiveWeapon handles
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (wsPtrs[i])
									ProcessMgr.AddScatterReadRequest(h, wsPtrs[i] + Offset::ActiveWeapon, &activeHandles[i], sizeof(DWORD));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Pass 3: read entityListDeref (shared, 1 read) + subLists
						DWORD64 entityListDeref = 0;
						ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityListDeref);
						if (entityListDeref) {
							for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
								int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
								VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
								if (!h) continue;
								for (int i = bs; i < be; i++) {
									if (activeHandles[i] == 0 || activeHandles[i] == 0xFFFFFFFF) continue;
									DWORD idx = activeHandles[i] & 0x7FFF;
									ProcessMgr.AddScatterReadRequest(h, entityListDeref + 0x10 + 8 * (idx >> 9), &subLists[i], sizeof(DWORD64));
								}
								ProcessMgr.ExecuteReadScatter(h);
								VMMDLL_Scatter_CloseHandle(h);
							}
						}

						// Pass 4: read weaponAddrs
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (!subLists[i]) continue;
								DWORD idx = activeHandles[i] & 0x7FFF;
								ProcessMgr.AddScatterReadRequest(h, subLists[i] + 0x70 * (idx & 0x1FF), &weaponAddrs[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Pass 4b: read m_iClip1 (ammo clip) from weapon entities
						if (Offset::iClip1) {
							for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
								int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
								VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
								if (!h) continue;
								for (int i = bs; i < be; i++) {
									if (!weaponAddrs[i]) continue;
									ProcessMgr.AddScatterReadRequest(h, weaponAddrs[i] + Offset::iClip1, &ammoBuf[i], sizeof(int));
								}
								ProcessMgr.ExecuteReadScatter(h);
								VMMDLL_Scatter_CloseHandle(h);
							}
						}

						// Pass 4c: read item definition index (reliable weapon name lookup).
						// m_AttributeManager / m_Item / m_iItemDefinitionIndex are embedded struct offsets,
						// so the final address is weaponAddr + sum of the three offsets (single DMA read).
						if (Offset::AttributeManager && Offset::Item && Offset::ItemDefinitionIndex) {
							DWORD itemDefOffset = Offset::AttributeManager + Offset::Item + Offset::ItemDefinitionIndex;
							for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
								int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
								VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
								if (!h) continue;
								for (int i = bs; i < be; i++) {
									if (!weaponAddrs[i]) continue;
									ProcessMgr.AddScatterReadRequest(h, weaponAddrs[i] + itemDefOffset, &itemDefIds[i], sizeof(uint16_t));
								}
								ProcessMgr.ExecuteReadScatter(h);
								VMMDLL_Scatter_CloseHandle(h);
							}
						}

						// Pass 5a: read m_pEntity (weaponAddr + 0x10)
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (!weaponAddrs[i]) continue;
								ProcessMgr.AddScatterReadRequest(h, weaponAddrs[i] + 0x10, &m_pEntities[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Pass 5b: read nameAddr (m_pEntity + 0x20)
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (!m_pEntities[i]) continue;
								ProcessMgr.AddScatterReadRequest(h, m_pEntities[i] + 0x20, &nameAddrs[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Pass 6: read weapon name strings (64 bytes)
						for (int bs = 0; bs < wpnSlotCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < wpnSlotCount) ? bs + WPN_BATCH : wpnSlotCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (!nameAddrs[i]) continue;
								ProcessMgr.AddScatterReadRequest(h, nameAddrs[i], nameBufs[i], 64);
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}

						// Parse weapon names: prefer item definition index lookup (reliable),
						// fall back to designer name string parsing if item ID missing/invalid.
						for (int i = 0; i < wpnSlotCount; i++) {
							bool resolved = false;
							if (itemDefIds[i] > 0 && itemDefIds[i] < 20000u) {
								if (const char* name = WeaponLookup::WeaponNameFromItemId(itemDefIds[i])) {
									*wpnNameOut[i] = name;
									resolved = true;
								}
							}
							if (!resolved && nameAddrs[i]) {
								nameBufs[i][63] = '\0';
								if (!memchr(nameBufs[i], 0, 64)) continue;
								std::string s(nameBufs[i]);
								if (s.empty()) continue;
								auto pos = s.find("_");
								if (pos == std::string::npos) continue;
								*wpnNameOut[i] = s.substr(pos + 1);
								resolved = true;
							}
							if (resolved && wpnPawns[i] && ammoBuf[i] >= 0)
								wpnPawns[i]->AmmoClip = ammoBuf[i];
						}
					}
				} else {
				const GameSnapshot& cur = Cheats::GetSnapshot();
				localPlayer.Pawn.WeaponName = cur.LocalPlayer.Pawn.WeaponName;
				for (auto& ce : entityCache) {
					for (const auto& old : cur.Entities) {
						if (old.Controller.Address == ce.controllerAddr) {
							ce.entity.Pawn.WeaponName = old.Pawn.WeaponName;
							break;
						}
					}
				}
			}
			}

			// ------- 7. Web Radar extra data -------
		if (MenuConfig::ShowWebRadar || MenuConfig::ShowBombESP || MenuConfig::ShowBombTimer) {
			if ((now - lastWrExtraUs) >= intervals::kWrExtraUs) {
				lastWrExtraUs = now;
				if (MenuConfig::ShowWebRadar) {
				int cnt = (int)entityCache.size();
				if (cnt > MAX_ENTITIES) cnt = MAX_ENTITIES;

					// Phase A: scatter-read pointer fields + color
					DWORD64 moneyPtrs[MAX_ENTITIES]{};
					DWORD64 itemPtrs[MAX_ENTITIES]{};
					int colorBuf[MAX_ENTITIES]{};
					DWORD64 localMoneyPtr = 0;
					DWORD64 localItemPtr = 0;
					int localColor = -1;

					constexpr int WR_BATCH = 4;
					for (int batchStart = 0; batchStart < cnt; batchStart += WR_BATCH) {
						int batchEnd = (batchStart + WR_BATCH < cnt) ? batchStart + WR_BATCH : cnt;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = batchStart; i < batchEnd; i++) {
							auto& ce = entityCache[i];
							if (ce.entity.Pawn.Health <= 0) continue;
							ProcessMgr.AddScatterReadRequest(h, ce.controllerAddr + Offset::MoneyService, &moneyPtrs[i], sizeof(DWORD64));
							ProcessMgr.AddScatterReadRequest(h, ce.pawnAddr + Offset::ItemServices, &itemPtrs[i], sizeof(DWORD64));
							ProcessMgr.AddScatterReadRequest(h, ce.controllerAddr + Offset::CompTeammateColor, &colorBuf[i], sizeof(int));
						}
						if (batchStart == 0 && localPlayer.Controller.Address != 0) {
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Controller.Address + Offset::MoneyService, &localMoneyPtr, sizeof(DWORD64));
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::ItemServices, &localItemPtr, sizeof(DWORD64));
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Controller.Address + Offset::CompTeammateColor, &localColor, sizeof(int));
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Phase B: scatter-read values from resolved pointers
					int moneyBuf[MAX_ENTITIES]{};
					uint8_t helmetBuf[MAX_ENTITIES]{};
					uint8_t defuserBuf[MAX_ENTITIES]{};
					int localMoney = 0;
					uint8_t localHelmet = 0, localDefuser = 0;

					for (int batchStart = 0; batchStart < cnt; batchStart += WR_BATCH) {
						int batchEnd = (batchStart + WR_BATCH < cnt) ? batchStart + WR_BATCH : cnt;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = batchStart; i < batchEnd; i++) {
							if (moneyPtrs[i])
								ProcessMgr.AddScatterReadRequest(h, moneyPtrs[i] + 64, &moneyBuf[i], sizeof(int));
							if (itemPtrs[i]) {
								ProcessMgr.AddScatterReadRequest(h, itemPtrs[i] + Offset::HasDefuser, &defuserBuf[i], sizeof(uint8_t));
								ProcessMgr.AddScatterReadRequest(h, itemPtrs[i] + Offset::HasHelmet, &helmetBuf[i], sizeof(uint8_t));
							}
						}
						if (batchStart == 0) {
							if (localMoneyPtr)
								ProcessMgr.AddScatterReadRequest(h, localMoneyPtr + 64, &localMoney, sizeof(int));
							if (localItemPtr) {
								ProcessMgr.AddScatterReadRequest(h, localItemPtr + Offset::HasDefuser, &localDefuser, sizeof(uint8_t));
								ProcessMgr.AddScatterReadRequest(h, localItemPtr + Offset::HasHelmet, &localHelmet, sizeof(uint8_t));
							}
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Apply player results
					for (int i = 0; i < cnt; i++) {
						entityCache[i].entity.Controller.Money = moneyBuf[i];
						entityCache[i].entity.Controller.Color = colorBuf[i];
						entityCache[i].entity.Pawn.HasHelmet = helmetBuf[i] != 0;
						entityCache[i].entity.Pawn.HasDefuser = defuserBuf[i] != 0;
					}
					localPlayer.Controller.Money = localMoney;
					localPlayer.Controller.Color = localColor;
					localPlayer.Pawn.HasHelmet = localHelmet != 0;
				localPlayer.Pawn.HasDefuser = localDefuser != 0;
				} // end if (MenuConfig::ShowWebRadar) for WebRadar extra data

				// --- Bomb data (every ~50ms) ---
				{
					StageTimer timer(g_stageBombUs);
					BombData bombSnap{};
					DWORD64 clientBase = gGame.GetClientDLLAddress();

					// Planted bomb 闁?dwPlantedC4 is a CUtlVector data ptr, need:
					// 1. Check count byte at (client + dwPlantedC4 - 8)
					// 2. Double deref: *(*(client + dwPlantedC4)) = entity
					DWORD64 plantedEntity = 0;
					uint8_t plantedCount = 0;
					ProcessMgr.ReadMemory<uint8_t>(clientBase + Offset::PlantedC4 - 8, plantedCount);
					if (plantedCount > 0) {
						DWORD64 listDataPtr = 0;
						if (ProcessMgr.ReadMemory<DWORD64>(clientBase + Offset::PlantedC4, listDataPtr) && listDataPtr) {
							ProcessMgr.ReadMemory<DWORD64>(listDataPtr, plantedEntity);
						}
					}
					if (plantedEntity && (plantedEntity >> 48) == 0) {
						uint8_t ticking = 0;
						ProcessMgr.ReadMemory<uint8_t>(plantedEntity + Offset::BombTicking, ticking);
						if (ticking) {
							bombSnap.isPlanted = true;
							// Read bomb fields in one scatter
						float blowTime = 0, defuseCD = 0;
						uint8_t defused = 0, defusing = 0;
						DWORD64 sceneNode = 0;
						DWORD defuserHandle = 0;
						{
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (h) {
								ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::C4Blow, &blowTime, sizeof(float));
								ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::BombDefused, &defused, sizeof(uint8_t));
								ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::BeingDefused, &defusing, sizeof(uint8_t));
								ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::DefuseCountDown, &defuseCD, sizeof(float));
								ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::GameSceneNode, &sceneNode, sizeof(DWORD64));
								if (Offset::PlantedC4_m_hBombDefuser)
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::PlantedC4_m_hBombDefuser, &defuserHandle, sizeof(DWORD));
								ProcessMgr.ExecuteReadScatter(h);
								VMMDLL_Scatter_CloseHandle(h);
							}
						}
							// Read curtime from GlobalVars (offset 0x30 = CurrentTime2, confirmed by RE)
							DWORD64 gvPtr = 0;
							float curtime = 0;
							if (ProcessMgr.ReadMemory<DWORD64>(clientBase + Offset::GlobalVars, gvPtr) && gvPtr) {
								ProcessMgr.ReadMemory<float>(gvPtr + Offset::GlobalVar.CurrentTime2, curtime);
							}
							bombSnap.blowTime = blowTime - curtime;
						if (bombSnap.blowTime < 0) bombSnap.blowTime = 0;
						bombSnap.defuserPawnHandle = defuserHandle & 0xFFFF;
						bombSnap.isDefused = defused != 0;
							bombSnap.isDefusing = defusing != 0;
							bombSnap.defuseTime = defuseCD - curtime;
							if (bombSnap.defuseTime < 0) bombSnap.defuseTime = 0;

							// Read position from scene node
							if (sceneNode && (sceneNode >> 48) == 0) { // sanity: valid usermode ptr
								Vec3 bombPos{};
								ProcessMgr.ReadMemory(sceneNode + Offset::vecAbsOrigin, bombPos);
								bombSnap.x = bombPos.x;
								bombSnap.y = bombPos.y;
								bombSnap.z = bombPos.z;
							}
						}
					}

					// Carried bomb: scan weapon lists for "c4"
					if (!bombSnap.isPlanted) {
						// Check local player
						for (const auto& w : localPlayer.Pawn.WeaponList) {
							if (w == "c4") {
								bombSnap.carrierPawnHandle = localPlayer.Controller.Pawn;
								bombSnap.x = localPlayer.Pawn.Pos.x;
								bombSnap.y = localPlayer.Pawn.Pos.y;
								bombSnap.z = localPlayer.Pawn.Pos.z;
								break;
							}
						}
						// Check other entities
						if (bombSnap.carrierPawnHandle == 0) {
							for (const auto& ce : entityCache) {
								for (const auto& w : ce.entity.Pawn.WeaponList) {
									if (w == "c4") {
										bombSnap.carrierPawnHandle = ce.entity.Controller.Pawn;
										bombSnap.x = ce.entity.Pawn.Pos.x;
										bombSnap.y = ce.entity.Pawn.Pos.y;
										bombSnap.z = ce.entity.Pawn.Pos.z;
										break;
									}
								}
								if (bombSnap.carrierPawnHandle != 0) break;
							}
						}

						// Check if carrier is planting (holding use on bomb site)
						if (bombSnap.carrierPawnHandle != 0 && Offset::C4_m_bIsPlantingViaUse) {
							DWORD64 weaponListPtr = 0;
							if (ProcessMgr.ReadMemory<DWORD64>(clientBase + Offset::WeaponC4, weaponListPtr) && weaponListPtr) {
								DWORD64 weaponEntity = 0;
								ProcessMgr.ReadMemory<DWORD64>(weaponListPtr, weaponEntity);
								if (weaponEntity && (weaponEntity >> 48) == 0) {
									uint8_t isPlanting = 0;
									ProcessMgr.ReadMemory<uint8_t>(weaponEntity + Offset::C4_m_bIsPlantingViaUse, isPlanting);
									bombSnap.isPlanting = isPlanting != 0;
								}
							}
						}
					}

					// Dropped C4: dwWeaponC4 has same CUtlVector layout as dwPlantedC4
					if (!bombSnap.isPlanted && bombSnap.carrierPawnHandle == 0) {
						DWORD64 weaponEntity = 0;
						DWORD64 weaponListPtr = 0;
						if (ProcessMgr.ReadMemory<DWORD64>(clientBase + Offset::WeaponC4, weaponListPtr) && weaponListPtr) {
							ProcessMgr.ReadMemory<DWORD64>(weaponListPtr, weaponEntity);
						}
						if (weaponEntity && (weaponEntity >> 48) == 0) {
							DWORD64 sn = 0;
							ProcessMgr.ReadMemory<DWORD64>(weaponEntity + Offset::GameSceneNode, sn);
							if (sn && (sn >> 48) == 0) {
								Vec3 dropPos{};
								ProcessMgr.ReadMemory(sn + Offset::vecAbsOrigin, dropPos);
								bombSnap.x = dropPos.x;
								bombSnap.y = dropPos.y;
								bombSnap.z = dropPos.z;
							}
						}
					}


					// Store bomb data for snapshot (in-place patch of the live buffer;
				// the next DataThread publish carries it forward via the
				// MapName/Bomb preservation in the main publish path).
				{
					std::unique_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
					int readIdx = Cheats::SnapshotReadIdx.load(std::memory_order_relaxed);
					Cheats::SnapshotBuf[readIdx].Bomb = bombSnap;
				}
				} // end bomb data scope
			}

			if (MenuConfig::ShowWebRadar) {
				// --- Model name + full weapon list (low frequency ~5s) ---
			if ((now - lastWrSlowUs) >= intervals::kWrSlowUs) {
				lastWrSlowUs = now;

				// 閻犲洩顕цぐ?GameRules 闁搞儳鍋涢幃搴ㄦ⒓閼告鍞?
				if (Offset::GameRules && Offset::GameRulesProxy_m_pGameRules) {
					DWORD64 clientBase = gGame.GetClientDLLAddress();
					DWORD64 gameRulesProxy = 0;
					ProcessMgr.ReadMemory<DWORD64>(clientBase + Offset::GameRules, gameRulesProxy);
					if (gameRulesProxy && (gameRulesProxy >> 48) == 0) {
						DWORD64 gameRulesPtr = 0;
						ProcessMgr.ReadMemory<DWORD64>(gameRulesProxy + Offset::GameRulesProxy_m_pGameRules, gameRulesPtr);
						if (gameRulesPtr && (gameRulesPtr >> 48) == 0) {
							uint8_t freezePeriod = 0;
							int32_t roundWinStatus = 0;
							if (Offset::CSGameRules_m_bFreezePeriod)
								ProcessMgr.ReadMemory<uint8_t>(gameRulesPtr + Offset::CSGameRules_m_bFreezePeriod, freezePeriod);
							if (Offset::CSGameRules_m_iRoundWinStatus)
								ProcessMgr.ReadMemory<int32_t>(gameRulesPtr + Offset::CSGameRules_m_iRoundWinStatus, roundWinStatus);

							const char* phase = "live";
							if (freezePeriod) phase = "freezetime";
							else if (roundWinStatus != 0) phase = "over";

							std::unique_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
							int readIdx = Cheats::SnapshotReadIdx.load(std::memory_order_relaxed);
							strncpy(Cheats::SnapshotBuf[readIdx].roundPhase, phase, 15);
							Cheats::SnapshotBuf[readIdx].roundPhase[15] = '\0';
						}
					}
				}

				int cnt = (int)entityCache.size();
				if (cnt > MAX_ENTITIES) cnt = MAX_ENTITIES;

					// ========= Model Name =========
					constexpr int MODEL_BATCH = 6; // 1 page per entity
					// Phase M1: read GameSceneNode pointers (batched)
					DWORD64 sceneNodes[MAX_ENTITIES]{};
					DWORD64 localSceneNode = 0;
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int i = 0; i < cnt; i++) {
							if (!entityCache[i].pawnAddr) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, entityCache[i].pawnAddr + Offset::GameSceneNode, &sceneNodes[i], sizeof(DWORD64));
							if (++bc >= MODEL_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (localPlayer.Pawn.Address) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); }
							if (h) ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::GameSceneNode, &localSceneNode, sizeof(DWORD64));
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase M2: read m_ModelName pointer (batched)
					DWORD64 nameAddrs[MAX_ENTITIES]{};
					DWORD64 localNameAddr = 0;
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int i = 0; i < cnt; i++) {
							if (!sceneNodes[i]) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, sceneNodes[i] + Offset::ModelStateOffset + Offset::ModelNameOffset, &nameAddrs[i], sizeof(DWORD64));
							if (++bc >= MODEL_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (localSceneNode) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); }
							if (h) ProcessMgr.AddScatterReadRequest(h, localSceneNode + Offset::ModelStateOffset + Offset::ModelNameOffset, &localNameAddr, sizeof(DWORD64));
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase M3: read model name strings
					auto readModelStr = [](DWORD64 addr) -> std::string {
						if (!addr) return "";
						char buf[128]{};
						ProcessMgr.ReadMemory(addr, buf, sizeof(buf) - 1);
						buf[127] = '\0';
						std::string s(buf);
						auto slash = s.rfind('/');
						if (slash != std::string::npos) s = s.substr(slash + 1);
						auto dot = s.rfind('.');
						if (dot != std::string::npos) s = s.substr(0, dot);
						return s;
					};

					for (int i = 0; i < cnt; i++)
						entityCache[i].entity.Pawn.ModelName = readModelStr(nameAddrs[i]);
					localPlayer.Pawn.ModelName = readModelStr(localNameAddr);

					// ========= Full Weapon List =========
					constexpr int MAX_WEAPONS_PER_PLAYER = 10;

					constexpr int WEP_BATCH = 6; // 1 page per entity
					// Phase W1: read WeaponServices pointers (batched)
					DWORD64 wsPtrs[MAX_ENTITIES]{};
					DWORD64 localWsPtr = 0;
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int i = 0; i < cnt; i++) {
							if (!entityCache[i].pawnAddr || entityCache[i].entity.Pawn.Health <= 0) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, entityCache[i].pawnAddr + Offset::WeaponServices, &wsPtrs[i], sizeof(DWORD64));
							if (++bc >= WEP_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (localPlayer.Pawn.Address) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); }
							if (h) ProcessMgr.AddScatterReadRequest(h, localPlayer.Pawn.Address + Offset::WeaponServices, &localWsPtr, sizeof(DWORD64));
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase W2: read m_hMyWeapons vector (batched)
					struct WepVec { int count; int pad; DWORD64 dataPtr; };
					WepVec wvBuf[MAX_ENTITIES]{};
					WepVec localWv{};
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int i = 0; i < cnt; i++) {
							if (!wsPtrs[i]) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, wsPtrs[i] + Offset::MyWeapons, &wvBuf[i], sizeof(WepVec));
							if (++bc >= WEP_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (localWsPtr) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); }
							if (h) ProcessMgr.AddScatterReadRequest(h, localWsPtr + Offset::MyWeapons, &localWv, sizeof(WepVec));
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase W3: read weapon handle arrays (batched)
					DWORD handleArrays[MAX_ENTITIES][MAX_WEAPONS_PER_PLAYER]{};
					DWORD localHandles[MAX_WEAPONS_PER_PLAYER]{};
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int i = 0; i < cnt; i++) {
							if (!wvBuf[i].dataPtr || wvBuf[i].count <= 0) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							int wc = wvBuf[i].count;
							if (wc > MAX_WEAPONS_PER_PLAYER) wc = MAX_WEAPONS_PER_PLAYER;
							ProcessMgr.AddScatterReadRequest(h, wvBuf[i].dataPtr, handleArrays[i], wc * sizeof(DWORD));
							if (++bc >= WEP_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (localWv.dataPtr && localWv.count > 0) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); }
							if (h) {
								int wc = localWv.count;
								if (wc > MAX_WEAPONS_PER_PLAYER) wc = MAX_WEAPONS_PER_PLAYER;
								ProcessMgr.AddScatterReadRequest(h, localWv.dataPtr, localHandles, wc * sizeof(DWORD));
							}
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase W4: resolve all weapon names via scatter batch read
					// Collect all weapon handles (entities + local player)
					constexpr int MAX_TOTAL_WEAPONS = (MAX_ENTITIES + 1) * MAX_WEAPONS_PER_PLAYER;
					static DWORD allHandles[MAX_TOTAL_WEAPONS];
					static int slotEntityIdx[MAX_TOTAL_WEAPONS]; // -1 for local player
					int allHandleCount = 0;
					for (int i = 0; i < cnt; i++) {
						int wc = wvBuf[i].count;
						if (wc > MAX_WEAPONS_PER_PLAYER) wc = MAX_WEAPONS_PER_PLAYER;
						for (int w = 0; w < wc && allHandleCount < MAX_TOTAL_WEAPONS; w++) {
							allHandles[allHandleCount] = handleArrays[i][w];
							slotEntityIdx[allHandleCount] = i;
							allHandleCount++;
						}
					}
					{
						int wc = localWv.count;
						if (wc > MAX_WEAPONS_PER_PLAYER) wc = MAX_WEAPONS_PER_PLAYER;
						for (int w = 0; w < wc && allHandleCount < MAX_TOTAL_WEAPONS; w++) {
							allHandles[allHandleCount] = localHandles[w];
							slotEntityIdx[allHandleCount] = -1;
							allHandleCount++;
						}
					}

					// Read entityListDeref (shared, 1 read)
					DWORD64 entityListDeref = 0;
					ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityListDeref);

					// Scatter batch resolve: subList 闁?weaponAddr 闁?m_pEntity 闁?nameAddr 闁?name string
					static DWORD64 subLists[MAX_TOTAL_WEAPONS];
					static DWORD64 weaponAddrs[MAX_TOTAL_WEAPONS];
					static DWORD64 m_pEntities[MAX_TOTAL_WEAPONS];
					static DWORD64 wepNameAddrs[MAX_TOTAL_WEAPONS];
					static char nameBufs[MAX_TOTAL_WEAPONS][64];
					memset(subLists, 0, allHandleCount * sizeof(DWORD64));
					memset(weaponAddrs, 0, allHandleCount * sizeof(DWORD64));
					memset(m_pEntities, 0, allHandleCount * sizeof(DWORD64));
					memset(wepNameAddrs, 0, allHandleCount * sizeof(DWORD64));
					memset(nameBufs, 0, (size_t)allHandleCount * 64);

					constexpr int WPN_BATCH = 6;

					// Pass 1: read subLists
					if (entityListDeref) {
						for (int bs = 0; bs < allHandleCount; bs += WPN_BATCH) {
							int be = (bs + WPN_BATCH < allHandleCount) ? bs + WPN_BATCH : allHandleCount;
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (!h) continue;
							for (int i = bs; i < be; i++) {
								if (allHandles[i] == 0 || allHandles[i] == 0xFFFFFFFF) continue;
								DWORD idx = allHandles[i] & 0x7FFF;
								ProcessMgr.AddScatterReadRequest(h, entityListDeref + 0x10 + 8 * (idx >> 9), &subLists[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}
					}

					// Pass 2: read weaponAddrs
					for (int bs = 0; bs < allHandleCount; bs += WPN_BATCH) {
						int be = (bs + WPN_BATCH < allHandleCount) ? bs + WPN_BATCH : allHandleCount;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = bs; i < be; i++) {
							if (!subLists[i]) continue;
							DWORD idx = allHandles[i] & 0x7FFF;
							ProcessMgr.AddScatterReadRequest(h, subLists[i] + 0x70 * (idx & 0x1FF), &weaponAddrs[i], sizeof(DWORD64));
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Pass 3a: read m_pEntities (weaponAddr + 0x10)
					for (int bs = 0; bs < allHandleCount; bs += WPN_BATCH) {
						int be = (bs + WPN_BATCH < allHandleCount) ? bs + WPN_BATCH : allHandleCount;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = bs; i < be; i++) {
							if (!weaponAddrs[i]) continue;
							ProcessMgr.AddScatterReadRequest(h, weaponAddrs[i] + 0x10, &m_pEntities[i], sizeof(DWORD64));
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Pass 3b: read wepNameAddrs (m_pEntity + 0x20)
					for (int bs = 0; bs < allHandleCount; bs += WPN_BATCH) {
						int be = (bs + WPN_BATCH < allHandleCount) ? bs + WPN_BATCH : allHandleCount;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = bs; i < be; i++) {
							if (!m_pEntities[i]) continue;
							ProcessMgr.AddScatterReadRequest(h, m_pEntities[i] + 0x20, &wepNameAddrs[i], sizeof(DWORD64));
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Pass 4: read name strings (64 bytes)
					for (int bs = 0; bs < allHandleCount; bs += WPN_BATCH) {
						int be = (bs + WPN_BATCH < allHandleCount) ? bs + WPN_BATCH : allHandleCount;
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (!h) continue;
						for (int i = bs; i < be; i++) {
							if (!wepNameAddrs[i]) continue;
							ProcessMgr.AddScatterReadRequest(h, wepNameAddrs[i], nameBufs[i], 64);
						}
						ProcessMgr.ExecuteReadScatter(h);
						VMMDLL_Scatter_CloseHandle(h);
					}

					// Parse names into per-entity weapon lists (same logic as resolveWeaponName)
					std::vector<std::string> entityWeapons[MAX_ENTITIES];
					std::vector<std::string> localWeapons;
					for (int i = 0; i < allHandleCount; i++) {
						if (!wepNameAddrs[i]) continue;
						nameBufs[i][63] = '\0';
						if (!memchr(nameBufs[i], 0, 64)) continue;
						std::string s(nameBufs[i]);
						if (s.empty()) continue;
						auto pos = s.find("_");
						if (pos == std::string::npos) continue;
						std::string name = s.substr(pos + 1);
						if (slotEntityIdx[i] >= 0)
							entityWeapons[slotEntityIdx[i]].push_back(name);
						else
							localWeapons.push_back(name);
					}
					for (int i = 0; i < cnt; i++)
					entityCache[i].entity.Pawn.WeaponList = std::move(entityWeapons[i]);
				localPlayer.Pawn.WeaponList = std::move(localWeapons);
			}
			}
			} // end if (MenuConfig::ShowWebRadar) for slow data + model name

			// Population watchdog: graded recovery when entityCache is empty
			if (entityCache.empty()) {
				if (s_zeroPlayerSinceUs == 0) {
					s_zeroPlayerSinceUs = now;
					s_popWatchProbeDone = false;
					s_popWatchRepairDone = false;
					s_popWatchFullDone = false;
				}

				int64_t zeroDur = now - s_zeroPlayerSinceUs;

				if (zeroDur >= 2000000 && !s_popWatchFullDone) {
					LOG_WARNING("Data", "Zero players for {}ms, requesting Full DMA refresh + recovery", zeroDur / 1000);
					s_coreReadFailStreak.clear();
					s_coreReadFailSinceUs.clear();
					s_pawnStaleSinceUs.clear();
					s_boneArrayStaleSinceUs.clear();
					s_coreStaleSinceUs.clear();
					s_deathConfirmCount.clear();
					s_hierarchyMissingSinceUs.clear();
					s_controllerMissingSinceUs.clear();
					s_zeroPawnSinceUs.clear();
					memset(s_lastControllerAddrBySlot, 0, sizeof(s_lastControllerAddrBySlot));
					s_partialCoreStreak = 0;
					s_partialCoreSinceUs = 0;
					s_partialCoreConfirmedStreak = 0;
					RequestDmaRefresh(DmaRefreshTier::Full);
					s_popWatchFullDone = true;
				} else if (zeroDur >= 800000 && !s_popWatchRepairDone) {
					LOG_WARNING("Data", "Zero players for {}ms, requesting Repair DMA refresh", zeroDur / 1000);
					s_coreReadFailStreak.clear();
					s_coreReadFailSinceUs.clear();
					s_pawnStaleSinceUs.clear();
					s_boneArrayStaleSinceUs.clear();
					s_coreStaleSinceUs.clear();
					s_deathConfirmCount.clear();
					s_hierarchyMissingSinceUs.clear();
					s_controllerMissingSinceUs.clear();
					s_zeroPawnSinceUs.clear();
					memset(s_lastControllerAddrBySlot, 0, sizeof(s_lastControllerAddrBySlot));
					s_partialCoreStreak = 0;
					s_partialCoreSinceUs = 0;
					s_partialCoreConfirmedStreak = 0;
					RequestDmaRefresh(DmaRefreshTier::Repair);
					s_popWatchRepairDone = true;
				} else if (zeroDur >= 300000 && !s_popWatchProbeDone) {
					LOG_WARNING("Data", "Zero players for {}ms, requesting Probe DMA refresh", zeroDur / 1000);
					RequestDmaRefresh(DmaRefreshTier::Probe);
					s_popWatchProbeDone = true;
				}
			} else {
				s_zeroPlayerSinceUs = 0;
				s_popWatchProbeDone = false;
				s_popWatchRepairDone = false;
				s_popWatchFullDone = false;
			}

			} else {
				// Only projectile ESP on: read local player pos for distance calc, skip entity pipeline
				entityCache.clear();
				if (localPlayer.Pawn.Address != 0) {
					Vec3 lpPos{};
					if (ProcessMgr.ReadMemory(localPlayer.Pawn.Address + Offset::Pos, lpPos) && IsValidPos(lpPos))
						localPlayer.Pawn.Pos = lpPos;
				}
			}

			// ------- 8. Grenade projectile scanning (adaptive interval + sharded discovery + 3-tier cache) -------
	static std::vector<GrenadeProjectile> projectileCache;
	// Task 12/16: dropped-weapon cache, published to GameSnapshot each frame.
	static std::vector<DroppedWeapon> droppedWeaponCache;
	static std::set<DWORD64> expiredEntities;

	// P5 Task 14: Sharded discovery state
	static int s_worldDiscoveryShard = 0;
	static int s_lastWorldEntityCount = 0;
	static int s_worldIdleStreak = 0;

	// P5 Task 15: Three-tier cache (skip DMA reads for unchanged entities)
	static DWORD64 s_cachedProjEntityAddrs[960] = {};
	static DWORD64 s_cachedProjSceneNodes[960] = {};
	static Vec3 s_cachedProjPositions[960] = {};
	static uint8_t s_cachedProjTypes[960] = {};
	// Task 6: cached thrower team (resolved from m_hThrower 闁?pawn 闁?m_iTeamNum)
	static DWORD s_cachedProjThrowers[960] = {};
	static int s_cachedProjTeams[960] = {};
	// C_Inferno entity flag (distinguishes inferno from molotov_projectile,
	// both share PROJ_MOLOTOV type but only inferno carries fire data).
	static bool s_cachedProjIsInferno[960] = {};
	// Task 12/16: cached weapon item-id for dropped-weapon cache-hit reuse.
	// Without this, weapons are only identified on cache-miss (first frame)
	// and vanish on subsequent cache-hit frames because droppedWeaponCache
	// is rebuilt from scratch each scan.
	static uint16_t s_cachedWeaponItemIds[960] = {};
	// Cached owner handle for dropped-weapon filtering.
	// m_hOwnerEntity == 0xFFFFFFFF means the weapon is dropped (no owner).
	// When the handle points to a player, the weapon is still held and must be skipped.
	static DWORD s_cachedWeaponOwnerHandles[960] = {};
	static uint64_t s_projCacheResetSerial = 0;

	if (MenuConfig::ShowProjectileESP || MenuConfig::ShowWorldProjectileTimers || MenuConfig::ShowWorldItems || MenuConfig::ShowWebRadar) {
		StageTimer timer(g_stageProjectileUs);
		LOG_TRACE("Data", "Projectile ESP scan (cache={})", projectileCache.size());

		// P5 Task 15: Invalidate cache on scene reset
		uint64_t currentSerial = SceneReset::CurrentSerial();
		if (currentSerial != s_projCacheResetSerial) {
			memset(s_cachedProjEntityAddrs, 0, sizeof(s_cachedProjEntityAddrs));
			memset(s_cachedProjSceneNodes, 0, sizeof(s_cachedProjSceneNodes));
			memset(s_cachedProjPositions, 0, sizeof(s_cachedProjPositions));
			memset(s_cachedProjTypes, 0, sizeof(s_cachedProjTypes));
			memset(s_cachedProjThrowers, 0, sizeof(s_cachedProjThrowers));
		memset(s_cachedProjTeams, 0, sizeof(s_cachedProjTeams));
		memset(s_cachedProjIsInferno, 0, sizeof(s_cachedProjIsInferno));
		memset(s_cachedWeaponItemIds, 0, sizeof(s_cachedWeaponItemIds));
		memset(s_cachedWeaponOwnerHandles, 0xFF, sizeof(s_cachedWeaponOwnerHandles));
		s_projCacheResetSerial = currentSerial;
		}

		// P5 Task 14: Adaptive interval + shard count based on entity count
		int64_t worldScanUs = intervals::kWorldScanBaseUs;
		int shardCount = 1;
		if (s_lastWorldEntityCount <= 800) {
			worldScanUs = intervals::kWorldScanBaseUs;
			shardCount = 1;
		} else if (s_lastWorldEntityCount <= 1200) {
			worldScanUs = intervals::kWorldScanHighUs;
			shardCount = 2;
		} else if (s_lastWorldEntityCount <= 2000) {
			worldScanUs = intervals::kWorldScanMedUs;
			shardCount = 4;
		} else {
			worldScanUs = intervals::kWorldScanLowUs;
			shardCount = 10;
		}

		// P5 Task 14: Idle detection 闁?double interval after 3 consecutive empty scans
		if (s_worldIdleStreak >= 3) {
			worldScanUs *= 2;
		}

		if ((now - lastProjectileScanUs) >= worldScanUs) {
			lastProjectileScanUs = now;
			std::vector<GrenadeProjectile> newProjectiles;
			// Task 12/16: dropped weapons discovered in this scan shard.
			std::vector<DroppedWeapon> newDroppedWeapons;

					// Scan entities across chunk 0 (64-511) and chunk 1 (512-1023)
					DWORD64 entityListPtr = 0;
					ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityListPtr);
					if (entityListPtr != 0) {
						constexpr int SCAN_START = 64;
						constexpr int SCAN_END = 1024;
						constexpr int SCAN_COUNT = SCAN_END - SCAN_START;

						// Read sub-list pointers for chunk 0 and chunk 1
						DWORD64 chunkPtrs[2]{};
						{
							VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
							if (h) {
								ProcessMgr.AddScatterReadRequest(h, entityListPtr + 0x10, &chunkPtrs[0], sizeof(DWORD64));
								ProcessMgr.AddScatterReadRequest(h, entityListPtr + 0x18, &chunkPtrs[1], sizeof(DWORD64));
								ProcessMgr.ExecuteReadScatter(h);
								VMMDLL_Scatter_CloseHandle(h);
							}
						}

						// P5 Task 14: Compute shard range for this scan
					int slotsPerShard = SCAN_COUNT / shardCount;
					if (slotsPerShard < 1) slotsPerShard = 1;
					int shardStart = s_worldDiscoveryShard * slotsPerShard;
					int shardEnd = (s_worldDiscoveryShard == shardCount - 1) ? SCAN_COUNT : shardStart + slotsPerShard;
					if (shardEnd > SCAN_COUNT) shardEnd = SCAN_COUNT;

					// Phase 1: Scatter-read entity addresses for current shard only
					DWORD64 entAddrs[SCAN_COUNT]{};
					int nonZeroCount = 0;
					{
						VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
						if (h) {
							for (int i = shardStart; i < shardEnd; i++) {
								int idx = SCAN_START + i;
								int chunk = idx / 512;
								int slot = idx % 512;
								if (chunk < 2 && chunkPtrs[chunk] != 0)
									ProcessMgr.AddScatterReadRequest(h, chunkPtrs[chunk] + (DWORD64)slot * 0x70, &entAddrs[i], sizeof(DWORD64));
							}
							ProcessMgr.ExecuteReadScatter(h);
							VMMDLL_Scatter_CloseHandle(h);
						}
						for (int i = shardStart; i < shardEnd; i++) {
							if (entAddrs[i] != 0) nonZeroCount++;
						}
					}
					s_lastWorldEntityCount = nonZeroCount;

					// P5 Task 15: Cache check 闁?split slots into cache-hit and cache-miss
					std::vector<int> cacheMissIdx;
					for (int i = shardStart; i < shardEnd; i++) {
						if (entAddrs[i] != 0 && entAddrs[i] == s_cachedProjEntityAddrs[i]) {
							// Cache hit 闁?reuse cached type/sceneNode/position
						} else {
							if (entAddrs[i] != 0) cacheMissIdx.push_back(i);
							s_cachedProjEntityAddrs[i] = entAddrs[i];
							s_cachedProjSceneNodes[i] = 0;
							s_cachedProjPositions[i] = Vec3{};
							s_cachedProjTypes[i] = 0;
							s_cachedProjIsInferno[i] = false;
						s_cachedWeaponItemIds[i] = 0;
						s_cachedWeaponOwnerHandles[i] = 0xFFFFFFFF;
					}
					}

						// Phases 2-4: random address reads, batch by actual non-zero count.
						// P5 Task 15: Only read cache-miss slots (unchanged entities skip DMA)
						constexpr int PROJ_RAND_BATCH = 6;

						DWORD64 identityPtrs[SCAN_COUNT]{};
						DWORD64 namePtrs[SCAN_COUNT]{};
						char nameStrings[SCAN_COUNT][40]{};

						if (!cacheMissIdx.empty()) {
							// Phase 2: Scatter-read identity pointers for cache-miss entities
							{
								VMMDLL_SCATTER_HANDLE h = nullptr;
								int bc = 0;
								for (int i : cacheMissIdx) {
									if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
									ProcessMgr.AddScatterReadRequest(h, entAddrs[i] + Offset::EntityIdentity, &identityPtrs[i], sizeof(DWORD64));
									if (++bc >= PROJ_RAND_BATCH) {
										ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
									}
								}
								if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
							}

							// Phase 3: Scatter-read designer name pointers
							{
								VMMDLL_SCATTER_HANDLE h = nullptr;
								int bc = 0;
								for (int i : cacheMissIdx) {
									if (identityPtrs[i] == 0) continue;
									if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
									ProcessMgr.AddScatterReadRequest(h, identityPtrs[i] + Offset::DesignerName, &namePtrs[i], sizeof(DWORD64));
									if (++bc >= PROJ_RAND_BATCH) {
										ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
									}
								}
								if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
							}

							// Phase 4: Scatter-read designer name strings
							{
								VMMDLL_SCATTER_HANDLE h = nullptr;
								int bc = 0;
								for (int i : cacheMissIdx) {
									if (namePtrs[i] == 0) continue;
									if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
									ProcessMgr.AddScatterReadRequest(h, namePtrs[i], nameStrings[i], 39);
									if (++bc >= PROJ_RAND_BATCH) {
										ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
									}
								}
								if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
							}
						}

					// Phase 5: Identify grenade projectiles (cache-miss slots)
				struct ProjCandidate { int idx; GrenadeProjectileType type; float radius; bool fromCache; bool isInferno; };
				std::vector<ProjCandidate> candidates;
					// Task 12/16: dropped-weapon candidates (designer name "weapon_*").
					struct WeaponCandidate { int idx; uint16_t itemId; const char* name; };
					std::vector<WeaponCandidate> weaponCandidates;
					for (int i : cacheMissIdx) {
						nameStrings[i][39] = '\0';
						if (nameStrings[i][0] == '\0') continue;
						const char* n = nameStrings[i];
						GrenadeProjectileType type = PROJ_UNKNOWN;
						float radius = 0;
						if (strstr(n, "smokegrenade_projectile"))        { type = PROJ_SMOKE;   radius = 0.f; }
						else if (strstr(n, "flashbang_projectile"))       { type = PROJ_FLASH;   radius = 0.f; }
						else if (strstr(n, "hegrenade_projectile"))       { type = PROJ_HE;      radius = 350.f; }
						else if (strstr(n, "molotov_projectile") || strstr(n, "incendiarygrenade_proj")) { type = PROJ_MOLOTOV; radius = 150.f; }
						else if (strstr(n, "decoy_projectile"))           { type = PROJ_DECOY;   radius = 0.f; }
					else if (strstr(n, "inferno"))                    { type = PROJ_MOLOTOV; radius = 150.f; }
						if (type == PROJ_UNKNOWN) {
							// Task 12/16: check for dropped weapons. The designer
							// name is "weapon_<visualKey>" (e.g. "weapon_ak47");
							// strip the prefix and reverse-lookup the entry.
							if (std::strncmp(n, "weapon_", 7) == 0) {
								const char* visualKey = n + 7;
								const WeaponLookup::WeaponLookupEntry* entry =
								WeaponLookup::FindWeaponLookupEntryByVisualKey(visualKey);
							if (entry) {
								weaponCandidates.push_back({ i, entry->id, entry->name });
								s_cachedWeaponItemIds[i] = entry->id;
							}
							}
							continue;
						}
						candidates.push_back({ i, type, radius, false, (type == PROJ_MOLOTOV) && (strstr(n, "inferno") != nullptr) });
					s_cachedProjTypes[i] = static_cast<uint8_t>(type) + 1;
					s_cachedProjIsInferno[i] = (type == PROJ_MOLOTOV) && (strstr(n, "inferno") != nullptr);
					}

					// Phase 5c: Read item definition index for cache-miss entities.
					// Designer name string reads are unreliable across 3 DMA hops;
					// item definition index is a single numeric read and is the
					// primary weapon identifier (matches player weapon scanning).
					if (Offset::AttributeManager && Offset::Item && Offset::ItemDefinitionIndex) {
						DWORD itemDefOffset = Offset::AttributeManager + Offset::Item + Offset::ItemDefinitionIndex;
						uint16_t itemDefIds[SCAN_COUNT]{};
						{
							VMMDLL_SCATTER_HANDLE h = nullptr;
							int bc = 0;
							for (int i : cacheMissIdx) {
								if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
								ProcessMgr.AddScatterReadRequest(h, entAddrs[i] + itemDefOffset, &itemDefIds[i], sizeof(uint16_t));
								if (++bc >= PROJ_RAND_BATCH) {
									ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
								}
							}
							if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
						}
						// Identify weapons by item definition index. Skips entities
						// already identified via designer name (avoids duplicates).
						std::set<int> alreadyWeapon;
						for (auto& w : weaponCandidates) alreadyWeapon.insert(w.idx);
						for (int i : cacheMissIdx) {
							if (alreadyWeapon.count(i)) continue;
							uint16_t itemId = itemDefIds[i];
							if (itemId == 0 || itemId >= 20000u) continue;
							const WeaponLookup::WeaponLookupEntry* entry =
								WeaponLookup::FindWeaponLookupEntry(itemId);
							if (entry) {
								weaponCandidates.push_back({ i, entry->id, entry->name });
								s_cachedWeaponItemIds[i] = itemId;
							}
						}
					}

					// Phase 5e: Read m_hOwnerEntity for all weapon candidates (cache-miss + cache-hit).
					// m_hOwnerEntity == 0xFFFFFFFF means the weapon is dropped (no owner);
					// a valid handle means a player is holding it and it must be skipped.
					// Mirrors reference project's world_process_entities.inl owner logic.
					if (Offset::OwnerEntity && !weaponCandidates.empty()) {
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (auto& w : weaponCandidates) {
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, entAddrs[w.idx] + Offset::OwnerEntity, &s_cachedWeaponOwnerHandles[w.idx], sizeof(DWORD));
							if (++bc >= PROJ_RAND_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

						// Phase 5b: Add cache-hit slots that were projectiles last scan
						for (int i = shardStart; i < shardEnd; i++) {
							if (entAddrs[i] != 0 && entAddrs[i] == s_cachedProjEntityAddrs[i] && s_cachedProjTypes[i] != 0) {
								GrenadeProjectileType type = static_cast<GrenadeProjectileType>(s_cachedProjTypes[i] - 1);
								float radius = 0;
								if (type == PROJ_HE) radius = 350.f;
								else if (type == PROJ_MOLOTOV) radius = 150.f;
								candidates.push_back({ i, type, radius, true, s_cachedProjIsInferno[i] });
							}
						}

						// Phase 5d: Add cache-hit slots that were weapons last scan.
						// Mirrors Phase 5b for projectiles: when the entity address
						// is unchanged (cache-hit), reuse the cached weapon item-id
						// instead of re-reading designer name / item definition index.
						// Without this, weapons vanish after the first frame because
						// droppedWeaponCache is rebuilt from scratch each scan.
						for (int i = shardStart; i < shardEnd; i++) {
							if (entAddrs[i] != 0 && entAddrs[i] == s_cachedProjEntityAddrs[i] && s_cachedWeaponItemIds[i] != 0) {
								uint16_t itemId = s_cachedWeaponItemIds[i];
								const WeaponLookup::WeaponLookupEntry* entry = WeaponLookup::FindWeaponLookupEntry(itemId);
								if (entry) {
									weaponCandidates.push_back({ i, entry->id, entry->name });
								}
							}
						}

						if (!candidates.empty() || !weaponCandidates.empty()) {
						// Phase 6: Scatter-read GameSceneNode for cache-miss candidates (batched)
						// P5 Task 15: cache-hit candidates reuse s_cachedProjSceneNodes
						// Task 12/16: weapon candidates share the same scene-node cache
						// slots (a slot is never both a grenade and a weapon).
							{
								VMMDLL_SCATTER_HANDLE h = nullptr;
								int bc = 0;
								for (auto& c : candidates) {
								if (c.fromCache) continue;
								if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
								ProcessMgr.AddScatterReadRequest(h, entAddrs[c.idx] + Offset::GameSceneNode, &s_cachedProjSceneNodes[c.idx], sizeof(DWORD64));
								if (++bc >= PROJ_RAND_BATCH) {
									ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
								}
							}
							for (auto& w : weaponCandidates) {
								if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
								ProcessMgr.AddScatterReadRequest(h, entAddrs[w.idx] + Offset::GameSceneNode, &s_cachedProjSceneNodes[w.idx], sizeof(DWORD64));
								if (++bc >= PROJ_RAND_BATCH) {
									ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
								}
							}
							if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
						}

						// Phase 7: Scatter-read positions for all candidates (batched)
						// 投掷物位置必须每帧重读，不能因缓存命中跳过，否则位置永不更新
						{
							VMMDLL_SCATTER_HANDLE h = nullptr;
							int bc = 0;
							for (auto& c : candidates) {
								if (s_cachedProjSceneNodes[c.idx] == 0) continue;
								if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
								ProcessMgr.AddScatterReadRequest(h, s_cachedProjSceneNodes[c.idx] + Offset::vecAbsOrigin, &s_cachedProjPositions[c.idx], sizeof(Vec3));
								if (++bc >= PROJ_RAND_BATCH) {
									ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
								}
							}
							for (auto& w : weaponCandidates) {
								if (s_cachedProjSceneNodes[w.idx] == 0) continue;
								if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
								ProcessMgr.AddScatterReadRequest(h, s_cachedProjSceneNodes[w.idx] + Offset::vecAbsOrigin, &s_cachedProjPositions[w.idx], sizeof(Vec3));
								if (++bc >= PROJ_RAND_BATCH) {
									ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
								}
							}
							if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
						}

					// Phase 7b (Task 6): Read m_hThrower for cache-miss candidates.
					// Cache-hit candidates reuse s_cachedProjThrowers. The inferno
					// fire count/positions are read fresh in the build phase below
					// because fire spreads over time.
					if (Offset::GrenadeThrower) {
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (auto& c : candidates) {
							if (c.fromCache) continue;
							if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
							ProcessMgr.AddScatterReadRequest(h, entAddrs[c.idx] + Offset::GrenadeThrower, &s_cachedProjThrowers[c.idx], sizeof(DWORD));
							if (++bc >= PROJ_RAND_BATCH) {
								ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
							}
						}
						if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
					}

					// Phase 7c (Task 6): Resolve m_hThrower handle 闁?pawn address 闁?m_iTeamNum.
					// Projectile count is small (typically 0-10), so we resolve via the entity
					// list the same way weapon handles are resolved (see Phase 3 weapon reads).
					if (Offset::GrenadeThrower && Offset::iTeamNum) {
						DWORD64 entityListDeref = 0;
						ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityListDeref);
						if (entityListDeref) {
							// Collect cache-miss candidates with a non-zero thrower handle
							std::vector<int> resolveCi;
							for (int ci = 0; ci < (int)candidates.size(); ci++) {
								auto& c = candidates[ci];
								if (c.fromCache) continue;
								DWORD thrower = s_cachedProjThrowers[c.idx];
								if (thrower == 0 || thrower == 0xFFFFFFFF) continue;
								resolveCi.push_back(ci);
							}
							if (!resolveCi.empty()) {
								// Pass 1: read sub-list pointers (entityListDeref + 0x10 + 8*(idx>>9))
								std::vector<DWORD64> subLists(candidates.size(), 0);
								{
									VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
									if (h) {
										for (int ci : resolveCi) {
											DWORD idx = s_cachedProjThrowers[candidates[ci].idx] & 0x7FFF;
											ProcessMgr.AddScatterReadRequest(h, entityListDeref + 0x10 + 8 * (idx >> 9), &subLists[ci], sizeof(DWORD64));
										}
										ProcessMgr.ExecuteReadScatter(h);
										VMMDLL_Scatter_CloseHandle(h);
									}
								}
								// Pass 2: read pawn addresses (subList + 0x70*(idx & 0x1FF))
								std::vector<DWORD64> pawnAddrs(candidates.size(), 0);
								{
									VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
									if (h) {
										for (int ci : resolveCi) {
											if (!subLists[ci]) continue;
											DWORD idx = s_cachedProjThrowers[candidates[ci].idx] & 0x7FFF;
											ProcessMgr.AddScatterReadRequest(h, subLists[ci] + 0x70 * (idx & 0x1FF), &pawnAddrs[ci], sizeof(DWORD64));
										}
										ProcessMgr.ExecuteReadScatter(h);
										VMMDLL_Scatter_CloseHandle(h);
									}
								}
								// Pass 3: read m_iTeamNum from each thrower pawn
								{
									VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
									if (h) {
										for (int ci : resolveCi) {
											if (!pawnAddrs[ci]) continue;
											ProcessMgr.AddScatterReadRequest(h, pawnAddrs[ci] + Offset::iTeamNum, &s_cachedProjTeams[candidates[ci].idx], sizeof(int));
										}
										ProcessMgr.ExecuteReadScatter(h);
										VMMDLL_Scatter_CloseHandle(h);
									}
								}
							}
						}
					}

						// Skip expired entities (e.g. smoke that finished but entity lingers)

							for (auto& c : candidates) {
								DWORD64 addr = entAddrs[c.idx];
								if (expiredEntities.count(addr)) continue;
								Vec3& pos = s_cachedProjPositions[c.idx];
								if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue;
								if (std::abs(pos.x) < 1.f && std::abs(pos.y) < 1.f && std::abs(pos.z) < 1.f) continue;
								GrenadeProjectile proj;
							proj.Position = pos;
							proj.EffectRadius = c.radius;
							proj.Type = c.type;
							proj.EntityAddr = addr;
							proj.Alive = true;
							proj.DisappearTimer = 0.f;
							// Stable entity ID for frontend DOM keying (lower 32 bits of entity address)
							proj.EntityId = static_cast<uint32_t>(addr & 0xFFFFFFFF);
							// Read "effect spawned" signal to determine if grenade has detonated.
							// Only detonated grenades are pushed as smoke/inferno/flash effects;
							// in-flight grenades are pushed as projectiles (icons) instead.
							if (c.type == PROJ_SMOKE && Offset::SmokeEffectTickBegin) {
								int tick = 0;
								if (ProcessMgr.ReadMemory<int>(addr + Offset::SmokeEffectTickBegin, tick) && tick > 0)
									proj.Exploded = true;
							} else if (c.isInferno && Offset::FireEffectTickBegin) {
							int tick = 0;
							if (ProcessMgr.ReadMemory<int>(addr + Offset::FireEffectTickBegin, tick) && tick > 0)
								proj.Exploded = true;
						} else if ((c.type == PROJ_FLASH || c.type == PROJ_HE) && Offset::ExplodeEffectTickBegin) {
								int tick = 0;
								if (ProcessMgr.ReadMemory<int>(addr + Offset::ExplodeEffectTickBegin, tick) && tick > 0)
									proj.Exploded = true;
							}
								// Task 6: thrower team (2=T, 3=CT). When m_hThrower
							// resolves to a valid team use it; otherwise leave
							// proj.Team = 0 (unknown). Do NOT fall back to the
							// local player's team 闁?that would mislabel enemy
							// grenades as friendly.
							int cachedTeam = s_cachedProjTeams[c.idx];
							if (cachedTeam == 2 || cachedTeam == 3) {
								proj.Team = cachedTeam;
							}
								// C_Inferno multi-flame points. Only the C_Inferno
							// entity carries m_fireCount / m_firePositions; the
							// C_MolotovProjectile in-flight entity does not.
							// Fire spreads over time, so we read fresh every scan.
							// The inferno entity is a static fire area 闁?compute
							// its center from the flame points instead of using
							// the entity origin (which may be the throw point).
							if (c.isInferno && Offset::InfernoFirePositions && Offset::InfernoFireCount) {
								int fc = 0;
								ProcessMgr.ReadMemory<int>(addr + Offset::InfernoFireCount, fc);
								if (fc > 0 && fc <= 64) {
									Vec3 flameBuf[64];
									if (ProcessMgr.ReadMemory(addr + Offset::InfernoFirePositions, flameBuf, sizeof(flameBuf))) {
										proj.FlameCount = fc;
										Vec3 center{ 0, 0, 0 };
										int validCount = 0;
										for (int fi = 0; fi < fc; fi++) {
											if (std::isfinite(flameBuf[fi].x) && std::isfinite(flameBuf[fi].y) && std::isfinite(flameBuf[fi].z)) {
												proj.Flames[fi] = flameBuf[fi];
												center.x += flameBuf[fi].x;
												center.y += flameBuf[fi].y;
												center.z += flameBuf[fi].z;
												validCount++;
											}
										}
										if (validCount > 0) {
											proj.Position = Vec3{ center.x / validCount, center.y / validCount, center.z / validCount };
										}
									}
								}
							}
							newProjectiles.push_back(proj);
						}

						// [Debug] 投掷物扫描调试日志（每 2 秒打印一次）
						{
							static auto lastProjDebugLog = std::chrono::steady_clock::now() - std::chrono::seconds(3);
							auto nowDebug = std::chrono::steady_clock::now();
							if (std::chrono::duration<float>(nowDebug - lastProjDebugLog).count() >= 2.0f && !newProjectiles.empty()) {
								lastProjDebugLog = nowDebug;
								LOG_INFO("Debug", "Projectile scan: {} projectiles", newProjectiles.size());
								for (const auto& p : newProjectiles) {
									const char* typeStr = "unknown";
									switch (p.Type) {
										case PROJ_SMOKE: typeStr = "smoke"; break;
										case PROJ_FLASH: typeStr = "flash"; break;
										case PROJ_HE: typeStr = "he"; break;
										case PROJ_MOLOTOV: typeStr = "molotov"; break;
										case PROJ_DECOY: typeStr = "decoy"; break;
										default: break;
									}
									LOG_INFO("Debug", "  proj type={} exploded={} team={} flames={} pos=({:.0f},{:.0f},{:.0f})",
										typeStr, p.Exploded, p.Team, p.FlameCount, p.Position.x, p.Position.y, p.Position.z);
								}
							}
						}

						// Task 12/16: Build dropped-weapon list from weapon candidates.
					// Mirrors reference project (world_process_entities.inl +
					// world_process_dropped_c4.inl) droppedOwnerReleased logic:
					//   - posNonOrigin: skip weapons at origin (0,0)
					//   - noOwner: owner handle is 0 / 0xFFFFFFFF -> dropped
					//   - owner not a known player -> dropped
					//   - owner not nearby (dist > 120) -> dropped
					// Only weapons held by a nearby player are skipped.
					{
						// Build pawnAddr -> player index map for owner resolution
						std::unordered_map<DWORD64, int> pawnAddrToIdx;
						for (int i = 0; i < (int)entityCache.size(); i++) {
							if (entityCache[i].pawnAddr != 0)
								pawnAddrToIdx[entityCache[i].pawnAddr] = i;
						}
						// Resolve owner handles that are non-zero / non-0xFFFFFFFF.
						// Weapon candidate count is small (<10), so we resolve via
						// the entity list the same way Phase 7c resolves thrower
						// handles: two-step scatter read.
						std::vector<int> resolveWi;
						for (int wi = 0; wi < (int)weaponCandidates.size(); wi++) {
							DWORD owner = s_cachedWeaponOwnerHandles[weaponCandidates[wi].idx];
							if (owner == 0 || owner == 0xFFFFFFFF) continue;
							resolveWi.push_back(wi);
						}
						std::vector<DWORD64> ownerEntityAddrs(weaponCandidates.size(), 0);
						if (!resolveWi.empty() && Offset::OwnerEntity) {
							DWORD64 entityListDeref = 0;
							ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entityListDeref);
							if (entityListDeref) {
								// Pass 1: read sub-list pointers
								std::vector<DWORD64> subLists(resolveWi.size(), 0);
								{
									VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
									if (h) {
										for (size_t k = 0; k < resolveWi.size(); k++) {
											DWORD idx = s_cachedWeaponOwnerHandles[weaponCandidates[resolveWi[k]].idx] & 0x7FFF;
											ProcessMgr.AddScatterReadRequest(h, entityListDeref + 0x10 + 8 * (idx >> 9), &subLists[k], sizeof(DWORD64));
										}
										ProcessMgr.ExecuteReadScatter(h);
										VMMDLL_Scatter_CloseHandle(h);
									}
								}
								// Pass 2: read entity addresses
								{
									VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
									if (h) {
										for (size_t k = 0; k < resolveWi.size(); k++) {
											if (!subLists[k]) continue;
											DWORD idx = s_cachedWeaponOwnerHandles[weaponCandidates[resolveWi[k]].idx] & 0x7FFF;
											ProcessMgr.AddScatterReadRequest(h, subLists[k] + 0x70 * (idx & 0x1FF), &ownerEntityAddrs[resolveWi[k]], sizeof(DWORD64));
										}
										ProcessMgr.ExecuteReadScatter(h);
										VMMDLL_Scatter_CloseHandle(h);
									}
								}
							}
						}
						for (size_t wi = 0; wi < weaponCandidates.size(); wi++) {
							auto& w = weaponCandidates[wi];
							DWORD64 addr = entAddrs[w.idx];
							Vec3& pos = s_cachedProjPositions[w.idx];
							if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue;
							// posNonOrigin: x or y must be > 1.0
							if (std::fabs(pos.x) <= 1.0f && std::fabs(pos.y) <= 1.0f) continue;
							// droppedOwnerReleased check
							DWORD ownerHandle = s_cachedWeaponOwnerHandles[w.idx];
							bool noOwner = (ownerHandle == 0 || ownerHandle == 0xFFFFFFFF);
							bool ownerHoldingNearby = false;
							if (!noOwner) {
								DWORD64 ownerAddr = ownerEntityAddrs[wi];
								auto it = pawnAddrToIdx.find(ownerAddr);
								if (it != pawnAddrToIdx.end()) {
									int pIdx = it->second;
									Vec3& ppos = entityCache[pIdx].entity.Pawn.Pos;
									if (std::isfinite(ppos.x) && std::isfinite(ppos.y)) {
										float dx = pos.x - ppos.x;
										float dy = pos.y - ppos.y;
										float dz = std::fabs(pos.z - ppos.z);
										ownerHoldingNearby = ((dx * dx + dy * dy) <= (120.0f * 120.0f)) && (dz <= 96.0f);
									}
								}
							}
							if (ownerHoldingNearby) continue; // player is holding it
							DroppedWeapon dw;
							dw.Position = pos;
							dw.ItemId = w.itemId;
							dw.Name = w.name ? w.name : "";
							dw.EntityAddr = addr;
							newDroppedWeapons.push_back(dw);
						}
					}

							// Clean up expired set: remove entries whose entities left the list
							std::set<DWORD64> stillInList;
							for (auto& c : candidates) stillInList.insert(entAddrs[c.idx]);
							for (auto& w : weaponCandidates) stillInList.insert(entAddrs[w.idx]);
							for (auto it = expiredEntities.begin(); it != expiredEntities.end(); ) {
								if (!stillInList.count(*it)) it = expiredEntities.erase(it);
								else ++it;
							}
						}

						// P5 Task 14: Advance shard and update idle detection
						s_worldDiscoveryShard = (s_worldDiscoveryShard + 1) % shardCount;
						if (newProjectiles.empty()) {
							s_worldIdleStreak++;
						} else {
							s_worldIdleStreak = 0;
						}
					}

					// Merge with previous cache
					static auto lastScanTime = std::chrono::steady_clock::now();
					auto now = std::chrono::steady_clock::now();
					float dt = std::chrono::duration<float>(now - lastScanTime).count();
					lastScanTime = now;

					// Build map of alive entities for quick lookup
					std::unordered_map<DWORD64, int> aliveMap;
					for (int i = 0; i < (int)newProjectiles.size(); i++)
						aliveMap[newProjectiles[i].EntityAddr] = i;

					// Carry over state from previous cache
					for (auto& old : projectileCache) {
						auto it = aliveMap.find(old.EntityAddr);
						if (it != aliveMap.end()) {
							// Entity still alive: carry over StationaryTimer
							auto& np = newProjectiles[it->second];
							float dx = np.Position.x - old.Position.x;
							float dy = np.Position.y - old.Position.y;
							float dz = np.Position.z - old.Position.z;
							if (dx * dx + dy * dy + dz * dz < 1.0f)
								np.StationaryTimer = old.StationaryTimer + dt;
							else
								np.StationaryTimer = 0.f;
						} else {
							// Entity disappeared: handle linger
							if (!old.Alive) {
								old.DisappearTimer += dt;
							} else {
								old.Alive = false;
								old.DisappearTimer = 0.f;
							}
							// Linger: molotov 7s, smoke uses remaining duration
							float maxLinger = 0.f;
							if (old.Type == PROJ_MOLOTOV) maxLinger = 7.0f;
							else if (old.Type == PROJ_SMOKE) maxLinger = std::max(0.f, 20.0f - old.StationaryTimer);
							if (old.DisappearTimer < maxLinger) {
								newProjectiles.push_back(old);
							}
						}
					}

					// Expire projectiles that exceeded their stationary duration
					// HE: immediate on stop, Smoke: 20s
					for (auto& p : newProjectiles) {
						if (!p.Alive) continue;
						bool expired = false;
						if (p.Type == PROJ_HE && p.StationaryTimer > 0.3f) expired = true;
						if (p.Type == PROJ_SMOKE && p.StationaryTimer > 20.0f) expired = true;
						if (expired) expiredEntities.insert(p.EntityAddr);
					}
					newProjectiles.erase(
						std::remove_if(newProjectiles.begin(), newProjectiles.end(),
							[](const GrenadeProjectile& p) {
								if (!p.Alive) return false;
								if (p.Type == PROJ_HE && p.StationaryTimer > 0.3f) return true;
							if (p.Type == PROJ_SMOKE && p.StationaryTimer > 20.0f) return true;
								return false;
							}),
						newProjectiles.end());

					projectileCache = std::move(newProjectiles);
				// Task 12/16: replace dropped-weapon cache with the latest shard
				// scan. In sharded mode (shardCount>1) only the current shard's
				// weapons are visible until the next shard sweep; this keeps the
				// implementation simple and avoids stale-weapon leaks.
				droppedWeaponCache = std::move(newDroppedWeapons);
			}
		} else {
			projectileCache.clear();
			droppedWeaponCache.clear();
			expiredEntities.clear();
		}

		// ------- 9. Build entity list and publish snapshot -------
			{
				std::vector<CEntity> publishEntities;
				publishEntities.reserve(entityCache.size());
				for (const auto& ce : entityCache) {
					publishEntities.push_back(ce.entity);
				}

				// ------- 9a. Local player observer target (for WebRadar m_observed_idx) -------
				// Read the local player's m_hObserverTarget so the serializer can
				// resolve which pawn the local player is spectating. Only set when
				// actually spectating (obsMode >= 4); otherwise stays 0 闁?-1.
				DWORD localObserverTarget = 0;
				if (localPawnAddr != 0) {
					DWORD64 obsSvcAddr = 0;
					if (ProcessMgr.ReadMemory<DWORD64>(localPawnAddr + Offset::ObserverServices, obsSvcAddr) && obsSvcAddr != 0) {
						int obsMode = 0;
						if (ProcessMgr.ReadMemory<int>(obsSvcAddr + Offset::ObserverMode, obsMode) && obsMode >= 4) {
							ProcessMgr.ReadMemory<DWORD>(obsSvcAddr + Offset::ObserverTarget, localObserverTarget);
						}
					}
				}

				// ------- 9b. Spectator detection -------
				std::vector<SpectatorInfo> spectators;
				if ((MenuConfig::ShowSpectatorList || MenuConfig::ShowWebRadar) && localPawnAddr != 0) {
					DWORD localPawnHandle = 0;
					// Get local pawn's entity handle (index lower 16 bits)
					// The local pawn address is known; we need its handle for comparison
					// m_hObserverTarget stores a CHandle 闁?lower 16 bits = entity index
					for (const auto& ce : entityCache) {
						if (ce.entity.Controller.AliveStatus != 1 && ce.pawnAddr != 0) {
							// Dead player 闁?check if spectating
							DWORD64 obsSvcAddr = 0;
							if (!ProcessMgr.ReadMemory<DWORD64>(ce.pawnAddr + Offset::ObserverServices, obsSvcAddr))
								continue;
							if (obsSvcAddr == 0) continue;

							int obsMode = 0;
							DWORD obsTarget = 0;
							if (!ProcessMgr.ReadMemory<int>(obsSvcAddr + Offset::ObserverMode, obsMode))
								continue;
							if (!ProcessMgr.ReadMemory<DWORD>(obsSvcAddr + Offset::ObserverTarget, obsTarget))
								continue;

							// obsMode >= 4 means spectating (IN_EYE=4, CHASE=5, ROAMING=6)
							if (obsMode >= 4 && obsTarget != 0) {
								// Resolve target pawn address from handle
								DWORD targetIdx = obsTarget & 0x1FF;
								DWORD64 targetListEntry = 0;
								if (!ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListEntry(), targetListEntry))
									continue;
								if (!ProcessMgr.ReadMemory<DWORD64>(targetListEntry + 0x10 + 8 * (targetIdx >> 9), targetListEntry))
									continue;
								DWORD64 targetPawnAddr = 0;
								if (!ProcessMgr.ReadMemory<DWORD64>(targetListEntry + 0x70 * (targetIdx & 0x1FF), targetPawnAddr))
									continue;

								if (targetPawnAddr == localPawnAddr) {
									SpectatorInfo si;
									si.Name = ce.entity.Controller.PlayerName;
									si.TeamID = ce.entity.Controller.TeamID;
									si.ObserverMode = obsMode;
									spectators.push_back(si);
								}
							}
						}
					}
				}

				LOG_TRACE("Data", "Publishing snapshot: entities={} projs={} localHP={} spectators={}", publishEntities.size(), projectileCache.size(), localPlayer.Pawn.Health, spectators.size());

			GameSnapshot newSnap;
			memcpy(newSnap.Matrix, matrix, sizeof(matrix));
			newSnap.LocalPlayer = localPlayer;
			newSnap.LocalPlayer.LocalPlayerControllerIndex = localPlayerIndex;
			newSnap.Entities = std::move(publishEntities);
			newSnap.Projectiles = projectileCache;
			// Task 12/16: publish dropped-weapon cache for world ESP rendering.
			newSnap.DroppedWeapons = droppedWeaponCache;
			newSnap.Spectators = std::move(spectators);
		// WebRadar: local player's observer target for m_observed_idx.
		newSnap.LocalObserverTarget = localObserverTarget;
		// ESP gap-closure stage 2: stamp capture time for render-loop interpolation.
		newSnap.CaptureTimeUs = now;

			// Preserve low-frequency fields this publish path does not refresh:
			//   MapName 闁?written by SlowUpdateThread (~10s)
			//   Bomb    闁?written by the WebRadar-extra block above (~50ms)
			// A shared_lock keeps the read exclusive against the in-place writers.
			{
				std::shared_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
				const GameSnapshot& cur = Cheats::GetSnapshot();
				memcpy(newSnap.MapName, cur.MapName, sizeof(newSnap.MapName));
				newSnap.Bomb = cur.Bomb;
				memcpy(newSnap.roundPhase, cur.roundPhase, sizeof(newSnap.roundPhase));
			}

			Cheats::PublishSnapshot(newSnap);
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR("Data", "DataThread exception: {}", e.what());
		}
		catch (...) {
			LOG_ERROR("Data", "DataThread unknown exception");
		}
	}
}

// =====================================================================
//  SlowUpdateThread 闁?low-frequency updates
// =====================================================================

VOID SlowUpdateThread()
{
	while (true)
	{
		try {
			if (globalVars::gameState.load() != AppState::RUNNING) {
				Sleep(1000);
				continue;
			}
			LOG_TRACE("SlowUpdate", "Updating entity list entry...");
			gGame.UpdateEntityListEntry();

			uintptr_t mapaddress = 0;
			uintptr_t mapaddress2 = 0;

			if (!ProcessMgr.ReadMemory(gGame.GetClientDLLAddress() + Offset::GlobalVars, mapaddress)) {
				Sleep(5000);
				continue;
			}

			if (!ProcessMgr.ReadMemory(mapaddress + Offset::GlobalVar.CurrentMap, mapaddress2)) {
				Sleep(5000);
				continue;
			}

			char tempMap[32]{};
			ProcessMgr.ReadMemory(mapaddress2, tempMap, 32);

			LOG_DEBUG("SlowUpdate", "Map name: '{}' (addr=0x{:X})", tempMap, mapaddress2);
		{
			std::unique_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
			int readIdx = Cheats::SnapshotReadIdx.load(std::memory_order_relaxed);
			memcpy(Cheats::SnapshotBuf[readIdx].MapName, tempMap, sizeof(tempMap));
		}

			Sleep(10000);
		}
		catch (const std::exception& e) {
			LOG_ERROR("SlowUpdate", "Exception: {}", e.what());
			Sleep(5000);
		}
		catch (...) {
			LOG_ERROR("SlowUpdate", "Unknown exception");
			Sleep(5000);
		}
	}
}

// =====================================================================
//  KeysCheckThread 闁?keyboard polling (unchanged logic)
// =====================================================================

VOID KeysCheckThread()
{
	while (true)
	{
		Sleep(10);
		Keys::MenuKey = ProcessMgr.is_key_down(MenuConfig::MenuHotKey);

		// Menu hotkey listening (reads host machine keys via DMA)
		if (MenuConfig::IsListeningForMenuKey) {
			for (int vk = 0x08; vk <= 0xFE; vk++) {
				if (vk >= 0x01 && vk <= 0x06) continue;
				if (ProcessMgr.is_key_down(vk)) {
					MenuConfig::MenuHotKey = vk;
					strcpy_s(MenuConfig::MenuHotKeyName, GrenadeHelper::GetKeyName(vk));
					MenuConfig::IsListeningForMenuKey = false;
					MyConfigSaver::MarkDirty();
					break;
				}
			}
			if (ProcessMgr.is_key_down(VK_ESCAPE))
				MenuConfig::IsListeningForMenuKey = false;
		}

		bool recordKeyPressed = ProcessMgr.is_key_down(GrenadeHelper::RecordHotKey);

		if (recordKeyPressed && !Keys::RecordKey) {
			CEntity localCopy;
			bool valid = false;
			{
				const auto& lp = Cheats::GetSnapshot().LocalPlayer;
				if (lp.Controller.Address != 0 &&
				    lp.Pawn.Address != 0 &&
				    lp.Pawn.Health > 0) {
					localCopy = lp;
					valid = true;
				}
			}
			if (valid)
				GrenadeHelper::RecordPosition(localCopy);
		}
		Keys::RecordKey = recordKeyPressed;

		// Hotkey bindings: detect rising edge and execute action
		for (int i = 0; i < MenuConfig::HOTKEY_COUNT; i++) {
			auto& hk = MenuConfig::Hotkeys[i];
			if (hk.vkCode == 0) {
				hk.wasPressed = false;
				continue;
			}
			bool pressed = ProcessMgr.is_key_down(hk.vkCode) || (GetAsyncKeyState(hk.vkCode) & 0x8000);
			if (pressed && !hk.wasPressed) {
				switch (i) {
				case MenuConfig::HOTKEY_TOGGLE_BOX_ESP:       MenuConfig::ShowBoxESP = !MenuConfig::ShowBoxESP; break;
				case MenuConfig::HOTKEY_TOGGLE_BONE_ESP:      MenuConfig::ShowBoneESP = !MenuConfig::ShowBoneESP; break;
				case MenuConfig::HOTKEY_TOGGLE_HEALTH_BAR:    MenuConfig::ShowHealthBar = !MenuConfig::ShowHealthBar; break;
				case MenuConfig::HOTKEY_TOGGLE_WEAPON_ESP:    MenuConfig::ShowWeaponESP = !MenuConfig::ShowWeaponESP; break;
				case MenuConfig::HOTKEY_TOGGLE_PLAYER_NAME:   MenuConfig::ShowPlayerName = !MenuConfig::ShowPlayerName; break;
				case MenuConfig::HOTKEY_TOGGLE_DISTANCE:      MenuConfig::ShowDistance = !MenuConfig::ShowDistance; break;
				case MenuConfig::HOTKEY_TOGGLE_EYE_RAY:       MenuConfig::ShowEyeRay = !MenuConfig::ShowEyeRay; break;
				case MenuConfig::HOTKEY_TOGGLE_SNAPLINE:      MenuConfig::ShowLineToEnemy = !MenuConfig::ShowLineToEnemy; break;
				case MenuConfig::HOTKEY_TOGGLE_BOMB_ESP:      MenuConfig::ShowBombESP = !MenuConfig::ShowBombESP; break;
				case MenuConfig::HOTKEY_TOGGLE_PROJECTILE_ESP: MenuConfig::ShowProjectileESP = !MenuConfig::ShowProjectileESP; break;
				case MenuConfig::HOTKEY_TOGGLE_SPECTATOR_LIST: MenuConfig::ShowSpectatorList = !MenuConfig::ShowSpectatorList; break;
				case MenuConfig::HOTKEY_TOGGLE_TEAM_CHECK:    MenuConfig::TeamCheck = !MenuConfig::TeamCheck; break;
				case MenuConfig::HOTKEY_TOGGLE_WEB_RADAR:     MenuConfig::ShowWebRadar = !MenuConfig::ShowWebRadar; break;
				case MenuConfig::HOTKEY_TOGGLE_SAFE_ZONE:     MenuConfig::SafeZoneEnabled = !MenuConfig::SafeZoneEnabled; break;
				case MenuConfig::HOTKEY_TOGGLE_CROSSHAIR:     MenuConfig::CrosshairEnabled = !MenuConfig::CrosshairEnabled; break;
				case MenuConfig::HOTKEY_RELOAD_GAME:
					ProcessMgr.Detach();
					globalVars::gameState.store(AppState::SEARCHING_GAME);
					break;
				}
				MyConfigSaver::MarkDirty();
			}
			hk.wasPressed = pressed;
		}
	}
}

// =====================================================================
//  DmaAdminThread 闁?asynchronous VMMDLL_ConfigSet refresh processing
//
//  Polls g_pendingRefreshFlags (set by RequestDmaRefresh) every 20ms and
//  performs the actual VMMDLL_ConfigSet calls off the DataThread path.
//  Executes the highest requested tier and bumps the scene-reset serial.
// =====================================================================

VOID DmaAdminThread()
{
	while (true)
	{
		try {
			Sleep(20);

			if (globalVars::gameState.load() == AppState::EXITING)
				return;

			uint32_t flags = g_pendingRefreshFlags.load(std::memory_order_acquire);
			if (flags == 0) continue;

			// Clear flags before issuing the (potentially slow) refresh call
			g_pendingRefreshFlags.store(0, std::memory_order_release);

			// Execute the highest tier requested
			if (flags & 0x4) { // Full
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
			} else if (flags & 0x2) { // Repair
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 1);
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1);
			} else if (flags & 0x1) { // Probe
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_FAST, 1);
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 1);
			}

			SceneReset::BumpSceneReset();
		}
		catch (...) {
			// Silently swallow 闁?keep the loop alive
		}
	}
}

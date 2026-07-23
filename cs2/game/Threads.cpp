#include "Threads.h"

#include "../render/GrenadeHelper.h"
#include "MenuConfig.h"
#include "../config/ConfigSaver.h"
#include "../config/Language.h"
#ifdef AIMBOT_ENABLED
#include "../config/AimConfig.h"
#include "../aim/AimBot.h"
#include "../aim/TriggerBot.h"
#include "../aim/MagnetTriggerBot.h"
#include "../aim/SprayControl.h"
#include "../aim/RecoilControl.h"
#include "../aim/InputManager.h"
#include "../hw/HwManager.h"
#include "../license/AntiTamper.h"
#endif
#include "../utils/Logger.h"
#include "../utils/DmaHealth.h"
#include "../utils/StageTimer.h"
#include "intervals.h"
#include "SceneReset.h"
#include "WeaponLookup.h"

#include "rapidjson/document.h"

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
// g_dmaHealth is now defined as inline in DmaHealth.h, shared across all TUs.

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

// ---------- Minimal validation helpers (direct-read mode) ----------
// Only reject obvious garbage: NaN/Infinity, all-zero coords, null/misaligned pointers.
// No range filtering (health>100, coord bounds etc.) — read what DMA gives, render as-is.

static bool IsValidPos(const Vec3& pos)
{
	return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)
		&& (std::abs(pos.x) + std::abs(pos.y) + std::abs(pos.z) > 1.0f);
}

static bool IsValidHealth(int) { return true; } // no filtering

// Unified game-pointer validation: rejects null, out-of-range, and misaligned values.
static bool IsLikelyGamePointer(DWORD64 ptr)
{
	if (ptr < 0x10000ULL || ptr >= 0x00007FF000000000ULL)
		return false;
	return (ptr & 7ULL) == 0ULL; // 8-byte alignment
}

// === Stage health checker ===
// 各数据读取阶段的独立健康检查，连续异常自动触发DMA刷新
struct StageHealth {
	int failStreak = 0;
	int64_t lastRefreshUs = 0;
	void reportFail(int64_t now, const char* name) {
		if (++failStreak >= 3 && (lastRefreshUs == 0 || (now - lastRefreshUs) >= 500000)) {
			LOG_WARNING("Data", "Stage '{}' anomaly (streak={}), refreshing DMA", name, failStreak);
			RequestDmaRefresh(DmaRefreshTier::Full);
			SceneReset::BumpSceneReset();
			lastRefreshUs = now;
			failStreak = 0;
		}
	}
	void reportOk() { failStreak = 0; }
};

// === Session player registry ===
// 单局玩家列表，每局开始时建立，用于精细化数据质量检查
struct SessionPlayer {
	DWORD64 controllerAddr = 0;
	int failStreak = 0;
	int goneStreak = 0;
	bool isAlive = true;
	int framesSinceAdded = 0;  // 加入session后的帧数，用于武器名称宽限期
	bool hadWeapon = false;    // 是否曾经读到过武器名称
};

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

	// Tiered update frequency 闁?microsecond intervals (P2)
	auto nowUs = []() -> int64_t {
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	};

		int64_t lastControllerRefreshUs = 0;
		int64_t lastWeaponUpdateUs = 0;
		int64_t lastWrExtraUs = 0;
		int64_t lastWrSlowUs = 0;
		int64_t lastProjectileScanUs = 0;

	static StageHealth s_matrixHealth;
	static StageHealth s_playerDataHealth;
	static std::vector<SessionPlayer> s_sessionPlayers;
	static uint64_t s_sessionSerial = 0;
		int64_t lastPeriodicRefreshUs = 0;
		int64_t lastPlayerStatusAuxUs = 0;

	int frameCounter = 0; // retained for logging only

	while (true)
	{
#ifdef AIMBOT_ENABLED
		// 多点校验：每 200 次循环检查一次（~1秒@200Hz）
		static int s_dataAntiTamper = 0;
		if (++s_dataAntiTamper >= 200) {
			s_dataAntiTamper = 0;
			if (!AntiTamper::PeriodicCheck()) {
				globalVars::gameState.store(AppState::DMA_FAILED);
				continue;
			}
		}
#endif
		try {
			PreciseSleepUs(5000); // 200Hz direct-read mode

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

			std::vector<CachedEntity> entityCache;
			entityCache.reserve(MAX_ENTITIES);

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
			bool needEntityPipeline = anyESPDraw || MenuConfig::ShowWebRadar;
#ifdef AIMBOT_ENABLED
			needEntityPipeline = needEntityPipeline || AimConfig::AimBot().enabled || AimConfig::TriggerBot().enabled || AimConfig::Magnet().enabled;
#endif
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
		// VPKVisibilityCheck 也需要本地 head 骨骼作为可见性射线起点
		bool needBones = anyESPDraw || MenuConfig::VPKVisibilityCheck;
#ifdef AIMBOT_ENABLED
		needBones = needBones || AimConfig::AimBot().enabled || AimConfig::Magnet().enabled;
#endif
			bool needViewAngle = MenuConfig::ShowEyeRay || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled;
#ifdef AIMBOT_ENABLED
			needViewAngle = needViewAngle || AimConfig::AimBot().enabled || AimConfig::Magnet().enabled;
#endif
			bool needCameraPos = MenuConfig::ShowEyeRay || MenuConfig::VPKVisibilityCheck;
#ifdef AIMBOT_ENABLED
			needCameraPos = needCameraPos || AimConfig::AimBot().visualCheck || AimConfig::Magnet().visualCheck;
#endif
			bool needWeapon = MenuConfig::ShowWeaponESP || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled || MenuConfig::ShowWeaponAmmo || MenuConfig::ShowWeaponIcon || MenuConfig::PlayerCountCheckEnabled;

			// ------- 1. Read matrix -------
			{
				StageTimer timer(g_stageMatrixUs);
		ProcessMgr.ReadMemory(gGame.GetMatrixAddress(), matrix, 64);
		memcpy(gGame.View.Matrix, matrix, 64);
			}

			// Matrix health check
			{
				bool matrixValid = false;
				for (int r = 0; r < 4 && !matrixValid; r++)
					for (int c = 0; c < 4; c++)
						if (std::isfinite(matrix[r][c]) && matrix[r][c] != 0.f) { matrixValid = true; break; }
				if (matrixValid) s_matrixHealth.reportOk();
				else s_matrixHealth.reportFail(now, "Matrix");
			}

			// ------- 2. Read local player addresses -------
			DWORD64 localControllerAddr = 0;
			DWORD64 localPawnAddr = 0;
			{
				StageTimer timer(g_stageLocalUs);
			if (!ProcessMgr.ReadMemory(gGame.GetLocalControllerAddress(), localControllerAddr)) {
			continue;
		}
		if (localControllerAddr == 0) {
			continue;
		}
		if (!ProcessMgr.ReadMemory(gGame.GetLocalPawnAddress(), localPawnAddr)) {
			continue;
		}

			LOG_TRACE("Data", "Local: ctrl=0x{:X} pawn=0x{:X}", localControllerAddr, localPawnAddr);

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

			{
				StageTimer timer(g_stageEntitiesUs);
				LOG_TRACE("Data", "--- Discovery frame (cache_size={}) ---", entityCache.size());

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
				continue;
			}
			if (entityAddr == localControllerAddr) {
					localPlayerIndex = i;
					continue;
				}

				auto it = oldCacheMap.find(entityAddr);
			if (it != oldCacheMap.end() && !controllerRefresh) {
				// Skip evicted entities (pawnAddr==0): let them be rediscovered
				// on the next refresh frame instead of keeping stale entries in cache.
				if (entityCache[it->second].pawnAddr != 0)
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
						if (ctrlBuf[i].pawn == 0) {
							// Pawn handle reads 0 (DMA jitter): still add to aliveSlots
							// so zero-pawn grace / hierarchy-missing hold can retain old cache.
							aliveSlots[aliveCount++] = i;
							continue;
						}
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
								if (subListEntries[i] == 0 || !IsLikelyGamePointer(subListEntries[i])) continue;
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
						if (pawnAddresses[i] == 0 || !IsLikelyGamePointer(pawnAddresses[i])) continue;
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
						if (sceneNodes[i] == 0 || !IsLikelyGamePointer(sceneNodes[i])) continue;
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
					continue;
				}

					DWORD64 effectiveBoneArray = boneArrays[i];
				bool isDeadPlayer = (ctrlBuf[i].isAlive != 1);
				if (effectiveBoneArray != 0 && !IsLikelyGamePointer(effectiveBoneArray)) {
					effectiveBoneArray = 0; // invalid pointer, treat as no bones
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
						// Don't retain evicted entities (pawnAddr==0) — only retain
						// genuinely dead players (pawnAddr!=0, isAlive==0) for WebRadar.
						if (copy.pawnAddr == 0) continue;
						copy.entity.Pawn.Health = 0;
						copy.entity.Controller.Health = 0;
						newCache.push_back(copy);
					}
				}
				}

				LOG_DEBUG("Data", "Discovery done: cache={} refresh={}", (int)newCache.size(), refreshCount);

		entityCache = std::move(newCache);

			// --- Player count health check ---
			// Smart detection: if entity list has enough addresses but the
			// final cache has fewer entities than expected, the DMA read
			// pipeline lost data mid-stream (not players leaving). Trigger
			// an automatic DMA refresh to recover.
			if (MenuConfig::PlayerCountCheckEnabled) {
				static int s_playerCountFailStreak = 0;
				static int64_t s_lastPlayerCountRefreshUs = 0;

				int addrCount = 0;
				for (int i = 0; i < scanCount; i++) {
					if (entityAddresses[i] != 0) addrCount++;
				}
				int validCount = (int)entityCache.size() + (localPlayerIndex >= 0 ? 1 : 0);
				const int expected = MenuConfig::ExpectedPlayerCount;

				if (addrCount >= expected && validCount < expected) {
					s_playerCountFailStreak++;
					int64_t now = nowUs();
					if (s_playerCountFailStreak >= 3 &&
						(s_lastPlayerCountRefreshUs == 0 || (now - s_lastPlayerCountRefreshUs) >= 500000)) {
						LOG_WARNING("Data", "Player count anomaly: addrCount={} validCount={} expected={}, refreshing DMA cache",
							addrCount, validCount, expected);
						RequestDmaRefresh(DmaRefreshTier::Full);
						SceneReset::BumpSceneReset();
						s_lastPlayerCountRefreshUs = now;
						s_playerCountFailStreak = 0;
					}
				} else {
					s_playerCountFailStreak = 0;
				}
			}
		}

			// ------- 4. Scatter read dynamic fields (2-pass: refresh bone addresses every frame) -------
			int count = (int)entityCache.size();
			if (count > MAX_ENTITIES) count = MAX_ENTITIES;
			{
				StageTimer timer(g_stageScatterUs);

				// --- Pass 1: pos, health, viewAngle, cameraPos + fresh BoneArray pointer ---
				// Pawn offsets span 3 pages: health@0x354(page0), pos@0x1588(page1), eyeAngles@0x3DD0(page3)
				// Plus sceneNode BoneArray@0x1D0 = 4 unique pages per entity. Batch=2 闁?8 pages.
				DWORD64 freshBoneArrays[MAX_ENTITIES]{};
				DWORD64 freshLocalBoneArray = 0;
				{
					for (int i = 0; i < count; i++) {
						DWORD64 addr = entityCache[i].controllerAddr;
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
					// Skip evicted entities (pawnAddr==0): reading from address 0+offset
					// produces garbage and poisons the scatter batch.
					if (pawn == 0) { memset(&buf, 0, sizeof(ScatterBuf)); continue; }
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
				if (!batchOk) {
					for (int i = batchStart; i < batchEnd; i++) {
					auto& ce = entityCache[i];
					auto& buf = scatterBuf[i];
					DWORD64 pawn = ce.pawnAddr;
					// Skip evicted entities in per-slot retry as well.
					if (pawn == 0) continue;
					VMMDLL_SCATTER_HANDLE slotHandle = ProcessMgr.CreateScatterHandle();
						if (!slotHandle) {
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
						ProcessMgr.ExecuteReadScatter(slotHandle);
						VMMDLL_Scatter_CloseHandle(slotHandle);
					}
				}
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
						if (needCameraPos)
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::vecLastClipCameraPos, &localBuf.cameraPos, sizeof(Vec3));
							if (needBones && localPlayer.Pawn.BoneData.SceneNodeAddress != 0)
								ProcessMgr.AddScatterReadRequest(handle, localPlayer.Pawn.BoneData.SceneNodeAddress + Offset::BoneArray, &freshLocalBoneArray, sizeof(DWORD64));
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
					if (freshBoneArrays[i] != 0 && IsLikelyGamePointer(freshBoneArrays[i])) {
						entityCache[i].boneArrayAddr = freshBoneArrays[i];
						entityCache[i].entity.Pawn.BoneData.BoneArrayAddress = freshBoneArrays[i];
					}
				}
				if (freshLocalBoneArray != 0 && IsLikelyGamePointer(freshLocalBoneArray))
					localPlayer.Pawn.BoneData.BoneArrayAddress = freshLocalBoneArray;

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
					// Local player bone data
					if (localPlayer.Pawn.BoneData.BoneArrayAddress != 0) {
						VMMDLL_SCATTER_HANDLE handle = ProcessMgr.CreateScatterHandle();
						if (handle) {
							ProcessMgr.AddScatterReadRequest(handle, localPlayer.Pawn.BoneData.BoneArrayAddress, localBuf.bones, CBone::NUM_BONES * sizeof(BoneJointData));
							ProcessMgr.ExecuteReadScatter(handle);
							VMMDLL_Scatter_CloseHandle(handle);
						}
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
					ce.entity.Pawn.Health = 0;
					ce.entity.Pawn.ScreenPosValid = false;
			} else {
				// Dead player protection: retain death position, skip update
					if (ce.entity.Pawn.Health <= 0 && buf.health <= 0) {
						continue;
					}
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

							// Per-slot bone data stale detection (ref: KevqDMA bone_reads.inl).
							// Detects when bone address is valid but data is all-zero.
							bool boneDataAllZero = true;
							for (int j = 0; j < CBone::NUM_BONES; j++) {
								const Vec3& bp = buf.bones[j].Pos;
								if (std::abs(bp.x) > 0.5f || std::abs(bp.y) > 0.5f || std::abs(bp.z) > 0.5f) {
									boneDataAllZero = false;
									break;
								}
							}
							if (boneDataAllZero) {
									ce.entity.Pawn.BoneData.BonePosCount = 0; // clear stale bones
								}
						}
					}
				}
			}

			// Apply local player scatter results
			if (IsValidPos(localBuf.pos)) {
				localPlayer.Pawn.PrevPos = localPlayer.Pawn.Pos;
				localPlayer.Pawn.Pos = localBuf.pos;
				localPlayer.Pawn.Health = localBuf.health;
				localPlayer.Pawn.Armor = (localBuf.armor >= 0 && localBuf.armor <= 100) ? localBuf.armor : 0;
			localPlayer.Pawn.FlashDuration = localBuf.flashDuration;
			if (needCameraPos)
				localPlayer.Pawn.CameraPos = localBuf.cameraPos;
			if (needViewAngle)
				localPlayer.Pawn.ViewAngle = localBuf.viewAngle;
				if (needBones) {
					localPlayer.Pawn.BoneData.BonePosCount = 0;
					for (int j = 0; j < CBone::NUM_BONES; j++) {
						const Vec3& bonePos = localBuf.bones[j].Pos;
						bool valid = std::isfinite(bonePos.x) && std::isfinite(bonePos.y) && std::isfinite(bonePos.z);
						localPlayer.Pawn.BoneData.BonePosList[j] = { bonePos, {0,0}, valid };
						localPlayer.Pawn.BoneData.BonePosCount++;
					}
					bool boneDataAllZero = true;
					for (int j = 0; j < CBone::NUM_BONES; j++) {
						const Vec3& bp = localBuf.bones[j].Pos;
						if (std::abs(bp.x) > 0.5f || std::abs(bp.y) > 0.5f || std::abs(bp.z) > 0.5f) {
							boneDataAllZero = false;
							break;
						}
					}
					if (boneDataAllZero)
						localPlayer.Pawn.BoneData.BonePosCount = 0;
				}
			}

			// --- Session player registry & per-player data quality check ---
			// 单局玩家列表：每局开始时建立，对每个玩家（包括自己）进行精细化数据质量检查。
			// 检查项：位置有效性、血量范围、骨骼数据完整性。
			// 任意玩家持续异常时，自动触发DMA刷新。
			{
				uint64_t curSerial = SceneReset::CurrentSerial();
				if (curSerial != s_sessionSerial) {
					s_sessionSerial = curSerial;
					s_sessionPlayers.clear();
				}

				// Add new players to session (including local player)
				for (const auto& ce : entityCache) {
					if (ce.controllerAddr == 0 || ce.pawnAddr == 0) continue;
					bool found = false;
					for (const auto& sp : s_sessionPlayers)
						if (sp.controllerAddr == ce.controllerAddr) { found = true; break; }
					if (!found)
						s_sessionPlayers.push_back({ce.controllerAddr, 0, 0});
				}
				if (localControllerAddr != 0) {
					bool found = false;
					for (const auto& sp : s_sessionPlayers)
						if (sp.controllerAddr == localControllerAddr) { found = true; break; }
					if (!found)
						s_sessionPlayers.push_back({localControllerAddr, 0, 0});
				}

				// Check each session player's data quality
				int anomalyCount = 0;
				for (auto& sp : s_sessionPlayers) {
					sp.framesSinceAdded++;
					// 本地玩家：从 localPlayer 变量检查
					if (sp.controllerAddr == localControllerAddr) {
						sp.goneStreak = 0;
						sp.isAlive = (localPlayer.Pawn.Health > 0);
						if (!sp.isAlive) {
							sp.failStreak = 0; // 死亡/观战：正常状态
						} else {
							bool ok = IsValidPos(localPlayer.Pawn.Pos) &&
							          localPlayer.Pawn.Health > 0 && localPlayer.Pawn.Health <= 100;
							if (ok && needBones)
								ok = localPlayer.Pawn.BoneData.BonePosCount > 0;
							if (ok && needViewAngle)
								ok = std::isfinite(localPlayer.Pawn.ViewAngle.x) && std::isfinite(localPlayer.Pawn.ViewAngle.y);
							if (ok && sp.framesSinceAdded > 20) {
							if (!localPlayer.Pawn.WeaponName.empty())
								sp.hadWeapon = true;
							if (sp.hadWeapon && localPlayer.Pawn.WeaponName.empty())
								ok = false;
						}
							if (ok) sp.failStreak = 0;
							else sp.failStreak++;
						}
						if (sp.failStreak >= 3) anomalyCount++;
						continue;
					}

					// 其他玩家：从 entityCache 检查
					const CEntity* ent = nullptr;
					for (const auto& ce : entityCache)
						if (ce.controllerAddr == sp.controllerAddr) { ent = &ce.entity; break; }

					if (!ent) {
						sp.goneStreak++;
						if (sp.goneStreak > 10) continue;
						sp.failStreak++;
					} else {
						sp.goneStreak = 0;
						sp.isAlive = (ent->Pawn.Health > 0);
						if (!sp.isAlive) {
							sp.failStreak = 0; // 死亡：正常状态
						} else {
							bool ok = IsValidPos(ent->Pawn.Pos) &&
							          ent->Pawn.Health > 0 && ent->Pawn.Health <= 100;
							if (ok && needBones)
								ok = ent->Pawn.BoneData.BonePosCount > 0;
							if (ok && needViewAngle)
								ok = std::isfinite(ent->Pawn.ViewAngle.x) && std::isfinite(ent->Pawn.ViewAngle.y);
							if (ok && sp.framesSinceAdded > 20) {
								if (!ent->Pawn.WeaponName.empty())
									sp.hadWeapon = true;
								if (sp.hadWeapon && ent->Pawn.WeaponName.empty())
									ok = false;
							}
							if (ok) sp.failStreak = 0;
							else sp.failStreak++;
						}
					}
					if (sp.failStreak >= 3) anomalyCount++;
				}

				// Remove long-gone players (disconnected or left)
				s_sessionPlayers.erase(
					std::remove_if(s_sessionPlayers.begin(), s_sessionPlayers.end(),
						[](const SessionPlayer& sp) { return sp.goneStreak > 10; }),
					s_sessionPlayers.end());

				// Trigger refresh if any session player has sustained anomalies
				if (!s_sessionPlayers.empty()) {
					if (anomalyCount >= 1) {
						// 诊断日志：输出具体异常信息
						for (const auto& sp : s_sessionPlayers) {
							if (sp.failStreak < 3) continue;
							if (sp.controllerAddr == localControllerAddr) {
								LOG_WARNING("Data", "Anomaly [local] hp={} boneCount={} boneArray=0x{:X} weapon={} viewAngle=({},{}) needBones={} needViewAngle={}",
									localPlayer.Pawn.Health, localPlayer.Pawn.BoneData.BonePosCount,
									localPlayer.Pawn.BoneData.BoneArrayAddress,
									localPlayer.Pawn.WeaponName.empty() ? "empty" : "ok",
									localPlayer.Pawn.ViewAngle.x, localPlayer.Pawn.ViewAngle.y,
									needBones, needViewAngle);
							} else {
								const CEntity* ent2 = nullptr;
								for (const auto& ce : entityCache)
									if (ce.controllerAddr == sp.controllerAddr) { ent2 = &ce.entity; break; }
								if (ent2) {
									LOG_WARNING("Data", "Anomaly [entity] hp={} pos=({},{},{}) boneCount={} boneArray=0x{:X} weapon={} viewAngle=({},{}) needBones={} needViewAngle={}",
										ent2->Pawn.Health, ent2->Pawn.Pos.x, ent2->Pawn.Pos.y, ent2->Pawn.Pos.z,
										ent2->Pawn.BoneData.BonePosCount, ent2->Pawn.BoneData.BoneArrayAddress,
										ent2->Pawn.WeaponName.empty() ? "empty" : "ok",
										ent2->Pawn.ViewAngle.x, ent2->Pawn.ViewAngle.y,
										needBones, needViewAngle);
								} else {
									LOG_WARNING("Data", "Anomaly [missing] goneStreak={}", sp.goneStreak);
								}
							}
						}
						s_playerDataHealth.reportFail(now, "PlayerData");
					} else
						s_playerDataHealth.reportOk();
				}
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
					if (Offset::vecVelocity) {
					const Vec3& v = velocityBuf[i];
					if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
						std::abs(v.x) < 5000.f && std::abs(v.y) < 5000.f && std::abs(v.z) < 5000.f)
						ce.entity.Pawn.Velocity = v;
					else
						ce.entity.Pawn.Velocity = {0, 0, 0};
				}
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
					if (Offset::vecVelocity) {
					if (std::isfinite(localVelocity.x) && std::isfinite(localVelocity.y) && std::isfinite(localVelocity.z) &&
						std::abs(localVelocity.x) < 5000.f && std::abs(localVelocity.y) < 5000.f && std::abs(localVelocity.z) < 5000.f)
						localPlayer.Pawn.Velocity = localVelocity;
					else
						localPlayer.Pawn.Velocity = {0, 0, 0};
				}
					if (MenuConfig::ShowFootstepESP && Offset::fFlags)
						localPlayer.Pawn.fFlags = localFFlags;
					if (MenuConfig::ShowFootstepESP && Offset::bIsWalking)
						localPlayer.Pawn.IsWalking = localWalking != 0;
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
					// Note: do NOT pre-clear WeaponName to "Weapon_None" here.
				// Scatter-read chain occasionally fails (DMA transient error or
				// ActiveWeapon handle == 0xFFFFFFFF right after weapon switch),
				// which would flash ESP/radar to "Weapon_None". Keep last good
				// value instead; only update when a name is successfully resolved.

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
						// 2-frame confirmation (ref: KevqDMA): require same pointer across
						// 2 consecutive reads before accepting, to filter DMA-jitter bad pointers.
						static DWORD64 s_pendingGameRulesCandidate = 0;
						static int s_gameRulesConfirmCount = 0;
						bool confirmed = false;
						if (gameRulesPtr != s_pendingGameRulesCandidate) {
							s_pendingGameRulesCandidate = gameRulesPtr;
							s_gameRulesConfirmCount = 1;
						} else {
							s_gameRulesConfirmCount++;
							if (s_gameRulesConfirmCount >= 2) {
								confirmed = true;
								s_pendingGameRulesCandidate = 0;
								s_gameRulesConfirmCount = 0;
							}
						}
						if (confirmed) {
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
	constexpr int PROJ_CACHE_SIZE = 960;
	static DWORD64 s_cachedProjEntityAddrs[PROJ_CACHE_SIZE] = {};
	static DWORD64 s_cachedProjSceneNodes[PROJ_CACHE_SIZE] = {};
	static Vec3 s_cachedProjPositions[PROJ_CACHE_SIZE] = {};
	static uint8_t s_cachedProjTypes[PROJ_CACHE_SIZE] = {};
	// Task 6: cached thrower team (resolved from m_hThrower 闁?pawn 闁?m_iTeamNum)
	static DWORD s_cachedProjThrowers[PROJ_CACHE_SIZE] = {};
	static int s_cachedProjTeams[PROJ_CACHE_SIZE] = {};
	// C_Inferno entity flag (distinguishes inferno from molotov_projectile,
	// both share PROJ_MOLOTOV type but only inferno carries fire data).
	static bool s_cachedProjIsInferno[PROJ_CACHE_SIZE] = {};
	// Task 12/16: cached weapon item-id for dropped-weapon cache-hit reuse.
	// Without this, weapons are only identified on cache-miss (first frame)
	// and vanish on subsequent cache-hit frames because droppedWeaponCache
	// is rebuilt from scratch each scan.
	static uint16_t s_cachedWeaponItemIds[PROJ_CACHE_SIZE] = {};
	// Cached owner handle for dropped-weapon filtering.
	// m_hOwnerEntity == 0xFFFFFFFF means the weapon is dropped (no owner).
	// When the handle points to a player, the weapon is still held and must be skipped.
	static DWORD s_cachedWeaponOwnerHandles[PROJ_CACHE_SIZE] = {};
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
						static_assert(SCAN_COUNT <= PROJ_CACHE_SIZE, "SCAN_COUNT exceeds PROJ_CACHE_SIZE: increase PROJ_CACHE_SIZE or narrow SCAN range");

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

				LOG_TRACE("Data", "Publishing snapshot: entities={} projs={} localHP={}", publishEntities.size(), projectileCache.size(), localPlayer.Pawn.Health);

			GameSnapshot newSnap;
			memcpy(newSnap.Matrix, matrix, sizeof(matrix));
			newSnap.LocalPlayer = localPlayer;
			newSnap.LocalPlayer.LocalPlayerControllerIndex = localPlayerIndex;
			newSnap.Entities = std::move(publishEntities);
			newSnap.Projectiles = projectileCache;
			// Task 12/16: publish dropped-weapon cache for world ESP rendering.
			newSnap.DroppedWeapons = droppedWeaponCache;
		// WebRadar: local player's observer target for m_observed_idx.
		newSnap.LocalObserverTarget = localObserverTarget;
		// ESP gap-closure stage 2: stamp capture time for render-loop interpolation.
		newSnap.CaptureTimeUs = now;

			// Preserve low-frequency fields this publish path does not refresh:
			//   MapName -> written by SlowUpdateThread (~10s)
			//   Bomb    -> written by the WebRadar-extra block above (~50ms)
			//   Money/HasDefuser/HasHelmet/Color -> carried over from previous
			//   snapshot to prevent flicker after DMA refresh resets entity cache.
			// A shared_lock keeps the read exclusive against the in-place writers.
			{
				std::shared_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
				const GameSnapshot& cur = Cheats::GetSnapshot();
				memcpy(newSnap.MapName, cur.MapName, sizeof(newSnap.MapName));
				newSnap.Bomb = cur.Bomb;
				memcpy(newSnap.roundPhase, cur.roundPhase, sizeof(newSnap.roundPhase));
				for (auto& ne : newSnap.Entities) {
					for (const auto& oe : cur.Entities) {
						if (ne.Controller.Address != oe.Controller.Address) continue;
						if (ne.Controller.Money == 0 && oe.Controller.Money > 0)
							ne.Controller.Money = oe.Controller.Money;
						if (!ne.Pawn.HasDefuser && oe.Pawn.HasDefuser)
							ne.Pawn.HasDefuser = oe.Pawn.HasDefuser;
						if (!ne.Pawn.HasHelmet && oe.Pawn.HasHelmet)
							ne.Pawn.HasHelmet = oe.Pawn.HasHelmet;
						if (ne.Controller.Color < 0 && oe.Controller.Color >= 0)
							ne.Controller.Color = oe.Controller.Color;
						break;
					}
				}
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
				case MenuConfig::HOTKEY_TOGGLE_TEAM_CHECK:    MenuConfig::TeamCheck = !MenuConfig::TeamCheck; break;
				case MenuConfig::HOTKEY_TOGGLE_WEB_RADAR:     MenuConfig::ShowWebRadar = !MenuConfig::ShowWebRadar; break;
				case MenuConfig::HOTKEY_TOGGLE_SAFE_ZONE:     MenuConfig::SafeZoneEnabled = !MenuConfig::SafeZoneEnabled; break;
				case MenuConfig::HOTKEY_TOGGLE_CROSSHAIR:     MenuConfig::CrosshairEnabled = !MenuConfig::CrosshairEnabled; break;
				case MenuConfig::HOTKEY_RELOAD_GAME:
					ProcessMgr.Detach();
					globalVars::gameState.store(AppState::SEARCHING_GAME);
					break;
#ifdef AIMBOT_ENABLED
				// Phase 4: 自瞄热键为状态查询型 (aim 模块用各自 config.hotkey 经
				// GetAsyncKeyState 自行判定), 此处仅占位避免 fallthrough, 不做 toggle。
				case MenuConfig::HOTKEY_AIMBOT:             break;
				case MenuConfig::HOTKEY_TRIGGERBOT:         break;
				case MenuConfig::HOTKEY_MAGNET_TRIGGERBOT:  break;
#endif
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
			bool shouldBumpSceneReset = false;
			if (flags & 0x4) { // Full
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
				shouldBumpSceneReset = true;
			} else if (flags & 0x2) { // Repair
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 1);
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1);
				shouldBumpSceneReset = true;
			} else if (flags & 0x1) { // Probe - lightweight TLB refresh, no scene reset
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_FAST, 1);
				VMMDLL_ConfigSet(ProcessMgr.HANDLE, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 1);
			}

			// Only bump scene reset for Repair/Full (address-invalidating refreshes).
			// Probe is a lightweight TLB refresh that doesn't invalidate addresses,
			// so entity cache should be preserved.
			if (shouldBumpSceneReset)
				SceneReset::BumpSceneReset();
		}
		catch (...) {
			// Silently swallow 闁?keep the loop alive
		}
	}
}

// =====================================================================
//  AimThread 闁?Phase 4 自瞄系统主循环 (200Hz)
//
//  职责:
//    1. DMA 健康检查 (Failed 时跳过)
//    2. 硬件配置应用 (type 变更或 hwReapplyRequested 时重建 rapidjson + ApplyConfig)
//    3. 同步 AimConfig -> 各 aim 模块单例的 public 配置字段
//    4. 调用 AimBot / TriggerBot / MagnetTriggerBot 的 Run()
//
//  数据来源: Cheats::GetSnapshot() (双缓冲, 与渲染线程共享)
//  DMA 只读: 所有鼠标动作经 Hw::HwManager::Get() 下发
// =====================================================================

#ifdef AIMBOT_ENABLED
namespace {

// 将 AimConfig::Hardware() 转为 rapidjson::Value 并应用 (HwManager API 要求)
void ApplyHwConfig() {
	auto& hw = AimConfig::Hardware();
	rapidjson::Document doc;
	doc.SetObject();
	auto& alloc = doc.GetAllocator();
	doc.AddMember("type", rapidjson::Value(hw.type.c_str(), alloc), alloc);
	doc.AddMember("ip", rapidjson::Value(hw.net.ip.c_str(), alloc), alloc);
	doc.AddMember("port", hw.net.port, alloc);
	doc.AddMember("uuid", rapidjson::Value(hw.net.uuid.c_str(), alloc), alloc);
	// BPro / Makcu 共用 comPort/baudRate 字段, 按当前类型选取对应值
	const std::string& comPort = (hw.type == "makcu") ? hw.makcu.comPort : hw.bpro.comPort;
	int baudRate = (hw.type == "makcu") ? hw.makcu.baudRate : hw.bpro.baudRate;
	doc.AddMember("comPort", rapidjson::Value(comPort.c_str(), alloc), alloc);
	doc.AddMember("baudRate", baudRate, alloc);
	Hw::HwManager::Get().ApplyConfig(doc);
}

// 将 AimConfig 全局字段同步到各 aim 模块单例的 public 配置字段
// (模块不 #include AimConfig.h, 保持解耦; AimThread 负责推送)
void SyncAimConfig() {
	// 全局配置 (teamCheck/predictionTimeMs/ignoreOnShot)
	auto& g = AimConfig::Global();

	// AimBot
	auto& ac = AimConfig::AimBot();
	auto& aim = Aim::AimBot::Get();
	aim.enabled           = ac.enabled;
	aim.hotkey            = ac.hotkey;
	aim.fov               = ac.fov;
	aim.smooth            = ac.smooth;
	aim.bone              = ac.bone;
	aim.visualCheck       = ac.visualCheck;
	aim.boneFallback      = ac.boneFallback;
	aim.teamCheck         = g.teamCheck;
	aim.ignoreOnShot      = g.ignoreOnShot;
	aim.targetSwitchDelay = ac.targetSwitchDelay;
	aim.predictionTimeMs  = g.predictionTimeMs;
	for (int i = 0; i < 6; i++) {
		aim.perWeapon[i].fov    = ac.perWeapon[i].fov;
		aim.perWeapon[i].smooth = ac.perWeapon[i].smooth;
		aim.perWeapon[i].bone   = ac.perWeapon[i].bone;
	}

	// 后坐力补偿: SprayControl 或 RCS (根据 mode 二选一)
	auto& spray = Aim::SprayControl::Get();
	auto& rcs   = Aim::RecoilControl::Get();
	if (g.spray.mode == 1) {
		// RCS 模式: 启用 RCS, 禁用 SprayControl
		rcs.config.enabled     = g.spray.enabled;
		rcs.config.scale       = g.spray.strength;
		rcs.config.sensitivity = g.spray.sensitivity;
		spray.config.enabled   = false;
		spray.config.strength  = g.spray.strength;
	} else {
		// SprayControl 模式: 启用 SprayControl, 禁用 RCS
		spray.config.enabled     = g.spray.enabled;
		spray.config.strength    = g.spray.strength;
		spray.config.sensitivity = g.spray.sensitivity;
		rcs.config.enabled       = false;
	}

	// TriggerBot
	auto& tc = AimConfig::TriggerBot();
	auto& trig = Aim::TriggerBot::Get();
	trig.config.enabled      = tc.enabled;
	trig.config.hotkey       = tc.hotkey;
	trig.config.mode         = tc.mode;
	trig.config.delay        = tc.delay;
	trig.config.delayJitter  = tc.delayJitter;
	trig.config.holdMs       = tc.holdMs;
	trig.config.teamCheck    = g.teamCheck;
	trig.config.ignoreOnShot = g.ignoreOnShot;

	// Magnet
	auto& mc = AimConfig::Magnet();
	auto& mag = Aim::MagnetTriggerBot::Get();
	mag.config.enabled            = mc.enabled;
	mag.config.hotkey             = mc.hotkey;
	mag.config.fov                = mc.fov;
	mag.config.smooth             = mc.smooth;
	mag.config.bone               = mc.bone;
	mag.config.visualCheck        = mc.visualCheck;
	mag.config.boneFallback       = mc.boneFallback;
	mag.config.targetSwitchDelay  = mc.targetSwitchDelay;
	mag.config.teamCheck          = g.teamCheck;
	mag.config.ignoreOnShot       = g.ignoreOnShot;
	mag.config.predictionTimeMs   = g.predictionTimeMs;
	for (int i = 0; i < 6; i++) {
		mag.config.perWeapon[i].fov    = mc.perWeapon[i].fov;
		mag.config.perWeapon[i].smooth = mc.perWeapon[i].smooth;
		mag.config.perWeapon[i].bone   = mc.perWeapon[i].bone;
	}
}

} // namespace

VOID AimThread()
{
	static std::string lastHwType = "none";
	bool firstHwApply = true;

	while (true) {
#ifdef AIMBOT_ENABLED
		// 多点校验：每 200 次循环检查一次（~1秒@200Hz）
		static int s_aimAntiTamper = 0;
		if (++s_aimAntiTamper >= 200) {
			s_aimAntiTamper = 0;
			if (!AntiTamper::PeriodicCheck())
				return;
		}
#endif
		try {
			Sleep(5);  // 200Hz

			if (globalVars::gameState.load() == AppState::EXITING)
				return;

			// DMA 健康检查: Failed 时跳过本帧 (避免无效读取)
			if (g_dmaHealth.GetState() == DmaHealth::DmaHealthState::Failed)
				continue;

			// 硬件配置应用: type 变更或 UI 请求重连时触发
			{
				auto& hw = AimConfig::Hardware();
				bool typeChanged = (hw.type != lastHwType);
				bool reapply = AimConfig::hwReapplyRequested.exchange(false);
				if (firstHwApply || typeChanged || reapply) {
					ApplyHwConfig();
					lastHwType = hw.type;
					firstHwApply = false;
				}
			}

			// 仅在游戏运行时执行自瞄逻辑
			if (globalVars::gameState.load() != AppState::RUNNING)
				continue;

			// 同步配置 -> 模块单例
			SyncAimConfig();

			// 统一查询按键状态 (左键串口查询一次, 各模块只读 atomic)
			Aim::InputManager::Get().Update();

			// 获取快照 (双缓冲, 复制保证一致)
			GameSnapshot snap = Cheats::GetSnapshot();
			const CEntity& local = snap.LocalPlayer;

			// 本地玩家无效时跳过 (aim 模块内部也会判断, 此处提前减少无谓工作)
			if (local.Pawn.Address == 0 || local.Pawn.Health <= 0)
				continue;

			// 调用模块 (各模块只计算 pending, 不直接下发):
			//   1. SprayControl 更新状态 (压枪回放由独立线程计算 pending)
			//   2. RecoilControl 计算 pending
			//   3. AimBot 计算自瞄移动量存 pending
			//   4. TriggerBot 自动开火 (按键操作, 非移动)
			//   5. MagnetTriggerBot 计算磁力移动量存 pending
			Aim::SprayControl::Get().UpdateState(local);
			Aim::RecoilControl::Get().Run(local);
			Aim::AimBot::Get().Run(local, snap.Entities);
			Aim::TriggerBot::Get().Run(local);
			Aim::MagnetTriggerBot::Get().Run(local, snap.Entities);

			// 统一收集所有 pending, 合并后一次 MoveRelative 下发
			int aimX = 0, aimY = 0;
			Aim::AimBot::Get().GetAndClearPending(aimX, aimY);
			int magnetX = 0, magnetY = 0;
			Aim::MagnetTriggerBot::Get().GetAndClearPending(magnetX, magnetY);
			int sprayX = 0, sprayY = 0;
			Aim::SprayControl::Get().GetAndClearPending(sprayX, sprayY);
			int totalX = aimX + magnetX + sprayX;
			int totalY = aimY + magnetY + sprayY;
			if (totalX != 0 || totalY != 0) {
				Hw::HwManager::Get().MoveRelative(totalX, totalY);
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR("Aim", "AimThread exception: {}", e.what());
		} catch (...) {
			LOG_ERROR("Aim", "AimThread unknown exception");
		}
	}
}
#endif

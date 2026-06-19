#include "Threads.h"

#include "../render/GrenadeHelper.h"
#include "MenuConfig.h"
#include "../config/ConfigSaver.h"
#include "../utils/Logger.h"
#include "intervals.h"
#include "SceneReset.h"

#include <winnt.h>
#include <windows.h>
#include <immintrin.h>
#include <thread>
#include <cmath>
#include <set>
#include <chrono>
#include <unordered_map>
#include <algorithm>

// =====================================================================
//  ConnectionThread — manages game process lifecycle
//
//  States:
//    SEARCHING_GAME    → try Attach("cs2.exe") every 1s
//    INITIALIZING_GAME → call InitAddress(), transition to RUNNING
//    RUNNING           → periodically check process alive
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
			} else if (status == FAILE_MODULE) {
				LOG_WARNING("Connection", "cs2.exe found but client.dll not decrypted, waiting...");
				searchAttempts = 0;
				globalVars::gameState.store(AppState::WAITING_DECRYPT);
			} else {
				Sleep(3000);
			}
			break;
		}
		case AppState::INITIALIZING_GAME:
		{
			static int initValidateRetries = 0;
			LOG_DEBUG("Connection", "Initializing game addresses...");
			if (gGame.InitAddress()) {
			// Post-init validation: refresh DMA + verify data is actually accessible
			RequestDmaRefresh(DmaRefreshTier::Full);
			Sleep(500);
				// Verify client.dll is accessible (decryption check)
				PVMMDLL_MAP_MODULEENTRY pModuleEntry = nullptr;
				BOOL moduleOk = VMMDLL_Map_GetModuleFromNameU(ProcessMgr.HANDLE, ProcessMgr.ProcessID, (LPSTR)"client.dll", &pModuleEntry, NULL);
				if (moduleOk && pModuleEntry) {
					VMMDLL_MemFree(pModuleEntry);
				} else {
					LOG_WARNING("Connection", "client.dll not accessible after InitAddress, waiting for decryption...");
					initValidateRetries++;
					globalVars::gameState.store(AppState::WAITING_DECRYPT);
					break;
				}
				DWORD64 testCtrl = 0;
				if (ProcessMgr.ReadMemory(gGame.GetLocalControllerAddress(), testCtrl) && testCtrl != 0) {
					LOG_INFO("Connection", "Game addresses initialized and validated (ctrl=0x{:X})", testCtrl);
					initValidateRetries = 0;
					globalVars::gameState.store(AppState::RUNNING);
				} else if (initValidateRetries < 10) {
					initValidateRetries++;
					LOG_DEBUG("Connection", "Validation failed (ctrl=0x{:X}), retry {}/10", testCtrl, initValidateRetries);
					Sleep(3000);
					// Stay in INITIALIZING_GAME — next loop will re-run InitAddress
				} else {
					LOG_WARNING("Connection", "Validation failed after 10 retries, entering RUNNING anyway");
					initValidateRetries = 0;
					globalVars::gameState.store(AppState::RUNNING);
				}
			} else {
				LOG_WARNING("Connection", "Failed to init addresses, retrying...");
				ProcessMgr.Detach();
				globalVars::gameState.store(AppState::SEARCHING_GAME);
			}
			break;
		}
		case AppState::WAITING_DECRYPT:
		{
			LOG_DEBUG("Connection", "Waiting for client.dll decryption...");
		RequestDmaRefresh(DmaRefreshTier::Full);
		Sleep(3000);
			PVMMDLL_MAP_MODULEENTRY pModuleEntry = nullptr;
			BOOL moduleOk = VMMDLL_Map_GetModuleFromNameU(ProcessMgr.HANDLE, ProcessMgr.ProcessID, (LPSTR)"client.dll", &pModuleEntry, NULL);
			if (moduleOk && pModuleEntry) {
				VMMDLL_MemFree(pModuleEntry);
				LOG_INFO("Connection", "client.dll decrypted, resuming initialization");
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
// and bump its priority. backFromLastCore = 0 → last core, 1 → second-to-last.
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
//  DataThread — optimized data pipeline
//
//  Optimizations:
//    - Feature-gated scatter: only read fields needed by active menu features
//    - Entity caching: reuse controller data across frames, re-discover every N frames
//    - Dead field removal: spottedMask/aimPunch/shotsFired/fFlags/teamID removed from scatter
//    - Scattered entity discovery: 64 sequential reads → 1 scatter batch
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

	// Scatter buffers — only fields actually consumed by render
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
	static int s_coreReadFailStreak[MAX_ENTITIES] = {};
	static int64_t s_coreReadFailSinceUs[MAX_ENTITIES] = {};
	constexpr int CORE_READ_EVICT_THRESHOLD = 96;

	// Tiered update frequency — microsecond intervals (P2)
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

	// Consecutive read failure counter — triggers tiered DMA refresh (P3 Task 9)
	// 100→Probe, 300→Repair, 500→Full (+ InitAddress + reset)
	int consecutiveFailCount = 0;

	// Stale data counter — matrix all-zero means DMA cache may be out of date
	int staleDataCount = 0;
	constexpr int STALE_DATA_THRESHOLD = 2000; // ~2s of all-zero matrix → refresh DMA cache

	while (true)
	{
		try {
			PreciseSleepUs(1000);

			if (globalVars::gameState.load() != AppState::RUNNING) {
				Sleep(100);
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
			                  MenuConfig::ShowOffscreenArrows || MenuConfig::ShowSoundESP ||
			                  MenuConfig::ShowPlayerFlags;
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

			// Get2DBox uses head bone → bones required for any ESP drawing
			bool needBones = anyESPDraw;
			bool needViewAngle = MenuConfig::ShowEyeRay || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled || MenuConfig::ShowOffscreenArrows;
			bool needCameraPos = MenuConfig::ShowEyeRay;
			bool needWeapon = MenuConfig::ShowWeaponESP || MenuConfig::ShowWebRadar || GrenadeHelper::Enabled;

			// ------- 1. Read matrix -------
		if (!ProcessMgr.ReadMemory(gGame.GetMatrixAddress(), matrix, 64)) {
			LOG_TRACE("Data", "ReadMemory matrix FAILED (addr=0x{:X})", gGame.GetMatrixAddress());
			consecutiveFailCount++;
			if (consecutiveFailCount >= 500) {
				LOG_WARNING("Data", "{} consecutive read failures, requesting Full DMA refresh", consecutiveFailCount);
				RequestDmaRefresh(DmaRefreshTier::Full);
				gGame.InitAddress();
				SceneReset::BumpSceneReset();
				consecutiveFailCount = 0;
			} else if (consecutiveFailCount >= 300) {
				RequestDmaRefresh(DmaRefreshTier::Repair);
			} else if (consecutiveFailCount >= 100) {
				RequestDmaRefresh(DmaRefreshTier::Probe);
			}
			continue;
		}
		consecutiveFailCount = 0;

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
		}
			continue;
		}
			staleDataCount = 0;

			memcpy(gGame.View.Matrix, matrix, 64);

			// ------- 2. Read local player addresses -------
			DWORD64 localControllerAddr = 0;
			DWORD64 localPawnAddr = 0;
			if (!ProcessMgr.ReadMemory(gGame.GetLocalControllerAddress(), localControllerAddr)) {
				LOG_TRACE("Data", "ReadMemory localController FAILED");
				continue;
			}
			if (!ProcessMgr.ReadMemory(gGame.GetLocalPawnAddress(), localPawnAddr)) {
				LOG_TRACE("Data", "ReadMemory localPawn FAILED");
				continue;
			}

			LOG_TRACE("Data", "Local: ctrl=0x{:X} pawn=0x{:X}", localControllerAddr, localPawnAddr);

			// Detect match entry: pawn 0→non-zero means player just entered a match
	if (localPawnAddr != 0 && localPawnAddrCached == 0) {
		LOG_INFO("Data", "Player entered match (pawn 0→0x{:X}), refreshing DMA cache", localPawnAddr);
		RequestDmaRefresh(DmaRefreshTier::Full);
		gGame.UpdateEntityListEntry();
		SceneReset::BumpSceneReset();
		// Reset entity cache to discard stale menu/loading data so the next
		// discovery frame rebuilds the player list from a clean state.
		entityCache.clear();
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
					// Player dead or pawn invalid — mark health 0, keep processing
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

			// ------- 3-7. Entity pipeline (skip when only projectile ESP is on) -------
			if (needEntityPipeline) {

			bool isDiscoveryFrame = ((now - lastDiscoveryUs) >= intervals::kDiscoveryUs) || entityCache.empty();
		if (isDiscoveryFrame) lastDiscoveryUs = now;

			if (isDiscoveryFrame) {
				LOG_TRACE("Data", "--- Discovery frame (cache_size={}) ---", entityCache.size());
			// Refresh DMA cache only when cache is empty (just entered match)
			// or when we detect stale data. Not every frame — too expensive.
			if (entityCache.empty()) {
				RequestDmaRefresh(DmaRefreshTier::Full);
			}

				// Refresh EntityListEntry every discovery frame — the pointer can change
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
			// miss loop: incomplete scan → small cache → small hint → small range.
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

				// Build address→index map for O(1) lookups into old cache
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
					if (entityAddr == 0) continue;
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
								if (ctrlBuf[i].isAlive != 1 || ctrlBuf[i].pawn == 0) continue;
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

					// Phase 4+5: GameSceneNode → BoneArray (batched, 1 page per entity)
					constexpr int DISC_BATCH = 6;
					DWORD64 sceneNodes[MAX_ENTITIES]{};
					DWORD64 boneArrays[MAX_ENTITIES]{};
					{
						VMMDLL_SCATTER_HANDLE h = nullptr;
						int bc = 0;
						for (int a = 0; a < aliveCount; a++) {
							int i = aliveSlots[a];
							if (pawnAddresses[i] == 0) continue;
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
						if (pawnAddresses[i] == 0 || boneArrays[i] == 0)
							continue;

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
						ent.Pawn.BoneData.BoneArrayAddress = boneArrays[i];

						CachedEntity ce;
						ce.controllerAddr = entityAddresses[i];
						ce.pawnAddr = pawnAddresses[i];
						ce.sceneNodeAddr = sceneNodes[i];
						ce.boneArrayAddr = boneArrays[i];
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
						}

						newCache.push_back(ce);
					}

					// Retain dead entities from previous cache for WebRadar continuity.
					// Without this, C4 killing everyone → empty cache → empty m_players → "waiting for data".
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
			entityCache = std::move(newCache);
		}

			// ------- 4. Scatter read dynamic fields (2-pass: refresh bone addresses every frame) -------
			{
				int count = (int)entityCache.size();
				if (count > MAX_ENTITIES) count = MAX_ENTITIES;

				// --- Pass 1: pos, health, viewAngle, cameraPos + fresh BoneArray pointer ---
				// Pawn offsets span 3 pages: health@0x354(page0), pos@0x1588(page1), eyeAngles@0x3DD0(page3)
				// Plus sceneNode BoneArray@0x1D0 = 4 unique pages per entity. Batch=2 → 8 pages.
				DWORD64 freshBoneArrays[MAX_ENTITIES]{};
				{
					for (int i = 0; i < count; i++)
						memset(&scatterBuf[i], 0, sizeof(ScatterBuf));
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
						ProcessMgr.AddScatterReadRequest(handle, pawn + Offset::Pos, &buf.pos, sizeof(Vec3));
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
						for (int i = batchStart; i < batchEnd; i++)
							s_coreReadFailStreak[i] = 0;
					} else {
						for (int i = batchStart; i < batchEnd; i++) {
							auto& ce = entityCache[i];
							auto& buf = scatterBuf[i];
							DWORD64 pawn = ce.pawnAddr;
							VMMDLL_SCATTER_HANDLE slotHandle = ProcessMgr.CreateScatterHandle();
							if (!slotHandle) {
								if (s_coreReadFailStreak[i] == 0)
									s_coreReadFailSinceUs[i] = now;
								s_coreReadFailStreak[i]++;
								continue;
							}
							ProcessMgr.AddScatterReadRequest(slotHandle, pawn + Offset::Pos, &buf.pos, sizeof(Vec3));
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
								s_coreReadFailStreak[i] = 0;
							} else {
								if (s_coreReadFailStreak[i] == 0)
									s_coreReadFailSinceUs[i] = now;
								s_coreReadFailStreak[i]++;
							}
						}
					}
				}

				// Per-slot eviction: persistent failure → drop pawn, rediscover next discovery frame
				for (int i = 0; i < count; i++) {
					if (s_coreReadFailStreak[i] >= CORE_READ_EVICT_THRESHOLD) {
						LOG_WARNING("Data", "Evicting entity slot {} (pawn=0x{:X}) after {} core read failures",
							i, entityCache[i].pawnAddr, s_coreReadFailStreak[i]);
						entityCache[i].pawnAddr = 0;
						s_coreReadFailStreak[i] = 0;
					}
				}

					// Local player dynamic fields (1 page, separate to avoid skip when count==0)
					{
						VMMDLL_SCATTER_HANDLE handle = ProcessMgr.CreateScatterHandle();
						if (handle) {
							DWORD64 lp = localPlayer.Pawn.Address;
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::Pos, &localBuf.pos, sizeof(Vec3));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::CurrentHealth, &localBuf.health, sizeof(int));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::PawnArmor, &localBuf.armor, sizeof(int));
							ProcessMgr.AddScatterReadRequest(handle, lp + Offset::flFlashDuration, &localBuf.flashDuration, sizeof(float));
							if (needViewAngle)
								ProcessMgr.AddScatterReadRequest(handle, lp + Offset::angEyeAngles, &localBuf.viewAngle, sizeof(Vec2));
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

					if (!IsValidPos(buf.pos) || !IsValidHealth(buf.health)) {
						ce.entity.Pawn.Health = 0;
						ce.entity.Pawn.ScreenPosValid = false;
					} else {
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
				Vec3 velocityBuf[MAX_ENTITIES];
				int pingBuf[MAX_ENTITIES]{};
				DWORD shotsBuf[MAX_ENTITIES]{};
				static DWORD lastShotsFired[MAX_ENTITIES];
				uint8_t localScoped = 0, localDefusing = 0;
				Vec3 localVelocity{};
				int localPing = 0;

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
						if (Offset::iPing)
							ProcessMgr.AddScatterReadRequest(h, ce.controllerAddr + Offset::iPing, &pingBuf[i], sizeof(int));
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
						if (Offset::iPing && localPlayer.Controller.Address != 0)
							ProcessMgr.AddScatterReadRequest(h, localPlayer.Controller.Address + Offset::iPing, &localPing, sizeof(int));
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
					if (Offset::iPing)
						ce.entity.Pawn.Ping = pingBuf[i];
					// Task 10: Sound ESP — fire ripple when ShotsFired increases.
					if (MenuConfig::ShowSoundESP && Offset::iShotsFired) {
						if (shotsBuf[i] > lastShotsFired[i]) {
							uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now().time_since_epoch()).count();
							ce.entity.Pawn.SoundUntilMs = nowMs + 420;
						}
						lastShotsFired[i] = shotsBuf[i];
					}
					// HasBomb: infer from weapon list (c4 carrier)
					ce.entity.Pawn.HasBomb = false;
					for (const auto& w : ce.entity.Pawn.WeaponList) {
						if (w == "c4") { ce.entity.Pawn.HasBomb = true; break; }
					}
				}
				if (localPlayer.Pawn.Address != 0 && localPlayer.Pawn.Health > 0) {
					if (Offset::bIsScoped)
						localPlayer.Pawn.Scoped = localScoped != 0;
					if (Offset::bIsDefusing)
						localPlayer.Pawn.Defusing = localDefusing != 0;
					if (Offset::vecVelocity)
						localPlayer.Pawn.Velocity = localVelocity;
					if (Offset::iPing)
						localPlayer.Pawn.Ping = localPing;
					localPlayer.Pawn.HasBomb = false;
					for (const auto& w : localPlayer.Pawn.WeaponList) {
						if (w == "c4") { localPlayer.Pawn.HasBomb = true; break; }
					}
				}
			}
		}

			// ------- 6. Weapon names (low frequency, only if feature needs it) -------
		if (needWeapon) {
			if ((now - lastWeaponUpdateUs) >= intervals::kWeaponUpdateUs) {
				lastWeaponUpdateUs = now;
				// --- Active weapon names via scatter batch read ---
					// Collect all player slots (local + alive entities)
					constexpr int MAX_WPN_SLOTS = MAX_ENTITIES + 1;
					DWORD64 wpnPawnAddrs[MAX_WPN_SLOTS]{};
					std::string* wpnNameOut[MAX_WPN_SLOTS]{};
					int wpnSlotCount = 0;
					if (localPlayer.Pawn.Address != 0) {
						wpnPawnAddrs[wpnSlotCount] = localPlayer.Pawn.Address;
						wpnNameOut[wpnSlotCount] = &localPlayer.Pawn.WeaponName;
						wpnSlotCount++;
					}
					for (auto& ce : entityCache) {
						if (ce.pawnAddr != 0 && ce.entity.Pawn.Health > 0 && wpnSlotCount < MAX_WPN_SLOTS) {
							wpnPawnAddrs[wpnSlotCount] = ce.pawnAddr;
							wpnNameOut[wpnSlotCount] = &ce.entity.Pawn.WeaponName;
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

						// Parse weapon names (same logic as GetWeaponName)
						for (int i = 0; i < wpnSlotCount; i++) {
							if (!nameAddrs[i]) continue;
							nameBufs[i][63] = '\0';
							if (!memchr(nameBufs[i], 0, 64)) continue;
							std::string s(nameBufs[i]);
							if (s.empty()) continue;
							auto pos = s.find("_");
							if (pos == std::string::npos) continue;
							*wpnNameOut[i] = s.substr(pos + 1);
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
		if (MenuConfig::ShowWebRadar) {
			if ((now - lastWrExtraUs) >= intervals::kWrExtraUs) {
				lastWrExtraUs = now;
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

					// --- Bomb data (every ~50ms) ---
					BombData bombSnap{};
					DWORD64 clientBase = gGame.GetClientDLLAddress();

					// Planted bomb — dwPlantedC4 is a CUtlVector data ptr, need:
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
							{
								VMMDLL_SCATTER_HANDLE h = ProcessMgr.CreateScatterHandle();
								if (h) {
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::C4Blow, &blowTime, sizeof(float));
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::BombDefused, &defused, sizeof(uint8_t));
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::BeingDefused, &defusing, sizeof(uint8_t));
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::DefuseCountDown, &defuseCD, sizeof(float));
									ProcessMgr.AddScatterReadRequest(h, plantedEntity + Offset::GameSceneNode, &sceneNode, sizeof(DWORD64));
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
				}

				// --- Model name + full weapon list (low frequency ~5s) ---
			if ((now - lastWrSlowUs) >= intervals::kWrSlowUs) {
				lastWrSlowUs = now;
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

					// Scatter batch resolve: subList → weaponAddr → m_pEntity → nameAddr → name string
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
	static uint64_t s_projCacheResetSerial = 0;

	if (MenuConfig::ShowProjectileESP) {
		LOG_TRACE("Data", "Projectile ESP scan (cache={})", projectileCache.size());

		// P5 Task 15: Invalidate cache on scene reset
		uint64_t currentSerial = SceneReset::CurrentSerial();
		if (currentSerial != s_projCacheResetSerial) {
			memset(s_cachedProjEntityAddrs, 0, sizeof(s_cachedProjEntityAddrs));
			memset(s_cachedProjSceneNodes, 0, sizeof(s_cachedProjSceneNodes));
			memset(s_cachedProjPositions, 0, sizeof(s_cachedProjPositions));
			memset(s_cachedProjTypes, 0, sizeof(s_cachedProjTypes));
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

		// P5 Task 14: Idle detection — double interval after 3 consecutive empty scans
		if (s_worldIdleStreak >= 3) {
			worldScanUs *= 2;
		}

		if ((now - lastProjectileScanUs) >= worldScanUs) {
			lastProjectileScanUs = now;
			std::vector<GrenadeProjectile> newProjectiles;

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

					// P5 Task 15: Cache check — split slots into cache-hit and cache-miss
					std::vector<int> cacheMissIdx;
					for (int i = shardStart; i < shardEnd; i++) {
						if (entAddrs[i] != 0 && entAddrs[i] == s_cachedProjEntityAddrs[i]) {
							// Cache hit — reuse cached type/sceneNode/position
						} else {
							if (entAddrs[i] != 0) cacheMissIdx.push_back(i);
							s_cachedProjEntityAddrs[i] = entAddrs[i];
							s_cachedProjSceneNodes[i] = 0;
							s_cachedProjPositions[i] = Vec3{};
							s_cachedProjTypes[i] = 0;
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
						struct ProjCandidate { int idx; GrenadeProjectileType type; float radius; bool fromCache; };
						std::vector<ProjCandidate> candidates;
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
							if (type == PROJ_UNKNOWN) continue;
							candidates.push_back({ i, type, radius, false });
							s_cachedProjTypes[i] = static_cast<uint8_t>(type) + 1;
						}

						// Phase 5b: Add cache-hit slots that were projectiles last scan
						for (int i = shardStart; i < shardEnd; i++) {
							if (entAddrs[i] != 0 && entAddrs[i] == s_cachedProjEntityAddrs[i] && s_cachedProjTypes[i] != 0) {
								GrenadeProjectileType type = static_cast<GrenadeProjectileType>(s_cachedProjTypes[i] - 1);
								float radius = 0;
								if (type == PROJ_HE) radius = 350.f;
								else if (type == PROJ_MOLOTOV) radius = 150.f;
								candidates.push_back({ i, type, radius, true });
							}
						}

						if (!candidates.empty()) {
							// Phase 6: Scatter-read GameSceneNode for cache-miss candidates (batched)
							// P5 Task 15: cache-hit candidates reuse s_cachedProjSceneNodes
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
								if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
							}

							// Phase 7: Scatter-read positions for cache-miss candidates (batched)
							// P5 Task 15: cache-hit candidates reuse s_cachedProjPositions
							{
								VMMDLL_SCATTER_HANDLE h = nullptr;
								int bc = 0;
								for (auto& c : candidates) {
									if (c.fromCache) continue;
									if (s_cachedProjSceneNodes[c.idx] == 0) continue;
									if (!h) { h = ProcessMgr.CreateScatterHandle(); if (!h) continue; bc = 0; }
									ProcessMgr.AddScatterReadRequest(h, s_cachedProjSceneNodes[c.idx] + Offset::vecAbsOrigin, &s_cachedProjPositions[c.idx], sizeof(Vec3));
									if (++bc >= PROJ_RAND_BATCH) {
										ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); h = nullptr; bc = 0;
									}
								}
								if (h) { ProcessMgr.ExecuteReadScatter(h); VMMDLL_Scatter_CloseHandle(h); }
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
								newProjectiles.push_back(proj);
							}

							// Clean up expired set: remove entries whose entities left the list
							std::set<DWORD64> stillInList;
							for (auto& c : candidates) stillInList.insert(entAddrs[c.idx]);
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
							else if (old.Type == PROJ_SMOKE) maxLinger = std::max(0.f, 18.0f - old.StationaryTimer);
							if (old.DisappearTimer < maxLinger) {
								newProjectiles.push_back(old);
							}
						}
					}

					// Expire projectiles that exceeded their stationary duration
					// HE: immediate on stop, Smoke: 18s
					for (auto& p : newProjectiles) {
						if (!p.Alive) continue;
						bool expired = false;
						if (p.Type == PROJ_HE && p.StationaryTimer > 0.3f) expired = true;
						if (p.Type == PROJ_SMOKE && p.StationaryTimer > 18.0f) expired = true;
						if (expired) expiredEntities.insert(p.EntityAddr);
					}
					newProjectiles.erase(
						std::remove_if(newProjectiles.begin(), newProjectiles.end(),
							[](const GrenadeProjectile& p) {
								if (!p.Alive) return false;
								if (p.Type == PROJ_HE && p.StationaryTimer > 0.3f) return true;
								if (p.Type == PROJ_SMOKE && p.StationaryTimer > 18.0f) return true;
								return false;
							}),
						newProjectiles.end());

					projectileCache = std::move(newProjectiles);
				}
			} else {
				projectileCache.clear();
				expiredEntities.clear();
			}

			// ------- 9. Build entity list and publish snapshot -------
			{
				std::vector<CEntity> publishEntities;
				publishEntities.reserve(entityCache.size());
				for (const auto& ce : entityCache) {
					publishEntities.push_back(ce.entity);
				}

				// ------- 9b. Spectator detection -------
				std::vector<SpectatorInfo> spectators;
				if (MenuConfig::ShowSpectatorList && localPawnAddr != 0) {
					DWORD localPawnHandle = 0;
					// Get local pawn's entity handle (index lower 16 bits)
					// The local pawn address is known; we need its handle for comparison
					// m_hObserverTarget stores a CHandle — lower 16 bits = entity index
					for (const auto& ce : entityCache) {
						if (ce.entity.Controller.AliveStatus != 1 && ce.pawnAddr != 0) {
							// Dead player — check if spectating
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
			newSnap.Spectators = std::move(spectators);
			// ESP gap-closure stage 2: stamp capture time for render-loop interpolation.
			newSnap.CaptureTimeUs = now;

			// Preserve low-frequency fields this publish path does not refresh:
			//   MapName — written by SlowUpdateThread (~10s)
			//   Bomb    — written by the WebRadar-extra block above (~50ms)
			// A shared_lock keeps the read exclusive against the in-place writers.
			{
				std::shared_lock<std::shared_mutex> lock(Cheats::SnapshotMutex);
				const GameSnapshot& cur = Cheats::GetSnapshot();
				memcpy(newSnap.MapName, cur.MapName, sizeof(newSnap.MapName));
				newSnap.Bomb = cur.Bomb;
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
//  SlowUpdateThread — low-frequency updates
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
//  KeysCheckThread — keyboard polling (unchanged logic)
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
//  DmaAdminThread — asynchronous VMMDLL_ConfigSet refresh processing
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
			// Silently swallow — keep the loop alive
		}
	}
}

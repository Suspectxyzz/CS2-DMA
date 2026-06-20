#pragma once

#include "../game/Game.h"
#include "../game/Entity.h"

#include <mutex>
#include <shared_mutex>
#include <atomic>

// Bomb state for web radar
struct BombData
{
	float x = 0, y = 0, z = 0;
	float blowTime = 0;
	bool isPlanted = false;
	bool isDefused = false;
	bool isDefusing = false;
	float defuseTime = 0;
	DWORD carrierPawnHandle = 0;
	DWORD defuserPawnHandle = 0;  // C_PlantedC4::m_hBombDefuser & 0xFFFF
	bool isPlanting = false;      // C_C4::m_bIsPlantingViaUse
};

// Grenade projectile types
enum GrenadeProjectileType : uint8_t
{
	PROJ_FLASH = 0,
	PROJ_SMOKE = 1,
	PROJ_HE = 2,
	PROJ_MOLOTOV = 3,
	PROJ_DECOY = 4,
	PROJ_UNKNOWN = 255
};

// In-flight grenade projectile data
struct GrenadeProjectile
{
	Vec3 Position;
	float EffectRadius;           // game-units radius for range circle
	GrenadeProjectileType Type;
	DWORD64 EntityAddr;           // for dedup across frames
	bool Alive = true;            // still found in entity list
	float DisappearTimer = 0.f;   // seconds since entity disappeared
	float StationaryTimer = 0.f;  // seconds since position stopped changing
	// Task 6: thrower team (2=T, 3=CT). 0 = unknown / fallback pending.
	int32_t Team = 0;
	// Task 7.2-7.4: inferno (molotov/incendiary) multi-flame points.
	// CS2 C_Inferno::m_firePositions is a Vector[64] array; we capture up to 64.
	int FlameCount = 0;
	Vec3 Flames[64];
	// Whether the grenade effect has spawned (smoke cloud / fire / flash bang / HE explosion).
	// Read from m_nSmokeEffectTickBegin / m_nExplodeEffectTickBegin etc.
	bool Exploded = false;
	// Stable entity ID for the frontend (lower 32 bits of EntityAddr).
	// Used as a stable DOM element key so position changes don't create duplicates.
	uint32_t EntityId = 0;
};

// ESP gap-closure stage 3b: dropped weapon on the ground (Task 12/16).
// Populated by the world scan in Threads.cpp from entities whose designer
// name starts with "weapon_". ItemId is resolved via WeaponLookup so the
// renderer can fetch icon glyph / name / mask filtering in one shot.
struct DroppedWeapon
{
	Vec3 Position;
	uint16_t ItemId = 0;
	std::string Name;            // display name, e.g. "AK-47"
	DWORD64 EntityAddr = 0;      // for dedup across frames
};

// Spectator info: player who is spectating (observer)
struct SpectatorInfo
{
	std::string Name;
	int TeamID = 0;        // team of the spectator
	int ObserverMode = 0;  // OBS_MODE_IN_EYE=4, OBS_MODE_CHASE=5, etc.
};

// Performance stats for the perf monitor overlay
struct PerfStats
{
	float Framerate = 0.f;
	float FrameTimeMs = 0.f;
	int EntityCount = 0;
	int ProjectileCount = 0;
	int SpectatorCount = 0;
	float DataThreadHz = 0.f;
};

// Unified snapshot: single lock protects all shared state
struct GameSnapshot
{
	float Matrix[4][4]{};
	CEntity LocalPlayer;
	std::vector<CEntity> Entities;
	char MapName[32]{};
	int LocalPlayerIndex = -1;
	// Local player's ObserverTarget pawn handle (m_hObserverTarget).
	// 0 when not spectating (alive / first-person / unreadable).
	// Used by WebRadar to emit m_observed_idx.
	DWORD LocalObserverTarget = 0;
	BombData Bomb;
	std::vector<GrenadeProjectile> Projectiles;
	std::vector<SpectatorInfo> Spectators;
	// ESP gap-closure stage 3b: dropped weapons on the ground (Task 12/16).
	std::vector<DroppedWeapon> DroppedWeapons;
	// ESP gap-closure stage 2: capture timestamp (microseconds since steady_clock epoch)
	// used by the render loop for snapshot interpolation + velocity extrapolation.
	int64_t CaptureTimeUs = 0;
	char roundPhase[16] = "live";  // freezetime/live/over
};

namespace Cheats
{
	void Menu();
	void Run();

	// Double-buffered lock-free snapshot.
	// The hot path — DataThread publishing and render/WebRadar/Keys readers —
	// is lock-free: the writer builds a full snapshot, copies it into the
	// inactive buffer, then flips the atomic index (release). Readers fetch
	// the active buffer via GetSnapshot() (acquire), never blocking the writer.
	//
	// The shared_mutex is retained for low-frequency in-place writers that
	// patch a single field of the live buffer instead of republishing the
	// whole snapshot (SlowUpdateThread → MapName, DataThread → Bomb). These
	// run at 10s / 50ms cadence, so mutex contention with the hot path is
	// negligible; a torn read there costs at most one stale frame.
	inline GameSnapshot SnapshotBuf[2];
	inline std::atomic<int> SnapshotReadIdx{ 0 };

	inline const GameSnapshot& GetSnapshot() {
		return SnapshotBuf[SnapshotReadIdx.load(std::memory_order_acquire)];
	}

	inline void PublishSnapshot(const GameSnapshot& snap) {
		int writeIdx = 1 - SnapshotReadIdx.load(std::memory_order_relaxed);
		SnapshotBuf[writeIdx] = snap;
		SnapshotReadIdx.store(writeIdx, std::memory_order_release);
	}

	// Retained for low-frequency in-place writers (MapName, Bomb).
	inline std::shared_mutex SnapshotMutex;
}

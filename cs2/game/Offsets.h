#pragma once

#include <Windows.h>
#include <string>
#include <cstdint>

namespace Offset
{
	// Steam.inf info (current game version from SteamTracking repo)
	struct SteamInfInfo {
		std::string patchVersion;    // e.g. "1.40.6.5"
		std::string versionDate;     // e.g. "2026/06/19"
		std::string versionTime;     // e.g. "00:30:00"
		int clientVersion = 0;
		int sourceRevision = 0;
	};

	// Version info
	inline std::string GameUpdateDate;           // from version.json (local offset date)
	inline int64_t GameUpdateTimestamp = 0;      // from version.json
	inline std::string LocalPatchVersion;        // from version.json (game patch when offsets were dumped)
	inline std::string LatestPatchVersion;       // from steam.inf (current game patch)
	inline std::string LatestGameUpdateDate;     // from steam.inf (current game update date)
	inline bool VersionMismatch = false;         // true if local patch != latest patch
	inline DWORD EntityList;
	inline DWORD Matrix ;
	inline DWORD ViewAngles;               // dwViewAngles (client.dll) — global camera angles
	inline DWORD LocalPlayerController;
	inline DWORD LocalPlayerPawn;
	inline DWORD GlobalVars;
	inline DWORD PlantedC4;
	inline DWORD WeaponC4;
	inline DWORD GameRules;                    // dwGameRules (client.dll)
	inline DWORD GameRulesProxy_m_pGameRules;  // C_CSGameRulesProxy::m_pGameRules
	inline DWORD CSGameRules_m_bFreezePeriod;  // C_CSGameRules::m_bFreezePeriod
	inline DWORD CSGameRules_m_iRoundWinStatus;// C_CSGameRules::m_iRoundWinStatus
	inline DWORD PlantedC4_m_hBombDefuser;     // C_PlantedC4::m_hBombDefuser
	inline DWORD C4_m_bIsPlantingViaUse;       // C_C4::m_bIsPlantingViaUse


	inline	DWORD Health;
	inline	DWORD TeamID;
	inline	DWORD IsAlive;
	inline	DWORD PlayerPawn;
	inline	DWORD iszPlayerName;
	inline	DWORD MoneyService;

	
	inline DWORD Pos;
	inline DWORD CurrentHealth;
	inline DWORD Armor;
	inline DWORD PawnArmor;  // C_CSPlayerPawn::m_ArmorValue (authoritative, on pawn)
	inline DWORD GameSceneNode;
	inline DWORD BoneArray;
	inline DWORD angEyeAngles;
	inline DWORD vecLastClipCameraPos;
	inline DWORD iShotsFired;
	inline DWORD flFlashDuration;
	inline DWORD AimPunchServices;     // C_CSPlayerPawn::m_pAimPunchServices
	inline DWORD AimPunchAngleOffset;   // CCSPlayer_AimPunchServices::m_predictableBaseAngle
	inline DWORD iTeamNum;
	inline DWORD CameraServices;
	inline DWORD iFovStart;
	inline DWORD fFlags;
	inline DWORD bSpottedByMask;
	inline DWORD ItemServices;
	inline DWORD HasHelmet;
	inline DWORD HasDefuser;
	inline DWORD CompTeammateColor;
	inline DWORD vecAbsOrigin;
	inline DWORD ModelStateOffset;
	inline DWORD ModelNameOffset;
	// ESP gap-closure stage 1: extended tactical offsets
	inline DWORD bIsScoped;              // C_CSPlayerPawn::m_bIsScoped
	inline DWORD bIsDefusing;            // C_CSPlayerPawn::m_bIsDefusing
	inline DWORD vecVelocity;            // C_BaseEntity::m_vecVelocity
	inline DWORD iClip1;                 // C_BasePlayerWeapon::m_iClip1
	// Weapon item definition (for reliable weapon name lookup)
	inline DWORD AttributeManager;       // C_EconEntity::m_AttributeManager
	inline DWORD Item;                   // C_AttributeContainer::m_Item
	inline DWORD ItemDefinitionIndex;    // C_EconItemView::m_iItemDefinitionIndex
	// C_PlantedC4
	inline DWORD BombTicking;
	inline DWORD C4Blow;
	inline DWORD BombDefused;
	inline DWORD BeingDefused;
	inline DWORD DefuseCountDown;
	// Weapon list
	inline DWORD WeaponServices;
	inline DWORD MyWeapons;
	inline DWORD ActiveWeapon;

	// Entity identity (for projectile scanning)
	inline DWORD EntityIdentity;         // CEntityInstance::m_pEntity
	inline DWORD DesignerName;           // CEntityIdentity::m_designerName
	// Observer (spectator detection)
	inline DWORD ObserverServices;        // C_BasePlayerPawn::m_pObserverServices
	inline DWORD ObserverMode;             // CPlayer_ObserverServices::m_iObserverMode
	inline DWORD ObserverTarget;           // CPlayer_ObserverServices::m_hObserverTarget

	// Grenade projectile
	inline DWORD GrenadeThrower;          // C_BaseGrenade::m_hThrower
	// Task 7.2-7.4: C_Inferno multi-flame points
	inline DWORD InfernoFireCount;        // C_Inferno::m_fireCount
	inline DWORD InfernoFirePositions;    // C_Inferno::m_firePositions (Vector[64])
	// Grenade effect spawned signal (tick count > 0 means effect has begun)
	inline DWORD SmokeEffectTickBegin;     // C_SmokeGrenadeProjectile::m_nSmokeEffectTickBegin
	inline DWORD FireEffectTickBegin;      // C_Inferno::m_nFireEffectTickBegin
	inline DWORD ExplodeEffectTickBegin;  // C_BaseCSGrenadeProjectile::m_nExplodeEffectTickBegin

	struct
	{
		DWORD RealTime = 0x00;
		DWORD FrameCount = 0x04;
		DWORD MaxClients = 0x10;
		DWORD IntervalPerTick = 0x14;
		DWORD CurrentTime = 0x2C;
		DWORD CurrentTime2 = 0x30;
		DWORD TickCount = 0x40;
		DWORD IntervalPerTick2 = 0x44;
		DWORD CurrentNetchan = 0x0048;
		DWORD CurrentMap = 0x0180;
		DWORD CurrentMapName = 0x0188;
	} inline GlobalVar;

	bool UpdateOffsets(std::string offsetdata, std::string clientdata);
	bool ParseVersion(const std::string& versionData);
	bool ParseSteamInf(const std::string& data, SteamInfInfo& out);
	bool CheckGameVersion(const std::string& steamInfData);
	bool GenerateVersionFromInfo(const std::string& infoPath, const std::string& versionPath);
}

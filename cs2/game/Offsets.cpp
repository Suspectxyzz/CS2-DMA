#include "Offsets.h"
#include "AppState.h"
#include "../utils/Logger.h"

#include "rapidjson/document.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <ctime>

using namespace rapidjson;

// Helper function for safe nested JSON access
static uint64_t SafeGetUint64(const Value& obj, const char* key, uint64_t defaultVal = 0) {
	if (obj.HasMember(key) && obj[key].IsUint64())
		return obj[key].GetUint64();
	return defaultVal;
}

bool Offset::UpdateOffsets(std::string offsetdata, std::string clientdata)
{
	Document offsets, client;
	offsets.Parse(offsetdata.c_str());
	client.Parse(clientdata.c_str());

	offsetdata.clear(); clientdata.clear();

	// Check for parse errors
	if (offsets.HasParseError()) {
		LOG_ERROR("Config", "Failed to parse offsets.json (code: {})", (int)offsets.GetParseError());
		return false;
	}
	if (client.HasParseError()) {
		LOG_ERROR("Config", "Failed to parse client_dll.json (code: {})", (int)client.GetParseError());
		return false;
	}

	// Parse offsets.json - client.dll
	if (offsets.HasMember("client.dll") && offsets["client.dll"].IsObject()) {
		const auto& clientDll = offsets["client.dll"];
		Offset::EntityList = SafeGetUint64(clientDll, "dwEntityList");
		Offset::Matrix = SafeGetUint64(clientDll, "dwViewMatrix");
		Offset::LocalPlayerController = SafeGetUint64(clientDll, "dwLocalPlayerController");
		Offset::LocalPlayerPawn = SafeGetUint64(clientDll, "dwLocalPlayerPawn");
		Offset::GlobalVars = SafeGetUint64(clientDll, "dwGlobalVars");
		Offset::PlantedC4 = SafeGetUint64(clientDll, "dwPlantedC4");
		Offset::WeaponC4 = SafeGetUint64(clientDll, "dwWeaponC4");
		LOG_INFO("Offsets", "client.dll: EntityList=0x{:X} Matrix=0x{:X} LocalCtrl=0x{:X} LocalPawn=0x{:X} GlobalVars=0x{:X} PlantedC4=0x{:X} WeaponC4=0x{:X}",
			Offset::EntityList, Offset::Matrix, Offset::LocalPlayerController, Offset::LocalPlayerPawn, Offset::GlobalVars, Offset::PlantedC4, Offset::WeaponC4);
	} else {
		LOG_INFO("Offsets", "client.dll section NOT FOUND in offsets.json");
	}

	// Parse offsets.json - matchmaking.dll (optional)
	if (offsets.HasMember("matchmaking.dll") && offsets["matchmaking.dll"].IsObject()) {
		const auto& matchmaking = offsets["matchmaking.dll"];
		Offset::MapName = SafeGetUint64(matchmaking, "dwGameTypes_mapName");
	}

	// Parse client_dll.json
	if (client.HasMember("client.dll") && client["client.dll"].HasMember("classes") && client["client.dll"]["classes"].IsObject()) {
		const auto& classes = client["client.dll"]["classes"];

		// C_BaseEntity
		if (classes.HasMember("C_BaseEntity") && classes["C_BaseEntity"].HasMember("fields")) {
			const auto& fields = classes["C_BaseEntity"]["fields"];
			Offset::Health = SafeGetUint64(fields, "m_iHealth");
			Offset::TeamID = SafeGetUint64(fields, "m_iTeamNum");
			Offset::MaxHealth = SafeGetUint64(fields, "m_iMaxHealth");
			Offset::CurrentHealth = SafeGetUint64(fields, "m_iHealth");
			Offset::GameSceneNode = SafeGetUint64(fields, "m_pGameSceneNode");
			Offset::fFlags = SafeGetUint64(fields, "m_fFlags");
			Offset::OwnerEntity = SafeGetUint64(fields, "m_hOwnerEntity");
			Offset::vecVelocity = SafeGetUint64(fields, "m_vecVelocity");
			LOG_INFO("Offsets", "C_BaseEntity: Health=0x{:X} TeamID=0x{:X} GameSceneNode=0x{:X} fFlags=0x{:X}",
				Offset::Health, Offset::TeamID, Offset::GameSceneNode, Offset::fFlags);
		}

		// CCSPlayerController
		if (classes.HasMember("CCSPlayerController") && classes["CCSPlayerController"].HasMember("fields")) {
			const auto& fields = classes["CCSPlayerController"]["fields"];
			Offset::Armor = SafeGetUint64(fields, "m_iPawnArmor");
			Offset::IsAlive = SafeGetUint64(fields, "m_bPawnIsAlive");
			Offset::MoneyService = SafeGetUint64(fields, "m_pInGameMoneyServices");
			Offset::PlayerPawn = SafeGetUint64(fields, "m_hPlayerPawn");
			Offset::CompTeammateColor = SafeGetUint64(fields, "m_iCompTeammateColor");
			Offset::iPing = SafeGetUint64(fields, "m_iPing");
			LOG_INFO("Offsets", "CCSPlayerController: Armor=0x{:X} IsAlive=0x{:X} PlayerPawn=0x{:X} MoneyService=0x{:X}",
				Offset::Armor, Offset::IsAlive, Offset::PlayerPawn, Offset::MoneyService);
		}

		// CBasePlayerController
		if (classes.HasMember("CBasePlayerController") && classes["CBasePlayerController"].HasMember("fields")) {
			const auto& fields = classes["CBasePlayerController"]["fields"];
			Offset::iszPlayerName = SafeGetUint64(fields, "m_iszPlayerName");
		}

		// C_BasePlayerPawn
		if (classes.HasMember("C_BasePlayerPawn") && classes["C_BasePlayerPawn"].HasMember("fields")) {
			const auto& fields = classes["C_BasePlayerPawn"]["fields"];
			Offset::Pos = SafeGetUint64(fields, "m_vOldOrigin");
			Offset::CameraServices = SafeGetUint64(fields, "m_pCameraServices");
			Offset::ItemServices = SafeGetUint64(fields, "m_pItemServices");
			Offset::WeaponServices = SafeGetUint64(fields, "m_pWeaponServices");
			Offset::ObserverServices = SafeGetUint64(fields, "m_pObserverServices");
			Offset::vecLastClipCameraPos = SafeGetUint64(fields, "m_vecLastCameraSetupLocalOrigin");
		}

		// CPlayer_ObserverServices
		if (classes.HasMember("CPlayer_ObserverServices") && classes["CPlayer_ObserverServices"].HasMember("fields")) {
			const auto& fields = classes["CPlayer_ObserverServices"]["fields"];
			Offset::ObserverMode = SafeGetUint64(fields, "m_iObserverMode");
			Offset::ObserverTarget = SafeGetUint64(fields, "m_hObserverTarget");
		}

		// CPlayer_WeaponServices
		if (classes.HasMember("CPlayer_WeaponServices") && classes["CPlayer_WeaponServices"].HasMember("fields")) {
			const auto& fields = classes["CPlayer_WeaponServices"]["fields"];
			Offset::MyWeapons = SafeGetUint64(fields, "m_hMyWeapons");
			Offset::ActiveWeapon = SafeGetUint64(fields, "m_hActiveWeapon");
		}

		// C_CSPlayerPawn
		if (classes.HasMember("C_CSPlayerPawn") && classes["C_CSPlayerPawn"].HasMember("fields")) {
			const auto& fields = classes["C_CSPlayerPawn"]["fields"];
			Offset::angEyeAngles = SafeGetUint64(fields, "m_angEyeAngles");
			Offset::iShotsFired = SafeGetUint64(fields, "m_iShotsFired");
			Offset::AimPunchServices = SafeGetUint64(fields, "m_pAimPunchServices");
			Offset::iIDEntIndex = SafeGetUint64(fields, "m_iIDEntIndex");
			Offset::PawnArmor = SafeGetUint64(fields, "m_ArmorValue");
			Offset::bIsScoped = SafeGetUint64(fields, "m_bIsScoped");
			Offset::bIsDefusing = SafeGetUint64(fields, "m_bIsDefusing");
			LOG_DEBUG("Offsets", "C_CSPlayerPawn: EyeAngles=0x{:X} PawnArmor=0x{:X} Scoped=0x{:X} Defusing=0x{:X}",
				Offset::angEyeAngles, Offset::PawnArmor, Offset::bIsScoped, Offset::bIsDefusing);

			// Calculate bSpottedByMask
			uint64_t m_entitySpottedState = SafeGetUint64(fields, "m_entitySpottedState");
			if (classes.HasMember("EntitySpottedState_t") && classes["EntitySpottedState_t"].HasMember("fields")) {
				const auto& spottedFields = classes["EntitySpottedState_t"]["fields"];
				uint64_t m_bSpottedByMask = SafeGetUint64(spottedFields, "m_bSpottedByMask");
				Offset::bSpottedByMask = m_entitySpottedState + m_bSpottedByMask;
			}
		}

		// C_CSPlayerPawnBase
		if (classes.HasMember("C_CSPlayerPawnBase") && classes["C_CSPlayerPawnBase"].HasMember("fields")) {
			const auto& fields = classes["C_CSPlayerPawnBase"]["fields"];
			Offset::flFlashDuration = SafeGetUint64(fields, "m_flFlashDuration");
		}

		// CSkeletonInstance
		if (classes.HasMember("CSkeletonInstance") && classes["CSkeletonInstance"].HasMember("fields")) {
			const auto& fields = classes["CSkeletonInstance"]["fields"];
			uint64_t modelState = SafeGetUint64(fields, "m_modelState");
			Offset::BoneArray = modelState + 0x80;
			Offset::ModelStateOffset = (DWORD)modelState;
			LOG_DEBUG("Offsets", "CSkeletonInstance: modelState=0x{:X} BoneArray=0x{:X}", modelState, Offset::BoneArray);
		}

		// CGameSceneNode
		if (classes.HasMember("CGameSceneNode") && classes["CGameSceneNode"].HasMember("fields")) {
			const auto& fields = classes["CGameSceneNode"]["fields"];
			Offset::vecAbsOrigin = SafeGetUint64(fields, "m_vecAbsOrigin");
		}

		// CModelState
		if (classes.HasMember("CModelState") && classes["CModelState"].HasMember("fields")) {
			const auto& fields = classes["CModelState"]["fields"];
			Offset::ModelNameOffset = SafeGetUint64(fields, "m_ModelName");
		}

		// C_PlantedC4
		if (classes.HasMember("C_PlantedC4") && classes["C_PlantedC4"].HasMember("fields")) {
			const auto& fields = classes["C_PlantedC4"]["fields"];
			Offset::BombTicking = SafeGetUint64(fields, "m_bBombTicking");
			Offset::C4Blow = SafeGetUint64(fields, "m_flC4Blow");
			Offset::BombDefused = SafeGetUint64(fields, "m_bBombDefused");
			Offset::BeingDefused = SafeGetUint64(fields, "m_bBeingDefused");
			Offset::DefuseCountDown = SafeGetUint64(fields, "m_flDefuseCountDown");
		}

		// CCSPlayer_ItemServices
		if (classes.HasMember("CCSPlayer_ItemServices") && classes["CCSPlayer_ItemServices"].HasMember("fields")) {
			const auto& fields = classes["CCSPlayer_ItemServices"]["fields"];
			Offset::HasDefuser = SafeGetUint64(fields, "m_bHasDefuser");
			Offset::HasHelmet = SafeGetUint64(fields, "m_bHasHelmet");
		}

		if (classes.HasMember("CCSPlayerBase_CameraServices") && classes["CCSPlayerBase_CameraServices"].HasMember("fields")) {
			const auto& fields = classes["CCSPlayerBase_CameraServices"]["fields"];
			Offset::iFovStart = SafeGetUint64(fields, "m_iFOVStart");
		}

		// CEntityInstance (entity identity pointer)
		if (classes.HasMember("CEntityInstance") && classes["CEntityInstance"].HasMember("fields")) {
			const auto& fields = classes["CEntityInstance"]["fields"];
			Offset::EntityIdentity = SafeGetUint64(fields, "m_pEntity");
		}

		// CEntityIdentity (designer name)
		if (classes.HasMember("CEntityIdentity") && classes["CEntityIdentity"].HasMember("fields")) {
			const auto& fields = classes["CEntityIdentity"]["fields"];
			Offset::DesignerName = SafeGetUint64(fields, "m_designerName");
		}

		// C_BaseGrenade
		if (classes.HasMember("C_BaseGrenade") && classes["C_BaseGrenade"].HasMember("fields")) {
			const auto& fields = classes["C_BaseGrenade"]["fields"];
			Offset::GrenadeIsLive = SafeGetUint64(fields, "m_bIsLive");
			Offset::GrenadeDmgRadius = SafeGetUint64(fields, "m_DmgRadius");
			Offset::GrenadeDetonateTime = SafeGetUint64(fields, "m_flDetonateTime");
			Offset::GrenadeThrower = SafeGetUint64(fields, "m_hThrower");
		}

		// CCSPlayer_AimPunchServices
		if (classes.HasMember("CCSPlayer_AimPunchServices") && classes["CCSPlayer_AimPunchServices"].HasMember("fields")) {
			const auto& fields = classes["CCSPlayer_AimPunchServices"]["fields"];
			Offset::AimPunchAngleOffset = SafeGetUint64(fields, "m_predictableBaseAngle");
		}

		// C_BaseCSGrenadeProjectile
		if (classes.HasMember("C_BaseCSGrenadeProjectile") && classes["C_BaseCSGrenadeProjectile"].HasMember("fields")) {
			const auto& fields = classes["C_BaseCSGrenadeProjectile"]["fields"];
			Offset::ProjSpawnTime = SafeGetUint64(fields, "m_flSpawnTime");
			Offset::ProjInitialVelocity = SafeGetUint64(fields, "m_vInitialVelocity");
			Offset::ProjBounces = SafeGetUint64(fields, "m_nBounces");
		}

		// C_BasePlayerWeapon (active weapon ammo clip)
		if (classes.HasMember("C_BasePlayerWeapon") && classes["C_BasePlayerWeapon"].HasMember("fields")) {
			const auto& fields = classes["C_BasePlayerWeapon"]["fields"];
			Offset::iClip1 = SafeGetUint64(fields, "m_iClip1");
		}

		// C_EconEntity (weapon item definition chain)
		if (classes.HasMember("C_EconEntity") && classes["C_EconEntity"].HasMember("fields")) {
			const auto& fields = classes["C_EconEntity"]["fields"];
			Offset::AttributeManager = SafeGetUint64(fields, "m_AttributeManager");
		}

		// C_AttributeContainer
		if (classes.HasMember("C_AttributeContainer") && classes["C_AttributeContainer"].HasMember("fields")) {
			const auto& fields = classes["C_AttributeContainer"]["fields"];
			Offset::Item = SafeGetUint64(fields, "m_Item");
		}

		// C_EconItemView (weapon item id for icon mapping)
		if (classes.HasMember("C_EconItemView") && classes["C_EconItemView"].HasMember("fields")) {
			const auto& fields = classes["C_EconItemView"]["fields"];
			Offset::iItemDefinitionIndex = SafeGetUint64(fields, "m_iItemDefinitionIndex");
		}
	}

	LOG_INFO("Config", "Successfully loaded offsets");
	LOG_DEBUG("Offsets", "Bomb: Ticking=0x{:X} C4Blow=0x{:X} Defused=0x{:X} BeingDefused=0x{:X} DefuseCD=0x{:X}",
		Offset::BombTicking, Offset::C4Blow, Offset::BombDefused, Offset::BeingDefused, Offset::DefuseCountDown);
	return true;
}

bool Offset::ParseVersion(const std::string& versionData)
{
	// Skip UTF-8 BOM if present (PowerShell Set-Content adds it)
	const char* data = versionData.c_str();
	if (versionData.size() >= 3 &&
		(unsigned char)data[0] == 0xEF &&
		(unsigned char)data[1] == 0xBB &&
		(unsigned char)data[2] == 0xBF)
		data += 3;

	Document doc;
	doc.Parse(data);
	if (doc.HasParseError()) {
		LOG_ERROR("Config", "Failed to parse version.json (code: {})", (int)doc.GetParseError());
		return false;
	}

	if (doc.HasMember("game_update_date") && doc["game_update_date"].IsString())
		GameUpdateDate = doc["game_update_date"].GetString();

	if (doc.HasMember("game_update_timestamp")) {
		if (doc["game_update_timestamp"].IsInt64())
			GameUpdateTimestamp = doc["game_update_timestamp"].GetInt64();
		else if (doc["game_update_timestamp"].IsInt())
			GameUpdateTimestamp = doc["game_update_timestamp"].GetInt();
	}

	LOG_INFO("Config", "Version info: date={}, timestamp={}", GameUpdateDate, GameUpdateTimestamp);

	// Load software version from file (overrides compile-time default)
	if (doc.HasMember("software_version") && doc["software_version"].IsString()) {
		std::string ver = doc["software_version"].GetString();
		if (!ver.empty()) {
			PROJECT_VERSION = ver;
			LOG_INFO("Config", "Software version from file: {}", PROJECT_VERSION);
		}
	}

	return !GameUpdateDate.empty() && GameUpdateTimestamp > 0;
}

bool Offset::CheckGameVersion(const std::string& steamNewsData)
{
	Document doc;
	doc.Parse(steamNewsData.c_str());
	if (doc.HasParseError()) {
		LOG_WARNING("Config", "Failed to parse Steam API response");
		return true; // assume OK if can't parse
	}

	if (!doc.HasMember("appnews") || !doc["appnews"].IsObject()) return true;
	const auto& appnews = doc["appnews"];
	if (!appnews.HasMember("newsitems") || !appnews["newsitems"].IsArray()) return true;

	const auto& items = appnews["newsitems"];
	int64_t latestUpdate = 0;
	for (SizeType i = 0; i < items.Size(); i++) {
		if (items[i].HasMember("date")) {
			int64_t d = 0;
			if (items[i]["date"].IsInt64()) d = items[i]["date"].GetInt64();
			else if (items[i]["date"].IsInt()) d = items[i]["date"].GetInt();
			if (d > latestUpdate) latestUpdate = d;
		}
	}

	LatestSteamUpdateTimestamp = latestUpdate;

	// Convert timestamps to human-readable dates for logging
	char localDate[32] = {}, steamDate[32] = {};
	time_t localT = static_cast<time_t>(GameUpdateTimestamp);
	tm localTm = {}, steamTm = {};
	gmtime_s(&localTm, &localT);
	strftime(localDate, sizeof(localDate), "%Y-%m-%d", &localTm);
	time_t steamT = static_cast<time_t>(latestUpdate);
	gmtime_s(&steamTm, &steamT);
	strftime(steamDate, sizeof(steamDate), "%Y-%m-%d", &steamTm);

	if (latestUpdate > GameUpdateTimestamp) {
		LOG_WARNING("Config", "CS2 game updated! Steam latest: {} ({}), Local offset date: {} ({})",
			steamDate, latestUpdate, localDate, GameUpdateTimestamp);
		return false; // version mismatch
	}

	LOG_INFO("Config", "CS2 game version matches. Steam latest: {} ({}), Local: {} ({})",
		steamDate, latestUpdate, localDate, GameUpdateTimestamp);
	return true; // version matches
}

bool Offset::GenerateVersionFromInfo(const std::string& infoPath, const std::string& versionPath)
{
	std::ifstream infoFile(infoPath);
	if (!infoFile) {
		LOG_ERROR("Config", "Cannot read info.json from {}", infoPath);
		return false;
	}
	std::stringstream buf;
	buf << infoFile.rdbuf();
	std::string infoData = buf.str();

	Document doc;
	doc.Parse(infoData.c_str());
	if (doc.HasParseError()) {
		LOG_ERROR("Config", "Failed to parse info.json");
		return false;
	}

	if (!doc.HasMember("timestamp") || !doc["timestamp"].IsString()) {
		LOG_ERROR("Config", "info.json missing 'timestamp' field");
		return false;
	}

	std::string timestamp = doc["timestamp"].GetString();
	// Extract date (first 10 chars: YYYY-MM-DD)
	std::string date = timestamp.substr(0, 10);

	// Convert ISO timestamp to Unix timestamp
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (sscanf_s(timestamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &sec) < 3) {
		LOG_ERROR("Config", "Failed to parse timestamp: {}", timestamp);
		return false;
	}

	struct tm tm_val = {};
	tm_val.tm_year = year - 1900;
	tm_val.tm_mon = month - 1;
	tm_val.tm_mday = day;
	tm_val.tm_hour = hour;
	tm_val.tm_min = minute;
	tm_val.tm_sec = sec;
	tm_val.tm_isdst = 0;

	time_t unixTime = _mkgmtime(&tm_val);

	std::ofstream ofs(versionPath);
	if (!ofs) {
		LOG_ERROR("Config", "Cannot write to {}", versionPath);
		return false;
	}

	ofs << "{\n    \"software_version\": \"" << PROJECT_VERSION << "\",\n    \"game_update_date\": \"" << date << "\",\n    \"game_update_timestamp\": " << unixTime << "\n}";

	LOG_INFO("Config", "Generated version.json: date={}, timestamp={}", date, unixTime);
	return true;
}
#include "Offsets.h"
#include "AppState.h"
#include "../utils/Logger.h"

#include "rapidjson/document.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

using namespace rapidjson;

// Helper function for safe nested JSON access
static uint64_t SafeGetUint64(const Value& obj, const char* key, uint64_t defaultVal = 0) {
	if (obj.HasMember(key) && obj[key].IsUint64())
		return obj[key].GetUint64();
	return defaultVal;
}

// Trim whitespace
static std::string TrimSpace(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// Fetch steam.inf from SteamTracking repo (used by GenerateVersionFromInfo)
// Note: uses default proxy; main.cpp's CheckGameVersion path uses downloadUrl which supports system proxy.
static std::string FetchSteamInf() {
	std::string result;
	HINTERNET hSession = WinHttpOpen(L"CS2-DMA/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) return result;

	HINTERNET hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
		L"/SteamTracking/GameTracking-CS2/master/game/csgo/steam.inf",
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

	DWORD connectTimeout = 5000, receiveTimeout = 8000;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
	WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout));

	if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		WinHttpReceiveResponse(hRequest, NULL)) {
		DWORD statusCode = 0, size = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			NULL, &statusCode, &size, NULL);
		if (statusCode == 200) {
			char buffer[4096];
			DWORD bytesRead = 0;
			while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
				result.append(buffer, bytesRead);
				bytesRead = 0;
			}
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return result;
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
		Offset::ViewAngles = SafeGetUint64(clientDll, "dwViewAngles");
		Offset::LocalPlayerController = SafeGetUint64(clientDll, "dwLocalPlayerController");
		Offset::LocalPlayerPawn = SafeGetUint64(clientDll, "dwLocalPlayerPawn");
		Offset::GlobalVars = SafeGetUint64(clientDll, "dwGlobalVars");
		Offset::PlantedC4 = SafeGetUint64(clientDll, "dwPlantedC4");
		Offset::WeaponC4 = SafeGetUint64(clientDll, "dwWeaponC4");
		Offset::GameRules = SafeGetUint64(clientDll, "dwGameRules");
		LOG_INFO("Offsets", "client.dll: EntityList=0x{:X} Matrix=0x{:X} ViewAngles=0x{:X} LocalCtrl=0x{:X} LocalPawn=0x{:X} GlobalVars=0x{:X} PlantedC4=0x{:X} WeaponC4=0x{:X} GameRules=0x{:X}",
			Offset::EntityList, Offset::Matrix, Offset::ViewAngles, Offset::LocalPlayerController, Offset::LocalPlayerPawn, Offset::GlobalVars, Offset::PlantedC4, Offset::WeaponC4, Offset::GameRules);
	} else {
		LOG_INFO("Offsets", "client.dll section NOT FOUND in offsets.json");
	}


	// Parse client_dll.json
	if (client.HasMember("client.dll") && client["client.dll"].HasMember("classes") && client["client.dll"]["classes"].IsObject()) {
		const auto& classes = client["client.dll"]["classes"];

		// C_BaseEntity
		if (classes.HasMember("C_BaseEntity") && classes["C_BaseEntity"].HasMember("fields")) {
			const auto& fields = classes["C_BaseEntity"]["fields"];
			Offset::Health = SafeGetUint64(fields, "m_iHealth");
			Offset::TeamID = SafeGetUint64(fields, "m_iTeamNum");
			Offset::CurrentHealth = SafeGetUint64(fields, "m_iHealth");
			Offset::GameSceneNode = SafeGetUint64(fields, "m_pGameSceneNode");
			Offset::fFlags = SafeGetUint64(fields, "m_fFlags");
			Offset::vecVelocity = SafeGetUint64(fields, "m_vecVelocity");
		Offset::OwnerEntity = SafeGetUint64(fields, "m_hOwnerEntity");
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
			Offset::PawnArmor = SafeGetUint64(fields, "m_ArmorValue");
			Offset::bIsScoped = SafeGetUint64(fields, "m_bIsScoped");
			Offset::bIsDefusing = SafeGetUint64(fields, "m_bIsDefusing");
			Offset::bIsWalking = SafeGetUint64(fields, "m_bIsWalking");
			LOG_DEBUG("Offsets", "C_CSPlayerPawn: EyeAngles=0x{:X} PawnArmor=0x{:X} Scoped=0x{:X} Defusing=0x{:X} Walking=0x{:X}",
				Offset::angEyeAngles, Offset::PawnArmor, Offset::bIsScoped, Offset::bIsDefusing, Offset::bIsWalking);

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
			Offset::PlantedC4_m_hBombDefuser = SafeGetUint64(fields, "m_hBombDefuser");
		}

		// C_CSGameRulesProxy
		if (classes.HasMember("C_CSGameRulesProxy") && classes["C_CSGameRulesProxy"].HasMember("fields")) {
			const auto& fields = classes["C_CSGameRulesProxy"]["fields"];
			Offset::GameRulesProxy_m_pGameRules = SafeGetUint64(fields, "m_pGameRules");
		}

		// C_CSGameRules
		if (classes.HasMember("C_CSGameRules") && classes["C_CSGameRules"].HasMember("fields")) {
			const auto& fields = classes["C_CSGameRules"]["fields"];
			Offset::CSGameRules_m_bFreezePeriod = SafeGetUint64(fields, "m_bFreezePeriod");
			Offset::CSGameRules_m_iRoundWinStatus = SafeGetUint64(fields, "m_iRoundWinStatus");
		}

		// C_C4
		if (classes.HasMember("C_C4") && classes["C_C4"].HasMember("fields")) {
			const auto& fields = classes["C_C4"]["fields"];
			Offset::C4_m_bIsPlantingViaUse = SafeGetUint64(fields, "m_bIsPlantingViaUse");
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
		Offset::ExplodeEffectTickBegin = SafeGetUint64(fields, "m_nExplodeEffectTickBegin");
		}

		// C_SmokeGrenadeProjectile (m_nSmokeEffectTickBegin: detonation signal)
		if (classes.HasMember("C_SmokeGrenadeProjectile") && classes["C_SmokeGrenadeProjectile"].HasMember("fields")) {
			const auto& fields = classes["C_SmokeGrenadeProjectile"]["fields"];
			Offset::SmokeEffectTickBegin = SafeGetUint64(fields, "m_nSmokeEffectTickBegin");
		}

		// C_Inferno (Task 7.2-7.4: multi-flame points for molotov/incendiary)
		if (classes.HasMember("C_Inferno") && classes["C_Inferno"].HasMember("fields")) {
			const auto& fields = classes["C_Inferno"]["fields"];
			Offset::InfernoFireCount = SafeGetUint64(fields, "m_fireCount");
			Offset::InfernoFirePositions = SafeGetUint64(fields, "m_firePositions");
		Offset::FireEffectTickBegin = SafeGetUint64(fields, "m_nFireEffectTickBegin");
		}

		// C_BasePlayerWeapon (active weapon ammo clip)
		if (classes.HasMember("C_BasePlayerWeapon") && classes["C_BasePlayerWeapon"].HasMember("fields")) {
			const auto& fields = classes["C_BasePlayerWeapon"]["fields"];
			Offset::iClip1 = SafeGetUint64(fields, "m_iClip1");
		}

		// C_EconEntity / C_AttributeContainer / C_EconItemView (reliable weapon name lookup via item definition index)
		if (classes.HasMember("C_EconEntity") && classes["C_EconEntity"].HasMember("fields")) {
			const auto& fields = classes["C_EconEntity"]["fields"];
			Offset::AttributeManager = SafeGetUint64(fields, "m_AttributeManager");
		}
		if (classes.HasMember("C_AttributeContainer") && classes["C_AttributeContainer"].HasMember("fields")) {
			const auto& fields = classes["C_AttributeContainer"]["fields"];
			Offset::Item = SafeGetUint64(fields, "m_Item");
		}
		if (classes.HasMember("C_EconItemView") && classes["C_EconItemView"].HasMember("fields")) {
			const auto& fields = classes["C_EconItemView"]["fields"];
			Offset::ItemDefinitionIndex = SafeGetUint64(fields, "m_iItemDefinitionIndex");
		}
		LOG_INFO("Offsets", "WeaponItem: AttributeManager=0x{:X} Item=0x{:X} ItemDefinitionIndex=0x{:X}",
			Offset::AttributeManager, Offset::Item, Offset::ItemDefinitionIndex);

	}

	LOG_INFO("Config", "Successfully loaded offsets");
	LOG_DEBUG("Offsets", "Bomb: Ticking=0x{:X} C4Blow=0x{:X} Defused=0x{:X} BeingDefused=0x{:X} DefuseCD=0x{:X}",
		Offset::BombTicking, Offset::C4Blow, Offset::BombDefused, Offset::BeingDefused, Offset::DefuseCountDown);
	LOG_INFO("Offsets", "GameRules: GameRules=0x{:X} Proxy_m_pGameRules=0x{:X} FreezePeriod=0x{:X} RoundWinStatus=0x{:X} BombDefuser=0x{:X} C4PlantingViaUse=0x{:X}",
		Offset::GameRules, Offset::GameRulesProxy_m_pGameRules, Offset::CSGameRules_m_bFreezePeriod,
		Offset::CSGameRules_m_iRoundWinStatus, Offset::PlantedC4_m_hBombDefuser, Offset::C4_m_bIsPlantingViaUse);
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

	if (doc.HasMember("patch_version") && doc["patch_version"].IsString())
		LocalPatchVersion = doc["patch_version"].GetString();

	LOG_INFO("Config", "Version info: date={}, timestamp={}, patch={}", GameUpdateDate, GameUpdateTimestamp, LocalPatchVersion);

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

bool Offset::ParseSteamInf(const std::string& data, SteamInfInfo& out)
{
	out = {};
	std::unordered_map<std::string, std::string> kv;
	std::istringstream stream(data);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t equals = line.find('=');
		if (equals == std::string::npos) continue;
		std::string key = TrimSpace(line.substr(0, equals));
		std::string val = TrimSpace(line.substr(equals + 1));
		if (key.empty()) continue;
		kv[key] = val;
	}

	out.patchVersion = kv["PatchVersion"];
	out.versionDate = kv["VersionDate"];
	out.versionTime = kv["VersionTime"];
	try { out.clientVersion = std::stoi(kv["ClientVersion"]); } catch (...) {}
	try { out.sourceRevision = std::stoi(kv["SourceRevision"]); } catch (...) {}

	return !out.patchVersion.empty();
}

bool Offset::CheckGameVersion(const std::string& steamInfData)
{
	SteamInfInfo info = {};
	if (!ParseSteamInf(steamInfData, info)) {
		LOG_WARNING("Config", "Failed to parse steam.inf, skipping version check");
		return true; // assume OK if can't parse
	}

	LatestPatchVersion = info.patchVersion;
	LatestGameUpdateDate = info.versionDate;

	LOG_INFO("Config", "Steam.inf: patch={}, date={}, client={}, sourceRev={}",
		info.patchVersion, info.versionDate, info.clientVersion, info.sourceRevision);

	// Primary check: compare patch version strings (most accurate)
	if (!LocalPatchVersion.empty()) {
		if (LocalPatchVersion != info.patchVersion) {
			LOG_WARNING("Config", "CS2 game updated! Steam patch: {}, Local offset patch: {}",
				info.patchVersion, LocalPatchVersion);
			VersionMismatch = true;
			return false;
		}
		LOG_INFO("Config", "CS2 game version matches. Patch: {}", info.patchVersion);
		VersionMismatch = false;
		return true;
	}

	// Fallback: no local patch version, compare by date
	// Convert steam.inf VersionDate (e.g. "2026/06/19") to timestamp
	if (!info.versionDate.empty() && GameUpdateTimestamp > 0) {
		int year = 0, month = 0, day = 0;
		bool parsed = false;

		// Try numeric slash format first: "2026/06/19"
		if (sscanf_s(info.versionDate.c_str(), "%d/%d/%d", &year, &month, &day) == 3) {
			parsed = true;
		} else {
			// steam.inf actually returns "Jun 16 2026" (month abbreviation)
			char monBuf[4] = {};
			if (sscanf_s(info.versionDate.c_str(), "%3s %d %d", monBuf, (unsigned)sizeof(monBuf), &day, &year) == 3) {
				static const char* const monthNames[] = {
					"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
				};
				std::string monStr(monBuf);
				for (int i = 0; i < 12; i++) {
					if (monStr == monthNames[i]) {
						month = i + 1;
						parsed = true;
						break;
					}
				}
			}
		}

		if (parsed) {
			struct tm tm_val = {};
			tm_val.tm_year = year - 1900;
			tm_val.tm_mon = month - 1;
			tm_val.tm_mday = day;
			tm_val.tm_isdst = 0;
			time_t steamT = _mkgmtime(&tm_val);
			if (steamT > GameUpdateTimestamp) {
				LOG_WARNING("Config", "CS2 game updated! Steam date: {}, Local offset date: {}",
					info.versionDate, GameUpdateDate);
				VersionMismatch = true;
				return false;
			}
		}
	}

	LOG_INFO("Config", "CS2 game version matches (by date). Steam date: {}", info.versionDate);
	VersionMismatch = false;
	return true;
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

	// Fetch steam.inf for real game version info (PatchVersion, VersionDate, etc.)
	SteamInfInfo steamInfo = {};
	std::string steamInf = FetchSteamInf();
	bool hasSteamInf = !steamInf.empty() && ParseSteamInf(steamInf, steamInfo);
	if (hasSteamInf) {
		LOG_INFO("Config", "Fetched steam.inf: patch={}, date={}",
			steamInfo.patchVersion, steamInfo.versionDate);
	} else {
		LOG_WARNING("Config", "Failed to fetch steam.inf, version.json will use dumper timestamp only");
	}

	// Use steam.inf date if available (real game update date), else dumper date
	std::string gameDate = hasSteamInf ? steamInfo.versionDate : date;
	std::string patchVer = hasSteamInf ? steamInfo.patchVersion : "";
	int clientVer = hasSteamInf ? steamInfo.clientVersion : 0;
	int sourceRev = hasSteamInf ? steamInfo.sourceRevision : 0;

	std::ofstream ofs(versionPath);
	if (!ofs) {
		LOG_ERROR("Config", "Cannot write to {}", versionPath);
		return false;
	}

	ofs << "{\n"
		<< "    \"software_version\": \"" << PROJECT_VERSION << "\",\n"
		<< "    \"game_update_date\": \"" << gameDate << "\",\n"
		<< "    \"game_update_timestamp\": " << unixTime << ",\n"
		<< "    \"patch_version\": \"" << patchVer << "\",\n"
		<< "    \"client_version\": " << clientVer << ",\n"
		<< "    \"source_revision\": " << sourceRev << "\n"
		<< "}";

	LOG_INFO("Config", "Generated version.json: date={}, patch={}, timestamp={}",
		gameDate, patchVer, unixTime);
	return true;
}

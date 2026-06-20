#include "Game.h"
#include "../utils/Logger.h"

bool CGame::InitAddress()
{
	LOG_INFO("Game", "InitAddress: resolving client.dll & engine2.dll...");
	this->Address.ClientDLL = GetProcessModuleHandle(ProcessMgr.HANDLE, ProcessMgr.ProcessID, "client.dll");
	LOG_INFO("Game", "InitAddress: client.dll = 0x{:X}", this->Address.ClientDLL);

	this->Address.Engine2DLL = GetProcessModuleHandle(ProcessMgr.HANDLE, ProcessMgr.ProcessID, "engine2.dll");
	LOG_INFO("Game", "InitAddress: engine2.dll = 0x{:X}", this->Address.Engine2DLL);

	// Reference project (KevqDMA): both client.dll AND engine2.dll must be non-zero
	// to consider the game ready. CS2 loads engine2.dll before client.dll; requiring
	// both avoids false "ready" when only one module is resolved.
	if (!this->Address.ClientDLL || !this->Address.Engine2DLL) {
		LOG_WARNING("Game", "InitAddress: module(s) not resolved yet (client=0x{:X} engine2=0x{:X}), will retry",
			this->Address.ClientDLL, this->Address.Engine2DLL);
		return false;
	}

	this->Address.MatchDLL = GetProcessModuleHandle(ProcessMgr.HANDLE, ProcessMgr.ProcessID, "matchmaking.dll");
	LOG_DEBUG("Game", "InitAddress: matchmaking.dll = 0x{:X}", this->Address.MatchDLL);

	this->Address.EntityList = GetClientDLLAddress() + Offset::EntityList;
	this->Address.Matrix = GetClientDLLAddress() + Offset::Matrix;
	this->Address.LocalController = GetClientDLLAddress() + Offset::LocalPlayerController;

	this->Address.LocalPawn = GetClientDLLAddress() + Offset::LocalPlayerPawn;
	this->Address.GlobalVars = GetClientDLLAddress() + Offset::GlobalVars;

	LOG_DEBUG("Game", "Addresses: EntityList=0x{:X} Matrix=0x{:X} LocalCtrl=0x{:X} LocalPawn=0x{:X} GlobalVars=0x{:X}",
		this->Address.EntityList, this->Address.Matrix, this->Address.LocalController, this->Address.LocalPawn, this->Address.GlobalVars);

	UpdateEntityListEntry();

	LOG_INFO("Game", "InitAddress: done (ClientDLL=0x{:X} Engine2DLL=0x{:X})", this->Address.ClientDLL, this->Address.Engine2DLL);
	return true;
}

DWORD64 CGame::GetClientDLLAddress()
{
	return this->Address.ClientDLL;
}

DWORD64 CGame::GetEngine2DLLAddress()
{
	return this->Address.Engine2DLL;
}

DWORD64 CGame::GetMatchDLLAddress()
{
	return this->Address.MatchDLL;
}

DWORD64 CGame::GetEntityListAddress()
{
	return this->Address.EntityList;
}

DWORD64 CGame::GetMatrixAddress()
{
	return this->Address.Matrix;
}

DWORD64 CGame::GetEntityListEntry()
{
	return this->Address.EntityListEntry;
}

DWORD64 CGame::GetLocalControllerAddress()
{
	return this->Address.LocalController;
}

DWORD64 CGame::GetLocalPawnAddress()
{
	return this->Address.LocalPawn;
}

DWORD64 CGame::GetGlobalVarsAddress()
{
	return this->Address.GlobalVars;
}

bool CGame::UpdateEntityListEntry()
{
	DWORD64 EntityListEntry = 0;
	if (!ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), EntityListEntry)) {
		LOG_TRACE("Game", "UpdateEntityListEntry: read EntityList FAILED");
		return false;
	}
	if (!ProcessMgr.ReadMemory<DWORD64>(EntityListEntry + 0x10, EntityListEntry)) {
		LOG_TRACE("Game", "UpdateEntityListEntry: read sub-entry FAILED");
		return false;
	}

	this->Address.EntityListEntry = EntityListEntry;
	LOG_TRACE("Game", "UpdateEntityListEntry: 0x{:X}", EntityListEntry);

	return this->Address.EntityListEntry != 0;
}


DWORD64 GetProcessModuleHandle(VMM_HANDLE HANDLE, DWORD ProcessID, std::string ModuleName)
{
	PVMMDLL_MAP_MODULEENTRY module_entry;

	// Short retry loop — INITIALIZING_GAME state controls the outer retry.
	// Each retry sync-refreshes VMM cache to pick up newly loaded modules.
	constexpr int MAX_RETRIES = 3;
	for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
	{
		if (attempt > 0) {
			VMMDLL_ConfigSet(HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
			Sleep(2000);
		}

		bool result = VMMDLL_Map_GetModuleFromNameU(HANDLE, ProcessID, (LPSTR)ModuleName.c_str(), &module_entry, NULL);
		if (result) {
			LOG_INFO("Memory", "GetProcessModuleHandle: '{}' resolved at 0x{:X} (attempt {})", ModuleName, module_entry->vaBase, attempt + 1);
			return module_entry->vaBase;
		}
		LOG_DEBUG("Memory", "GetProcessModuleHandle: '{}' attempt {} failed", ModuleName, attempt + 1);

	}

	// Fallback: CR3/DTB remediation via ProcessManager
	LOG_WARNING("Memory", "GetProcessModuleHandle: '{}' not found after {} retries, trying CR3 fix...", ModuleName, MAX_RETRIES);
	if (ProcessMgr.FixCr3()) {
		bool result = VMMDLL_Map_GetModuleFromNameU(HANDLE, ProcessID, (LPSTR)ModuleName.c_str(), &module_entry, NULL);
		if (result) {
			LOG_INFO("Memory", "GetProcessModuleHandle: '{}' resolved at 0x{:X} after CR3 fix", ModuleName, module_entry->vaBase);
			return module_entry->vaBase;
		}
	}

	LOG_ERROR("Memory", "GetProcessModuleHandle: '{}' failed after CR3 fix", ModuleName);
	return false;
}

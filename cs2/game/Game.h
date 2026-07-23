#pragma once
#include <optional>

#include "../utils/ProcessManager.h"

#include "Offsets.h"
#include "View.h"

#include<iostream>
#include<fstream>
#include<sstream>

class CGame
{
private:
	// EntityList 2-frame confirmation state (ref: KevqDMA)
	static DWORD64 s_pendingEntityListCandidate;
	static int s_entityListConfirmCount;
	struct
	{
		DWORD64 ClientDLL;
		DWORD64 Engine2DLL;
		DWORD64 MatchDLL;
		DWORD64 EntityList;
		DWORD64 Matrix;
		DWORD64 EntityListEntry;
		DWORD64 LocalController;
		DWORD64 LocalPawn;
		DWORD64 GlobalVars;
	}Address;

public:
	CView View;

public:	

	bool InitAddress();

	DWORD64 GetClientDLLAddress();

	DWORD64 GetEngine2DLLAddress();

	DWORD64 GetMatchDLLAddress();

	DWORD64 GetEntityListAddress();

	DWORD64 GetMatrixAddress();

	DWORD64 GetEntityListEntry();

	DWORD64 GetLocalControllerAddress();

	DWORD64 GetLocalPawnAddress();

	DWORD64 GetGlobalVarsAddress();

	bool UpdateEntityListEntry();

};

inline CGame gGame;

DWORD64 GetProcessModuleHandle(VMM_HANDLE HANDLE, DWORD  ProcessID, std::string ModuleName);



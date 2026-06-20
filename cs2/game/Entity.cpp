#include "Entity.h"
#include "../utils/Logger.h"

bool CEntity::UpdateController(const DWORD64& PlayerControllerAddress)
{
	if (PlayerControllerAddress == 0)
		return false;

	this->Controller.Address = PlayerControllerAddress;

	if (!this->Controller.GetHealth()) {
		LOG_TRACE("Entity", "Controller GetHealth FAIL");
		return false;
	}

	if (!this->Controller.GetArmor()) {
		LOG_TRACE("Entity", "Controller GetArmor FAIL");
		return false;
	}

	if (!this->Controller.GetIsAlive()) {
		LOG_TRACE("Entity", "Controller IsAlive FAIL");
		return false;
	}

	if (!this->Controller.GetTeamID())
		return false;

	if (!this->Controller.GetPlayerName())
		return false;

	this->Pawn.Address = this->Controller.GetPlayerPawnAddress();

	return true;
}

bool PlayerController::GetTeamID()
{
	return GetDataAddressWithOffset<int>(Address, Offset::TeamID, this->TeamID);
}

bool PlayerController::GetHealth()
{
	return GetDataAddressWithOffset<int>(Address, Offset::Health, this->Health);
}


bool PlayerController::GetIsAlive()
{
	return GetDataAddressWithOffset<int>(Address, Offset::IsAlive, this->AliveStatus);
}

bool PlayerController::GetPlayerName()
{
	char Buffer[MAX_PATH]{};

	if (!ProcessMgr.ReadMemory(Address + Offset::iszPlayerName, Buffer, MAX_PATH))
		return false;

	this->PlayerName = Buffer;
	if (this->PlayerName.empty())
		this->PlayerName = "Name_None";

	return true;
}

bool PlayerController::GetArmor()
{
	return GetDataAddressWithOffset<int>(Address, Offset::Armor, this->Armor);
}

DWORD64 PlayerController::GetPlayerPawnAddress()
{
	DWORD64 EntityPawnListEntry = 0;
	DWORD64 EntityPawnAddress = 0;

	if (!GetDataAddressWithOffset<DWORD>(Address, Offset::PlayerPawn, this->Pawn))
		return 0;

	if (!ProcessMgr.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), EntityPawnListEntry))
		return 0;

	if (!ProcessMgr.ReadMemory<DWORD64>(EntityPawnListEntry + 0x10 + 8 * ((Pawn & 0x7FFF) >> 9), EntityPawnListEntry))
		return 0;

	if (!ProcessMgr.ReadMemory<DWORD64>(EntityPawnListEntry + 0x70 * (Pawn & 0x1FF), EntityPawnAddress))
		return 0;

	return EntityPawnAddress;
}

bool CEntity::InitPawnAddress(const DWORD64& PlayerPawnAddress)
{
	if (PlayerPawnAddress == 0)
		return false;

	this->Pawn.Address = PlayerPawnAddress;

	// Only resolve BoneArrayAddress (2 reads) - scatter handles all dynamic fields
	if (!this->Pawn.BoneData.ResolveBoneAddress(PlayerPawnAddress))
		return false;

	return true;
}

bool CEntity::IsAlive() const
{
	return this->Controller.AliveStatus == 1 && this->Pawn.Health > 0;
}

bool CEntity::IsInScreen()
{
	return gGame.View.WorldToScreen(this->Pawn.Pos, this->Pawn.ScreenPos);
}

CBone CEntity::GetBone() const
{
	if (this->Pawn.Address == 0)
		return CBone{};
	return this->Pawn.BoneData;
}
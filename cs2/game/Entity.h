#pragma once
#include "Game.h"
#include "View.h"
#include "Bone.h"
#include "MemoryHelper.h"

struct C_UTL_VECTOR
{
	DWORD64 Count = 0;
	DWORD64 Data = 0;
};

class PlayerController
{
public:
	DWORD64 Address = 0;
	int TeamID = 0;
	int Health = 0;
	int Armor = 0;
	int AliveStatus = 0;
	int Money = 0;
	int Color = -1;
	DWORD Pawn = 0;
	std::string PlayerName;
public:
	bool GetTeamID();
	bool GetHealth();
	bool GetIsAlive();
	bool GetPlayerName();
	bool GetArmor();
	DWORD64 GetPlayerPawnAddress();
};

class PlayerPawn
{
public:
	DWORD64 Address = 0;
	CBone BoneData;
	Vec2 ViewAngle;
	Vec3 Pos;
	Vec2 ScreenPos;
	bool ScreenPosValid = false;
	Vec3 CameraPos;
	std::string WeaponName;
	DWORD ShotsFired;
	Vec2 AimPunchAngle;
	int Health;
	int Armor = 0;
	float FlashDuration = 0.f;
	int TeamID;
	int Fov;
	DWORD64 bSpottedByMask;
	int fFlags;
	bool HasHelmet = false;
	bool HasDefuser = false;
	std::string ModelName;
	std::vector<std::string> WeaponList;
	// ESP gap-closure stage 1: extended tactical fields
	bool Scoped = false;
	bool Defusing = false;
	bool IsWalking = false;
	int AmmoClip = -1;
	uint64_t SoundUntilMs = 0;
	Vec3 Velocity;
	Vec3 PrevPos;          // previous frame position (snapshot interpolation)
};

class CEntity
{
public:
	PlayerController Controller;
	PlayerPawn Pawn;
	int LocalPlayerControllerIndex = 0;
public:
	bool UpdateController(const DWORD64& PlayerControllerAddress);

	// Lightweight: only resolves PawnAddress + BoneArrayAddress for scatter
	bool InitPawnAddress(const DWORD64& PlayerPawnAddress);

	bool IsAlive() const;

	bool IsInScreen();

	CBone GetBone() const;
};
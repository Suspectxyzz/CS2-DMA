#pragma once

#define _USE_MATH_DEFINES

#include <math.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "../game/Entity.h"
#include "Cheats.h"
#include "../game/MenuConfig.h"
#include "../game/WeaponLookup.h"

namespace Render
{
	inline bool IsValidFloat(float v) { return std::isfinite(v) && std::abs(v) < 1e7f; }
	inline bool IsValidScreenPos(const ImVec2& p) { return IsValidFloat(p.x) && IsValidFloat(p.y); }
	inline bool IsValidRect(const ImVec4& r) { return IsValidFloat(r.x) && IsValidFloat(r.y) && IsValidFloat(r.z) && IsValidFloat(r.w) && r.z >= 1.f && r.w >= 1.f; }

	void DrawDistance(const CEntity& LocalEntity, const CEntity& Entity, ImVec4 Rect);

	ImVec4 Get2DBox(const CEntity& Entity);



	void DrawBone(const CEntity& Entity, ImColor Color, float Thickness,
	              const bool* boneVisibility = nullptr,
	              ImColor visColor = ImColor(0, 255, 0, 255),
	              ImColor hidColor = ImColor(255, 0, 0, 255));
	void ShowLosLine(const CEntity& Entity, const float Length, ImColor Color, float Thickness, const float Matrix[4][4]);
	ImVec4 Get2DBoneRect(const CEntity& Entity);

	class HealthBar
	{
	private:
		using TimePoint_ = std::chrono::steady_clock::time_point;
		const int ShowBackUpHealthDuration = 500;
		float MaxHealth = 0.f;
		float CurrentHealth = 0.f;
		float LastestBackupHealth = 0.f;
		ImVec2 RectPos{};
		ImVec2 RectSize{};
		bool InShowBackupHealth = false;
		TimePoint_ BackupHealthTimePoint{};
	public:
		HealthBar() {}
	void DrawHealthBar(float MaxHealth, float CurrentHealth, ImVec2 Pos, ImVec2 Size, bool vertical);
private:
		ImColor Mix(ImColor Col_1, ImColor Col_2, float t);
		ImColor FirstStageColor = ImColor(96, 246, 113, 220);
		ImColor SecondStageColor = ImColor(247, 214, 103, 220);
		ImColor ThirdStageColor = ImColor(255, 95, 95, 220);
		ImColor BackupHealthColor = ImColor(255, 255, 255, 220);
		ImColor FrameColor = ImColor(45, 45, 45, 220);
		ImColor BackGroundColor = ImColor(90, 90, 90, 220);
	};

	void DrawHealthBar(DWORD Sign, float MaxHealth, float CurrentHealth, ImVec2 Pos, ImVec2 Size, bool Horizontal);
	void DrawCornerBox(ImVec4 Rect, ImColor Color, float Thickness, float CornerFrac);
	void DrawHeadDot(const CEntity& Entity, ImColor Color, float Radius);
	void DrawArmorBar(int Armor, ImVec4 Rect, ImColor Color, float BarWidth, int Type);
	void LineToEnemyEx(ImVec4 Rect, ImColor Color, float Thickness, int Origin);
	void DrawBombESP(const BombData& bomb, const float matrix[4][4]);
	void DrawProjectileESP(const std::vector<GrenadeProjectile>& projectiles, const float matrix[4][4], const Vec3& localPos);

	// ESP gap-closure stage 2: unified text shadow helper.
	// Draws text with a 1px black shadow offset, reusing the caller's alpha.
	// Pass fontSize>0 and a non-null font to push that font for the draw.
	void DrawTextShadow(ImDrawList* drawList, ImFont* font, float fontSize,
	                     const ImVec2& pos, ImU32 color, const char* text,
	                     const char* textEnd = nullptr);

	// ESP gap-closure stage 3a: player status flags (Task 8).
	// Draws Blind/Scoped/Defusing/Kit/Money labels stacked vertically to the
	// right of the player's 2D box.
	void DrawPlayerFlags(ImDrawList* drawList, const CEntity& entity,
	                     const ImVec2& boxMin, const ImVec2& boxMax, float fontSize);

	// ESP gap-closure stage 3a: sound ESP ripple (Task 10).
	// Draws an expanding double-ring circle at a player's feet when a shot
	// was recently fired (SoundUntilMs window).
	void DrawSoundESP(ImDrawList* drawList, const Vec2& screenPos,
	                  uint64_t nowMs, uint64_t soundUntilMs, ImU32 color);

	// Footstep ESP: persistent pulsing circle at a player's feet while
	// moving on ground (caller decides isMoving; this only draws the pulse).
	void DrawFootstepESP(ImDrawList* drawList, const Vec2& screenPos,
	                     uint64_t nowMs, ImU32 color);

	// ESP gap-closure stage 3b: C4 bomb timer overlay (Task 11).
	// Draws a draggable ImGui window at the screen center showing the C4
	// countdown progress bar and the defuse progress bar when applicable.
	// bombLeftSec/bombTotalSec describe the C4 timer; defuseLeftSec/
	// defuseTotalSec describe the active defuse (0 when nobody defuses).
	void DrawBombTimerOverlay(ImDrawList* drawList, float screenW, float screenH,
	                          float bombLeftSec, float bombTotalSec,
	                          bool defusing, float defuseLeftSec, float defuseTotalSec);

	// ESP gap-closure stage 3b: world ESP (Task 12).
	// Draws grenade effect timers (Smoke/Inferno/Decoy) on top of the
	// existing projectile dots. nowUs is steady_clock microseconds.
	void DrawWorldESP(ImDrawList* drawList, const std::vector<GrenadeProjectile>& projectiles,
	                  const float matrix[4][4], uint64_t nowUs);

	// ESP gap-closure stage 3b: dropped-weapon world ESP (Task 12/16).
	// Draws icon + name for each dropped weapon on the ground, filtered by
	// EspItemEnabledMask. Reuses the weapon icon font loaded in OS-ImGui_Base.
	void DrawDroppedWeapons(ImDrawList* drawList, const std::vector<DroppedWeapon>& weapons,
	                        const float matrix[4][4]);

	// ESP gap-closure stage 3b: weapon icon ESP (Task 13).
	// Draws the active weapon's icon glyph above the weapon name. Falls back
	// to no-op when the icon font is missing or the weapon is unknown.
	void DrawWeaponIcon(ImDrawList* drawList, const ImVec2& pos, uint16_t weaponId,
	                    float fontSize, ImU32 color, bool skipKnife);

	// ESP gap-closure stage 3b: weapon ammo ESP (Task 13).
	// Draws "Ammo XX/YY" below the weapon name; shows a red "LOW" tag when
	// ammoClip <= maxClip/5. When maxClip<=0 (unknown weapon) only the
	// current ammo is shown.
	void DrawWeaponAmmo(ImDrawList* drawList, const ImVec2& pos, int ammoClip, int maxClip,
	                    float fontSize, ImU32 color, ImU32 lowColor);

	// Returns the magazine capacity for common CS2 weapons by name.
	// Unknown weapons return 0 (caller should only show current ammo).
	int WeaponMaxClip(const std::string& weaponName);

	// ESP gap-closure stage 2: bar value label (Task 14).
	// Draws a numeric label with a semi-transparent background box above a
	// health/armor bar. The box has a 1px accent line at the bottom in the
	// bar's color. screenW is used for edge avoidance.
	void DrawBarLabel(ImDrawList* drawList, const ImVec2& barPos, float barHeight,
	                  int value, ImU32 barColor, float fontSize, float screenW);

	// ESP audit stage 2: unified health-color from ratio (Task 8).
	// Returns IM_COL32 red→green color for the given health value (0..100).
	// health is clamped to [0,100]; ratio = health/100; R=255*(1-r), G=255*r.
	ImU32 HealthColorFromRatio(int health);

	// ESP audit stage 2: box-type dispatch (Task 9).
	// Returns the 2D box for the entity according to MenuConfig::BoxType:
	// case 1 -> Get2DBoneRect, all other cases -> Get2DBox.
	ImVec4 GetBoxByType(const CEntity& Entity);

	// ESP audit stage 2: projectile timer computation (Task 10).
	// Validates the projectile position, dispatches label + maxDuration by
	// proj.Type, and computes the remaining effect time. Returns false for
	// unknown types or invalid positions (caller skips). worldMode=true
	// uses the WorldESP table (Smoke/Fire/Decoy, Decoy=15s); worldMode=false
	// uses the ProjectileESP table (all 5 types, Decoy=0s/no timer).
	bool ComputeProjectileTimer(const GrenadeProjectile& proj, bool worldMode,
	                            const char** label, float* maxDuration, float* remaining);
}

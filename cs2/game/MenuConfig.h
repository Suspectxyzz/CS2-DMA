#pragma once

#include "Game.h"

#include "Bone.h"

#include <map>
#include <vector>
#include <bitset>



namespace MenuConfig

{

	inline int MaxFrameRate = 0;
	inline bool VSync = false;

	// ======== Render Resolution ========
	inline int RenderWidth = 0;   // 0 = auto (monitor resolution)
	inline int RenderHeight = 0;  // 0 = auto (monitor resolution)

	// ======== Monitor Selection ========
	inline int MonitorIndex = 0;  // 0 = primary monitor, 1+ = secondary

	struct MonitorDesc {
		int index;
		int x, y, width, height;
		std::string name; // e.g. "Monitor 1 (1920x1080)"
	};
	inline std::vector<MonitorDesc> MonitorList;



	inline int SelectedLanguage = 0;



	inline std::string path = "saved/configs";



	inline bool ShowBoneESP = false;

	inline bool ShowBoxESP = true;

	inline bool ShowHealthBar = true;

	inline bool ShowWeaponESP = true;

	inline bool ShowDistance = false;

	inline bool ShowEyeRay = false;

	inline bool ShowPlayerName = true;



	// 0: normal 1: dynamic 2: corner

	inline int  BoxType = 0;

	inline int  HealthBarType = 0;



	inline ImColor BoneColor = ImColor(255, 255, 255, 255);

	inline ImColor BoxColor = ImColor(255, 255, 255, 255);

	inline ImColor EyeRayColor = ImColor(255, 0, 0, 255);



	inline bool ShowMenu = true;

	// ======== Web Radar ========
	inline bool ShowWebRadar = false;
	inline int  WebRadarPort = 22006;
	inline int  WebRadarInterval = 50; // ms between broadcasts (20 FPS)
	inline bool WebRadarPasswordEnabled = false;
	inline std::string WebRadarPassword = "123456";
	// Task 10: Origin whitelist for /api/* paths (comma-separated).
	// Empty = allow all origins. Example: "http://localhost:5173,https://example.com"
	inline std::string WebRadarOriginAllowlist;
	// Cloudflare quick tunnel toggle (public access). Off by default.
	inline bool WebRadarCloudflareTunnel = false;

	inline bool TeamCheck = true;



	inline bool ShowLineToEnemy = false;
	inline ImColor LineToEnemyColor = ImColor(255, 255, 255, 220);

	// ======== Box Customization ========
	inline float BoxThickness = 1.3f;
	inline float BoxRounding = 0.f;
	inline bool  BoxFilled = false;
	inline float BoxFillAlpha = 0.15f;
	// BoxType 2 = Corner Box
	inline float CornerLength = 0.25f; // fraction of edge length

	// ======== Bone Customization ========
	inline float BoneThickness = 1.3f;

	// ======== Head Dot ========
	inline bool  ShowHeadDot = false;
	inline ImColor HeadDotColor = ImColor(255, 0, 0, 255);
	inline float HeadDotSize = 3.f;

	// ======== Armor Bar ========
	inline bool  ShowArmorBar = false;
	// 0: Vertical(right side)  1: Horizontal(top)
	inline int   ArmorBarType = 0;
	inline ImColor ArmorBarColor = ImColor(0, 120, 255, 220);
	inline float ArmorBarWidth = 3.f;

	// ======== Health Bar Width ========
	inline float HealthBarWidth = 4.f;

	// ======== Eye Ray Customization ========
	inline float EyeRayLength = 50.f;
	inline float EyeRayThickness = 1.3f;

	// ======== Snapline Customization ========
	inline float LineToEnemyThickness = 1.2f;
	// 0: Top  1: Center  2: Bottom
	inline int   LineToEnemyOrigin = 0;

	// ======== Bomb ESP ========
	inline bool  ShowBombESP = true;
	inline ImColor BombPlantedColor = ImColor(255, 50, 50, 255);
	inline ImColor BombCarrierColor = ImColor(255, 200, 0, 255);
	inline ImColor BombDroppedColor = ImColor(255, 150, 0, 255);
	inline ImColor BombDefusingColor = ImColor(0, 180, 255, 255);

	// ======== Grenade Projectile ESP ========
	inline bool  ShowProjectileESP = true;
	inline bool  ShowProjectileRange = true;
	inline float ProjectileRangeAlpha = 0.12f;

	// ======== Debug Log ========
	inline bool  DebugLog = false;

	// ======== ESP Render Quality (stage 2) ========
	// Snapshot interpolation + velocity extrapolation for player positions.
	inline bool  InterpolationEnabled = true;
	// Bone reliability check + 150ms persistence to suppress flicker.
	inline bool  BoneReliabilityEnabled = true;

	// ======== Safe Zone (Crosshair Cutout) ========
	inline bool  SafeZoneEnabled = false;
	inline float SafeZoneRadius = 100.f;
	inline int   SafeZoneShape = 0; // 0: Circle, 1: Square
	inline int   SafeZoneMode = 0;  // 0: Mask, 1: Skip Drawing
	inline bool  SafeZoneSkipBox = true;
	inline bool  SafeZoneSkipBone = true;
	inline bool  SafeZoneSkipHealthBar = false;
	inline bool  SafeZoneSkipArmorBar = false;
	inline bool  SafeZoneSkipWeapon = false;
	inline bool  SafeZoneSkipName = false;
	inline bool  SafeZoneSkipSnapline = false;
	inline bool  SafeZoneSkipEyeRay = false;
	inline bool  SafeZoneSkipHeadDot = false;
	inline bool  SafeZoneSkipDistance = false;

	// ======== Crosshair Overlay ========
	inline bool    CrosshairEnabled = false;
	inline float   CrosshairSize = 6.f;       // arm length in px
	inline float   CrosshairThickness = 1.5f;
	inline float   CrosshairGap = 3.f;        // center gap in px
	inline int     CrosshairStyle = 0;        // 0: Cross  1: Dot  2: Circle  3: Cross+Dot
	inline ImColor CrosshairColor = ImColor(0, 255, 0, 255);
	inline bool    CrosshairOnEnemyColor = true;
	inline ImColor CrosshairEnemyColor = ImColor(255, 0, 0, 255);

	// ======== Menu Hotkey ========
	inline int   MenuHotKey = VK_F8;
	inline char  MenuHotKeyName[32] = "F8";
	inline bool  IsListeningForMenuKey = false;

	// ======== Spectator List ========
	inline bool  ShowSpectatorList = false;

	// ======== Performance Monitor ========
	inline bool  ShowPerfMonitor = false;

	// ======== ESP gap-closure stage 3a: Offscreen Arrows (Task 7) ========
	inline bool    ShowOffscreenArrows = false;
	inline ImColor OffscreenArrowColor = ImColor(255, 255, 255, 200);
	inline float   OffscreenArrowSize = 12.f;

	// ======== ESP gap-closure stage 3a: Player Flags (Task 8) ========
	inline bool    ShowPlayerFlags = false;
	inline bool    FlagBlindEnabled = true;
	inline ImColor FlagBlindColor = ImColor(255, 255, 0, 255);
	inline bool    FlagScopedEnabled = true;
	inline ImColor FlagScopedColor = ImColor(0, 255, 255, 255);
	inline bool    FlagDefusingEnabled = true;
	inline ImColor FlagDefusingColor = ImColor(255, 100, 100, 255);
	inline bool    FlagKitEnabled = true;
	inline ImColor FlagKitColor = ImColor(100, 255, 100, 255);
	inline bool    FlagMoneyEnabled = false;
	inline ImColor FlagMoneyColor = ImColor(100, 255, 100, 255);
	inline float   FlagFontSize = 12.f;

	// ======== ESP gap-closure stage 3a: Visibility Coloring (Task 9) ========
	inline bool    VisibilityColoring = false;
	inline ImColor VisibleColor = ImColor(0, 255, 0, 255);
	inline ImColor HiddenColor = ImColor(255, 0, 0, 255);

	// ======== ESP gap-closure stage 3a: Sound ESP (Task 10) ========
	inline bool    ShowSoundESP = false;
	inline ImColor SoundESPColor = ImColor(0, 150, 255, 200);

	// ======== ESP gap-closure stage 3b: C4 Bomb Timer Overlay (Task 11) ========
	inline bool    ShowBombTimer = false;
	inline float   BombTimerX = -1.f;   // -1 = default center
	inline float   BombTimerY = -1.f;

	// ======== ESP gap-closure stage 3b: World ESP (Task 12) ========
	// Note: dropped-weapon scanning is deferred; this toggle currently
	// drives the grenade effect timers (Smoke/Inferno/Decoy) overlay.
	inline bool    ShowWorldESP = false;
	inline bool    ShowWorldProjectileTimers = true;
	inline bool    ShowWorldSmokeTimer = true;
	inline bool    ShowWorldInfernoTimer = true;
	inline bool    ShowWorldDecoyTimer = true;
	inline ImColor WorldESPColor = ImColor(255, 255, 255, 200);

	// ======== ESP gap-closure stage 3b: Weapon Ammo ESP (Task 13) ========
	inline bool    ShowWeaponAmmo = false;
	inline float   WeaponAmmoFontSize = 12.f;
	inline ImColor WeaponAmmoColor = ImColor(255, 255, 255, 255);
	inline ImColor WeaponLowAmmoColor = ImColor(255, 50, 50, 255);

	// ======== ESP gap-closure stage 3b: Weapon Icon ESP (Task 13) ========
	// Renders the active weapon's icon glyph (from weapons.ttf) above the
	// weapon name. Falls back to the weapon name text when the icon font
	// is unavailable or the weapon is unknown.
	inline bool    ShowWeaponIcon = false;
	inline float   WeaponIconFontSize = 18.f;
	inline ImColor WeaponIconColor = ImColor(240, 240, 240, 240);
	// Skip the icon for knives (they all share one glyph and add clutter).
	inline bool    WeaponIconNoKnife = true;

	// ======== ESP gap-closure stage 3b: Dropped-Weapon World ESP (Task 12/16) ========
	// When ShowWorldESP is on, dropped weapons on the ground are rendered
	// with their icon + name. EspItemEnabledMask gates which weapon ids
	// are drawn (default: all enabled).
	inline bool    ShowWorldItems = false;
	inline float   WorldItemFontSize = 13.f;
	inline std::bitset<1200> EspItemEnabledMask;

	// ======== ESP gap-closure stage 3b: Bar Value Labels (Task 14) ========
	inline bool    ShowHealthText = false;
	inline bool    ShowArmorText = false;
	inline float   BarLabelFontSize = 11.f;

	// ======== Text Customization ========
	inline ImColor NameColor = ImColor(255, 255, 255, 255);
	inline float NameFontSize = 14.f;
	inline ImColor WeaponColor = ImColor(200, 200, 200, 255);
	inline float WeaponFontSize = 14.f;
	inline ImColor DistanceColor = ImColor(255, 255, 255, 255);
	inline float DistanceFontSize = 14.f;

	// ======== Hotkey Bindings ========
	enum HotkeyActionType {
		HOTKEY_TOGGLE_BOX_ESP = 0,
		HOTKEY_TOGGLE_BONE_ESP,
		HOTKEY_TOGGLE_HEALTH_BAR,
		HOTKEY_TOGGLE_WEAPON_ESP,
		HOTKEY_TOGGLE_PLAYER_NAME,
		HOTKEY_TOGGLE_DISTANCE,
		HOTKEY_TOGGLE_EYE_RAY,
		HOTKEY_TOGGLE_SNAPLINE,
		HOTKEY_TOGGLE_BOMB_ESP,
		HOTKEY_TOGGLE_PROJECTILE_ESP,
		HOTKEY_TOGGLE_SPECTATOR_LIST,
		HOTKEY_TOGGLE_TEAM_CHECK,
		HOTKEY_TOGGLE_WEB_RADAR,
		HOTKEY_TOGGLE_SAFE_ZONE,
		HOTKEY_TOGGLE_CROSSHAIR,
		HOTKEY_RELOAD_GAME,
		HOTKEY_COUNT
	};

	struct HotkeyBinding {
		int vkCode = 0;
		char keyName[32] = "None";
		bool isListening = false;
		bool wasPressed = false;
	};

	inline HotkeyBinding Hotkeys[HOTKEY_COUNT];

}

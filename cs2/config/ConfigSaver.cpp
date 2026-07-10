#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cstdlib>
#include "ConfigSaver.h"
#include "../game/MenuConfig.h"
#include "../render/GrenadeHelper.h"
#include "../utils/Logger.h"

namespace MyConfigSaver {

    void SaveConfig(const std::string& filename) {
        std::ofstream configFile(MenuConfig::path+'/'+filename);
        if (!configFile.is_open()) {
            LOG_DEBUG("Config", "SaveConfig: failed to open '{}'", MenuConfig::path + '/' + filename);
            return;
        }
        LOG_TRACE("Config", "SaveConfig: writing '{}'", filename);
       
        configFile << "ShowBoneESP " << MenuConfig::ShowBoneESP << std::endl;
        configFile << "ShowBoxESP " << MenuConfig::ShowBoxESP << std::endl;
        configFile << "ShowHealthBar " << MenuConfig::ShowHealthBar << std::endl;
        configFile << "ShowLineToEnemy " << MenuConfig::ShowLineToEnemy << std::endl;
        configFile << "ShowWeaponESP " << MenuConfig::ShowWeaponESP << std::endl;
        configFile << "ShowDistance " << MenuConfig::ShowDistance << std::endl;
        configFile << "ShowEyeRay " << MenuConfig::ShowEyeRay << std::endl;
        configFile << "ShowPlayerName " << MenuConfig::ShowPlayerName << std::endl;
        configFile << "HealthBarType " << MenuConfig::HealthBarType << std::endl;
        configFile << "BoneColor " << MenuConfig::BoneColor.Value.x << " " << MenuConfig::BoneColor.Value.y << " " << MenuConfig::BoneColor.Value.z << " " << MenuConfig::BoneColor.Value.w << std::endl;
        configFile << "LineToEnemyColor " << MenuConfig::LineToEnemyColor.Value.x << " " << MenuConfig::LineToEnemyColor.Value.y << " " << MenuConfig::LineToEnemyColor.Value.z << " " << MenuConfig::LineToEnemyColor.Value.w << std::endl;
        configFile << "BoxColor " << MenuConfig::BoxColor.Value.x << " " << MenuConfig::BoxColor.Value.y << " " << MenuConfig::BoxColor.Value.z << " " << MenuConfig::BoxColor.Value.w << std::endl;
        configFile << "EyeRayColor " << MenuConfig::EyeRayColor.Value.x << " " << MenuConfig::EyeRayColor.Value.y << " " << MenuConfig::EyeRayColor.Value.z << " " << MenuConfig::EyeRayColor.Value.w << std::endl;
        configFile << "ShowMenu " << MenuConfig::ShowMenu << std::endl;
        configFile << "BoxType " << MenuConfig::BoxType << std::endl;
        configFile << "TeamCheck " << MenuConfig::TeamCheck << std::endl;
        configFile << "Frames " << MenuConfig::MaxFrameRate << std::endl;
        configFile << "BoxThickness " << MenuConfig::BoxThickness << std::endl;
        configFile << "BoxRounding " << MenuConfig::BoxRounding << std::endl;
        configFile << "BoxFilled " << MenuConfig::BoxFilled << std::endl;
        configFile << "BoxFillAlpha " << MenuConfig::BoxFillAlpha << std::endl;
        configFile << "CornerLength " << MenuConfig::CornerLength << std::endl;
        configFile << "BoneThickness " << MenuConfig::BoneThickness << std::endl;
        configFile << "ShowHeadDot " << MenuConfig::ShowHeadDot << std::endl;
        configFile << "HeadDotColor " << MenuConfig::HeadDotColor.Value.x << " " << MenuConfig::HeadDotColor.Value.y << " " << MenuConfig::HeadDotColor.Value.z << " " << MenuConfig::HeadDotColor.Value.w << std::endl;
        configFile << "HeadDotSize " << MenuConfig::HeadDotSize << std::endl;
        configFile << "ShowArmorBar " << MenuConfig::ShowArmorBar << std::endl;
        configFile << "ArmorBarType " << MenuConfig::ArmorBarType << std::endl;
        configFile << "ArmorBarColor " << MenuConfig::ArmorBarColor.Value.x << " " << MenuConfig::ArmorBarColor.Value.y << " " << MenuConfig::ArmorBarColor.Value.z << " " << MenuConfig::ArmorBarColor.Value.w << std::endl;
        configFile << "ArmorBarWidth " << MenuConfig::ArmorBarWidth << std::endl;
        configFile << "HealthBarWidth " << MenuConfig::HealthBarWidth << std::endl;
        configFile << "EyeRayLength " << MenuConfig::EyeRayLength << std::endl;
        configFile << "EyeRayThickness " << MenuConfig::EyeRayThickness << std::endl;
        configFile << "LineToEnemyThickness " << MenuConfig::LineToEnemyThickness << std::endl;
        configFile << "LineToEnemyOrigin " << MenuConfig::LineToEnemyOrigin << std::endl;
        configFile << "NameColor " << MenuConfig::NameColor.Value.x << " " << MenuConfig::NameColor.Value.y << " " << MenuConfig::NameColor.Value.z << " " << MenuConfig::NameColor.Value.w << std::endl;
        configFile << "NameFontSize " << MenuConfig::NameFontSize << std::endl;
        configFile << "WeaponColor " << MenuConfig::WeaponColor.Value.x << " " << MenuConfig::WeaponColor.Value.y << " " << MenuConfig::WeaponColor.Value.z << " " << MenuConfig::WeaponColor.Value.w << std::endl;
        configFile << "WeaponFontSize " << MenuConfig::WeaponFontSize << std::endl;
        configFile << "DistanceColor " << MenuConfig::DistanceColor.Value.x << " " << MenuConfig::DistanceColor.Value.y << " " << MenuConfig::DistanceColor.Value.z << " " << MenuConfig::DistanceColor.Value.w << std::endl;
        configFile << "DistanceFontSize " << MenuConfig::DistanceFontSize << std::endl;
        configFile << "SafeZoneEnabled " << MenuConfig::SafeZoneEnabled << std::endl;
        configFile << "SafeZoneRadius " << MenuConfig::SafeZoneRadius << std::endl;
        configFile << "SafeZoneShape " << MenuConfig::SafeZoneShape << std::endl;
        configFile << "SafeZoneMode " << MenuConfig::SafeZoneMode << std::endl;
        configFile << "SafeZoneSkipBox " << MenuConfig::SafeZoneSkipBox << std::endl;
        configFile << "SafeZoneSkipBone " << MenuConfig::SafeZoneSkipBone << std::endl;
        configFile << "SafeZoneSkipHealthBar " << MenuConfig::SafeZoneSkipHealthBar << std::endl;
        configFile << "SafeZoneSkipArmorBar " << MenuConfig::SafeZoneSkipArmorBar << std::endl;
        configFile << "SafeZoneSkipWeapon " << MenuConfig::SafeZoneSkipWeapon << std::endl;
        configFile << "SafeZoneSkipName " << MenuConfig::SafeZoneSkipName << std::endl;
        configFile << "SafeZoneSkipSnapline " << MenuConfig::SafeZoneSkipSnapline << std::endl;
        configFile << "SafeZoneSkipEyeRay " << MenuConfig::SafeZoneSkipEyeRay << std::endl;
        configFile << "SafeZoneSkipHeadDot " << MenuConfig::SafeZoneSkipHeadDot << std::endl;
        configFile << "SafeZoneSkipDistance " << MenuConfig::SafeZoneSkipDistance << std::endl;
        configFile << "CrosshairEnabled " << MenuConfig::CrosshairEnabled << std::endl;
        configFile << "CrosshairSize " << MenuConfig::CrosshairSize << std::endl;
        configFile << "CrosshairThickness " << MenuConfig::CrosshairThickness << std::endl;
        configFile << "CrosshairGap " << MenuConfig::CrosshairGap << std::endl;
        configFile << "CrosshairStyle " << MenuConfig::CrosshairStyle << std::endl;
        configFile << "CrosshairColor " << MenuConfig::CrosshairColor.Value.x << " " << MenuConfig::CrosshairColor.Value.y << " " << MenuConfig::CrosshairColor.Value.z << " " << MenuConfig::CrosshairColor.Value.w << std::endl;
        configFile << "CrosshairOnEnemyColor " << MenuConfig::CrosshairOnEnemyColor << std::endl;
        configFile << "CrosshairEnemyColor " << MenuConfig::CrosshairEnemyColor.Value.x << " " << MenuConfig::CrosshairEnemyColor.Value.y << " " << MenuConfig::CrosshairEnemyColor.Value.z << " " << MenuConfig::CrosshairEnemyColor.Value.w << std::endl;
        configFile << "VSync " << MenuConfig::VSync << std::endl;
        configFile << "RenderWidth " << MenuConfig::RenderWidth << std::endl;
        configFile << "RenderHeight " << MenuConfig::RenderHeight << std::endl;
        configFile << "MonitorIndex " << MenuConfig::MonitorIndex << std::endl;
        configFile << "DebugLog " << MenuConfig::DebugLog << std::endl;
        configFile << "SelectedLanguage " << MenuConfig::SelectedLanguage << std::endl;
        configFile << "ShowWebRadar " << MenuConfig::ShowWebRadar << std::endl;
        configFile << "WebRadarPort " << MenuConfig::WebRadarPort << std::endl;
        configFile << "WebRadarInterval " << MenuConfig::WebRadarInterval << std::endl;
        configFile << "WebRadarPasswordEnabled " << MenuConfig::WebRadarPasswordEnabled << std::endl;
        configFile << "WebRadarPassword " << MenuConfig::WebRadarPassword << std::endl;
        configFile << "WebRadarOriginAllowlist " << MenuConfig::WebRadarOriginAllowlist << std::endl;
        configFile << "WebRadarCloudflareTunnel " << MenuConfig::WebRadarCloudflareTunnel << std::endl;
        configFile << "ShowBombESP " << MenuConfig::ShowBombESP << std::endl;
        configFile << "BombPlantedColor " << MenuConfig::BombPlantedColor.Value.x << " " << MenuConfig::BombPlantedColor.Value.y << " " << MenuConfig::BombPlantedColor.Value.z << " " << MenuConfig::BombPlantedColor.Value.w << std::endl;
        configFile << "BombCarrierColor " << MenuConfig::BombCarrierColor.Value.x << " " << MenuConfig::BombCarrierColor.Value.y << " " << MenuConfig::BombCarrierColor.Value.z << " " << MenuConfig::BombCarrierColor.Value.w << std::endl;
        configFile << "BombDroppedColor " << MenuConfig::BombDroppedColor.Value.x << " " << MenuConfig::BombDroppedColor.Value.y << " " << MenuConfig::BombDroppedColor.Value.z << " " << MenuConfig::BombDroppedColor.Value.w << std::endl;
        configFile << "BombDefusingColor " << MenuConfig::BombDefusingColor.Value.x << " " << MenuConfig::BombDefusingColor.Value.y << " " << MenuConfig::BombDefusingColor.Value.z << " " << MenuConfig::BombDefusingColor.Value.w << std::endl;
        configFile << "ShowSpectatorList " << MenuConfig::ShowSpectatorList << std::endl;
        configFile << "ShowPerfMonitor " << MenuConfig::ShowPerfMonitor << std::endl;
        configFile << "ShowEspPreview " << MenuConfig::ShowEspPreview << std::endl;
        configFile << "ShowDebugStats " << MenuConfig::ShowDebugStats << std::endl;
        configFile << "WebRadarAutoReload " << MenuConfig::WebRadarAutoReload << std::endl;
        configFile << "ShowProjectileESP " << MenuConfig::ShowProjectileESP << std::endl;
        configFile << "ShowProjectileRange " << MenuConfig::ShowProjectileRange << std::endl;
        configFile << "ProjectileRangeAlpha " << MenuConfig::ProjectileRangeAlpha << std::endl;
        configFile << "MenuHotKey " << MenuConfig::MenuHotKey << std::endl;

        // ======== ESP Gap-Closure Stage 3 (Task 7-14) ========
        // Task 8: Player Flags
        configFile << "ShowPlayerFlags " << MenuConfig::ShowPlayerFlags << std::endl;
        configFile << "FlagBlindEnabled " << MenuConfig::FlagBlindEnabled << std::endl;
        configFile << "FlagBlindColor " << MenuConfig::FlagBlindColor.Value.x << " " << MenuConfig::FlagBlindColor.Value.y << " " << MenuConfig::FlagBlindColor.Value.z << " " << MenuConfig::FlagBlindColor.Value.w << std::endl;
        configFile << "FlagScopedEnabled " << MenuConfig::FlagScopedEnabled << std::endl;
        configFile << "FlagScopedColor " << MenuConfig::FlagScopedColor.Value.x << " " << MenuConfig::FlagScopedColor.Value.y << " " << MenuConfig::FlagScopedColor.Value.z << " " << MenuConfig::FlagScopedColor.Value.w << std::endl;
        configFile << "FlagDefusingEnabled " << MenuConfig::FlagDefusingEnabled << std::endl;
        configFile << "FlagDefusingColor " << MenuConfig::FlagDefusingColor.Value.x << " " << MenuConfig::FlagDefusingColor.Value.y << " " << MenuConfig::FlagDefusingColor.Value.z << " " << MenuConfig::FlagDefusingColor.Value.w << std::endl;
        configFile << "FlagKitEnabled " << MenuConfig::FlagKitEnabled << std::endl;
        configFile << "FlagKitColor " << MenuConfig::FlagKitColor.Value.x << " " << MenuConfig::FlagKitColor.Value.y << " " << MenuConfig::FlagKitColor.Value.z << " " << MenuConfig::FlagKitColor.Value.w << std::endl;
        configFile << "FlagMoneyEnabled " << MenuConfig::FlagMoneyEnabled << std::endl;
        configFile << "FlagMoneyColor " << MenuConfig::FlagMoneyColor.Value.x << " " << MenuConfig::FlagMoneyColor.Value.y << " " << MenuConfig::FlagMoneyColor.Value.z << " " << MenuConfig::FlagMoneyColor.Value.w << std::endl;
        configFile << "FlagFontSize " << MenuConfig::FlagFontSize << std::endl;
        // Task 9: Visibility Coloring
        configFile << "VisibilityColoring " << MenuConfig::VisibilityColoring << std::endl;
        configFile << "VisibleColor " << MenuConfig::VisibleColor.Value.x << " " << MenuConfig::VisibleColor.Value.y << " " << MenuConfig::VisibleColor.Value.z << " " << MenuConfig::VisibleColor.Value.w << std::endl;
        configFile << "HiddenColor " << MenuConfig::HiddenColor.Value.x << " " << MenuConfig::HiddenColor.Value.y << " " << MenuConfig::HiddenColor.Value.z << " " << MenuConfig::HiddenColor.Value.w << std::endl;
        // Task 5-8: VPK Visibility Check
        configFile << "VPKVisibilityCheck " << MenuConfig::VPKVisibilityCheck << std::endl;
        // Task 10: Sound ESP
        configFile << "ShowSoundESP " << MenuConfig::ShowSoundESP << std::endl;
        configFile << "SoundESPColor " << MenuConfig::SoundESPColor.Value.x << " " << MenuConfig::SoundESPColor.Value.y << " " << MenuConfig::SoundESPColor.Value.z << " " << MenuConfig::SoundESPColor.Value.w << std::endl;
        // Task 11: C4 Bomb Timer
        configFile << "ShowBombTimer " << MenuConfig::ShowBombTimer << std::endl;
        configFile << "BombTimerX " << MenuConfig::BombTimerX << std::endl;
        configFile << "BombTimerY " << MenuConfig::BombTimerY << std::endl;
        // Task 12: World ESP
        configFile << "ShowWorldProjectileTimers " << MenuConfig::ShowWorldProjectileTimers << std::endl;
        configFile << "ShowWorldSmokeTimer " << MenuConfig::ShowWorldSmokeTimer << std::endl;
        configFile << "ShowWorldInfernoTimer " << MenuConfig::ShowWorldInfernoTimer << std::endl;
        configFile << "ShowWorldDecoyTimer " << MenuConfig::ShowWorldDecoyTimer << std::endl;
        configFile << "WorldESPColor " << MenuConfig::WorldESPColor.Value.x << " " << MenuConfig::WorldESPColor.Value.y << " " << MenuConfig::WorldESPColor.Value.z << " " << MenuConfig::WorldESPColor.Value.w << std::endl;
        // Task 13: Weapon Ammo
        configFile << "ShowWeaponAmmo " << MenuConfig::ShowWeaponAmmo << std::endl;
        configFile << "WeaponAmmoFontSize " << MenuConfig::WeaponAmmoFontSize << std::endl;
        configFile << "WeaponAmmoColor " << MenuConfig::WeaponAmmoColor.Value.x << " " << MenuConfig::WeaponAmmoColor.Value.y << " " << MenuConfig::WeaponAmmoColor.Value.z << " " << MenuConfig::WeaponAmmoColor.Value.w << std::endl;
        configFile << "WeaponLowAmmoColor " << MenuConfig::WeaponLowAmmoColor.Value.x << " " << MenuConfig::WeaponLowAmmoColor.Value.y << " " << MenuConfig::WeaponLowAmmoColor.Value.z << " " << MenuConfig::WeaponLowAmmoColor.Value.w << std::endl;
        // Task 13: Weapon Icon
        configFile << "ShowWeaponIcon " << MenuConfig::ShowWeaponIcon << std::endl;
        configFile << "WeaponIconFontSize " << MenuConfig::WeaponIconFontSize << std::endl;
        configFile << "WeaponIconColor " << MenuConfig::WeaponIconColor.Value.x << " " << MenuConfig::WeaponIconColor.Value.y << " " << MenuConfig::WeaponIconColor.Value.z << " " << MenuConfig::WeaponIconColor.Value.w << std::endl;
        configFile << "WeaponIconNoKnife " << MenuConfig::WeaponIconNoKnife << std::endl;
        // Task 12/16: Dropped-weapon world ESP
        configFile << "ShowWorldItems " << MenuConfig::ShowWorldItems << std::endl;
        configFile << "WorldItemFontSize " << MenuConfig::WorldItemFontSize << std::endl;
        // Task 16: EspItemEnabledMask serialized as a 0/1 string (1200 chars).
        {
            std::string maskStr(MenuConfig::EspItemEnabledMask.size(), '0');
            for (size_t i = 0; i < MenuConfig::EspItemEnabledMask.size(); i++)
                maskStr[i] = MenuConfig::EspItemEnabledMask.test(i) ? '1' : '0';
            configFile << "EspItemMask " << maskStr << std::endl;
        }
        // Task 14: Bar Value Labels
        configFile << "ShowHealthText " << MenuConfig::ShowHealthText << std::endl;
        configFile << "ShowArmorText " << MenuConfig::ShowArmorText << std::endl;
        configFile << "BarLabelFontSize " << MenuConfig::BarLabelFontSize << std::endl;
        // Stage 2: Render Quality
        configFile << "InterpolationEnabled " << MenuConfig::InterpolationEnabled << std::endl;
        configFile << "BoneReliabilityEnabled " << MenuConfig::BoneReliabilityEnabled << std::endl;

        // Hotkey bindings
        for (int i = 0; i < MenuConfig::HOTKEY_COUNT; i++) {
            configFile << "Hotkey_" << i << " " << MenuConfig::Hotkeys[i].vkCode << std::endl;
        }

        configFile.close();
    }

    void LoadConfig(const std::string& filename) {
        std::string tempkey;

        std::ifstream configFile(MenuConfig::path + '/' + filename);
        if (!configFile.is_open()) {
            LOG_DEBUG("Config", "LoadConfig: file not found '{}'", MenuConfig::path + '/' + filename);
            return;
        }
        LOG_DEBUG("Config", "LoadConfig: reading '{}'", filename);

        std::string line;
        while (std::getline(configFile, line)) {
            std::istringstream iss(line);
            std::string key;
            if (iss >> key) {
                if (key == "ShowBoneESP") iss >> MenuConfig::ShowBoneESP;
                if (key == "ShowBoxESP") iss >> MenuConfig::ShowBoxESP;
                if (key == "ShowHealthBar") iss >> MenuConfig::ShowHealthBar;
                if (key == "ShowLineToEnemy") iss >> MenuConfig::ShowLineToEnemy;
                if (key == "ShowWeaponESP") iss >> MenuConfig::ShowWeaponESP;
                if (key == "ShowDistance") iss >> MenuConfig::ShowDistance;
                if (key == "ShowEyeRay") iss >> MenuConfig::ShowEyeRay;
                if (key == "ShowPlayerName") iss >> MenuConfig::ShowPlayerName;
                if (key == "HealthBarType") iss >> MenuConfig::HealthBarType;
                if (key == "BoneColor") iss >> MenuConfig::BoneColor.Value.x >> MenuConfig::BoneColor.Value.y >> MenuConfig::BoneColor.Value.z >> MenuConfig::BoneColor.Value.w;
                if (key == "LineToEnemyColor") iss >> MenuConfig::LineToEnemyColor.Value.x >> MenuConfig::LineToEnemyColor.Value.y >> MenuConfig::LineToEnemyColor.Value.z >> MenuConfig::LineToEnemyColor.Value.w;
                if (key == "BoxColor") iss >> MenuConfig::BoxColor.Value.x >> MenuConfig::BoxColor.Value.y >> MenuConfig::BoxColor.Value.z >> MenuConfig::BoxColor.Value.w;
                if (key == "EyeRayColor") iss >> MenuConfig::EyeRayColor.Value.x >> MenuConfig::EyeRayColor.Value.y >> MenuConfig::EyeRayColor.Value.z >> MenuConfig::EyeRayColor.Value.w;
                if (key == "ShowMenu") iss >> MenuConfig::ShowMenu;
                if (key == "BoxType") iss >> MenuConfig::BoxType;
                if (key == "TeamCheck") iss >> MenuConfig::TeamCheck;
                if (key == "Frames") iss >> MenuConfig::MaxFrameRate;
                if (key == "BoxThickness") iss >> MenuConfig::BoxThickness;
                if (key == "BoxRounding") iss >> MenuConfig::BoxRounding;
                if (key == "BoxFilled") iss >> MenuConfig::BoxFilled;
                if (key == "BoxFillAlpha") iss >> MenuConfig::BoxFillAlpha;
                if (key == "CornerLength") iss >> MenuConfig::CornerLength;
                if (key == "BoneThickness") iss >> MenuConfig::BoneThickness;
                if (key == "ShowHeadDot") iss >> MenuConfig::ShowHeadDot;
                if (key == "HeadDotColor") iss >> MenuConfig::HeadDotColor.Value.x >> MenuConfig::HeadDotColor.Value.y >> MenuConfig::HeadDotColor.Value.z >> MenuConfig::HeadDotColor.Value.w;
                if (key == "HeadDotSize") iss >> MenuConfig::HeadDotSize;
                if (key == "ShowArmorBar") iss >> MenuConfig::ShowArmorBar;
                if (key == "ArmorBarType") iss >> MenuConfig::ArmorBarType;
                if (key == "ArmorBarColor") iss >> MenuConfig::ArmorBarColor.Value.x >> MenuConfig::ArmorBarColor.Value.y >> MenuConfig::ArmorBarColor.Value.z >> MenuConfig::ArmorBarColor.Value.w;
                if (key == "ArmorBarWidth") iss >> MenuConfig::ArmorBarWidth;
                if (key == "HealthBarWidth") iss >> MenuConfig::HealthBarWidth;
                if (key == "EyeRayLength") iss >> MenuConfig::EyeRayLength;
                if (key == "EyeRayThickness") iss >> MenuConfig::EyeRayThickness;
                if (key == "LineToEnemyThickness") iss >> MenuConfig::LineToEnemyThickness;
                if (key == "LineToEnemyOrigin") iss >> MenuConfig::LineToEnemyOrigin;
                if (key == "NameColor") iss >> MenuConfig::NameColor.Value.x >> MenuConfig::NameColor.Value.y >> MenuConfig::NameColor.Value.z >> MenuConfig::NameColor.Value.w;
                if (key == "NameFontSize") iss >> MenuConfig::NameFontSize;
                if (key == "WeaponColor") iss >> MenuConfig::WeaponColor.Value.x >> MenuConfig::WeaponColor.Value.y >> MenuConfig::WeaponColor.Value.z >> MenuConfig::WeaponColor.Value.w;
                if (key == "WeaponFontSize") iss >> MenuConfig::WeaponFontSize;
                if (key == "DistanceColor") iss >> MenuConfig::DistanceColor.Value.x >> MenuConfig::DistanceColor.Value.y >> MenuConfig::DistanceColor.Value.z >> MenuConfig::DistanceColor.Value.w;
                if (key == "DistanceFontSize") iss >> MenuConfig::DistanceFontSize;
                if (key == "SafeZoneEnabled") iss >> MenuConfig::SafeZoneEnabled;
                if (key == "SafeZoneRadius") iss >> MenuConfig::SafeZoneRadius;
                if (key == "SafeZoneShape") iss >> MenuConfig::SafeZoneShape;
                if (key == "SafeZoneMode") iss >> MenuConfig::SafeZoneMode;
                if (key == "SafeZoneSkipBox") iss >> MenuConfig::SafeZoneSkipBox;
                if (key == "SafeZoneSkipBone") iss >> MenuConfig::SafeZoneSkipBone;
                if (key == "SafeZoneSkipHealthBar") iss >> MenuConfig::SafeZoneSkipHealthBar;
                if (key == "SafeZoneSkipArmorBar") iss >> MenuConfig::SafeZoneSkipArmorBar;
                if (key == "SafeZoneSkipWeapon") iss >> MenuConfig::SafeZoneSkipWeapon;
                if (key == "SafeZoneSkipName") iss >> MenuConfig::SafeZoneSkipName;
                if (key == "SafeZoneSkipSnapline") iss >> MenuConfig::SafeZoneSkipSnapline;
                if (key == "SafeZoneSkipEyeRay") iss >> MenuConfig::SafeZoneSkipEyeRay;
                if (key == "SafeZoneSkipHeadDot") iss >> MenuConfig::SafeZoneSkipHeadDot;
                if (key == "SafeZoneSkipDistance") iss >> MenuConfig::SafeZoneSkipDistance;
                if (key == "CrosshairEnabled") iss >> MenuConfig::CrosshairEnabled;
                if (key == "CrosshairSize") iss >> MenuConfig::CrosshairSize;
                if (key == "CrosshairThickness") iss >> MenuConfig::CrosshairThickness;
                if (key == "CrosshairGap") iss >> MenuConfig::CrosshairGap;
                if (key == "CrosshairStyle") iss >> MenuConfig::CrosshairStyle;
                if (key == "CrosshairColor") iss >> MenuConfig::CrosshairColor.Value.x >> MenuConfig::CrosshairColor.Value.y >> MenuConfig::CrosshairColor.Value.z >> MenuConfig::CrosshairColor.Value.w;
                if (key == "CrosshairOnEnemyColor") iss >> MenuConfig::CrosshairOnEnemyColor;
                if (key == "CrosshairEnemyColor") iss >> MenuConfig::CrosshairEnemyColor.Value.x >> MenuConfig::CrosshairEnemyColor.Value.y >> MenuConfig::CrosshairEnemyColor.Value.z >> MenuConfig::CrosshairEnemyColor.Value.w;
                if (key == "VSync") iss >> MenuConfig::VSync;
                if (key == "RenderWidth") iss >> MenuConfig::RenderWidth;
                if (key == "RenderHeight") iss >> MenuConfig::RenderHeight;
                if (key == "MonitorIndex") iss >> MenuConfig::MonitorIndex;
                if (key == "DebugLog") iss >> MenuConfig::DebugLog;
                if (key == "SelectedLanguage") iss >> MenuConfig::SelectedLanguage;
                if (key == "ShowWebRadar") iss >> MenuConfig::ShowWebRadar;
                if (key == "WebRadarPort") iss >> MenuConfig::WebRadarPort;
                if (key == "WebRadarInterval") iss >> MenuConfig::WebRadarInterval;
                if (key == "WebRadarPasswordEnabled") iss >> MenuConfig::WebRadarPasswordEnabled;
                if (key == "WebRadarPassword") iss >> MenuConfig::WebRadarPassword;
                if (key == "WebRadarOriginAllowlist") iss >> MenuConfig::WebRadarOriginAllowlist;
                if (key == "WebRadarCloudflareTunnel") iss >> MenuConfig::WebRadarCloudflareTunnel;
                if (key == "ShowBombESP") iss >> MenuConfig::ShowBombESP;
                if (key == "BombPlantedColor") iss >> MenuConfig::BombPlantedColor.Value.x >> MenuConfig::BombPlantedColor.Value.y >> MenuConfig::BombPlantedColor.Value.z >> MenuConfig::BombPlantedColor.Value.w;
                if (key == "BombCarrierColor") iss >> MenuConfig::BombCarrierColor.Value.x >> MenuConfig::BombCarrierColor.Value.y >> MenuConfig::BombCarrierColor.Value.z >> MenuConfig::BombCarrierColor.Value.w;
                if (key == "BombDroppedColor") iss >> MenuConfig::BombDroppedColor.Value.x >> MenuConfig::BombDroppedColor.Value.y >> MenuConfig::BombDroppedColor.Value.z >> MenuConfig::BombDroppedColor.Value.w;
                if (key == "BombDefusingColor") iss >> MenuConfig::BombDefusingColor.Value.x >> MenuConfig::BombDefusingColor.Value.y >> MenuConfig::BombDefusingColor.Value.z >> MenuConfig::BombDefusingColor.Value.w;
                if (key == "ShowSpectatorList") iss >> MenuConfig::ShowSpectatorList;
                if (key == "ShowPerfMonitor") iss >> MenuConfig::ShowPerfMonitor;
                if (key == "ShowEspPreview") iss >> MenuConfig::ShowEspPreview;
                if (key == "ShowDebugStats") iss >> MenuConfig::ShowDebugStats;
                if (key == "WebRadarAutoReload") iss >> MenuConfig::WebRadarAutoReload;
                if (key == "ShowProjectileESP") iss >> MenuConfig::ShowProjectileESP;
                if (key == "ShowProjectileRange") iss >> MenuConfig::ShowProjectileRange;
                if (key == "ProjectileRangeAlpha") iss >> MenuConfig::ProjectileRangeAlpha;
                if (key == "MenuHotKey") {
                    iss >> MenuConfig::MenuHotKey;
                    strcpy_s(MenuConfig::MenuHotKeyName, GrenadeHelper::GetKeyName(MenuConfig::MenuHotKey));
                }
                // ======== ESP Gap-Closure Stage 3 (Task 7-14) ========
                // Note: using independent if (not else-if) to avoid MSVC C1061
                // (block nesting limit) from an overly long else-if chain.
                // Safe because each key is unique.
                // Task 8: Player Flags
                if (key == "ShowPlayerFlags") iss >> MenuConfig::ShowPlayerFlags;
                if (key == "FlagBlindEnabled") iss >> MenuConfig::FlagBlindEnabled;
                if (key == "FlagBlindColor") iss >> MenuConfig::FlagBlindColor.Value.x >> MenuConfig::FlagBlindColor.Value.y >> MenuConfig::FlagBlindColor.Value.z >> MenuConfig::FlagBlindColor.Value.w;
                if (key == "FlagScopedEnabled") iss >> MenuConfig::FlagScopedEnabled;
                if (key == "FlagScopedColor") iss >> MenuConfig::FlagScopedColor.Value.x >> MenuConfig::FlagScopedColor.Value.y >> MenuConfig::FlagScopedColor.Value.z >> MenuConfig::FlagScopedColor.Value.w;
                if (key == "FlagDefusingEnabled") iss >> MenuConfig::FlagDefusingEnabled;
                if (key == "FlagDefusingColor") iss >> MenuConfig::FlagDefusingColor.Value.x >> MenuConfig::FlagDefusingColor.Value.y >> MenuConfig::FlagDefusingColor.Value.z >> MenuConfig::FlagDefusingColor.Value.w;
                if (key == "FlagKitEnabled") iss >> MenuConfig::FlagKitEnabled;
                if (key == "FlagKitColor") iss >> MenuConfig::FlagKitColor.Value.x >> MenuConfig::FlagKitColor.Value.y >> MenuConfig::FlagKitColor.Value.z >> MenuConfig::FlagKitColor.Value.w;
                if (key == "FlagMoneyEnabled") iss >> MenuConfig::FlagMoneyEnabled;
                if (key == "FlagMoneyColor") iss >> MenuConfig::FlagMoneyColor.Value.x >> MenuConfig::FlagMoneyColor.Value.y >> MenuConfig::FlagMoneyColor.Value.z >> MenuConfig::FlagMoneyColor.Value.w;
                if (key == "FlagFontSize") iss >> MenuConfig::FlagFontSize;
                // Task 9: Visibility Coloring
                if (key == "VisibilityColoring") iss >> MenuConfig::VisibilityColoring;
                if (key == "VisibleColor") iss >> MenuConfig::VisibleColor.Value.x >> MenuConfig::VisibleColor.Value.y >> MenuConfig::VisibleColor.Value.z >> MenuConfig::VisibleColor.Value.w;
                if (key == "HiddenColor") iss >> MenuConfig::HiddenColor.Value.x >> MenuConfig::HiddenColor.Value.y >> MenuConfig::HiddenColor.Value.z >> MenuConfig::HiddenColor.Value.w;
                // Task 5-8: VPK Visibility Check
                if (key == "VPKVisibilityCheck") iss >> MenuConfig::VPKVisibilityCheck;
                // Task 10: Sound ESP
                if (key == "ShowSoundESP") iss >> MenuConfig::ShowSoundESP;
                if (key == "SoundESPColor") iss >> MenuConfig::SoundESPColor.Value.x >> MenuConfig::SoundESPColor.Value.y >> MenuConfig::SoundESPColor.Value.z >> MenuConfig::SoundESPColor.Value.w;
                // Task 11: C4 Bomb Timer
                if (key == "ShowBombTimer") iss >> MenuConfig::ShowBombTimer;
                if (key == "BombTimerX") iss >> MenuConfig::BombTimerX;
                if (key == "BombTimerY") iss >> MenuConfig::BombTimerY;
                // Task 12: World ESP
                if (key == "ShowWorldProjectileTimers") iss >> MenuConfig::ShowWorldProjectileTimers;
                if (key == "ShowWorldSmokeTimer") iss >> MenuConfig::ShowWorldSmokeTimer;
                if (key == "ShowWorldInfernoTimer") iss >> MenuConfig::ShowWorldInfernoTimer;
                if (key == "ShowWorldDecoyTimer") iss >> MenuConfig::ShowWorldDecoyTimer;
                if (key == "WorldESPColor") iss >> MenuConfig::WorldESPColor.Value.x >> MenuConfig::WorldESPColor.Value.y >> MenuConfig::WorldESPColor.Value.z >> MenuConfig::WorldESPColor.Value.w;
                // Task 13: Weapon Ammo
                if (key == "ShowWeaponAmmo") iss >> MenuConfig::ShowWeaponAmmo;
                if (key == "WeaponAmmoFontSize") iss >> MenuConfig::WeaponAmmoFontSize;
                if (key == "WeaponAmmoColor") iss >> MenuConfig::WeaponAmmoColor.Value.x >> MenuConfig::WeaponAmmoColor.Value.y >> MenuConfig::WeaponAmmoColor.Value.z >> MenuConfig::WeaponAmmoColor.Value.w;
                if (key == "WeaponLowAmmoColor") iss >> MenuConfig::WeaponLowAmmoColor.Value.x >> MenuConfig::WeaponLowAmmoColor.Value.y >> MenuConfig::WeaponLowAmmoColor.Value.z >> MenuConfig::WeaponLowAmmoColor.Value.w;
                // Task 13: Weapon Icon
                if (key == "ShowWeaponIcon") iss >> MenuConfig::ShowWeaponIcon;
                if (key == "WeaponIconFontSize") iss >> MenuConfig::WeaponIconFontSize;
                if (key == "WeaponIconColor") iss >> MenuConfig::WeaponIconColor.Value.x >> MenuConfig::WeaponIconColor.Value.y >> MenuConfig::WeaponIconColor.Value.z >> MenuConfig::WeaponIconColor.Value.w;
                if (key == "WeaponIconNoKnife") iss >> MenuConfig::WeaponIconNoKnife;
                // Task 12/16: Dropped-weapon world ESP
                if (key == "ShowWorldItems") iss >> MenuConfig::ShowWorldItems;
                if (key == "WorldItemFontSize") iss >> MenuConfig::WorldItemFontSize;
                // Task 16: EspItemEnabledMask — 0/1 string of 1200 chars.
                if (key == "EspItemMask") {
                    std::string maskStr;
                    iss >> maskStr;
                    for (size_t i = 0; i < MenuConfig::EspItemEnabledMask.size() && i < maskStr.size(); i++)
                        MenuConfig::EspItemEnabledMask.set(i, maskStr[i] == '1');
                }
                // Task 14: Bar Value Labels
                if (key == "ShowHealthText") iss >> MenuConfig::ShowHealthText;
                if (key == "ShowArmorText") iss >> MenuConfig::ShowArmorText;
                if (key == "BarLabelFontSize") iss >> MenuConfig::BarLabelFontSize;
                // Stage 2: Render Quality
                if (key == "InterpolationEnabled") iss >> MenuConfig::InterpolationEnabled;
                if (key == "BoneReliabilityEnabled") iss >> MenuConfig::BoneReliabilityEnabled;
                else if (key.substr(0, 7) == "Hotkey_" && key.size() > 7) {
                    int idx = std::atoi(key.substr(7).c_str());
                    if (idx >= 0 && idx < MenuConfig::HOTKEY_COUNT) {
                        iss >> MenuConfig::Hotkeys[idx].vkCode;
                        if (MenuConfig::Hotkeys[idx].vkCode != 0)
                            strcpy_s(MenuConfig::Hotkeys[idx].keyName, GrenadeHelper::GetKeyName(MenuConfig::Hotkeys[idx].vkCode));
                    }
                }
            }
        }

        configFile.close();
        LOG_DEBUG("Config", "LoadConfig: done, DebugLog={} ShowWebRadar={} Language={}", MenuConfig::DebugLog, MenuConfig::ShowWebRadar, MenuConfig::SelectedLanguage);
    }

    static bool g_configDirty = false;

    void MarkDirty()  { g_configDirty = true; }
    bool IsDirty()    { return g_configDirty; }
    void ClearDirty() { g_configDirty = false; }
}

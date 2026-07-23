#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cstdlib>
#include "ConfigSaver.h"
#include "../game/MenuConfig.h"
#include "../render/GrenadeHelper.h"
#include "../utils/Logger.h"
#ifdef AIMBOT_ENABLED
#include "AimConfig.h"
#endif

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
        configFile << "TextOutlineEnabled " << MenuConfig::TextOutlineEnabled << std::endl;
        configFile << "TextOutlineColor " << MenuConfig::TextOutlineColor.Value.x << " " << MenuConfig::TextOutlineColor.Value.y << " " << MenuConfig::TextOutlineColor.Value.z << " " << MenuConfig::TextOutlineColor.Value.w << std::endl;
        configFile << "TextOutlineThickness " << MenuConfig::TextOutlineThickness << std::endl;
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
        configFile << "ShowPerfMonitor " << MenuConfig::ShowPerfMonitor << std::endl;
        configFile << "ShowDebugStats " << MenuConfig::ShowDebugStats << std::endl;
        configFile << "PlayerCountCheckEnabled " << MenuConfig::PlayerCountCheckEnabled << std::endl;
        configFile << "ExpectedPlayerCount " << MenuConfig::ExpectedPlayerCount << std::endl;
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
        // Task 5-8/9: VPK Visibility Check (unified single switch)
        configFile << "VPKVisibilityCheck " << MenuConfig::VPKVisibilityCheck << std::endl;
        configFile << "VisibleColor " << MenuConfig::VisibleColor.Value.x << " " << MenuConfig::VisibleColor.Value.y << " " << MenuConfig::VisibleColor.Value.z << " " << MenuConfig::VisibleColor.Value.w << std::endl;
        configFile << "HiddenColor " << MenuConfig::HiddenColor.Value.x << " " << MenuConfig::HiddenColor.Value.y << " " << MenuConfig::HiddenColor.Value.z << " " << MenuConfig::HiddenColor.Value.w << std::endl;
        // Task 10: Sound ESP
        configFile << "ShowSoundESP " << MenuConfig::ShowSoundESP << std::endl;
        configFile << "SoundESPColor " << MenuConfig::SoundESPColor.Value.x << " " << MenuConfig::SoundESPColor.Value.y << " " << MenuConfig::SoundESPColor.Value.z << " " << MenuConfig::SoundESPColor.Value.w << std::endl;
        // Footstep ESP
        configFile << "ShowFootstepESP " << MenuConfig::ShowFootstepESP << std::endl;
        configFile << "FootstepColor " << MenuConfig::FootstepColor.Value.x << " " << MenuConfig::FootstepColor.Value.y << " " << MenuConfig::FootstepColor.Value.z << " " << MenuConfig::FootstepColor.Value.w << std::endl;
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

        // Hotkey bindings
        for (int i = 0; i < MenuConfig::HOTKEY_COUNT; i++) {
            configFile << "Hotkey_" << i << " " << MenuConfig::Hotkeys[i].vkCode << std::endl;
        }

        // === GrenadeHelper ===
        configFile << "GH_Enabled " << GrenadeHelper::Enabled << std::endl;
        configFile << "GH_MaxDistance " << GrenadeHelper::MaxDistance << std::endl;
        configFile << "GH_AimIndicatorDistance " << GrenadeHelper::AimIndicatorDistance << std::endl;
        configFile << "GH_ShowName " << GrenadeHelper::ShowName << std::endl;
        configFile << "GH_ShowLine " << GrenadeHelper::ShowLine << std::endl;
        configFile << "GH_ShowBox " << GrenadeHelper::ShowBox << std::endl;
        configFile << "GH_BoxSize " << GrenadeHelper::BoxSize << std::endl;
        configFile << "GH_RecordHotKey " << GrenadeHelper::RecordHotKey << std::endl;
        configFile << "GH_AutoSave " << GrenadeHelper::AutoSave << std::endl;
        configFile << "GH_DefaultGrenadeType " << GrenadeHelper::DefaultGrenadeType << std::endl;
        configFile << "GH_DefaultThrowStyle " << GrenadeHelper::DefaultThrowStyle << std::endl;
        configFile << "GH_CrosshairStyle " << GrenadeHelper::CrosshairStyle << std::endl;
        configFile << "GH_CrosshairColor " << GrenadeHelper::CrosshairColorR << ' '
                   << GrenadeHelper::CrosshairColorG << ' '
                   << GrenadeHelper::CrosshairColorB << ' '
                   << GrenadeHelper::CrosshairColorA << std::endl;
        configFile << "GH_CrosshairSize " << GrenadeHelper::CrosshairSize << std::endl;
        configFile << "GH_CrosshairLen " << GrenadeHelper::CrosshairLen << std::endl;
        configFile << "GH_CrosshairGap " << GrenadeHelper::CrosshairGap << std::endl;
        configFile << "GH_CrosshairThickness " << GrenadeHelper::CrosshairThickness << std::endl;
        configFile << "GH_ShowAllAimInDistance " << GrenadeHelper::ShowAllAimInDistance << std::endl;
        configFile << "GH_MaxSimultaneousAims " << GrenadeHelper::MaxSimultaneousAims << std::endl;

#ifdef AIMBOT_ENABLED
        // === Phase 4: AimConfig (自瞄系统配置) ===
        {
            auto& g = AimConfig::Global();
            configFile << "Global_TeamCheck " << g.teamCheck << std::endl;
            configFile << "Global_IgnoreOnShot " << g.ignoreOnShot << std::endl;
            configFile << "Global_PredictionTimeMs " << g.predictionTimeMs << std::endl;
            configFile << "Aim_Spray_Enabled " << g.spray.enabled << std::endl;
            configFile << "Aim_Spray_Mode " << g.spray.mode << std::endl;
            configFile << "Aim_Spray_Strength " << g.spray.strength << std::endl;
            configFile << "Aim_Spray_Dpi " << g.spray.dpi << std::endl;
            configFile << "Aim_Spray_Sensitivity " << g.spray.sensitivity << std::endl;
            configFile << "Aim_Spray_ShowPredictedImpact " << g.spray.showPredictedImpact << std::endl;
        }
        {
            auto& a = AimConfig::AimBot();
            configFile << "Aim_Enabled " << a.enabled << std::endl;
            configFile << "Aim_Hotkey " << a.hotkey << std::endl;
            configFile << "Aim_Fov " << a.fov << std::endl;
            configFile << "Aim_Smooth " << a.smooth << std::endl;
            configFile << "Aim_Bone " << (int)a.bone << std::endl;
            configFile << "Aim_VisualCheck " << a.visualCheck << std::endl;
            configFile << "Aim_BoneFallback " << a.boneFallback << std::endl;
            configFile << "Aim_TargetSwitchDelay " << a.targetSwitchDelay << std::endl;
            configFile << "Aim_FovCircle_Enabled " << a.fovCircle.enabled << std::endl;
            configFile << "Aim_FovCircle_Color " << a.fovCircle.color.Value.x << " " << a.fovCircle.color.Value.y << " " << a.fovCircle.color.Value.z << " " << a.fovCircle.color.Value.w << std::endl;
            for (int i = 0; i < 6; i++) {
                configFile << "Aim_Weapon" << i << "_Fov " << a.perWeapon[i].fov << std::endl;
                configFile << "Aim_Weapon" << i << "_Smooth " << a.perWeapon[i].smooth << std::endl;
                configFile << "Aim_Weapon" << i << "_Bone " << (int)a.perWeapon[i].bone << std::endl;
            }
        }
        {
            auto& t = AimConfig::TriggerBot();
            configFile << "Trigger_Enabled " << t.enabled << std::endl;
            configFile << "Trigger_Hotkey " << t.hotkey << std::endl;
            configFile << "Trigger_Mode " << t.mode << std::endl;
            configFile << "Trigger_Delay " << t.delay << std::endl;
            configFile << "Trigger_DelayJitter " << t.delayJitter << std::endl;
            configFile << "Trigger_HoldMs " << t.holdMs << std::endl;
        }
        {
            auto& m = AimConfig::Magnet();
            configFile << "Magnet_Enabled " << m.enabled << std::endl;
            configFile << "Magnet_Hotkey " << m.hotkey << std::endl;
            configFile << "Magnet_Fov " << m.fov << std::endl;
            configFile << "Magnet_Smooth " << m.smooth << std::endl;
            configFile << "Magnet_Bone " << (int)m.bone << std::endl;
            configFile << "Magnet_VisualCheck " << m.visualCheck << std::endl;
            configFile << "Magnet_BoneFallback " << m.boneFallback << std::endl;
            configFile << "Magnet_TargetSwitchDelay " << m.targetSwitchDelay << std::endl;
            configFile << "Magnet_FovCircle_Enabled " << m.fovCircle.enabled << std::endl;
            configFile << "Magnet_FovCircle_Color " << m.fovCircle.color.Value.x << " " << m.fovCircle.color.Value.y << " " << m.fovCircle.color.Value.z << " " << m.fovCircle.color.Value.w << std::endl;
            for (int i = 0; i < 6; i++) {
                configFile << "Magnet_Weapon" << i << "_Fov " << m.perWeapon[i].fov << std::endl;
                configFile << "Magnet_Weapon" << i << "_Smooth " << m.perWeapon[i].smooth << std::endl;
                configFile << "Magnet_Weapon" << i << "_Bone " << (int)m.perWeapon[i].bone << std::endl;
            }
        }
        {
            auto& h = AimConfig::Hardware();
            configFile << "Hw_Type " << h.type << std::endl;
            configFile << "Hw_Net_Ip " << h.net.ip << std::endl;
            configFile << "Hw_Net_Port " << h.net.port << std::endl;
            configFile << "Hw_Net_Uuid " << h.net.uuid << std::endl;
            configFile << "Hw_BPro_ComPort " << h.bpro.comPort << std::endl;
            configFile << "Hw_BPro_BaudRate " << h.bpro.baudRate << std::endl;
            configFile << "Hw_Makcu_ComPort " << h.makcu.comPort << std::endl;
            configFile << "Hw_Makcu_BaudRate " << h.makcu.baudRate << std::endl;
        }
#endif

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
                if (key == "TextOutlineEnabled") iss >> MenuConfig::TextOutlineEnabled;
                if (key == "TextOutlineColor") iss >> MenuConfig::TextOutlineColor.Value.x >> MenuConfig::TextOutlineColor.Value.y >> MenuConfig::TextOutlineColor.Value.z >> MenuConfig::TextOutlineColor.Value.w;
                if (key == "TextOutlineThickness") iss >> MenuConfig::TextOutlineThickness;
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
                if (key == "ShowPerfMonitor") iss >> MenuConfig::ShowPerfMonitor;
                if (key == "ShowDebugStats") iss >> MenuConfig::ShowDebugStats;
                if (key == "PlayerCountCheckEnabled") iss >> MenuConfig::PlayerCountCheckEnabled;
                if (key == "ExpectedPlayerCount") iss >> MenuConfig::ExpectedPlayerCount;
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
                // Task 5-8/9: VPK Visibility Check (unified single switch)
                if (key == "VPKVisibilityCheck") iss >> MenuConfig::VPKVisibilityCheck;
                if (key == "VisibleColor") iss >> MenuConfig::VisibleColor.Value.x >> MenuConfig::VisibleColor.Value.y >> MenuConfig::VisibleColor.Value.z >> MenuConfig::VisibleColor.Value.w;
                if (key == "HiddenColor") iss >> MenuConfig::HiddenColor.Value.x >> MenuConfig::HiddenColor.Value.y >> MenuConfig::HiddenColor.Value.z >> MenuConfig::HiddenColor.Value.w;
                // Task 10: Sound ESP
                if (key == "ShowSoundESP") iss >> MenuConfig::ShowSoundESP;
                if (key == "SoundESPColor") iss >> MenuConfig::SoundESPColor.Value.x >> MenuConfig::SoundESPColor.Value.y >> MenuConfig::SoundESPColor.Value.z >> MenuConfig::SoundESPColor.Value.w;
                // Footstep ESP
                if (key == "ShowFootstepESP") iss >> MenuConfig::ShowFootstepESP;
                if (key == "FootstepColor") iss >> MenuConfig::FootstepColor.Value.x >> MenuConfig::FootstepColor.Value.y >> MenuConfig::FootstepColor.Value.z >> MenuConfig::FootstepColor.Value.w;
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
                else if (key.substr(0, 7) == "Hotkey_" && key.size() > 7) {
                    int idx = std::atoi(key.substr(7).c_str());
                    if (idx >= 0 && idx < MenuConfig::HOTKEY_COUNT) {
                        iss >> MenuConfig::Hotkeys[idx].vkCode;
                        if (MenuConfig::Hotkeys[idx].vkCode != 0)
                            strcpy_s(MenuConfig::Hotkeys[idx].keyName, GrenadeHelper::GetKeyName(MenuConfig::Hotkeys[idx].vkCode));
                    }
                }

                // === GrenadeHelper ===
                if (key == "GH_Enabled") iss >> GrenadeHelper::Enabled;
                if (key == "GH_MaxDistance") iss >> GrenadeHelper::MaxDistance;
                if (key == "GH_AimIndicatorDistance") iss >> GrenadeHelper::AimIndicatorDistance;
                if (key == "GH_ShowName") iss >> GrenadeHelper::ShowName;
                if (key == "GH_ShowLine") iss >> GrenadeHelper::ShowLine;
                if (key == "GH_ShowBox") iss >> GrenadeHelper::ShowBox;
                if (key == "GH_BoxSize") iss >> GrenadeHelper::BoxSize;
                if (key == "GH_RecordHotKey") {
                    iss >> GrenadeHelper::RecordHotKey;
                    strcpy_s(GrenadeHelper::RecordHotKeyName, GrenadeHelper::GetKeyName(GrenadeHelper::RecordHotKey));
                }
                if (key == "GH_AutoSave") iss >> GrenadeHelper::AutoSave;
                if (key == "GH_DefaultGrenadeType") iss >> GrenadeHelper::DefaultGrenadeType;
                if (key == "GH_DefaultThrowStyle") iss >> GrenadeHelper::DefaultThrowStyle;
                if (key == "GH_CrosshairStyle") iss >> GrenadeHelper::CrosshairStyle;
                if (key == "GH_CrosshairColor") iss >> GrenadeHelper::CrosshairColorR
                                                    >> GrenadeHelper::CrosshairColorG
                                                    >> GrenadeHelper::CrosshairColorB
                                                    >> GrenadeHelper::CrosshairColorA;
                if (key == "GH_CrosshairSize") iss >> GrenadeHelper::CrosshairSize;
                if (key == "GH_CrosshairLen") iss >> GrenadeHelper::CrosshairLen;
                if (key == "GH_CrosshairGap") iss >> GrenadeHelper::CrosshairGap;
                if (key == "GH_CrosshairThickness") iss >> GrenadeHelper::CrosshairThickness;
                if (key == "GH_ShowAllAimInDistance") iss >> GrenadeHelper::ShowAllAimInDistance;
                if (key == "GH_MaxSimultaneousAims") iss >> GrenadeHelper::MaxSimultaneousAims;

#ifdef AIMBOT_ENABLED
                // === Phase 4: AimConfig (自瞄系统配置) ===
                // 独立 if (非 else-if) 避免 MSVC C1061 嵌套层级限制, 每个 key 唯一故安全。
                // 全局配置 (向后兼容: 旧键名 Aim_TeamCheck/Aim_IgnoreOnShot/Aim_PredictionTimeMs/Trigger_TeamCheck 也接受)
                if (key == "Global_TeamCheck") iss >> AimConfig::Global().teamCheck;
                if (key == "Global_IgnoreOnShot") iss >> AimConfig::Global().ignoreOnShot;
                if (key == "Global_PredictionTimeMs") iss >> AimConfig::Global().predictionTimeMs;
                if (key == "Aim_TeamCheck") iss >> AimConfig::Global().teamCheck;
                if (key == "Aim_IgnoreOnShot") iss >> AimConfig::Global().ignoreOnShot;
                if (key == "Aim_PredictionTimeMs") iss >> AimConfig::Global().predictionTimeMs;
                if (key == "Trigger_TeamCheck") iss >> AimConfig::Global().teamCheck;
                // AimBot
                if (key == "Aim_Enabled") iss >> AimConfig::AimBot().enabled;
                if (key == "Aim_Hotkey") iss >> AimConfig::AimBot().hotkey;
                if (key == "Aim_Fov") iss >> AimConfig::AimBot().fov;
                if (key == "Aim_Smooth") iss >> AimConfig::AimBot().smooth;
                if (key == "Aim_Bone") { int b=0; iss >> b; AimConfig::AimBot().bone = (Aim::BoneTarget)b; }
                if (key == "Aim_VisualCheck") iss >> AimConfig::AimBot().visualCheck;
                if (key == "Aim_BoneFallback") iss >> AimConfig::AimBot().boneFallback;
                if (key == "Aim_TargetSwitchDelay") { unsigned v=0; iss >> v; AimConfig::AimBot().targetSwitchDelay = v; }
                if (key == "Aim_Spray_Enabled") iss >> AimConfig::Global().spray.enabled;
                if (key == "Aim_Spray_Mode") iss >> AimConfig::Global().spray.mode;
                if (key == "Aim_Spray_Strength") iss >> AimConfig::Global().spray.strength;
                if (key == "Aim_Spray_Dpi") iss >> AimConfig::Global().spray.dpi;
                if (key == "Aim_Spray_Sensitivity") iss >> AimConfig::Global().spray.sensitivity;
                if (key == "Aim_Spray_ShowPredictedImpact") iss >> AimConfig::Global().spray.showPredictedImpact;
                if (key == "Aim_FovCircle_Enabled") iss >> AimConfig::AimBot().fovCircle.enabled;
                if (key == "Aim_FovCircle_Color") iss >> AimConfig::AimBot().fovCircle.color.Value.x >> AimConfig::AimBot().fovCircle.color.Value.y >> AimConfig::AimBot().fovCircle.color.Value.z >> AimConfig::AimBot().fovCircle.color.Value.w;
                if (key.size() > 12 && key.substr(0,11) == "Aim_Weapon" && key[11] >= '0' && key[11] <= '5') {
                    int idx = key[11] - '0';
                    if (key.substr(12) == "_Fov")    iss >> AimConfig::AimBot().perWeapon[idx].fov;
                    if (key.substr(12) == "_Smooth") iss >> AimConfig::AimBot().perWeapon[idx].smooth;
                    if (key.substr(12) == "_Bone")   { int b=0; iss >> b; AimConfig::AimBot().perWeapon[idx].bone = (Aim::BoneTarget)b; }
                }
                // TriggerBot
                if (key == "Trigger_Enabled") iss >> AimConfig::TriggerBot().enabled;
                if (key == "Trigger_Hotkey") iss >> AimConfig::TriggerBot().hotkey;
                if (key == "Trigger_Mode") iss >> AimConfig::TriggerBot().mode;
                if (key == "Trigger_Delay") iss >> AimConfig::TriggerBot().delay;
                if (key == "Trigger_DelayJitter") iss >> AimConfig::TriggerBot().delayJitter;
                if (key == "Trigger_HoldMs") iss >> AimConfig::TriggerBot().holdMs;
                // Magnet
                if (key == "Magnet_Enabled") iss >> AimConfig::Magnet().enabled;
                if (key == "Magnet_Hotkey") iss >> AimConfig::Magnet().hotkey;
                if (key == "Magnet_Fov") iss >> AimConfig::Magnet().fov;
                if (key == "Magnet_Smooth") iss >> AimConfig::Magnet().smooth;
                if (key == "Magnet_Bone") { int b=0; iss >> b; AimConfig::Magnet().bone = (Aim::BoneTarget)b; }
                if (key == "Magnet_VisualCheck") iss >> AimConfig::Magnet().visualCheck;
                if (key == "Magnet_BoneFallback") iss >> AimConfig::Magnet().boneFallback;
                if (key == "Magnet_TargetSwitchDelay") { unsigned v=0; iss >> v; AimConfig::Magnet().targetSwitchDelay = v; }
                if (key == "Magnet_FovCircle_Enabled") iss >> AimConfig::Magnet().fovCircle.enabled;
                if (key == "Magnet_FovCircle_Color") iss >> AimConfig::Magnet().fovCircle.color.Value.x >> AimConfig::Magnet().fovCircle.color.Value.y >> AimConfig::Magnet().fovCircle.color.Value.z >> AimConfig::Magnet().fovCircle.color.Value.w;
                if (key.size() > 14 && key.substr(0,13) == "Magnet_Weapon" && key[13] >= '0' && key[13] <= '5') {
                    int idx = key[13] - '0';
                    if (key.substr(14) == "_Fov")    iss >> AimConfig::Magnet().perWeapon[idx].fov;
                    if (key.substr(14) == "_Smooth") iss >> AimConfig::Magnet().perWeapon[idx].smooth;
                    if (key.substr(14) == "_Bone")   { int b=0; iss >> b; AimConfig::Magnet().perWeapon[idx].bone = (Aim::BoneTarget)b; }
                }
                if (key == "Hw_Type") iss >> AimConfig::Hardware().type;
                if (key == "Hw_Net_Ip") iss >> AimConfig::Hardware().net.ip;
                if (key == "Hw_Net_Port") iss >> AimConfig::Hardware().net.port;
                if (key == "Hw_Net_Uuid") iss >> AimConfig::Hardware().net.uuid;
                if (key == "Hw_BPro_ComPort") iss >> AimConfig::Hardware().bpro.comPort;
                if (key == "Hw_BPro_BaudRate") iss >> AimConfig::Hardware().bpro.baudRate;
                if (key == "Hw_Makcu_ComPort") iss >> AimConfig::Hardware().makcu.comPort;
                if (key == "Hw_Makcu_BaudRate") iss >> AimConfig::Hardware().makcu.baudRate;
#endif
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

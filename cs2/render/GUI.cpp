#include "GUI.h"
#include "Render.h"
#include "GrenadeHelper.h"
#include "WebRadar.h"
#include "UITheme.h"
#include "../config/ConfigMenu.h"
#include "../config/ConfigSaver.h"
#include "../utils/Logger.h"
#include "../game/map_registry.h"
#include "../render/Cheats.h"
#include <shellapi.h>
#include <thread>
#include <chrono>
#include "../game/OffsetUpdater.h"
#include "../game/Offsets.h"
#include "qrcodegen.h"
#include <atomic>

// Offset update state (for GUI-triggered offset refetch)
static std::atomic<bool> s_offsetUpdateRunning{false};
static std::atomic<bool> s_offsetUpdateSuccess{false};
static std::atomic<bool> s_offsetUpdateDone{false};
static bool s_triggerSuccessPopup = false;
static bool s_triggerFailPopup = false;

// ============================================================================
// Color Scheme: Dark Purple-Blue Accent (UITheme)
// Background: #0F0F14 | Surface: #16161E | Accent: #7c5cfc | Active: #9b7dff
// ============================================================================

static void setStyles() {
	ImGuiStyle* style = &ImGui::GetStyle();

	style->WindowPadding = UITheme::WindowPadding;
	style->WindowRounding = UITheme::RadiusLG;
	style->FramePadding = UITheme::FramePadding;
	style->FrameRounding = UITheme::RadiusSM;
	style->ItemSpacing = UITheme::ItemSpacing;
	style->ItemInnerSpacing = UITheme::ItemInnerSpacing;
	style->IndentSpacing = 16.0f;
	style->ScrollbarSize = 12.0f;
	style->ScrollbarRounding = UITheme::RadiusMD;
	style->GrabMinSize = 6.0f;
	style->GrabRounding = UITheme::RadiusSM;
	style->TabRounding = UITheme::RadiusSM;
	style->ChildRounding = UITheme::RadiusSM;
	style->PopupRounding = UITheme::RadiusSM;

	style->Colors[ImGuiCol_WindowBg]           = UITheme::Background;
	style->Colors[ImGuiCol_ChildBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style->Colors[ImGuiCol_PopupBg]            = ImVec4(0.07f, 0.07f, 0.09f, 0.98f);
	style->Colors[ImGuiCol_Border]             = UITheme::Border;
	style->Colors[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	style->Colors[ImGuiCol_Text]               = UITheme::TextPrimary;
	style->Colors[ImGuiCol_TextDisabled]        = UITheme::TextDisabled;

	style->Colors[ImGuiCol_FrameBg]            = UITheme::FrameBg;
	style->Colors[ImGuiCol_FrameBgHovered]     = UITheme::FrameBgHovered;
	style->Colors[ImGuiCol_FrameBgActive]      = UITheme::FrameBgActive;

	style->Colors[ImGuiCol_TitleBg]            = UITheme::Background;
	style->Colors[ImGuiCol_TitleBgActive]      = UITheme::Background;
	style->Colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(UITheme::Background.x, UITheme::Background.y, UITheme::Background.z, 0.75f);

	style->Colors[ImGuiCol_Button]             = UITheme::AccentBg60;
	style->Colors[ImGuiCol_ButtonHovered]      = ImVec4(UITheme::AccentHover.x, UITheme::AccentHover.y, UITheme::AccentHover.z, 0.80f);
	style->Colors[ImGuiCol_ButtonActive]       = UITheme::Accent;

	style->Colors[ImGuiCol_Header]             = UITheme::Surface;
	style->Colors[ImGuiCol_HeaderHovered]      = UITheme::AccentBg40;
	style->Colors[ImGuiCol_HeaderActive]       = UITheme::AccentBg60;

	style->Colors[ImGuiCol_Tab]                = UITheme::Surface;
	style->Colors[ImGuiCol_TabHovered]         = UITheme::AccentBg60;
	style->Colors[ImGuiCol_TabActive]          = UITheme::AccentBg80;

	style->Colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.30f, 0.30f, 0.38f, 0.60f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.50f, 0.80f);
	style->Colors[ImGuiCol_ScrollbarGrabActive]  = UITheme::Accent;

	style->Colors[ImGuiCol_CheckMark]          = UITheme::AccentHover;
	style->Colors[ImGuiCol_SliderGrab]         = UITheme::AccentBg80;
	style->Colors[ImGuiCol_SliderGrabActive]   = UITheme::AccentHover;

	style->Colors[ImGuiCol_Separator]          = UITheme::Border;
	style->Colors[ImGuiCol_SeparatorHovered]   = UITheme::AccentBg60;
	style->Colors[ImGuiCol_SeparatorActive]    = UITheme::Accent;

	style->Colors[ImGuiCol_ResizeGrip]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style->Colors[ImGuiCol_ResizeGripHovered]  = UITheme::AccentBg40;
	style->Colors[ImGuiCol_ResizeGripActive]   = UITheme::AccentBg80;

	style->Colors[ImGuiCol_PlotLines]          = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered]   = UITheme::AccentHover;
	style->Colors[ImGuiCol_PlotHistogram]      = UITheme::AccentBg80;
	style->Colors[ImGuiCol_PlotHistogramHovered] = UITheme::AccentHover;
	style->Colors[ImGuiCol_TextSelectedBg]     = UITheme::AccentBg20;

	style->Colors[ImGuiCol_MenuBarBg]          = ImVec4(0.08f, 0.07f, 0.10f, 1.00f);
}

// ============================================================================
// Helper: Draw a styled nav button (left sidebar)
// ============================================================================
static bool NavButton(const char* label, bool active, float width, float height = 28.0f) {
	if (active) {
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UITheme::RadiusSM);
		ImGui::PushStyleColor(ImGuiCol_Button, UITheme::AccentBg60);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(UITheme::AccentHover.x, UITheme::AccentHover.y, UITheme::AccentHover.z, 0.90f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::Accent);
		ImGui::PushStyleColor(ImGuiCol_Text, UITheme::TextWhite);
	} else {
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UITheme::RadiusSM);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.28f, 0.50f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::AccentBg40);
		ImGui::PushStyleColor(ImGuiCol_Text, UITheme::TextSecondary);
	}
	bool clicked = ImGui::Button(label, ImVec2(width, height));
	if (active) {
		ImVec2 p = ImGui::GetItemRectMin();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(ImVec2(p.x, p.y + 4), ImVec2(p.x + 3, p.y + height - 4), ImGui::ColorConvertFloat4ToU32(UITheme::Accent), 2.0f);
	}
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
	return clicked;
}

// ============================================================================
// Helper: Section header with accent line
// ============================================================================
static void SectionHeader(const char* label) {
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(p.x + 4, p.y + 18), ImGui::ColorConvertFloat4ToU32(UITheme::Accent), 2.0f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
	ImGui::TextColored(UITheme::TextPrimary, "%s", label);
	ImVec2 afterText = ImGui::GetCursorScreenPos();
	dl->AddRectFilled(ImVec2(afterText.x + 4, afterText.y - 1), ImVec2(afterText.x + ImGui::GetContentRegionAvail().x, afterText.y), ImGui::ColorConvertFloat4ToU32(UITheme::BorderSubtle), 0.0f);
}

// Render a QR code encoding `url` at the given pixel size using the current
// window's ImDrawList. The QR is drawn as filled rectangles (white background,
// dark modules). On failure (e.g. URL too long for QR capacity) a fallback
// message is shown instead.
static void DrawQRCode(const char* url, float size) {
	if (!url || !url[0]) return;
	try {
		qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url, qrcodegen::QrCode::Ecc::MEDIUM);
		int modules = qr.getSize();
		if (modules <= 0) {
			ImGui::TextDisabled("QR code: empty module grid");
			return;
		}
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		float moduleSize = size / static_cast<float>(modules);
		// White background with subtle border
		draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(255, 255, 255, 255));
		// Dark modules
		ImU32 dark = IM_COL32(0, 0, 0, 255);
		for (int y = 0; y < modules; y++) {
			for (int x = 0; x < modules; x++) {
				if (qr.getModule(x, y)) {
					draw->AddRectFilled(
						ImVec2(pos.x + x * moduleSize, pos.y + y * moduleSize),
						ImVec2(pos.x + (x + 1) * moduleSize, pos.y + (y + 1) * moduleSize),
						dark);
				}
			}
		}
		ImGui::Dummy(ImVec2(size, size));
	} catch (const std::exception& e) {
		ImGui::TextDisabled("QR code failed: %s", e.what());
	}
}

// ============================================================================
// Tab 0: Visuals
// ============================================================================
static void DrawTab_Visuals() {
	// Task 22: tooltip helper - shows "(?)" indicator with bilingual tooltip
	auto tip = [](const char* en, const char* zh) {
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip(MenuConfig::SelectedLanguage == 0 ? en : zh);
	};

	if (ImGui::CollapsingHeader(lang.header_playerbox.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_showbox.c_str(), &MenuConfig::ShowBoxESP);
		if (MenuConfig::ShowBoxESP) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_boxcolor.c_str(), reinterpret_cast<float*>(&MenuConfig::BoxColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(180);
			ImGui::Combo(lang.visuals_boxtype.c_str(), &MenuConfig::BoxType, lang.visuals_boxtypeselect, IM_ARRAYSIZE(lang.visuals_boxtypeselect));

			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat((lang.visuals_box_thickness + "##box").c_str(), &MenuConfig::BoxThickness, 0.5f, 5.0f, "%.1f px");
			if (MenuConfig::BoxType != 2) {
				ImGui::SetNextItemWidth(150);
				ImGui::SliderFloat((lang.visuals_rounding + "##box").c_str(), &MenuConfig::BoxRounding, 0.f, 10.f, "%.1f px");
			}
			if (MenuConfig::BoxType == 2) {
				ImGui::SetNextItemWidth(150);
				ImGui::SliderFloat(lang.visuals_cornersize.c_str(), &MenuConfig::CornerLength, 0.1f, 0.5f, "%.1f px");
			}
			Gui.MyCheckBox((lang.visuals_filled + "##box").c_str(), &MenuConfig::BoxFilled);
			if (MenuConfig::BoxFilled) {
				ImGui::SameLine(0, 16);
				ImGui::SetNextItemWidth(120);
				ImGui::SliderFloat(lang.visuals_fillalpha.c_str(), &MenuConfig::BoxFillAlpha, 0.01f, 0.5f, "%.0f");
			}
		}
	}

	if (ImGui::CollapsingHeader(lang.header_skeleton.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_showbone.c_str(), &MenuConfig::ShowBoneESP);
		if (MenuConfig::ShowBoneESP) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_bonecolor.c_str(), reinterpret_cast<float*>(&MenuConfig::BoneColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat((lang.visuals_bone_thickness + "##bone").c_str(), &MenuConfig::BoneThickness, 0.5f, 5.0f, "%.1f px");
		}

		Gui.MyCheckBox(lang.visuals_headdot.c_str(), &MenuConfig::ShowHeadDot);
		if (MenuConfig::ShowHeadDot) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_headdot_color.c_str(), reinterpret_cast<float*>(&MenuConfig::HeadDotColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(120);
			ImGui::SliderFloat(lang.visuals_dotsize.c_str(), &MenuConfig::HeadDotSize, 1.f, 8.f, "%.0f px");
		}

		Gui.MyCheckBox(lang.visuals_showeyeray.c_str(), &MenuConfig::ShowEyeRay);
		if (MenuConfig::ShowEyeRay) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_eyeraycolor.c_str(), reinterpret_cast<float*>(&MenuConfig::EyeRayColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat((lang.visuals_length + "##ray").c_str(), &MenuConfig::EyeRayLength, 10.f, 200.f, "%.0f px");
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat((lang.visuals_eyeray_thickness + "##ray").c_str(), &MenuConfig::EyeRayThickness, 0.5f, 5.0f, "%.1f px");
		}
	}

	// ======== Health & Armor (merged with Bar Value Labels - Task 15) ========
	if (ImGui::CollapsingHeader(lang.header_health.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_showbar.c_str(), &MenuConfig::ShowHealthBar);
		if (MenuConfig::ShowHealthBar) {
			ImGui::SameLine(0, 16);
			ImGui::SetNextItemWidth(160);
			ImGui::Combo(lang.visuals_barpos.c_str(), &MenuConfig::HealthBarType, lang.visuals_heathbarselect, IM_ARRAYSIZE(lang.visuals_heathbarselect));
			if (MenuConfig::HealthBarType == 0) {
				ImGui::SetNextItemWidth(120);
				ImGui::SliderFloat((lang.visuals_healthbar_width + "##hp").c_str(), &MenuConfig::HealthBarWidth, 2.f, 10.f, "%.1f px");
			}
		}

		Gui.MyCheckBox(lang.visuals_armorbar.c_str(), &MenuConfig::ShowArmorBar);
		if (MenuConfig::ShowArmorBar) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_armorbar_color.c_str(), reinterpret_cast<float*>(&MenuConfig::ArmorBarColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(120);
			ImGui::Combo((lang.visuals_armorbar_position + "##armor").c_str(), &MenuConfig::ArmorBarType, lang.armorbar_typeselect, IM_ARRAYSIZE(lang.armorbar_typeselect));
			ImGui::SetNextItemWidth(120);
			ImGui::SliderFloat((lang.visuals_armorbar_width + "##armor").c_str(), &MenuConfig::ArmorBarWidth, 1.f, 8.f, "%.1f px");
		}

		// --- Bar Value Labels (Task 15, moved from Advanced ESP) ---
		Gui.MyCheckBox(lang.visuals_health_text.c_str(), &MenuConfig::ShowHealthText);
		Gui.MyCheckBox(lang.visuals_armor_text.c_str(), &MenuConfig::ShowArmorText);
		if (MenuConfig::ShowHealthText || MenuConfig::ShowArmorText) {
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.visuals_bar_label_fontsize.c_str(), &MenuConfig::BarLabelFontSize, 8.f, 16.f, "%.0f px");
		}
	}

	// ======== Weapon (Task 14, new section: WeaponESP + WeaponAmmo + WeaponIcon) ========
	if (ImGui::CollapsingHeader(lang.header_weapon.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		// --- Weapon ESP (moved from Info & Text) ---
		Gui.MyCheckBox(lang.visuals_weaponesp.c_str(), &MenuConfig::ShowWeaponESP);
		if (MenuConfig::ShowWeaponESP) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_weapon_color.c_str(), reinterpret_cast<float*>(&MenuConfig::WeaponColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(120);
			ImGui::SliderFloat((lang.visuals_weapon_size + "##weapon").c_str(), &MenuConfig::WeaponFontSize, 8.f, 24.f, "%.0f px");
		}

		// --- Weapon Ammo (moved from Advanced ESP) ---
		Gui.MyCheckBox(lang.visuals_weapon_ammo.c_str(), &MenuConfig::ShowWeaponAmmo);
		if (MenuConfig::ShowWeaponAmmo) {
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.visuals_ammo_fontsize.c_str(), &MenuConfig::WeaponAmmoFontSize, 8.f, 20.f, "%.0f px");
			ImGui::ColorEdit4(lang.visuals_ammo_color.c_str(), reinterpret_cast<float*>(&MenuConfig::WeaponAmmoColor), ImGuiColorEditFlags_NoInputs);
			ImGui::ColorEdit4(lang.visuals_low_ammo_color.c_str(), reinterpret_cast<float*>(&MenuConfig::WeaponLowAmmoColor), ImGuiColorEditFlags_NoInputs);
		}

		// --- Weapon Icon (moved from Advanced ESP) ---
		Gui.MyCheckBox(lang.visuals_weapon_icon.c_str(), &MenuConfig::ShowWeaponIcon);
		if (MenuConfig::ShowWeaponIcon) {
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.visuals_weapon_icon_fontsize.c_str(), &MenuConfig::WeaponIconFontSize, 10.f, 28.f, "%.0f px");
			ImGui::ColorEdit4(lang.visuals_weapon_icon_color.c_str(), reinterpret_cast<float*>(&MenuConfig::WeaponIconColor), ImGuiColorEditFlags_NoInputs);
			Gui.MyCheckBox(lang.visuals_weapon_icon_noknife.c_str(), &MenuConfig::WeaponIconNoKnife);
		}
	}

	// ======== Info & Text (Task 17: removed WeaponESP, added Spectator List) ========
	if (ImGui::CollapsingHeader(lang.header_info.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_name.c_str(), &MenuConfig::ShowPlayerName);
		if (MenuConfig::ShowPlayerName) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_name_color.c_str(), reinterpret_cast<float*>(&MenuConfig::NameColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(120);
			ImGui::SliderFloat((lang.visuals_name_size + "##name").c_str(), &MenuConfig::NameFontSize, 8.f, 24.f, "%.0f px");
		}

		Gui.MyCheckBox(lang.visuals_distance.c_str(), &MenuConfig::ShowDistance);
		if (MenuConfig::ShowDistance) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_distance_color.c_str(), reinterpret_cast<float*>(&MenuConfig::DistanceColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(120);
			ImGui::SliderFloat((lang.visuals_distance_size + "##dist").c_str(), &MenuConfig::DistanceFontSize, 8.f, 24.f, "%.0f px");
		}

		// --- Spectator List (moved from its own section - Task 17) ---
		Gui.MyCheckBox(lang.visuals_spectatorlist.c_str(), &MenuConfig::ShowSpectatorList);
	}

	if (ImGui::CollapsingHeader(lang.header_snapline.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_line.c_str(), &MenuConfig::ShowLineToEnemy);
		if (MenuConfig::ShowLineToEnemy) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_linecolor.c_str(), reinterpret_cast<float*>(&MenuConfig::LineToEnemyColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat((lang.visuals_line_thickness + "##line").c_str(), &MenuConfig::LineToEnemyThickness, 0.5f, 5.0f, "%.1f px");
			ImGui::SetNextItemWidth(120);
			ImGui::Combo((lang.visuals_origin + "##line").c_str(), &MenuConfig::LineToEnemyOrigin, lang.snapline_originselect, IM_ARRAYSIZE(lang.snapline_originselect));
		}
	}

	// ======== C4 / Bomb (Task 13: merged with BombTimer) ========
	if (ImGui::CollapsingHeader(lang.header_bomb.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_bombesp.c_str(), &MenuConfig::ShowBombESP);
		if (MenuConfig::ShowBombESP) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_bombplanted.c_str(), reinterpret_cast<float*>(&MenuConfig::BombPlantedColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine(0, 12);
			ImGui::ColorEdit4(lang.visuals_bombdefusing.c_str(), reinterpret_cast<float*>(&MenuConfig::BombDefusingColor), ImGuiColorEditFlags_NoInputs);

			ImGui::ColorEdit4(lang.visuals_bombcarrier.c_str(), reinterpret_cast<float*>(&MenuConfig::BombCarrierColor), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine(0, 12);
			ImGui::ColorEdit4(lang.visuals_bombdropped.c_str(), reinterpret_cast<float*>(&MenuConfig::BombDroppedColor), ImGuiColorEditFlags_NoInputs);
		}

		// --- C4 Bomb Timer (moved from Advanced ESP - Task 13) ---
		Gui.MyCheckBox(lang.visuals_bomb_timer.c_str(), &MenuConfig::ShowBombTimer);
		tip("C4 countdown window (draggable)", "C4 倒计时窗口（可拖动）");
	}

	// ======== World & Projectiles (Task 17: merged Grenade Projectile ESP + World ESP) ========
	if (ImGui::CollapsingHeader(lang.header_world_proj.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		// --- Grenade Projectile ESP (moved from its own section) ---
		Gui.MyCheckBox(lang.proj_enable.c_str(), &MenuConfig::ShowProjectileESP);
		tip("Projectile real-time position and range circle", "投掷物实时位置和范围圈");
		if (MenuConfig::ShowProjectileESP) {
			Gui.MyCheckBox(lang.proj_range.c_str(), &MenuConfig::ShowProjectileRange);
			tip("Projectile effect range circle", "投掷物生效范围圈");
			if (MenuConfig::ShowProjectileRange) {
				ImGui::SetNextItemWidth(180);
				ImGui::SliderFloat(lang.proj_rangealpha.c_str(), &MenuConfig::ProjectileRangeAlpha, 0.02f, 0.5f, "%.0f");
			}
		}

		// --- World ESP (moved from Advanced ESP) ---
		Gui.MyCheckBox(lang.visuals_world_esp.c_str(), &MenuConfig::ShowWorldESP);
		tip("Grenade effect timers in the world", "世界中的手雷效果计时器");
		if (MenuConfig::ShowWorldESP) {
			Gui.MyCheckBox(lang.visuals_world_timers.c_str(), &MenuConfig::ShowWorldProjectileTimers);
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_world_color.c_str(), reinterpret_cast<float*>(&MenuConfig::WorldESPColor), ImGuiColorEditFlags_NoInputs);

			// Task 12: per-type grenade timer sub-switches
			Gui.MyCheckBox(lang.visuals_world_smoke_timer.c_str(), &MenuConfig::ShowWorldSmokeTimer);
			Gui.MyCheckBox(lang.visuals_world_inferno_timer.c_str(), &MenuConfig::ShowWorldInfernoTimer);
			Gui.MyCheckBox(lang.visuals_world_decoy_timer.c_str(), &MenuConfig::ShowWorldDecoyTimer);

			// Task 12/16: dropped-weapon world ESP + per-weapon-id filter.
			Gui.MyCheckBox(lang.visuals_world_items.c_str(), &MenuConfig::ShowWorldItems);
			tip("Dropped weapons on the ground", "地面掉落武器显示");
			if (MenuConfig::ShowWorldItems) {
				ImGui::SetNextItemWidth(150);
				ImGui::SliderFloat(lang.visuals_world_item_fontsize.c_str(), &MenuConfig::WorldItemFontSize, 8.f, 20.f, "%.0f px");

				if (ImGui::TreeNode(lang.visuals_item_filter.c_str())) {
					// Quick actions: enable / disable all known weapons.
					if (ImGui::Button(lang.visuals_item_filter_all.c_str())) {
						for (const auto& e : WeaponLookup::kWeaponLookup)
							MenuConfig::EspItemEnabledMask.set(e.id);
					}
					ImGui::SameLine();
					if (ImGui::Button(lang.visuals_item_filter_none.c_str())) {
						for (const auto& e : WeaponLookup::kWeaponLookup)
							MenuConfig::EspItemEnabledMask.reset(e.id);
					}

					// Group weapons by item-id lists (WeaponSlotKind::Primary
					// covers SMG/rifle/sniper/heavy, so we sub-classify here).
					auto drawGroup = [&](const char* title, const uint16_t* ids, size_t count) {
						if (!ImGui::TreeNode(title)) return;
						for (size_t i = 0; i < count; i++) {
							const WeaponLookup::WeaponLookupEntry* e =
								WeaponLookup::FindWeaponLookupEntry(ids[i]);
							if (!e) continue;
							bool enabled = MenuConfig::EspItemEnabledMask.test(e->id);
							ImGui::Checkbox(e->name, &enabled);
							MenuConfig::EspItemEnabledMask.set(e->id, enabled);
						}
						ImGui::TreePop();
					};
					static const uint16_t kPistols[]  = { 1, 2, 3, 4, 30, 32, 36, 61, 63, 64 };
					static const uint16_t kSmgs[]     = { 17, 19, 23, 24, 26, 33, 34 };
					static const uint16_t kRifles[]   = { 7, 8, 10, 13, 16, 39, 60 };
					static const uint16_t kSnipers[]  = { 9, 11, 38, 40 };
					static const uint16_t kHeavy[]    = { 14, 25, 27, 28, 29, 35 };
					static const uint16_t kGear[]     = { 31, 57 };
					drawGroup(lang.visuals_item_filter_pistols.c_str(),  kPistols,  sizeof(kPistols)/sizeof(kPistols[0]));
					drawGroup(lang.visuals_item_filter_smgs.c_str(),     kSmgs,     sizeof(kSmgs)/sizeof(kSmgs[0]));
					drawGroup(lang.visuals_item_filter_rifles.c_str(),   kRifles,   sizeof(kRifles)/sizeof(kRifles[0]));
					drawGroup(lang.visuals_item_filter_snipers.c_str(),  kSnipers,  sizeof(kSnipers)/sizeof(kSnipers[0]));
					drawGroup(lang.visuals_item_filter_heavy.c_str(),    kHeavy,    sizeof(kHeavy)/sizeof(kHeavy[0]));
					drawGroup(lang.visuals_item_filter_gear.c_str(),     kGear,     sizeof(kGear)/sizeof(kGear[0]));
					ImGui::TreePop();
				}
			}
		}
	}

	// ======== Crosshair (Task 11: moved from Fusion Tab) ========
	if (ImGui::CollapsingHeader(lang.header_crosshair.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.crosshair_enable.c_str(), &MenuConfig::CrosshairEnabled);
		if (MenuConfig::CrosshairEnabled) {
			ImGui::SetNextItemWidth(150);
			ImGui::Combo(lang.crosshair_style.c_str(), &MenuConfig::CrosshairStyle, lang.crosshair_styleselect, IM_ARRAYSIZE(lang.crosshair_styleselect));

			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.crosshair_size.c_str(), &MenuConfig::CrosshairSize, 1.f, 30.f, "%.0f px");
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.crosshair_thickness.c_str(), &MenuConfig::CrosshairThickness, 0.5f, 5.f, "%.1f");
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.crosshair_gap.c_str(), &MenuConfig::CrosshairGap, 0.f, 20.f, "%.0f px");

			ImGui::ColorEdit4(lang.crosshair_color.c_str(), reinterpret_cast<float*>(&MenuConfig::CrosshairColor), ImGuiColorEditFlags_NoInputs);

			Gui.MyCheckBox(lang.crosshair_onenemycolor.c_str(), &MenuConfig::CrosshairOnEnemyColor);
			if (MenuConfig::CrosshairOnEnemyColor) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.crosshair_enemycolor.c_str(), reinterpret_cast<float*>(&MenuConfig::CrosshairEnemyColor), ImGuiColorEditFlags_NoInputs);
			}
		}
	}

	// ======== Safe Zone (Task 11: moved from Fusion Tab) ========
	if (ImGui::CollapsingHeader(lang.header_safezone.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.safezone_enable.c_str(), &MenuConfig::SafeZoneEnabled);
		if (MenuConfig::SafeZoneEnabled) {
			ImGui::SetNextItemWidth(180);
			ImGui::SliderFloat(lang.safezone_radius.c_str(), &MenuConfig::SafeZoneRadius, 1.f, 300.f, "%.0f px");
			ImGui::SetNextItemWidth(120);
			ImGui::Combo(lang.safezone_shape.c_str(), &MenuConfig::SafeZoneShape, lang.safezone_shapeselect, IM_ARRAYSIZE(lang.safezone_shapeselect));
			ImGui::SetNextItemWidth(150);
			ImGui::Combo(lang.safezone_mode.c_str(), &MenuConfig::SafeZoneMode, lang.safezone_modeselect, IM_ARRAYSIZE(lang.safezone_modeselect));

			if (MenuConfig::SafeZoneMode == 1) {
				ImGui::Spacing();
				Gui.MyCheckBox(lang.safezone_skip_box.c_str(), &MenuConfig::SafeZoneSkipBox);
				Gui.MyCheckBox(lang.safezone_skip_bone.c_str(), &MenuConfig::SafeZoneSkipBone);
				Gui.MyCheckBox(lang.safezone_skip_healthbar.c_str(), &MenuConfig::SafeZoneSkipHealthBar);
				Gui.MyCheckBox(lang.safezone_skip_armorbar.c_str(), &MenuConfig::SafeZoneSkipArmorBar);
				Gui.MyCheckBox(lang.safezone_skip_weapon.c_str(), &MenuConfig::SafeZoneSkipWeapon);
				Gui.MyCheckBox(lang.safezone_skip_name.c_str(), &MenuConfig::SafeZoneSkipName);
				Gui.MyCheckBox(lang.safezone_skip_snapline.c_str(), &MenuConfig::SafeZoneSkipSnapline);
				Gui.MyCheckBox(lang.safezone_skip_eyeray.c_str(), &MenuConfig::SafeZoneSkipEyeRay);
				Gui.MyCheckBox(lang.safezone_skip_headdot.c_str(), &MenuConfig::SafeZoneSkipHeadDot);
				Gui.MyCheckBox(lang.safezone_skip_distance.c_str(), &MenuConfig::SafeZoneSkipDistance);
			}
		}
	}

	// ======== Team Filter (Task 12: moved from Settings Tab) ========
	if (ImGui::CollapsingHeader(lang.header_teamfilter.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.utilities_teamcheck.c_str(), &MenuConfig::TeamCheck);
	}

	// ======== Player Flags (Task 17: independent section from Advanced ESP) ========
	if (ImGui::CollapsingHeader(lang.visuals_player_flags.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_player_flags.c_str(), &MenuConfig::ShowPlayerFlags);
		tip("Player status flags (Blind/Scoped/Defusing/Kit/Money)", "玩家状态标签（Blind/Scoped/Defusing/Kit/Money）");
		if (MenuConfig::ShowPlayerFlags) {
			Gui.MyCheckBox(lang.visuals_flag_blind.c_str(), &MenuConfig::FlagBlindEnabled);
			if (MenuConfig::FlagBlindEnabled) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.visuals_flag_blind_color.c_str(), reinterpret_cast<float*>(&MenuConfig::FlagBlindColor), ImGuiColorEditFlags_NoInputs);
			}
			Gui.MyCheckBox(lang.visuals_flag_scoped.c_str(), &MenuConfig::FlagScopedEnabled);
			if (MenuConfig::FlagScopedEnabled) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.visuals_flag_scoped_color.c_str(), reinterpret_cast<float*>(&MenuConfig::FlagScopedColor), ImGuiColorEditFlags_NoInputs);
			}
			Gui.MyCheckBox(lang.visuals_flag_defusing.c_str(), &MenuConfig::FlagDefusingEnabled);
			if (MenuConfig::FlagDefusingEnabled) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.visuals_flag_defusing_color.c_str(), reinterpret_cast<float*>(&MenuConfig::FlagDefusingColor), ImGuiColorEditFlags_NoInputs);
			}
			Gui.MyCheckBox(lang.visuals_flag_kit.c_str(), &MenuConfig::FlagKitEnabled);
			if (MenuConfig::FlagKitEnabled) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.visuals_flag_kit_color.c_str(), reinterpret_cast<float*>(&MenuConfig::FlagKitColor), ImGuiColorEditFlags_NoInputs);
			}
			Gui.MyCheckBox(lang.visuals_flag_money.c_str(), &MenuConfig::FlagMoneyEnabled);
			if (MenuConfig::FlagMoneyEnabled) {
				ImGui::SameLine(0, 16);
				ImGui::ColorEdit4(lang.visuals_flag_money_color.c_str(), reinterpret_cast<float*>(&MenuConfig::FlagMoneyColor), ImGuiColorEditFlags_NoInputs);
			}
			ImGui::SetNextItemWidth(150);
			ImGui::SliderFloat(lang.visuals_flag_fontsize.c_str(), &MenuConfig::FlagFontSize, 8.f, 20.f, "%.0f px");
		}
	}

	// ======== Visibility Coloring (Task 17: independent section from Advanced ESP) ========
	if (ImGui::CollapsingHeader(lang.visuals_visibility.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_visibility.c_str(), &MenuConfig::VisibilityColoring);
		tip("Color by visibility (different color for enemies behind walls)", "根据可见性着色（墙后敌人用不同颜色）");
		if (MenuConfig::VisibilityColoring) {
			ImGui::ColorEdit4(lang.visuals_visible_color.c_str(), reinterpret_cast<float*>(&MenuConfig::VisibleColor), ImGuiColorEditFlags_NoInputs);
			ImGui::ColorEdit4(lang.visuals_hidden_color.c_str(), reinterpret_cast<float*>(&MenuConfig::HiddenColor), ImGuiColorEditFlags_NoInputs);
		}
	}

	// ======== Sound ESP (Task 17: independent section from Advanced ESP) ========
	if (ImGui::CollapsingHeader(lang.visuals_sound_esp.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_sound_esp.c_str(), &MenuConfig::ShowSoundESP);
		tip("Player firing sound waves", "玩家开火声波纹");
		if (MenuConfig::ShowSoundESP) {
			ImGui::SameLine(0, 16);
			ImGui::ColorEdit4(lang.visuals_sound_color.c_str(), reinterpret_cast<float*>(&MenuConfig::SoundESPColor), ImGuiColorEditFlags_NoInputs);
		}
	}

	// ======== ESP Preview (Task: preview window toggle) ========
	ImGui::Spacing();
	if (ImGui::Button("ESP Preview")) {
		Render::EspPreviewOpen = !Render::EspPreviewOpen;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Open a preview window showing current ESP settings");
}

// ============================================================================
// Tab 1: Radar
// ============================================================================
static void DrawTab_Radar() {
	// ---- Web Radar ----
	SectionHeader(lang.webradar_enable.c_str());

	Gui.MyCheckBox(lang.webradar_enable.c_str(), &MenuConfig::ShowWebRadar);

	if (MenuConfig::ShowWebRadar) {
		ImGui::Spacing();

		ImGui::SetNextItemWidth(120);
		ImGui::InputInt((lang.webradar_port + "##webradar").c_str(), &MenuConfig::WebRadarPort);
		if (MenuConfig::WebRadarPort < 1024) MenuConfig::WebRadarPort = 1024;
		if (MenuConfig::WebRadarPort > 65535) MenuConfig::WebRadarPort = 65535;

		ImGui::SetNextItemWidth(180);
		ImGui::SliderInt((lang.webradar_interval + "##webradar").c_str(), &MenuConfig::WebRadarInterval, 50, 500);

		Gui.MyCheckBox(lang.webradar_password_enable.c_str(), &MenuConfig::WebRadarPasswordEnabled);
		if (MenuConfig::WebRadarPasswordEnabled) {
			static char passwordBuf[64] = "";
			if (passwordBuf[0] == '\0' && !MenuConfig::WebRadarPassword.empty())
				strncpy_s(passwordBuf, MenuConfig::WebRadarPassword.c_str(), 63);
			ImGui::SetNextItemWidth(180);
			if (ImGui::InputText((lang.webradar_password + "##webradar").c_str(), passwordBuf, sizeof(passwordBuf))) {
				MenuConfig::WebRadarPassword = passwordBuf;
			}
		}

		// Task 10: Origin allowlist for /api/* CORS
		{
			static char originBuf[512] = "";
			if (originBuf[0] == '\0' && !MenuConfig::WebRadarOriginAllowlist.empty())
				strncpy_s(originBuf, MenuConfig::WebRadarOriginAllowlist.c_str(), 511);
			ImGui::SetNextItemWidth(280);
			if (ImGui::InputText("Origin Allowlist##webradar", originBuf, sizeof(originBuf))) {
				MenuConfig::WebRadarOriginAllowlist = originBuf;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Comma-separated origins for /api/* CORS. Empty = allow all.");
		}

		// ---- Server status ----
		ImGui::Spacing();
		bool running = g_webRadarRunning.load();
		int clients = g_webRadarClientCount.load();
		if (running) {
			ImGui::TextColored(UITheme::Success, "Online");
			ImGui::SameLine(0, 16);
			ImGui::Text("%s: %d", lang.webradar_clients.c_str(), clients);
		} else {
			ImGui::TextColored(UITheme::Danger, "%s", lang.webradar_not_running.c_str());
		}

		// ---- Task 9: Runtime stats debug panel ----
		if (running && ImGui::CollapsingHeader("Runtime Stats##webradar")) {
			RuntimeStatsSnapshot snap;
			{
				std::lock_guard<std::mutex> lock(g_webRadarStatsMutex);
				snap = g_webRadarStats;
			}

			// Compute send Hz by sampling framesSent over time
			static uint64_t prevFramesSent = 0;
			static auto prevTime = std::chrono::steady_clock::now();
			static double sendHz = 0.0;
			auto now = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(now - prevTime).count();
			if (elapsed >= 0.5) {
				uint64_t delta = snap.framesSent - prevFramesSent;
				sendHz = delta / elapsed;
				prevFramesSent = snap.framesSent;
				prevTime = now;
			}

			uint64_t avgSerUs = snap.serializeUsCount > 0 ? snap.serializeUsTotal / snap.serializeUsCount : 0;
			uint64_t avgBcUs  = snap.broadcastUsCount > 0 ? snap.broadcastUsTotal / snap.broadcastUsCount : 0;

			ImGui::Text("Send Hz:        %.1f", sendHz);
			ImGui::Text("Clients:        %d", snap.clientCount);
			ImGui::Text("Frames sent:    %llu", (unsigned long long)snap.framesSent);
			ImGui::Text("Frames dropped: %llu", (unsigned long long)snap.framesDropped);
			ImGui::Text("Coalesced:      %llu", (unsigned long long)snap.coalescedFrames);
			ImGui::Text("Slow client drops: %llu", (unsigned long long)snap.droppedFramesSlowClient);
			ImGui::Text("Serialize avg:  %llu us", (unsigned long long)avgSerUs);
			ImGui::Text("Broadcast avg:  %llu us", (unsigned long long)avgBcUs);
			ImGui::Text("Payload last:   %zu bytes", snap.payloadBytesLast);
		ImGui::Text("Payload peak:   %zu bytes", snap.payloadBytesPeak);
		// Task 17: derived runtime stats from the backend.
		ImGui::Text("Bytes/sec:      %llu", (unsigned long long)snap.bytesOutPerSec);
		ImGui::Text("Active map:     %s", snap.activeMap.empty() ? "-" : snap.activeMap.c_str());
		ImGui::Text("Status:         %s", snap.statusText.empty() ? "-" : snap.statusText.c_str());
	}

		// ---- Reload & Auto Reload ----
	ImGui::Spacing();
	if (ImGui::Button("Reload WebRadar Clients")) {
		g_webRadarReloadRequested.store(true);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Push a pageUpdate control message to all connected clients");
	ImGui::SameLine(0, 16);
	ImGui::Checkbox("Auto Reload", &MenuConfig::WebRadarAutoReload);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Automatically push pageUpdate to clients when resources update");

		// ---- Local / LAN Access ----
		SectionHeader(lang.webradar_local_access.c_str());

		// Build local access URL
		char localUrl[256];
		snprintf(localUrl, sizeof(localUrl), "http://localhost:%d", MenuConfig::WebRadarPort);
		ImGui::TextColored(UITheme::Info, "%s", localUrl);
		ImGui::SameLine(0, 8);
		if (ImGui::SmallButton((lang.webradar_copy_url + "##local").c_str())) {
			ImGui::SetClipboardText(localUrl);
		}

		// Build LAN access URL with local IP
		char lanUrl[256] = {};
		std::string localIP = GetLocalIP();
		if (!localIP.empty()) {
			snprintf(lanUrl, sizeof(lanUrl), "http://%s:%d", localIP.c_str(), MenuConfig::WebRadarPort);
		}
		if (lanUrl[0]) {
			ImGui::TextColored(UITheme::Success, "%s", lanUrl);
			ImGui::SameLine(0, 8);
			if (ImGui::SmallButton((lang.webradar_copy_url + "##lan").c_str())) {
				ImGui::SetClipboardText(lanUrl);
			}
		}

		// ---- QR Code (scan to open on mobile) ----
		// Prefer LAN URL for mobile scanning; fall back to localhost if LAN IP unavailable.
		const char* qrUrl = lanUrl[0] ? lanUrl : localUrl;
		if (qrUrl[0] && ImGui::CollapsingHeader("QR Code##webradar")) {
			// Center the QR code within the available content width
			const float qrSize = 180.0f;
			float avail = ImGui::GetContentRegionAvail().x;
			if (avail > qrSize) {
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - qrSize) * 0.5f);
			}
			DrawQRCode(qrUrl, qrSize);
			ImGui::Spacing();
			ImGui::TextDisabled("Scan with phone camera to open");
		}

		// ---- Cloudflare Tunnel (Public Access) ----
		SectionHeader(lang.webradar_tunnel_section.c_str());

		// Cache cloudflared installation check (refresh every 5s)
		static bool s_cfInstalled = true;
		static auto s_lastCfCheck = std::chrono::steady_clock::now() - std::chrono::hours(1);
		auto cfNow = std::chrono::steady_clock::now();
		if (cfNow - s_lastCfCheck > std::chrono::seconds(5)) {
			s_cfInstalled = IsCloudflaredInstalled();
			s_lastCfCheck = cfNow;
		}

		if (!s_cfInstalled) {
			ImGui::TextColored(UITheme::Warning, "%s", lang.webradar_tunnel_not_installed.c_str());
			ImGui::TextDisabled("winget install Cloudflare.cloudflared");
		} else {
			bool prev = MenuConfig::WebRadarCloudflareTunnel;
			Gui.MyCheckBox(lang.webradar_tunnel_enable.c_str(), &MenuConfig::WebRadarCloudflareTunnel);
			if (MenuConfig::WebRadarCloudflareTunnel != prev) {
				if (MenuConfig::WebRadarCloudflareTunnel) {
					// User enabled — start tunnel
					if (!StartCloudflareTunnel(MenuConfig::WebRadarPort)) {
						MenuConfig::WebRadarCloudflareTunnel = false;
					}
				} else {
					// User disabled — stop tunnel
					StopCloudflareTunnel();
				}
			}

			if (MenuConfig::WebRadarCloudflareTunnel) {
				std::string url = GetCloudflareTunnelURL();
				if (!url.empty()) {
					ImGui::TextColored(UITheme::Success, "%s", url.c_str());
					ImGui::SameLine(0, 8);
					if (ImGui::SmallButton((lang.webradar_copy_url + "##cf").c_str())) {
						ImGui::SetClipboardText(url.c_str());
					}
				} else {
					ImGui::TextColored(UITheme::Warning, "%s", lang.webradar_tunnel_starting.c_str());
				}
			}
		}

		// ---- Task 19.4: Radar Calibration Panel ----
		if (ImGui::CollapsingHeader("Radar Calibration##webradar")) {
			// Static state persists across frames (GUI thread is single-threaded)
			static std::string s_calibMapName;
			static radar::RadarCalibrationRecord s_calibRecord;
			static bool s_calibDirty = false;
			static auto s_lastChangeTime = std::chrono::steady_clock::now();
			static bool s_initialized = false;

			// Read current map name from the game snapshot
			std::string currentMap;
			{
				const auto& snap = Cheats::GetSnapshot();
				currentMap = snap.MapName;
			}
			// Clean and resolve via registry
			std::string displayMapName;
			if (!currentMap.empty()) {
				// Strip path/extension the same way CleanMapName does
				std::string cleaned = currentMap;
				if (cleaned.find("maps/") == 0) cleaned = cleaned.substr(5);
				auto pos = cleaned.find(".vpk");
				if (pos != std::string::npos) cleaned = cleaned.substr(0, pos);
				pos = cleaned.find(".bsp");
				if (pos != std::string::npos) cleaned = cleaned.substr(0, pos);

				if (const radar::MapDefinition* known = radar::FindMapByName(cleaned)) {
					displayMapName = known->name;
				} else if (cleaned.empty() || cleaned.find("<empty>") != std::string::npos) {
					displayMapName = "(no map)";
				} else {
					displayMapName = "dynamic_" + cleaned;
				}
			} else {
				displayMapName = "(no map)";
			}

			ImGui::Text("Map: %s", displayMapName.c_str());

			// Load calibration when map changes
			if (displayMapName != s_calibMapName && displayMapName != "(no map)") {
				radar::RadarCalibrationRecord loaded{};
				if (radar::LoadRadarCalibrationForMap(displayMapName, loaded)) {
					s_calibRecord = loaded;
				} else {
					s_calibRecord = radar::RadarCalibrationRecord{};
				}
				s_calibMapName = displayMapName;
				s_calibDirty = false;
				s_initialized = true;
				SetRadarCalibration(s_calibMapName, s_calibRecord);
			}

			if (s_initialized && s_calibMapName != "(no map)") {
				ImGui::Spacing();

				bool changed = false;
				ImGui::SetNextItemWidth(200);
				changed |= ImGui::SliderFloat("Rotation##calib", &s_calibRecord.rotationDeg, -180.0f, 180.0f, "%.1f deg");

				ImGui::SetNextItemWidth(200);
				changed |= ImGui::SliderFloat("Scale##calib", &s_calibRecord.scale, 0.5f, 1.5f, "%.3f");

				ImGui::SetNextItemWidth(200);
				changed |= ImGui::SliderFloat("Offset X##calib", &s_calibRecord.offsetX, -0.25f, 0.25f, "%.4f");

				ImGui::SetNextItemWidth(200);
				changed |= ImGui::SliderFloat("Offset Y##calib", &s_calibRecord.offsetY, -0.25f, 0.25f, "%.4f");

				if (changed) {
					s_calibDirty = true;
					s_lastChangeTime = std::chrono::steady_clock::now();
					SetRadarCalibration(s_calibMapName, s_calibRecord);
				}

				// Auto-save after 1200ms debounce
				if (s_calibDirty) {
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - s_lastChangeTime).count();
					if (elapsed >= 1200) {
						radar::SaveRadarCalibrationForMap(s_calibMapName, s_calibRecord);
						s_calibDirty = false;
					} else {
						ImGui::SameLine();
						ImGui::TextDisabled("(saving...)");
					}
				}

				ImGui::Spacing();
				if (ImGui::SmallButton("Save##calib")) {
					radar::SaveRadarCalibrationForMap(s_calibMapName, s_calibRecord);
					s_calibDirty = false;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset##calib")) {
					s_calibRecord = radar::RadarCalibrationRecord{};
					s_calibDirty = true;
					s_lastChangeTime = std::chrono::steady_clock::now();
					SetRadarCalibration(s_calibMapName, s_calibRecord);
				}
			}
		}
	}

}

// ============================================================================
// Tab 2: Settings
// ============================================================================
static void DrawTab_Settings() {
	// Task 22: tooltip helper - shows "(?)" indicator with bilingual tooltip
	auto tip = [](const char* en, const char* zh) {
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip(MenuConfig::SelectedLanguage == 0 ? en : zh);
	};

	if (ImGui::CollapsingHeader(lang.header_general.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
		Gui.MyCheckBox(lang.settings_vsync.c_str(), &MenuConfig::VSync);
		if (!MenuConfig::VSync) {
			ImGui::SetNextItemWidth(200);
			ImGui::SliderInt(lang.settings_maxfps.c_str(), &MenuConfig::MaxFrameRate, 0, 500, MenuConfig::MaxFrameRate == 0 ? lang.settings_unlimited.c_str() : "%d");
			if (MenuConfig::MaxFrameRate < 0) MenuConfig::MaxFrameRate = 0;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.settings_unlimitedtip.c_str());
		}
	}

	// ======== Render Quality (Task 16: moved from Advanced ESP) ========
	if (ImGui::CollapsingHeader(lang.header_render_quality.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.visuals_interpolation.c_str(), &MenuConfig::InterpolationEnabled);
		tip("Player position interpolation smoothing", "玩家位置插值平滑");
		Gui.MyCheckBox(lang.visuals_bone_reliability.c_str(), &MenuConfig::BoneReliabilityEnabled);
		tip("Bone data reliability check", "骨骼数据可靠性检查");
	}

	if (ImGui::CollapsingHeader(lang.header_display.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		// Monitor selection
		if (!MenuConfig::MonitorList.empty()) {
			ImGui::SetNextItemWidth(220);
			const auto& monitors = MenuConfig::MonitorList;
			int curIdx = (MenuConfig::MonitorIndex >= 0 && MenuConfig::MonitorIndex < (int)monitors.size()) ? MenuConfig::MonitorIndex : 0;
			if (ImGui::BeginCombo(lang.settings_monitor.c_str(), monitors[curIdx].name.c_str())) {
				for (int i = 0; i < (int)monitors.size(); i++) {
					bool selected = (i == MenuConfig::MonitorIndex);
					if (ImGui::Selectable(monitors[i].name.c_str(), selected))
						MenuConfig::MonitorIndex = i;
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.settings_monitortip.c_str());
			ImGui::SameLine(0, 8);
			ImGui::TextDisabled("(%s)", lang.settings_restarttip.c_str());
		}

		// Resolution selection (common 4:3 and 16:9 presets)
		{
			struct ResPreset { int w, h; const char* label; };
			static const ResPreset resPresets[] = {
				{0, 0, "Auto"},
				// 4:3
				{1024, 768, "1024x768 (4:3)"},
				{1280, 960, "1280x960 (4:3)"},
				{1440, 1080, "1440x1080 (4:3)"},
				{1600, 1200, "1600x1200 (4:3)"},
				// 16:9
				{1280, 720, "1280x720 (16:9)"},
				{1366, 768, "1366x768 (16:9)"},
				{1600, 900, "1600x900 (16:9)"},
				{1920, 1080, "1920x1080 (16:9)"},
				{2560, 1440, "2560x1440 (16:9)"},
				{3840, 2160, "3840x2160 (16:9)"},
				// 16:10
				{1440, 900, "1440x900 (16:10)"},
				{1680, 1050, "1680x1050 (16:10)"},
				{1920, 1200, "1920x1200 (16:10)"},
			};
			constexpr int resCount = sizeof(resPresets) / sizeof(resPresets[0]);

			// Find current selection
			int curRes = 0;
			for (int i = 1; i < resCount; i++) {
				if (MenuConfig::RenderWidth == resPresets[i].w && MenuConfig::RenderHeight == resPresets[i].h) {
					curRes = i;
					break;
				}
			}
			// If custom value not in list, show as "Custom (WxH)"
			bool isCustom = (curRes == 0 && (MenuConfig::RenderWidth != 0 || MenuConfig::RenderHeight != 0));
			char customLabel[64] = {};
			if (isCustom)
				_snprintf_s(customLabel, _TRUNCATE, "Custom (%dx%d)", MenuConfig::RenderWidth, MenuConfig::RenderHeight);

			ImGui::SetNextItemWidth(220);
			if (ImGui::BeginCombo(lang.settings_resolution.c_str(), isCustom ? customLabel : resPresets[curRes].label)) {
				for (int i = 0; i < resCount; i++) {
					bool selected = (i == curRes && !isCustom);
					if (ImGui::Selectable(resPresets[i].label, selected)) {
						MenuConfig::RenderWidth = resPresets[i].w;
						MenuConfig::RenderHeight = resPresets[i].h;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.settings_renderautotip.c_str());
			ImGui::SameLine(0, 8);
			ImGui::TextDisabled("(%s)", lang.settings_restarttip.c_str());
		}
	}

	if (ImGui::CollapsingHeader(lang.header_system.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		// Menu hotkey
		ImGui::Text("%s:", lang.settings_menuhotkey.c_str());
		ImGui::SameLine();
		if (MenuConfig::IsListeningForMenuKey) {
			ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg90);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::Danger);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::DangerBg80);
		ImGui::Button(lang.grenade_pressanykey.c_str(), ImVec2(130, 0));
		ImGui::PopStyleColor(3);
	} else {
		if (ImGui::Button(MenuConfig::MenuHotKeyName, ImVec2(130, 0))) {
			MenuConfig::IsListeningForMenuKey = true;
		}
	}
	ImGui::SameLine();
	ImGui::TextColored(UITheme::Warning, "%s", lang.grenade_hotkeytip.c_str());

		ImGui::Spacing();
		Gui.MyCheckBox(lang.settings_perfmonitor.c_str(), &MenuConfig::ShowPerfMonitor);
		if (MenuConfig::ShowPerfMonitor) {
			ImGui::SameLine(0, 16);
			ImGui::Checkbox("Debug Stats", &MenuConfig::ShowDebugStats);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Show detailed stage timings and DMA health stats in the PerfMonitor overlay");
		}

		ImGui::Spacing();
		{
			bool prev = MenuConfig::DebugLog;
			Gui.MyCheckBox(lang.settings_debuglog.c_str(), &MenuConfig::DebugLog);
			if (MenuConfig::DebugLog != prev)
				Logger::SetDebugMode(MenuConfig::DebugLog);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.settings_debuglog_tip.c_str());

		// --- Offset refetch section ---
		ImGui::Spacing();
		ImGui::Separator();
		// Show local offset version + date
		if (!Offset::LocalPatchVersion.empty()) {
			ImGui::TextColored(UITheme::TextMuted, lang.offset_local_version.c_str(), Offset::LocalPatchVersion.c_str(), Offset::GameUpdateDate.c_str());
		} else if (!Offset::GameUpdateDate.empty()) {
			ImGui::TextColored(UITheme::TextMuted, lang.offset_current_date.c_str(), Offset::GameUpdateDate.c_str());
		}
		// Show latest game version + match status
		if (!Offset::LatestPatchVersion.empty()) {
			if (Offset::VersionMismatch) {
				ImGui::TextColored(UITheme::Danger, lang.offset_latest_mismatch.c_str(), Offset::LatestPatchVersion.c_str());
			} else {
				ImGui::TextColored(UITheme::Success, lang.offset_latest_match.c_str(), Offset::LatestPatchVersion.c_str());
			}
		}
		if (ImGui::Button(lang.offset_refetch_button.c_str(), ImVec2(200, 28))) {
			if (!s_offsetUpdateRunning.load()) {
				ImGui::OpenPopup("##OffsetGuide");
			}
		}

		// Guide dialog
		if (ImGui::BeginPopupModal("##OffsetGuide", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("%s", lang.offset_guide_title.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("%s", lang.offset_guide_body.c_str());
			ImGui::Spacing();
			if (ImGui::Button(lang.offset_guide_confirm.c_str(), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				s_offsetUpdateRunning.store(true);
				s_offsetUpdateDone.store(false);
				std::thread([]() {
					bool ok = RunOffsetUpdateWithDma();
					s_offsetUpdateSuccess.store(ok);
					s_offsetUpdateDone.store(true);
					s_offsetUpdateRunning.store(false);
				}).detach();
			}
			ImGui::SameLine();
			if (ImGui::Button(lang.offset_guide_cancel.c_str(), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Status display (while running)
		if (s_offsetUpdateRunning.load()) {
			ImGui::TextColored(UITheme::Warning, "%s", lang.offset_status_running.c_str());
		}

		// Check if update just finished
		if (s_offsetUpdateDone.load()) {
			if (s_offsetUpdateSuccess.load()) {
				s_triggerSuccessPopup = true;
			} else {
				s_triggerFailPopup = true;
			}
			s_offsetUpdateDone.store(false);
		}

		// Trigger result popups
		if (s_triggerSuccessPopup) {
			ImGui::OpenPopup("##OffsetSuccess");
			s_triggerSuccessPopup = false;
		}
		if (s_triggerFailPopup) {
			ImGui::OpenPopup("##OffsetFail");
			s_triggerFailPopup = false;
		}

		// Success dialog
		if (ImGui::BeginPopupModal("##OffsetSuccess", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("%s", lang.offset_result_success_title.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("%s", lang.offset_result_success_body.c_str());
			ImGui::Spacing();
			if (ImGui::Button(lang.offset_result_restart.c_str(), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				RestartSelf();
			}
			ImGui::EndPopup();
		}

		// Failure dialog
		if (ImGui::BeginPopupModal("##OffsetFail", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("%s", lang.offset_result_fail_title.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("%s", lang.offset_result_fail_body.c_str());
			ImGui::Spacing();
			if (ImGui::Button(lang.offset_result_fail_ok.c_str(), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				ReattachDma();
			}
			ImGui::EndPopup();
		}
		ImGui::Separator();
		// --- End offset refetch section ---

		ImGui::Spacing();
		if (ImGui::Button(lang.utilities_reloadhack.c_str(), ImVec2(200, 28))) {
			ProcessMgr.Detach();
			globalVars::gameState.store(AppState::SEARCHING_GAME);
		}

		ImGui::Spacing();
		if (ImGui::Button(lang.utilities_help.c_str(), ImVec2(160, 28))) {
			ShellExecuteA(nullptr, "open", "https://github.com/chao-shushu/CS2-DMA", nullptr, nullptr, SW_SHOWNORMAL);
			TerminateProcess(GetCurrentProcess(), 0);
		}

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg80);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::DangerBg90);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::Danger);
		if (ImGui::Button(lang.utilities_closehack.c_str(), ImVec2(160, 28))) {
			TerminateProcess(GetCurrentProcess(), 0);
		}
		ImGui::PopStyleColor(3);
	}
}

// ============================================================================
// Tab 6: Hotkeys
// ============================================================================
static bool IsKeyDownBoth(int vk) {
	// Check both DMA (remote machine) and local (GetAsyncKeyState)
	if (ProcessMgr.is_key_down(vk)) return true;
	if (GetAsyncKeyState(vk) & 0x8000) return true;
	return false;
}

static void HotkeyButton(int actionIndex) {
	auto& hk = MenuConfig::Hotkeys[actionIndex];
	ImGui::PushID(actionIndex);

	if (hk.isListening) {
		ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg90);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::Danger);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::DangerBg80);
		if (ImGui::Button(lang.grenade_pressanykey.c_str(), ImVec2(130, 0))) {
			hk.isListening = false;
		}
		ImGui::PopStyleColor(3);

		// Key listening — detect both DMA and local keys
		for (int vk = 0x08; vk <= 0xFE; vk++) {
			if (vk >= 0x01 && vk <= 0x06) continue;
			if (IsKeyDownBoth(vk)) {
				hk.vkCode = vk;
				strcpy_s(hk.keyName, GrenadeHelper::GetKeyName(vk));
				hk.isListening = false;
				MyConfigSaver::MarkDirty();
				break;
			}
		}
		if (IsKeyDownBoth(VK_XBUTTON1)) { hk.vkCode = VK_XBUTTON1; strcpy_s(hk.keyName, GrenadeHelper::GetKeyName(VK_XBUTTON1)); hk.isListening = false; MyConfigSaver::MarkDirty(); }
		else if (IsKeyDownBoth(VK_XBUTTON2)) { hk.vkCode = VK_XBUTTON2; strcpy_s(hk.keyName, GrenadeHelper::GetKeyName(VK_XBUTTON2)); hk.isListening = false; MyConfigSaver::MarkDirty(); }
		else if (IsKeyDownBoth(VK_MBUTTON)) { hk.vkCode = VK_MBUTTON; strcpy_s(hk.keyName, GrenadeHelper::GetKeyName(VK_MBUTTON)); hk.isListening = false; MyConfigSaver::MarkDirty(); }
		if (IsKeyDownBoth(VK_ESCAPE)) hk.isListening = false;
	} else {
		const char* label = hk.vkCode ? hk.keyName : lang.hotkey_none.c_str();
		if (ImGui::Button(label, ImVec2(130, 0))) {
			// Cancel any other listening
			for (int i = 0; i < MenuConfig::HOTKEY_COUNT; i++)
				MenuConfig::Hotkeys[i].isListening = false;
			hk.isListening = true;
		}
		if (hk.vkCode && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", lang.hotkey_cleartip.c_str());
		}
		if (hk.vkCode && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			hk.vkCode = 0;
			strcpy_s(hk.keyName, "None");
			MyConfigSaver::MarkDirty();
		}
	}

	ImGui::PopID();
}

static void DrawTab_Hotkeys() {
	// ESP Toggles
	if (ImGui::CollapsingHeader(lang.hotkey_header_esp.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		for (int i = 0; i <= 9; i++) {
			ImGui::Text("%s:", lang.hotkey_action_labels[i]);
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130);
			HotkeyButton(i);
		}
	}

	// Feature Toggles
	if (ImGui::CollapsingHeader(lang.hotkey_header_features.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		for (int i = 10; i <= 14; i++) {
			ImGui::Text("%s:", lang.hotkey_action_labels[i]);
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130);
			HotkeyButton(i);
		}
	}

	// Actions
	if (ImGui::CollapsingHeader(lang.hotkey_header_actions.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("%s:", lang.hotkey_action_labels[15]);
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130);
		HotkeyButton(15);
	}

	ImGui::Spacing();
	ImGui::TextColored(UITheme::Warning, "%s", lang.grenade_hotkeytip.c_str());
}

// ============================================================================
// Tab 3: Config
// ============================================================================
static void DrawTab_Config() {
	ConfigMenu::RenderConfigMenu();
}

// ============================================================================
// Tab 4: Grenade Helper
// ============================================================================
static void DrawTab_Grenade() {
	// --- Display Settings ---
	if (ImGui::CollapsingHeader(lang.header_display.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		Gui.MyCheckBox(lang.grenade_enable.c_str(), &GrenadeHelper::Enabled);

		if (GrenadeHelper::Enabled) {
			Gui.MyCheckBox(lang.grenade_showname.c_str(), &GrenadeHelper::ShowName);
			ImGui::SameLine(0, 16);
			Gui.MyCheckBox(lang.grenade_showbox.c_str(), &GrenadeHelper::ShowBox);
			ImGui::SameLine(0, 16);
			Gui.MyCheckBox(lang.grenade_showline.c_str(), &GrenadeHelper::ShowLine);

			ImGui::Spacing();
			ImGui::SetNextItemWidth(200);
			ImGui::SliderFloat(lang.grenade_maxdistance.c_str(), &GrenadeHelper::MaxDistance, 100.0f, 1000.0f, "%.0f");
			ImGui::SetNextItemWidth(200);
			ImGui::SliderFloat(lang.grenade_boxsize.c_str(), &GrenadeHelper::BoxSize, 4.0f, 20.0f, "%.0f");
		}

		ImGui::Spacing();
		ImGui::Text("%s: %s", lang.grenade_currentmap.c_str(), GrenadeHelper::CurrentMap.c_str());
		if (GrenadeHelper::CurrentThrows) {
			ImGui::SameLine(0, 16);
			ImGui::Text("%s: %d", lang.grenade_availablethrows.c_str(), (int)GrenadeHelper::CurrentThrows->size());
		} else {
			ImGui::TextColored(UITheme::Danger, "%s", lang.grenade_nomapdata.c_str());
		}

		// Legend
		ImGui::Spacing();
		ImGui::TextColored(ImColor(255, 255, 0), "%s", lang.grenade_flash.c_str());
		ImGui::SameLine(0, 8);
		ImGui::TextColored(ImColor(128, 128, 128), "%s", lang.grenade_smoke.c_str());
		ImGui::SameLine(0, 8);
		ImGui::TextColored(ImColor(255, 128, 0), "%s", lang.grenade_he.c_str());
		ImGui::SameLine(0, 8);
		ImGui::TextColored(ImColor(255, 64, 0), "%s", lang.grenade_molotov.c_str());
	}

	// --- Recording Settings ---
	if (ImGui::CollapsingHeader(lang.header_recording.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		// Record hotkey
		ImGui::Text("%s:", lang.grenade_record_hotkey.c_str());
		ImGui::SameLine();
		if (GrenadeHelper::IsListeningForKey) {
			ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg90);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::Danger);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::DangerBg80);
			if (ImGui::Button(lang.grenade_pressanykey.c_str(), ImVec2(130, 0))) {
				GrenadeHelper::IsListeningForKey = false;
			}
			ImGui::PopStyleColor(3);
		} else {
			if (ImGui::Button(GrenadeHelper::RecordHotKeyName, ImVec2(130, 0))) {
				GrenadeHelper::IsListeningForKey = true;
			}
		}

		// Key listening logic (reads host machine keys via DMA)
		if (GrenadeHelper::IsListeningForKey) {
			for (int vk = 0x08; vk <= 0xFE; vk++) {
				if (vk >= 0x01 && vk <= 0x06) continue;
				if (ProcessMgr.is_key_down(vk)) {
					GrenadeHelper::SetCustomHotKey(vk);
					break;
				}
			}
			if (ProcessMgr.is_key_down(VK_XBUTTON1)) GrenadeHelper::SetCustomHotKey(VK_XBUTTON1);
			else if (ProcessMgr.is_key_down(VK_XBUTTON2)) GrenadeHelper::SetCustomHotKey(VK_XBUTTON2);
			else if (ProcessMgr.is_key_down(VK_MBUTTON)) GrenadeHelper::SetCustomHotKey(VK_MBUTTON);
			if (ProcessMgr.is_key_down(VK_ESCAPE)) GrenadeHelper::IsListeningForKey = false;
		}

		ImGui::TextColored(UITheme::Warning, "%s", lang.grenade_hotkeytip.c_str());

		ImGui::Spacing();
		ImGui::Checkbox(lang.grenade_autosave.c_str(), &GrenadeHelper::AutoSave);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.grenade_autosavetip.c_str());

		ImGui::SetNextItemWidth(150);
		ImGui::Combo(lang.grenade_defaulttype.c_str(), &GrenadeHelper::DefaultGrenadeType, lang.grenade_typeselect, IM_ARRAYSIZE(lang.grenade_typeselect));
		ImGui::SetNextItemWidth(150);
		ImGui::Combo(lang.grenade_defaultstyle.c_str(), &GrenadeHelper::DefaultThrowStyle, lang.grenade_styleselect, IM_ARRAYSIZE(lang.grenade_styleselect));
	}

	// --- Pending Throws ---
	if (ImGui::CollapsingHeader(lang.header_pending.c_str())) {
		ImGui::Text("%s: %d", lang.grenade_pending_throws.c_str(), (int)GrenadeHelper::PendingThrows.size());

		if (GrenadeHelper::PendingThrows.empty()) {
			ImGui::TextColored(UITheme::TextSecondary, "%s", lang.grenade_no_pending.c_str());
		} else {
			static int selectedThrow = -1;
			static char throwName[128] = "";
			static int selectedStyle = 0;
			static int selectedType = 0;

			ImGui::BeginChild("PendingThrowsList", ImVec2(0, 130), true);
			for (int i = 0; i < (int)GrenadeHelper::PendingThrows.size(); i++) {
				const auto& pt = GrenadeHelper::PendingThrows[i];
				ImGui::PushID(i);
				bool isSelected = (selectedThrow == i);
				char label[256];
				sprintf_s(label, "#%d [%s] P:%.0f Y:%.0f (%s)",
					i + 1, GrenadeHelper::GetGrenadeTypeName(pt.Type),
					pt.Pitch, pt.Yaw, pt.Timestamp.c_str());
				if (ImGui::Selectable(label, isSelected)) {
					selectedThrow = i;
					strcpy_s(throwName, pt.Name.c_str());
					selectedStyle = pt.Style;
					selectedType = pt.Type;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text("%s: (%.1f, %.1f, %.1f)", lang.grenade_position.c_str(), pt.Position.x, pt.Position.y, pt.Position.z);
					ImGui::Text("%s: P=%.1f Y=%.1f", lang.grenade_angle.c_str(), pt.Pitch, pt.Yaw);
					ImGui::Text("%s: %s", lang.grenade_throw_type.c_str(), GrenadeHelper::GetGrenadeTypeName(pt.Type));
					ImGui::Text("%s: %s", lang.grenade_recorded_at.c_str(), pt.Timestamp.c_str());
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();

			// Edit selected
			ImGui::Separator();
			ImGui::SetNextItemWidth(200);
			ImGui::InputText(lang.grenade_throw_name.c_str(), throwName, sizeof(throwName));
			ImGui::SetNextItemWidth(150);
			ImGui::Combo(lang.grenade_throw_style.c_str(), &selectedStyle, lang.grenade_styleselect, IM_ARRAYSIZE(lang.grenade_styleselect));
			ImGui::SetNextItemWidth(150);
			ImGui::Combo(lang.grenade_throw_type.c_str(), &selectedType, lang.grenade_typeselect, IM_ARRAYSIZE(lang.grenade_typeselect));

			// Buttons row
			if (ImGui::Button(lang.grenade_name_throw.c_str(), ImVec2(90, 25))) {
				if (selectedThrow >= 0 && strlen(throwName) > 0) {
					GrenadeHelper::NamePendingThrow(selectedThrow, throwName, selectedStyle, selectedType);
					selectedThrow = -1;
					throwName[0] = '\0';
				}
			}
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg80);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::DangerBg90);
			if (ImGui::Button(lang.grenade_delete.c_str(), ImVec2(70, 25))) {
				if (selectedThrow >= 0) {
					GrenadeHelper::DeletePendingThrow(selectedThrow);
					if (selectedThrow >= (int)GrenadeHelper::PendingThrows.size())
						selectedThrow = (int)GrenadeHelper::PendingThrows.size() - 1;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(lang.grenade_clear_all.c_str(), ImVec2(70, 25))) {
				GrenadeHelper::ClearPendingThrows();
				selectedThrow = -1;
			}
			ImGui::PopStyleColor(2);

			// Save
			ImGui::Spacing();
			if (ImGui::Button(lang.grenade_save_throws.c_str(), ImVec2(140, 28))) {
				if (!GrenadeHelper::CurrentMap.empty()) {
					GrenadeHelper::SaveToFile(GrenadeHelper::CurrentMap);
				}
			}
			ImGui::SameLine();
			ImGui::TextColored(UITheme::TextSecondary, "-> %s.json", GrenadeHelper::CurrentMap.c_str());
		}
	}

	// --- Saved Throws Editor ---
	if (ImGui::CollapsingHeader(lang.header_savededitor.c_str())) {
		if (ImGui::Button(lang.grenade_reloadfiles.c_str(), ImVec2(100, 25))) {
			GrenadeHelper::LoadMapData("GrenadeHelper");
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang.grenade_reloadtip.c_str());

		ImGui::Spacing();

		static int selectedMapIndex = 0;
		auto availableMaps = GrenadeHelper::GetAvailableMaps();
		if (!availableMaps.empty()) {
			static std::string mapNamesStr;
			mapNamesStr.clear();
			for (size_t i = 0; i < availableMaps.size(); i++) {
				mapNamesStr += availableMaps[i] + '\0';
			}
			ImGui::SetNextItemWidth(200);
			ImGui::Combo(lang.grenade_selectmap.c_str(), &selectedMapIndex, mapNamesStr.c_str(), (int)availableMaps.size());

			std::string selectedMap = availableMaps[selectedMapIndex];

			if (GrenadeHelper::MapsData.find(selectedMap) != GrenadeHelper::MapsData.end()) {
				auto& throws = GrenadeHelper::MapsData[selectedMap].Throws;
				ImGui::Text("%s: %d", lang.grenade_totalthrows.c_str(), (int)throws.size());

				static int selectedSavedThrow = -1;
				static char editName[128] = "";
				static int editStyle = 0;
				static int editType = 0;
				static float editPosX = 0, editPosY = 0, editPosZ = 0;
				static float editPitch = 0, editYaw = 0;

				ImGui::BeginChild("SavedThrowsList", ImVec2(0, 150), true);
				for (int i = 0; i < (int)throws.size(); i++) {
					const auto& t = throws[i];
					ImGui::PushID(i);
					ImColor typeColor = GrenadeHelper::GetGrenadeColor(t.Type);
					bool isSelected = (selectedSavedThrow == i);
					char label[256];
					sprintf_s(label, "#%d [%s] %s", i + 1, GrenadeHelper::GetGrenadeTypeName(t.Type), t.Name.c_str());
					ImGui::PushStyleColor(ImGuiCol_Text, typeColor.Value);
					if (ImGui::Selectable(label, isSelected)) {
						selectedSavedThrow = i;
						strcpy_s(editName, t.Name.c_str());
						editStyle = t.Style;
						editType = t.Type;
						editPosX = t.Position.x;
						editPosY = t.Position.y;
						editPosZ = t.Position.z;
						editPitch = t.Pitch;
						editYaw = t.Yaw;
					}
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::BeginTooltip();
						ImGui::Text("%s: %s", lang.grenade_name_label.c_str(), t.Name.c_str());
						ImGui::Text("%s: %s", lang.grenade_throw_style.c_str(), GrenadeHelper::GetStyleName(t.Style));
						ImGui::Text("%s: (%.1f, %.1f, %.1f)", lang.grenade_position.c_str(), t.Position.x, t.Position.y, t.Position.z);
						ImGui::Text("%s: P=%.1f Y=%.1f", lang.grenade_angle.c_str(), t.Pitch, t.Yaw);
						ImGui::EndTooltip();
					}
					ImGui::PopID();
				}
				ImGui::EndChild();

				// Edit form
				if (selectedSavedThrow >= 0 && selectedSavedThrow < (int)throws.size()) {
					ImGui::Separator();
					ImGui::TextColored(UITheme::Info, "%s #%d", lang.grenade_editthrow.c_str(), selectedSavedThrow + 1);

					ImGui::SetNextItemWidth(200);
					ImGui::InputText((lang.grenade_name_label + "##saved").c_str(), editName, sizeof(editName));
					ImGui::SetNextItemWidth(150);
					ImGui::Combo((lang.grenade_throw_style + "##saved").c_str(), &editStyle, lang.grenade_styleselect, IM_ARRAYSIZE(lang.grenade_styleselect));
					ImGui::SetNextItemWidth(150);
					ImGui::Combo((lang.grenade_throw_type + "##saved").c_str(), &editType, lang.grenade_typeselect, IM_ARRAYSIZE(lang.grenade_typeselect));

					// Position on one line
					ImGui::Text("%s:", lang.grenade_position.c_str());
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					ImGui::InputFloat(u8"X##pos", &editPosX, 0.0f, 0.0f, "%.1f");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					ImGui::InputFloat(u8"Y##pos", &editPosY, 0.0f, 0.0f, "%.1f");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					ImGui::InputFloat(u8"Z##pos", &editPosZ, 0.0f, 0.0f, "%.1f");

					ImGui::Text("%s:", lang.grenade_angle.c_str());
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					ImGui::InputFloat(u8"Pitch##saved", &editPitch, 0.0f, 0.0f, "%.1f");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					ImGui::InputFloat(u8"Yaw##saved", &editYaw, 0.0f, 0.0f, "%.1f");

					ImGui::Spacing();
					if (ImGui::Button(lang.grenade_update.c_str(), ImVec2(80, 25))) {
						GrenadeHelper::UpdateThrow(selectedMap, selectedSavedThrow, editName, editStyle, editType,
							editPosX, editPosY, editPosZ, editPitch, editYaw);
					}
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Button, UITheme::DangerBg80);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::DangerBg90);
					if (ImGui::Button((lang.grenade_delete + "##saved").c_str(), ImVec2(80, 25))) {
						GrenadeHelper::DeleteThrow(selectedMap, selectedSavedThrow);
						if (selectedSavedThrow >= (int)throws.size())
							selectedSavedThrow = (int)throws.size() - 1;
					}
					ImGui::PopStyleColor(2);
				}
			}
		} else {
			ImGui::TextColored(UITheme::TextSecondary, "%s", lang.grenade_nomaps.c_str());
		}
	}
}

// ============================================================================
// Main Menu
// ============================================================================
void Cheats::Menu()
{
	static int active_tab = 0;
	static bool IsMenuInit = false;
	static bool MenuCollapsed = false;
	static ImVec2 CollapsedPos = ImVec2(100, 100);

	if (!IsMenuInit) {
		setStyles();
		IsMenuInit = true;
	}

	// --- Collapsed mode: small draggable circle ---
	if (MenuCollapsed) {
		ImGui::SetNextWindowPos(CollapsedPos, ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(40, 40));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UITheme::Accent.x, UITheme::Accent.y, UITheme::Accent.z, 0.90f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(UITheme::AccentHover.x, UITheme::AccentHover.y, UITheme::AccentHover.z, 0.80f));
		ImGui::Begin("##CollapsedMenu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
		CollapsedPos = ImGui::GetWindowPos();
		ImVec2 wp = ImGui::GetWindowPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(ImVec2(wp.x + 11, wp.y + 10), ImColor(1.0f, 1.0f, 1.0f, 1.0f), "C2");
		if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			MenuCollapsed = false;
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(globalVars::windowx, globalVars::windowy), ImGuiCond_Once);
	ImGui::Begin("CS2 DMA", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar);
	ImGui::SetWindowSize(ImVec2(globalVars::windowx, globalVars::windowy));
	{
		ImVec2 winPos = ImGui::GetWindowPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 titleBarMin = ImVec2(winPos.x, winPos.y);
		ImVec2 titleBarMax = ImVec2(winPos.x + globalVars::windowx, winPos.y + 28);
		dl->AddRectFilled(
			ImVec2(winPos.x, winPos.y),
			ImVec2(winPos.x + globalVars::windowx, winPos.y + 3),
			ImGui::ColorConvertFloat4ToU32(UITheme::Accent)
		);

		if (ImGui::IsMouseHoveringRect(titleBarMin, titleBarMax) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			CollapsedPos = winPos;
			MenuCollapsed = true;
		}

		ImGui::SetCursorPos(ImVec2(12, 8));
		ImGui::TextColored(UITheme::TextAccent, "CS2 / DMA");
		ImGui::SameLine(globalVars::windowx - 220);
		ImGui::TextColored(UITheme::TextMuted, "by github");
		ImGui::SameLine(0, 4);
		ImGui::TextColored(UITheme::TextAccent, "chao-shushu");

		ImGui::SetCursorPosY(28);
		ImGui::Separator();

		float navWidth = 150.0f;
		float contentWidth = globalVars::windowx - navWidth - 24;
		float contentHeight = globalVars::windowy - 50;

		// ========== Left Navigation Panel ==========
		ImGui::BeginChild("##NavPanel", ImVec2(navWidth, contentHeight), false);
		{
			float btnW = navWidth - 8;

			ImGui::TextColored(UITheme::TextDisabled, "  ESP");
			if (NavButton(lang.tab_visuals.c_str(), active_tab == 0, btnW)) active_tab = 0;
			if (NavButton(lang.tab_radar.c_str(), active_tab == 1, btnW)) active_tab = 1;

			ImGui::TextColored(UITheme::TextDisabled, "  TOOLS");
			if (NavButton(lang.tab_grenade.c_str(), active_tab == 4, btnW)) active_tab = 4;
			if (NavButton(lang.tab_hotkeys.c_str(), active_tab == 5, btnW)) active_tab = 5;

			ImGui::TextColored(UITheme::TextDisabled, "  SYSTEM");
			if (NavButton(lang.tab_settings.c_str(), active_tab == 2, btnW)) active_tab = 2;
			if (NavButton(lang.tab_config.c_str(), active_tab == 3, btnW)) active_tab = 3;

			float remainHeight = ImGui::GetContentRegionAvail().y;
			if (remainHeight > 60) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remainHeight - 56);
			}
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "\xe2\x98\x85");
			ImGui::SameLine(0, 4);
			ImGui::TextColored(UITheme::TextSecondary, "Star on GitHub");
			if (ImGui::IsItemHovered()) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				ImGui::SetTooltip("https://github.com/chao-shushu/CS2-DMA");
			}
			if (ImGui::IsItemClicked()) {
				ShellExecuteA(nullptr, "open", "https://github.com/chao-shushu/CS2-DMA", nullptr, nullptr, SW_SHOWNORMAL);
			}
			ImGui::Separator();
			ImGui::SetNextItemWidth(btnW);
			if (ImGui::Combo("##lang", &MenuConfig::SelectedLanguage, lang.utilities_langselect, IM_ARRAYSIZE(lang.utilities_langselect))) {
				switch (MenuConfig::SelectedLanguage) {
				case 0: lang.english(); break;
				case 1: lang.chineese(); break;
				default: break;
				}
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// ========== Vertical separator ==========
		{
			ImVec2 cp = ImGui::GetCursorScreenPos();
			dl->AddRectFilled(ImVec2(cp.x - 4, cp.y), ImVec2(cp.x - 2, cp.y + contentHeight), ImGui::ColorConvertFloat4ToU32(UITheme::BorderSubtle));
		}

		// ========== Right Content Panel ==========
		ImGui::BeginChild("##ContentPanel", ImVec2(contentWidth, contentHeight), false);
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, UITheme::ContentPadding);
			switch (active_tab) {
			case 0: DrawTab_Visuals(); break;
			case 1: DrawTab_Radar(); break;
			case 2: DrawTab_Settings(); break;
			case 3: DrawTab_Config(); break;
			case 4: DrawTab_Grenade(); break;
			case 5: DrawTab_Hotkeys(); break;
			default: break;
			}
			ImGui::PopStyleVar();

			{
				static bool wasAnyActive = false;
				bool anyActive = ImGui::IsAnyItemActive();
				if (wasAnyActive && !anyActive)
					MyConfigSaver::MarkDirty();
				wasAnyActive = anyActive;
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

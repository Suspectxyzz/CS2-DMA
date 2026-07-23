#include "../game/AppState.h"
#include "../game/CameraWorker.h"

#include "GUI.h"
#include "UITheme.h"

#include "Cheats.h"
#include "Render.h"
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <string>
#include "../game/MenuConfig.h"
#include "GrenadeHelper.h"
#include "../config/ConfigMenu.h"
#include "../config/ConfigSaver.h"
#include "../utils/Logger.h"
#include "../utils/DmaHealth.h"
#include "../utils/StageTimer.h"
#include "../utils/VisibilityChecker.h"
#include "WebRadar.h"
#ifdef AIMBOT_ENABLED
#include "FovCircle.h"
#include "../aim/SprayControl.h"  // ExtractFovFromMatrix / CountsToPixels
#include "../game/Game.h"         // gGame.View.Matrix
#include "../license/LicenseClient.h"
#include "../license/AntiTamper.h"
#endif

// g_dmaHealth is now defined as inline in DmaHealth.h (header-only, shared across all TUs).

// Forward declaration: defined in main.cpp
extern bool InitializeDMAAndThreads();



// ESP gap-closure stage 2: quintic (smootherstep) easing for snapshot interpolation.
// Produces a C1-continuous curve that eases position transitions between snapshots.
static float QuinticEase(float t)
{
	return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}


void Cheats::Run()
{
	static bool firstRun = true;
	if (firstRun) {
		LOG_INFO("Render", "Cheats::Run() first initialization");
		// Task 16: default EspItemEnabledMask to all-enabled so dropped
		// weapons render unless the user explicitly disables them. The
		// bitset is zero-initialised; set() flips every bit to 1.
		MenuConfig::EspItemEnabledMask.set();
		firstRun = false;
	}
#ifdef AIMBOT_ENABLED
	if (LicenseClient::DrawActivationUI()) return;
	// 激活成功后首次初始化 DMA 和线程
	{
		static bool s_dmaStarted = false;
		if (!s_dmaStarted && LicenseClient::IsLicensed()) {
			s_dmaStarted = true;
			InitializeDMAAndThreads();
		}
	}
	// 多点校验：周期性反调试 + 授权状态验证（每 ~10 秒）
	{
		static int s_antiTamperCnt = 0;
		if (++s_antiTamperCnt >= 600) {
			s_antiTamperCnt = 0;
			if (!AntiTamper::PeriodicCheck())
				globalVars::gameState.store(AppState::DMA_FAILED);
		}
	}
	// 已授权时在右上角显示剩余天数
	if (LicenseClient::IsLicensed()) {
		int days = LicenseClient::GetRemainingDays();
		char buf[64];
		snprintf(buf, sizeof(buf), u8"授权剩余: %d天", days);
		ImVec2 ts = ImGui::CalcTextSize(buf);
		ImVec2 screen = ImGui::GetIO().DisplaySize;
		ImU32 color = days <= 3 ? IM_COL32(255, 80, 80, 255) :
		              days <= 7 ? IM_COL32(255, 200, 50, 255) :
		              IM_COL32(100, 255, 100, 255);
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		dl->AddRectFilled({screen.x - ts.x - 16, 8}, {screen.x - 4, ts.y + 16}, IM_COL32(10, 10, 18, 200), 4.0f);
		dl->AddText({screen.x - ts.x - 10, 12}, color, buf);
	}
#endif
	try {
		static std::chrono::time_point LastTimePoint = std::chrono::steady_clock::now();
		auto CurTimePoint = std::chrono::steady_clock::now();

		if (Keys::MenuKey
			&& CurTimePoint - LastTimePoint >= std::chrono::milliseconds(150))
		{
			MenuConfig::ShowMenu = !MenuConfig::ShowMenu;
			LastTimePoint = CurTimePoint;
		}

		if (MenuConfig::ShowMenu)
			Menu();

		AppState currentState = globalVars::gameState.load();
		if (currentState != AppState::RUNNING)
		{
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			ImVec2 screenSize = ImGui::GetIO().DisplaySize;

			const char* statusText = "";
			switch (currentState) {
			case AppState::DMA_INITIALIZING: statusText = lang.status_dma_init.c_str(); break;
			case AppState::DMA_FAILED:       statusText = lang.status_dma_failed.c_str(); break;
			case AppState::SEARCHING_GAME:   statusText = lang.status_searching.c_str(); break;
			case AppState::INITIALIZING_GAME:statusText = lang.status_init_game.c_str(); break;
			case AppState::WAITING_DECRYPT:  statusText = lang.status_waiting_decrypt.c_str(); break;
			default:                         statusText = lang.status_unknown.c_str(); break;
			}

			ImVec2 textSize = ImGui::CalcTextSize(statusText);
			// DMA_FAILED 状态下额外显示失败原因（红色）
			const std::string& failReason = (currentState == AppState::DMA_FAILED)
				? globalVars::g_dmaFailReason : std::string{};
			ImVec2 reasonSize = failReason.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(failReason.c_str());

			// PCILeech 内存解密进度（橙色，仅在解密触发时显示）
			std::string decryptText;
			if (ProcessMgr.MemoryDecryptProgress.load(std::memory_order_relaxed) >= 0) {
				if (ProcessMgr.MemoryDecryptTimedOut.load(std::memory_order_relaxed)) {
					decryptText = u8"[!] 内存解密超时，尝试 DTB 修复";
				} else {
					char dbuf[64];
					snprintf(dbuf, sizeof(dbuf), u8"[*] 内存解密中 %d%%",
						ProcessMgr.MemoryDecryptProgress.load(std::memory_order_relaxed));
					decryptText = dbuf;
				}
			}
			ImVec2 decryptSize = decryptText.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(decryptText.c_str());

			float maxTextWidth = textSize.x;
			if (reasonSize.x > maxTextWidth) maxTextWidth = reasonSize.x;
			if (decryptSize.x > maxTextWidth) maxTextWidth = decryptSize.x;
			float totalHeight = textSize.y;
			if (!failReason.empty()) totalHeight += reasonSize.y + 6.0f;
			if (!decryptText.empty()) totalHeight += decryptSize.y + 6.0f;
			ImVec2 textPos = { (screenSize.x - maxTextWidth) / 2, (screenSize.y - totalHeight) / 2 };

			drawList->AddRectFilled(
				{ textPos.x - 20, textPos.y - 10 },
				{ textPos.x + maxTextWidth + 20, textPos.y + totalHeight + 10 },
				IM_COL32(10, 10, 18, 210), 8.0f);
			drawList->AddRect(
				{ textPos.x - 20, textPos.y - 10 },
				{ textPos.x + maxTextWidth + 20, textPos.y + totalHeight + 10 },
				IM_COL32(124, 92, 252, 80), 8.0f, 15, 1.0f);
			// 状态文本居中显示
			ImVec2 statusPos = { textPos.x + (maxTextWidth - textSize.x) / 2, textPos.y };
			drawList->AddText(statusPos, IM_COL32(232, 232, 240, 255), statusText);
			// 失败原因（红色，在状态文本下方）
			float lineY = textPos.y + textSize.y;
			if (!failReason.empty()) {
				ImVec2 reasonPos = { textPos.x + (maxTextWidth - reasonSize.x) / 2, lineY + 6.0f };
				drawList->AddText(reasonPos, IM_COL32(255, 80, 80, 255), failReason.c_str());
				lineY += reasonSize.y + 6.0f;
			}
			// 内存解密进度（橙色，在失败原因下方）
			if (!decryptText.empty()) {
				ImVec2 decryptPos = { textPos.x + (maxTextWidth - decryptSize.x) / 2, lineY + 6.0f };
				drawList->AddText(decryptPos, ImGui::ColorConvertFloat4ToU32(UITheme::Warning), decryptText.c_str());
			}

			return;
		}

		// Phase 4: 自瞄 FOV 圆圈可视化 (屏幕中心, 背景层)
		// playerFov 从 ViewMatrix 推导 (自动适配 4:3/16:9/拉伸)
#ifdef AIMBOT_ENABLED
		{
			Aim::FovPair fov = Aim::ExtractFovFromMatrix(gGame.View.Matrix);
			FovCircle::RenderFovCircles(fov.hFov);
		}
		FovCircle::RenderPredictedImpact();
#endif

		// Lock-free snapshot read (double-buffered, acquire on the index).
		GameSnapshot snap = Cheats::GetSnapshot();
		LOG_TRACE("Render", "Snapshot: entities={} projs={} map='{}'", snap.Entities.size(), snap.Projectiles.size(), snap.MapName);

		const auto& LocalPlayerSnapshot = snap.LocalPlayer;

		// View matrix is read at 500Hz by the dedicated CameraWorker thread,
		// decoupling camera reads from the render loop. Re-projecting here
		// ensures ESP tracks view rotation at display refresh rate —
		// eliminates stutter during mouse movement.
		float freshMatrix[4][4];
		if (CameraWorker::HasFreshData()) {
			CameraWorker::GetLatestMatrix(freshMatrix);
		} else {
			// No fresh data yet (worker still warming up) — fall back to the
			// matrix captured in the last snapshot.
			memcpy(freshMatrix, snap.Matrix, sizeof(freshMatrix));
		}

		// Triple fallback: live -> snapshot -> last good matrix (ref: KevqDMA draw.inl)
		static float s_lastGoodDrawMatrix[4][4]{};
		static bool s_lastGoodDrawMatrixValid = false;
		auto isLikelyViewMatrix = [](const float m[4][4]) -> bool {
			int nonZero = 0; float absSum = 0.f;
			for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
				float v = m[r][c]; if (!std::isfinite(v)) return false;
				if (v != 0.f) nonZero++; absSum += std::abs(v);
			}
			return nonZero >= 6 && absSum > 1.0f;
		};
		if (isLikelyViewMatrix(freshMatrix)) {
			memcpy(s_lastGoodDrawMatrix, freshMatrix, sizeof(freshMatrix));
			s_lastGoodDrawMatrixValid = true;
		} else if (s_lastGoodDrawMatrixValid) {
			memcpy(freshMatrix, s_lastGoodDrawMatrix, sizeof(freshMatrix));
		}

		// Re-project all entities: world coords -> screen coords with fresh matrix
		//
		// ESP gap-closure stage 2: snapshot interpolation + velocity extrapolation.
		// renderPos = lerp(PrevPos, Pos, QuinticEase(alpha)) + Velocity * extrapolationSec
		// - alpha advances 0→1 across one snapshot interval (smooths position jumps)
		// - extrapolationSec kicks in after the interval, capped at 80ms, using Velocity
		// - PrevPos == (0,0,0) (first frame) skips interpolation and uses Pos directly
		const bool interpOn = MenuConfig::InterpolationEnabled;
		const int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		const int64_t captureUs = snap.CaptureTimeUs;
		static int64_t s_prevCaptureUs = 0;
		int64_t intervalUs = (s_prevCaptureUs > 0 && captureUs > s_prevCaptureUs)
			? (captureUs - s_prevCaptureUs) : 16000;
		if (captureUs != s_prevCaptureUs) s_prevCaptureUs = captureUs;

		float lerpT = 1.f;
		float extrapolationSec = 0.f;
		if (interpOn && captureUs > 0) {
			int64_t elapsedUs = nowUs - captureUs;
			float alpha = std::clamp((float)elapsedUs / (float)intervalUs, 0.f, 1.f);
			lerpT = QuinticEase(alpha);
			int64_t excessUs = elapsedUs - intervalUs;
			if (excessUs > 0)
				extrapolationSec = std::min(0.08f, (float)excessUs / 1000000.f);
		}

		for (auto& Entity : snap.Entities) {
			if (!Entity.Pawn.ScreenPosValid) continue;

			// Interpolated + extrapolated render position (falls back to Pos when off).
			Vec3 renderPos = Entity.Pawn.Pos;
			if (interpOn) {
				const Vec3& prevPos = Entity.Pawn.PrevPos;
				bool prevValid = (prevPos.x != 0.f || prevPos.y != 0.f || prevPos.z != 0.f);
				if (prevValid) {
					renderPos.x = prevPos.x + (Entity.Pawn.Pos.x - prevPos.x) * lerpT;
					renderPos.y = prevPos.y + (Entity.Pawn.Pos.y - prevPos.y) * lerpT;
					renderPos.z = prevPos.z + (Entity.Pawn.Pos.z - prevPos.z) * lerpT;
				}
				if (extrapolationSec > 0.f) {
					renderPos.x += Entity.Pawn.Velocity.x * extrapolationSec;
					renderPos.y += Entity.Pawn.Velocity.y * extrapolationSec;
					renderPos.z += Entity.Pawn.Velocity.z * extrapolationSec;
				}
			}

			Vec2 footScreen;
			Entity.Pawn.ScreenPosValid = CView::WorldToScreen(freshMatrix, renderPos, footScreen);
			Entity.Pawn.ScreenPos = footScreen;

			// Apply the same interpolation offset to bone positions so that
			// foot and head are from the same time basis. Without this, Get2DBox
			// mixes interpolated foot with non-interpolated head, causing box
			// height/position drift during movement.
			Vec3 interpOffset = { renderPos.x - Entity.Pawn.Pos.x,
			                      renderPos.y - Entity.Pawn.Pos.y,
			                      renderPos.z - Entity.Pawn.Pos.z };
			for (int j = 0; j < Entity.Pawn.BoneData.BonePosCount; j++) {
				auto& bp = Entity.Pawn.BoneData.BonePosList[j];
				if (!bp.IsVisible) continue; // already marked invalid by data thread
				Vec3 interpBonePos = { bp.Pos.x + interpOffset.x,
				                       bp.Pos.y + interpOffset.y,
				                       bp.Pos.z + interpOffset.z };
				Vec2 sp;
				bp.IsVisible = CView::WorldToScreen(freshMatrix, interpBonePos, sp);
				bp.ScreenPos = sp;
			}
		}

		const auto& EntityListSnapshot = snap.Entities;

		ImVec2 szCenter = { 0, 0 };
		bool szSkipMode = MenuConfig::SafeZoneEnabled && MenuConfig::SafeZoneMode == 1;
		if (szSkipMode) {
			szCenter = { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f };
		}

		// Task 10: Sound ESP needs a millisecond timestamp to compare against SoundUntilMs.
		const uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();

		for (int i = 0; i < EntityListSnapshot.size(); i++)
		{
			const CEntity& Entity = EntityListSnapshot[i];

			if (MenuConfig::TeamCheck && Entity.Controller.TeamID == LocalPlayerSnapshot.Controller.TeamID)
				continue;

			if (!Entity.Pawn.ScreenPosValid) continue;
			if (!std::isfinite(Entity.Pawn.ScreenPos.x) || !std::isfinite(Entity.Pawn.ScreenPos.y)) continue;
			if (!std::isfinite(Entity.Pawn.Pos.x) || !std::isfinite(Entity.Pawn.Pos.y) || !std::isfinite(Entity.Pawn.Pos.z)) continue;
			if (Entity.Controller.Address == 0) continue;

			// Bone availability: when bones are insufficient, skip bone-dependent
			// ESP elements (skeleton, head dot, eye ray) but still draw box, health,
			// name, distance etc. using a fallback box.
			bool hasBones = Entity.GetBone().BonePosCount > (int)BONEINDEX::head;

			ImVec4 Rect;
			if (hasBones)
				Rect = Render::GetBoxByType(Entity);
			else
				Rect = ImVec4{ 0, 0, 0, 0 };

			// Fallback: if bone-based rect failed (head off-screen, too far, etc.),
			// estimate a box from foot screen position and world distance.
			if (Rect.z < 1.f || Rect.w < 1.f ||
				!std::isfinite(Rect.x) || !std::isfinite(Rect.y) ||
				!std::isfinite(Rect.z) || !std::isfinite(Rect.w)) {
				float dx = Entity.Pawn.Pos.x - LocalPlayerSnapshot.Pawn.Pos.x;
				float dy = Entity.Pawn.Pos.y - LocalPlayerSnapshot.Pawn.Pos.y;
				float dz = Entity.Pawn.Pos.z - LocalPlayerSnapshot.Pawn.Pos.z;
				float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
				float boxH = (distance > 1.f) ? std::clamp(3600.f / distance, 15.f, 400.f) : 80.f;
				float boxW = boxH * 0.5f;
				Rect = ImVec4(Entity.Pawn.ScreenPos.x - boxW * 0.5f,
				              Entity.Pawn.ScreenPos.y - boxH, boxW, boxH);
			}

			bool inSafeZone = false;
			if (szSkipMode) {
				float ex = Rect.x + Rect.z * 0.5f;
				float ey = Rect.y + Rect.w * 0.5f;
				if (MenuConfig::SafeZoneShape == 0) {
					float dx = ex - szCenter.x;
					float dy = ey - szCenter.y;
					inSafeZone = (dx * dx + dy * dy) <= (MenuConfig::SafeZoneRadius * MenuConfig::SafeZoneRadius);
				} else {
					inSafeZone = fabsf(ex - szCenter.x) <= MenuConfig::SafeZoneRadius &&
					             fabsf(ey - szCenter.y) <= MenuConfig::SafeZoneRadius;
				}
			}

			// Task 5-8/9: 可见性着色 —— 单一开关 VPKVisibilityCheck 控制
			// - 开启且有 BVH：用 BVH 射线检测真实遮挡，所有 ESP 元素按可见性变色
			//   * 骨骼 ESP：每段骨骼独立判断（visible 数组逐关节计算）
			//   * 方框/头部圆点/视线/连线：整体用头部射线结果
			// - 关闭或 BVH 不可用：所有元素使用原色
			bool visEnabled = MenuConfig::VPKVisibilityCheck && g_visibilityChecker.IsEnabled() && hasBones;
			bool isVisibleNow = true;       // 整人可见性（用于方框/头点/视线/连线）
			bool boneVisible[CBone::NUM_BONES] = {};  // 每骨骼可见性（用于骨骼 ESP）
			Vec3 fromEye{};

			if (visEnabled) {
			// 本地眼睛位置: 优先 head 骨骼(真实眼睛), 回退 Pos+64(站立兜底).
			// m_vecLastCameraSetupLocalOrigin 实测为相机 setup 本地原点(Z≈0), 不可用作眼睛位置.
			fromEye = LocalPlayerSnapshot.Pawn.Pos;
			fromEye.z += 64.f;
			const CBone& lpBone = LocalPlayerSnapshot.Pawn.BoneData;
			if (lpBone.BonePosCount > (int)BONEINDEX::head) {
				const Vec3& headPos = lpBone.BonePosList[(int)BONEINDEX::head].Pos;
				if (headPos.x != 0.f || headPos.y != 0.f || headPos.z != 0.f)
					fromEye = headPos;
			}
				const CBone& bone = Entity.GetBone();
				// 逐骨骼射线检测
				for (int j = 0; j < bone.BonePosCount && j < CBone::NUM_BONES; j++) {
					boneVisible[j] = g_visibilityChecker.IsVisible(fromEye, bone.BonePosList[j].Pos);
				}
				// 整人可见性 = 头部射线结果
				if (bone.BonePosCount > (int)BONEINDEX::head) {
					isVisibleNow = boneVisible[BONEINDEX::head];
				}
			}

			// 元素颜色：visEnabled 时按可见性切换；否则用原色
			ImColor boxCol        = visEnabled ? (isVisibleNow ? MenuConfig::VisibleColor : MenuConfig::HiddenColor) : MenuConfig::BoxColor;
			ImColor headDotCol    = visEnabled ? (isVisibleNow ? MenuConfig::VisibleColor : MenuConfig::HiddenColor) : MenuConfig::HeadDotColor;
			ImColor eyeRayCol     = visEnabled ? (isVisibleNow ? MenuConfig::VisibleColor : MenuConfig::HiddenColor) : MenuConfig::EyeRayColor;
			ImColor lineToEnemyCol= visEnabled ? (isVisibleNow ? MenuConfig::VisibleColor : MenuConfig::HiddenColor) : MenuConfig::LineToEnemyColor;

			if (MenuConfig::ShowBoneESP && hasBones && !(inSafeZone && MenuConfig::SafeZoneSkipBone))
				Render::DrawBone(Entity, MenuConfig::BoneColor, MenuConfig::BoneThickness,
				                visEnabled ? boneVisible : nullptr,
				                MenuConfig::VisibleColor, MenuConfig::HiddenColor);

			if (MenuConfig::ShowHeadDot && hasBones && !(inSafeZone && MenuConfig::SafeZoneSkipHeadDot))
				Render::DrawHeadDot(Entity, headDotCol, MenuConfig::HeadDotSize);

			if (MenuConfig::ShowEyeRay && hasBones && !(inSafeZone && MenuConfig::SafeZoneSkipEyeRay))
				Render::ShowLosLine(Entity, MenuConfig::EyeRayLength, eyeRayCol, MenuConfig::EyeRayThickness, freshMatrix);

			if (MenuConfig::ShowLineToEnemy && !(inSafeZone && MenuConfig::SafeZoneSkipSnapline))
				Render::LineToEnemyEx(Rect, lineToEnemyCol, MenuConfig::LineToEnemyThickness, MenuConfig::LineToEnemyOrigin);

			if (MenuConfig::ShowBoxESP && !(inSafeZone && MenuConfig::SafeZoneSkipBox))
			{
				if (MenuConfig::BoxType == 2)
					Render::DrawCornerBox(Rect, boxCol, MenuConfig::BoxThickness, MenuConfig::CornerLength);
				else
					Gui.Rectangle({ Rect.x,Rect.y }, { Rect.z,Rect.w }, boxCol, MenuConfig::BoxThickness, MenuConfig::BoxRounding);
			}

			// Task 8: Player status flags — Blind/Scoped/Defusing/Kit/Money stacked
			// vertically to the right of the 2D box.
			if (MenuConfig::ShowPlayerFlags && !(inSafeZone && MenuConfig::SafeZoneSkipBox))
				Render::DrawPlayerFlags(ImGui::GetBackgroundDrawList(), Entity,
					ImVec2(Rect.x, Rect.y), ImVec2(Rect.x + Rect.z, Rect.y + Rect.w),
					MenuConfig::FlagFontSize);

			// Task 10: Sound ESP — expanding ripple at the player's feet when a
			// shot was fired within the last 420ms.
			if (MenuConfig::ShowSoundESP && Entity.Pawn.SoundUntilMs > nowMs)
			{
				Render::DrawSoundESP(ImGui::GetBackgroundDrawList(), Entity.Pawn.ScreenPos,
					nowMs, Entity.Pawn.SoundUntilMs,
					ImGui::ColorConvertFloat4ToU32(MenuConfig::SoundESPColor.Value));
			}

			// Footstep ESP — persistent pulsing circle while enemy moves on ground
			// (shift-walk filtered via IsWalking, air filtered via FL_ONGROUND).
			if (MenuConfig::ShowFootstepESP)
			{
				float dx = Entity.Pawn.Velocity.x;
				float dy = Entity.Pawn.Velocity.y;
				float hSpeed = sqrtf(dx * dx + dy * dy);
				bool isGrounded = (Entity.Pawn.fFlags & 1) != 0;   // FL_ONGROUND = bit 0
				if (isGrounded && !Entity.Pawn.IsWalking && hSpeed > 50.f)
				{
					Render::DrawFootstepESP(ImGui::GetBackgroundDrawList(), Entity.Pawn.ScreenPos,
						nowMs, ImGui::ColorConvertFloat4ToU32(MenuConfig::FootstepColor.Value));
				}
			}

			if (MenuConfig::ShowHealthBar && !(inSafeZone && MenuConfig::SafeZoneSkipHealthBar))
			{
				if (MenuConfig::HealthBarType == 2)
				{
					ImColor hpColor = ImColor(Render::HealthColorFromRatio(Entity.Pawn.Health));
					float hpY = Rect.y - MenuConfig::NameFontSize;
					if (MenuConfig::ShowPlayerName)
						hpY -= MenuConfig::NameFontSize;
					Gui.StrokeText(std::to_string(Entity.Pawn.Health), Vec2(Rect.x + Rect.z / 2, hpY), hpColor, MenuConfig::NameFontSize, true);
				}
				else
				{
					ImVec2 HealthBarPos, HealthBarSize;
					if (MenuConfig::HealthBarType == 0)
					{
						HealthBarPos = { Rect.x - MenuConfig::HealthBarWidth - 3.f, Rect.y };
						HealthBarSize = { MenuConfig::HealthBarWidth, Rect.w };
					}
					else
					{
						HealthBarPos = { Rect.x + Rect.z / 2 - 70 / 2, Rect.y - 13 };
						HealthBarSize = { 70, 8 };
					}
					Render::DrawHealthBar(Entity.Controller.Address, 100, Entity.Pawn.Health, HealthBarPos, HealthBarSize, MenuConfig::HealthBarType);
				}
			}

			if (MenuConfig::ShowArmorBar && !(inSafeZone && MenuConfig::SafeZoneSkipArmorBar))
				Render::DrawArmorBar(Entity.Pawn.Armor, Rect, MenuConfig::ArmorBarColor, MenuConfig::ArmorBarWidth, MenuConfig::ArmorBarType);

			// Task 14: Bar value labels — numeric tag above health/armor bars.
			// Only drawn for the vertical bar variants (type 0) to avoid
			// overlap with the horizontal/top bar layouts.
			if ((MenuConfig::ShowHealthText || MenuConfig::ShowArmorText) &&
			    !(inSafeZone && (MenuConfig::SafeZoneSkipHealthBar || MenuConfig::SafeZoneSkipArmorBar)))
			{
				ImDrawList* labelDL = ImGui::GetBackgroundDrawList();
				float screenW = ImGui::GetIO().DisplaySize.x;

				if (MenuConfig::ShowHealthText && MenuConfig::ShowHealthBar &&
				    MenuConfig::HealthBarType == 0 && Entity.Pawn.Health < 100)
				{
					ImU32 hpCol = Render::HealthColorFromRatio(Entity.Pawn.Health);
					ImVec2 hpBarPos(Rect.x - MenuConfig::HealthBarWidth - 3.f, Rect.y);
					Render::DrawBarLabel(labelDL, hpBarPos, MenuConfig::HealthBarWidth,
					                     Entity.Pawn.Health, hpCol, MenuConfig::BarLabelFontSize, screenW);
				}

				if (MenuConfig::ShowArmorText && MenuConfig::ShowArmorBar &&
				    MenuConfig::ArmorBarType == 0 && Entity.Pawn.Armor > 0 && Entity.Pawn.Armor < 100)
				{
					ImU32 apCol = ImGui::ColorConvertFloat4ToU32(MenuConfig::ArmorBarColor.Value);
					ImVec2 apBarPos(Rect.x + Rect.z + 2.f, Rect.y);
					Render::DrawBarLabel(labelDL, apBarPos, MenuConfig::ArmorBarWidth,
					                     Entity.Pawn.Armor, apCol, MenuConfig::BarLabelFontSize, screenW);
				}
			}

			if (MenuConfig::ShowWeaponESP && !(inSafeZone && MenuConfig::SafeZoneSkipWeapon))
				Gui.StrokeText(Entity.Pawn.WeaponName, Vec2(Rect.x, Rect.y + Rect.w), MenuConfig::WeaponColor, MenuConfig::WeaponFontSize);

			// Task 13: Weapon icon ESP — render the active weapon's icon glyph
			// (from weapons.ttf) above the weapon name. WeaponName is the
			// visualKey (e.g. "ak47"), so we reverse-lookup the item id.
			if (MenuConfig::ShowWeaponIcon && !(inSafeZone && MenuConfig::SafeZoneSkipWeapon))
			{
				const WeaponLookup::WeaponLookupEntry* entry =
					WeaponLookup::FindWeaponLookupEntryByVisualKey(Entity.Pawn.WeaponName.c_str());
				if (entry) {
					ImVec2 iconPos(Rect.x + Rect.z * 0.5f, Rect.y + Rect.w + 2.f);
					Render::DrawWeaponIcon(ImGui::GetBackgroundDrawList(), iconPos,
					                       entry->id, MenuConfig::WeaponIconFontSize,
					                       ImGui::ColorConvertFloat4ToU32(MenuConfig::WeaponIconColor.Value),
					                       MenuConfig::WeaponIconNoKnife);
				}
			}

			// Task 13: Weapon ammo ESP — "Ammo XX/YY" + LOW warning below weapon name.
			if (MenuConfig::ShowWeaponAmmo && !(inSafeZone && MenuConfig::SafeZoneSkipWeapon))
			{
				int maxClip = Render::WeaponMaxClip(Entity.Pawn.WeaponName);
				ImVec2 ammoPos(Rect.x, Rect.y + Rect.w + MenuConfig::WeaponFontSize + 2.f);
				Render::DrawWeaponAmmo(ImGui::GetBackgroundDrawList(), ammoPos,
				                       Entity.Pawn.AmmoClip, maxClip,
				                       MenuConfig::WeaponAmmoFontSize,
				                       ImGui::ColorConvertFloat4ToU32(MenuConfig::WeaponAmmoColor.Value),
				                       ImGui::ColorConvertFloat4ToU32(MenuConfig::WeaponLowAmmoColor.Value));
			}

			if (MenuConfig::ShowDistance && !(inSafeZone && MenuConfig::SafeZoneSkipDistance))
				Render::DrawDistance(LocalPlayerSnapshot, Entity, Rect);

			if (MenuConfig::ShowPlayerName && !(inSafeZone && MenuConfig::SafeZoneSkipName))
			{
				std::string displayName = Entity.Controller.PlayerName;
				if (MenuConfig::HealthBarType == 1)
					Gui.StrokeText(displayName, Vec2(Rect.x + Rect.z / 2, Rect.y - 13 - MenuConfig::NameFontSize), MenuConfig::NameColor, MenuConfig::NameFontSize, true);
				else
					Gui.StrokeText(displayName, Vec2(Rect.x + Rect.z / 2, Rect.y - MenuConfig::NameFontSize), MenuConfig::NameColor, MenuConfig::NameFontSize, true);
			}
		}

		// Bomb ESP
		if (MenuConfig::ShowBombESP)
			Render::DrawBombESP(snap.Bomb, freshMatrix);

		// Task 11: C4 bomb timer overlay — draggable center window with C4
		// countdown + defuse progress bars. Only shown while planted & ticking.
		if (MenuConfig::ShowBombTimer &&
		    snap.Bomb.isPlanted && !snap.Bomb.isDefused && snap.Bomb.blowTime > 0.f)
		{
			// Infer defuse total (5s with kit, 10s without) once when defuse starts.
			static float s_defuseTotalSec = 10.f;
			static bool  s_prevDefusing = false;
			if (snap.Bomb.isDefusing) {
				if (!s_prevDefusing)
					s_defuseTotalSec = (snap.Bomb.defuseTime > 5.5f) ? 10.f : 5.f;
				s_prevDefusing = true;
			} else {
				s_prevDefusing = false;
			}

			ImVec2 ds = ImGui::GetIO().DisplaySize;
			Render::DrawBombTimerOverlay(ImGui::GetBackgroundDrawList(),
			                             ds.x, ds.y,
			                             snap.Bomb.blowTime, 40.f,
			                             snap.Bomb.isDefusing, snap.Bomb.defuseTime, s_defuseTotalSec);
		}

		// Grenade Projectile ESP
		if (MenuConfig::ShowProjectileESP && !snap.Projectiles.empty())
			Render::DrawProjectileESP(snap.Projectiles, freshMatrix, LocalPlayerSnapshot.Pawn.Pos);

		// Task 12: World ESP — grenade effect timers (Smoke/Inferno/Decoy).
		// Dropped-weapon scanning is deferred; this overlay only draws the
		// lingering effect countdown above active smoke/fire/decoy spots.
		if (MenuConfig::ShowWorldProjectileTimers && !snap.Projectiles.empty())
			Render::DrawWorldESP(ImGui::GetBackgroundDrawList(), snap.Projectiles, freshMatrix, 0);

		// Task 12/16: Dropped-weapon world ESP — icon + name for weapons on
		// the ground, filtered by EspItemEnabledMask. Drawn under
		// ShowWorldItems (independent toggle).
		if (MenuConfig::ShowWorldItems && !snap.DroppedWeapons.empty())
			Render::DrawDroppedWeapons(ImGui::GetBackgroundDrawList(), snap.DroppedWeapons, freshMatrix);

		// Crosshair overlay: drawn on top of ESP, below safe zone mask
		if (MenuConfig::CrosshairEnabled) {
			ImVec2 c = { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f };
			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			// Determine if crosshair is on an enemy
			bool onEnemy = false;
			if (MenuConfig::CrosshairOnEnemyColor) {
				for (const auto& Entity : EntityListSnapshot) {
				if (MenuConfig::TeamCheck && Entity.Controller.TeamID == LocalPlayerSnapshot.Controller.TeamID) continue;
					if (!Entity.Pawn.ScreenPosValid) continue;

					ImVec4 eRect;
					eRect = Render::GetBoxByType(Entity);
					if (eRect.z < 1.f || eRect.w < 1.f) continue;
					if (c.x >= eRect.x && c.x <= eRect.x + eRect.z &&
						c.y >= eRect.y && c.y <= eRect.y + eRect.w) {
						onEnemy = true;
						break;
					}
				}
			}

			ImU32 col = onEnemy
				? ImGui::ColorConvertFloat4ToU32(MenuConfig::CrosshairEnemyColor.Value)
				: ImGui::ColorConvertFloat4ToU32(MenuConfig::CrosshairColor.Value);
			float sz = MenuConfig::CrosshairSize;
			float th = MenuConfig::CrosshairThickness;
			float gap = MenuConfig::CrosshairGap;

			// Style 0: Cross, 1: Dot, 2: Circle, 3: Cross+Dot
			bool drawCross = (MenuConfig::CrosshairStyle == 0 || MenuConfig::CrosshairStyle == 3);
			bool drawDot   = (MenuConfig::CrosshairStyle == 1 || MenuConfig::CrosshairStyle == 3);
			bool drawCircle = (MenuConfig::CrosshairStyle == 2);

			if (drawCross) {
				dl->AddLine({ c.x - gap - sz, c.y }, { c.x - gap, c.y }, col, th);
				dl->AddLine({ c.x + gap, c.y }, { c.x + gap + sz, c.y }, col, th);
				dl->AddLine({ c.x, c.y - gap - sz }, { c.x, c.y - gap }, col, th);
				dl->AddLine({ c.x, c.y + gap }, { c.x, c.y + gap + sz }, col, th);
			}
			if (drawDot) {
				dl->AddCircleFilled({ c.x, c.y }, th, col, 12);
			}
			if (drawCircle) {
				dl->AddCircle({ c.x, c.y }, sz, col, 32, th);
			}
		}

		// Safe zone black mask: drawn on top of all ESP to erase the crosshair area
		if (MenuConfig::SafeZoneEnabled && MenuConfig::SafeZoneMode == 0) {
			ImVec2 c = { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f };
			float r = MenuConfig::SafeZoneRadius;
			ImDrawList* dl = ImGui::GetBackgroundDrawList();
			if (MenuConfig::SafeZoneShape == 0)
				dl->AddCircleFilled({ c.x, c.y }, r, IM_COL32(0, 0, 0, 255), 64);
			else
				dl->AddRectFilled({ c.x - r, c.y - r }, { c.x + r, c.y + r }, IM_COL32(0, 0, 0, 255));
		}

		// Grenade Helper - Update map and render
		static std::string lastMapName;
		if (snap.MapName[0] != '\0' && snap.MapName != lastMapName) {
			LOG_DEBUG("Render", "Map changed: '{}' -> '{}'", lastMapName, snap.MapName);
			GrenadeHelper::UpdateMap(snap.MapName);
			// Task 5-8: VPK 可见性检查 —— 加载新地图几何数据并构建 BVH
			g_visibilityChecker.UpdateForMap(snap.MapName);
			lastMapName = snap.MapName;
		}
		GrenadeHelper::Render(LocalPlayerSnapshot);

		// Handle auto-save for grenade helper
		if (GrenadeHelper::NeedSave) {
			GrenadeHelper::SaveToFile(GrenadeHelper::CurrentMap);
			GrenadeHelper::NeedSave = false;
		}

		// Task 5-8: VPK 可见性检查 —— 当前地图无几何数据时在屏幕底部提示
		if (MenuConfig::VPKVisibilityCheck && !g_visibilityChecker.IsEnabled()) {
			ImDrawList* dl = ImGui::GetBackgroundDrawList();
			const char* hint = u8"当前地图无 VPK 几何数据，使用近似可见性";
			ImVec2 ts = ImGui::CalcTextSize(hint);
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			dl->AddText(ImVec2(10, screen.y - ts.y - 10),
				ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.6f, 0.0f, 1.0f)), hint);
		}

		// Performance Monitor overlay
		if (MenuConfig::ShowPerfMonitor) {
			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			float posX = 10.f;
			float posY = 10.f;
			float lineH = 16.f;
			float padX = 10.f;
			float padY = 6.f;

			float fps = ImGui::GetIO().Framerate;
			float frameTimeMs = 1000.f / (fps > 0.f ? fps : 1.f);

			// Lines: FPS, Frame, Entities, Projectiles, DMA Health
		// When ShowDebugStats: + Stage header + 2 stage lines + WebRadar header + 1 wr line
		int lines = 5;
		if (MenuConfig::ShowDebugStats) lines += 5;
			float bgHeight = padY * 2 + lineH * lines;

			dl->AddRectFilled({ posX - padX, posY - padY }, { posX + 180.f + padX, posY - padY + bgHeight },
				IM_COL32(10, 10, 18, 210), 8.f);
			dl->AddRect({ posX - padX, posY - padY }, { posX + 180.f + padX, posY - padY + bgHeight },
				IM_COL32(124, 92, 252, 80), 8.f, 15, 1.f);

			ImColor labelCol(160, 160, 185, 255);
			ImColor valueCol(232, 232, 240, 255);

			ImColor fpsCol;
			if (fps >= 60.f) fpsCol = ImColor(74, 222, 128, 255);
			else if (fps >= 30.f) fpsCol = ImColor(251, 191, 36, 255);
			else fpsCol = ImColor(248, 113, 113, 255);

			char buf[128];
			snprintf(buf, sizeof(buf), "%.0f", fps);
			dl->AddText({ posX, posY }, labelCol, "FPS:");
			dl->AddText({ posX + 80.f, posY }, fpsCol, buf);
			posY += lineH;

			snprintf(buf, sizeof(buf), "%.1f ms", frameTimeMs);
			dl->AddText({ posX, posY }, labelCol, "Frame:");
			dl->AddText({ posX + 80.f, posY }, valueCol, buf);
			posY += lineH;

			snprintf(buf, sizeof(buf), "%d", (int)snap.Entities.size());
			dl->AddText({ posX, posY }, labelCol, "Entities:");
			dl->AddText({ posX + 80.f, posY }, valueCol, buf);
			posY += lineH;

			snprintf(buf, sizeof(buf), "%d", (int)snap.Projectiles.size());
			dl->AddText({ posX, posY }, labelCol, "Projectiles:");
			dl->AddText({ posX + 80.f, posY }, valueCol, buf);
			posY += lineH;

			// DMA Health (always shown in PerfMonitor)
			auto healthState = g_dmaHealth.GetState();
			auto healthStats = g_dmaHealth.GetStats();
			ImColor healthCol;
			const char* healthLabel;
			if (healthState == DmaHealth::DmaHealthState::Healthy) { healthCol = ImColor(74, 222, 128, 255); healthLabel = "Healthy"; }
			else if (healthState == DmaHealth::DmaHealthState::Degraded) { healthCol = ImColor(251, 191, 36, 255); healthLabel = "Degraded"; }
			else { healthCol = ImColor(248, 113, 113, 255); healthLabel = "Failed"; }
			dl->AddText({ posX, posY }, labelCol, "DMA:");
			snprintf(buf, sizeof(buf), "%s (%lldF/%lldS)", healthLabel,
				(long long)healthStats.totalFailures, (long long)healthStats.totalSuccesses);
			dl->AddText({ posX + 80.f, posY }, healthCol, buf);
			posY += lineH;

			// Detailed debug stats (stage timings)
		if (MenuConfig::ShowDebugStats) {
			dl->AddText({ posX, posY }, labelCol, "Stage (us):");
			posY += lineH;
			snprintf(buf, sizeof(buf), "mtx:%lld loc:%lld ent:%lld",
				(long long)g_stageMatrixUs.load(), (long long)g_stageLocalUs.load(),
				(long long)g_stageEntitiesUs.load());
			dl->AddText({ posX + 20.f, posY }, valueCol, buf);
			posY += lineH;
			snprintf(buf, sizeof(buf), "sct:%lld wpn:%lld bomb:%lld proj:%lld",
				(long long)g_stageScatterUs.load(), (long long)g_stageWeaponUs.load(),
				(long long)g_stageBombUs.load(), (long long)g_stageProjectileUs.load());
			dl->AddText({ posX + 20.f, posY }, valueCol, buf);
			posY += lineH;

			// WebRadar stats
			RuntimeStatsSnapshot wrSnap;
			{
				std::lock_guard<std::mutex> lock(g_webRadarStatsMutex);
				wrSnap = g_webRadarStats;
			}
			dl->AddText({ posX, posY }, labelCol, "WebRadar:");
			posY += lineH;
			snprintf(buf, sizeof(buf), "cli:%d hz:%.1f bytes:%zu",
				wrSnap.clientCount, wrSnap.sendHz, wrSnap.payloadBytesLast);
			dl->AddText({ posX + 20.f, posY }, valueCol, buf);
			posY += lineH;
		}
		}

		// Auto-save config: save immediately when dirty (200ms debounce), skip when clean
		{
			static auto lastAutoSave = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			if (MyConfigSaver::IsDirty() && (now - lastAutoSave) >= std::chrono::milliseconds(200)) {
				MyConfigSaver::SaveConfig("_autosave.config");
				MyConfigSaver::ClearDirty();
				lastAutoSave = now;
			}
		}

	}
	catch (std::exception const& e)
	{
		LOG_ERROR("Render", "Cheats::Run exception: {}", e.what());
	}
	catch (...)
	{
		LOG_ERROR("Render", "Cheats::Run unknown exception");
	}
}

#include "Render.h"
#include <cctype>
#include <string>



namespace Render

{




	void DrawDistance(const CEntity& LocalEntity, const CEntity& Entity, ImVec4 Rect)

	{

		if (!IsValidRect(Rect)) return;
		if (!IsValidFloat(Entity.Pawn.Pos.x) || !IsValidFloat(LocalEntity.Pawn.Pos.x)) return;
		int distance = static_cast<int>(Entity.Pawn.Pos.DistanceTo(LocalEntity.Pawn.Pos) / 100);

		std::string dis_str = std::to_string(distance) + "m";

		Gui.StrokeText(dis_str, Vec2(Rect.x + Rect.z + 4, Rect.y), MenuConfig::DistanceColor, MenuConfig::DistanceFontSize, false);

	}





	ImVec4 Get2DBox(const CEntity& Entity)

	{

		const CBone& bone = Entity.GetBone();

		// Safety check: ensure bone list has enough elements for head index

		if (bone.BonePosCount <= BONEINDEX::head) {

			return ImVec4{ 0, 0, 0, 0 };

		}



		const BoneJointPos& Head = bone.BonePosList[BONEINDEX::head];

		if (!Head.IsVisible) return ImVec4{ 0, 0, 0, 0 };



		Vec2 Size, Pos;

		Size.y = (Entity.Pawn.ScreenPos.y - Head.ScreenPos.y) * 1.09f;

		Size.x = Size.y * 0.6f;



		// Reject invalid boxes (negative/zero size or extreme dimensions)

		if (Size.y < 4.f || Size.x < 2.f || Size.y > 4000.f) return ImVec4{ 0, 0, 0, 0 };



		Pos = ImVec2(Entity.Pawn.ScreenPos.x - Size.x / 2, Head.ScreenPos.y - Size.y * 0.18f);



		return ImVec4{ Pos.x,Pos.y,Size.x,Size.y };

	}




	void DrawBone(const CEntity& Entity, ImColor Color, float Thickness,
	              const bool* boneVisibility,
	              ImColor visColor, ImColor hidColor)

	{

		CBone bone = Entity.GetBone();
		if (bone.BonePosCount <= 0) return;

		BoneJointPos Previous, Current;

		for (auto i : BoneJointList::List)

		{

			Previous.Pos = Vec3(0, 0, 0);

			for (auto Index : i)

			{

				if ((int)Index >= bone.BonePosCount) continue;

				Current = bone.BonePosList[Index];

				if (Previous.Pos == Vec3(0, 0, 0))

				{

					Previous = Current;

					continue;

				}

				if (Previous.IsVisible && Current.IsVisible)

				{

					ImColor lineColor = Color;

					if (boneVisibility)

						lineColor = boneVisibility[Index] ? visColor : hidColor;

					Gui.Line(Previous.ScreenPos, Current.ScreenPos, lineColor, Thickness);

				}

				Previous = Current;

			}

		}

	}



	void ShowLosLine(const CEntity& Entity, const float Length, ImColor Color, float Thickness, const float Matrix[4][4])
	{
		if (!IsValidFloat(Length) || Length <= 0.f) return;
		const CBone& losbone = Entity.GetBone();
		if (losbone.BonePosCount <= BONEINDEX::head) return;

		BoneJointPos Head = Entity.GetBone().BonePosList[BONEINDEX::head];
		if (!Head.IsVisible) return;

		Vec2 StartPoint = Head.ScreenPos;

		float LineLength = cos(Entity.Pawn.ViewAngle.x * M_PI / 180) * Length;

		Vec3 Temp;
		Temp.x = Head.Pos.x + cos(Entity.Pawn.ViewAngle.y * M_PI / 180) * LineLength;
		Temp.y = Head.Pos.y + sin(Entity.Pawn.ViewAngle.y * M_PI / 180) * LineLength;
		Temp.z = Head.Pos.z - sin(Entity.Pawn.ViewAngle.x * M_PI / 180) * Length;

		Vec2 EndPoint;
		if (!CView::WorldToScreen(Matrix, Temp, EndPoint))
			return;

		Gui.Line(StartPoint, EndPoint, Color, Thickness);
	}



	ImVec4 Get2DBoneRect(const CEntity& Entity)

	{

		const CBone& bone = Entity.GetBone();

		// Find first visible bone to seed Min/Max

		bool found = false;

		Vec2 Min{}, Max{};

		for (int i = 0; i < CBone::NUM_BONES; i++)

		{

			if (!bone.BonePosList[i].IsVisible) continue;

			if (!found) {

				Min = Max = bone.BonePosList[i].ScreenPos;

				found = true;

				continue;

			}

			Min.x = std::min(bone.BonePosList[i].ScreenPos.x, Min.x);

			Min.y = std::min(bone.BonePosList[i].ScreenPos.y, Min.y);

			Max.x = std::max(bone.BonePosList[i].ScreenPos.x, Max.x);

			Max.y = std::max(bone.BonePosList[i].ScreenPos.y, Max.y);

		}

		if (!found) return ImVec4{ 0, 0, 0, 0 };



		Vec2 Size;

		Size.x = Max.x - Min.x;

		Size.y = Max.y - Min.y;



		// Reject zero/tiny/extreme boxes

		if (Size.y < 4.f || Size.x < 2.f || Size.y > 4000.f) return ImVec4{ 0, 0, 0, 0 };



		return ImVec4(Min.x, Min.y, Size.x, Size.y);

	}






	void HealthBar::DrawHealthBar(float MaxHealth, float CurrentHealth, ImVec2 Pos, ImVec2 Size, bool vertical)

	{

		if (MaxHealth <= 0.f || !IsValidFloat(MaxHealth) || !IsValidFloat(CurrentHealth)) return;
		if (!IsValidScreenPos(Pos) || !IsValidFloat(Size.x) || !IsValidFloat(Size.y)) return;
		if (Size.x < 1.f || Size.y < 1.f) return;
		CurrentHealth = std::clamp(CurrentHealth, 0.f, MaxHealth);

		auto InRange = [&](float value, float min, float max) -> bool

		{

			return value > min && value <= max;

		};



		ImDrawList* DrawList = ImGui::GetBackgroundDrawList();



		this->MaxHealth = MaxHealth;

		this->CurrentHealth = CurrentHealth;

		this->RectPos = Pos;

		this->RectSize = Size;



		float Proportion = CurrentHealth / MaxHealth;

		// Fill extent along the bar's primary axis: horizontal uses width,
		// vertical uses height (drawn from the bottom upward).
		float FillExtent = vertical ? (RectSize.y * Proportion) : (RectSize.x * Proportion);

		ImColor Color;

		DrawList->AddRectFilled(RectPos,

			{ RectPos.x + RectSize.x,RectPos.y + RectSize.y },

			BackGroundColor, 5, 15);

		float Color_Lerp_t = pow(Proportion, 2.5);

		if (InRange(Proportion, 0.5, 1))

			Color = Mix(FirstStageColor, SecondStageColor, Color_Lerp_t * 3 - 1);

		else

			Color = Mix(SecondStageColor, ThirdStageColor, Color_Lerp_t * 4);



		if (LastestBackupHealth == 0

			|| LastestBackupHealth < CurrentHealth)

			LastestBackupHealth = CurrentHealth;



		if (LastestBackupHealth != CurrentHealth)

		{

			if (!InShowBackupHealth)

			{

				BackupHealthTimePoint = std::chrono::steady_clock::now();

				InShowBackupHealth = true;

			}



			std::chrono::steady_clock::time_point CurrentTime = std::chrono::steady_clock::now();

			if (CurrentTime - BackupHealthTimePoint > std::chrono::milliseconds(ShowBackUpHealthDuration))

			{

				LastestBackupHealth = CurrentHealth;

				InShowBackupHealth = false;

			}



			if (InShowBackupHealth)

			{

				float BackupHealthExtent = LastestBackupHealth / MaxHealth * (vertical ? RectSize.y : RectSize.x);

				float BackupHealthColorAlpha = 1 - 0.95 * (std::chrono::duration_cast<std::chrono::milliseconds>(CurrentTime - BackupHealthTimePoint).count() / (float)ShowBackUpHealthDuration);

				ImColor BackupHealthColorTemp = BackupHealthColor;

				BackupHealthColorTemp.Value.w = BackupHealthColorAlpha;

				float BackupHealthExtent_Lerp = 1 * (std::chrono::duration_cast<std::chrono::milliseconds>(CurrentTime - BackupHealthTimePoint).count() / (float)ShowBackUpHealthDuration);

				BackupHealthExtent_Lerp *= (BackupHealthExtent - FillExtent);

				BackupHealthExtent -= BackupHealthExtent_Lerp;

				if (vertical)

				{

					DrawList->AddRectFilled({ RectPos.x,RectPos.y + RectSize.y - BackupHealthExtent },

						{ RectPos.x + RectSize.x,RectPos.y + RectSize.y },

						BackupHealthColorTemp, 5);

				}

				else

				{

					DrawList->AddRectFilled(RectPos,

						{ RectPos.x + BackupHealthExtent,RectPos.y + RectSize.y },

						BackupHealthColorTemp, 5);

				}

			}

		}



		if (vertical)

		{

			DrawList->AddRectFilled({ RectPos.x,RectPos.y + RectSize.y - FillExtent },

				{ RectPos.x + RectSize.x,RectPos.y + RectSize.y },

				Color, 5);

		}

		else

		{

			DrawList->AddRectFilled(RectPos,

				{ RectPos.x + FillExtent,RectPos.y + RectSize.y },

				Color, 5);

		}



		DrawList->AddRect(RectPos,

			{ RectPos.x + RectSize.x,RectPos.y + RectSize.y },

			FrameColor, 5, 15, 1);

	}



	ImColor HealthBar::Mix(ImColor Col_1, ImColor Col_2, float t)

	{

		ImColor Col;

		Col.Value.x = t * Col_1.Value.x + (1 - t) * Col_2.Value.x;

		Col.Value.y = t * Col_1.Value.y + (1 - t) * Col_2.Value.y;

		Col.Value.z = t * Col_1.Value.z + (1 - t) * Col_2.Value.z;

		Col.Value.w = Col_1.Value.w;

		return Col;

	}



	void DrawHealthBar(DWORD Sign, float MaxHealth, float CurrentHealth, ImVec2 Pos, ImVec2 Size, bool Horizontal)
	{
		if (MaxHealth <= 0.f || CurrentHealth < 0.f || Sign == 0) return;
		if (!IsValidFloat(CurrentHealth) || !IsValidScreenPos(Pos)) return;
		static std::unordered_map<DWORD, HealthBar> HealthBarMap;
		if (!HealthBarMap.count(Sign))
		{
			HealthBarMap.insert({ Sign,HealthBar() });
		}
		if (HealthBarMap.count(Sign))
		{
			HealthBarMap[Sign].DrawHealthBar(MaxHealth, CurrentHealth, Pos, Size, !Horizontal);
		}
	}

	// ================================================================
	//  Corner-style box (only corners drawn, not full edges)
	// ================================================================
	void DrawCornerBox(ImVec4 Rect, ImColor Color, float Thickness, float CornerFrac)
	{
		if (!IsValidRect(Rect)) return;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		float x = Rect.x, y = Rect.y, w = Rect.z, h = Rect.w;
		float lx = w * CornerFrac, ly = h * CornerFrac;

		// top-left
		dl->AddLine({ x, y }, { x + lx, y }, Color, Thickness);
		dl->AddLine({ x, y }, { x, y + ly }, Color, Thickness);
		// top-right
		dl->AddLine({ x + w, y }, { x + w - lx, y }, Color, Thickness);
		dl->AddLine({ x + w, y }, { x + w, y + ly }, Color, Thickness);
		// bottom-left
		dl->AddLine({ x, y + h }, { x + lx, y + h }, Color, Thickness);
		dl->AddLine({ x, y + h }, { x, y + h - ly }, Color, Thickness);
		// bottom-right
		dl->AddLine({ x + w, y + h }, { x + w - lx, y + h }, Color, Thickness);
		dl->AddLine({ x + w, y + h }, { x + w, y + h - ly }, Color, Thickness);
	}

	// ================================================================
	//  Head dot (circle on head bone position)
	// ================================================================
	void DrawHeadDot(const CEntity& Entity, ImColor Color, float Radius)
	{
		const CBone& bone = Entity.GetBone();
		if (bone.BonePosCount <= BONEINDEX::head) return;
		if (!bone.BonePosList[BONEINDEX::head].IsVisible) return;
		Vec2 headScreen = bone.BonePosList[BONEINDEX::head].ScreenPos;
		if (!IsValidFloat(headScreen.x) || !IsValidFloat(headScreen.y)) return;

		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		dl->AddCircleFilled({ headScreen.x, headScreen.y }, Radius, Color, 12);
		dl->AddCircle({ headScreen.x, headScreen.y }, Radius, ImColor(0, 0, 0, 200), 12, 1.f);
	}

	// ================================================================
	//  Armor bar (simple proportional bar)
	// ================================================================
	void DrawArmorBar(int Armor, ImVec4 Rect, ImColor Color, float BarWidth, int Type)
	{
		if (Armor <= 0 || Armor > 100) return;
		if (!IsValidRect(Rect)) return;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		float proportion = Armor / 100.f;
		ImColor bgCol(90, 90, 90, 160);
		ImColor frameCol(45, 45, 45, 180);

		if (Type == 0) {
			// vertical, right side of box
			float x = Rect.x + Rect.z + 2.f;
			float y = Rect.y;
			float h = Rect.w;
			dl->AddRectFilled({ x, y }, { x + BarWidth, y + h }, bgCol, 2);
			dl->AddRectFilled({ x, y + h * (1.f - proportion) }, { x + BarWidth, y + h }, Color, 2);
			dl->AddRect({ x, y }, { x + BarWidth, y + h }, frameCol, 2, 0, 1);
		}
		else {
			// horizontal, above box
			float x = Rect.x;
			float y = Rect.y - BarWidth - 2.f;
			float w = Rect.z;
			dl->AddRectFilled({ x, y }, { x + w, y + BarWidth }, bgCol, 2);
			dl->AddRectFilled({ x, y }, { x + w * proportion, y + BarWidth }, Color, 2);
			dl->AddRect({ x, y }, { x + w, y + BarWidth }, frameCol, 2, 0, 1);
		}
	}

	// ================================================================
	//  Snapline with configurable origin point
	// ================================================================
	void LineToEnemyEx(ImVec4 Rect, ImColor Color, float Thickness, int Origin)
	{
		if (!IsValidRect(Rect)) return;
		Vec2 entityTop;
		entityTop.x = Rect.x + Rect.z / 2;
		entityTop.y = Rect.y;
		Vec2 from;
		ImVec2 ds3 = ImGui::GetIO().DisplaySize;
		switch (Origin) {
		default:
		case 0: from.x = ds3.x / 2; from.y = 0; break;
		case 1: from.x = ds3.x / 2; from.y = ds3.y / 2; break;
		case 2: from.x = ds3.x / 2; from.y = ds3.y; break;
		}
		Gui.Line(entityTop, from, Color, Thickness);
	}

	// ================================================================
	//  Bomb ESP: dot + C4 text + timer + defuse info
	// ================================================================
	void DrawBombESP(const BombData& bomb, const float matrix[4][4])
	{
		if (bomb.x == 0 && bomb.y == 0) return;
		if (!IsValidFloat(bomb.x) || !IsValidFloat(bomb.y) || !IsValidFloat(bomb.z)) return;

		Vec3 bombWorld{ bomb.x, bomb.y, bomb.z };
		Vec2 screenPos;
		if (!CView::WorldToScreen(matrix, bombWorld, screenPos)) return;

		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		float sx = screenPos.x, sy = screenPos.y;

		// Base color for dot + "C4" label depends on bomb state:
		// planted -> BombPlantedColor, carried -> BombCarrierColor, dropped -> BombDroppedColor.
		ImColor baseCol;
		if (bomb.isPlanted)
			baseCol = MenuConfig::BombPlantedColor;
		else if (bomb.carrierPawnHandle != 0)
			baseCol = MenuConfig::BombCarrierColor;
		else
			baseCol = MenuConfig::BombDroppedColor;

		// Small dot
		dl->AddCircleFilled({ sx, sy }, 4.f, baseCol, 12);
		dl->AddCircle({ sx, sy }, 4.f, ImColor(0, 0, 0, 200), 12, 1.f);

		// "C4" label
		Gui.StrokeText("C4", Vec2(sx, sy - 18.f), baseCol, 14.f, true);

		float textY = sy + 8.f;

		// Timer (only when planted and ticking)
		if (bomb.isPlanted && !bomb.isDefused && bomb.blowTime > 0) {
			char timerBuf[32];
			snprintf(timerBuf, sizeof(timerBuf), "%.1fs", bomb.blowTime);

			// Color: BombPlantedColor > 10s, BombCarrierColor (warning) 5~10s, BombPlantedColor (red) < 5s
			ImColor timerCol;
			if (bomb.blowTime > 10.f)
				timerCol = MenuConfig::BombPlantedColor;
			else if (bomb.blowTime > 5.f)
				timerCol = MenuConfig::BombCarrierColor;
			else
				timerCol = MenuConfig::BombPlantedColor;

			Gui.StrokeText(timerBuf, Vec2(sx, textY), timerCol, 14.f, true);
			textY += 16.f;

			// Defuse info
			if (bomb.isDefusing) {
				// Kit detection: with kit = 5s, without = 10s
				// If defuseTime was initially <= ~5.5s, defuser has kit
				bool hasKit = (bomb.defuseTime <= 5.5f);
				char defBuf[64];
				if (hasKit)
					snprintf(defBuf, sizeof(defBuf), "DEFUSE %.1fs [KIT]", bomb.defuseTime);
				else
					snprintf(defBuf, sizeof(defBuf), "DEFUSE %.1fs", bomb.defuseTime);

				// Semantic green/red: can make it vs can't (not bound to config).
				bool canDefuse = bomb.blowTime > bomb.defuseTime;
				ImColor defCol = canDefuse ? ImColor(0, 220, 100, 255) : ImColor(255, 50, 50, 255);
				Gui.StrokeText(defBuf, Vec2(sx, textY), defCol, 14.f, true);
			}
		}

		if (bomb.isDefused) {
			Gui.StrokeText("DEFUSED", Vec2(sx, textY), MenuConfig::BombDefusingColor, 14.f, true);
		}
	}

	// ================================================================
	//  Grenade Projectile ESP
	//  Color-coded dots + name + remaining duration
	// ================================================================

	void DrawProjectileESP(const std::vector<GrenadeProjectile>& projectiles,
	                        const float matrix[4][4], const Vec3& localPos)
	{
		if (!IsValidFloat(localPos.x) || !IsValidFloat(localPos.y)) return;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		ImColor outline(0, 0, 0, 180);

		for (const auto& proj : projectiles) {
			const char* label = "";
			float maxDuration = 0.f;
			float remaining = 0.f;
			if (!ComputeProjectileTimer(proj, false, &label, &maxDuration, &remaining))
				continue;

			Vec2 screenPos;
			if (!CView::WorldToScreen(matrix, proj.Position, screenPos))
				continue;

			// Per-type color. rangeCol alpha is driven by config.
		ImU32 rangeAlphaByte = (ImU32)std::clamp(MenuConfig::ProjectileRangeAlpha * 255.f, 0.f, 255.f);
		ImColor dotCol, rangeCol;
		switch (proj.Type) {
			case PROJ_SMOKE:  dotCol = ImColor(100, 180, 255, 255); rangeCol = ImColor(100, 180, 255, rangeAlphaByte); break;
			case PROJ_FLASH:  dotCol = ImColor(255, 255, 80, 255);  rangeCol = ImColor(255, 255, 80, rangeAlphaByte); break;
			case PROJ_HE:     dotCol = ImColor(255, 60, 60, 255);   rangeCol = ImColor(255, 60, 60, rangeAlphaByte); break;
			case PROJ_MOLOTOV:dotCol = ImColor(255, 140, 0, 255);   rangeCol = ImColor(255, 140, 0, rangeAlphaByte); break;
			case PROJ_DECOY:  dotCol = ImColor(180, 180, 180, 255); rangeCol = ImColor(180, 180, 180, rangeAlphaByte); break;
			default: continue;
		}

			// Colored dot
			dl->AddCircleFilled({ screenPos.x, screenPos.y }, 5.f, dotCol, 12);
			dl->AddCircle({ screenPos.x, screenPos.y }, 5.f, outline, 12, 1.2f);

			// Label + optional timer
			char text[32];
			bool showTimer = maxDuration > 0.f && (proj.StationaryTimer > 0.5f || !proj.Alive);
			if (showTimer) {
				snprintf(text, sizeof(text), "%s %.1fs", label, remaining);
			} else {
				snprintf(text, sizeof(text), "%s", label);
			}
			Gui.StrokeText(text, Vec2(screenPos.x, screenPos.y - 18.f), dotCol, 13.f, true);

			// Range circle outline (gated by config).
			bool showRange = MenuConfig::ShowProjectileRange;
			if (showRange && proj.EffectRadius > 0.f) {
				constexpr int CIRCLE_SEGMENTS = 48;
				Vec2 prevScreen;
				bool prevValid = false;

				for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
					float angle = (float)i / CIRCLE_SEGMENTS * 2.f * 3.14159265f;
					Vec3 worldPt;
					worldPt.x = proj.Position.x + cosf(angle) * proj.EffectRadius;
					worldPt.y = proj.Position.y + sinf(angle) * proj.EffectRadius;
					worldPt.z = proj.Position.z;

					Vec2 sp;
					bool valid = CView::WorldToScreen(matrix, worldPt, sp);
					if (valid && prevValid)
						dl->AddLine({ prevScreen.x, prevScreen.y }, { sp.x, sp.y }, rangeCol, 2.0f);
					prevScreen = sp;
					prevValid = valid;
				}
			}
		}
	}

	// ================================================================
	// ESP text outline helper (formerly DrawTextShadow).
	// Draws an 8-direction stroke behind the text, gated by the master
	// toggle MenuConfig::TextOutlineEnabled. Stroke color/alpha and pixel
	// offset come from MenuConfig::TextOutlineColor / TextOutlineThickness.
	// When the toggle is off (or thickness <= 0), text is drawn plain.
	// The function name is retained to avoid churning all call sites.
	// ================================================================
	void DrawTextShadow(ImDrawList* drawList, ImFont* font, float fontSize,
	                     const ImVec2& pos, ImU32 color, const char* text,
	                     const char* textEnd)
	{
		if (!drawList || !text || text[0] == '\0') return;

		if (fontSize > 0.f && font) ImGui::PushFont(font);

		if (MenuConfig::TextOutlineEnabled) {
			const float t = MenuConfig::TextOutlineThickness;
			if (t > 0.f) {
				const ImU32 oc = MenuConfig::TextOutlineColor;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) continue;
						drawList->AddText(ImVec2(pos.x + dx * t, pos.y + dy * t), oc, text, textEnd);
					}
				}
			}
		}
		drawList->AddText(pos, color, text, textEnd);

		if (fontSize > 0.f && font) ImGui::PopFont();
	}

	// ================================================================
	// ESP gap-closure stage 3a: player status flags (Task 8).
	// Draws Blind/Scoped/Defusing/Kit/Money labels stacked vertically
	// to the right of the player's 2D box. Each flag has its own toggle
	// and color in MenuConfig.
	// ================================================================
	void DrawPlayerFlags(ImDrawList* drawList, const CEntity& entity,
	                     const ImVec2& boxMin, const ImVec2& boxMax, float fontSize)
	{
		if (!drawList) return;

		float flagX = boxMax.x + 6.0f;
		float flagY = boxMin.y;
		float lineH = fontSize > 0.f ? fontSize : 12.f;

		auto drawFlag = [&](const char* text, ImColor color) {
			if (!text || text[0] == '\0') return;
			DrawTextShadow(drawList, nullptr, 0.f, ImVec2(flagX, flagY),
			               ImGui::ColorConvertFloat4ToU32(color.Value), text);
			flagY += lineH + 1.0f;
		};

		if (MenuConfig::FlagBlindEnabled && entity.Pawn.FlashDuration > 0.f)
			drawFlag("Blind", MenuConfig::FlagBlindColor);
		if (MenuConfig::FlagScopedEnabled && entity.Pawn.Scoped)
			drawFlag("Scoped", MenuConfig::FlagScopedColor);
		if (MenuConfig::FlagDefusingEnabled && entity.Pawn.Defusing)
			drawFlag("Defusing", MenuConfig::FlagDefusingColor);
		if (MenuConfig::FlagKitEnabled && entity.Pawn.HasDefuser)
			drawFlag("Kit", MenuConfig::FlagKitColor);
		if (MenuConfig::FlagMoneyEnabled && entity.Controller.Money > 0) {
			char moneyBuf[16];
			snprintf(moneyBuf, sizeof(moneyBuf), "$%d", entity.Controller.Money);
			drawFlag(moneyBuf, MenuConfig::FlagMoneyColor);
		}
	}

	// ================================================================
	// ESP gap-closure stage 3a: sound ESP ripple (Task 10).
	// Draws an expanding double-ring circle at a player's feet when a
	// shot was recently fired. Progress 0->1 over 420ms; alpha fades out.
	// ================================================================
	void DrawSoundESP(ImDrawList* drawList, const Vec2& screenPos,
	                  uint64_t nowMs, uint64_t soundUntilMs, ImU32 color)
	{
		if (!drawList) return;
		if (nowMs >= soundUntilMs) return;
		if (!IsValidFloat(screenPos.x) || !IsValidFloat(screenPos.y)) return;

		float remain = (float)(soundUntilMs - nowMs);
		float progress = 1.f - remain / 420.f;
		if (progress < 0.f) progress = 0.f;
		if (progress > 1.f) progress = 1.f;

		float radiusA = 10.f + 18.f * progress;
		float radiusB = radiusA + 7.f;
		int alphaA = (int)((1.f - progress) * 200.f);
		int alphaB = (int)((1.f - progress) * 120.f);
		if (alphaA < 0) alphaA = 0;
		if (alphaB < 0) alphaB = 0;

		ImU32 baseRGB = color & 0x00FFFFFF;
		ImU32 colA = baseRGB | ((ImU32)alphaA << 24);
		ImU32 colB = baseRGB | ((ImU32)alphaB << 24);

		drawList->AddCircle(ImVec2(screenPos.x, screenPos.y), radiusA, colA, 32, 2.0f);
		drawList->AddCircle(ImVec2(screenPos.x, screenPos.y), radiusB, colB, 32, 2.0f);
	}

	// ================================================================
	// Footstep ESP: persistent pulsing circle at a player's feet while
	// they are moving on ground. Radius oscillates 16-24px over 600ms;
	// alpha is constant (caller only invokes this while isMoving).
	// ================================================================
	void DrawFootstepESP(ImDrawList* drawList, const Vec2& screenPos,
	                     uint64_t nowMs, ImU32 color)
	{
		if (!drawList) return;
		if (!IsValidFloat(screenPos.x) || !IsValidFloat(screenPos.y)) return;

		constexpr float PI = 3.14159265358979323846f;
		float radius = 20.f + 4.f * sinf((float)nowMs * 0.001f * 2.f * PI / 0.6f);
		if (radius < 1.f) radius = 1.f;

		drawList->AddCircle(ImVec2(screenPos.x, screenPos.y), radius, color, 36, 2.0f);
	}

	// ================================================================
	// ESP gap-closure stage 3b: C4 bomb timer overlay (Task 11).
	// Draggable ImGui window with C4 countdown + defuse progress bars.
	// The displayed countdown is smoothed per-frame (s_bombDisplayedLeft)
	// and re-synced to the raw value when the drift exceeds 0.5s.
	// ================================================================
	void DrawBombTimerOverlay(ImDrawList* drawList, float screenW, float screenH,
	                          float bombLeftSec, float bombTotalSec,
	                          bool defusing, float defuseLeftSec, float defuseTotalSec)
	{
		(void)drawList; // window uses its own draw list

		// Smoothed display values (persist across frames).
		static float s_bombDisplayedLeft = 0.f;
		static float s_defuseDisplayedLeft = 0.f;
		static bool  s_prevVisible = false;
		static bool  s_prevDefusing = false;
		// Explosion suppression window: pins the display at 0.0s for 1.75s
		// after the countdown reaches zero to avoid the overlay flickering
		// between 0.0s and the raw feed while the game state settles.
		static float s_bombExplodedTimer = -1.f; // -1 = inactive, >=0 = suppression active

		const float frameDt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.10f);
		const float bombTotal = (bombTotalSec > 1.f && std::isfinite(bombTotalSec)) ? bombTotalSec : 40.f;

		// Smooth C4 countdown: decrement by frameDt, re-sync on >0.5s drift.
		float rawBomb = std::clamp(bombLeftSec, 0.f, bombTotal);
		float prevDisplayed = s_bombDisplayedLeft;

		if (s_bombExplodedTimer >= 0.f) {
			// Suppression window active: pin display at 0.0s, unless a fresh
			// bomb appears (raw feed jumps well above 0).
			if (rawBomb > 1.f) {
				s_bombExplodedTimer = -1.f;
				s_bombDisplayedLeft = rawBomb;
			} else {
				s_bombDisplayedLeft = 0.f;
				s_bombExplodedTimer += frameDt;
				if (s_bombExplodedTimer > 1.75f) {
					s_bombExplodedTimer = -1.f;
					s_prevVisible = false; // force re-init when the next bomb appears
				}
			}
		} else if (!s_prevVisible || s_bombDisplayedLeft <= 0.f) {
			s_bombDisplayedLeft = rawBomb;
		} else {
			s_bombDisplayedLeft = std::max(0.f, s_bombDisplayedLeft - frameDt);
			if (std::fabs(rawBomb - s_bombDisplayedLeft) > 0.5f)
				s_bombDisplayedLeft = rawBomb;
		}

		// Detect explosion: displayed countdown transitioned from positive to zero.
		if (s_bombExplodedTimer < 0.f && s_prevVisible &&
		    prevDisplayed > 0.f && s_bombDisplayedLeft <= 0.f) {
			s_bombExplodedTimer = 0.f;
			s_bombDisplayedLeft = 0.f;
		}
		s_prevVisible = true;

		// Smooth defuse countdown the same way.
		if (defusing) {
			float rawDefuse = defuseTotalSec > 0.f ? std::clamp(defuseLeftSec, 0.f, defuseTotalSec) : 0.f;
			if (!s_prevDefusing || s_defuseDisplayedLeft <= 0.f) {
				s_defuseDisplayedLeft = rawDefuse;
			} else {
				s_defuseDisplayedLeft = std::max(0.f, s_defuseDisplayedLeft - frameDt);
				if (std::fabs(rawDefuse - s_defuseDisplayedLeft) > 0.4f)
					s_defuseDisplayedLeft = rawDefuse;
			}
			s_prevDefusing = true;
		} else {
			s_prevDefusing = false;
			s_defuseDisplayedLeft = 0.f;
		}

		// Default to top-right corner on first call.
		if (MenuConfig::BombTimerX < 0.f || MenuConfig::BombTimerY < 0.f) {
			MenuConfig::BombTimerX = std::max(0.f, screenW - 230.f);
			MenuConfig::BombTimerY = 10.f;
		}
		// Clamp to visible screen area.
		MenuConfig::BombTimerX = std::clamp(MenuConfig::BombTimerX, 0.f, std::max(0.f, screenW - 230.f));
		MenuConfig::BombTimerY = std::clamp(MenuConfig::BombTimerY, 0.f, std::max(0.f, screenH - 90.f));

		ImGui::SetNextWindowPos(ImVec2(MenuConfig::BombTimerX, MenuConfig::BombTimerY), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 5.0f));

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
		                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
		if (!MenuConfig::ShowMenu)
			flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

		if (ImGui::Begin("##BombTimerOverlay", nullptr, flags))
		{
			// Dragging (only when menu is open).
			static bool s_dragging = false;
			if (MenuConfig::ShowMenu) {
				if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
				    ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					s_dragging = true;
				if (s_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
					ImVec2 delta = ImGui::GetIO().MouseDelta;
					MenuConfig::BombTimerX += delta.x;
					MenuConfig::BombTimerY += delta.y;
					ImGui::SetWindowPos(ImVec2(MenuConfig::BombTimerX, MenuConfig::BombTimerY), ImGuiCond_Always);
				} else if (s_dragging) {
					s_dragging = false;
				}
			}

			ImDrawList* wdl = ImGui::GetWindowDrawList();
			const ImVec2 titlePos = ImGui::GetCursorScreenPos();
			const float barW = 204.f;
			const float barH = 8.f;

			// Title row: "C4 XX.Xs" with a colored accent stripe.
			char title[48];
			snprintf(title, sizeof(title), "C4  %.1fs", (double)s_bombDisplayedLeft);
			bool bombCritical = s_bombDisplayedLeft <= 10.f;
			ImU32 titleAccent = bombCritical ? IM_COL32(255, 82, 72, 240) : IM_COL32(255, 192, 72, 240);
			ImU32 titleText   = bombCritical ? IM_COL32(255, 104, 96, 250) : IM_COL32(255, 202, 82, 250);
			wdl->AddRectFilled(ImVec2(titlePos.x, titlePos.y + 2.f),
			                   ImVec2(titlePos.x + 3.f, titlePos.y + 17.f), titleAccent, 2.f);
			wdl->AddText(ImVec2(titlePos.x + 10.f, titlePos.y), titleText, title);
			ImGui::Dummy(ImVec2(barW, ImGui::GetTextLineHeight()));

			// C4 progress bar.
			const ImVec2 barPos = ImGui::GetCursorScreenPos();
			ImU32 bombBarCol  = bombCritical ? IM_COL32(255, 82, 72, 245)  : IM_COL32(255, 194, 72, 245);
			ImU32 bombTrackCol= bombCritical ? IM_COL32(74, 24, 24, 150)   : IM_COL32(72, 55, 22, 150);
			wdl->AddRectFilled(ImVec2(barPos.x - 1.f, barPos.y - 1.f),
			                   ImVec2(barPos.x + barW + 1.f, barPos.y + barH + 1.f),
			                   (bombBarCol & 0x00FFFFFF) | (70u << 24), 5.f);
			wdl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH), bombTrackCol, 4.f);
			float bombFrac = std::clamp(s_bombDisplayedLeft / std::max(1.f, bombTotal), 0.f, 1.f);
			if (bombFrac > 0.f)
				wdl->AddRectFilled(barPos, ImVec2(barPos.x + std::max(barH, barW * bombFrac), barPos.y + barH), bombBarCol, 4.f);
			ImGui::Dummy(ImVec2(barW, barH));

			// Defuse progress bar (only while defusing).
			if (defusing && s_defuseDisplayedLeft > 0.f) {
				char defText[56];
				snprintf(defText, sizeof(defText), "Defuse  %.1fs", (double)s_defuseDisplayedLeft);
				bool canFinish = s_defuseDisplayedLeft <= s_bombDisplayedLeft;
				ImU32 defTextCol = canFinish ? IM_COL32(100, 255, 132, 245) : IM_COL32(255, 92, 82, 245);
				const ImVec2 defTextPos = ImGui::GetCursorScreenPos();
				wdl->AddText(defTextPos, defTextCol, defText);
				ImGui::Dummy(ImVec2(barW, ImGui::GetTextLineHeight()));

				const ImVec2 defBarPos = ImGui::GetCursorScreenPos();
				ImU32 defBarCol   = canFinish ? IM_COL32(72, 235, 108, 245) : IM_COL32(255, 82, 72, 245);
				ImU32 defTrackCol = canFinish ? IM_COL32(19, 62, 30, 150)  : IM_COL32(74, 24, 24, 150);
				wdl->AddRectFilled(ImVec2(defBarPos.x - 1.f, defBarPos.y - 1.f),
				                   ImVec2(defBarPos.x + barW + 1.f, defBarPos.y + barH + 1.f),
				                   (defBarCol & 0x00FFFFFF) | (70u << 24), 5.f);
				wdl->AddRectFilled(defBarPos, ImVec2(defBarPos.x + barW, defBarPos.y + barH), defTrackCol, 4.f);
				float defTotal = defuseTotalSec > 0.f ? defuseTotalSec : 10.f;
				float defFrac = std::clamp(s_defuseDisplayedLeft / std::max(1.f, defTotal), 0.f, 1.f);
				if (defFrac > 0.f)
					wdl->AddRectFilled(defBarPos, ImVec2(defBarPos.x + std::max(barH, barW * defFrac), defBarPos.y + barH), defBarCol, 4.f);
				ImGui::Dummy(ImVec2(barW, barH));
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	// ================================================================
	// ESP gap-closure stage 3b: world ESP (Task 12).
	// Draws grenade effect timers (Smoke/Inferno/Decoy) above the
	// existing projectile dots. nowUs is steady_clock microseconds.
	// ================================================================
	void DrawWorldESP(ImDrawList* drawList, const std::vector<GrenadeProjectile>& projectiles,
	                  const float matrix[4][4], uint64_t nowUs)
	{
		if (!drawList) return;
		(void)nowUs; // remaining time is derived from StationaryTimer/DisappearTimer

		for (const auto& proj : projectiles) {
			const char* label = nullptr;
			float maxDuration = 0.f;
			float remaining = 0.f;
			if (!ComputeProjectileTimer(proj, true, &label, &maxDuration, &remaining))
				continue;

			// Per-type sub-switches (Task 12): master toggle + per-type toggle
			bool typeEnabled = (proj.Type == PROJ_SMOKE   && MenuConfig::ShowWorldSmokeTimer)
			                || (proj.Type == PROJ_MOLOTOV && MenuConfig::ShowWorldInfernoTimer)
			                || (proj.Type == PROJ_DECOY   && MenuConfig::ShowWorldDecoyTimer);
			if (!MenuConfig::ShowWorldProjectileTimers || !typeEnabled) continue;

			// Effect timer only applies once the projectile has settled
			// (StationaryTimer > 0.5s) or after it disappeared (lingering
			// effect). In-flight projectiles show no timer.
			bool showTimer = (proj.StationaryTimer > 0.5f || !proj.Alive);
			if (!showTimer) continue;

			Vec2 screenPos;
			if (!CView::WorldToScreen(matrix, proj.Position, screenPos))
				continue;

			// Per-type text color (label/maxDuration came from ComputeProjectileTimer).
			ImU32 textCol = 0;
			switch (proj.Type) {
				case PROJ_SMOKE:   textCol = IM_COL32(180, 180, 220, 255); break;
				case PROJ_MOLOTOV: textCol = IM_COL32(255, 120, 40, 255); break;
				case PROJ_DECOY:   textCol = IM_COL32(200, 200, 80, 255); break;
				default: continue;
			}

			char text[32];
			snprintf(text, sizeof(text), "%s %.1fs", label, remaining);

			ImU32 labelCol = MenuConfig::ShowWorldProjectileTimers
				? textCol
				: ImGui::ColorConvertFloat4ToU32(MenuConfig::WorldESPColor.Value);
			DrawTextShadow(drawList, nullptr, 0.f,
			               ImVec2(screenPos.x, screenPos.y - 30.f), labelCol, text);
		}
	}

	// ================================================================
	// ESP gap-closure stage 3b: weapon ammo ESP (Task 13).
	// Returns the magazine capacity for common CS2 weapons by name.
	// Unknown weapons return 0 (only current ammo is shown).
	// ================================================================
	int WeaponMaxClip(const std::string& weaponName)
	{
		// Lowercase compare for robustness.
		std::string n = weaponName;
		std::transform(n.begin(), n.end(), n.begin(),
		               [](unsigned char c) { return (char)std::tolower(c); });

		if (n == "ak47")                       return 30;
		if (n == "m4a1" || n == "m4a1_silencer") return 30;
		if (n == "awp")                        return 10;
		if (n == "deagle")                     return 7;
		if (n == "glock" || n == "usp_silencer") return 20;
		if (n == "p250")                       return 13;
		if (n == "fiveseven")                  return 20;
		if (n == "tec9")                       return 18;
		if (n == "cz75a")                      return 12;
		if (n == "revolver")                   return 8;
		if (n == "mp9" || n == "mac10")        return 30;
		if (n == "mp7" || n == "ump45")        return 25;
		if (n == "p90")                        return 50;
		if (n == "bizon")                      return 64;
		if (n == "famas" || n == "galilar")    return 25;
		if (n == "sg556" || n == "aug")        return 30;
		if (n == "ssg08")                      return 10;
		if (n == "scar20" || n == "g3sg1")     return 20;
		if (n == "m249" || n == "negev")       return 100;
		if (n == "nova" || n == "xm1014")      return 8;
		if (n == "sawedoff" || n == "mag7")    return 8;
		return 0;
	}

	void DrawWeaponAmmo(ImDrawList* drawList, const ImVec2& pos, int ammoClip, int maxClip,
	                    float fontSize, ImU32 color, ImU32 lowColor)
	{
		if (!drawList) return;
		if (ammoClip < 0) return; // unknown ammo -> nothing to show

		char ammoText[32];
		if (maxClip > 0) {
			snprintf(ammoText, sizeof(ammoText), "Ammo %d/%d", ammoClip, maxClip);
		} else {
			snprintf(ammoText, sizeof(ammoText), "Ammo %d", ammoClip);
		}

		bool lowAmmo = (maxClip > 0) && (ammoClip <= std::max(1, maxClip / 5));
		ImU32 textCol = lowAmmo ? lowColor : color;
		DrawTextShadow(drawList, nullptr, 0.f, pos, textCol, ammoText);

		if (lowAmmo) {
			ImVec2 lowPos = ImVec2(pos.x, pos.y + fontSize + 1.f);
			DrawTextShadow(drawList, nullptr, 0.f, lowPos, lowColor, "LOW");
		}
	}

	// ================================================================
	// ESP gap-closure stage 3b: weapon icon ESP (Task 13).
	// Draws the active weapon's icon glyph (from weapons.ttf) at the given
	// position. When the icon font is missing or the weapon id is unknown
	// the function is a no-op so callers can fall back to text. Knives are
	// skipped when skipKnife is set (they share one glyph and add clutter).
	// ================================================================
	void DrawWeaponIcon(ImDrawList* drawList, const ImVec2& pos, uint16_t weaponId,
	                    float fontSize, ImU32 color, bool skipKnife)
	{
		if (!drawList) return;
		if (weaponId == 0) return;
		if (skipKnife && WeaponLookup::IsKnifeItemId(weaponId)) return;

		const char* icon = WeaponLookup::WeaponIconFromItemId(weaponId);
		if (!icon || icon[0] == '\0') return;

		ImFont* font = Gui.weaponIconFont;
		if (!font) return;

		// Scale the font to the requested size; the atlas was baked at 16px.
		font->Scale = fontSize / 16.0f;
		ImGui::PushFont(font);
		DrawTextShadow(drawList, font, fontSize, pos, color, icon);
		ImGui::PopFont();
		font->Scale = 1.0f;
	}

	// ================================================================
	// ESP gap-closure stage 3b: dropped-weapon world ESP (Task 12/16).
	// Renders icon + name for each dropped weapon on the ground. Weapons
	// are filtered by EspItemEnabledMask (C4 and knives are always hidden
	// to avoid duplicating the dedicated bomb ESP and cluttering the view).
	// ================================================================
	void DrawDroppedWeapons(ImDrawList* drawList, const std::vector<DroppedWeapon>& weapons,
	                        const float matrix[4][4])
	{
		if (!drawList) return;
		if (weapons.empty()) return;

		ImFont* iconFont = Gui.weaponIconFont;
		ImU32 nameCol = ImGui::ColorConvertFloat4ToU32(MenuConfig::WorldESPColor.Value);
		ImU32 iconCol = IM_COL32(240, 240, 240, 240);
		float iconSize = MenuConfig::WorldItemFontSize + 3.f;
		float nameSize = MenuConfig::WorldItemFontSize;

		for (const auto& w : weapons) {
			if (w.ItemId == 0) continue;
			// C4 has its own dedicated bomb ESP; knives add clutter.
			if (w.ItemId == 49) continue;
			if (WeaponLookup::IsKnifeItemId(w.ItemId)) continue;
			if (w.ItemId < MenuConfig::EspItemEnabledMask.size() &&
			    !MenuConfig::EspItemEnabledMask.test(w.ItemId))
				continue;

			if (!IsValidFloat(w.Position.x) || !IsValidFloat(w.Position.y) || !IsValidFloat(w.Position.z))
				continue;
			if (std::abs(w.Position.x) < 1.f && std::abs(w.Position.y) < 1.f && std::abs(w.Position.z) < 1.f)
				continue;

			Vec2 screenPos;
			if (!CView::WorldToScreen(matrix, w.Position, screenPos))
				continue;

			// Icon glyph above the name.
			const char* icon = WeaponLookup::WeaponIconFromItemId(w.ItemId);
			if (icon && icon[0] != '\0' && iconFont) {
				iconFont->Scale = iconSize / 16.0f;
				ImGui::PushFont(iconFont);
				ImVec2 iconBounds = iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon, nullptr);
				ImVec2 iconPos(screenPos.x - iconBounds.x * 0.5f, screenPos.y - iconBounds.y - nameSize - 4.f);
				DrawTextShadow(drawList, iconFont, iconSize, iconPos, iconCol, icon);
				ImGui::PopFont();
				iconFont->Scale = 1.0f;
			}

			// Name text below the icon.
			const char* name = w.Name.empty() ? WeaponLookup::WeaponNameFromItemId(w.ItemId) : w.Name.c_str();
			if (name && name[0] != '\0') {
				ImVec2 textBounds = ImGui::CalcTextSize(name);
				ImVec2 textPos(screenPos.x - textBounds.x * 0.5f, screenPos.y - textBounds.y * 0.5f);
				DrawTextShadow(drawList, nullptr, 0.f, textPos, nameCol, name);
			}
		}
	}

	// ================================================================
	// ESP gap-closure stage 3b: bar value label (Task 14).
	// Draws a numeric label with a semi-transparent background box above
	// a health/armor bar. The box has a 1px accent line at the bottom in
	// the bar's color. screenW is used for edge avoidance.
	// ================================================================
	void DrawBarLabel(ImDrawList* drawList, const ImVec2& barPos, float barHeight,
	                  int value, ImU32 barColor, float fontSize, float screenW)
	{
		if (!drawList) return;
		if (value < 0) return;

		char text[8];
		snprintf(text, sizeof(text), "%d", value);

		ImFont* font = ImGui::GetFont();
		float fs = fontSize > 0.f ? fontSize : 11.f;
		ImVec2 textSize = font
			? font->CalcTextSizeA(fs, FLT_MAX, 0.f, text)
			: ImGui::CalcTextSize(text);

		const float padX = 2.5f;
		const float padY = 1.0f;
		float labelW = textSize.x + padX * 2.f;
		float labelH = textSize.y + padY * 2.f;

		// Center horizontally over the bar, clamp to screen edges.
		float bgX = (barPos.x + barHeight * 0.5f) - labelW * 0.5f;
		float bgY = barPos.y - labelH - 3.f;
		if (bgX < 2.f) bgX = 2.f;
		if (bgX + labelW > screenW - 2.f) bgX = screenW - labelW - 2.f;
		if (bgY < 2.f) bgY = 2.f;

		ImVec2 bgMin(bgX, bgY);
		ImVec2 bgMax(bgX + labelW, bgY + labelH);
		ImVec2 textPos(bgX + padX, bgY + padY - 0.25f);

		// Background + outline.
		drawList->AddRectFilled(bgMin, bgMax, IM_COL32(8, 8, 8, 205), 2.5f);
		drawList->AddRect(bgMin, bgMax, IM_COL32(0, 0, 0, 150), 2.5f, 0, 1.f);
		// 1px accent line at the bottom in the bar's color.
		drawList->AddRectFilled(ImVec2(bgMin.x + 1.f, bgMax.y - 2.f),
		                        ImVec2(bgMax.x - 1.f, bgMax.y - 1.f), barColor, 1.f);

		// Text with shadow.
		if (font)
			drawList->AddText(font, fs, ImVec2(textPos.x + 1.f, textPos.y + 1.f),
			                  IM_COL32(0, 0, 0, 210), text);
		else
			drawList->AddText(ImVec2(textPos.x + 1.f, textPos.y + 1.f),
			                  IM_COL32(0, 0, 0, 210), text);
		if (font)
			drawList->AddText(font, fs, textPos, IM_COL32(255, 255, 255, 255), text);
		else
			drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
	}

	// ================================================================
	// ESP audit stage 2: unified health-color from ratio (Task 8).
	// ================================================================
	ImU32 HealthColorFromRatio(int health)
	{
		float hpRatio = std::clamp((float)health / 100.f, 0.f, 1.f);
		return IM_COL32((int)(255 * (1.f - hpRatio)), (int)(255 * hpRatio), 0, 255);
	}

	// ================================================================
	// ESP audit stage 2: box-type dispatch (Task 9).
	// ================================================================
	ImVec4 GetBoxByType(const CEntity& Entity)
	{
		switch (MenuConfig::BoxType) {
		case 1:  return Get2DBoneRect(Entity);
		default: return Get2DBox(Entity);
		}
	}

	// ================================================================
	// ESP audit stage 2: projectile timer computation (Task 10).
	// Unifies the validate+switch+remaining logic shared by
	// DrawProjectileESP and DrawWorldESP. worldMode selects between the
	// two switch tables (Smoke/Fire/Decoy for WorldESP with Decoy=15s;
	// all 5 types for ProjectileESP with Decoy=0s).
	// ================================================================
	bool ComputeProjectileTimer(const GrenadeProjectile& proj, bool worldMode,
	                            const char** label, float* maxDuration, float* remaining)
	{
		if (!IsValidFloat(proj.Position.x) || !IsValidFloat(proj.Position.y) || !IsValidFloat(proj.Position.z))
			return false;

		*label = "";
		*maxDuration = 0.f;
		if (worldMode) {
			switch (proj.Type) {
				case PROJ_SMOKE:   *label = "Smoke"; *maxDuration = 20.0f; break;
				case PROJ_MOLOTOV: *label = "Fire";  *maxDuration = 7.0f;  break;
				case PROJ_DECOY:   *label = "Decoy"; *maxDuration = 15.0f; break;
				default: return false;
			}
		} else {
			switch (proj.Type) {
				case PROJ_SMOKE:   *label = "Smoke"; *maxDuration = 20.0f; break;
				case PROJ_FLASH:   *label = "Flash"; break;
				case PROJ_HE:      *label = "HE"; break;
				case PROJ_MOLOTOV: *label = "Fire"; *maxDuration = 7.0f; break;
				case PROJ_DECOY:   *label = "Decoy"; break;
				default: return false;
			}
		}

		*remaining = *maxDuration - proj.StationaryTimer - proj.DisappearTimer;
		if (*remaining < 0.f) *remaining = 0.f;
		return true;
	}

}


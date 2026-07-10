#pragma once

// ============================================================================
// VisibilityChecker —— 基于 BVH 射线检测的可见性判断
// ----------------------------------------------------------------------------
// 封装 MapGeometryLoader + BVH，为 ESP 渲染提供真实遮挡检测：
//   - UpdateForMap：地图切换时加载几何数据并构建 BVH
//   - IsVisible：从本地玩家眼睛位置向目标发射线，
//                若与地图几何相交则被遮挡（返回 false），否则可见（返回 true）
//   - 无可用 BVH 时 IsVisible 直接返回 true（回退为可见，保持原有行为）
//
// DMA 只读性约束：仅读取本地地图几何文件，不触及宿主机内存。
// ============================================================================

#include <string>

#include "../OS-ImGui/OS-ImGui_Struct.h"	// Vec3
#include "MapGeometryLoader.h"
#include "BVH.h"

class VisibilityChecker
{
public:
	VisibilityChecker() = default;
	~VisibilityChecker() = default;

	// 地图切换时调用：加载几何数据并构建 BVH
	// mapName 可为 "de_dust2" 或 "maps/de_dust2.vpk"
	// 加载失败（文件不存在/格式错误）时标记为不可用，ESP 回退原逻辑
	void UpdateForMap(const std::string& mapName);

	// 可见性判断：返回 true 表示无遮挡（可见），false 表示被墙遮挡
	// fromEye：本地玩家眼睛位置（世界坐标）
	// toTarget：目标位置（世界坐标，通常为敌人头部/胸部）
	bool IsVisible(const Vec3& fromEye, const Vec3& toTarget);

	// 当前是否有可用的 BVH（可用于 ESP 真实遮挡检测）
	bool IsEnabled() const { return m_hasGeometry; }

	// 清空当前地图数据（释放内存）
	void Clear();

private:
	MapGeometryLoader m_loader;
	BVH m_bvh;
	bool m_hasGeometry = false;
};

// 全局单例（与 ProcessMgr 风格一致）
inline VisibilityChecker g_visibilityChecker;

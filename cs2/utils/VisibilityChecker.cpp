// ============================================================================
// VisibilityChecker —— 基于 BVH 射线检测的可见性判断实现
// ============================================================================

#include "VisibilityChecker.h"
#include "Logger.h"

#include <cmath>

void VisibilityChecker::UpdateForMap(const std::string& mapName)
{
	// 清空旧数据
	Clear();

	// 加载地图几何数据
	if (!m_loader.LoadMapGeometry(mapName)) {
		LOG_WARNING("VisCheck", "UpdateForMap: no geometry for '{}', ESP will fallback", mapName);
		m_hasGeometry = false;
		return;
	}

	// 构建 BVH
	const MapGeometry* geom = m_loader.GetCurrentGeometry();
	if (!geom || geom->TriangleCount() == 0) {
		LOG_WARNING("VisCheck", "UpdateForMap: empty geometry for '{}'", mapName);
		m_hasGeometry = false;
		return;
	}

	m_bvh.Build(*geom);
	m_hasGeometry = m_bvh.IsValid();

	if (m_hasGeometry) {
		LOG_INFO("VisCheck", "VPK visibility enabled for '{}'", m_loader.GetCurrentMapName());
	} else {
		LOG_WARNING("VisCheck", "BVH build failed for '{}'", m_loader.GetCurrentMapName());
	}
}

bool VisibilityChecker::IsVisible(const Vec3& fromEye, const Vec3& toTarget)
{
	// 无可用 BVH：回退为可见（保持原有行为）
	if (!m_hasGeometry) return true;

	// 计算方向与距离（原始 float 运算，避免 Vec3 非 const 运算符限制）
	float dx = toTarget.x - fromEye.x;
	float dy = toTarget.y - fromEye.y;
	float dz = toTarget.z - fromEye.z;
	float maxDist = sqrtf(dx * dx + dy * dy + dz * dz);

	// 起点与目标几乎重合：视为可见
	if (maxDist < 1e-4f) return true;

	// 归一化方向
	float invLen = 1.0f / maxDist;
	Vec3 dir;
	dir.x = dx * invLen;
	dir.y = dy * invLen;
	dir.z = dz * invLen;

	// 射线检测：命中地图几何表示中间有遮挡物
	float hitDist = 0.f;
	bool hit = m_bvh.RayIntersect(fromEye, dir, maxDist, hitDist);

	// 命中（被遮挡）返回 false，未命中（可见）返回 true
	return !hit;
}

void VisibilityChecker::Clear()
{
	m_bvh.Clear();
	m_hasGeometry = false;
	// 注意：不重置 m_loader，保留最后一次加载的地图名供查询
}

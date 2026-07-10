// ============================================================================
// BVH —— 层次包围盒加速结构实现
// ============================================================================
// 构建：递归 AABB 树，叶节点 ≤32 三角形，最长轴中位数分割
// 求交：AABB slab 方法 + 三角形 Möller–Trumbore，DFS 遍历命中即返回
// ============================================================================

#include "BVH.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <chrono>

// 叶节点最大三角形数（与 ProCS2 sub_1400125E0 一致）
static constexpr size_t LEAF_SIZE = 32;

// 最大递归深度（防止退化情况下的栈溢出）
static constexpr int MAX_DEPTH = 48;

// ----------------------------------------------------------------------------
// AABB 实现
// ----------------------------------------------------------------------------

void AABB::Expand(const Vec3& v)
{
	if (v.x < min.x) min.x = v.x;
	if (v.y < min.y) min.y = v.y;
	if (v.z < min.z) min.z = v.z;
	if (v.x > max.x) max.x = v.x;
	if (v.y > max.y) max.y = v.y;
	if (v.z > max.z) max.z = v.z;
}

void AABB::Expand(const Vec3& v0, const Vec3& v1, const Vec3& v2)
{
	Expand(v0);
	Expand(v1);
	Expand(v2);
}

bool AABB::Intersect(const Vec3& origin, const Vec3& invDir, float maxDist) const
{
	// Slab 方法：分别计算三轴的进入/离开 t 值
	float t1 = (min.x - origin.x) * invDir.x;
	float t2 = (max.x - origin.x) * invDir.x;
	float tmin = std::min(t1, t2);
	float tmax = std::max(t1, t2);

	t1 = (min.y - origin.y) * invDir.y;
	t2 = (max.y - origin.y) * invDir.y;
	tmin = std::max(tmin, std::min(t1, t2));
	tmax = std::min(tmax, std::max(t1, t2));

	t1 = (min.z - origin.z) * invDir.z;
	t2 = (max.z - origin.z) * invDir.z;
	tmin = std::max(tmin, std::min(t1, t2));
	tmax = std::min(tmax, std::max(t1, t2));

	// 命中条件：包围盒在射线前方（tmax >= 0）、slab 重叠（tmax >= tmin）、
	// 且进入点在最大距离内（tmin < maxDist）
	return tmax >= 0.f && tmax >= tmin && tmin < maxDist;
}

int AABB::LongestAxis() const
{
	float ex = max.x - min.x;
	float ey = max.y - min.y;
	float ez = max.z - min.z;
	// 与 ProCS2 sub_1400125E0 的轴选择逻辑一致
	if (ex >= ey)
		return (ex >= ez) ? 0 : 2;
	else
		return (ey >= ez) ? 1 : 2;
}

// ----------------------------------------------------------------------------
// Möller–Trumbore 射线-三角形求交（原始 float 运算，避免 Vec3 开销）
// ----------------------------------------------------------------------------
static inline bool RayTriangleIntersect(
	const Vec3& origin, const Vec3& dir,
	const Vec3& v0, const Vec3& v1, const Vec3& v2,
	float maxDist, float& outT)
{
	float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
	float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;

	// h = cross(dir, e2)
	float hx = dir.y * e2z - dir.z * e2y;
	float hy = dir.z * e2x - dir.x * e2z;
	float hz = dir.x * e2y - dir.y * e2x;

	// a = dot(e1, h)
	float a = e1x * hx + e1y * hy + e1z * hz;
	if (a > -1e-6f && a < 1e-6f) return false;	// 射线与三角形平面平行

	float f = 1.0f / a;

	// s = origin - v0
	float sx = origin.x - v0.x, sy = origin.y - v0.y, sz = origin.z - v0.z;

	// u = f * dot(s, h)
	float u = f * (sx * hx + sy * hy + sz * hz);
	if (u < 0.f || u > 1.f) return false;

	// q = cross(s, e1)
	float qx = sy * e1z - sz * e1y;
	float qy = sz * e1x - sx * e1z;
	float qz = sx * e1y - sy * e1x;

	// v = f * dot(dir, q)
	float v = f * (dir.x * qx + dir.y * qy + dir.z * qz);
	if (v < 0.f || u + v > 1.f) return false;

	// t = f * dot(e2, q)
	float t = f * (e2x * qx + e2y * qy + e2z * qz);
	if (t <= 1e-4f || t >= maxDist) return false;

	outT = t;
	return true;
}

// ----------------------------------------------------------------------------
// BVH 实现
// ----------------------------------------------------------------------------

void BVH::Build(const MapGeometry& geometry)
{
	auto t0 = std::chrono::steady_clock::now();

	m_root.reset();
	m_geometry = &geometry;
	m_centroids.clear();

	size_t triCount = geometry.TriangleCount();
	if (triCount == 0) {
		LOG_WARNING("BVH", "Build: geometry has 0 triangles");
		return;
	}

	// 预计算每个三角形的质心（用于分割时的中位数比较）
	m_centroids.resize(triCount);
	for (uint32_t i = 0; i < triCount; i++) {
		const Vec3* v0; const Vec3* v1; const Vec3* v2;
		geometry.GetTriangle(i, v0, v1, v2);
		m_centroids[i].x = (v0->x + v1->x + v2->x) * (1.f / 3.f);
		m_centroids[i].y = (v0->y + v1->y + v2->y) * (1.f / 3.f);
		m_centroids[i].z = (v0->z + v1->z + v2->z) * (1.f / 3.f);
	}

	// 初始索引数组 0,1,...,triCount-1
	std::vector<uint32_t> triIndices(triCount);
	for (uint32_t i = 0; i < triCount; i++)
		triIndices[i] = i;

	m_root = BuildRecursive(std::move(triIndices), 0);

	auto t1 = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	LOG_INFO("BVH", "Built BVH: triangles={} ({}ms)", (int)triCount, (int)ms);
}

std::unique_ptr<BVHNode> BVH::BuildRecursive(std::vector<uint32_t> tris, int depth)
{
	if (tris.empty()) return nullptr;

	auto node = std::make_unique<BVHNode>();

	// 计算当前节点所有三角形的 AABB
	for (uint32_t ti : tris) {
		const Vec3* v0; const Vec3* v1; const Vec3* v2;
		m_geometry->GetTriangle(ti, v0, v1, v2);
		node->bounds.Expand(*v0, *v1, *v2);
	}

	// 叶节点条件：三角形数 ≤ 阈值 或 达到最大深度
	if (tris.size() <= LEAF_SIZE || depth >= MAX_DEPTH) {
		node->triangleIndices = std::move(tris);
		return node;
	}

	// 选择最长轴
	int axis = node->bounds.LongestAxis();

	// 沿最长轴按质心做中位数分区（nth_element，平均 O(n)）
	auto mid = tris.begin() + tris.size() / 2;
	std::nth_element(tris.begin(), mid, tris.end(),
		[&](uint32_t a, uint32_t b) {
			float ca = (axis == 0) ? m_centroids[a].x
			         : (axis == 1) ? m_centroids[a].y
			         : m_centroids[a].z;
			float cb = (axis == 0) ? m_centroids[b].x
			         : (axis == 1) ? m_centroids[b].y
			         : m_centroids[b].z;
			if (ca != cb) return ca < cb;
			return a < b;	// 平局时用索引保证严格弱序
		});

	// 分割为左右两半
	std::vector<uint32_t> leftTris(tris.begin(), mid);
	std::vector<uint32_t> rightTris(mid, tris.end());

	// 递归构建子树（处理退化情况：一侧为空则作为叶节点）
	if (!leftTris.empty())
		node->left = BuildRecursive(std::move(leftTris), depth + 1);
	if (!rightTris.empty())
		node->right = BuildRecursive(std::move(rightTris), depth + 1);

	// 若两侧都构建失败（不应发生），保留当前三角形作为叶节点
	if (!node->left && !node->right) {
		node->triangleIndices = std::move(tris);
	}

	return node;
}

bool BVH::RayIntersect(const Vec3& origin, const Vec3& dir, float maxDist, float& hitDist) const
{
	if (!m_root || !m_geometry) return false;

	// 预计算逆方向（零分量用大数替代，slab 方法可正确处理平行射线）
	Vec3 invDir;
	invDir.x = (fabsf(dir.x) < 1e-8f) ? 1e30f : 1.0f / dir.x;
	invDir.y = (fabsf(dir.y) < 1e-8f) ? 1e30f : 1.0f / dir.y;
	invDir.z = (fabsf(dir.z) < 1e-8f) ? 1e30f : 1.0f / dir.z;

	hitDist = maxDist;
	return TraverseNode(m_root.get(), origin, dir, invDir, hitDist);
}

bool BVH::TraverseNode(const BVHNode* node, const Vec3& origin, const Vec3& dir,
                       const Vec3& invDir, float& hitDist) const
{
	if (!node) return false;

	// AABB 剪枝：当前节点包围盒与射线不相交则跳过
	if (!node->bounds.Intersect(origin, invDir, hitDist))
		return false;

	// 叶节点：逐三角形求交
	if (node->IsLeaf()) {
		bool hit = false;
		for (uint32_t ti : node->triangleIndices) {
			const Vec3* v0; const Vec3* v1; const Vec3* v2;
			m_geometry->GetTriangle(ti, v0, v1, v2);
			float t;
			if (RayTriangleIntersect(origin, dir, *v0, *v1, *v2, hitDist, t)) {
				hitDist = t;	// 收紧最大距离，加速后续剪枝
				hit = true;
			}
		}
		return hit;
	}

	// 内部节点：DFS 遍历左右子树
	// 命中任一侧即返回 true（遮挡检测只需任意命中）
	if (TraverseNode(node->left.get(), origin, dir, invDir, hitDist))
		return true;
	return TraverseNode(node->right.get(), origin, dir, invDir, hitDist);
}

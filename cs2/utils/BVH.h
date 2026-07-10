#pragma once

// ============================================================================
// BVH —— 层次包围盒（Bounding Volume Hierarchy）加速结构
// ----------------------------------------------------------------------------
// 用于地图几何的射线遮挡检测。构建策略与 ProCS2（sub_1400125E0）一致：
//   - 递归 AABB 树
//   - 叶节点阈值：32 个三角形
//   - 分割轴选择：当前节点 AABB 的最长轴
//   - 分割策略：沿最长轴按三角形质心做中位数分区（nth_element）
//
// 射线求交：
//   - AABB：slab 方法（预计算逆方向，处理平行射线）
//   - 三角形：Möller–Trumbore 算法
//   - 遍历：深度优先（DFS），命中即返回（遮挡检测只需任意命中）
//
// 性能目标：构建 < 500ms，单次射线 < 0.1ms
// ============================================================================

#include <vector>
#include <memory>
#include <cstdint>
#include <cfloat>

#include "../OS-ImGui/OS-ImGui_Struct.h"	// Vec3
#include "MapGeometryLoader.h"

// 轴对齐包围盒
struct AABB
{
	Vec3 min{ FLT_MAX, FLT_MAX, FLT_MAX };
	Vec3 max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	// 扩展包围盒以包含一个点
	void Expand(const Vec3& v);

	// 扩展包围盒以包含一个三角形（3 顶点）
	void Expand(const Vec3& v0, const Vec3& v1, const Vec3& v2);

	// 射线-AABB 相交检测（slab 方法）
	// origin/dir：射线起点和方向（dir 不需要归一化，maxDist 为方向向量长度比例）
	// invDir：1/dir 各分量（预计算，零分量用大数替代）
	// maxDist：最大命中距离（射线长度）
	bool Intersect(const Vec3& origin, const Vec3& invDir, float maxDist) const;

	// 返回最长轴索引：0=x, 1=y, 2=z
	int LongestAxis() const;
};

// BVH 节点
struct BVHNode
{
	AABB bounds;
	std::unique_ptr<BVHNode> left;
	std::unique_ptr<BVHNode> right;
	std::vector<uint32_t> triangleIndices;	// 仅叶节点非空

	bool IsLeaf() const { return left == nullptr && right == nullptr; }
};

// 层次包围盒
class BVH
{
public:
	BVH() = default;
	~BVH() = default;

	// 从地图几何数据构建 BVH
	void Build(const MapGeometry& geometry);

	// 射线相交检测：返回 true 表示命中（被遮挡），hitDist 为命中距离
	// origin/dir：射线起点和方向（dir 需归一化，maxDist 为世界距离）
	bool RayIntersect(const Vec3& origin, const Vec3& dir, float maxDist, float& hitDist) const;

	// BVH 是否可用（已构建且非空）
	bool IsValid() const { return m_root != nullptr; }

	// 释放内部数据
	void Clear() { m_root.reset(); m_geometry = nullptr; m_centroids.clear(); }

private:
	// 递归构建节点
	std::unique_ptr<BVHNode> BuildRecursive(std::vector<uint32_t> tris, int depth);

	// 递归射线遍历（DFS）
	bool TraverseNode(const BVHNode* node, const Vec3& origin, const Vec3& dir,
	                  const Vec3& invDir, float& hitDist) const;

	std::unique_ptr<BVHNode> m_root;
	const MapGeometry* m_geometry = nullptr;
	std::vector<Vec3> m_centroids;	// 预计算的三角形质心，加速分割
};

#pragma once

// ============================================================================
// MapGeometryLoader —— ProCS2 maps 几何文件加载器
// ----------------------------------------------------------------------------
// 文件格式（详见 procs2-maps-format.md）：
//   - 纯 float32 三角形列表，无头部
//   - 每三角形 36 字节 = 3 顶点 × 3 float32(xyz)
//   - 校验条件：file_size % 36 == 0
//
// 内存布局：
//   - vertices: 扁平顶点数组，大小 = triangleCount × 3
//     三角形 k 的顶点为 vertices[k*3], vertices[k*3+1], vertices[k*3+2]
//   - indices:  三角形索引数组，大小 = triangleCount，初始 indices[k] = k
//     BVH 构建时可重排该数组以加速射线遍历
//
// DMA 只读性约束：本模块仅读取本地磁盘文件，不触及宿主机内存。
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>

#include "../OS-ImGui/OS-ImGui_Struct.h"	// Vec3

// 地图几何数据（三角形列表）
struct MapGeometry
{
	std::vector<Vec3> vertices;		// 扁平顶点数组，每三角形 3 顶点
	std::vector<uint32_t> indices;	// 三角形索引（indices[k] 指向第 k 个三角形）

	// 三角形数量
	size_t TriangleCount() const { return indices.size(); }

	// 取三角形 k 的三个顶点（只读引用）
	void GetTriangle(uint32_t triIdx, const Vec3*& v0, const Vec3*& v1, const Vec3*& v2) const
	{
		uint32_t base = triIdx * 3;
		v0 = &vertices[base];
		v1 = &vertices[base + 1];
		v2 = &vertices[base + 2];
	}
};

class MapGeometryLoader
{
public:
	MapGeometryLoader() = default;
	~MapGeometryLoader() = default;

	// 从 data\maps\<mapName> 加载地图几何文件
	// mapName 可为 "de_dust2" 或 "maps/de_dust2.vpk"，内部会做归一化
	// 成功返回 true 并更新当前几何数据，失败返回 false
	bool LoadMapGeometry(const std::string& mapName);

	// 获取当前已加载的几何数据（未加载时返回 nullptr）
	const MapGeometry* GetCurrentGeometry() const;

	// 检查指定地图的几何文件是否存在（不加载）
	bool HasGeometryForMap(const std::string& mapName) const;

	// 当前已加载的地图名（归一化后）
	const std::string& GetCurrentMapName() const { return m_currentMapName; }

private:
	// 将 "maps/de_dust2.vpk" / "de_dust2.bsp" 归一化为 "de_dust2"
	static std::string NormalizeMapName(const std::string& raw);

	// 构造几何文件完整路径（相对 exe 目录的 data\maps\<mapName>）
	static std::string BuildFilePath(const std::string& normalizedMapName);

	MapGeometry m_current;
	std::string m_currentMapName;
	bool m_loaded = false;
};

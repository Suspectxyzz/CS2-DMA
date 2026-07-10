// ============================================================================
// MapGeometryLoader —— ProCS2 maps 几何文件加载器实现
// ============================================================================
// 文件格式：纯 float32 三角形列表，每三角形 36 字节（3 顶点 × 3 float32 xyz）
// 校验条件：file_size % 36 == 0
// 加载流程参照 procs2-maps-format.md 中 sub_140011C40 的反编译伪代码。
// ============================================================================

#include "MapGeometryLoader.h"
#include "Logger.h"

#include <fstream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>

#include <Windows.h>

// 三角形文件每条记录的字节数（3 顶点 × 3 float32 = 36 字节）
static constexpr size_t TRIANGLE_BYTES = 36;

std::string MapGeometryLoader::NormalizeMapName(const std::string& raw)
{
	std::string name = raw;

	// 去除 "maps/" 前缀
	if (name.find("maps/") == 0)
		name = name.substr(5);
	if (name.find("maps\\") == 0)
		name = name.substr(5);

	// 去除 ".vpk" / ".bsp" 后缀
	auto stripExt = [&](const char* ext) {
		size_t pos = name.find(ext);
		if (pos != std::string::npos)
			name = name.substr(0, pos);
	};
	stripExt(".vpk");
	stripExt(".bsp");

	// 去除尾部路径分隔符
	while (!name.empty() && (name.back() == '/' || name.back() == '\\'))
		name.pop_back();

	return name;
}

std::string MapGeometryLoader::BuildFilePath(const std::string& normalizedMapName)
{
	// 相对 exe 目录解析 data\maps\<mapName>（与 GrenadeHelper 路径解析方式一致）
	char modulePath[MAX_PATH] = { 0 };
	GetModuleFileNameA(NULL, modulePath, MAX_PATH);
	std::string exeDir(modulePath);
	size_t slash = exeDir.find_last_of("\\/");
	if (slash != std::string::npos)
		exeDir = exeDir.substr(0, slash);
	else
		exeDir = ".";

	return exeDir + "\\data\\maps\\" + normalizedMapName;
}

bool MapGeometryLoader::HasGeometryForMap(const std::string& mapName) const
{
	std::string normalized = NormalizeMapName(mapName);
	if (normalized.empty()) return false;

	std::string filePath = BuildFilePath(normalized);
	DWORD attrs = GetFileAttributesA(filePath.c_str());
	return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool MapGeometryLoader::LoadMapGeometry(const std::string& mapName)
{
	auto t0 = std::chrono::steady_clock::now();

	std::string normalized = NormalizeMapName(mapName);
	if (normalized.empty()) {
		LOG_WARNING("MapGeom", "LoadMapGeometry: empty map name after normalize");
		return false;
	}

	std::string filePath = BuildFilePath(normalized);

	// 1. 以二进制模式打开文件
	std::ifstream file(filePath, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		LOG_WARNING("MapGeom", "LoadMapGeometry: cannot open '{}'", filePath);
		m_loaded = false;
		return false;
	}

	// 2. 获取文件大小
	file.seekg(0, std::ios::end);
	std::streamoff fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// 3. 校验：文件大小必须能被 36 整除（与 ProCS2 sub_140011C40 一致）
	if (fileSize <= 0 || (static_cast<size_t>(fileSize) % TRIANGLE_BYTES) != 0) {
		LOG_WARNING("MapGeom", "LoadMapGeometry: invalid file size {} (must be divisible by {})", 
			(int)fileSize, (int)TRIANGLE_BYTES);
		file.close();
		m_loaded = false;
		return false;
	}

	size_t triangleCount = static_cast<size_t>(fileSize) / TRIANGLE_BYTES;

	// 4. 一次性读取全部文件数据（ProCS2 使用 std::istream::read 一次性读取）
	std::vector<char> rawBuffer(static_cast<size_t>(fileSize));
	file.read(rawBuffer.data(), fileSize);
	file.close();

	if (file.gcount() != fileSize) {
		LOG_ERROR("MapGeom", "LoadMapGeometry: short read, expected {} got {}", 
			(int)fileSize, (int)file.gcount());
		m_loaded = false;
		return false;
	}

	// 5. 解析为顶点数组（每三角形 3 顶点）
	const float* floatData = reinterpret_cast<const float*>(rawBuffer.data());
	size_t vertexCount = triangleCount * 3;

	m_current.vertices.resize(vertexCount);
	// 直接 memcpy（float32 布局与 Vec3{x,y,z} 内存布局一致）
	std::memcpy(m_current.vertices.data(), floatData, vertexCount * sizeof(Vec3));

	// 6. 生成顺序索引数组 0,1,2,...,triangleCount-1（与 ProCS2 一致）
	m_current.indices.resize(triangleCount);
	for (uint32_t i = 0; i < triangleCount; i++)
		m_current.indices[i] = i;

	m_currentMapName = normalized;
	m_loaded = true;

	auto t1 = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	LOG_INFO("MapGeom", "Loaded [{}] triangles={} vertices={} ({}ms)",
		normalized, (int)triangleCount, (int)vertexCount, (int)ms);

	return true;
}

const MapGeometry* MapGeometryLoader::GetCurrentGeometry() const
{
	if (!m_loaded) return nullptr;
	return &m_current;
}

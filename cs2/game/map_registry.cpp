#include "map_registry.h"

#include "../utils/Logger.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <Shlobj.h>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
	// ========================================================================
	//  Fallback map definitions — hardcoded bounds for known CS2 maps.
	//  Bounds values match the KevqDMA reference (map_registry.cpp).
	//  origin/scale are overridden by data.json when available.
	// ========================================================================
	std::vector<radar::MapDefinition> BuildFallbackMaps()
	{
		return {
			{ "de_mirage",     "Mirage",      -3230.0, 1890.0, -3407.0, 1713.0, -3230.0, 1713.0, 5.00, false, "/data/de_mirage/radar.webp", "/data/de_mirage/radar.webp" },
			{ "de_inferno",    "Inferno",     -2087.0, 2930.6, -1147.6, 3870.0, -2087.0, 3870.0, 4.90, false, "/data/de_inferno/radar.webp", "/data/de_inferno/radar.webp" },
			{ "de_dust2",      "Dust II",     -2476.0, 2029.6, -1266.6, 3239.0, -2476.0, 3239.0, 4.40, false, "/data/de_dust2/radar.webp", "/data/de_dust2/radar.webp" },
			{ "de_nuke",       "Nuke",        -3453.0, 3715.0, -4281.0, 2887.0, -3453.0, 2887.0, 7.00, false, "/data/de_nuke/radar.webp", "/data/de_nuke/radar.webp" },
			{ "de_overpass",   "Overpass",    -4831.0, 493.8,  -3543.8, 1781.0, -4831.0, 1781.0, 5.20, false, "/data/de_overpass/radar.webp", "/data/de_overpass/radar.webp" },
			{ "de_train",      "Train",       -2308.0, 1872.046848, -2102.046848, 2078.0, -2308.0, 2078.0, 4.082077, false, "/data/de_train/radar.webp", "/data/de_train/radar.webp" },
			{ "de_cache",      "Cache",       -2000.0, 3632.0, -2382.0, 3250.0, -2000.0, 3250.0, 5.50, false, "/data/de_cache/radar.webp", "/data/de_cache/radar.webp" },
			{ "de_ancient",    "Ancient",     -2953.0, 2167.0, -2956.0, 2164.0, -2953.0, 2164.0, 5.00, false, "/data/de_ancient/radar.webp", "/data/de_ancient/radar.webp" },
			{ "de_anubis",     "Anubis",      -2796.0, 2549.28, -2017.28, 3328.0, -2796.0, 3328.0, 5.22, false, "/data/de_anubis/radar.webp", "/data/de_anubis/radar.webp" },
			{ "de_vertigo",    "Vertigo",     -3168.0, 928.0,  -2334.0, 1762.0, -3168.0, 1762.0, 4.00, false, "/data/de_vertigo/radar.webp", "/data/de_vertigo/radar.webp" },
			{ "cs_office",     "Office",      -1838.0, 2360.4, -2340.4, 1858.0, -1838.0, 1858.0, 4.10, false, "/data/cs_office/radar.webp", "/data/cs_office/radar.webp" },
			{ "cs_italy",      "Italy",       -2647.0, 2063.4, -2118.4, 2592.0, -2647.0, 2592.0, 4.60, false, "/data/cs_italy/radar.webp", "/data/cs_italy/radar.webp" },
			{ "de_thera",      "Thera",       -2953.0, 2167.0, -2956.0, 2164.0, -2953.0, 2164.0, 5.00, false, "/data/de_thera/radar.webp", "/data/de_thera/radar.webp" },
			{ "de_palacio",    "Palacio",     -2953.0, 2167.0, -2956.0, 2164.0, -2953.0, 2164.0, 5.00, false, "/data/de_palacio/radar.webp", "/data/de_palacio/radar.webp" },
			{ "de_grail",      "Grail",       -2953.0, 2167.0, -2956.0, 2164.0, -2953.0, 2164.0, 5.00, false, "/data/de_grail/radar.webp", "/data/de_grail/radar.webp" },
			{ "de_golden",     "Golden",      -2953.0, 2167.0, -2956.0, 2164.0, -2953.0, 2164.0, 5.00, false, "/data/de_golden/radar.webp", "/data/de_golden/radar.webp" },
			{ "cs_agency",     "Agency",      -1838.0, 2360.4, -2340.4, 1858.0, -1838.0, 1858.0, 4.10, false, "/data/cs_agency/radar.webp", "/data/cs_agency/radar.webp" },
			{ "aim_custom",    "Aim / Workshop (Dynamic)", -1024.0, 1024.0, -1024.0, 1024.0, 0.0, 0.0, 1.00, true, "/data/aim_custom/radar.webp", "/data/aim_custom/radar.webp" },
		};
	}

	// ========================================================================
	//  Path helpers
	// ========================================================================

	// Returns the directory containing the running executable.
	std::string GetExeDir()
	{
		char buf[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, buf, MAX_PATH);
		std::string path(buf);
		auto pos = path.find_last_of("\\/");
		return (pos != std::string::npos) ? path.substr(0, pos) : path;
	}

	// Returns the webapp data directory: <exeDir>\external\webradar\webapp\public\data
	// or <exeDir>\webapp\data (dist mode). Returns empty if not found.
	std::string FindWebappDataDir()
	{
		std::string exeDir = GetExeDir();
		const char* candidates[] = {
			"\\external\\webradar\\webapp\\public\\data",
			"\\webapp\\data",
			"\\external\\webradar\\webapp\\dist\\data",
		};
		for (const char* rel : candidates) {
			std::string path = exeDir + rel;
			if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
				return path;
		}
		return {};
	}

	// Returns the calibration config directory: <exeDir>\config\radar_calibration
	std::string GetCalibrationDir()
	{
		return GetExeDir() + "\\config\\radar_calibration";
	}

	// ========================================================================
	//  data.json loading — overrides origin/scale from frontend data files
	// ========================================================================

	// Parse a single data.json file and update the matching fallback map's
	// origin/scale. data.json format: { "x": ..., "y": ..., "scale": ... }
	void MergeDataJson(const fs::path& jsonPath, std::vector<radar::MapDefinition>& maps)
	{
		std::ifstream file(jsonPath, std::ios::in | std::ios::binary);
		if (!file.is_open())
			return;

		std::string content((std::istreambuf_iterator<char>(file)),
		                    std::istreambuf_iterator<char>());
		file.close();

		rapidjson::Document doc;
		doc.Parse(content.c_str());
		if (doc.HasParseError() || !doc.IsObject())
			return;

		std::string mapName = jsonPath.parent_path().filename().string();

		for (auto& map : maps) {
			if (map.name != mapName)
				continue;

			if (doc.HasMember("x") && doc["x"].IsNumber())
				map.originX = doc["x"].GetDouble();
			if (doc.HasMember("y") && doc["y"].IsNumber())
				map.originY = doc["y"].GetDouble();
			if (doc.HasMember("scale") && doc["scale"].IsNumber())
				map.scale = doc["scale"].GetDouble();
			return;
		}
	}

	// Scan the webapp data directory for data.json files and merge origin/scale
	// into the fallback map list. Also adds any unknown maps as dynamic entries.
	void MergeDataJsonMaps(std::vector<radar::MapDefinition>& maps)
	{
		std::string dataDir = FindWebappDataDir();
		if (dataDir.empty())
			return;

		std::error_code ec;
		if (!fs::exists(dataDir, ec))
			return;

		for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
			if (ec)
				break;
			if (!entry.is_directory())
				continue;

			fs::path jsonPath = entry.path() / "data.json";
			if (!fs::exists(jsonPath, ec))
				continue;

			MergeDataJson(jsonPath, maps);
		}
	}

	// ========================================================================
	//  Name normalization
	// ========================================================================

	bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
	{
		if (lhs.size() != rhs.size())
			return false;
		for (size_t i = 0; i < lhs.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
			    std::tolower(static_cast<unsigned char>(rhs[i])))
				return false;
		}
		return true;
	}

	std::string NormalizeMapCandidate(std::string_view rawName)
	{
		size_t begin = 0;
		size_t end = rawName.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(rawName[begin])) != 0)
			++begin;
		while (end > begin && std::isspace(static_cast<unsigned char>(rawName[end - 1])) != 0)
			--end;
		if (begin >= end)
			return {};

		std::string text(rawName.substr(begin, end - begin));
		std::replace(text.begin(), text.end(), '\\', '/');

		const size_t slash = text.find_last_of('/');
		if (slash != std::string::npos)
			text.erase(0, slash + 1);

		const size_t dot = text.find_last_of('.');
		if (dot != std::string::npos)
			text.erase(dot);

		std::string normalized;
		normalized.reserve(text.size());
		for (const unsigned char ch : text) {
			if (std::isalnum(ch) != 0 || ch == '_')
				normalized.push_back(static_cast<char>(std::tolower(ch)));
		}

		if (normalized.empty() || normalized.size() >= 64)
			return {};

		return normalized;
	}

	int QuantizeMapCoord(double value)
	{
		constexpr double kStep = 16.0;
		return static_cast<int>(std::lround(value / kStep) * kStep);
	}

	// ========================================================================
	//  Map definition loader (called once, cached)
	// ========================================================================
	std::vector<radar::MapDefinition> LoadMapDefinitions()
	{
		auto maps = BuildFallbackMaps();
		MergeDataJsonMaps(maps);
		return maps;
	}

} // anonymous namespace

namespace radar {

const std::vector<MapDefinition>& GetMapDefinitions()
{
	static const std::vector<MapDefinition> maps = LoadMapDefinitions();
	return maps;
}

std::string NormalizeMapName(std::string_view name)
{
	const std::string normalized = NormalizeMapCandidate(name);
	if (normalized.empty())
		return {};

	for (const auto& map : GetMapDefinitions()) {
		if (EqualsIgnoreCase(map.name, normalized))
			return map.name;
	}

	return {};
}

const MapDefinition* FindMapByName(std::string_view name)
{
	const std::string normalized = NormalizeMapCandidate(name);
	if (normalized.empty())
		return nullptr;

	for (const auto& map : GetMapDefinitions()) {
		if (EqualsIgnoreCase(map.name, normalized))
			return &map;
	}

	return nullptr;
}

const MapDefinition* ResolveMapByBounds(
	double minX,
	double minY,
	double maxX,
	double maxY,
	bool allowLooseMatch)
{
	if (!std::isfinite(minX) || !std::isfinite(maxX) ||
	    !std::isfinite(minY) || !std::isfinite(maxY)) {
		return nullptr;
	}

	const double runtimeSpanX = std::fabs(maxX - minX);
	const double runtimeSpanY = std::fabs(maxY - minY);
	const double runtimeCenterX = (minX + maxX) * 0.5;
	const double runtimeCenterY = (minY + maxY) * 0.5;

	const MapDefinition* best = nullptr;
	const MapDefinition* second = nullptr;
	double bestLegacyScore = 1e18;
	double bestSpanScore = 1e18;
	double bestCenterScore = 1e18;
	double secondLegacyScore = 1e18;
	double secondSpanScore = 1e18;
	double secondCenterScore = 1e18;

	for (const auto& map : GetMapDefinitions()) {
		if (map.dynamic)
			continue;

		const double mapSpanX = std::fabs(map.maxX - map.minX);
		const double mapSpanY = std::fabs(map.maxY - map.minY);
		const double mapCenterX = (map.minX + map.maxX) * 0.5;
		const double mapCenterY = (map.minY + map.maxY) * 0.5;

		const double legacyScore =
			std::fabs(minX - map.minX) +
			std::fabs(maxX - map.maxX) +
			std::fabs(minY - map.minY) +
			std::fabs(maxY - map.maxY);
		const double spanScore =
			std::fabs(runtimeSpanX - mapSpanX) +
			std::fabs(runtimeSpanY - mapSpanY);
		const double centerScore =
			std::fabs(runtimeCenterX - mapCenterX) +
			std::fabs(runtimeCenterY - mapCenterY);

		const bool better =
			legacyScore < bestLegacyScore - 0.1 ||
			(std::fabs(legacyScore - bestLegacyScore) <= 0.1 &&
			 (spanScore < bestSpanScore - 0.1 ||
			  (std::fabs(spanScore - bestSpanScore) <= 0.1 &&
			   centerScore < bestCenterScore)));
		if (!better) {
			const bool secondBetter =
				legacyScore < secondLegacyScore - 0.1 ||
				(std::fabs(legacyScore - secondLegacyScore) <= 0.1 &&
				 (spanScore < secondSpanScore - 0.1 ||
				  (std::fabs(spanScore - secondSpanScore) <= 0.1 &&
				   centerScore < secondCenterScore)));
			if (secondBetter) {
				second = &map;
				secondLegacyScore = legacyScore;
				secondSpanScore = spanScore;
				secondCenterScore = centerScore;
			}
			continue;
		}

		second = best;
		secondLegacyScore = bestLegacyScore;
		secondSpanScore = bestSpanScore;
		secondCenterScore = bestCenterScore;
		best = &map;
		bestLegacyScore = legacyScore;
		bestSpanScore = spanScore;
		bestCenterScore = centerScore;
	}

	if (!best)
		return nullptr;

	const bool exactBoundsMatch = bestLegacyScore <= 320.0;
	const bool closeSpanMatch =
		allowLooseMatch &&
		bestLegacyScore <= 900.0 &&
		bestSpanScore <= 128.0 &&
		bestCenterScore <= 384.0;
	const bool ambiguousBounds =
		second != nullptr &&
		secondLegacyScore <= 900.0 &&
		std::fabs(secondLegacyScore - bestLegacyScore) <= 64.0 &&
		secondSpanScore <= 192.0 &&
		secondCenterScore <= 512.0;
	if (ambiguousBounds)
		return nullptr;

	return (exactBoundsMatch || closeSpanMatch) ? best : nullptr;
}

std::string BuildMapKeyFromBounds(double minX, double minY, double maxX, double maxY)
{
	if (const MapDefinition* map = ResolveMapByBounds(minX, minY, maxX, maxY, true))
		return map->name;

	return "dynamic_" + std::to_string(QuantizeMapCoord(minX)) +
	       "_" + std::to_string(QuantizeMapCoord(minY)) +
	       "__" + std::to_string(QuantizeMapCoord(maxX)) +
	       "_" + std::to_string(QuantizeMapCoord(maxY));
}

// ========================================================================
//  Task 19: Per-map calibration persistence (rapidjson)
// ========================================================================

void SaveRadarCalibrationForMap(const std::string& mapName, const RadarCalibrationRecord& record)
{
	if (mapName.empty())
		return;

	std::string dir = GetCalibrationDir();
	std::error_code ec;
	fs::create_directories(dir, ec);

	// Sanitize map name for filename (keep alnum, underscore, hyphen)
	std::string safeName;
	safeName.reserve(mapName.size());
	for (unsigned char ch : mapName) {
		if (std::isalnum(ch) || ch == '_' || ch == '-')
			safeName.push_back(static_cast<char>(ch));
	}
	if (safeName.empty())
		return;

	std::string filePath = dir + "\\" + safeName + ".json";

	rapidjson::Document doc;
	doc.SetObject();
	auto& a = doc.GetAllocator();

	doc.AddMember("map", rapidjson::Value(mapName.c_str(), a), a);
	doc.AddMember("rotationDeg", record.rotationDeg, a);
	doc.AddMember("scale", record.scale, a);
	doc.AddMember("offsetX", record.offsetX, a);
	doc.AddMember("offsetY", record.offsetY, a);

	rapidjson::StringBuffer buf;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
	doc.Accept(writer);

	std::ofstream file(filePath, std::ios::out | std::ios::binary);
	if (!file.is_open()) {
		LOG_WARNING("MapRegistry", "Failed to write calibration: {}", filePath);
		return;
	}
	file.write(buf.GetString(), buf.GetSize());
	file.close();

	LOG_TRACE("MapRegistry", "Saved calibration for '{}' → {}", mapName, filePath);
}

bool LoadRadarCalibrationForMap(const std::string& mapName, RadarCalibrationRecord& outRecord)
{
	if (mapName.empty())
		return false;

	std::string safeName;
	safeName.reserve(mapName.size());
	for (unsigned char ch : mapName) {
		if (std::isalnum(ch) || ch == '_' || ch == '-')
			safeName.push_back(static_cast<char>(ch));
	}
	if (safeName.empty())
		return false;

	std::string filePath = GetCalibrationDir() + "\\" + safeName + ".json";
	std::ifstream file(filePath, std::ios::in | std::ios::binary);
	if (!file.is_open())
		return false;

	std::string content((std::istreambuf_iterator<char>(file)),
	                    std::istreambuf_iterator<char>());
	file.close();

	rapidjson::Document doc;
	doc.Parse(content.c_str());
	if (doc.HasParseError() || !doc.IsObject())
		return false;

	outRecord = RadarCalibrationRecord{};
	if (doc.HasMember("rotationDeg") && doc["rotationDeg"].IsFloat())
		outRecord.rotationDeg = doc["rotationDeg"].GetFloat();
	if (doc.HasMember("scale") && doc["scale"].IsFloat())
		outRecord.scale = doc["scale"].GetFloat();
	if (doc.HasMember("offsetX") && doc["offsetX"].IsFloat())
		outRecord.offsetX = doc["offsetX"].GetFloat();
	if (doc.HasMember("offsetY") && doc["offsetY"].IsFloat())
		outRecord.offsetY = doc["offsetY"].GetFloat();

	return true;
}

} // namespace radar

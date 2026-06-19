#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace radar {

// Map definition with bounds for runtime matching.
// Bounds are used by ResolveMapByBounds() to identify maps when the game's
// MapName string is empty or refers to an unknown (workshop) map.
struct MapDefinition
{
	std::string name;
	std::string displayName;
	double minX = 0.0;
	double maxX = 0.0;
	double minY = 0.0;
	double maxY = 0.0;
	double originX = 0.0;
	double originY = 0.0;
	double scale = 1.0;
	bool dynamic = false;
	std::string radarImage;
	std::string backgroundImage;
};

// Per-map radar calibration record (Task 19).
// Persisted to config/radar_calibration/<mapname>.json.
struct RadarCalibrationRecord
{
	float rotationDeg = 0.0f;   // -180 ~ 180
	float scale = 1.0f;         // 0.5 ~ 1.5
	float offsetX = 0.0f;       // -0.25 ~ 0.25
	float offsetY = 0.0f;       // -0.25 ~ 0.25
};

// Returns the list of known map definitions (loaded once, cached).
// Merges hardcoded fallback bounds with origin/scale from data.json files
// found under external/webradar/webapp/public/data/.
const std::vector<MapDefinition>& GetMapDefinitions();

// Normalize a raw map name (strip path/extension, lowercase, alnum+underscore).
// Returns empty string if the result is invalid or too long.
std::string NormalizeMapName(std::string_view name);

// Find a map definition by name (case-insensitive). Returns nullptr if not found.
const MapDefinition* FindMapByName(std::string_view name);

// Resolve a map by runtime minimap bounds using three-level scoring.
// (legacyScore → spanScore → centerScore). Returns nullptr if no confident match.
// allowLooseMatch enables the closeSpanMatch threshold for fuzzy matching.
const MapDefinition* ResolveMapByBounds(
	double minX,
	double minY,
	double maxX,
	double maxY,
	bool allowLooseMatch = true);

// Build a stable map key from bounds. Tries ResolveMapByBounds first; if no
// match, returns "dynamic_{minX}_{minY}__{maxX}_{maxY}" with coords quantized
// to 16-unit steps.
std::string BuildMapKeyFromBounds(double minX, double minY, double maxX, double maxY);

// ---- Task 19: Per-map calibration persistence ----

// Save calibration for a map to config/radar_calibration/<mapName>.json.
// Uses rapidjson. Creates the directory if it doesn't exist.
void SaveRadarCalibrationForMap(const std::string& mapName, const RadarCalibrationRecord& record);

// Load calibration for a map from config/radar_calibration/<mapName>.json.
// Returns false if the file doesn't exist or is invalid.
bool LoadRadarCalibrationForMap(const std::string& mapName, RadarCalibrationRecord& outRecord);

} // namespace radar

#pragma once

#include <cstdint>

// Microsecond-based update intervals (P2 cache mechanism enhancement).
// Replaces the legacy frameCounter % N frequency division with time-based gating.
namespace intervals {
	// Base data
	constexpr int64_t kDiscoveryUs = 10000;         // 10ms  - entity discovery
	constexpr int64_t kControllerRefreshUs = 100000; // 100ms - controller refresh
	constexpr int64_t kWeaponUpdateUs = 10000;       // 10ms  - active weapon
	constexpr int64_t kWrExtraUs = 50000;            // 50ms  - WebRadar extra
	constexpr int64_t kWrSlowUs = 5000000;           // 5s    - WebRadar slow
	constexpr int64_t kProjectileScanUs = 20000;     // 20ms  - projectile scan (legacy baseline)
	// P5 Task 14: Adaptive world-scan intervals (selected by entity-count tier)
	constexpr int64_t kWorldScanBaseUs = 50000;      // 50ms  - entities <= 800
	constexpr int64_t kWorldScanHighUs = 70000;      // 70ms  - entities 800-1200
	constexpr int64_t kWorldScanMedUs = 90000;       // 90ms  - entities 1200-2000
	constexpr int64_t kWorldScanLowUs = 120000;      // 120ms - entities > 2000
	constexpr int64_t kPeriodicRefreshUs = 100000;   // 100ms - periodic refresh (was frameCounter % 50)
}

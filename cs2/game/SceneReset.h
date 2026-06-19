#pragma once

#include <atomic>
#include <cstdint>

// Unified scene-reset serial (P2 cache mechanism enhancement).
// Bumped whenever the DMA cache is refreshed due to a scene transition
// (match entry, stale data, read failures). Consumers can compare their
// cached serial against CurrentSerial() to detect invalidation.
namespace SceneReset {
	inline std::atomic<uint64_t> g_sceneResetSerial{0};

	inline void BumpSceneReset() {
		g_sceneResetSerial.fetch_add(1, std::memory_order_release);
	}

	inline uint64_t CurrentSerial() {
		return g_sceneResetSerial.load(std::memory_order_acquire);
	}
}

// Multi-tier DMA refresh (P3 Task 9).
// RequestDmaRefresh only sets a flag; the actual VMMDLL_ConfigSet call is
// performed asynchronously by DmaAdminThread (P3 Task 10) to avoid
// blocking DataThread.
enum class DmaRefreshTier { Probe, Repair, Full };

// bit0=Probe, bit1=Repair, bit2=Full
inline std::atomic<uint32_t> g_pendingRefreshFlags{0};

inline void RequestDmaRefresh(DmaRefreshTier tier) {
	uint32_t flag = 1u << static_cast<uint32_t>(tier);
	g_pendingRefreshFlags.fetch_or(flag, std::memory_order_release);
}

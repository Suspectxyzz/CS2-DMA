#pragma once

#include <string>
#include <vector>
#include <atomic>

// Current software version (loaded from data/version.json at runtime)
inline std::string PROJECT_VERSION = "1.2.0";         // fallback if version.json missing

enum class AppState {
	DMA_INITIALIZING,
	DMA_FAILED,
	SEARCHING_GAME,
	INITIALIZING_GAME,
	WAITING_DECRYPT,
	RUNNING,
	EXITING,
};

namespace globalVars {
	inline float windowx = 800;
	inline float windowy = 500;
	inline std::atomic<AppState> gameState{ AppState::DMA_INITIALIZING };
	// DMA 失败原因（初始化失败或运行时 VT/IOMMU 拒绝时填充），供 UI 显示。
	// 注意：非原子，仅在主线程填充、渲染线程读取；最坏情况下读到半截字符串也
	// 只是 UI 显示一帧乱码，无功能影响。
	inline std::string g_dmaFailReason;
}

namespace Keys {
	inline bool MenuKey = false;
	inline bool RecordKey = false;  // For grenade position recording
}

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
}

namespace Keys {
	inline bool MenuKey = false;
	inline bool RecordKey = false;  // For grenade position recording
}

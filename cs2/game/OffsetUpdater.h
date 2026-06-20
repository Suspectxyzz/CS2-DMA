#pragma once
#include <string>

// Run cs2-dumper in DMA mode to update offsets from live game memory.
// Returns true on success, false on failure.
// This function blocks until the dumper completes.
bool RunDMAOffsetDumper();

// Full offset update flow for GUI-triggered acquisition:
// 1. Closes DMA handle (so dumper can exclusively access FPGA)
// 2. Runs cs2-dumper
// 3. Returns true on success (caller should restart), false on failure (caller should call ReattachDma)
bool RunOffsetUpdateWithDma();

// Re-initialize DMA and resume game connection after failed offset update.
void ReattachDma();

// Restart the current program: launches a new instance via a delayed batch script, then exits.
void RestartSelf();

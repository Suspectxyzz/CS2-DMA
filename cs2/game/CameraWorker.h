#pragma once

// CameraWorker — independent 500Hz thread that reads the view matrix.
//
// Decouples camera (view-angle) reads from the render thread so that ESP
// re-projection tracks view rotation at a high, stable rate without
// burning a DMA read on every render frame.
//
// Lifecycle:
//   Start() spawns the worker thread (returns immediately).
//   Stop()  signals exit and joins.
//
// Render thread reads the latest matrix via GetLatestMatrix(); if no fresh
// data has arrived yet (HasFreshData() == false) it should fall back to the
// snapshot matrix.

namespace CameraWorker
{
	// Spawn the 500Hz worker thread. Safe to call once; no-op if already running.
	void Start();

	// Signal the worker to exit and join it. Safe to call once; no-op if not running.
	void Stop();

	// Copy the latest validated view matrix into out (always writes 64 bytes).
	void GetLatestMatrix(float out[4][4]);

	// True once at least one validated matrix has been published.
	bool HasFreshData();
}

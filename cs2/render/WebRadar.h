#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>

// Forward-declare SOCKET to avoid including winsock2.h here
// (winsock2.h must come before windows.h; GUI.cpp includes windows.h first)
#ifndef _WINSOCK2API_
typedef unsigned __int64 SOCKET;
#define INVALID_SOCKET (SOCKET)(~0)
#endif

// Runtime statistics for WebRadar (Task 9). All counters are atomic so they
// can be updated by the worker/broadcast threads and read by the GUI thread
// without extra locking.
struct RuntimeStats {
	std::atomic<uint64_t> framesSent{ 0 };
	std::atomic<uint64_t> framesDropped{ 0 };
	std::atomic<uint64_t> coalescedFrames{ 0 };
	std::atomic<uint64_t> droppedFramesSlowClient{ 0 };
	std::atomic<uint64_t> serializeUsTotal{ 0 };
	std::atomic<uint64_t> serializeUsCount{ 0 };
	std::atomic<uint64_t> broadcastUsTotal{ 0 };
	std::atomic<uint64_t> broadcastUsCount{ 0 };
	std::atomic<size_t>   payloadBytesLast{ 0 };
	std::atomic<size_t>   payloadBytesPeak{ 0 };
	std::atomic<int>      clientCount{ 0 };
};

// Non-atomic snapshot of RuntimeStats, used to ferry stats from the
// WebRadarThread (writer) to the GUI thread (reader) via g_webRadarStats.
struct RuntimeStatsSnapshot {
	uint64_t framesSent = 0;
	uint64_t framesDropped = 0;
	uint64_t coalescedFrames = 0;
	uint64_t droppedFramesSlowClient = 0;
	uint64_t serializeUsTotal = 0;
	uint64_t serializeUsCount = 0;
	uint64_t broadcastUsTotal = 0;
	uint64_t broadcastUsCount = 0;
	size_t   payloadBytesLast = 0;
	size_t   payloadBytesPeak = 0;
	int      clientCount = 0;
};

// Lightweight WebSocket server for Web Radar (RFC 6455)
// Zero external dependencies — uses only Winsock2 (already linked).
// Binds to 0.0.0.0 so any LAN device can connect.
class WebRadarServer {
public:
	WebRadarServer() = default;
	~WebRadarServer();

	WebRadarServer(const WebRadarServer&) = delete;
	WebRadarServer& operator=(const WebRadarServer&) = delete;

	bool Start(uint16_t port);
	void Stop();

	// Send a message to all connected WebSocket clients.
	// Thread-safe — can be called from any thread.
	void Broadcast(const std::string& message);

	bool IsRunning() const { return m_running.load(); }
	int  GetClientCount() const;

	// Worker thread: serializes GameSnapshot → JSON off the broadcast path,
	// so slow clients can't stall serialization. The broadcast side picks up
	// the latest payload via ConsumePayload().
	void StartWorker();
	void StopWorker();
	// If a new payload is ready, move it into out and clear the ready flag.
	bool ConsumePayload(std::string& out);
	// Last serialization cost in microseconds (for runtime stats).
	uint64_t GetSerializeUs() const { return m_serializeUs.load(); }

	// Returns a snapshot of runtime statistics (Task 9).
	RuntimeStatsSnapshot GetStats() const;
	// Returns the latest payload for HTTP polling (/api/live) and SSE initial
	// event. Does not consume the payload — safe to call from ClientLoop.
	std::string GetLatestPayloadForPolling() const;

private:
	void AcceptLoop();
	void ClientLoop(SOCKET clientSock);
	bool DoHandshake(SOCKET clientSock);
	bool DoHandshakeWithRequest(SOCKET clientSock, const std::string& request);
	bool SendFrame(SOCKET sock, const std::string& payload);
	void RemoveClient(SOCKET sock);
	void RemoveSseClient(SOCKET sock);
	void WorkerLoop();

	SOCKET m_listenSock = INVALID_SOCKET;
	std::atomic<bool> m_running{ false };
	std::thread m_acceptThread;

	mutable std::mutex m_clientsMutex;
	std::vector<SOCKET> m_clients;

	// SSE clients (Task 8: /api/stream endpoint). Separate mutex so SSE
	// ClientLoops can remove themselves without contending with WebSocket
	// broadcast.
	mutable std::mutex m_sseMutex;
	std::vector<SOCKET> m_sseClients;

	// Runtime statistics (Task 9).
	RuntimeStats m_stats;

	// Last broadcasted payload, used by /api/live and /api/stream initial event.
	mutable std::mutex m_lastPayloadMutex;
	std::string m_lastBroadcastPayload;

	// Worker thread: produces payloads independently of the broadcast loop.
	std::thread m_workerThread;
	std::atomic<bool> m_workerRunning{ false };
	std::condition_variable m_workerCv;       // wakes worker on stop
	std::mutex m_payloadMutex;                // guards m_latestPayload
	std::string m_latestPayload;
	std::atomic<bool> m_payloadReady{ false };
	std::atomic<uint64_t> m_serializeUs{ 0 }; // last serialize cost (Task 9 stats)
};

// Global state exposed for GUI (updated by WebRadarThread)
inline std::atomic<int> g_webRadarClientCount{ 0 };
inline std::atomic<bool> g_webRadarRunning{ false };

// Runtime stats snapshot for GUI (Task 9). Written by WebRadarThread,
// read by GUI thread. Guarded by g_webRadarStatsMutex.
inline RuntimeStatsSnapshot g_webRadarStats;
inline std::mutex g_webRadarStatsMutex;

// Cloudflare tunnel state (updated by Start/StopCloudflareTunnel)
inline std::atomic<bool> g_cloudflareTunnelRunning{ false };
inline std::string g_cloudflareTunnelURL;   // guarded by g_cloudflareTunnelMutex
inline std::mutex g_cloudflareTunnelMutex;

// Installation state for cloudflared auto-install
enum class CfInstallState {
	None,           // no install in progress
	Installing,     // winget install running
	Success,        // installed successfully
	Failed,         // installation failed
};
inline std::atomic<CfInstallState> g_cfInstallState{ CfInstallState::None };

// Get first LAN IPv4 address of this machine (for GUI display).
// Returns empty string on failure. Implemented in WebRadar.cpp.
std::string GetLocalIP();

// Cloudflare tunnel management (implemented in WebRadar.cpp)
// Starts cloudflared quick tunnel pointing at the given port.
// If cloudflared not found, auto-installs via winget, then starts.
// Returns false only if install fails. URL is captured asynchronously.
bool StartCloudflareTunnel(int port);
void StopCloudflareTunnel();

// Thread function — reads GameSnapshot, serializes to JSON, broadcasts via WebSocket.
// Declared here, implemented in WebRadar.cpp, started from main.cpp alongside other threads.
VOID WebRadarThread();

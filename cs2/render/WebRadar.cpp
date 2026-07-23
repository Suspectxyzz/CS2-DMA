// winsock2.h must be included before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WebRadar.h"
#include "WebRadar/embedded_assets.h"

#include "../utils/base64.h"
#include "../utils/Logger.h"
#include "Cheats.h"
#include "../game/MenuConfig.h"

// Remove non-UTF-8 bytes from a string so that browsers don't close
// the WebSocket (RFC 6455 requires text frames to be valid UTF-8).
static std::string SanitizeUtf8(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = (unsigned char)s[i];
		int codeLen = 0;
		if (c < 0x80)       codeLen = 1;
		else if (c < 0xC0)  { i++; continue; } // stray continuation byte
		else if (c < 0xE0)  codeLen = 2;
		else if (c < 0xF0)  codeLen = 3;
		else if (c < 0xF8)  codeLen = 4;
		else                 { i++; continue; } // invalid leading byte

		if (i + codeLen > s.size()) { i++; continue; } // truncated

		bool valid = true;
		for (int j = 1; j < codeLen; j++) {
			if (((unsigned char)s[i + j] & 0xC0) != 0x80) { valid = false; break; }
		}
		if (valid) out.append(s, i, codeLen);
		i += codeLen;
	}
	return out;
}
#include "../game/AppState.h"
#include "../game/map_registry.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <chrono>
#include <shared_mutex>
#include <fstream>

// Windows API for Cloudflare tunnel (CreateProcess, Job Object, pipes)
#include <Windows.h>

// ============================================================================
//  Static webapp directory (set by FindWebappDir, used by ServeStaticFile)
// ============================================================================

static std::string g_webappDir;

// ============================================================================
//  Task 19: Per-map radar calibration state (shared between GUI and serializer)
// ============================================================================
// The GUI thread writes to g_radarCalibration; the worker thread reads it
// during SerializeSnapshot to append calibration params to the JSON payload.
// Protected by g_radarCalibrationMutex.
static radar::RadarCalibrationRecord g_radarCalibration;
static std::string g_radarCalibrationMapName; // current map being calibrated
static std::mutex g_radarCalibrationMutex;

// GUI → serializer: set current calibration + map name
void SetRadarCalibration(const std::string& mapName, const radar::RadarCalibrationRecord& record)
{
	std::lock_guard<std::mutex> lock(g_radarCalibrationMutex);
	g_radarCalibrationMapName = mapName;
	g_radarCalibration = record;
}

// Serializer: read current calibration (thread-safe copy)
static radar::RadarCalibrationRecord GetRadarCalibrationSafe()
{
	std::lock_guard<std::mutex> lock(g_radarCalibrationMutex);
	return g_radarCalibration;
}

static std::string ToLowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
		});
	return value;
}

static std::string UrlDecode(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i+1]) && std::isxdigit((unsigned char)s[i+2])) {
			char hex[3] = { s[i+1], s[i+2], '\0' };
			out += (char)strtol(hex, nullptr, 16);
			i += 2;
		} else if (s[i] == '+') {
			out += ' ';
		} else {
			out += s[i];
		}
	}
	return out;
}

static std::string ExtractQueryParam(const std::string& url, const std::string& param) {
	auto qpos = url.find('?');
	if (qpos == std::string::npos) return "";
	std::string query = url.substr(qpos + 1);
	std::string target = param + "=";
	auto pos = query.find(target);
	if (pos == std::string::npos) return "";
	size_t start = pos + target.size();
	auto end = query.find('&', start);
	std::string value = (end == std::string::npos) ? query.substr(start) : query.substr(start, end - start);
	return UrlDecode(value);
}

static std::string ExtractPath(const std::string& url) {
	auto qpos = url.find('?');
	return (qpos == std::string::npos) ? url : url.substr(0, qpos);
}

// Task 10: Extract Origin header value from an HTTP request.
static std::string ExtractOrigin(const std::string& request) {
	std::string lower = ToLowerAscii(request);
	auto pos = lower.find("origin:");
	if (pos == std::string::npos) return "";
	pos = request.find(':', pos) + 1;
	while (pos < request.size() && (request[pos] == ' ' || request[pos] == '\t')) pos++;
	auto end = request.find("\r\n", pos);
	if (end == std::string::npos) end = request.size();
	return request.substr(pos, end - pos);
}

// Task 10: Check if an Origin is allowed by the comma-separated allowlist.
// Empty allowlist = allow all (backward compatible).
static bool IsOriginAllowed(const std::string& origin, const std::string& allowlist) {
	if (allowlist.empty()) return true;
	std::stringstream ss(allowlist);
	std::string item;
	while (std::getline(ss, item, ',')) {
		auto start = item.find_first_not_of(" \t");
		auto end = item.find_last_not_of(" \t");
		if (start == std::string::npos) continue;
		std::string trimmed = item.substr(start, end - start + 1);
		if (trimmed == "*" || trimmed == origin) return true;
	}
	return false;
}

// Send raw bytes to a socket with a simple retry loop (used for SSE).
static bool SendRaw(SOCKET sock, const std::string& data) {
	int total = (int)data.size();
	int sent = 0;
	while (sent < total) {
		int r = send(sock, data.data() + sent, total - sent, 0);
		if (r <= 0) return false;
		sent += r;
	}
	return true;
}

static std::string GetMimeType(const std::string& path) {
	auto dot = path.rfind('.');
	if (dot == std::string::npos) return "application/octet-stream";
	std::string ext = path.substr(dot + 1);
	if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
	if (ext == "css") return "text/css; charset=utf-8";
	if (ext == "js" || ext == "mjs") return "application/javascript; charset=utf-8";
	if (ext == "json") return "application/json; charset=utf-8";
	if (ext == "png") return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "svg") return "image/svg+xml";
	if (ext == "ico") return "image/x-icon";
	if (ext == "woff" || ext == "woff2") return "font/woff2";
	if (ext == "ttf") return "font/ttf";
	return "application/octet-stream";
}

// ============================================================================
//  Embedded asset lookup — finds RCDATA resources baked into cs2.exe by URL.
//  Resource names are URL paths (e.g. "/index.html"); see webRadar_resources.rc.
// ============================================================================
namespace webradar {

bool FindEmbeddedAsset(const std::string& urlPath, EmbeddedAsset* out) {
	if (!out || urlPath.empty())
		return false;

	// rc.exe uppercases all string resource names at compile time, so the
	// lookup key must be uppercased to match (FindResourceA is case-sensitive).
	std::string upperPath = urlPath;
	for (auto& c : upperPath) c = (char)toupper((unsigned char)c);

	const HMODULE hModule = GetModuleHandleA(nullptr);
	const HRSRC hRes = FindResourceA(hModule, upperPath.c_str(), MAKEINTRESOURCEA(10)); // RT_RCDATA
	if (!hRes)
		return false;

	const HGLOBAL hData = LoadResource(hModule, hRes);
	if (!hData)
		return false;

	const void* ptr = LockResource(hData);
	const DWORD sz = SizeofResource(hModule, hRes);
	if (!ptr || sz == 0)
		return false;

	out->data = ptr;
	out->size = static_cast<size_t>(sz);
	return true;
}

} // namespace webradar

static bool ServeStaticFile(SOCKET clientSock, const std::string& httpPath) {
	// Sanitize path: no ".." traversal
	if (httpPath.find("..") != std::string::npos) return false;

	// Normalize: map "/" and "" to "/index.html" for both embedded + filesystem lookup.
	std::string servePath = (httpPath == "/" || httpPath.empty()) ? "/index.html" : httpPath;

	// Browsers auto-request /favicon.ico; the actual asset lives at /img/favicon.ico.
	if (servePath == "/favicon.ico") servePath = "/img/favicon.ico";

	// 1. Try embedded resource first (single-file deployment).
	webradar::EmbeddedAsset asset{};
	if (webradar::FindEmbeddedAsset(servePath, &asset)) {
		std::string content(static_cast<const char*>(asset.data), asset.size);
		std::string mime = GetMimeType(servePath);
		// HTML/JS/CSS are volatile (index.html changes, JS/CSS are hash-named but
		// referenced by index.html); everything else (icons, map data, fonts) is
		// immutable and can be cached aggressively.
		bool volatileAsset = (servePath == "/index.html" ||
			mime == "application/javascript; charset=utf-8" ||
			mime == "text/css; charset=utf-8");
		std::string cacheControl = volatileAsset ? "no-store" : "public, max-age=86400";
		std::string header =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: " + mime + "\r\n"
			"Content-Length: " + std::to_string(content.size()) + "\r\n"
			"Cache-Control: " + cacheControl + "\r\n"
			"Connection: close\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"\r\n";
		send(clientSock, header.c_str(), (int)header.size(), 0);
		send(clientSock, content.c_str(), (int)content.size(), 0);
		LOG_TRACE("WebRadar", "Served embedded {} ({} bytes, {})", servePath, content.size(), mime);
		return true;
	}

	// 2. Fallback: serve from filesystem (dev mode / dist directory).
	if (g_webappDir.empty()) {
		LOG_ERROR("WebRadar", "ServeStaticFile: g_webappDir is empty, cannot serve {}", httpPath);
		return false;
	}

	std::string filePath = g_webappDir;
	{
		std::string rel = servePath;
		for (auto& c : rel) if (c == '/') c = '\\';
		filePath += rel;
	}

	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open()) return false;

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	std::string mime = GetMimeType(filePath);
	std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + mime + "\r\n"
		"Content-Length: " + std::to_string(content.size()) + "\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n";

	send(clientSock, header.c_str(), (int)header.size(), 0);
	send(clientSock, content.c_str(), (int)content.size(), 0);
	LOG_TRACE("WebRadar", "Served {} ({} bytes, {})", httpPath, content.size(), mime);
	return true;
}

// ============================================================================
//  Minimal SHA-1 (RFC 3174) — only used for the WebSocket handshake key.
// ============================================================================
namespace {

struct SHA1Ctx {
	uint32_t state[5]{};
	uint64_t count = 0;
	uint8_t  buffer[64]{};

	SHA1Ctx() { reset(); }

	void reset() {
		state[0] = 0x67452301; state[1] = 0xEFCDAB89;
		state[2] = 0x98BADCFE; state[3] = 0x10325476;
		state[4] = 0xC3D2E1F0;
		count = 0;
		memset(buffer, 0, 64);
	}

	static uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

	void transform(const uint8_t block[64]) {
		uint32_t w[80];
		for (int i = 0; i < 16; i++)
			w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
		for (int i = 16; i < 80; i++)
			w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

		uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
		for (int i = 0; i < 80; i++) {
			uint32_t f, k;
			if      (i < 20) { f = (b & c) | ((~b) & d);           k = 0x5A827999; }
			else if (i < 40) { f = b ^ c ^ d;                       k = 0x6ED9EBA1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d);    k = 0x8F1BBCDC; }
			else              { f = b ^ c ^ d;                       k = 0xCA62C1D6; }
			uint32_t tmp = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(b, 30); b = a; a = tmp;
		}
		state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
	}

	void update(const uint8_t* data, size_t len) {
		size_t idx = (size_t)(count % 64);
		count += len;
		for (size_t i = 0; i < len; i++) {
			buffer[idx++] = data[i];
			if (idx == 64) { transform(buffer); idx = 0; }
		}
	}

	void update(const std::string& s) { update((const uint8_t*)s.data(), s.size()); }

	std::string digest() {
		uint64_t bits = count * 8;
		uint8_t pad = 0x80;
		update(&pad, 1);
		pad = 0;
		while (count % 64 != 56) update(&pad, 1);
		uint8_t bits_be[8];
		for (int i = 7; i >= 0; i--) { bits_be[i] = (uint8_t)(bits & 0xFF); bits >>= 8; }
		update(bits_be, 8);

		std::string result(20, '\0');
		for (int i = 0; i < 5; i++) {
			result[i*4]   = (char)(state[i] >> 24);
			result[i*4+1] = (char)(state[i] >> 16);
			result[i*4+2] = (char)(state[i] >> 8);
			result[i*4+3] = (char)(state[i]);
		}
		return result;
	}
};

std::string sha1(const std::string& input) {
	SHA1Ctx ctx;
	ctx.update(input);
	return ctx.digest();
}

std::string base64_encode_raw(const std::string& bytes) {
	return base64::encode_into<std::string>(bytes.begin(), bytes.end());
}

static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

} // anonymous namespace

// ============================================================================
//  GetLocalIP — returns first LAN IPv4 address for GUI display
// ============================================================================

std::string GetLocalIP() {
	char hostname[256] = {};
	if (gethostname(hostname, sizeof(hostname)) != 0) return "";

	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo* result = nullptr;
	if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 || !result) return "";

	char ipStr[INET_ADDRSTRLEN] = {};
	auto* sin = (sockaddr_in*)result->ai_addr;
	inet_ntop(AF_INET, &sin->sin_addr, ipStr, sizeof(ipStr));
	freeaddrinfo(result);
	return ipStr;
}

// ============================================================================
//  WebRadarServer implementation
// ============================================================================

WebRadarServer::~WebRadarServer() {
	Stop();
}

bool WebRadarServer::Start(uint16_t port) {
	if (m_running.load()) return true;

	m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_listenSock == INVALID_SOCKET) {
		LOG_ERROR("WebRadar", "socket() failed: {}", WSAGetLastError());
		return false;
	}

	int opt = 1;
	setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		LOG_ERROR("WebRadar", "bind() on port {} failed: {}", port, WSAGetLastError());
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
		return false;
	}

	if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR) {
		LOG_ERROR("WebRadar", "listen() failed: {}", WSAGetLastError());
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
		return false;
	}

	m_running.store(true);
	m_acceptThread = std::thread(&WebRadarServer::AcceptLoop, this);

	LOG_INFO("WebRadar", "WebSocket server listening on 0.0.0.0:{}", port);
	return true;
}

void WebRadarServer::Stop() {
	// Stop the worker first so it stops writing m_latestPayload while we tear down.
	StopWorker();
	if (!m_running.load()) return;
	m_running.store(false);

	if (m_listenSock != INVALID_SOCKET) {
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
	}

	if (m_acceptThread.joinable())
		m_acceptThread.join();

	{
		std::lock_guard<std::mutex> lock(m_clientsMutex);
		for (auto s : m_clients) closesocket(s);
		m_clients.clear();
	}

	// Close SSE clients too
	{
		std::lock_guard<std::mutex> lock(m_sseMutex);
		for (auto s : m_sseClients) closesocket(s);
		m_sseClients.clear();
	}

	LOG_INFO("WebRadar", "Server stopped");
}

int WebRadarServer::GetClientCount() const {
	int count = 0;
	{
		std::lock_guard<std::mutex> lock(m_clientsMutex);
		count += (int)m_clients.size();
	}
	{
		std::lock_guard<std::mutex> lock(m_sseMutex);
		count += (int)m_sseClients.size();
	}
	return count;
}

void WebRadarServer::AcceptLoop() {
	while (m_running.load()) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(m_listenSock, &readSet);
		timeval tv{ 0, 500000 }; // 500 ms poll

		int sel = select(0, &readSet, nullptr, nullptr, &tv);
		if (sel <= 0) continue;

		SOCKET clientSock = accept(m_listenSock, nullptr, nullptr);
		if (clientSock == INVALID_SOCKET) continue;

		// Set send timeout so Broadcast doesn't block on slow clients
		DWORD sendTimeout = 2000;
		setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout));

		LOG_TRACE("WebRadar", "New connection accepted");

		// Detached thread per client — lightweight; radar typically has 1–3 clients
		std::thread([this, clientSock]() {
			this->ClientLoop(clientSock);
		}).detach();
	}
}

bool WebRadarServer::DoHandshakeWithRequest(SOCKET clientSock, const std::string& request) {
	LOG_DEBUG("WebRadar", "Handshake: received {} bytes", request.size());

	std::string lowerRequest = ToLowerAscii(request);

	// Extract Sec-WebSocket-Key
	auto keyPos = lowerRequest.find("sec-websocket-key:");
	if (keyPos == std::string::npos) return false;
	keyPos = request.find(':', keyPos) + 1;
	while (keyPos < request.size() && request[keyPos] == ' ') keyPos++;
	auto keyEnd = request.find("\r\n", keyPos);
	if (keyEnd == std::string::npos) return false;
	std::string wsKey = request.substr(keyPos, keyEnd - keyPos);

	// Compute Sec-WebSocket-Accept per RFC 6455
	std::string acceptHash = sha1(wsKey + WS_GUID);
	std::string acceptB64 = base64_encode_raw(acceptHash);

	std::string response =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: " + acceptB64 + "\r\n"
		"\r\n";

	LOG_DEBUG("WebRadar", "Handshake: sending 101 response ({} bytes)", response.size());
	return send(clientSock, response.c_str(), (int)response.size(), 0) > 0;
}

void WebRadarServer::ClientLoop(SOCKET clientSock) {
	// Peek at the request to determine if it's HTTP or WebSocket
	char buf[4096];
	int received = recv(clientSock, buf, sizeof(buf) - 1, 0);
	if (received <= 0) { closesocket(clientSock); return; }
	buf[received] = '\0';
	std::string request(buf, received);
	std::string lowerRequest = ToLowerAscii(request);

	// Log first line of request for debugging
	auto firstLineEnd = request.find("\r\n");
	std::string firstLine = (firstLineEnd != std::string::npos) ? request.substr(0, firstLineEnd) : request.substr(0, 80);
	LOG_DEBUG("WebRadar", "ClientLoop: received {} bytes, first line: {}", received, firstLine);

	// If not a WebSocket upgrade, try serving static files
	if (lowerRequest.find("upgrade:") == std::string::npos) {
		LOG_TRACE("WebRadar", "HTTP request: {}", firstLine);
		// Extract the HTTP path from GET line
		auto pathStart = request.find(' ');
		if (pathStart != std::string::npos) {
			auto pathEnd = request.find(' ', pathStart + 1);
			if (pathEnd != std::string::npos) {
				std::string httpPath = request.substr(pathStart + 1, pathEnd - pathStart - 1);
				std::string cleanPath = ExtractPath(httpPath);

				// Task 10: Origin whitelist for /api/* paths
				bool isApiPath = (cleanPath.rfind("/api/", 0) == 0);
				std::string origin = isApiPath ? ExtractOrigin(request) : "";
				std::string corsOrigin = "*";
				if (isApiPath) {
					if (!IsOriginAllowed(origin, MenuConfig::WebRadarOriginAllowlist)) {
						std::string body = "Origin not allowed";
						std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
						send(clientSock, resp.c_str(), (int)resp.size(), 0);
						closesocket(clientSock);
						return;
					}
					// Reflect the specific origin if whitelist is set, else *
					corsOrigin = MenuConfig::WebRadarOriginAllowlist.empty() ? "*" : origin;
				}

				// Task 8: SSE endpoint /api/stream
				if (cleanPath == "/api/stream") {
					// Shorter send timeout for SSE (matches KevqDMA reference)
					DWORD sseTimeout = 600;
					setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sseTimeout, sizeof(sseTimeout));
					const BOOL noDelay = TRUE;
					setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

					std::string headers =
						"HTTP/1.1 200 OK\r\n"
						"Content-Type: text/event-stream\r\n"
						"Cache-Control: no-store\r\n"
						"Connection: keep-alive\r\n"
						"Access-Control-Allow-Origin: " + corsOrigin + "\r\n"
						"\r\n";
					if (!SendRaw(clientSock, headers)) {
						closesocket(clientSock);
						return;
					}

					// Send initial payload if available
					std::string initialPayload = GetLatestPayloadForPolling();
					if (!initialPayload.empty()) {
						std::string sseEvent = "data: " + initialPayload + "\n\n";
						SendRaw(clientSock, sseEvent);
					}

					// Register as SSE client so Broadcast() pushes to it
					{
						std::lock_guard<std::mutex> lock(m_sseMutex);
						m_sseClients.push_back(clientSock);
					}
					LOG_INFO("WebRadar", "SSE client connected");

					// Monitoring loop — detect client disconnect.
					// SSE is server-push only; any readable event means close/error.
					while (m_running.load()) {
						fd_set readSet;
						FD_ZERO(&readSet);
						FD_SET(clientSock, &readSet);
						timeval tv{ 1, 0 };
						int sel = select(0, &readSet, nullptr, nullptr, &tv);
						if (sel < 0) break;
						if (sel == 0) continue;
						char buf[16];
						int r = recv(clientSock, buf, sizeof(buf), 0);
						if (r <= 0) break;
						// Discard client data (SSE is one-way)
					}

					RemoveSseClient(clientSock);
					closesocket(clientSock);
					LOG_INFO("WebRadar", "SSE client disconnected");
					return;
				}

				// Task 8: HTTP polling endpoint /api/live
			if (cleanPath == "/api/live") {
				std::string payload = GetLatestPayloadForPolling();
				std::string resp =
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: application/json\r\n"
					"Cache-Control: no-store\r\n"
					"Access-Control-Allow-Origin: " + corsOrigin + "\r\n"
					"Content-Length: " + std::to_string(payload.size()) + "\r\n"
					"Connection: close\r\n"
					"\r\n" + payload;
				send(clientSock, resp.c_str(), (int)resp.size(), 0);
				closesocket(clientSock);
				return;
			}

			// Task 14: RTT measurement endpoint — lightweight, no auth.
			// Returns server-side unix-ms timestamp so the frontend can compute
			// round-trip time via fetch("/api/ping").
			if (cleanPath == "/api/ping") {
				auto now = std::chrono::system_clock::now();
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch()).count();
				std::string body = "{\"t\":" + std::to_string(ms) + "}";
				std::string resp =
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: application/json\r\n"
					"Cache-Control: no-store\r\n"
					"Access-Control-Allow-Origin: " + corsOrigin + "\r\n"
					"Content-Length: " + std::to_string(body.size()) + "\r\n"
					"Connection: close\r\n"
					"\r\n" + body;
				send(clientSock, resp.c_str(), (int)resp.size(), 0);
				closesocket(clientSock);
				return;
			}

				if (cleanPath == "/cs2_auth_status") {
					std::string json = "{\"password_required\":" + std::string(MenuConfig::WebRadarPasswordEnabled ? "true" : "false") + "}";
					std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + json;
					send(clientSock, resp.c_str(), (int)resp.size(), 0);
				} else if (!ServeStaticFile(clientSock, cleanPath)) {
					std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found";
					send(clientSock, resp.c_str(), (int)resp.size(), 0);
				}
			}
		}
		closesocket(clientSock);
		return;
	}

	// WebSocket upgrade — check password if enabled
	{
		auto pathStart = request.find(' ');
		if (pathStart != std::string::npos) {
			auto pathEnd = request.find(' ', pathStart + 1);
			if (pathEnd != std::string::npos) {
				std::string wsUrl = request.substr(pathStart + 1, pathEnd - pathStart - 1);
				if (MenuConfig::WebRadarPasswordEnabled) {
					std::string clientPassword = ExtractQueryParam(wsUrl, "password");
					if (clientPassword != MenuConfig::WebRadarPassword) {
						LOG_WARNING("WebRadar", "WebSocket auth failed: wrong password");
						std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nContent-Length: 12\r\nConnection: close\r\n\r\nUnauthorized";
						send(clientSock, resp.c_str(), (int)resp.size(), 0);
						closesocket(clientSock);
						return;
					}
				}
			}
		}
	}

	// WebSocket handshake using the already-read data
	LOG_INFO("WebRadar", "WebSocket upgrade request: {}", firstLine);
	if (!DoHandshakeWithRequest(clientSock, request)) {
		closesocket(clientSock);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_clientsMutex);
		m_clients.push_back(clientSock);
	}
	LOG_INFO("WebRadar", "Handshake OK, clients: {}", GetClientCount());

	// Read loop: handle ping/close from browser, ignore data frames.
	// Task 10.2: handle continuation frames (opcode 0x0) per RFC 6455 §5.4.
	std::string fragmentBuffer;
	bool fragmenting = false;
	while (m_running.load()) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(clientSock, &readSet);
		timeval tv{ 1, 0 };

		int sel = select(0, &readSet, nullptr, nullptr, &tv);
		if (sel < 0) {
			LOG_DEBUG("WebRadar", "ClientLoop: select() failed ({})", WSAGetLastError());
			break;
		}
		if (sel == 0) continue;

		uint8_t header[2];
		int received = recv(clientSock, (char*)header, 2, 0);
		if (received <= 0) {
			LOG_WARNING("WebRadar", "ClientLoop: recv header failed (ret={}, err={})", received, WSAGetLastError());
			break;
		}

		uint8_t opcode = header[0] & 0x0F;
		bool fin = (header[0] & 0x80) != 0;
		bool masked = (header[1] & 0x80) != 0;
		uint64_t payloadLen = header[1] & 0x7F;

		if (payloadLen == 126) {
			uint8_t ext[2];
			if (recv(clientSock, (char*)ext, 2, 0) != 2) {
				LOG_DEBUG("WebRadar", "ClientLoop: recv ext16 failed");
				break;
			}
			payloadLen = ((uint64_t)ext[0] << 8) | ext[1];
		} else if (payloadLen == 127) {
			uint8_t ext[8];
			if (recv(clientSock, (char*)ext, 8, 0) != 8) {
				LOG_DEBUG("WebRadar", "ClientLoop: recv ext64 failed");
				break;
			}
			payloadLen = 0;
			for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | ext[i];
		}

		uint8_t maskKey[4]{};
		if (masked && recv(clientSock, (char*)maskKey, 4, 0) != 4) {
			LOG_DEBUG("WebRadar", "ClientLoop: recv maskKey failed");
			break;
		}

		// Read payload (client data is discarded, but fragment buffer is maintained)
		if (payloadLen > 65536) {
			LOG_DEBUG("WebRadar", "ClientLoop: payload too large ({})", payloadLen);
			break;
		}
		std::string payload((size_t)payloadLen, '\0');
		size_t totalRead = 0;
		while (totalRead < payloadLen) {
			int r = recv(clientSock, &payload[totalRead], (int)(payloadLen - totalRead), 0);
			if (r <= 0) goto disconnect;
			totalRead += r;
		}

		// Unmask payload if needed (for fragment buffer correctness)
		if (masked && payloadLen > 0) {
			for (size_t i = 0; i < payloadLen; i++)
				payload[i] ^= maskKey[i % 4];
		}

		if (opcode == 0x8) {
			// Close frame payload: first 2 bytes = close code (big-endian)
			uint16_t closeCode = 0;
			if (payloadLen >= 2) {
				closeCode = ((uint8_t)payload[0]) << 8;
				closeCode |= (uint8_t)payload[1];
			}
			LOG_WARNING("WebRadar", "ClientLoop: received close frame (code={}, payloadLen={})", closeCode, payloadLen);
			break;
		}

		if (opcode == 0x9) { // Ping → Pong
			uint8_t pong[2] = { 0x8A, 0x00 };
			send(clientSock, (const char*)pong, 2, 0);
			continue;
		}

		if (opcode == 0xA) { // Pong — ignore
			continue;
		}

		// Task 10.2: WebSocket fragmentation (RFC 6455 §5.4)
		if (opcode == 0x0) { // Continuation frame
			if (!fragmenting) {
				LOG_DEBUG("WebRadar", "ClientLoop: unexpected continuation frame");
				break;
			}
			fragmentBuffer += payload;
			if (fin) {
				// Complete fragmented message — discard (we don't process client data)
				fragmentBuffer.clear();
				fragmenting = false;
			}
		} else if (opcode == 0x1 || opcode == 0x2) { // Text or Binary
			if (fragmenting) {
				LOG_DEBUG("WebRadar", "ClientLoop: new data frame while fragmenting");
				break;
			}
			if (!fin) {
				// Start of fragmented message
				fragmentBuffer = payload;
				fragmenting = true;
			}
			// else: complete unfragmented message — discard
		}
	}

disconnect:
	RemoveClient(clientSock);
	closesocket(clientSock);
	LOG_INFO("WebRadar", "Client disconnected, clients: {}", GetClientCount());
}

bool WebRadarServer::DoHandshake(SOCKET clientSock) {
	char buf[4096];
	int received = recv(clientSock, buf, sizeof(buf) - 1, 0);
	if (received <= 0) return false;
	buf[received] = '\0';
	std::string request(buf, received);
	return DoHandshakeWithRequest(clientSock, request);
}

// P0 fix #2: extract frame-building so Broadcast can construct once and reuse
// for all clients (avoids per-client std::vector heap allocation).
static void BuildFrame(const std::string& payload, std::vector<uint8_t>& out) {
	out.clear();
	out.reserve(10 + payload.size());

	// FIN=1, opcode=0x1 (text) - JSON payload, browser receives as string directly
	out.push_back(0x81);

	size_t len = payload.size();
	if (len < 126) {
		out.push_back((uint8_t)len);
	} else if (len < 65536) {
		out.push_back(126);
		out.push_back((uint8_t)(len >> 8));
		out.push_back((uint8_t)(len & 0xFF));
	} else {
		out.push_back(127);
		for (int i = 7; i >= 0; i--)
			out.push_back((uint8_t)((len >> (i * 8)) & 0xFF));
	}

	out.insert(out.end(), payload.begin(), payload.end());
}

bool WebRadarServer::SendFrame(SOCKET sock, const std::string& payload) {
	std::vector<uint8_t> frame;
	BuildFrame(payload, frame);

	int total = (int)frame.size();
	int sent = 0;
	while (sent < total) {
		int r = send(sock, (const char*)frame.data() + sent, total - sent, 0);
		if (r <= 0) return false;
		sent += r;
	}
	return true;
}

void WebRadarServer::Broadcast(const std::string& message) {
	auto t0 = std::chrono::steady_clock::now();

	// Save latest payload for /api/live and /api/stream initial event.
	{
		std::lock_guard<std::mutex> lock(m_lastPayloadMutex);
		m_lastBroadcastPayload = message;
	}

	int sentCount = 0;

	// P0 fix #1+#2: build frame once, snapshot clients, send outside lock.
	// Avoids serializing all clients behind m_clientsMutex and avoids
	// per-client frame allocation.
	std::vector<uint8_t> wsFrame;
	BuildFrame(message, wsFrame);

	std::vector<SOCKET> wsClients;
	{
		std::lock_guard<std::mutex> lock(m_clientsMutex);
		wsClients = m_clients;
	}
	LOG_TRACE("WebRadar", "Broadcast: {} bytes to {} ws clients", message.size(), wsClients.size());

	std::vector<SOCKET> deadClients;
	int wsTotal = (int)wsFrame.size();
	for (SOCKET sock : wsClients) {
		int sent = 0;
		bool ok = true;
		while (sent < wsTotal) {
			int r = send(sock, (const char*)wsFrame.data() + sent, wsTotal - sent, 0);
			if (r <= 0) { ok = false; break; }
			sent += r;
		}
		if (!ok) {
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT) {
				// Task 9: slow client - skip this broadcast, keep connection
				m_stats.droppedFramesSlowClient++;
				LOG_DEBUG("WebRadar", "SendFrame timed out, skipping slow client");
			} else {
				LOG_DEBUG("WebRadar", "SendFrame failed (err={}), will remove client", err);
				deadClients.push_back(sock);
			}
		} else {
			sentCount++;
			// Task 17: accumulate bytes actually pushed to clients.
			m_stats.bytesOutTotal += message.size();
		}
	}

	// Batch-remove dead clients under lock.
	if (!deadClients.empty()) {
		std::lock_guard<std::mutex> lock(m_clientsMutex);
		for (SOCKET s : deadClients) {
			m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), s), m_clients.end());
		}
	}

	// Send to SSE clients (Task 8)
	{
		std::lock_guard<std::mutex> lock(m_sseMutex);
		if (!m_sseClients.empty()) {
			std::string sseMsg = "data: " + message + "\n\n";
			LOG_TRACE("WebRadar", "Broadcast: {} bytes to {} sse clients", sseMsg.size(), m_sseClients.size());
			for (auto it = m_sseClients.begin(); it != m_sseClients.end(); ) {
				if (!SendRaw(*it, sseMsg)) {
					int err = WSAGetLastError();
					if (err == WSAETIMEDOUT) {
						m_stats.droppedFramesSlowClient++;
						++it;
					} else {
						// Don't closesocket — SSE ClientLoop owns the socket.
						it = m_sseClients.erase(it);
					}
				} else {
					sentCount++;
					// Task 17: accumulate bytes actually pushed to clients.
					m_stats.bytesOutTotal += sseMsg.size();
					++it;
				}
			}
		}
	}

	// Task 9: update stats
	if (sentCount > 0) {
		m_stats.framesSent++;
	} else {
		m_stats.framesDropped++;
	}
	auto t1 = std::chrono::steady_clock::now();
	uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
	m_stats.broadcastUsTotal += us;
	m_stats.broadcastUsCount++;
}

void WebRadarServer::BroadcastPageUpdate() {
	Broadcast("{\"type\":\"pageUpdate\"}");
}

void WebRadarServer::RemoveClient(SOCKET sock) {
	std::lock_guard<std::mutex> lock(m_clientsMutex);
	m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), sock), m_clients.end());
}

void WebRadarServer::RemoveSseClient(SOCKET sock) {
	std::lock_guard<std::mutex> lock(m_sseMutex);
	m_sseClients.erase(std::remove(m_sseClients.begin(), m_sseClients.end(), sock), m_sseClients.end());
}

// ============================================================================
//  Webapp directory discovery (filesystem fallback for dev mode)
//  Embedded RCDATA resources are always tried first by ServeStaticFile;
//  the filesystem is only used when the binary runs from a source checkout.
// ============================================================================

static std::string FindWebappDir() {
	char exePath[MAX_PATH]{};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::string exeDir(exePath);
	exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));

	// Look for the webapp source directory (plain static files, no build step).
	const char* candidates[] = {
		"\\external\\webradar\\webapp",
		"\\webapp",
	};
	for (auto rel : candidates) {
		std::string path = exeDir + rel;
		if (GetFileAttributesA((path + "\\index.html").c_str()) != INVALID_FILE_ATTRIBUTES)
			return path;
	}
	return {};
}

// ============================================================================
//  JSON Serialization — converts GameSnapshot → cs2_webradar JSON format
// ============================================================================

static std::string CleanMapName(const char* raw) {
	std::string name = SanitizeUtf8(std::string(raw));
	if (name.find("maps/") == 0) name = name.substr(5);
	auto pos = name.find(".vpk");
	if (pos != std::string::npos) name = name.substr(0, pos);
	pos = name.find(".bsp");
	if (pos != std::string::npos) name = name.substr(0, pos);
	if (name.empty() || name.find("<empty>") != std::string::npos)
		return "invalid";
	return name;
}

// Weapon category for slot assignment (only active weapon is known via DMA)
enum class WeaponSlot { Unknown, Primary, Secondary, Melee, Utility, C4 };
static WeaponSlot CategorizeWeapon(const std::string& name) {
	if (name.empty() || name == "None" || name == "Weapon_None") return WeaponSlot::Unknown;
	// Pistols
	if (name == "glock" || name == "hkp2000" || name == "usp_silencer" || name == "p250" ||
		name == "tec9" || name == "elite" || name == "fiveseven" || name == "deagle" ||
		name == "revolver" || name == "cz75a") return WeaponSlot::Secondary;
	// Primary
	if (name == "ak47" || name == "m4a1" || name == "m4a1_silencer" || name == "aug" ||
		name == "sg556" || name == "famas" || name == "galilar" || name == "awp" ||
		name == "ssg08" || name == "scar20" || name == "g3sg1" || name == "mac10" ||
		name == "mp9" || name == "mp7" || name == "mp5sd" || name == "ump45" ||
		name == "p90" || name == "bizon" || name == "nova" || name == "xm1014" ||
		name == "sawedoff" || name == "mag7" || name == "m249" || name == "negev")
		return WeaponSlot::Primary;
	// Melee
	if (name.find("knife") != std::string::npos || name.find("bayonet") != std::string::npos ||
		name == "taser") return WeaponSlot::Melee;
	// Grenades
	if (name == "hegrenade" || name == "flashbang" || name == "smokegrenade" ||
		name == "molotov" || name == "incgrenade" || name == "decoy")
		return WeaponSlot::Utility;
	if (name == "c4") return WeaponSlot::C4;
	return WeaponSlot::Unknown;
}

// Clean weapon name: strip "weapon_" prefix artifacts and normalize
static std::string CleanWeaponName(const std::string& raw) {
	if (raw.empty() || raw == "Weapon_None" || raw == "None") return "";
	return raw;
}

// Build m_weapons JSON object from full weapon list + active weapon
static void BuildWeaponsObject(rapidjson::Value& weapons, const std::string& rawActive,
	const std::vector<std::string>& weaponList, int ammoClip, rapidjson::Document::AllocatorType& a) {
	weapons.SetObject();
	std::string active = CleanWeaponName(rawActive);
	if (!active.empty())
		weapons.AddMember("m_active", rapidjson::Value(active.c_str(), a), a);

	if (ammoClip >= 0)
		weapons.AddMember("m_ammo_clip", ammoClip, a);

	// If we have a full weapon list, use it for proper categorization
	const auto& src = weaponList.empty() ? std::vector<std::string>{active} : weaponList;

	std::string primary, secondary;
	rapidjson::Value melee(rapidjson::kArrayType);
	rapidjson::Value utilities(rapidjson::kArrayType);

	for (const auto& w : src) {
		std::string name = CleanWeaponName(w);
		if (name.empty()) continue;
		WeaponSlot slot = CategorizeWeapon(name);
		switch (slot) {
		case WeaponSlot::Primary:   if (primary.empty()) primary = name; break;
		case WeaponSlot::Secondary: if (secondary.empty()) secondary = name; break;
		case WeaponSlot::Melee:     melee.PushBack(rapidjson::Value(name.c_str(), a), a); break;
		case WeaponSlot::Utility:   utilities.PushBack(rapidjson::Value(name.c_str(), a), a); break;
		default: break;
		}
	}
	if (!primary.empty())
		weapons.AddMember("m_primary", rapidjson::Value(primary.c_str(), a), a);
	if (!secondary.empty())
		weapons.AddMember("m_secondary", rapidjson::Value(secondary.c_str(), a), a);
	if (melee.Size() > 0)
		weapons.AddMember("m_melee", melee, a);
	if (utilities.Size() > 0)
		weapons.AddMember("m_utilities", utilities, a);
}

// Serialize a single player entity into JSON
static void SerializePlayer(rapidjson::Value& players, const CEntity& e,
	int idx, bool isDead, bool hasBomb, bool isLocal, rapidjson::Document::AllocatorType& a)
{
	rapidjson::Value p(rapidjson::kObjectType);
	p.AddMember("m_idx", idx, a);
	p.AddMember("m_name", rapidjson::Value(SanitizeUtf8(e.Controller.PlayerName).c_str(), a), a);
	// Color: -1 means unassigned, clamp to 0-5 range for frontend (6 colors)
	int color = e.Controller.Color;
	if (color < 0 || color > 5) color = idx % 6;
	p.AddMember("m_color", color, a);
	p.AddMember("m_team", e.Controller.TeamID, a);
	p.AddMember("m_health", e.Pawn.Health, a);
	p.AddMember("m_is_dead", isDead, a);
	p.AddMember("m_is_local", isLocal, a);
	p.AddMember("m_armor", e.Pawn.Armor, a);
	p.AddMember("m_money", e.Controller.Money, a);

	rapidjson::Value pos(rapidjson::kObjectType);
	pos.AddMember("x", e.Pawn.Pos.x, a);
	pos.AddMember("y", e.Pawn.Pos.y, a);
	pos.AddMember("z", e.Pawn.Pos.z, a);
	p.AddMember("m_position", pos, a);

	// m_velocity: for frontend velocity extrapolation. [0,0,0] when dead/unreadable (Task 7)
	rapidjson::Value vel(rapidjson::kArrayType);
	if (isDead) {
		vel.PushBack(0.0f, a).PushBack(0.0f, a).PushBack(0.0f, a);
	} else {
		vel.PushBack(e.Pawn.Velocity.x, a).PushBack(e.Pawn.Velocity.y, a).PushBack(e.Pawn.Velocity.z, a);
	}
	p.AddMember("m_velocity", vel, a);

	// Convert CS2 ViewAngle.y (-180..180) to BoltObserv compass angle (0..360).
	// BoltObserv's wrap-around logic in loopFast.js assumes 0..360 range;
	// without this conversion, crossing the 180/-180 boundary causes the
	// dot to spin 360 degrees.
	float compassAngle = 90.0f - e.Pawn.ViewAngle.y;
	compassAngle = fmodf(compassAngle, 360.0f);
	if (compassAngle < 0) compassAngle += 360.0f;
	p.AddMember("m_eye_angle", compassAngle, a);

	// m_flashed: FlashDuration (seconds remaining). Populated by DataThread
	// via Offset::flFlashDuration; defaults to 0.0f when unreadable.
	p.AddMember("m_flashed", e.Pawn.FlashDuration, a);

	rapidjson::Value weapons(rapidjson::kObjectType);
	BuildWeaponsObject(weapons, e.Pawn.WeaponName, e.Pawn.WeaponList, e.Pawn.AmmoClip, a);
	p.AddMember("m_weapons", weapons, a);

	// m_ammo_clip at player root level: the frontend (_socket.js) reads
	// p.m_ammo_clip directly. Also kept inside m_weapons for backward compat.
	p.AddMember("m_ammo_clip", e.Pawn.AmmoClip, a);

	p.AddMember("m_has_helmet", e.Pawn.HasHelmet, a);
	p.AddMember("m_has_defuser", e.Pawn.HasDefuser, a);
	p.AddMember("m_has_bomb", hasBomb, a);
	p.AddMember("m_model_name", rapidjson::Value(SanitizeUtf8(e.Pawn.ModelName).c_str(), a), a);

	players.PushBack(p, a);
}

static std::string SerializeSnapshot(const GameSnapshot& snap) {
	rapidjson::Document doc;
	doc.SetObject();
	auto& a = doc.GetAllocator();

	doc.AddMember("m_local_team", snap.LocalPlayer.Controller.TeamID, a);
	doc.AddMember("m_round_phase", rapidjson::Value(snap.roundPhase, a), a);

	// Task 18: Resolve map name via registry. If the cleaned map name is
	// known (in our map registry), use it directly. If unknown (workshop
	// map or empty), prefix with "dynamic_" so the frontend can use
	// default bounds-projection coordinate conversion.
	std::string rawMapName = CleanMapName(snap.MapName);
	std::string mapName;
	if (const radar::MapDefinition* known = radar::FindMapByName(rawMapName)) {
		mapName = known->name;
	} else if (rawMapName.empty() || rawMapName == "invalid") {
		mapName = "dynamic_unknown";
	} else {
		mapName = "dynamic_" + rawMapName;
	}
	doc.AddMember("m_map", rapidjson::Value(mapName.c_str(), a), a);

	// Determine bomb carrier pawn index (lower 16 bits of entity handle)
	DWORD bombCarrierIdx = snap.Bomb.carrierPawnHandle & 0xFFFF;

	rapidjson::Value players(rapidjson::kArrayType);

	// Local player
	const auto& lp = snap.LocalPlayer;
	if (lp.Controller.TeamID >= 2
		&& std::isfinite(lp.Pawn.Pos.x) && std::isfinite(lp.Pawn.Pos.y) && std::isfinite(lp.Pawn.Pos.z)
		&& !(lp.Pawn.Pos.x == 0.0f && lp.Pawn.Pos.y == 0.0f && lp.Pawn.Pos.z == 0.0f)) {
		bool isDead = lp.Pawn.Health <= 0;
		bool hasBomb = (!isDead && !snap.Bomb.isPlanted && bombCarrierIdx != 0 && (lp.Controller.Pawn & 0xFFFF) == bombCarrierIdx);
		SerializePlayer(players, lp, snap.LocalPlayerIndex >= 0 ? snap.LocalPlayerIndex : 999, isDead, hasBomb, true, a);
	}

	// Other entities — use Controller.Pawn as stable ID (React key)
	for (size_t i = 0; i < snap.Entities.size(); i++) {
		const auto& e = snap.Entities[i];
		// Skip entities with invalid position (DMA jitter: NaN/Infinity or all-zero)
		if (!std::isfinite(e.Pawn.Pos.x) || !std::isfinite(e.Pawn.Pos.y) || !std::isfinite(e.Pawn.Pos.z))
			continue;
		if (e.Pawn.Pos.x == 0.0f && e.Pawn.Pos.y == 0.0f && e.Pawn.Pos.z == 0.0f)
			continue;
		bool isDead = e.Pawn.Health <= 0;
		bool hasBomb = (!isDead && !snap.Bomb.isPlanted && bombCarrierIdx != 0 && (e.Controller.Pawn & 0xFFFF) == bombCarrierIdx);
		SerializePlayer(players, e, (int)(e.Controller.Pawn & 0xFFFF), isDead, hasBomb, false, a);
	}

	doc.AddMember("m_players", players, a);

	// m_observed_idx: index into m_players of the pawn the local player is
	// currently spectating. Resolved by matching the local player's
	// ObserverTarget handle (lower 16 bits = entity index) against each
	// entity's Controller.Pawn handle. -1 when not spectating anyone
	// (first-person, alive, or target unreadable).
	int observedIdx = -1;
	if (snap.LocalObserverTarget != 0) {
		DWORD observedHandle = snap.LocalObserverTarget & 0xFFFF;
		bool localAdded = (lp.Controller.TeamID >= 2);
		for (size_t i = 0; i < snap.Entities.size(); i++) {
			if ((snap.Entities[i].Controller.Pawn & 0xFFFF) == observedHandle) {
				observedIdx = (int)i + (localAdded ? 1 : 0);
				break;
			}
		}
	}
	doc.AddMember("m_observed_idx", observedIdx, a);

	// Bomb data
	if (snap.Bomb.isPlanted || (snap.Bomb.x != 0 || snap.Bomb.y != 0) || snap.Bomb.carrierPawnHandle != 0) {
		rapidjson::Value bomb(rapidjson::kObjectType);
		bomb.AddMember("x", snap.Bomb.x, a);
		bomb.AddMember("y", snap.Bomb.y, a);
		bomb.AddMember("z", snap.Bomb.z, a);
		if (snap.Bomb.isPlanted) {
			bomb.AddMember("m_blow_time", snap.Bomb.blowTime, a);
			bomb.AddMember("m_is_defused", snap.Bomb.isDefused, a);
			bomb.AddMember("m_is_defusing", snap.Bomb.isDefusing, a);
			bomb.AddMember("m_defuse_time", snap.Bomb.defuseTime, a);
			if (snap.Bomb.isDefusing) {
				bomb.AddMember("m_defuser_handle", (int)snap.Bomb.defuserPawnHandle, a);
			}
		} else if (snap.Bomb.carrierPawnHandle != 0) {
			bomb.AddMember("m_is_planting", snap.Bomb.isPlanting, a);
		}
		doc.AddMember("m_bomb", bomb, a);
	}

	// Projectiles (in-flight / active grenades)
	if (!snap.Projectiles.empty()) {
		rapidjson::Value projectiles(rapidjson::kArrayType);
		for (const auto& proj : snap.Projectiles) {
			const char* typeStr = nullptr;
			float maxDuration = 0.f;
			switch (proj.Type) {
				case PROJ_FLASH:   typeStr = "flash";     maxDuration = 2.5f;  break;
				case PROJ_SMOKE:   typeStr = "smoke";     maxDuration = 20.0f; break;
				case PROJ_HE:      typeStr = "explosive"; break;
				case PROJ_MOLOTOV: typeStr = "inferno";   maxDuration = 7.0f;  break;
				case PROJ_DECOY:   typeStr = "decoy";     break;
				default: continue;
			}
			// Only push detonated smoke/inferno/flash as effects.
			// In-flight (not yet detonated) smoke/inferno/flash are pushed as
			// projectiles (icons) so the frontend shows real-time position.
			// HE (explosive) is always pushed as projectile icon regardless of detonation.
			bool pushAsEffect = proj.Exploded &&
				(proj.Type == PROJ_SMOKE || proj.Type == PROJ_MOLOTOV || proj.Type == PROJ_FLASH);
			rapidjson::Value p(rapidjson::kObjectType);
		p.AddMember("m_type", rapidjson::Value(typeStr, a), a);
		rapidjson::Value pos(rapidjson::kObjectType);
		pos.AddMember("x", proj.Position.x, a);
		pos.AddMember("y", proj.Position.y, a);
		pos.AddMember("z", proj.Position.z, a);
		p.AddMember("m_position", pos, a);
		// Task 6: thrower team (2=T, 3=CT, 0=unknown)
		p.AddMember("m_team", proj.Team, a);
		// Stable entity ID for frontend DOM keying (avoids duplicate elements when position changes)
		p.AddMember("m_entity_id", proj.EntityId, a);
		// Detonation flag so frontend can distinguish in-flight vs detonated
		p.AddMember("m_exploded", proj.Exploded, a);
		// Whether this projectile should be rendered as a detonated effect (smoke cloud / fire / flash)
		p.AddMember("m_is_effect", pushAsEffect, a);
			float lifeRemaining = 0.f;
			if (maxDuration > 0.f) {
				lifeRemaining = maxDuration - proj.StationaryTimer - proj.DisappearTimer;
				if (lifeRemaining < 0.f) lifeRemaining = 0.f;
			}
			p.AddMember("m_life_remaining", lifeRemaining, a);
			// Task 7.1: flash maxDuration (CS2 flashbang max lifetime = 2.5s)
			if (proj.Type == PROJ_FLASH) {
				p.AddMember("maxDuration", 2.5f, a);
			}
			// Task 7.2-7.4: inferno multi-flame points (molotov/incendiary).
			// Emit the full firePositions array so the frontend can render the
			// fire spread polygon instead of a single center point.
			if (proj.Type == PROJ_MOLOTOV && proj.FlameCount > 0) {
				rapidjson::Value flames(rapidjson::kArrayType);
				for (int fi = 0; fi < proj.FlameCount; fi++) {
					rapidjson::Value fp(rapidjson::kObjectType);
					fp.AddMember("x", proj.Flames[fi].x, a);
					fp.AddMember("y", proj.Flames[fi].y, a);
					fp.AddMember("z", proj.Flames[fi].z, a);
					flames.PushBack(fp, a);
				}
				p.AddMember("m_flames", flames, a);
			}
			projectiles.PushBack(p, a);
		}
		doc.AddMember("m_projectiles", projectiles, a);
	}

	// Dropped weapons (world items)
	if (!snap.DroppedWeapons.empty()) {
		rapidjson::Value weapons(rapidjson::kArrayType);
		for (const auto& dw : snap.DroppedWeapons) {
			rapidjson::Value w(rapidjson::kObjectType);
			rapidjson::Value pos(rapidjson::kObjectType);
			pos.AddMember("x", dw.Position.x, a);
			pos.AddMember("y", dw.Position.y, a);
			pos.AddMember("z", dw.Position.z, a);
			w.AddMember("m_position", pos, a);
			w.AddMember("m_item_id", dw.ItemId, a);
			w.AddMember("m_name", rapidjson::Value(dw.Name.c_str(), a), a);
			w.AddMember("m_entity_id", static_cast<uint32_t>(dw.EntityAddr & 0xFFFFFFFF), a);
			weapons.PushBack(w, a);
		}
		doc.AddMember("m_dropped_weapons", weapons, a);
	}

	// Task 19.3: Append per-map radar calibration params so the frontend
	// can apply rotation/scale/offset during coordinate conversion.
	{
		radar::RadarCalibrationRecord cal = GetRadarCalibrationSafe();
		rapidjson::Value calibration(rapidjson::kObjectType);
		calibration.AddMember("rotationDeg", cal.rotationDeg, a);
		calibration.AddMember("scale", cal.scale, a);
		calibration.AddMember("offsetX", cal.offsetX, a);
		calibration.AddMember("offsetY", cal.offsetY, a);
		doc.AddMember("m_calibration", calibration, a);
	}

	rapidjson::StringBuffer buf;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
	doc.Accept(writer);
	return buf.GetString();
}

// ============================================================================
//  Worker thread — serializes GameSnapshot → JSON off the broadcast path.
//  Runs at MenuConfig::WebRadarInterval; hands the latest payload to the
//  broadcast loop (WebRadarThread) via m_latestPayload + m_payloadReady.
//  Slow clients can't stall this thread because Broadcast() lives elsewhere.
// ============================================================================

void WebRadarServer::WorkerLoop() {
	LOG_INFO("WebRadar", "Worker thread started");

	while (m_workerRunning.load()) {
		// Sleep for the configured interval, but wake immediately on stop.
		{
			std::unique_lock<std::mutex> lock(m_payloadMutex);
			m_workerCv.wait_for(lock,
				std::chrono::milliseconds(MenuConfig::WebRadarInterval),
				[this] { return !m_workerRunning.load(); });
		}
		if (!m_workerRunning.load()) break;

		// Nothing to serialize while the game isn't running.
		if (globalVars::gameState.load() != AppState::RUNNING) continue;

		// No viewers → skip to save CPU (matches previous single-thread behavior).
		if (GetClientCount() == 0) continue;

		// Serialize off the broadcast path.
		auto t0 = std::chrono::steady_clock::now();
		GameSnapshot snap = Cheats::GetSnapshot();
		std::string json = SerializeSnapshot(snap);
		auto t1 = std::chrono::steady_clock::now();
		uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
		m_serializeUs.store(us);

		// Task 9: record serialize stats
		m_stats.serializeUsTotal += us;
		m_stats.serializeUsCount++;

		// Task 17: update activeMap from the latest snapshot.
		{
			std::string cleanName = CleanMapName(snap.MapName);
			std::lock_guard<std::mutex> lock(m_stats.m_textMutex);
			m_stats.activeMap = cleanName;
		}

		size_t payloadBytes = json.size();
		{
			std::lock_guard<std::mutex> lock(m_payloadMutex);
			// Task 9: if previous payload wasn't consumed yet, it's being coalesced
			if (m_payloadReady.load()) {
				m_stats.coalescedFrames++;
			}
			m_latestPayload = std::move(json);
			m_payloadReady.store(true);
		}

		// Task 9: update payload byte stats
		m_stats.payloadBytesLast.store(payloadBytes);
		size_t currentPeak = m_stats.payloadBytesPeak.load();
		while (payloadBytes > currentPeak &&
		       !m_stats.payloadBytesPeak.compare_exchange_weak(currentPeak, payloadBytes)) {
			// currentPeak was reloaded by compare_exchange_weak; retry
		}

		LOG_TRACE("WebRadar", "Worker: serialized {} bytes in {}us (map='{}', entities={})",
			payloadBytes, us, CleanMapName(snap.MapName), snap.Entities.size());
	}

	LOG_INFO("WebRadar", "Worker thread stopped");
}

void WebRadarServer::StartWorker() {
	if (m_workerRunning.load()) return;
	m_payloadReady.store(false);
	m_workerRunning.store(true);
	m_workerThread = std::thread(&WebRadarServer::WorkerLoop, this);
}

void WebRadarServer::StopWorker() {
	if (!m_workerRunning.load()) return;
	m_workerRunning.store(false);
	m_workerCv.notify_all();   // wake the worker so it exits promptly
	if (m_workerThread.joinable())
		m_workerThread.join();
}

bool WebRadarServer::ConsumePayload(std::string& out) {
	if (!m_payloadReady.load()) return false;
	std::lock_guard<std::mutex> lock(m_payloadMutex);
	if (!m_payloadReady.load()) return false;   // double-check under lock
	out = std::move(m_latestPayload);
	m_payloadReady.store(false);
	return true;
}

RuntimeStatsSnapshot WebRadarServer::GetStats() const {
	RuntimeStatsSnapshot s;
	s.framesSent = m_stats.framesSent.load();
	s.framesDropped = m_stats.framesDropped.load();
	s.coalescedFrames = m_stats.coalescedFrames.load();
	s.droppedFramesSlowClient = m_stats.droppedFramesSlowClient.load();
	s.serializeUsTotal = m_stats.serializeUsTotal.load();
	s.serializeUsCount = m_stats.serializeUsCount.load();
	s.broadcastUsTotal = m_stats.broadcastUsTotal.load();
	s.broadcastUsCount = m_stats.broadcastUsCount.load();
	s.payloadBytesLast = m_stats.payloadBytesLast.load();
	s.payloadBytesPeak = m_stats.payloadBytesPeak.load();
	s.clientCount = GetClientCount();
	// Task 17: derived stats.
	s.sendHz = m_stats.sendHz.load();
	s.bytesOutPerSec = m_stats.bytesOutPerSec.load();
	s.bytesOutTotal = m_stats.bytesOutTotal.load();
	{
		std::lock_guard<std::mutex> lock(m_stats.m_textMutex);
		s.activeMap = m_stats.activeMap;
		s.statusText = m_stats.statusText;
	}
	return s;
}

void WebRadarServer::UpdateRuntimeDerivedStats(double sendHz, uint64_t bytesPerSec, const std::string& statusText) {
	m_stats.sendHz.store(sendHz);
	m_stats.bytesOutPerSec.store(bytesPerSec);
	std::lock_guard<std::mutex> lock(m_stats.m_textMutex);
	m_stats.statusText = statusText;
}

std::string WebRadarServer::GetLatestPayloadForPolling() const {
	std::lock_guard<std::mutex> lock(m_lastPayloadMutex);
	return m_lastBroadcastPayload;
}

// ============================================================================
//  WebRadarThread — lifecycle: start/stop server based on menu toggle,
//  serialize + broadcast at configurable interval.
// ============================================================================

VOID WebRadarThread() {
	LOG_INFO("WebRadar", "Thread started");

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		LOG_ERROR("WebRadar", "WSAStartup failed: {}", WSAGetLastError());
		return;
	}

	WebRadarServer server;
	bool firstBroadcast = true;

	while (true) {
		try {
			// ---- Toggle server on/off based on menu ----
			if (!MenuConfig::ShowWebRadar) {
				if (server.IsRunning()) {
					server.Stop();   // stops worker + accept + closes clients
					LOG_INFO("WebRadar", "Disabled by user");
				}
				g_webRadarRunning.store(false);
				g_webRadarClientCount.store(0);
				firstBroadcast = true;
				Sleep(500);
				continue;
			}

			if (!server.IsRunning()) {
				LOG_INFO("WebRadar", "Starting server on port {}...", MenuConfig::WebRadarPort);
				if (!server.Start((uint16_t)MenuConfig::WebRadarPort)) {
					LOG_ERROR("WebRadar", "Failed to start, retry in 5s");
					Sleep(5000);
					continue;
				}
				// Locate the webapp directory for filesystem fallback (dev mode).
				// Embedded RCDATA resources are always tried first by ServeStaticFile.
				g_webappDir = FindWebappDir();
				if (g_webappDir.empty()) {
					LOG_WARNING("WebRadar", "Webapp directory not found; relying on embedded assets only");
				} else {
					LOG_INFO("WebRadar", "Webapp directory: {}", g_webappDir);
				}

				// Start the serializer worker — it produces payloads off this
				// broadcast path so slow clients can't stall serialization.
				server.StartWorker();
			}
			g_webRadarRunning.store(true);

			// ---- Task 17: compute derived stats (sendHz / bytesOutPerSec / statusText) ----
			// Sample framesSent and bytesOutTotal every ~1s and push the rates
			// back into the server so GetStats() / GUI can read them.
			static auto prevStatsTime = std::chrono::steady_clock::now();
			static uint64_t prevFramesSent = 0;
			static uint64_t prevBytesOut = 0;
			auto statsNow = std::chrono::steady_clock::now();
			double statsElapsed = std::chrono::duration<double>(statsNow - prevStatsTime).count();
			if (statsElapsed >= 1.0) {
				RuntimeStatsSnapshot cur = server.GetStats();
				double hz = (cur.framesSent > prevFramesSent)
					? (double)(cur.framesSent - prevFramesSent) / statsElapsed
					: 0.0;
				uint64_t bytesDelta = (cur.bytesOutTotal > prevBytesOut)
					? (cur.bytesOutTotal - prevBytesOut)
					: 0;
				uint64_t bps = (uint64_t)(bytesDelta / statsElapsed);
				std::string status = "listening";
				if (cur.clientCount == 0) status = "listening (no clients)";
				server.UpdateRuntimeDerivedStats(hz, bps, status);
				prevFramesSent = cur.framesSent;
				prevBytesOut = cur.bytesOutTotal;
				prevStatsTime = statsNow;
			}

			// ---- Update client count + stats for UI ----
			g_webRadarClientCount.store(server.GetClientCount());
			// Task 9: copy stats snapshot for GUI debug panel
			{
				RuntimeStatsSnapshot snap = server.GetStats();
				std::lock_guard<std::mutex> lock(g_webRadarStatsMutex);
				g_webRadarStats = snap;
			}

			// ---- Broadcast the latest payload if the worker produced one ----
		// Game-state / no-client gating now lives in WorkerLoop; this loop
		// only ships whatever payload is ready.
		std::string payload;
		if (server.ConsumePayload(payload)) {
			server.Broadcast(payload);
			if (firstBroadcast) {
				LOG_INFO("WebRadar", "First broadcast sent ({} bytes)", payload.size());
				firstBroadcast = false;
			}
		}

		// ---- Handle page reload request from GUI ----
		if (g_webRadarReloadRequested.exchange(false)) {
			server.BroadcastPageUpdate();
			LOG_INFO("WebRadar", "pageUpdate broadcast (manual reload)");
		}

		// Poll the worker at a short cadence so ready payloads ship promptly.
		Sleep(10);
		}
		catch (...) {
			Sleep(1000);
		}
	}
}

// ============================================================================
//  Cloudflare Tunnel (quick tunnel) — exposes local port to the internet
// ============================================================================
static HANDLE g_cfJob = nullptr;
static HANDLE g_cfProcess = nullptr;
static std::thread g_cfReaderThread;
static std::atomic<bool> g_cfReaderRunning{ false };

bool IsCloudflaredInstalled() {
	char path[MAX_PATH] = {};
	return SearchPathA(nullptr, "cloudflared", ".exe", MAX_PATH, path, nullptr) != 0;
}

// Reader thread: captures the public URL from cloudflared's stderr.
// cloudflared prints: "... |  https://xxx.trycloudflare.com  | ..."
static void CloudflareOutputReader(HANDLE hPipe) {
	std::string accumulated;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (g_cfReaderRunning.load()) {
		if (!ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) || bytesRead == 0)
			break;
		buffer[bytesRead] = '\0';
		accumulated.append(buffer, bytesRead);

		// Search for https://xxx.trycloudflare.com
		size_t pos = 0;
		while ((pos = accumulated.find("https://", pos)) != std::string::npos) {
			size_t end = accumulated.find_first_of(" \t\r\n|", pos);
			if (end == std::string::npos) end = accumulated.size();
			std::string url = accumulated.substr(pos, end - pos);
			if (url.find("trycloudflare.com") != std::string::npos) {
				std::lock_guard<std::mutex> lock(g_cloudflareTunnelMutex);
				g_cloudflareTunnelURL = url;
				g_cloudflareTunnelRunning.store(true);
				LOG_INFO("WebRadar", "Cloudflare tunnel URL: {}", url);
				break;
			}
			pos = end;
		}
		if (g_cloudflareTunnelRunning.load()) break;
	}
	CloseHandle(hPipe);
}

bool StartCloudflareTunnel(int port) {
	if (g_cfProcess != nullptr) return true;  // already running

	if (!IsCloudflaredInstalled()) {
		LOG_ERROR("WebRadar", "cloudflared not found in PATH. Install: winget install Cloudflare.cloudflared");
		return false;
	}

	// Create Job Object so cloudflared dies when our process exits
	g_cfJob = CreateJobObjectA(nullptr, nullptr);
	if (g_cfJob) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
		jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_cfJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
	}

	// Build command: cloudflared.exe tunnel --url http://localhost:PORT
	std::string cmd = "cloudflared.exe tunnel --url http://localhost:" + std::to_string(port);

	// Pipe to capture stderr (cloudflared prints the URL there)
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	HANDLE hRead = nullptr, hWrite = nullptr;
	CreatePipe(&hRead, &hWrite, &sa, 0);
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;
	PROCESS_INFORMATION pi = {};

	if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		LOG_ERROR("WebRadar", "CreateProcess failed for cloudflared: {}", GetLastError());
		CloseHandle(hRead);
		CloseHandle(hWrite);
		if (g_cfJob) { CloseHandle(g_cfJob); g_cfJob = nullptr; }
		return false;
	}

	CloseHandle(hWrite);   // parent doesn't need the write end
	CloseHandle(pi.hThread);
	g_cfProcess = pi.hProcess;
	if (g_cfJob) AssignProcessToJobObject(g_cfJob, g_cfProcess);

	// Start reader thread to capture the public URL
	g_cfReaderRunning.store(true);
	g_cfReaderThread = std::thread(CloudflareOutputReader, hRead);

	LOG_INFO("WebRadar", "Cloudflare tunnel started for port {}", port);
	return true;
}

void StopCloudflareTunnel() {
	g_cfReaderRunning.store(false);

	if (g_cfProcess) {
		TerminateProcess(g_cfProcess, 0);
		CloseHandle(g_cfProcess);
		g_cfProcess = nullptr;
	}
	if (g_cfJob) {
		CloseHandle(g_cfJob);
		g_cfJob = nullptr;
	}
	if (g_cfReaderThread.joinable())
		g_cfReaderThread.join();

	{
		std::lock_guard<std::mutex> lock(g_cloudflareTunnelMutex);
		g_cloudflareTunnelURL.clear();
	}
	g_cloudflareTunnelRunning.store(false);
	LOG_INFO("WebRadar", "Cloudflare tunnel stopped");
}

bool IsCloudflareTunnelRunning() {
	return g_cloudflareTunnelRunning.load();
}

std::string GetCloudflareTunnelURL() {
	std::lock_guard<std::mutex> lock(g_cloudflareTunnelMutex);
	return g_cloudflareTunnelURL;
}

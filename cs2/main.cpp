#include "config/SettingsManager.h"

#include "config/Language.h"
#include "game/AppState.h"

#include "utils/Logger.h"
#include "utils/CrashHandler.h"
#include "utils/Telemetry.h"
#include "utils/SystemInfo.h"

#ifndef NTSTATUS
typedef long NTSTATUS;
#endif

#include "game/Threads.h"
#include "game/CameraWorker.h"
#include "game/MenuConfig.h"
#include "render/GrenadeHelper.h"
#include "config/ConfigSaver.h"

#include <iostream>
#include <filesystem>
#include <windows.h>
#include <timeapi.h>
#include <winhttp.h>
#include <shellapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "winhttp.lib")


namespace fs = std::filesystem;

std::string readFile(const std::string& path) {
	std::ifstream file(path);
	if (!file) return "";
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

// Helper: perform HTTP GET on an already-opened session+connect+request chain
static std::string doWinHttpFetch(HINTERNET hSession, const wchar_t* host, const wchar_t* path) {
	std::string result;
	HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) return result;

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) { WinHttpCloseHandle(hConnect); return result; }

	DWORD connectTimeout = 3000;
	DWORD receiveTimeout = 8000;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
	WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout));

	if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		WinHttpReceiveResponse(hRequest, NULL)) {
		DWORD statusCode = 0, size = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			NULL, &statusCode, &size, NULL);
		if (statusCode == 200) {
			char buffer[4096];
			DWORD bytesRead = 0;
			while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
				result.append(buffer, bytesRead);
				bytesRead = 0;
			}
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	return result;
}

// Read system proxy settings from registry (returns proxy string or PAC URL)
static bool ReadSystemProxyFromRegistry(std::wstring& outProxy, std::wstring& outPacUrl) {
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return false;

	DWORD proxyEnabled = 0;
	DWORD size = sizeof(proxyEnabled);
	if (RegQueryValueExW(hKey, L"ProxyEnable", NULL, NULL, (LPBYTE)&proxyEnabled, &size) == ERROR_SUCCESS && proxyEnabled) {
		wchar_t proxyServer[512] = {};
		DWORD proxySize = sizeof(proxyServer);
		if (RegQueryValueExW(hKey, L"ProxyServer", NULL, NULL, (LPBYTE)proxyServer, &proxySize) == ERROR_SUCCESS && proxyServer[0]) {
			outProxy = proxyServer;
		}
	}

	wchar_t autoConfigUrl[512] = {};
	DWORD acSize = sizeof(autoConfigUrl);
	if (RegQueryValueExW(hKey, L"AutoConfigURL", NULL, NULL, (LPBYTE)autoConfigUrl, &acSize) == ERROR_SUCCESS && autoConfigUrl[0]) {
		outPacUrl = autoConfigUrl;
	}

	RegCloseKey(hKey);
	return !outProxy.empty() || !outPacUrl.empty();
}

// Cached proxy session for reuse across multiple downloadUrl calls
static std::wstring g_cachedProxy;
static bool g_proxyChecked = false;

// Detect system proxy once and cache it
static const std::wstring& GetCachedProxy() {
	if (g_proxyChecked) return g_cachedProxy;
	g_proxyChecked = true;
	std::wstring pacUrl;
	if (ReadSystemProxyFromRegistry(g_cachedProxy, pacUrl)) {
		if (!g_cachedProxy.empty())
			LOG_INFO("Config", "System proxy from registry: {}", std::string(g_cachedProxy.begin(), g_cachedProxy.end()));
	}
	return g_cachedProxy;
}

// Download with auto system-proxy detection.
// If system proxy is configured, use it directly (skip slow direct-attempt fallback).
// Otherwise try default proxy then give up.
static std::string downloadUrl(const wchar_t* host, const wchar_t* path) {
	const std::wstring& proxy = GetCachedProxy();

	// --- If proxy known, use it directly (skip slow direct connection) ---
	if (!proxy.empty()) {
		HINTERNET hSession = WinHttpOpen(L"CS2-DMA/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
			proxy.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
		if (hSession) {
			auto result = doWinHttpFetch(hSession, host, path);
			WinHttpCloseHandle(hSession);
			if (!result.empty()) return result;
		}
	}

	// --- Fallback: default (IE/WinInet proxy settings) ---
	{
		HINTERNET hSession = WinHttpOpen(L"CS2-DMA/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (hSession) {
			auto result = doWinHttpFetch(hSession, host, path);
			WinHttpCloseHandle(hSession);
			if (!result.empty()) return result;
		}
	}

	LOG_WARNING("Config", "All download attempts failed for {}", std::string(host, host + wcslen(host)));
	return {};
}

// Check GitHub Releases for newer version; if found, open releases page and return false (stop launch)
static bool CheckForUpdates() {
	LOG_INFO("Config", "Checking for updates (current: v{})...", PROJECT_VERSION);
	std::string response = downloadUrl(L"api.github.com", L"/repos/chao-shushu/CS2-DMA/releases/latest");
	if (response.empty()) {
		LOG_WARNING("Config", "Could not reach GitHub Releases API, continuing");
		return true; // network issue, don't block launch
	}

	// Simple JSON parse: find "tag_name":"..."
	const std::string tagKey = "\"tag_name\"";
	size_t pos = response.find(tagKey);
	if (pos == std::string::npos) return true;
	pos = response.find('"', pos + tagKey.size());
	if (pos == std::string::npos) return true;
	size_t endPos = response.find('"', pos + 1);
	if (endPos == std::string::npos) return true;
	std::string latestTag = response.substr(pos + 1, endPos - pos - 1);

	// Skip beta/pre-release versions (e.g. "1.3.0-beta", "v2.0.0-rc1")
	if (latestTag.find("-beta") != std::string::npos ||
		latestTag.find("-alpha") != std::string::npos ||
		latestTag.find("-rc") != std::string::npos ||
		latestTag.find("-pre") != std::string::npos) {
		LOG_INFO("Config", "Latest release {} is a pre-release, skipping update check", latestTag);
		return true;
	}

	// Strip optional 'v' prefix for comparison
	std::string latestVer = latestTag;
	if (!latestVer.empty() && latestVer[0] == 'v') latestVer = latestVer.substr(1);
	std::string curVer = PROJECT_VERSION;

	// Semantic version comparison: only prompt if remote is strictly newer
	auto parseVer = [](const std::string& v) -> std::vector<int> {
		std::vector<int> parts;
		size_t start = 0;
		for (size_t i = 0; i <= v.size(); ++i) {
			if (i == v.size() || v[i] == '.') {
				parts.push_back(std::atoi(v.substr(start, i - start).c_str()));
				start = i + 1;
			}
		}
		return parts;
	};
	std::vector<int> remote = parseVer(latestVer);
	std::vector<int> local  = parseVer(curVer);
	bool remoteNewer = false;
	for (size_t i = 0; i < remote.size() || i < local.size(); ++i) {
		int r = i < remote.size() ? remote[i] : 0;
		int l = i < local.size()  ? local[i]  : 0;
		if (r > l) { remoteNewer = true; break; }
		if (r < l) { break; } // local is newer at this component
	}

	if (remoteNewer) {
		LOG_INFO("Config", "New version available: {} (current: v{})", latestTag, PROJECT_VERSION);
		std::cout << "\n========================================" << std::endl;
		std::cout << lang.console_new_version << latestTag << " (v" << PROJECT_VERSION << ")" << std::endl;
		std::cout << lang.console_open_releases;
		char choice = 'y';
		std::cin >> choice;
		std::cout << "========================================\n" << std::endl;

		if (choice == 'y' || choice == 'Y') {
			ShellExecuteA(nullptr, "open", "https://github.com/chao-shushu/CS2-DMA/releases/latest",
				nullptr, nullptr, SW_SHOWNORMAL);
			return false; // stop launch, user chose to update
		}
		LOG_INFO("Config", "User chose to continue with current version");
		return true; // user chose to continue
	} else {
		LOG_INFO("Config", "Already up to date (v{})", PROJECT_VERSION);
		return true;
	}
}

// Get directory containing the running executable
static std::string GetExeDir() {
	char buf[MAX_PATH] = {};
	GetModuleFileNameA(NULL, buf, MAX_PATH);
	return fs::path(buf).parent_path().string();
}

// Run cs2-dumper in DMA mode to update offsets from live game memory
static bool RunDMAOffsetDumper() {
	std::string exeDir = GetExeDir();
	std::string dumperExe = exeDir + "\\dumper\\cs2-dumper.exe";
	std::string outputDir = exeDir + "\\dumper\\output";
	std::string dumperDir = exeDir + "\\dumper";

	if (!fs::exists(dumperExe)) {
		LOG_ERROR("Config", "cs2-dumper.exe not found at {}", dumperExe);
		std::cout << lang.console_dma_dumper_missing << std::endl;
		return false;
	}

	// Ensure output directory exists
	fs::create_directories(outputDir);

	// Build command line
	std::string cmdLine = "\"" + dumperExe + "\" -c pcileech -a \":device=FPGA\" -p cs2.exe -f json -o \"" + outputDir + "\" -vv";
	LOG_INFO("Config", "Running: {}", cmdLine);

	// Build environment block with MEMFLOW_PLUGIN_PATH pointing to dumper\plugins
	std::string pluginsDir = dumperDir + "\\plugins";
	std::string envBlock;
	// Copy current environment and append MEMFLOW_PLUGIN_PATH
	LPTCH rawEnv = GetEnvironmentStringsA();
	if (rawEnv) {
		char* p = rawEnv;
		while (*p) {
			envBlock.append(p);
			envBlock.push_back('\0');
			p += strlen(p) + 1;
		}
		FreeEnvironmentStringsA(rawEnv);
	}
	envBlock.append("MEMFLOW_PLUGIN_PATH=");
	envBlock.append(pluginsDir);
	envBlock.push_back('\0');
	envBlock.push_back('\0');

	// Use CreateProcessA to avoid cmd.exe path interpretation issues
	// and set working directory to dumper's directory (for log file + DLL search)
	STARTUPINFOA si = { sizeof(STARTUPINFOA) };
	PROCESS_INFORMATION pi = {};
	std::string mutableCmd = cmdLine; // CreateProcessA requires mutable buffer

	BOOL ok = CreateProcessA(
		NULL,                // application name (NULL = use command line)
		mutableCmd.data(),   // command line (must be mutable)
		NULL, NULL,          // process/thread security
		FALSE,               // inherit handles
		0,                   // creation flags
		envBlock.data(),     // environment block with MEMFLOW_PLUGIN_PATH
		dumperDir.c_str(),   // working directory = dumper folder
		&si, &pi
	);

	if (!ok) {
		LOG_ERROR("Config", "CreateProcessA failed: {}", GetLastError());
		return false;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (exitCode != 0) {
		LOG_ERROR("Config", "cs2-dumper exited with code {}", exitCode);
		return false;
	}

	// Copy offsets.json from dumper output to data/
	std::string srcOffsets = outputDir + "\\offsets.json";
	std::string dstOffsets = exeDir + "\\data\\offsets.json";
	if (fs::exists(srcOffsets)) {
		fs::copy_file(srcOffsets, dstOffsets, fs::copy_options::overwrite_existing);
		LOG_INFO("Config", "Copied offsets.json");
	} else {
		LOG_WARNING("Config", "offsets.json not found in dumper output");
		return false;
	}

	// Copy client_dll.json from dumper output to data/
	std::string srcClient = outputDir + "\\client_dll.json";
	std::string dstClient = exeDir + "\\data\\client_dll.json";
	if (fs::exists(srcClient)) {
		fs::copy_file(srcClient, dstClient, fs::copy_options::overwrite_existing);
		LOG_INFO("Config", "Copied client_dll.json");
	} else {
		LOG_WARNING("Config", "client_dll.json not found in dumper output");
		return false;
	}

	// Generate version.json from dumper info.json
	std::string srcInfo = outputDir + "\\info.json";
	std::string dstVersion = exeDir + "\\data\\version.json";
	if (fs::exists(srcInfo)) {
		Offset::GenerateVersionFromInfo(srcInfo, dstVersion);
	} else {
		LOG_WARNING("Config", "info.json not found in dumper output, skipping version.json generation");
	}

	return true;
}

void main(HMODULE module) {
	SetConsoleOutputCP(65001);

	// Set console font to TrueType so CJK characters render correctly on non-CJK systems
	{
		CONSOLE_FONT_INFOEX cfi = { sizeof(cfi) };
		cfi.dwFontSize.Y = 16;
		cfi.FontWeight = 400;
		cfi.FontFamily = 0x36; // TrueType + Modern
		wcscpy_s(cfi.FaceName, L"NSimSun");
		SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi);
	}

	// Enable DPI awareness for crisp rendering at native resolution
	{
		HMODULE shcore = LoadLibraryA("shcore.dll");
		if (shcore) {
			typedef HRESULT(WINAPI* SetProcessDpiAwareness_t)(int);
			auto fn = (SetProcessDpiAwareness_t)GetProcAddress(shcore, "SetProcessDpiAwareness");
			if (fn) fn(2); // PROCESS_PER_MONITOR_DPI_AWARE
			FreeLibrary(shcore);
		}
		else {
			HMODULE user32 = GetModuleHandleA("user32.dll");
			if (user32) {
				typedef BOOL(WINAPI* SetProcessDPIAware_t)();
				auto fn = (SetProcessDPIAware_t)GetProcAddress(user32, "SetProcessDPIAware");
				if (fn) fn();
			}
		}
	}

	timeBeginPeriod(1);

	std::string machineCode = SystemInfo::GetMachineCode();

	Logger::Get().Init("logs", machineCode);
	CrashHandler::Install("logs");
	Telemetry::SetMachineCode(machineCode);
	Telemetry::Init();

	LOG_INFO("DMA", "CS2-DMA starting...");
	LOG_INFO("DMA", "Software coded by kuchao-chaoshushu");

	settingsJson.LoadSettings();
	LOG_INFO("Config", "Settings parsed (language: {})", settingsJson.language);

	if (settingsJson.language == "en") lang.english();
	else if (settingsJson.language == "ch") lang.chineese();
	else {
		// Auto-detect from system UI language
		LANGID uiLang = GetUserDefaultUILanguage();
		// Primary language sub-id: 0x04 = Chinese (zh)
		if ((uiLang & 0xFF) == 0x04) lang.chineese();
		else lang.english();
	}

	std::string offsets = readFile("data/offsets.json");
	std::string client = readFile("data/client_dll.json");

	// --- Version validation (DMA-based offset update) ---
	bool versionMismatch = false;

	// Check game version via Steam API against local version.json
	std::string versionData = readFile("data/version.json");
	if (!versionData.empty()) {
		if (!Offset::ParseVersion(versionData)) {
			LOG_WARNING("Config", "version.json parse failed, skipping game version check");
		} else {
			LOG_INFO("Config", "Checking CS2 game version via Steam API...");
			std::string steamNews = downloadUrl(L"api.steampowered.com", L"/ISteamNews/GetNewsForApp/v2/?appid=730&count=3&maxlength=0");
			if (!steamNews.empty()) {
				if (!Offset::CheckGameVersion(steamNews)) {
					versionMismatch = true;
				}
			} else {
				LOG_WARNING("Config", "Could not fetch Steam API, skipping game version check");
			}
		}
	} else {
		LOG_WARNING("Config", "version.json not found, skipping game version check");
	}

	// Prompt for DMA offset update if game version is newer
	if (versionMismatch) {
		std::cout << "\n========================================" << std::endl;
		std::cout << lang.console_version_mismatch_prefix << Offset::GameUpdateDate << lang.console_version_mismatch_suffix << std::endl;
		std::cout << "========================================\n" << std::endl;
		std::cout << lang.console_fetch_offsets;
		char choice = 'n';
		std::cin >> choice;
		if (choice == 'y' || choice == 'Y') {
			LOG_INFO("Config", "User chose to update offsets via DMA");
			std::cout << lang.console_dma_updating << std::endl;
			if (RunDMAOffsetDumper()) {
				std::cout << lang.console_dma_update_ok << std::endl;
				std::cout << lang.console_dma_restart << std::endl;
				LOG_INFO("Config", "Offsets updated successfully via DMA, exiting for restart");
				return;
			} else {
				std::cout << lang.console_dma_update_fail << std::endl;
				LOG_WARNING("Config", "DMA offset update failed, continuing with local offsets");
			}
		} else {
			LOG_INFO("Config", "User chose to continue with local offsets");
		}
	}
	// --- End version validation ---

	// --- Auto-update check (stops launch if new version available) ---
	if (!CheckForUpdates()) {
		LOG_INFO("Config", "User chose to update, stopping launch");
		return;
	}

	Offset::UpdateOffsets(offsets, client);
	LOG_INFO("Config", "Offsets updated");

	if (!fs::directory_entry(MenuConfig::path).exists()) {
		fs::create_directory(MenuConfig::path);
		LOG_INFO("Config", "Created config folder: {}", MenuConfig::path);
	}

	MyConfigSaver::LoadConfig("_autosave.config");
	if (MenuConfig::SelectedLanguage == 0) lang.english();
	else lang.chineese();
	Logger::SetDebugMode(MenuConfig::DebugLog);
	LOG_INFO("Config", "Auto-loaded settings from _autosave.config (debug_log: {})", MenuConfig::DebugLog);

	GrenadeHelper::LoadMapData("data/grenade-helper");
	LOG_INFO("DMA", "Grenade helper loaded");

	globalVars::gameState.store(AppState::DMA_INITIALIZING);
	LOG_INFO("DMA", "Initializing DMA device...");

	if (!ProcessMgr.InitDMA()) {
		globalVars::gameState.store(AppState::DMA_FAILED);
		LOG_ERROR("DMA", "DMA connection failed!");
	} else {
		LOG_INFO("DMA", "DMA connected successfully");
		ProcessMgr.init_keystates();
		globalVars::gameState.store(AppState::SEARCHING_GAME);
	}

	auto safeCreateThread = [](LPTHREAD_START_ROUTINE threadFunc, const char* name) -> bool {
		HANDLE hThread = CreateThread(nullptr, 0, threadFunc, NULL, 0, 0);
		if (hThread == NULL) {
			LOG_ERROR("DMA", "Failed to create {} thread (error: {})", name, (unsigned long)GetLastError());
			return false;
		}
		CloseHandle(hThread);
		return true;
	};

	if (globalVars::gameState.load() != AppState::DMA_FAILED) {
		safeCreateThread((LPTHREAD_START_ROUTINE)(ConnectionThread), "ConnectionThread");
		safeCreateThread((LPTHREAD_START_ROUTINE)(DataThread), "DataThread");
		safeCreateThread((LPTHREAD_START_ROUTINE)(SlowUpdateThread), "SlowUpdateThread");
		safeCreateThread((LPTHREAD_START_ROUTINE)(KeysCheckThread), "KeysCheckThread");
		safeCreateThread((LPTHREAD_START_ROUTINE)(DmaAdminThread), "DmaAdminThread");
		LOG_INFO("DMA", "All threads started, searching for cs2.exe...");
	}

	// CameraWorker runs its own 500Hz thread; only needs DMA + matrix address,
	// which become valid once the game is RUNNING (it idles until then).
	CameraWorker::Start();

	// WebRadar server starts independently — it only needs the HTTP/WS server,
	// not DMA. Game data broadcast is skipped when DMA is not connected.
	safeCreateThread((LPTHREAD_START_ROUTINE)(WebRadarThread), "WebRadarThread");

	SetThreadPriority(GetCurrentThread(), HIGH_PRIORITY_CLASS);

	{
		Gui.NewWindow("CS2DMA", Vec2((float)MenuConfig::RenderWidth, (float)MenuConfig::RenderHeight), Cheats::Run);
	}

	// Signal background threads to exit before tearing down subsystems
	globalVars::gameState.store(AppState::EXITING);

	// CameraWorker checks EXITING and exits its loop; join before teardown.
	CameraWorker::Stop();

	// Session ended — upload log before exit
	Telemetry::UploadSessionLog();
	Logger::Get().Shutdown();
}
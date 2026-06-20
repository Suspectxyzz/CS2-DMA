#include "OffsetUpdater.h"
#include "Offsets.h"
#include "../config/Language.h"
#include "../utils/Logger.h"
#include "../utils/ProcessManager.h"
#include "../game/AppState.h"
#include <filesystem>
#include <windows.h>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Get directory containing the running executable
std::string GetExeDir() {
	char buf[MAX_PATH] = {};
	GetModuleFileNameA(NULL, buf, MAX_PATH);
	return fs::path(buf).parent_path().string();
}

} // namespace

// Run cs2-dumper in DMA mode to update offsets from live game memory
bool RunDMAOffsetDumper() {
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

bool RunOffsetUpdateWithDma() {
	LOG_INFO("OffsetUpdater", "Closing DMA for offset update...");
	ProcessMgr.Detach();
	// Close DMA handle so dumper can exclusively access FPGA
	// Need to add CloseDMA() to ProcessManager.h - see Task below
	ProcessMgr.CloseDMA();

	bool ok = RunDMAOffsetDumper();

	if (ok) {
		LOG_INFO("OffsetUpdater", "Offset update succeeded, restart required");
	} else {
		LOG_ERROR("OffsetUpdater", "Offset update failed");
	}
	return ok;
}

void ReattachDma() {
	LOG_INFO("OffsetUpdater", "Re-initializing DMA after failed offset update...");
	if (ProcessMgr.InitDMA()) {
		LOG_INFO("OffsetUpdater", "DMA re-initialized successfully");
		ProcessMgr.init_keystates();
		globalVars::gameState.store(AppState::SEARCHING_GAME);
	} else {
		LOG_ERROR("OffsetUpdater", "DMA re-initialization failed!");
		globalVars::gameState.store(AppState::DMA_FAILED);
	}
}

void RestartSelf() {
	char exePath[MAX_PATH] = {};
	GetModuleFileNameA(NULL, exePath, MAX_PATH);

	// Create a temporary batch script that waits for this process to exit, then launches new instance
	char tempPath[MAX_PATH] = {};
	GetTempPathA(MAX_PATH, tempPath);

	std::string batchPath = std::string(tempPath) + "cs2dma_restart.bat";
	std::ofstream batchFile(batchPath);
	batchFile << "@echo off\r\n";
	batchFile << "timeout /t 3 /nobreak >nul\r\n";
	batchFile << "start \"\" \"" << exePath << "\"\r\n";
	batchFile << "del \"%~f0\"\r\n";
	batchFile.close();

	// Launch the batch script as an independent process
	STARTUPINFOA si = { sizeof(STARTUPINFOA) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	std::string cmd = "cmd.exe /c \"" + batchPath + "\"";
	CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	LOG_INFO("OffsetUpdater", "Restart scheduled, exiting current process...");

	// Signal threads to exit and terminate
	globalVars::gameState.store(AppState::EXITING);
	ExitProcess(0);
}

#pragma once

// ============================================================================
// DMA 只读性约束声明
// ----------------------------------------------------------------------------
// 本项目硬性约束：保持 DMA 只读性，禁止对宿主机进行任何内存写入操作。
// 本文件中所有 ProcessManager API 仅执行只读 DMA 操作，包括：
//   - VMMDLL_MemReadEx（内存读取）
//   - VMMDLL_Scatter_Initialize / PrepareEx / ExecuteRead / Clear（scatter 读取）
//   - VMMDLL_VfsReadW / VfsListU（VFS 读取）
//   - VMMDLL_PidList / ProcessGetInformation / Map_* / Pdb*（元数据查询）
//   - VMMDLL_ConfigSet（VMM 缓存/DTB 配置，非内存写入）
//
// 禁止引入以下写入 API（静态审计守护，grep 检查）：
//   - VMMDLL_MemWrite / VMMDLL_MemWriteScatter（宿主机内存写入）
//   - VMMDLL_VmMemWrite / VMMDLL_VmMemWriteScatter（虚拟机内存写入）
//   - VMMDLL_Scatter_PrepareWrite / PrepareWriteEx（scatter 写入准备）
//   - LcWrite / LcWriteScatter（leechcore 写入）
//
// 例外：LcCommand(LC_CMD_FPGA_CFGREGCFG_MARKWR) 用于 FPGA 板卡自身配置
// 寄存器清理，属于 DMA 板卡硬件初始化范畴，不触及宿主机内存，见下方注释。
// ============================================================================

#include "../game/AppState.h"

#include "Logger.h"

#include "DmaHealth.h"

#include <iostream>

#include <Windows.h>

#include <vector>

#include <Tlhelp32.h>

#include <leechcore.h>

#include <vmmdll.h>

#include <chrono>

#include <string>

#include <mutex>

#include <atomic>

#include <algorithm>

#include <sstream>



#define _is_invalid_ret(v) if(v==NULL) return false

#define _is_invalid(v,n) if(v==NULL) return n



inline thread_local int SCATTER_PENDING_COUNT = 0;

inline thread_local int SCATTER_PREPARE_FAIL_COUNT = 0;



/**

 * @file ProcessManager.hpp

 * @brief Lightweight wrapper around VMMDLL to manage target process access and memory operations.

 *

 * This header defines the `ProcessManager` class which provides process discovery,

 * read memory helpers (including scatter reads), registry queries and a small

 * keyboard state helper using kernel exports. Designed to encapsulate VMMDLL usage

 * for the project.

 */



/**

 * @enum StatusCode

 * @brief Return codes for process attach operations.

 */

enum StatusCode

{

	/// Operation succeeded.

	SUCCEED,

	/// Failed to obtain the target process ID.

	FAILE_PROCESSID,

	/// Failed to obtain a process handle (unused with VMM).

	FAILE_HPROCESS,

	/// Failed to find a module.

	FAILE_MODULE,

};



/**

 * @struct Info

 * @brief Small information record used for enumerating processes/modules.

 */

struct Info

{

	/// Index in an enumeration.

	uint32_t index;

	/// Process identifier (PID).

	uint32_t process_id;

	/// Directory table base (DTB) value if available.

	uint64_t dtb;

	/// Kernel address associated with the entry.

	uint64_t kernelAddr;

	/// Friendly name for the process/module.

	std::string name;

};



/**

 * @class ProcessManager

 * @brief Encapsulates VMMDLL-based process management and memory helpers.

 *

 * Responsibilities:

 * - Initialize/attach to VMMDLL.

 * - Find and refresh the target process PID.

 * - Provide typed read memory helpers, including scatter reads.

 * - Provide registry query helper and keyboard state helpers backed by kernel exports.

 *

 * Note: This class uses VMMDLL APIs and therefore uses VMMDLL-specific flags and

 * conventions (e.g. process IDs passed to VMMDLL functions).

 */

class ProcessManager

{

private:



	bool   Attached = false;



	uint64_t gafAsyncKeyStateExport = 0;



	uint8_t state_bitmap[64]{ };



	uint8_t previous_state_bitmap[256 / 8]{ };

	uint64_t win32kbase = 0;



	int win_logon_pid = 0;

	std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();



public:
	std::string AttachProcessName;
	HANDLE hProcess = 0;
	DWORD  ProcessID = 0;
	VMM_HANDLE HANDLE;
	// DMA 初始化失败时的错误描述（来自 PLC_CONFIG_ERRORINFO->wszUserText），
	// 供上层（main.cpp / Cheats UI）展示给用户。
	std::string lastDmaError;

	// PCILeech 内存解密进度（暴露给 GUI 线程轮询）
	// -1 = 未触发（正常连接，无需解密），0-100 = 解密进度百分比
	std::atomic<int> MemoryDecryptProgress{ -1 };
	std::atomic<bool> MemoryDecryptTimedOut{ false };

public:

	~ProcessManager()

	{

		// Only close VMM handle if no longer attached (threads stopped).

		// If still attached, threads may be using the handle — closing it

		// would cause use-after-free. The OS will clean up on process exit.

		if (this->HANDLE && !Attached) {

			VMMDLL_Close(this->HANDLE);

			this->HANDLE = nullptr;

		}

	}



	bool InitDMA()
	{
		if (this->HANDLE) return true;
		lastDmaError.clear();

		// Build args dynamically based on mmap file presence
		std::string device = "fpga://algo=0";
		char modulePath[MAX_PATH];
		GetModuleFileNameA(NULL, modulePath, MAX_PATH);
		std::string mmapPath = modulePath;
		auto lastSlash = mmapPath.rfind('\\');
		if (lastSlash != std::string::npos)
			mmapPath = mmapPath.substr(0, lastSlash + 1);
		mmapPath += "mmap.txt";

		bool hasMmap = (GetFileAttributesA(mmapPath.c_str()) != INVALID_FILE_ATTRIBUTES);
		// 使用 VMMDLL_InitializeEx 获取失败时的详细错误信息（PLC_CONFIG_ERRORINFO），
		// 用于区分 FPGA 未找到、VT/IOMMU 拒绝等不同失败场景。
		PLC_CONFIG_ERRORINFO pErrInfo = nullptr;
		if (hasMmap) {
			LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)device.c_str(), (LPSTR)"-memmap", (LPSTR)"-norefresh" };
			this->HANDLE = VMMDLL_InitializeEx(5, args, &pErrInfo);
		} else {
			LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)device.c_str(), (LPSTR)"-norefresh" };
			this->HANDLE = VMMDLL_InitializeEx(4, args, &pErrInfo);
		}

		// 初始化失败时提取错误信息（可能包含 VT/IOMMU 拒绝、FPGA 未找到等提示）
		if (!this->HANDLE && pErrInfo) {
			if (pErrInfo->cwszUserText > 0 && pErrInfo->wszUserText) {
				int len = WideCharToMultiByte(CP_UTF8, 0, pErrInfo->wszUserText,
					(int)pErrInfo->cwszUserText, nullptr, 0, nullptr, nullptr);
				if (len > 0) {
					lastDmaError.resize(len);
					WideCharToMultiByte(CP_UTF8, 0, pErrInfo->wszUserText,
						(int)pErrInfo->cwszUserText, &lastDmaError[0], len, nullptr, nullptr);
				}
			}
			LOG_ERROR("ProcessMgr", "VMMDLL_InitializeEx failed: fUserInputRequest={}, errorText='{}'",
				pErrInfo->fUserInputRequest ? 1 : 0, lastDmaError);
			LcMemFree(pErrInfo);
		} else if (!this->HANDLE) {
			lastDmaError = "VMMDLL_InitializeEx returned NULL with no error info";
			LOG_ERROR("ProcessMgr", "{}", lastDmaError);
		}

		if (this->HANDLE) {
			// Check FPGA firmware version and clean registers if >= 4.7
			ULONG64 verMajor = 0, verMinor = 0;
			VMMDLL_ConfigGet(this->HANDLE, LC_OPT_FPGA_VERSION_MAJOR, &verMajor);
			VMMDLL_ConfigGet(this->HANDLE, LC_OPT_FPGA_VERSION_MINOR, &verMinor);
			LOG_INFO("ProcessMgr", "FPGA firmware version: {}.{}", (DWORD)verMajor, (DWORD)verMinor);
			if (verMajor > 4 || (verMajor == 4 && verMinor >= 7)) {
				ULONG64 lcHandleVal = 0;
				VMMDLL_ConfigGet(this->HANDLE, VMMDLL_OPT_CORE_LEECHCORE_HANDLE, &lcHandleVal);
				if (lcHandleVal) {
					::HANDLE hLC = (::HANDLE)lcHandleVal;
					// ----------------------------------------------------------------------
					// FPGA 板卡配置寄存器清理（非宿主机内存写入）
					// ----------------------------------------------------------------------
					// 以下 LcCommand 调用使用 LC_CMD_FPGA_CFGREGCFG_MARKWR 命令，对 FPGA
					// 板卡自身的配置寄存器（CFG register 0x60，CMD register）执行带掩码
					// 的清理操作（data=0x0000, mask=0xFFFF）。
					//
					// 定性：这是 DMA 板卡硬件初始化范畴的操作，仅作用于 FPGA 板卡内部的
					// 配置寄存器，不触及宿主机内存，与项目"禁止宿主机内存写入"约束不冲突。
					// 仅在固件版本 >= 4.7 时执行一次，用于清理可能残留的 CMD 寄存器状态。
					// ----------------------------------------------------------------------
					BYTE data[4] = { 0x00, 0x00, 0xFF, 0xFF }; // data=0x0000, mask=0xFFFF
					LcCommand(hLC, LC_CMD_FPGA_CFGREGCFG_MARKWR | 0x60, 4, data, NULL, NULL);
					LOG_INFO("ProcessMgr", "FPGA register cleanup performed (fw >= 4.7)");
				}
			}
		}
		return this->HANDLE != 0;

	}



	StatusCode Attach(std::string ProcessName)

	{

		this->AttachProcessName = ProcessName;



		if (!this->HANDLE) {

			LOG_ERROR("ProcessMgr", "Attach: HANDLE is null");

			return FAILE_PROCESSID;

		}



		ProcessID = 0;
		Attached = false;

		BOOL refreshOk = VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
		LOG_DEBUG("ProcessMgr", "Attach: ConfigSet REFRESH_ALL returned {}", refreshOk ? "true" : "false");

		// Try direct PID lookup first (simpler, faster)
		DWORD pid = 0;
		if (VMMDLL_PidGetFromName(this->HANDLE, (LPSTR)ProcessName.c_str(), &pid) && pid != 0) {
			ProcessID = pid;
			LOG_DEBUG("ProcessMgr", "Attach: PidGetFromName found PID={}", ProcessID);
		} else {
			// Fallback: enumerate all processes
			SIZE_T pcPIDs = 0;
			BOOL pidListOk = VMMDLL_PidList(this->HANDLE, nullptr, &pcPIDs);
			LOG_DEBUG("ProcessMgr", "Attach: PidList count={}, ok={}", (int)pcPIDs, pidListOk ? "true" : "false");

			if (!pidListOk || pcPIDs == 0) {
				LOG_WARNING("ProcessMgr", "Attach: PidList failed or empty");
				return FAILE_PROCESSID;
			}

			DWORD* pPIDs = (DWORD*)new char[pcPIDs * 4];
			VMMDLL_PidList(this->HANDLE, pPIDs, &pcPIDs);

			for (int i = 0; i < pcPIDs; i++)
			{
				VMMDLL_PROCESS_INFORMATION ProcessInformation = { 0 };
				ProcessInformation.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
				ProcessInformation.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
				SIZE_T pcbProcessInformation = sizeof(VMMDLL_PROCESS_INFORMATION);
				VMMDLL_ProcessGetInformation(this->HANDLE, pPIDs[i], &ProcessInformation, &pcbProcessInformation);

				if (strcmp(ProcessInformation.szName, ProcessName.c_str()) == 0) {
					ProcessID = pPIDs[i];
					break;
				}
			}

			delete[] pPIDs;
		}

		if (ProcessID == 0) {
			LOG_DEBUG("ProcessMgr", "Attach: '{}' not found", ProcessName);
			return FAILE_PROCESSID;
		}

		// CR3/DTB remediation is deferred to InitAddress / GetProcessModuleHandle.
		// Attach only resolves the PID; module acquisition + FixCr3 happens in
		// INITIALIZING_GAME state. Calling FixCr3 here blocks Attach for 30s+
		// (DTB remediation), keeping the UI stuck in SEARCHING_GAME.
		LOG_INFO("ProcessMgr", "Attach: '{}' found, PID={}", ProcessName, ProcessID);

		Attached = true;
		return SUCCEED;

	}



	// ============================================================
	// CR3/DTB remediation — verifies module accessibility and
	// tries candidate DTBs from dtb.txt when the current DTB
	// is stale (e.g. after anti-cheat DTB hiding).
	// ============================================================

public:
	// Verify a module is truly readable by checking the MZ magic.
	// Refreshes VMM cache before probing to avoid stale module lists.
	bool HasReadableModule(DWORD pid, const std::string& moduleName)
	{
		if (moduleName.empty()) return false;
		if (this->HANDLE)
			VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
		std::string nameCopy = moduleName;
		DWORD64 base = VMMDLL_ProcessGetModuleBaseU(this->HANDLE, pid, const_cast<LPSTR>(nameCopy.data()));
		if (base == 0) return false;

		IMAGE_DOS_HEADER dos{};
		if (!VMMDLL_MemReadEx(this->HANDLE, pid, base, (PBYTE)&dos, sizeof(IMAGE_DOS_HEADER), nullptr, VMMDLL_FLAG_NOCACHE))
			return false;
		return dos.e_magic == 0x5A4D; // "MZ"
	}

private:
	static inline std::mutex g_fixCr3Mutex;
	static inline bool s_fixCr3PluginsInitialized = false;
	static inline std::atomic<uint64_t> s_dtbFileSize{ 0x80000 };

	static VOID cbAddFileDtb(_Inout_ ::HANDLE, _In_ LPSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO)
	{
		if (strcmp(uszName, "dtb.txt") == 0)
			s_dtbFileSize.store(cb, std::memory_order_relaxed);
	}

	// CS2-aware probe: accept client.dll OR engine2.dll as proof of access.
	bool CanAccessProbeModules(DWORD pid, const std::string& processName)
	{
		std::string lower = processName;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		bool isCs2 = (lower == "cs2.exe" || lower == "cs2");
		if (isCs2)
			return HasReadableModule(pid, "client.dll") || HasReadableModule(pid, "engine2.dll");

		return HasReadableModule(pid, processName);
	}

public:
	// Wait for PCILeech memory decryption to complete (mirrors ProCS2 sub_140008AE0).
	// Returns true if modules are accessible (or become accessible within 50s),
	// false on timeout. Progress is exposed via MemoryDecryptProgress atomic
	// for GUI polling. On normal connections this returns immediately.
	bool WaitForMemoryDecryption()
	{
		if (!this->HANDLE || ProcessID == 0) return true;

		// Fast path: if VMMDLL_Map_GetModuleFromNameU succeeds, memory is not
		// encrypted and no waiting is needed (MemoryDecryptProgress stays -1).
		PVMMDLL_MAP_MODULEENTRY pModuleEntry = nullptr;
		if (VMMDLL_Map_GetModuleFromNameU(this->HANDLE, ProcessID,
				const_cast<LPSTR>(AttachProcessName.c_str()), &pModuleEntry, 0)) {
			if (pModuleEntry) VMMDLL_MemFree(pModuleEntry);
			return true;
		}

		LOG_INFO("ProcessMgr", "[!] Memory encryption detected, decrypting...");
		MemoryDecryptProgress.store(0, std::memory_order_relaxed);
		MemoryDecryptTimedOut.store(false, std::memory_order_relaxed);

		// Initialize VFS plugins (required for VfsReadW to access \misc\procinfo\)
		if (!s_fixCr3PluginsInitialized) {
			if (!VMMDLL_InitializePlugins(this->HANDLE)) {
				LOG_ERROR("ProcessMgr", "WaitForMemoryDecryption: VMMDLL_InitializePlugins failed");
				return false;
			}
			s_fixCr3PluginsInitialized = true;
		}

		// Poll progress_percent.txt (max 50s, 200ms interval)
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(50);
		while (std::chrono::steady_clock::now() < deadline) {
			BYTE buf[4] = { 0 };
			DWORD bytesRead = 0;
			if (VMMDLL_VfsReadW(this->HANDLE,
					(LPWSTR)L"\\misc\\procinfo\\progress_percent.txt",
					buf, 3, &bytesRead, 0) == VMMDLL_STATUS_SUCCESS) {
				int progress = atoi(reinterpret_cast<LPSTR>(buf));
				MemoryDecryptProgress.store(progress, std::memory_order_relaxed);
				if (progress == 100) {
					LOG_INFO("ProcessMgr", "Memory decryption complete (progress=100)");
					return true;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}

		LOG_WARNING("ProcessMgr", "[!] Memory decryption timed out (50s)");
		MemoryDecryptTimedOut.store(true, std::memory_order_relaxed);
		return false;
	}

public:
	// Attempt CR3/DTB remediation. Returns true if probe modules are accessible.
	bool FixCr3()
	{
		std::lock_guard<std::mutex> lock(g_fixCr3Mutex);

		if (ProcessID == 0) {
			LOG_WARNING("ProcessMgr", "FixCr3: ProcessID is 0, nothing to fix");
			return false;
		}

		// Wait for PCILeech memory decryption before attempting DTB fix.
		// On normal connections this returns immediately (no extra delay).
		if (!WaitForMemoryDecryption()) {
			LOG_WARNING("ProcessMgr", "[!] Memory decryption timed out, attempting DTB fix anyway");
		}

		if (this->HANDLE)
			VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);

		// Fast path: modules already accessible
		if (CanAccessProbeModules(ProcessID, AttachProcessName)) {
			LOG_DEBUG("ProcessMgr", "FixCr3: modules already accessible, no fix needed");
			return true;
		}

		LOG_INFO("ProcessMgr", "FixCr3: modules not accessible, starting DTB remediation...");

		// Initialize VFS plugins once per VMM handle
		if (!s_fixCr3PluginsInitialized) {
			if (!VMMDLL_InitializePlugins(this->HANDLE)) {
				LOG_ERROR("ProcessMgr", "FixCr3: VMMDLL_InitializePlugins failed");
				return false;
			}
			s_fixCr3PluginsInitialized = true;
			LOG_INFO("ProcessMgr", "FixCr3: VFS plugins initialized");
		}

		// Wait for plugin initialization to complete (poll progress_percent.txt)
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		bool progressReady = false;
		while (std::chrono::steady_clock::now() < deadline) {
			BYTE bytes[4] = { 0 };
			DWORD i = 0;
			auto nt = VMMDLL_VfsReadW(this->HANDLE, (LPWSTR)L"\\misc\\procinfo\\progress_percent.txt", bytes, 3, &i, 0);
			if (nt == VMMDLL_STATUS_SUCCESS && atoi(reinterpret_cast<LPSTR>(bytes)) == 100) {
				progressReady = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (!progressReady) {
			LOG_WARNING("ProcessMgr", "FixCr3: plugin progress timed out (30s)");
			return false;
		}
		LOG_INFO("ProcessMgr", "FixCr3: plugin progress ready");

		// Query dtb.txt file size via VFS list callback
		VMMDLL_VFS_FILELIST2 VfsFileList;
		VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
		VfsFileList.h = 0;
		VfsFileList.pfnAddDirectory = 0;
		VfsFileList.pfnAddFile = cbAddFileDtb;
		s_dtbFileSize.store(0x80000, std::memory_order_relaxed);

		if (!VMMDLL_VfsListU(this->HANDLE, (LPSTR)"\\misc\\procinfo\\", &VfsFileList)) {
			LOG_WARNING("ProcessMgr", "FixCr3: VfsListU failed");
			return false;
		}

		const uint64_t fileSize = s_dtbFileSize.load(std::memory_order_relaxed);
		if (fileSize == 0 || fileSize > 16ULL * 1024ULL * 1024ULL) {
			LOG_WARNING("ProcessMgr", "FixCr3: invalid dtb.txt size {}", fileSize);
			return false;
		}

		// Read dtb.txt content
		const size_t bufferSize = static_cast<size_t>(fileSize) + 1;
		std::vector<BYTE> bytes(bufferSize, 0);
		DWORD bytesRead = 0;
		if (VMMDLL_VfsReadW(this->HANDLE, (LPWSTR)L"\\misc\\procinfo\\dtb.txt",
			bytes.data(), static_cast<DWORD>(bufferSize - 1), &bytesRead, 0) != VMMDLL_STATUS_SUCCESS) {
			LOG_WARNING("ProcessMgr", "FixCr3: VfsReadW dtb.txt failed");
			return false;
		}

		// Parse candidate DTBs
		std::vector<uint64_t> possibleDtbs;
		const size_t textSize = std::min(static_cast<size_t>(bytesRead), bufferSize - 1);
		std::string lines(reinterpret_cast<char*>(bytes.data()), textSize);
		std::istringstream iss(lines);
		std::string line;

		while (std::getline(iss, line)) {
			Info info{};
			std::istringstream info_ss(line);
			if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id >> std::hex >> info.dtb >> info.kernelAddr >> info.name) {
				if (info.process_id == 0)
					possibleDtbs.push_back(info.dtb);
				if (AttachProcessName.find(info.name) != std::string::npos)
					possibleDtbs.push_back(info.dtb);
			}
		}

		LOG_INFO("ProcessMgr", "FixCr3: {} candidate DTBs to try", possibleDtbs.size());

		// Try each candidate DTB
		for (size_t i = 0; i < possibleDtbs.size(); i++) {
			auto dtb = possibleDtbs[i];
			LOG_DEBUG("ProcessMgr", "FixCr3: trying DTB 0x{:X} ({}/{})", dtb, i + 1, possibleDtbs.size());
			VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_PROCESS_DTB | ProcessID, dtb);
			VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
			if (CanAccessProbeModules(ProcessID, AttachProcessName)) {
				LOG_INFO("ProcessMgr", "FixCr3: DTB 0x{:X} works, modules accessible", dtb);
				return true;
			}
		}

		// All candidates failed — reset DTB to avoid leaving stale state
		LOG_WARNING("ProcessMgr", "FixCr3: no valid DTB found among {} candidates", possibleDtbs.size());
		VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_PROCESS_DTB | ProcessID, 0);
		VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_ALL, 1);
		return false;
	}



	bool IsProcessAlive()

	{

		if (!this->HANDLE || ProcessID == 0) return false;

		VMMDLL_ConfigSet(this->HANDLE, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1);

		VMMDLL_PROCESS_INFORMATION info = { 0 };

		info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;

		info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

		SIZE_T cbInfo = sizeof(info);

		if (!VMMDLL_ProcessGetInformation(this->HANDLE, ProcessID, &info, &cbInfo))

			return false;

		return strcmp(info.szName, AttachProcessName.c_str()) == 0;

	}



	void Detach()

	{

		ProcessID = 0;

		Attached = false;

	}



	void CloseDMA()

	{

		if (this->HANDLE) {

			VMMDLL_Close(this->HANDLE);

			this->HANDLE = nullptr;

		}

		ProcessID = 0;

		Attached = false;

	}



	template <typename ReadType>

	bool ReadMemory(DWORD64 Address, ReadType& Value, int Size)

	{

		_is_invalid(ProcessID, false);

		if (VMMDLL_MemReadEx(this->HANDLE, ProcessID, Address, (PBYTE)&Value, Size, 0, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING_IO))

			return true;

		return false;

	}



	template <typename ReadType>

	bool ReadMemory(DWORD64 Address, ReadType& Value)

	{

		_is_invalid(ProcessID, false);



		if (VMMDLL_MemReadEx(this->HANDLE, ProcessID, Address, (PBYTE)&Value, sizeof(ReadType), 0, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING_IO))

			return true;

		return false;

	}



	VMMDLL_SCATTER_HANDLE CreateScatterHandle()

	{

		return VMMDLL_Scatter_Initialize(this->HANDLE, ProcessID, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO);

	}



	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)

	{

		if (VMMDLL_Scatter_PrepareEx(handle, address, size, (PBYTE)buffer, NULL))

			SCATTER_PENDING_COUNT++;

		else

			SCATTER_PREPARE_FAIL_COUNT++;

	}

	bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle)

	{

		if (!handle)

			return false;

		const int pending = SCATTER_PENDING_COUNT;

		const int prepareFails = SCATTER_PREPARE_FAIL_COUNT;

		SCATTER_PENDING_COUNT = 0;

		SCATTER_PREPARE_FAIL_COUNT = 0;

		if (pending == 0)

		{

			VMMDLL_Scatter_Clear(handle, ProcessID, NULL);

			return prepareFails == 0;

		}

		for (int attempt = 0; attempt < 3; ++attempt)

		{

			if (VMMDLL_Scatter_ExecuteRead(handle))

			{

				VMMDLL_Scatter_Clear(handle, ProcessID, NULL);

				g_dmaHealth.RecordSuccess();

				return true;

			}

			Sleep(1);

		}

		VMMDLL_Scatter_Clear(handle, ProcessID, NULL);

		g_dmaHealth.RecordFailure();

		return false;

	}



	DWORD64 TraceAddress(DWORD64 BaseAddress, std::vector<DWORD> Offsets)

	{

		_is_invalid(ProcessID, 0);

		DWORD64 Address = 0;



		if (Offsets.size() == 0)

			return BaseAddress;



		if (!ReadMemory<DWORD64>(BaseAddress, Address))

			return 0;



		for (int i = 0; i < Offsets.size() - 1; i++)

		{

			if (!ReadMemory<DWORD64>(Address + Offsets[i], Address))

				return 0;

		}

		return Address == 0 ? 0 : Address + Offsets[Offsets.size() - 1];

	}



	template <typename T>

	bool ReadMemoryExtra(uintptr_t address, DWORD pid, T& outBuffer, bool cache = false, const DWORD size = sizeof(T))

	{

		DWORD bytes_read = 0;

		BOOL ok;

		if (!cache)

			ok = VMMDLL_MemReadEx(this->HANDLE, pid, address, (PBYTE)&outBuffer, size, &bytes_read, VMMDLL_FLAG_NOCACHE);

		else

			ok = VMMDLL_MemReadEx(this->HANDLE, pid, address, (PBYTE)&outBuffer, size, &bytes_read, VMMDLL_FLAG_CACHE_RECENT_ONLY);

		return ok == TRUE;

	}



	DWORD GetProcID_Keys(LPSTR proc_name)

	{

		DWORD procID = 0;

		VMMDLL_PidGetFromName(this->HANDLE, (LPSTR)proc_name, &procID);

		return procID;

	}



	std::vector<int> GetPidListFromName(std::string name)

	{

		PVMMDLL_PROCESS_INFORMATION process_info = NULL;

		DWORD total_processes = 0;

		std::vector<int> list = { };



		if (!VMMDLL_ProcessGetInformationAll(this->HANDLE, &process_info, &total_processes))

		{

			return list;

		}



		for (size_t i = 0; i < total_processes; i++)

		{

			auto process = process_info[i];

			if (strstr(process.szNameLong, name.c_str()))

				list.push_back(process.dwPID);

		}



		return list;

	}



	enum class e_registry_type

	{

		none = REG_NONE,

		sz = REG_SZ,

		expand_sz = REG_EXPAND_SZ,

		binary = REG_BINARY,

		dword = REG_DWORD,

		dword_little_endian = REG_DWORD_LITTLE_ENDIAN,

		dword_big_endian = REG_DWORD_BIG_ENDIAN,

		link = REG_LINK,

		multi_sz = REG_MULTI_SZ,

		resource_list = REG_RESOURCE_LIST,

		full_resource_descriptor = REG_FULL_RESOURCE_DESCRIPTOR,

		resource_requirements_list = REG_RESOURCE_REQUIREMENTS_LIST,

		qword = REG_QWORD,

		qword_little_endian = REG_QWORD_LITTLE_ENDIAN

	};



	std::string QueryValue(const char* path, e_registry_type type)

	{

		BYTE buffer[0x128];

		DWORD _type = static_cast<DWORD>(type);

		DWORD size = sizeof(buffer);



		if (!VMMDLL_WinReg_QueryValueExU(this->HANDLE, const_cast<LPSTR>(path), &_type, buffer, &size))

		{

			return "";

		}



		std::wstring wstr = std::wstring(reinterpret_cast<wchar_t*>(buffer));

		return std::string(wstr.begin(), wstr.end());

	}



	// Signature scan in a kernel module's code section via DMA.
	// pattern: space-separated hex bytes, "??" = wildcard (e.g. "48 8B 05 ?? ?? ?? ?? 48 8B 04 C8")
	// Returns the virtual address of the first match, or 0 on failure.
	uintptr_t SigScan(DWORD pid, uintptr_t moduleBase, DWORD moduleSize, const char* pattern)
	{
		if (!moduleBase || !moduleSize || !pattern || !*pattern)
			return 0;

		// Parse pattern into byte array + mask
		struct PatByte { uint8_t val; bool wildcard; };
		std::vector<PatByte> patBytes;
		const char* p = pattern;
		while (*p) {
			while (*p == ' ') p++;
			if (!*p) break;
			if (*p == '?') {
				patBytes.push_back({0, true});
				p++; if (*p == '?') p++;
			} else {
				char hex[3] = {p[0], p[1], 0};
				patBytes.push_back({(uint8_t)strtoul(hex, nullptr, 16), false});
				p += 2;
			}
		}
		if (patBytes.empty()) return 0;

		// Read the entire module code section
		std::vector<uint8_t> buf(moduleSize, 0);
		DWORD bytesRead = 0;
		if (!VMMDLL_MemReadEx(this->HANDLE, pid, moduleBase, buf.data(), moduleSize, &bytesRead, VMMDLL_FLAG_NOCACHE))
			return 0;

		// Scan for pattern
		for (size_t i = 0; i + patBytes.size() <= bytesRead; i++) {
			bool match = true;
			for (size_t j = 0; j < patBytes.size(); j++) {
				if (!patBytes[j].wildcard && buf[i + j] != patBytes[j].val) {
					match = false;
					break;
				}
			}
			if (match)
				return moduleBase + i;
		}
		return 0;
	}

	// Find gSessionGlobalSlots address by scanning win32ksgd.sys / win32k.sys
	// Pattern: mov rax, [rip+disp]; ...  (48 8B 05 ?? ?? ?? ?? ...)
	// Returns the resolved VA of gSessionGlobalSlots, or 0 on failure.
	uintptr_t FindSessionGlobalSlots(DWORD csrssPid)
	{
		LPWSTR moduleNames[] = { (LPWSTR)L"win32ksgd.sys", (LPWSTR)L"win32k.sys" };

		for (auto modName : moduleNames) {
			PVMMDLL_MAP_MODULEENTRY pModuleEntry = nullptr;

			if (!VMMDLL_Map_GetModuleFromNameW(this->HANDLE, csrssPid, modName, &pModuleEntry, 0))
				continue;

			if (!pModuleEntry) {
				continue;
			}

			uintptr_t base = pModuleEntry->vaBase;
			DWORD size = pModuleEntry->cbImageSize;
			VMMDLL_MemFree(pModuleEntry);

			if (!base || !size) continue;

			// Pattern 1: mov rax, [rip+xxx]; mov rax, [rax+rcx*8]
			uintptr_t addr = SigScan(csrssPid, base, size, "48 8B 05 ?? ?? ?? ?? 48 8B 04 C8");
			// Pattern 2: mov rax, [rip+xxx]; dec ecx
			if (!addr)
				addr = SigScan(csrssPid, base, size, "48 8B 05 ?? ?? ?? ?? FF C9");

			if (addr) {
				// Read the 32-bit RIP-relative displacement (at offset +3 from instruction start)
				int32_t disp = 0;
				DWORD cbRead = 0;
				if (!VMMDLL_MemReadEx(this->HANDLE, csrssPid, addr + 3, (PBYTE)&disp, 4, &cbRead, VMMDLL_FLAG_NOCACHE))
					continue;

				// RIP-relative: target = instruction_end + disp = addr + 7 + disp
				uintptr_t result = addr + 7 + disp;
				LOG_INFO("Input", "SigScan: gSessionGlobalSlots found via '{}' at 0x{:X} (csrss pid {})",
					wcscmp(modName, L"win32ksgd.sys") == 0 ? "win32ksgd.sys" : "win32k.sys", result, csrssPid);
				return result;
			}
		}
		return 0;
	}

	// Find gafAsyncKeyState offset within tagWINDOWSTATION by scanning win32kbase.sys
	// Pattern: lea rdx, [rax+offset]; call ...; xorps xmm0,xmm0
	// (48 8D 90 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 57 C0)
	// Returns the offset, or 0 on failure.
	DWORD FindGafAsyncKeyStateOffset(DWORD csrssPid, uintptr_t userSessionState)
	{
		PVMMDLL_MAP_MODULEENTRY pModuleEntry = nullptr;

		if (!VMMDLL_Map_GetModuleFromNameW(this->HANDLE, csrssPid, (LPWSTR)L"win32kbase.sys", &pModuleEntry, 0))
			return 0;

		if (!pModuleEntry) {
			return 0;
		}

		uintptr_t base = pModuleEntry->vaBase;
		DWORD size = pModuleEntry->cbImageSize;
		VMMDLL_MemFree(pModuleEntry);

		if (!base || !size) return 0;

		// lea rdx, [rax+disp32]; call rel32; xorps xmm0, xmm0
		uintptr_t addr = SigScan(csrssPid, base, size, "48 8D 90 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 57 C0");
		if (!addr) {
			LOG_DEBUG("Input", "SigScan: LEA signature not found in win32kbase.sys (csrss pid {})", csrssPid);
			return 0;
		}

		// Read the 32-bit displacement from lea rdx, [rax+disp32] (at offset +3)
		int32_t disp = 0;
		DWORD cbRead = 0;
		if (!VMMDLL_MemReadEx(this->HANDLE, csrssPid, addr + 3, (PBYTE)&disp, 4, &cbRead, VMMDLL_FLAG_NOCACHE))
			return 0;

		uintptr_t resultAddr = userSessionState + disp;
		if (resultAddr > 0x7FFFFFFFFFFF) {
			LOG_INFO("Input", "SigScan: gafAsyncKeyState offset = 0x{:X} (LEA at 0x{:X}, csrss pid {})",
				disp, addr, csrssPid);
			return (DWORD)disp;
		}

		LOG_DEBUG("Input", "SigScan: calculated address 0x{:X} not in kernel high VA", resultAddr);
		return 0;
	}

	void init_keystates() {

		std::string win = QueryValue("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\CurrentBuild", e_registry_type::sz);

		int Winver = 0;

		if (!win.empty())

			Winver = std::stoi(win);



		LOG_INFO("Input", "Windows build: {} ({})", win, Winver > 22000 ? "Win11 path" : "legacy path");

		this->win_logon_pid = GetProcID_Keys((LPSTR)"winlogon.exe");



		if (Winver > 22000) {

			auto pids = GetPidListFromName("csrss.exe");

			bool found = false;



			// ============================================================

			// Strategy 1: Signature scan (version-independent, no PDB needed)

			// Scan win32ksgd.sys/win32k.sys for gSessionGlobalSlots,

			// then scan win32kbase.sys for gafAsyncKeyState offset.

			// ============================================================

			for (size_t i = 0; i < pids.size() && !found; i++) {

				auto pid = pids[i];

				uintptr_t g_slots_va = FindSessionGlobalSlots(pid);

				if (!g_slots_va) continue;



				// Three-level pointer dereference: gSessionGlobalSlots -> [0] -> [session_idx] -> user_session_state

				uintptr_t user_session_state = 0;

				for (int slot = 0; slot < 8 && !found; slot++) {

					uintptr_t slot_ptr = 0;

					if (!ReadMemoryExtra<uintptr_t>(g_slots_va, pid, slot_ptr) || !slot_ptr) continue;

					uintptr_t entry = 0;

					if (!ReadMemoryExtra<uintptr_t>(slot_ptr + 8 * slot, pid, entry) || !entry) continue;

					user_session_state = 0;

					if (!ReadMemoryExtra<uintptr_t>(entry, pid, user_session_state) || !user_session_state || user_session_state < 0x7FFFFFFFFFFF) continue;



					// Find gafAsyncKeyState offset via LEA signature in win32kbase.sys

					DWORD gafOffset = FindGafAsyncKeyStateOffset(pid, user_session_state);

					if (gafOffset) {

						gafAsyncKeyStateExport = user_session_state + gafOffset;

						if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF) {

							LOG_INFO("Input", "Keys init OK [SigScan]: csrss pid {}, addr 0x{:X}, gafOffset 0x{:X}",

								pid, gafAsyncKeyStateExport, gafOffset);

							found = true;

							break;

						}

					}

				}

			}



			// ============================================================

			// Strategy 2: PDB symbol resolution (requires symbol server access)

			// ============================================================

			if (!found) {

				LOG_INFO("Input", "SigScan failed, trying PDB resolution...");

				for (size_t i = 0; i < pids.size() && !found; i++) {

					auto pid = pids[i];

					uintptr_t win32ksgd_base = VMMDLL_ProcessGetModuleBaseU(this->HANDLE, pid, const_cast<LPSTR>("win32ksgd.sys"));

					if (win32ksgd_base == 0) continue;



					char szModuleName[MAX_PATH] = { 0 };

					if (!VMMDLL_PdbLoad(this->HANDLE, pid, win32ksgd_base, szModuleName)) {

						LOG_DEBUG("Input", "PDB: failed to load win32ksgd.sys symbols (csrss pid {})", pid);

						continue;

					}



					ULONG64 g_slots_va = 0;

					if (!VMMDLL_PdbSymbolAddress(this->HANDLE, szModuleName,

							const_cast<LPSTR>("gSessionGlobalSlots"), &g_slots_va) || !g_slots_va) {

						LOG_DEBUG("Input", "PDB: gSessionGlobalSlots not found in {} (csrss pid {})", szModuleName, pid);

						continue;

					}

					LOG_DEBUG("Input", "PDB: gSessionGlobalSlots = 0x{:X} (csrss pid {})", g_slots_va, pid);



					uintptr_t tmp1 = 0, tmp2 = 0, user_session_state = 0;

					ReadMemoryExtra<uintptr_t>(g_slots_va, pid, tmp1);

					ReadMemoryExtra<uintptr_t>(tmp1, pid, tmp2);

					ReadMemoryExtra<uintptr_t>(tmp2, pid, user_session_state);



					if (user_session_state == 0 || user_session_state < 0x7FFFFFFFFFFF) {

						LOG_DEBUG("Input", "PDB: user_session_state invalid (0x{:X})", user_session_state);

						continue;

					}



					// Try resolving gafAsyncKeyState field offset from win32kbase.sys PDB

					uintptr_t win32kbase_base = VMMDLL_ProcessGetModuleBaseU(this->HANDLE, pid, const_cast<LPSTR>("win32kbase.sys"));

					if (win32kbase_base) {

						char szWin32kBase[MAX_PATH] = { 0 };

						if (VMMDLL_PdbLoad(this->HANDLE, pid, win32kbase_base, szWin32kBase)) {

							DWORD gafOffset = 0;

							if (VMMDLL_PdbTypeChildOffset(this->HANDLE, szWin32kBase,

									const_cast<LPSTR>("tagWINDOWSTATION"),

									const_cast<LPSTR>("gafAsyncKeyState"), &gafOffset) && gafOffset > 0) {

								gafAsyncKeyStateExport = user_session_state + gafOffset;

								if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF) {

									LOG_INFO("Input", "Keys init OK [PDB]: csrss pid {}, addr 0x{:X}, gafOffset 0x{:X}",

										pid, gafAsyncKeyStateExport, gafOffset);

									found = true;

									break;

								}

							}

						}

					}



					// PDB type resolution for gafAsyncKeyState failed, try known offsets

					const uintptr_t gaf_offsets[] = { 0x3690, 0x36A8 };

					for (auto offset : gaf_offsets) {

						gafAsyncKeyStateExport = user_session_state + offset;

						if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF) {

							LOG_INFO("Input", "Keys init OK [PDB+offset]: csrss pid {}, addr 0x{:X}, gafOffset 0x{:X}",

								pid, gafAsyncKeyStateExport, offset);

							found = true;

							break;

						}

					}

				}

			}



			// ============================================================

			// Strategy 3: Hardcoded offset table (last resort fallback)

			// ============================================================

			if (!found) {

				LOG_INFO("Input", "PDB failed, trying hardcoded offset table...");

				struct Win11Offsets { uintptr_t session_global_slots; uintptr_t gaf_async_key_state; };

				const Win11Offsets offset_table[] = {

					{ 0x3110, 0x3690 },  // Win11 21H2 ~ 23H2

					{ 0x3148, 0x36A8 },  // Win11 24H2+ (26100+)

				};



				for (const auto& offsets : offset_table) {

					for (size_t i = 0; i < pids.size(); i++)

					{

						auto pid = pids[i];

						uintptr_t tmp = VMMDLL_ProcessGetModuleBaseU(this->HANDLE, pid, const_cast<LPSTR>("win32ksgd.sys"));

						if (tmp == 0) continue;

						uintptr_t g_session_global_slots = tmp + offsets.session_global_slots;

						uintptr_t tmp1 = 0, tmp2 = 0, user_session_state = 0;

						ReadMemoryExtra<uintptr_t>(g_session_global_slots, pid, tmp1);

						ReadMemoryExtra<uintptr_t>(tmp1, pid, tmp2);

						ReadMemoryExtra<uintptr_t>(tmp2, pid, user_session_state);

						gafAsyncKeyStateExport = user_session_state + offsets.gaf_async_key_state;

						if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF) {

							LOG_INFO("Input", "Keys init OK [hardcoded]: offsets [0x{:X}, 0x{:X}], csrss pid {}, addr 0x{:X}",

								offsets.session_global_slots, offsets.gaf_async_key_state, pid, gafAsyncKeyStateExport);

							found = true;

							break;

						}

					}

					if (found) break;

				}

			}



			if (!found)

				LOG_ERROR("Input", "Keys init failed: gafAsyncKeyStateExport invalid (build {}). "

					"Offsets may be outdated for this Windows version. Falling back to local GetAsyncKeyState.", Winver);

		} else {

			// Win10: EAT export -> PDB fallback

			PVMMDLL_MAP_EAT eat_map = NULL;

			PVMMDLL_MAP_EATENTRY eat_map_entry;

			DWORD kernelPid = GetProcID_Keys((LPSTR)"winlogon.exe") | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;

			bool result = VMMDLL_Map_GetEATU(this->HANDLE, kernelPid, (LPSTR)"win32kbase.sys", &eat_map);

			if (result && eat_map) {

				for (int i = 0; i < eat_map->cMap; i++)

				{

					eat_map_entry = eat_map->pMap + i;

					if (strcmp(eat_map_entry->uszFunction, "gafAsyncKeyState") == 0)

					{

						gafAsyncKeyStateExport = eat_map_entry->vaFunction;

						break;

					}

				}

				VMMDLL_MemFree(eat_map);

				eat_map = NULL;

			}



			// EAT failed, try PDB on Win10

			if (gafAsyncKeyStateExport == 0) {

				LOG_INFO("Input", "EAT failed on Win10, trying PDB...");

				auto pids = GetPidListFromName("csrss.exe");

				for (size_t i = 0; i < pids.size(); i++) {

					auto pid = pids[i];

					uintptr_t win32kbase_base = VMMDLL_ProcessGetModuleBaseU(this->HANDLE, pid, const_cast<LPSTR>("win32kbase.sys"));

					if (!win32kbase_base) continue;



					char szModuleName[MAX_PATH] = { 0 };

					if (!VMMDLL_PdbLoad(this->HANDLE, pid, win32kbase_base, szModuleName))

						continue;



					ULONG64 gaf_va = 0;

					if (VMMDLL_PdbSymbolAddress(this->HANDLE, szModuleName,

							const_cast<LPSTR>("gafAsyncKeyState"), &gaf_va) && gaf_va > 0x7FFFFFFFFFFF) {

						gafAsyncKeyStateExport = gaf_va;

						LOG_INFO("Input", "Keys init OK [PDB Win10]: csrss pid {}, addr 0x{:X}", pid, gafAsyncKeyStateExport);

						break;

					}

				}

			}

		}

	}



	void update_key_state_bitmap() {

		uint8_t previous_key_state_bitmap[64] = { 0 };

		memcpy(previous_key_state_bitmap, state_bitmap, 64);



		VMMDLL_MemReadEx(this->HANDLE, this->win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, gafAsyncKeyStateExport, (PBYTE)&state_bitmap, 64, NULL, VMMDLL_FLAG_NOCACHE);

		for (int vk = 0; vk < 256; ++vk)

			if ((state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2) && !(previous_key_state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2))

				previous_state_bitmap[vk / 8] |= 1 << vk % 8;

	}



	bool is_key_down(uint32_t virtual_key_code) {

		// Fallback to Windows API if DMA keyboard state not initialized

		if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF) {

			return (GetAsyncKeyState(virtual_key_code) & 0x8000) != 0;

		}

		if (std::chrono::system_clock::now() - start > std::chrono::milliseconds(50))

		{

			update_key_state_bitmap();

			start = std::chrono::system_clock::now();

		}

		return state_bitmap[(virtual_key_code * 2 / 8)] & 1 << virtual_key_code % 4 * 2;

	}

};



inline ProcessManager ProcessMgr;